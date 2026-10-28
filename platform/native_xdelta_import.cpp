/* xdelta track importer: see include/platform/native_xdelta_import.h.

   One pass over the patch, in target order:
   1. the decoder streams the patched disc as raw 2352-byte sectors;
   2. the ISO 9660 directories and the BIGFILE table are read as their
      sectors go past (a second pass runs when one sits after its data);
   3. every BIGFILE entry is compared with the same entry of the player's
      disc while it streams; changed entries that can be a LEV or VRM go to a
      scratch file, everything else is dropped;
   4. each changed LEV is paired with the VRM before it, cut (cutter version
      1), built into a package and run through the package verifier, which
      decides race, arena or other. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include <platform/native_xdelta_import.h>
#include <platform/native_xdelta_decode.h>
#include <platform/native_custom_package.h>
#include <platform/native_custom_content_verify.h>
#include <platform/native_custom_offline.h>
#include <platform/native_sha256.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

using nlohmann::json;

namespace {

const unsigned int kRawSector = 2352;
const unsigned int kUserData = 24; /* 12 sync + 4 header + 8 subheader (Mode 2 Form 1). */
const unsigned int kSector = 2048;
const size_t kEntryMax = 16u * 1024u * 1024u; /* The package LEV limit; larger entries are never tracks. */
const char *kRetailDiscSha256 = "f780bf2331476aabfc00772fa758b12dd95ebfbc907968132cbd3cdd4e2c07c0";
const size_t kDescriptorMax = 256 * 1024;
const size_t kActiveMax = 8;
const size_t kDirBytesMax = 8u * 1024u * 1024u;       /* Directory bytes collected at once. */
const long long kScratchBytesMax = 512ll * 1024 * 1024; /* Changed entries kept on disk. */                         /* Overlapping BIGFILE entries held at once. */
const size_t kOfferedBytesMax = 192u * 1024u * 1024u; /* Package bytes held for one review list. */

std::string hex_sha256(const void *bytes, size_t size)
{
    NativeSha256Ctx context;
    unsigned char digest[32];
    char text[65];
    NativeSha256_Init(&context);
    NativeSha256_Update(&context, bytes, size);
    NativeSha256_Final(&context, digest);
    NativeSha256_ToHex(digest, text);
    return text;
}

uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* SHA-1, only for the UUIDv5 name hash (not a security use). */
void sha1(const unsigned char *data, size_t size, unsigned char out[20])
{
    uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    std::vector<unsigned char> m(data, data + size);
    const unsigned long long bits = (unsigned long long)size * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; i--) m.push_back((unsigned char)(bits >> (i * 8)));
    auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (size_t block = 0; block < m.size(); block += 64)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)m[block + i * 4] << 24 | m[block + i * 4 + 1] << 16 | m[block + i * 4 + 2] << 8 | m[block + i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++)
        {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5a827999u;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ed9eba1u;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8f1bbcdcu;
            else f = b ^ c ^ d, k = 0xca62c1d6u;
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++) out[i * 4 + j] = (unsigned char)(h[i] >> (24 - j * 8));
}

int seek64(FILE *file, unsigned long long offset)
{
#ifdef _WIN32
    return _fseeki64(file, (long long)offset, SEEK_SET);
#else
    return fseeko(file, (off_t)offset, SEEK_SET);
#endif
}

unsigned long long file_size(FILE *file)
{
#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_END)) return 0;
    long long size = _ftelli64(file);
#else
    if (fseeko(file, 0, SEEK_END)) return 0;
    long long size = (long long)ftello(file);
#endif
    std::rewind(file);
    return size < 0 ? 0 : (unsigned long long)size;
}

/* UTF-8 paths on every platform (the file dialog hands over UTF-8). */
#ifdef _WIN32
std::wstring wide(const std::string &text)
{
    if (text.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
    if (size <= 0) return std::wstring();
    std::wstring result((size_t)size, 0);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), &result[0], size) != size) return std::wstring();
    return result;
}
std::string narrow(const wchar_t *text)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return std::string();
    std::string result((size_t)size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], size, nullptr, nullptr);
    return result;
}
FILE *fs_open(const std::string &path, const char *mode)
{
    std::wstring p = wide(path), m = wide(mode);
    return p.empty() ? nullptr : _wfopen(p.c_str(), m.c_str());
}
bool fs_mkdir(const std::string &path) { std::wstring p = wide(path); return !p.empty() && !_wmkdir(p.c_str()); }
void fs_rmdir(const std::string &path) { std::wstring p = wide(path); if (!p.empty()) _wrmdir(p.c_str()); }
void fs_remove(const std::string &path) { std::wstring p = wide(path); if (!p.empty()) _wremove(p.c_str()); }
bool fs_stat(const std::string &path, long long *size, long long *mtime)
{
    struct _stat64 info;
    std::wstring p = wide(path);
    if (p.empty() || _wstat64(p.c_str(), &info)) return false;
    *size = (long long)info.st_size; *mtime = (long long)info.st_mtime;
    return true;
}
#else
FILE *fs_open(const std::string &path, const char *mode) { return std::fopen(path.c_str(), mode); }
bool fs_mkdir(const std::string &path) { return !mkdir(path.c_str(), 0700); }
void fs_rmdir(const std::string &path) { rmdir(path.c_str()); }
void fs_remove(const std::string &path) { std::remove(path.c_str()); }
bool fs_stat(const std::string &path, long long *size, long long *mtime)
{
    struct stat info;
    if (stat(path.c_str(), &info)) return false;
    *size = (long long)info.st_size; *mtime = (long long)info.st_mtime;
    return true;
}
#endif

/* Flags shared with the main thread. */
int load_flag(const volatile int *flag) { return flag ? __atomic_load_n((const int *)flag, __ATOMIC_ACQUIRE) : 0; }
void store_flag(volatile int *flag, int value) { if (flag) __atomic_store_n((int *)flag, value, __ATOMIC_RELEASE); }

struct FileCloser { void operator()(FILE *f) const { if (f) std::fclose(f); } };
using File = std::unique_ptr<FILE, FileCloser>;

File open_read(const std::string &path)
{
    File file(fs_open(path, "rb"));
    if (!file) throw std::runtime_error("Cannot open " + path.substr(path.find_last_of("/\\") + 1));
    return file;
}

struct IsoFile { uint32_t lba = 0, size = 0; };

/* Random access to the player's disc. */
struct Disc
{
    File file;
    unsigned long long bytes = 0;
    std::map<std::string, IsoFile> files;
    std::vector<std::pair<uint32_t, uint32_t>> bigfile; /* (offset in sectors, bytes) */
    uint32_t bigfileLba = 0;

    explicit Disc(const std::string &path) : file(open_read(path)), bytes(file_size(file.get())) {}
    bool raw(uint32_t lba, unsigned char *out)
    {
        return (unsigned long long)(lba + 1) * kRawSector <= bytes && !seek64(file.get(), (unsigned long long)lba * kRawSector) &&
               std::fread(out, 1, kRawSector, file.get()) == kRawSector;
    }
    bool sector(uint32_t lba, unsigned char *out)
    {
        unsigned char rawBytes[kRawSector];
        if (!raw(lba, rawBytes)) return false;
        std::memcpy(out, rawBytes + kUserData, kSector);
        return true;
    }
    std::vector<unsigned char> extent(uint32_t lba, uint32_t size)
    {
        std::vector<unsigned char> out((size_t)(size + kSector - 1) / kSector * kSector);
        for (uint32_t i = 0; i * kSector < size; i++)
            if (!sector(lba + i, out.data() + (size_t)i * kSector)) throw std::runtime_error("Cannot read the disc");
        out.resize(size);
        return out;
    }
    void walk_dir(uint32_t lba, uint32_t size, const std::string &prefix, int depth)
    {
        if (depth > 8 || size > 1024 * 1024) throw std::runtime_error("Disc directory is not ISO 9660");
        auto data = extent(lba, size);
        for (size_t i = 0; i < data.size();)
        {
            unsigned ln = data[i];
            if (!ln) { i = (i / kSector + 1) * kSector; continue; }
            if (i + ln > data.size() || ln < 34) break;
            const unsigned char *rec = &data[i];
            unsigned nl = rec[32];
            if (33u + nl > ln) break;
            std::string name(reinterpret_cast<const char *>(rec + 33), nl);
            i += ln;
            if (name == std::string(1, '\0') || name == std::string(1, '\1')) continue;
            name = name.substr(0, name.find(';'));
            if (rec[25] & 2) walk_dir(rd32(rec + 2), rd32(rec + 10), prefix + "/" + name, depth + 1);
            else files[prefix + "/" + name] = {rd32(rec + 2), rd32(rec + 10)};
        }
    }
    void walk()
    {
        unsigned char pvd[kSector];
        if (!sector(16, pvd) || std::memcmp(pvd + 1, "CD001", 5)) throw std::runtime_error("Disc is not a raw MODE2/2352 image");
        walk_dir(rd32(pvd + 156 + 2), rd32(pvd + 156 + 10), "", 0);
        auto it = files.find("/BIGFILE.BIG");
        if (it == files.end()) throw std::runtime_error("Disc has no BIGFILE.BIG");
        bigfileLba = it->second.lba;
        unsigned char head[kSector];
        if (!sector(bigfileLba, head)) throw std::runtime_error("Cannot read the disc");
        uint32_t n = rd32(head + 4);
        if (n > 4096 || 8 + 8 * (size_t)n > it->second.size) throw std::runtime_error("Disc BIGFILE table is damaged");
        auto table = extent(bigfileLba, 8 + 8 * n);
        for (uint32_t i = 0; i < n; i++) bigfile.push_back({rd32(&table[8 + 8 * i]), rd32(&table[12 + 8 * i])});
    }
    /* Bytes of one retail BIGFILE entry. */
    std::vector<unsigned char> entry(uint32_t index)
    {
        if (index >= bigfile.size()) return {};
        return extent(bigfileLba + bigfile[index].first, bigfile[index].second);
    }
};

std::string hash_file(const std::string &path, unsigned long long *bytes, const volatile int *cancel)
{
    File file = open_read(path);
    NativeSha256Ctx context;
    NativeSha256_Init(&context);
    std::vector<unsigned char> buffer(1 << 20);
    unsigned long long total = 0;
    size_t got;
    while ((got = std::fread(buffer.data(), 1, buffer.size(), file.get())) > 0)
    {
        if (load_flag(cancel)) throw std::runtime_error("Cancelled");
        NativeSha256_Update(&context, buffer.data(), got);
        total += got;
    }
    if (std::ferror(file.get())) throw std::runtime_error("Cannot read " + path.substr(path.find_last_of("/\\") + 1));
    unsigned char digest[32];
    char text[65];
    NativeSha256_Final(&context, digest);
    NativeSha256_ToHex(digest, text);
    if (bytes) *bytes = total;
    return text;
}

/* ---- Cutter version 1 (the 2026-09-28 and 2026-10-15 research scripts). */
size_t cut_lev(const unsigned char *p, size_t size)
{
    if (size < 0x200) return 0;
    const uint32_t map = rd32(p);
    if (map & 3 || map < 0x190 || (unsigned long long)map + 8 > size) return 0;
    const uint32_t mapBytes = rd32(p + 4 + map);
    if (mapBytes & 3) return 0;
    const unsigned long long end = 4ull + map + 4 + mapBytes;
    return end <= size ? (size_t)end : 0;
}

size_t cut_vrm(const unsigned char *p, size_t size)
{
    if (size < 20) return 0;
    if (rd32(p) != 0x20)
    {
        const unsigned long long end = 20ull + (unsigned long long)rd16(p + 16) * rd16(p + 18) * 2;
        return end <= size && end > 20 ? (size_t)end : 0;
    }
    unsigned long long at = 4;
    for (int images = 0; images < 64; images++)
    {
        if (at + 4 > size) return 0;
        const uint32_t z = rd32(p + at);
        at += 4;
        if (!z) return images ? (size_t)at : 0;
        if (at + z > size) return 0;
        at += z;
    }
    return 0;
}

/* ---- Pack descriptor (<patch stem>.ctrpack.json), reviewed offline. */
struct TrackMeta
{
    std::string title, author, version, trackKey;
    unsigned int laps = 0;
    bool relic = false;
    int ticks[3] = {0, 0, 0};
    bool excludeRelic = false, excludeTokens = false;
};
struct Descriptor
{
    std::string sha256, packTitle, packAuthor, sourceUrl;
    std::map<int, TrackMeta> tracks;
};

std::string bounded_text(const json &value, const char *field, size_t max)
{
    if (!value.is_string()) throw std::runtime_error(std::string(field) + " must be text");
    auto text = value.get<std::string>();
    if (text.empty() || text.size() > max || std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error(std::string(field) + " is empty, too long or has control characters");
    return text;
}

void closed(const json &object, std::initializer_list<const char *> allowed, const char *where)
{
    if (!object.is_object()) throw std::runtime_error(std::string(where) + " must be an object");
    for (auto it = object.begin(); it != object.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char *a) { return it.key() == a; }))
            throw std::runtime_error(std::string(where) + " has unknown field " + it.key().substr(0, 32));
}

/* A registry track key: [a-z0-9]+(-[a-z0-9]+)*, at most 64 bytes. It names a
   folder under assets/tracks, so nothing else is ever accepted. */
bool track_key_valid(const std::string &key)
{
    if (key.empty() || key.size() > 64 || key.front() == '-' || key.back() == '-') return false;
    for (size_t i = 0; i < key.size(); i++)
    {
        const char c = key[i];
        if (c == '-') { if (key[i - 1] == '-') return false; }
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

/* The registry's key rule (AP-Pie ctr_registry.rules.slugify_key, without
   its clash suffix): NFKD, ASCII only, lowercase, every run of anything else
   one '-', cut to 48, "track" when nothing is left. */
std::string title_track_key(const std::string &title)
{
    char folded[641];
    if (!CustomPackage_AsciiFold(title.data(), title.size(), folded, sizeof folded)) return "";
    std::string key;
    bool dash = false;
    for (const char *p = folded; *p; p++)
    {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            if (dash && !key.empty()) key += '-';
            key += c;
            dash = false;
        }
        else dash = true;
    }
    if (key.size() > 48) key.resize(48);
    while (!key.empty() && key.back() == '-') key.pop_back();
    return key.empty() ? "track" : key;
}

Descriptor parse_descriptor(const std::vector<unsigned char> &bytes, const std::string &patchSha256)
{
    Descriptor d;
    d.sha256 = hex_sha256(bytes.data(), bytes.size());
    std::set<std::string> keys;
    const json doc = json::parse(bytes.begin(), bytes.end());
    closed(doc, {"descriptor_version", "patch_sha256", "pack_title", "pack_author", "source_url", "tracks"}, "Descriptor");
    if (!doc.contains("descriptor_version") || doc["descriptor_version"] != 1) throw std::runtime_error("Unsupported descriptor version");
    if (!doc.contains("patch_sha256") || !doc["patch_sha256"].is_string() || doc["patch_sha256"].get<std::string>() != patchSha256)
        throw std::runtime_error("Descriptor is for another patch");
    if (doc.contains("pack_title")) d.packTitle = bounded_text(doc["pack_title"], "pack_title", 160);
    if (doc.contains("pack_author")) d.packAuthor = bounded_text(doc["pack_author"], "pack_author", 80);
    if (doc.contains("source_url")) d.sourceUrl = bounded_text(doc["source_url"], "source_url", 512);
    if (!doc.contains("tracks") || !doc["tracks"].is_object() || doc["tracks"].size() > 256)
        throw std::runtime_error("Descriptor needs a tracks object");
    for (auto it = doc["tracks"].begin(); it != doc["tracks"].end(); ++it)
    {
        const auto &key = it.key();
        if (key.empty() || key.size() > 4 || key.find_first_not_of("0123456789") != std::string::npos || (key.size() > 1 && key[0] == '0'))
            throw std::runtime_error("Descriptor track keys are BIGFILE indexes");
        const json &t = it.value();
        closed(t, {"title", "author", "laps", "relic_targets", "excluded", "version", "track_key"}, "Descriptor track");
        TrackMeta m;
        if (t.contains("title")) m.title = bounded_text(t["title"], "title", 640);
        if (t.contains("author")) m.author = bounded_text(t["author"], "author", 80);
        if (t.contains("version")) m.version = bounded_text(t["version"], "version", 64);
        if (t.contains("track_key"))
        {
            m.trackKey = bounded_text(t["track_key"], "track_key", 64);
            if (!track_key_valid(m.trackKey)) throw std::runtime_error("track_key must be a registry track key");
        }
        if (t.contains("laps"))
        {
            if (!t["laps"].is_number_unsigned() || t["laps"] < 1 || t["laps"] > 7) throw std::runtime_error("laps must be 1 to 7");
            m.laps = t["laps"].get<unsigned int>();
        }
        if (t.contains("relic_targets"))
        {
            const json &r = t["relic_targets"];
            closed(r, {"sapphire", "gold", "platinum"}, "relic_targets");
            const char *tiers[] = {"sapphire", "gold", "platinum"};
            for (int i = 0; i < 3; i++)
            {
                if (!r.contains(tiers[i]) || !r[tiers[i]].is_number_unsigned() || r[tiers[i]] < 1 || r[tiers[i]] > 3456000)
                    throw std::runtime_error("relic_targets needs sapphire, gold and platinum ticks");
                m.ticks[i] = r[tiers[i]].get<int>();
            }
            if (!(m.ticks[0] > m.ticks[1] && m.ticks[1] > m.ticks[2])) throw std::runtime_error("relic_targets must be faster from sapphire to platinum");
            m.relic = true;
        }
        if (t.contains("excluded"))
        {
            if (!t["excluded"].is_array() || t["excluded"].size() > 2) throw std::runtime_error("excluded must be a list");
            for (const auto &e : t["excluded"])
            {
                if (e == "relic") m.excludeRelic = true;
                else if (e == "ctr_tokens") m.excludeTokens = true;
                else throw std::runtime_error("excluded takes relic and ctr_tokens");
            }
        }
        d.tracks[std::stoi(key)] = m;
    }
    return d;
}

std::string path_stem(const std::string &path)
{
    auto name = path.substr(path.find_last_of("/\\") + 1);
    auto dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string path_dir(const std::string &path)
{
    auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

/* ---- Streaming extractor. */
struct ChangedEntry
{
    uint32_t index = 0, size = 0;
    long long scratch = -1; /* Offset in the scratch file, -1 when not kept. */
};

struct PassLayout
{
    std::map<uint32_t, uint32_t> dirs;            /* lba -> bytes, with their path prefix below */
    std::map<uint32_t, std::string> dirPrefix;
    std::map<std::string, IsoFile> files;
    std::vector<unsigned char> bigfileTable;
};

struct Extractor
{
    Disc &disc;
    FILE *scratch;
    PassLayout layout;    /* What earlier passes and this one learned. */
    bool needPass = false;
    long long lastLba = -1;
    unsigned char raw[kRawSector];
    size_t rawFill = 0;
    uint32_t nextLba = 0;
    long long scratchEnd = 0;

    /* Directory collection. */
    std::map<uint32_t, std::vector<unsigned char>> dirData;
    size_t dirBytes = 0; /* Collected directory bytes, this pass. */
    std::set<uint32_t> dirsParsed;

    /* BIGFILE. */
    uint32_t bigLba = 0, bigSize = 0;
    bool bigKnown = false, tableReady = false;
    std::vector<std::pair<uint32_t, uint32_t>> entries;
    std::vector<uint32_t> order; /* entry indexes sorted by start */
    size_t nextEntry = 0;
    struct Active { uint32_t index; std::vector<unsigned char> bytes; bool differs; bool keep; };
    std::vector<Active> active;
    std::vector<ChangedEntry> changed;

    /* Other ISO files: path -> changed */
    std::map<std::string, bool> fileChanged;

    Extractor(Disc &d, FILE *s, const PassLayout &previous) : disc(d), scratch(s), layout(previous)
    {
        if (layout.dirs.empty()) return;
        for (auto &f : layout.files) fileChanged[f.first] = false;
        auto big = layout.files.find("/BIGFILE.BIG");
        if (big != layout.files.end()) { bigLba = big->second.lba; bigSize = big->second.size; bigKnown = true; }
        if (bigKnown && !layout.bigfileTable.empty()) parse_table(layout.bigfileTable);
        if (!tableReady) layout.bigfileTable.clear(); /* A partial table from an earlier pass is read again. */
    }

    void learn_dir(uint32_t lba, uint32_t size, const std::string &prefix)
    {
        if (size > 1024 * 1024 || layout.dirs.size() > 4096) throw std::runtime_error("Patched disc directory is not ISO 9660");
        if (layout.dirs.count(lba)) return; /* Known from this or an earlier pass. */
        layout.dirs[lba] = size;
        layout.dirPrefix[lba] = prefix;
        if ((long long)lba <= lastLba) needPass = true;
    }

    void learn_file(const std::string &path, uint32_t lba, uint32_t size)
    {
        if (layout.files.size() > 65536) throw std::runtime_error("Patched disc has too many files");
        bool known = layout.files.count(path) != 0;
        layout.files[path] = {lba, size};
        byStartDirty = true;
        if (!known)
        {
            fileChanged[path] = false;
            if ((long long)lba <= lastLba && size) needPass = true;
        }
        if (path == "/BIGFILE.BIG" && !bigKnown)
        {
            bigLba = lba; bigSize = size; bigKnown = true;
        }
    }

    void parse_dir(uint32_t lba)
    {
        const auto &data = dirData[lba];
        const std::string prefix = layout.dirPrefix[lba];
        for (size_t i = 0; i < data.size();)
        {
            unsigned ln = data[i];
            if (!ln) { i = (i / kSector + 1) * kSector; continue; }
            if (i + ln > data.size() || ln < 34) break;
            const unsigned char *rec = &data[i];
            unsigned nl = rec[32];
            if (33u + nl > ln) break;
            std::string name(reinterpret_cast<const char *>(rec + 33), nl);
            i += ln;
            if (name == std::string(1, '\0') || name == std::string(1, '\1')) continue;
            name = name.substr(0, name.find(';'));
            if (rec[25] & 2)
            {
                if (prefix.size() < 256) learn_dir(rd32(rec + 2), rd32(rec + 10), prefix + "/" + name);
            }
            else learn_file(prefix + "/" + name, rd32(rec + 2), rd32(rec + 10));
        }
        dirsParsed.insert(lba);
        dirData.erase(lba);
    }

    void parse_table(const std::vector<unsigned char> &table)
    {
        if (table.size() < 8) return;
        uint32_t n = rd32(&table[4]);
        if (n > 4096 || table.size() < 8 + 8 * (size_t)n) return; /* Incomplete: never parsed. */
        entries.clear();
        for (uint32_t i = 0; i < n; i++) entries.push_back({rd32(&table[8 + 8 * i]), rd32(&table[12 + 8 * i])});
        order.clear();
        for (uint32_t i = 0; i < n; i++)
            if (entries[i].second) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return entries[a].first < entries[b].first; });
        tableReady = true;
        layout.bigfileTable = table;
        if (!order.empty() && (long long)(bigLba + entries[order[0]].first) <= lastLba) needPass = true;
    }

    void finish_entry(Active &a)
    {
        const uint32_t size = entries[a.index].second;
        bool differs = a.differs || a.index >= disc.bigfile.size() || disc.bigfile[a.index].second != size;
        if (!differs) return;
        ChangedEntry c;
        c.index = a.index;
        c.size = size;
        if (a.keep && a.bytes.size() == size)
        {
            if (scratchEnd + (long long)size > kScratchBytesMax) throw std::runtime_error("Patch changes too much track data");
            if (seek64(scratch, (unsigned long long)scratchEnd) || std::fwrite(a.bytes.data(), 1, size, scratch) != size)
                throw std::runtime_error("Cannot write the import scratch file");
            c.scratch = scratchEnd;
            scratchEnd += size;
        }
        changed.push_back(c);
    }

    void bigfile_sector(uint32_t lba, const unsigned char *data)
    {
        const uint32_t rel = lba - bigLba;
        if (!tableReady)
        {
            if (rel == 0)
            {
                uint32_t n = rd32(data + 4);
                if (n > 4096 || 8 + 8 * (size_t)n > bigSize) throw std::runtime_error("Patched BIGFILE table is damaged");
                layout.bigfileTable.assign(data, data + kSector);
                layout.bigfileTable.resize(8 + 8 * (size_t)n > kSector ? kSector : 8 + 8 * (size_t)n);
                if (8 + 8 * (size_t)n <= kSector) parse_table(layout.bigfileTable);
            }
            else if (!layout.bigfileTable.empty())
            {
                uint32_t n = rd32(&layout.bigfileTable[4]);
                size_t want = 8 + 8 * (size_t)n, have = layout.bigfileTable.size();
                size_t take = std::min<size_t>(kSector, want - have);
                if (have == (size_t)rel * kSector) layout.bigfileTable.insert(layout.bigfileTable.end(), data, data + take);
                if (layout.bigfileTable.size() == want) parse_table(layout.bigfileTable);
            }
            if (!tableReady) return;
        }
        while (nextEntry < order.size() && entries[order[nextEntry]].first <= rel)
        {
            uint32_t index = order[nextEntry++];
            if (entries[index].first < rel) { needPass = true; continue; } /* Started before we knew it. */
            if (active.size() >= kActiveMax) throw std::runtime_error("Patched BIGFILE has too many overlapping entries");
            Active a{index, {}, false, entries[index].second <= kEntryMax};
            if (a.keep) a.bytes.reserve(entries[index].second);
            active.push_back(std::move(a));
        }
        unsigned char retail[kSector];
        for (size_t k = 0; k < active.size();)
        {
            Active &a = active[k];
            const uint32_t start = entries[a.index].first, size = entries[a.index].second;
            const uint32_t at = (rel - start) * kSector;
            const uint32_t take = std::min<uint32_t>(kSector, size - at);
            if (a.keep) a.bytes.insert(a.bytes.end(), data, data + take);
            if (!a.differs)
            {
                if (a.index >= disc.bigfile.size() || disc.bigfile[a.index].second != size ||
                    !disc.sector(disc.bigfileLba + disc.bigfile[a.index].first + (rel - start), retail) ||
                    std::memcmp(retail, data, take))
                    a.differs = true;
            }
            if (!a.differs && !a.keep) { /* Nothing to keep yet. */ }
            if (a.differs && !a.keep) a.bytes.clear();
            if (at + take >= size)
            {
                finish_entry(a);
                active.erase(active.begin() + (long)k);
                continue;
            }
            k++;
        }
    }

    /* Non-BIGFILE files by start sector, rebuilt when a directory adds files. */
    std::map<uint32_t, std::vector<std::string>> byStart;
    bool byStartDirty = true;

    void file_sector(uint32_t lba, const unsigned char *rawSector)
    {
        if (byStartDirty)
        {
            byStart.clear();
            for (auto &f : layout.files)
                if (f.first != "/BIGFILE.BIG" && f.second.size && byStart[f.second.lba].size() < 4)
                    byStart[f.second.lba].push_back(f.first); /* Hostile layouts stay bounded. */
            byStartDirty = false;
        }
        /* Files may share or overlap extents; look at the closest starts only. */
        auto it = byStart.upper_bound(lba);
        int looked = 0;
        while (it != byStart.begin() && looked < 4)
        {
            --it; looked++;
            for (const auto &path : it->second)
            {
                const IsoFile &file = layout.files[path];
                uint32_t sectors = (file.size + kSector - 1) / kSector;
                if (lba >= file.lba + sectors) continue;
                bool &changedFlag = fileChanged[path];
                if (changedFlag) continue;
                auto retail = disc.files.find(path);
                unsigned char retailRaw[kRawSector];
                if (retail == disc.files.end() || retail->second.size != file.size ||
                    !disc.raw(retail->second.lba + (lba - file.lba), retailRaw) ||
                    std::memcmp(retailRaw + 16, rawSector + 16, kRawSector - 16))
                    changedFlag = true;
            }
        }
    }

    const volatile int *cancel = nullptr;

    void sector(uint32_t lba, const unsigned char *rawSector)
    {
        if (!(lba & 255) && load_flag(cancel)) throw std::runtime_error("Cancelled");
        const unsigned char *data = rawSector + kUserData;
        lastLba = lba;
        if (lba == 16)
        {
            if (std::memcmp(data + 1, "CD001", 5)) throw std::runtime_error("Patched disc is not ISO 9660");
            learn_dir(rd32(data + 156 + 2), rd32(data + 156 + 10), "");
        }
        auto dir = layout.dirs.upper_bound(lba);
        if (dir != layout.dirs.begin())
        {
            --dir;
            uint32_t sectors = (dir->second + kSector - 1) / kSector;
            if (lba < dir->first + sectors && !dirsParsed.count(dir->first))
            {
                auto &buffer = dirData[dir->first];
                if (buffer.size() == (size_t)(lba - dir->first) * kSector)
                {
                    if ((dirBytes += kSector) > kDirBytesMax) throw std::runtime_error("Patched disc directory is too large");
                    buffer.insert(buffer.end(), data, data + kSector);
                }
                if (buffer.size() >= dir->second) { buffer.resize(dir->second); parse_dir(dir->first); }
            }
        }
        if (bigKnown && lba >= bigLba && lba < bigLba + (bigSize + kSector - 1) / kSector) bigfile_sector(lba, data);
        else file_sector(lba, rawSector);
    }

    void write(const unsigned char *bytes, size_t size)
    {
        while (size)
        {
            size_t take = std::min<size_t>(size, kRawSector - rawFill);
            std::memcpy(raw + rawFill, bytes, take);
            rawFill += take; bytes += take; size -= take;
            if (rawFill == kRawSector) { sector(nextLba++, raw); rawFill = 0; }
        }
    }

    void finish()
    {
        for (auto &a : active) { a.differs = true; finish_entry(a); } /* Cut short by the end of the disc. */
        active.clear();
        if (!bigKnown || !tableReady) needPass = true;
        for (auto &d : layout.dirs) if (!dirsParsed.count(d.first)) needPass = true;
    }
};

struct DecodeContext
{
    FILE *patch;
    FILE *source;
    unsigned long long patchBytes, read = 0;
    Extractor *extractor;
    volatile int *progress;
    int passBase, passSpan;
    std::string failure;
};

long read_patch(void *context, void *destination, size_t size)
{
    auto *c = static_cast<DecodeContext *>(context);
    size_t got = std::fread(destination, 1, size, c->patch);
    if (std::ferror(c->patch)) return -1;
    c->read += got;
    if (c->progress && c->patchBytes)
        store_flag(c->progress, c->passBase + (int)((unsigned long long)c->passSpan * c->read / c->patchBytes));
    return (long)got;
}

long read_source(void *context, unsigned long long offset, void *destination, size_t size)
{
    auto *c = static_cast<DecodeContext *>(context);
    if (seek64(c->source, offset)) return -1;
    size_t got = std::fread(destination, 1, size, c->source);
    return std::ferror(c->source) ? -1 : (long)got;
}

int write_target(void *context, const void *bytes, size_t size)
{
    auto *c = static_cast<DecodeContext *>(context);
    try { c->extractor->write(static_cast<const unsigned char *>(bytes), size); return 1; }
    catch (const std::exception &e) { c->failure = e.what(); return 0; }
}

/* Region of a BIGFILE index in the retail layout, for grouped skip lines. */
bool retail_code_entry(uint32_t index) { return index >= 221 && index < 234; }

bool code_file(const std::string &path)
{
    std::string upper = path;
    for (char &c : upper) if (c >= 'a' && c <= 'z') c -= 32;
    auto ends = [&](const char *s) { size_t n = std::strlen(s); return upper.size() >= n && upper.compare(upper.size() - n, n, s) == 0; };
    return upper.find("SCUS_") != std::string::npos || ends(".BIN") || ends(".EXE") || ends(".CNF");
}

struct Built
{
    std::string manifest, pin;
    CustomPackageOwned *owned = nullptr;
};

void set_text(char *destination, size_t capacity, const std::string &text)
{
    std::snprintf(destination, capacity, "%s", text.c_str());
}

/* Trim a UTF-8 string to at most max codepoints without splitting one. */
std::string limit_codepoints(const std::string &text, size_t max)
{
    size_t count = 0, i = 0;
    while (i < text.size() && count < max)
    {
        unsigned char c = (unsigned char)text[i];
        i += c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        count++;
    }
    return text.substr(0, std::min(i, text.size()));
}

} // namespace

struct XdeltaImportScan
{
    XdeltaImportSummary summary = {};
    unsigned dropped = 0; /* Items past CTR_XDELTA_ITEM_MAX. */
    std::vector<Built> built; /* Parallel to summary.items. */
    ~XdeltaImportScan() { for (auto &b : built) CustomPackage_Free(&b.owned); }
};

extern "C" void XdeltaImport_PackageUuid(const char *patchSha256, int bigfileIndex, char out[37])
{
    static const unsigned char ns[] = {0x6b,0xa7,0xb8,0x11,0x9d,0xad,0x11,0xd1,0x80,0xb4,0x00,0xc0,0x4f,0xd4,0x30,0xc8};
    std::string input(reinterpret_cast<const char *>(ns), sizeof ns);
    input += "xdelta:" + std::string(patchSha256 ? patchSha256 : "") + ":" + std::to_string(bigfileIndex);
    unsigned char digest[20];
    sha1(reinterpret_cast<const unsigned char *>(input.data()), input.size(), digest);
    digest[6] = (unsigned char)((digest[6] & 0x0f) | 0x50);
    digest[8] = (unsigned char)((digest[8] & 0x3f) | 0x80);
    static const char hex[] = "0123456789abcdef";
    std::string text;
    for (int i = 0; i < 16; i++)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10) text += '-';
        text += hex[digest[i] >> 4];
        text += hex[digest[i] & 15];
    }
    std::memcpy(out, text.c_str(), 37);
}

extern "C" size_t XdeltaImport_CutLev(const unsigned char *bytes, size_t size) { return bytes ? cut_lev(bytes, size) : 0; }
extern "C" size_t XdeltaImport_CutVrm(const unsigned char *bytes, size_t size) { return bytes ? cut_vrm(bytes, size) : 0; }

namespace {

struct Candidate
{
    uint32_t index;
    std::vector<unsigned char> lev, vrm;
};

std::string json_dump(const json &value) { return value.dump(-1, ' ', false, json::error_handler_t::strict); }

/* Build the package for one cut pair and acquire it through the verifier. */
Built build_package(const Candidate &c, const std::string &title, const std::string &version, const TrackMeta *meta,
                    const XdeltaImportSummary &summary, const std::string &author, std::string &failure)
{
    Built b;
    const std::string levHash = hex_sha256(c.lev.data(), c.lev.size());
    const std::string vrmHash = hex_sha256(c.vrm.data(), c.vrm.size());
    json source = {{"schema_version", 1}, {"provider", "xdelta"}, {"patch_sha256", summary.patchSha256},
                   {"patch_bytes", summary.patchBytes}, {"base_disc_sha256", summary.discSha256},
                   {"bigfile_index", c.index}, {"cutter_version", CTR_XDELTA_CUTTER_VERSION}};
    if (summary.descriptorSha256[0]) source["descriptor_sha256"] = summary.descriptorSha256;
    if (!author.empty()) source["author"] = author;
    if (meta && meta->excludeRelic) source["excluded"].push_back("relic");
    if (meta && meta->excludeTokens) source["excluded"].push_back("ctr_tokens");
    if (meta && !meta->trackKey.empty()) source["track_key"] = meta->trackKey;
    const std::string presentation = json_dump(source);
    std::string settings, relic;
    const std::string index = std::to_string(c.index);
    json files = json::array();
    files.push_back({{"role", "lev"}, {"path", "original/bigfile-" + index + ".lev"}, {"sha256", levHash}, {"bytes", c.lev.size()}});
    files.push_back({{"role", "presentation"}, {"path", "metadata/xdelta-source.json"},
                     {"sha256", hex_sha256(presentation.data(), presentation.size())}, {"bytes", presentation.size()}});
    if (meta && meta->laps)
    {
        settings = json_dump({{"schema_version", 1}, {"laps", meta->laps}, {"lev_sha256", levHash}, {"vrm_sha256", vrmHash}});
        files.push_back({{"role", "race_settings"}, {"path", "metadata/race-settings.json"},
                         {"sha256", hex_sha256(settings.data(), settings.size())}, {"bytes", settings.size()}});
    }
    if (meta && meta->relic)
    {
        relic = json_dump({{"schema_version", 1}, {"lev_sha256", levHash}, {"vrm_sha256", vrmHash},
                           {"laps", meta->laps ? meta->laps : 3}, {"ticks_per_second", 960},
                           {"sapphire", meta->ticks[0]}, {"gold", meta->ticks[1]}, {"platinum", meta->ticks[2]}});
        files.push_back({{"role", "relic_targets"}, {"path", "metadata/relic-targets.json"},
                         {"sha256", hex_sha256(relic.data(), relic.size())}, {"bytes", relic.size()}});
    }
    files.push_back({{"role", "vrm"}, {"path", "original/bigfile-" + index + ".vrm"}, {"sha256", vrmHash}, {"bytes", c.vrm.size()}});
    std::sort(files.begin(), files.end(), [](const json &a, const json &b) {
        return a["role"] == b["role"] ? a["path"] < b["path"] : a["role"] < b["role"];
    });
    char uuid[37];
    XdeltaImport_PackageUuid(summary.patchSha256, (int)c.index, uuid);
    b.manifest = json_dump({{"schema_version", 1}, {"package_uuid", uuid}, {"version", version}, {"title", title},
                            {"compatibility", {{"native_contract", 1}, {"apworld_contract", 1}}}, {"files", files}});
    b.pin = hex_sha256(b.manifest.data(), b.manifest.size());
    std::vector<CustomPackageInput> inputs = {{"lev", c.lev.data(), c.lev.size()}, {"vrm", c.vrm.data(), c.vrm.size()},
                                              {"presentation", presentation.data(), presentation.size()}};
    if (!settings.empty()) inputs.push_back({"race_settings", settings.data(), settings.size()});
    if (!relic.empty()) inputs.push_back({"relic_targets", relic.data(), relic.size()});
    char detail[512];
    if (!CustomPackage_AcquireBuffers(b.manifest.data(), b.manifest.size(), b.pin.c_str(), inputs.data(), inputs.size(),
                                      &b.owned, detail, sizeof detail))
    {
        failure = detail;
        b.owned = nullptr;
    }
    return b;
}

void add_item(XdeltaImportScan &scan, const XdeltaImportItem &item, Built built = Built())
{
    if (scan.summary.count >= CTR_XDELTA_ITEM_MAX - 1)
    {
        /* The last row is kept for the overflow line. */
        CustomPackage_Free(&built.owned);
        scan.dropped++;
        return;
    }
    scan.summary.items[scan.summary.count++] = item;
    scan.built.push_back(std::move(built));
}

XdeltaImportItem skip_item(int index, const std::string &title, const std::string &reason)
{
    XdeltaImportItem item = {};
    item.bigfileIndex = index;
    set_text(item.title, sizeof item.title, limit_codepoints(title, 160));
    set_text(item.reason, sizeof item.reason, limit_codepoints(reason, 90));
    return item;
}

std::string scratch_folder(const std::string &parent)
{
    std::random_device device;
    std::mt19937 generator(device() ^ (unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    for (int attempt = 0; attempt < 16; attempt++)
    {
        char name[32];
        std::snprintf(name, sizeof name, ".import-%08x", (unsigned)generator());
        std::string path = parent + "/" + name;
        if (fs_mkdir(path)) return path;
    }
    throw std::runtime_error("Cannot create the import scratch folder");
}

struct ScratchGuard
{
    std::string folder, file;
    ~ScratchGuard()
    {
        if (!file.empty()) fs_remove(file);
        if (!folder.empty()) fs_rmdir(folder);
    }
};

std::mutex &disc_hash_mutex() { static std::mutex m; return m; }
std::map<std::string, std::string> &disc_hash_cache() { static auto *m = new std::map<std::string, std::string>(); return *m; }

std::string disc_identity(const std::string &path)
{
    long long size = 0, mtime = 0;
    if (!fs_stat(path, &size, &mtime)) return path;
    return path + "|" + std::to_string(size) + "|" + std::to_string(mtime);
}

} // namespace

extern "C" int XdeltaImport_Scan(const XdeltaImportRequest *request, XdeltaImportScan **out,
                                 volatile int *progress, const volatile int *cancel, char *error, size_t errorSize)
{
    if (error && errorSize) error[0] = 0;
    store_flag(progress, 0);
    try
    {
        if (!request || !request->patchPath || !request->discPath || !request->scratchDir || !out || *out)
            throw std::runtime_error("Expected patch, disc and scratch folder");
        std::unique_ptr<XdeltaImportScan> scan(new XdeltaImportScan());
        XdeltaImportSummary &summary = scan->summary;
        const std::string patchPath = request->patchPath, discPath = request->discPath;

        /* 1. Identify the patch and check the base. */
        File patch = open_read(patchPath);
        unsigned char head[8] = {};
        size_t headBytes = std::fread(head, 1, sizeof head, patch.get());
        int header = NativeXdelta_CheckHeader(head, headBytes);
        if (header == NATIVE_XDELTA_NOT_VCDIFF) throw std::runtime_error("Not an xdelta patch");
        if (header == NATIVE_XDELTA_UNSUPPORTED) throw std::runtime_error("Unsupported patch compression");
        set_text(summary.patchSha256, sizeof summary.patchSha256, hash_file(patchPath, &summary.patchBytes, cancel));
        std::string discSha;
        {
            std::lock_guard<std::mutex> guard(disc_hash_mutex());
            auto key = disc_identity(discPath);
            auto cached = disc_hash_cache().find(key);
            if (cached != disc_hash_cache().end()) discSha = cached->second;
            else disc_hash_cache()[key] = discSha = hash_file(discPath, nullptr, cancel);
        }
        if (!request->skipRetailCheck && discSha != kRetailDiscSha256)
            throw std::runtime_error("Your disc is not the retail NTSC-U image this patch needs");
        set_text(summary.discSha256, sizeof summary.discSha256, discSha);
        store_flag(progress, 5);
        Disc disc(discPath);
        disc.walk();

        /* 2. The optional pack descriptor. */
        Descriptor descriptor;
        bool haveDescriptor = false;
        {
            std::string path = request->descriptorPath ? request->descriptorPath
                                                       : path_dir(patchPath) + "/" + path_stem(patchPath) + ".ctrpack.json";
            File file(fs_open(path, "rb"));
            if (file)
            {
                try
                {
                    unsigned long long size = file_size(file.get());
                    if (size > kDescriptorMax) throw std::runtime_error("Descriptor is too large");
                    std::vector<unsigned char> bytes((size_t)size);
                    if (size && std::fread(bytes.data(), 1, bytes.size(), file.get()) != bytes.size())
                        throw std::runtime_error("Cannot read the descriptor");
                    descriptor = parse_descriptor(bytes, summary.patchSha256);
                    haveDescriptor = true;
                    set_text(summary.descriptorSha256, sizeof summary.descriptorSha256, descriptor.sha256);
                }
                catch (const std::exception &e)
                {
                    set_text(summary.descriptorNote, sizeof summary.descriptorNote,
                             limit_codepoints(std::string("Pack descriptor not used: ") + e.what(), 90));
                }
            }
        }

        /* 3. Decode, at most three passes. */
        ScratchGuard guard;
        guard.folder = scratch_folder(request->scratchDir);
        guard.file = guard.folder + "/entries.bin";
        File scratch(fs_open(guard.file, "w+b"));
        if (!scratch) throw std::runtime_error("Cannot create the import scratch file");
        File source = open_read(discPath);
        PassLayout layout;
        std::unique_ptr<Extractor> extractor;
        for (unsigned pass = 0;; pass++)
        {
            if (pass == 3) throw std::runtime_error("Patched disc layout could not be read in three passes");
            extractor.reset(new Extractor(disc, scratch.get(), layout));
            extractor->cancel = cancel;
            std::rewind(patch.get());
            DecodeContext context{patch.get(), source.get(), summary.patchBytes, 0, extractor.get(), progress,
                                  5, 85, {}};
            NativeXdeltaIo io = {read_patch, read_source, write_target, &context, disc.bytes, cancel};
            char detail[256];
            int result = NativeXdelta_Decode(&io, detail, sizeof detail);
            if (result == NATIVE_XDELTA_STOPPED && !context.failure.empty()) throw std::runtime_error(context.failure);
            if (result != NATIVE_XDELTA_OK) throw std::runtime_error(detail);
            extractor->finish();
            summary.passes = pass + 1;
            if (!extractor->needPass) break;
            layout = extractor->layout;
        }
        store_flag(progress, 92);

        /* 4. Pair, cut, build, classify. */
        Extractor &ex = *extractor;
        std::map<uint32_t, const ChangedEntry *> byIndex;
        for (const auto &c : ex.changed) byIndex[c.index] = &c;
        auto entry_bytes = [&](uint32_t index) -> std::vector<unsigned char> {
            auto it = byIndex.find(index);
            if (it == byIndex.end()) return disc.entry(index); /* Unchanged: the player's own bytes. */
            if (it->second->scratch < 0) return {};
            std::vector<unsigned char> bytes(it->second->size);
            if (seek64(scratch.get(), (unsigned long long)it->second->scratch) ||
                std::fread(bytes.data(), 1, bytes.size(), scratch.get()) != bytes.size())
                throw std::runtime_error("Cannot read the import scratch file");
            return bytes;
        };
        std::set<uint32_t> consumed;
        size_t offeredBytes = 0;
        std::map<std::pair<std::string, std::string>, std::string> seen;
        const std::string stem = limit_codepoints(path_stem(patchPath), 100);
        for (const auto &c : ex.changed)
        {
            if (load_flag(cancel)) throw std::runtime_error("Cancelled");
            if (c.scratch < 0) continue;
            auto lev = entry_bytes(c.index);
            size_t levCut = cut_lev(lev.data(), lev.size());
            if (!levCut) continue;
            /* A LEV pairs with the VRM before it in its BIGFILE group. A
               LEV-shaped entry without one (racer models and the like) is
               not a track and lands in the grouped "other" line. */
            if (c.index == 0) continue;
            auto vrm = entry_bytes(c.index - 1);
            size_t vrmCut = cut_vrm(vrm.data(), vrm.size());
            if (!vrmCut) continue;
            consumed.insert(c.index);
            const TrackMeta *meta = nullptr;
            if (haveDescriptor && descriptor.tracks.count((int)c.index)) meta = &descriptor.tracks[(int)c.index];
            std::string title = meta && !meta->title.empty() ? meta->title : stem + " slot " + std::to_string(c.index);
            if (!CustomPackage_ValidateTitle(title.data(), title.size())) title = "Imported track slot " + std::to_string(c.index);
            if (byIndex.count(c.index - 1)) consumed.insert(c.index - 1);
            Candidate candidate{c.index, std::vector<unsigned char>(lev.begin(), lev.begin() + (long)levCut),
                                std::vector<unsigned char>(vrm.begin(), vrm.begin() + (long)vrmCut)};
            lev.clear(); lev.shrink_to_fit(); vrm.clear(); vrm.shrink_to_fit();
            const std::string levHash = hex_sha256(candidate.lev.data(), candidate.lev.size());
            const std::string vrmHash = hex_sha256(candidate.vrm.data(), candidate.vrm.size());
            auto duplicate = seen.find({levHash, vrmHash});
            if (duplicate != seen.end())
            {
                add_item(*scan, skip_item((int)c.index, title, "Same track as " + duplicate->second));
                continue;
            }
            if (offeredBytes + candidate.lev.size() + candidate.vrm.size() > kOfferedBytesMax)
            {
                add_item(*scan, skip_item((int)c.index, title, "Too much for one import"));
                continue;
            }
            std::string version = meta && !meta->version.empty() ? meta->version : "1.0.0";
            std::string author = meta && !meta->author.empty() ? meta->author : descriptor.packAuthor;
            std::string failure;
            Built built = build_package(candidate, title, version, meta, summary, author, failure);
            if (!built.owned && meta && (meta->relic || meta->laps || !meta->version.empty()))
            {
                /* A descriptor value the package rules refuse never costs the track. */
                std::string retry;
                built = build_package(candidate, title, "1.0.0", nullptr, summary, author, retry);
                if (built.owned) failure.clear();
            }
            XdeltaImportItem item = skip_item((int)c.index, title, "");
            set_text(item.levSha256, sizeof item.levSha256, levHash);
            set_text(item.vrmSha256, sizeof item.vrmSha256, vrmHash);
            if (!built.owned)
            {
                set_text(item.reason, sizeof item.reason, limit_codepoints("Files failed checks: " + failure, 90));
                add_item(*scan, item);
                continue;
            }
            CustomContentVerification report;
            CustomPackage_GetPairReport(built.owned, &report);
            char ttReason[64] = {}, arcadeReason[64] = {};
            int timeTrial = CustomOffline_PackageTimeTrialReason(built.owned, ttReason, sizeof ttReason);
            int arcade = CustomOffline_PackageArcadeReason(built.owned, arcadeReason, sizeof arcadeReason);
            const auto &m = report.measured;
            if (!timeTrial)
            {
                bool arena = !m.checkpoints && (m.crystals > 0 || (m.spawns >= 2 && m.spawns <= 4 && m.weaponCrates > 0));
                std::string reason = arena ? "Arena: not supported yet"
                                   : !m.checkpoints ? "Not a race track (no lap checkpoints)"
                                   : "Files failed checks: " + std::string(ttReason);
                set_text(item.reason, sizeof item.reason, reason);
                CustomPackage_Free(&built.owned);
                add_item(*scan, item);
                continue;
            }
            std::string tags = "Race TT";
            if (arcade) tags += " Arcade";
            CustomPackageRelicTargets targets;
            if (m.relicCrates > 0 && !(meta && meta->excludeRelic) &&
                CustomPackage_GetRelicTargets(built.owned, &targets, nullptr, 0)) tags += " Relic";
            if (m.letterC == 1 && m.letterT == 1 && m.letterR == 1 && !(meta && meta->excludeTokens)) tags += " CTR";
            set_text(item.tags, sizeof item.tags, tags);
            set_text(item.pin, sizeof item.pin, built.pin);
            set_text(item.trackKey, sizeof item.trackKey, meta && !meta->trackKey.empty() ? meta->trackKey : title_track_key(title));
            if (!track_key_valid(item.trackKey))
            {
                set_text(item.reason, sizeof item.reason, "No folder name fits this title");
                CustomPackage_Free(&built.owned);
                add_item(*scan, item);
                continue;
            }
            item.offered = 1;
            offeredBytes += candidate.lev.size() + candidate.vrm.size();
            seen[{levHash, vrmHash}] = title;
            add_item(*scan, item, std::move(built));
        }

        /* 5. Grouped lines for everything that is not a track. */
        std::vector<std::string> code;
        unsigned other = 0;
        for (const auto &f : ex.fileChanged)
            if (f.second && f.first != "/BIGFILE.BIG")
            {
                if (code_file(f.first)) code.push_back(f.first.substr(1));
                else other++;
            }
        for (const auto &f : disc.files)
            if (!ex.layout.files.count(f.first)) other++; /* Removed from the patched disc. */
        for (const auto &c : ex.changed)
        {
            if (consumed.count(c.index)) continue;
            if (retail_code_entry(c.index)) code.push_back("overlay " + std::to_string(c.index));
            else other++;
        }
        if (!code.empty())
        {
            std::string names;
            for (size_t i = 0; i < code.size() && i < 4; i++) names += (i ? ", " : "") + code[i];
            if (code.size() > 4) names += ", +" + std::to_string(code.size() - 4);
            add_item(*scan, skip_item(-1, "Game code (" + names + ")", "Code does not run in this client"));
        }
        if (other)
            add_item(*scan, skip_item(-1, "Other changed files (" + std::to_string(other) + ")", "Not track content"));
        if (scan->dropped)
        {
            XdeltaImportItem more = skip_item(-1, std::to_string(scan->dropped) + " more items", "Too many items for one list");
            scan->summary.items[scan->summary.count++] = more;
            scan->built.push_back(Built());
        }
        store_flag(progress, 100);
        *out = scan.release();
        return 1;
    }
    catch (const std::exception &exception)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", exception.what());
        return 0;
    }
}

extern "C" const XdeltaImportSummary *XdeltaImport_Summary(const XdeltaImportScan *scan)
{
    return scan ? &scan->summary : nullptr;
}

extern "C" int XdeltaImport_CopyManifest(const XdeltaImportScan *scan, unsigned int item, void *destination,
                                         size_t capacity, size_t *written)
{
    if (!scan || item >= scan->built.size() || !scan->built[item].owned) return 0;
    return CustomPackage_CopyManifestJSON(scan->built[item].owned, destination, capacity, written);
}

extern "C" int XdeltaImport_TrackKey(const char *title, char out[65])
{
    if (!out) return 0;
    out[0] = 0;
    if (!title) return 0;
    const std::string key = title_track_key(title);
    if (!track_key_valid(key)) return 0;
    std::snprintf(out, 65, "%s", key.c_str());
    return 1;
}

namespace {

/* ---- Room folder install (decision 35): ASSETS/tracks/<key>/original. Rooms
   and solo seeds look there and pick a LEV and VRM by their hashes
   (CustomTrackManager_LocateContentFiles), so the files only need a name of
   their own: xdelta-<first 12 of the patch hash>-<BIGFILE index>.lev/.vrm. A
   file already under that name is kept when its bytes are the same and
   refused otherwise; nothing is ever replaced. If the VRM cannot be placed,
   a LEV this install placed is removed again. <key> and original/ may not
   be links; the tracks folder itself is the player's configuration and may
   be one, as for Saphi installs (ap_custom_track_download.cpp). */
#ifdef CTR_XDELTA_TESTING
/* Harness only: fail the VRM after the LEV is placed, to test the rollback. */
extern "C" int g_xdeltaTestFailRoomVrm;
int g_xdeltaTestFailRoomVrm;
#define XDELTA_TEST_FAIL_VRM() do { if (g_xdeltaTestFailRoomVrm) throw std::runtime_error("Test failure"); } while (0)
#else
#define XDELTA_TEST_FAIL_VRM() do { } while (0)
#endif

#ifdef _WIN32
struct Handle
{
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Handle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};

/* Open a directory and keep it open without FILE_SHARE_DELETE, so it cannot
   be renamed, removed or swapped for a junction while the install runs. */
bool pin_directory(const std::string &path, bool create, bool allowLink, Handle &out)
{
    std::wstring p = wide(path);
    if (p.empty()) return false;
    if (create) _wmkdir(p.c_str());
    out.h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | (allowLink ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), nullptr);
    BY_HANDLE_FILE_INFORMATION info;
    return out.h != INVALID_HANDLE_VALUE && GetFileInformationByHandle(out.h, &info) &&
           (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           (allowLink || !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT));
}

/* Hold a path's own entry (a junction or a plain folder) open without delete
   sharing, so it cannot be renamed or replaced. */
bool pin_link(const std::string &path, Handle &out)
{
    std::wstring p = wide(path);
    out.h = p.empty() ? INVALID_HANDLE_VALUE
                      : CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info;
    return out.h != INVALID_HANDLE_VALUE && GetFileInformationByHandle(out.h, &info) &&
           (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* Remove a LEV this install placed: only while the name still holds exactly
   these bytes, through one handle opened without sharing, so the removed
   file is the one checked. Returns false when it could not be done safely. */
bool remove_own_file(const std::string &target, const std::vector<unsigned char> &bytes)
{
    HANDLE h = CreateFileW(wide(target).c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool same = true;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || (unsigned long long)size.QuadPart != bytes.size()) same = false;
    std::vector<unsigned char> back(same ? bytes.size() : 0);
    DWORD got = 0;
    if (same && (!ReadFile(h, back.data(), (DWORD)back.size(), &got, nullptr) || got != back.size() || back != bytes)) same = false;
    FILE_DISPOSITION_INFO dispose = {TRUE};
    const bool removed = same && SetFileInformationByHandle(h, FileDispositionInfo, &dispose, sizeof dispose);
    CloseHandle(h);
    return removed;
}

/* 0 absent, 1 the same bytes, -1 something else. */
int room_name_state(const std::string &target, const std::vector<unsigned char> &bytes)
{
    DWORD attributes = GetFileAttributesW(wide(target).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return -1;
    try { return hash_file(target, nullptr, nullptr) == hex_sha256(bytes.data(), bytes.size()) ? 1 : -1; }
    catch (const std::exception &) { return -1; }
}

void publish_room_file(const std::string &folder, const std::string &name, const std::vector<unsigned char> &bytes)
{
    std::string temp;
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int k = 0; k < 64 && h == INVALID_HANDLE_VALUE; k++)
    {
        temp = folder + "/.xdelta-" + std::to_string((unsigned long)GetCurrentProcessId()) + "-" + std::to_string(k) + ".part";
        h = CreateFileW(wide(temp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not write in the track folder");
    DWORD wrote = 0;
    const bool ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr) && wrote == bytes.size() && FlushFileBuffers(h);
    CloseHandle(h);
    /* MoveFileEx without MOVEFILE_REPLACE_EXISTING never replaces a name. */
    const bool moved = ok && MoveFileExW(wide(temp).c_str(), wide(folder + "/" + name).c_str(), MOVEFILE_WRITE_THROUGH);
    const DWORD moveError = GetLastError();
    if (!moved)
    {
        fs_remove(temp);
        throw std::runtime_error(ok && (moveError == ERROR_ALREADY_EXISTS || moveError == ERROR_FILE_EXISTS)
                                 ? "Another file is in the way: " + name : "Could not write in the track folder");
    }
    if (room_name_state(folder + "/" + name, bytes) != 1) throw std::runtime_error("A track file changed while it was installed");
}

void publish_room_vrm(const std::string &folder, const std::string &name, const std::vector<unsigned char> &bytes)
{
    XDELTA_TEST_FAIL_VRM();
    publish_room_file(folder, name, bytes);
}

void install_room(const std::string &assets, const std::string &key, const std::string &stem,
                  const std::vector<unsigned char> &lev, const std::vector<unsigned char> &vrm)
{
    const std::string tracks = assets + "/tracks", root = tracks + "/" + key, original = root + "/original";
    /* The tracks folder may be a junction: pin the junction itself and its
       target, so neither can be swapped while the install runs. */
    Handle tracksLink, tracksPin, rootPin, originalPin;
    if (!pin_directory(tracks, true, true, tracksPin) || !pin_link(tracks, tracksLink) ||
        !pin_directory(root, true, false, rootPin) || !pin_directory(original, true, false, originalPin))
        throw std::runtime_error("tracks/" + key + "/original is a link or could not be made");
    const std::string names[2] = {stem + ".lev", stem + ".vrm"};
    const std::vector<unsigned char> *bytes[2] = {&lev, &vrm};
    int state[2];
    for (int i = 0; i < 2; i++)
        if ((state[i] = room_name_state(original + "/" + names[i], *bytes[i])) < 0)
            throw std::runtime_error("Another file is in the way: " + names[i]);
    bool placedLev = false;
    try
    {
        if (!state[0]) { publish_room_file(original, names[0], lev); placedLev = true; }
        if (!state[1]) publish_room_vrm(original, names[1], vrm);
    }
    catch (const std::exception &e)
    {
        if (placedLev && !remove_own_file(original + "/" + names[0], lev))
            throw std::runtime_error(std::string(e.what()) + ". The LEV stays in tracks/" + key + "/original");
        throw;
    }
}
#else
struct Fd
{
    int fd = -1;
    explicit Fd(int value) : fd(value) {}
    ~Fd() { if (fd >= 0) close(fd); }
};

int open_child_directory(int parent, const char *name)
{
    if (mkdirat(parent, name, 0755) != 0 && errno != EEXIST) return -1;
    return openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

bool same_bytes(int fd, const std::vector<unsigned char> &bytes)
{
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || (unsigned long long)info.st_size != bytes.size()) return false;
    std::vector<unsigned char> back(bytes.size());
    size_t at = 0;
    while (at < back.size())
    {
        const ssize_t n = pread(fd, back.data() + at, back.size() - at, (off_t)at);
        if (n <= 0) return false;
        at += (size_t)n;
    }
    return back == bytes;
}

/* 0 absent, 1 the same bytes, -1 something else. */
int room_name_state(int folder, const std::string &name, const std::vector<unsigned char> &bytes)
{
    Fd existing(openat(folder, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (existing.fd < 0) return errno == ENOENT ? 0 : -1;
    return same_bytes(existing.fd, bytes) ? 1 : -1;
}

/* Publishes name and returns its identity, for a rollback that removes only
   this install's own file. */
struct stat publish_room_file(int folder, const std::string &name, const std::vector<unsigned char> &bytes)
{
    char temp[64] = "";
    Fd out(-1);
    for (int k = 0; out.fd < 0 && k < 64; k++)
    {
        std::snprintf(temp, sizeof temp, ".xdelta-%d-%d.part", (int)getpid(), k);
        out.fd = openat(folder, temp, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (out.fd < 0 && errno != EEXIST) break;
    }
    if (out.fd < 0) throw std::runtime_error("Could not write in the track folder");
    struct stat mine;
    size_t at = 0;
    if (fstat(out.fd, &mine) == 0)
        while (at < bytes.size())
        {
            const ssize_t n = write(out.fd, bytes.data() + at, bytes.size() - at);
            if (n <= 0) break;
            at += (size_t)n;
        }
    /* linkat never replaces: a name that appeared meanwhile fails the install. */
    const bool linked = at == bytes.size() && fsync(out.fd) == 0 && linkat(folder, temp, folder, name.c_str(), 0) == 0;
    const int linkError = errno;
    unlinkat(folder, temp, 0);
    if (!linked)
        throw std::runtime_error(linkError == EEXIST ? "Another file is in the way: " + name : "Could not write in the track folder");
    struct stat placed;
    if (fstatat(folder, name.c_str(), &placed, AT_SYMLINK_NOFOLLOW) || placed.st_dev != mine.st_dev ||
        placed.st_ino != mine.st_ino || !same_bytes(out.fd, bytes))
        throw std::runtime_error("A track file changed while it was installed");
    return mine;
}

void publish_room_vrm(int folder, const std::string &name, const std::vector<unsigned char> &bytes)
{
    XDELTA_TEST_FAIL_VRM();
    publish_room_file(folder, name, bytes);
}

/* Remove a LEV this install placed without ever removing another file:
   move the name aside to a fresh private name first (RENAME_NOREPLACE), and
   unlink it only when the moved file is this install's own. Anything else is
   moved back and kept. Returns false when the LEV could not be removed. */
bool remove_own_file(int folder, const std::string &name, const struct stat &mine)
{
    char aside[64];
    for (int k = 0; k < 64; k++)
    {
        std::snprintf(aside, sizeof aside, ".xdelta-rollback-%d-%d", (int)getpid(), k);
        if (renameat2(folder, name.c_str(), folder, aside, RENAME_NOREPLACE) == 0) break;
        if (errno != EEXIST) return false;
        aside[0] = 0;
    }
    if (!aside[0]) return false;
    struct stat now;
    if (!fstatat(folder, aside, &now, AT_SYMLINK_NOFOLLOW) && now.st_dev == mine.st_dev && now.st_ino == mine.st_ino)
        return unlinkat(folder, aside, 0) == 0;
    renameat2(folder, aside, folder, name.c_str(), RENAME_NOREPLACE);
    return false;
}

void install_room(const std::string &assets, const std::string &key, const std::string &stem,
                  const std::vector<unsigned char> &lev, const std::vector<unsigned char> &vrm)
{
    Fd base(open(assets.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (base.fd >= 0 && mkdirat(base.fd, "tracks", 0755) != 0 && errno != EEXIST) base.fd = -1;
    Fd tracks(base.fd < 0 ? -1 : openat(base.fd, "tracks", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    Fd root(tracks.fd < 0 ? -1 : open_child_directory(tracks.fd, key.c_str()));
    Fd original(root.fd < 0 ? -1 : open_child_directory(root.fd, "original"));
    if (original.fd < 0) throw std::runtime_error("tracks/" + key + "/original is a link or could not be made");
    const std::string names[2] = {stem + ".lev", stem + ".vrm"};
    const std::vector<unsigned char> *bytes[2] = {&lev, &vrm};
    int state[2];
    for (int i = 0; i < 2; i++)
        if ((state[i] = room_name_state(original.fd, names[i], *bytes[i])) < 0)
            throw std::runtime_error("Another file is in the way: " + names[i]);
    bool placedLev = false;
    struct stat levIdentity = {};
    try
    {
        if (!state[0]) { levIdentity = publish_room_file(original.fd, names[0], lev); placedLev = true; }
        if (!state[1]) publish_room_vrm(original.fd, names[1], vrm);
    }
    catch (const std::exception &e)
    {
        if (placedLev && !remove_own_file(original.fd, names[0], levIdentity))
            throw std::runtime_error(std::string(e.what()) + ". The LEV stays in tracks/" + key + "/original");
        throw;
    }
}
#endif

std::vector<unsigned char> package_file(const CustomPackageOwned *package, const char *role)
{
    CustomPackageManifest manifest;
    size_t size = 0, written = 0;
    if (CustomPackage_GetManifest(package, &manifest))
        for (size_t i = 0; i < manifest.count; i++)
            if (!std::strcmp(manifest.files[i].role, role)) size = manifest.files[i].bytes;
    std::vector<unsigned char> bytes(size);
    if (!size || !CustomPackage_CopyFile(package, role, bytes.data(), bytes.size(), &written) || written != size)
        throw std::runtime_error("The verified files could not be read back");
    return bytes;
}

} // namespace

extern "C" int XdeltaImport_Install(const XdeltaImportScan *scan, const unsigned char *selected,
                                    const char *assets, int where, char *error, size_t errorSize)
{
    if (error && errorSize) error[0] = 0;
    if (!scan || !selected || !assets || !(where & (CTR_XDELTA_INSTALL_PACKAGES | CTR_XDELTA_INSTALL_ROOM))) return -1;
    int installed = 0;
    for (unsigned int i = 0; i < scan->summary.count; i++)
    {
        const XdeltaImportItem &item = scan->summary.items[i];
        if (!selected[i] || !item.offered || !scan->built[i].owned) continue;
        char detail[512];
        if ((where & CTR_XDELTA_INSTALL_PACKAGES) &&
            !CustomPackage_InstallAssets(scan->built[i].owned, assets, detail, sizeof detail))
        {
            if (error && errorSize)
            {
                if (installed) std::snprintf(error, errorSize, "Installed %d, then %s: %s", installed, item.title, detail);
                else std::snprintf(error, errorSize, "%s: %s", item.title, detail);
            }
            return -1;
        }
        if (where & CTR_XDELTA_INSTALL_ROOM)
        {
            try
            {
                if (!track_key_valid(item.trackKey)) throw std::runtime_error("No folder name fits this title");
                const std::string stem = "xdelta-" + std::string(scan->summary.patchSha256).substr(0, 12) + "-" +
                                         std::to_string(item.bigfileIndex);
                install_room(assets, item.trackKey, stem, package_file(scan->built[i].owned, "lev"),
                             package_file(scan->built[i].owned, "vrm"));
            }
            catch (const std::exception &e)
            {
                if (error && errorSize)
                {
                    if (installed) std::snprintf(error, errorSize, "Installed %d, then %s: %s", installed, item.title, e.what());
                    else std::snprintf(error, errorSize, "%s: %s", item.title, e.what());
                }
                return -1;
            }
        }
        installed++;
    }
    return installed;
}

extern "C" void XdeltaImport_Free(XdeltaImportScan **scan)
{
    if (!scan) return;
    delete *scan;
    *scan = nullptr;
}

extern "C" int XdeltaImport_ListFolder(const char *assets, char names[][CTR_XDELTA_NAME_MAX], int max)
{
    if (!assets || !names || max <= 0) return 0;
    std::vector<std::string> found;
    const std::string folder = std::string(assets) + "/tracks/import";
    auto wanted = [](const std::string &name) {
        std::string lower = name;
        for (char &c : lower) if (c >= 'A' && c <= 'Z') c += 32;
        auto ends = [&](const char *s) { size_t n = std::strlen(s); return lower.size() > n && lower.compare(lower.size() - n, n, s) == 0; };
        return name.size() < CTR_XDELTA_NAME_MAX && name[0] != '.' && (ends(".xdelta") || ends(".vcdiff"));
    };
    fs_mkdir(std::string(assets) + "/tracks");
    fs_mkdir(folder);
#ifdef _WIN32
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(wide(folder + "/*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::string name = narrow(data.cFileName);
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !name.empty() && wanted(name)) found.push_back(name);
        }
        while (FindNextFileW(find, &data) && found.size() < 1024);
        FindClose(find);
    }
#else
    if (DIR *dir = opendir(folder.c_str()))
    {
        while (dirent *e = readdir(dir))
        {
            if (found.size() >= 1024) break;
            struct stat info;
            if (wanted(e->d_name) && !stat((folder + "/" + e->d_name).c_str(), &info) && S_ISREG(info.st_mode)) found.push_back(e->d_name);
        }
        closedir(dir);
    }
#endif
    std::sort(found.begin(), found.end());
    int count = 0;
    for (const auto &name : found)
    {
        if (count == max) break;
        std::snprintf(names[count++], CTR_XDELTA_NAME_MAX, "%s", name.c_str());
    }
    return count;
}

/* ---- Job API. */
namespace {
struct JobState
{
    std::mutex mutex;
    std::future<void> scanJob, installJob;
    volatile int progress = 0;
    volatile int cancel = 0;
    XdeltaImportScan *scan = nullptr;
    std::string error;
    int scanState = -1;     /* -1 idle, 0 running, 1 ready, -2 failed */
    int installState = -1;
    int installed = 0;
};
JobState &job() { static auto *s = new JobState(); return *s; }
}

extern "C" int XdeltaImport_StartScan(const char *patchPath, const char *discPath, const char *assets)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    if (s.scanState == 0 || s.installState == 0 || !patchPath || !discPath || !*discPath || !assets ||
        std::strlen(patchPath) > 4096 || std::strlen(assets) > 4096) return 0;
    XdeltaImport_Free(&s.scan);
    s.scanState = 0; store_flag(&s.progress, 0); store_flag(&s.cancel, 0); s.error.clear();
    try
    {
        std::string patch(patchPath), disc(discPath), root(assets);
        s.scanJob = std::async(std::launch::async, [patch, disc, root] {
            auto &state = job();
            std::string tracks = root + "/tracks";
            fs_mkdir(tracks);
            XdeltaImportRequest request = {patch.c_str(), disc.c_str(), nullptr, tracks.c_str(), 0};
            XdeltaImportScan *result = nullptr;
            char error[256];
            int ok = XdeltaImport_Scan(&request, &result, &state.progress, &state.cancel, error, sizeof error);
            std::lock_guard<std::mutex> lock(state.mutex);
            state.scan = result;
            state.error = ok ? "" : error;
            state.scanState = ok ? 1 : -2;
        });
        return 1;
    }
    catch (const std::exception &) { s.scanState = -1; return 0; }
}

extern "C" int XdeltaImport_PollScan(int *progress, XdeltaImportSummary *summary, char *error, size_t errorSize)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    if (progress) *progress = load_flag(&s.progress);
    if (s.scanState == 1 && summary && s.scan) *summary = s.scan->summary;
    if (s.scanState == -2 && error && errorSize) std::snprintf(error, errorSize, "%s", s.error.c_str());
    return s.scanState;
}

extern "C" void XdeltaImport_CancelScan(void) { store_flag(&job().cancel, 1); }

extern "C" int XdeltaImport_StartInstall(const unsigned char *selected, unsigned int count, const char *assets, int where)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    if (s.scanState != 1 || !s.scan || s.installState == 0 || !selected || !assets || count != s.scan->summary.count) return 0;
    s.installState = 0;
    try
    {
        std::vector<unsigned char> chosen(selected, selected + count);
        std::string root(assets);
        s.installJob = std::async(std::launch::async, [chosen, root, where] {
            auto &state = job();
            char error[512];
            int n = XdeltaImport_Install(state.scan, chosen.data(), root.c_str(), where, error, sizeof error);
            std::lock_guard<std::mutex> lock(state.mutex);
            state.installed = n < 0 ? 0 : n;
            state.error = n < 0 ? error : "";
            state.installState = n < 0 ? -2 : 1;
        });
        return 1;
    }
    catch (const std::exception &) { s.installState = -1; return 0; }
}

extern "C" int XdeltaImport_PollInstall(int *installed, char *error, size_t errorSize)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    int state = s.installState;
    if (state == 1 || state == -2)
    {
        if (installed) *installed = s.installed;
        if (error && errorSize) std::snprintf(error, errorSize, "%s", s.error.c_str());
        s.installState = -1;
        XdeltaImport_Free(&s.scan);
        s.scanState = -1;
    }
    return state;
}

extern "C" void XdeltaImport_DiscardScan(void)
{
    auto &s = job();
    store_flag(&s.cancel, 1);
    if (s.scanJob.valid()) s.scanJob.wait();
    std::lock_guard<std::mutex> guard(s.mutex);
    if (s.installState == 0) return;
    XdeltaImport_Free(&s.scan);
    s.scanState = -1;
    store_flag(&s.cancel, 0);
}
