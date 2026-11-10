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
#include <platform/native_custom_finish.h>
#include <platform/native_lev_normalize.hpp>
#include <platform/native_xdelta_decode.h>
#include <platform/native_custom_package.h>
extern "C" {
#include <platform/native_custom_content_verify.h> /* C linkage: the header has no guard */
}
#include <platform/native_custom_offline.h>
#include <platform/native_sha256.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
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
/* A changed disc file that starts with a level is kept up to this size: the
   LEV and VRM limits plus room for the map and other data after them. */
const size_t kEmbeddedFileMax = 32u * 1024u * 1024u;
const size_t kEmbeddedFilesMax = 8;

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
bool fs_replace(const std::string &from, const std::string &to)
{
    std::wstring a = wide(from), b = wide(to);
    return !a.empty() && !b.empty() && MoveFileExW(a.c_str(), b.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
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
bool fs_replace(const std::string &from, const std::string &to) { return !std::rename(from.c_str(), to.c_str()); }
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

/* SHA-256 of an open file from its start, leaving it rewound. Hashing the
   handle that is then read keeps the hashed bytes the applied bytes. */
std::string hash_stream(FILE *file, const std::string &path, unsigned long long *bytes, const volatile int *cancel)
{
    std::rewind(file);
    NativeSha256Ctx context;
    NativeSha256_Init(&context);
    std::vector<unsigned char> buffer(1 << 20);
    unsigned long long total = 0;
    size_t got;
    while ((got = std::fread(buffer.data(), 1, buffer.size(), file)) > 0)
    {
        if (load_flag(cancel)) throw std::runtime_error("Cancelled");
        NativeSha256_Update(&context, buffer.data(), got);
        total += got;
    }
    if (std::ferror(file)) throw std::runtime_error("Cannot read " + path.substr(path.find_last_of("/\\") + 1));
    std::rewind(file);
    unsigned char digest[32];
    char text[65];
    NativeSha256_Final(&context, digest);
    NativeSha256_ToHex(digest, text);
    if (bytes) *bytes = total;
    return text;
}

/* Size and modification time of an open file, to notice an edit in place while it is read. */
std::string file_stamp(FILE *file)
{
#ifdef _WIN32
    struct _stat64 info;
    if (_fstat64(_fileno(file), &info)) return "?";
    return std::to_string((long long)info.st_size) + "|" + std::to_string((long long)info.st_mtime);
#else
    struct stat info;
    if (fstat(fileno(file), &info)) return "?";
    return std::to_string((long long)info.st_size) + "|" + std::to_string((long long)info.st_mtim.tv_sec) + "|" +
           std::to_string((long long)info.st_mtim.tv_nsec);
#endif
}

std::string hash_file(const std::string &path, unsigned long long *bytes, const volatile int *cancel)
{
    File file = open_read(path);
    return hash_stream(file.get(), path, bytes, cancel);
}

/* ---- Cutter version 1 (the 2026-09-28 and 2026-10-15 research scripts). */
bool level_header_bounded(const unsigned char *p, uint32_t map);

bool unsupported_lev_map(const unsigned char *p, size_t size)
{
    if (size < 0x200) return false;
    const uint32_t map = rd32(p);
    if ((map & 3) || map < 0x190 || 8ull + map > size) return false;
    if (!(rd32(p + 4 + map) & 0xe0000000u)) return false;
    return level_header_bounded(p, map);
}

/* Require bounded level structures as well as flag bits, since arbitrary
   data can contain a high-bit word too. Offsets here are relative to the
   level body, which starts after the size word; p holds at least 0x200 bytes. */
bool level_header_bounded(const unsigned char *p, uint32_t map)
{
    if ((map & 3) || map < 0x190) return false;
    const uint32_t mesh = rd32(p + 4);
    const uint32_t count = rd32(p + 4 + 0x0c);
    const uint32_t instances = rd32(p + 4 + 0x10);
    return mesh >= 0x190 && !(mesh & 3) && (unsigned long long)mesh + 0x10 <= map &&
           count > 0 && count <= 65536 && instances >= 0x190 && !(instances & 3) &&
           (unsigned long long)instances + (unsigned long long)count * 0x40 <= map;
}

size_t cut_lev(const unsigned char *p, size_t size);

/* Either level map form, as the scan below recognizes it. */
bool lev_shaped(const std::vector<unsigned char> &b)
{
    return cut_lev(b.data(), b.size()) || unsupported_lev_map(b.data(), b.size());
}

size_t cut_lev(const unsigned char *p, size_t size)
{
    if (size < 0x200) return 0;
    const uint32_t map = rd32(p);
    if (map & 3 || map < 0x190 || (unsigned long long)map + 8 > size) return 0;
    const uint32_t mapBytes = rd32(p + 4 + map);
    if (mapBytes & 0xe0000000u) return 0;
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

std::string path_basename(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
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

/* A disc file outside BIGFILE that starts with a level, kept whole. */
struct EmbeddedFile
{
    std::string path;
    long long scratch = -1;
    uint32_t size = 0;
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

    /* Files that start with a level, kept while their sectors arrive in order. */
    std::map<std::string, std::vector<unsigned char>> capturing;
    std::vector<EmbeddedFile> embedded;

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
                capture_sector(path, file, lba, rawSector);
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

    /* Some packs keep a level, its pointer map and its texture file in one
       disc file their own code loads. Such a file is kept whole when its
       first sector holds a bounded level header; the scan decides later
       whether it changed and what it holds. */
    void capture_sector(const std::string &path, const IsoFile &file, uint32_t lba, const unsigned char *rawSector)
    {
        const unsigned char *data = rawSector + kUserData;
        auto it = capturing.find(path);
        if (lba == file.lba)
        {
            if (it != capturing.end()) { capturing.erase(it); return; } /* Seen twice: never kept. */
            if (file.size < 0x200 || file.size > kEmbeddedFileMax || (rawSector[18] & 0x20) ||
                capturing.size() + embedded.size() >= kEmbeddedFilesMax)
                return;
            for (const auto &e : embedded) if (e.path == path) return;
            const uint32_t map = rd32(data);
            if (8ull + map > file.size || !level_header_bounded(data, map)) return;
            it = capturing.emplace(path, std::vector<unsigned char>()).first;
            it->second.reserve(file.size);
        }
        if (it == capturing.end()) return;
        std::vector<unsigned char> &bytes = it->second;
        if ((unsigned long long)(lba - file.lba) * kSector != bytes.size()) { capturing.erase(it); return; }
        const uint32_t take = std::min<uint32_t>(kSector, file.size - (uint32_t)bytes.size());
        bytes.insert(bytes.end(), data, data + take);
        if (bytes.size() < file.size) return;
        if (scratchEnd + (long long)file.size > kScratchBytesMax) throw std::runtime_error("Patch changes too much track data");
        if (seek64(scratch, (unsigned long long)scratchEnd) || std::fwrite(bytes.data(), 1, bytes.size(), scratch) != bytes.size())
            throw std::runtime_error("Cannot write the import scratch file");
        embedded.push_back({path, scratchEnd, file.size});
        scratchEnd += file.size;
        capturing.erase(it);
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
        capturing.clear(); /* Cut short: never kept. */
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
/* Retail adventure hubs: five groups of VRM, LEV and pointer map (BI_ADVENTUREHUB). */
const uint32_t kHubFirst = 200;

bool code_file(const std::string &path)
{
    std::string upper = path;
    for (char &c : upper) if (c >= 'a' && c <= 'z') c -= 32;
    auto ends = [&](const char *s) { size_t n = std::strlen(s); return upper.size() >= n && upper.compare(upper.size() - n, n, s) == 0; };
    return upper.find("SCUS_") != std::string::npos || ends(".BIN") || ends(".EXE") || ends(".CNF");
}

/* Where a level found inside a changed disc file came from. */
struct EmbeddedSource
{
    std::string file, fileSha256;
    uint32_t levOffset = 0, mapOffset = 0, vrmOffset = 0;
};

struct Built
{
    std::string manifest, pin;
    std::string originalLevHash;
    size_t originalLevBytes = 0;
    /* The BIGFILE entry the VRM came from, when it is not the entry before the LEV. */
    uint32_t followingVrm = 0;
    EmbeddedSource embedded; /* file empty for BIGFILE pairs */
    CustomPackageOwned *owned = nullptr;
    /* From the pack descriptor, for the Play Solo descriptor. */
    std::string author;
    bool excludeRelic = false, excludeTokens = false;
    /* For XdeltaImport_RenameItem: what build_package needs again. */
    TrackMeta meta;
    bool hasMeta = false;
    std::string version;
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
    std::string patchStem;    /* The patch file's name without its extension (A3-20). */
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

/* Known custom track names, keyed by the SHA-256 of the cut LEV the importer builds.
   Source: names that two packs load from their own code, matched to the LEV hashes of
   their patched discs (LRG2Pancho overlay table, nightmarefueled loader slot table;
   Opus handoff 2026-11-04). The four adventure-template packs are left out on
   purpose: their template readings disagree on 11 shared hashes. BOSSn is shown as
   "Boss race n". */
const struct { const char *lev; const char *name; } kKnownTracks[] = {
    {"001a3a3a18c74e0b1b11ddf5f1167e767381ce97af3e6cea971e3b047af749eb", "Dark Ruins"},
    {"14ab262ac2aaf3f0157b8d2d39b27bcfe510657fbe7136b42921e49a8ad98e80", "Luigis Mansion"},
    {"256d1ed4597aacd081d2806130c28969681ce35df5a5ba53200545cbd1835eb8", "Ancient Lake"},
    {"27e41e0e9c429c500ec9197fb18f66418e395d8d50731c72ba525547a39c59b6", "Neonstadion"},
    {"29e821ce632c9f483d3e0711e41cc077ab087a8a6044b7a70db826b7b40e71d0", "Pepsi City"},
    {"407d6c9b388d96017bd3e45ee0c713a2319cc8103b1bdd6249cebe5c141b6d11", "Haunted Hovel"},
    {"4232d4396e59df560678f3d0a370ce874649821ad614139b067250494af8f44c", "Sewer Baby Park"},
    {"4242e1c377f56d2d7bfe51ae447b5a5fceb6cc55005712c7ac1d657c1b892b31", "Knightmare A Thon"},
    {"52f446ecde445dcd686afa7ae946349175902ac169399d7c33bf0c568a669e9a", "Boss race 3"},
    {"557d952a537b9c481c3153dae1db936d06b3b184968d7024b08fef62e4766ba1", "Lunatic Laboratory"},
    {"6598755fd3d9b3172bc7a27af250bcf3cb99dd8b709ee5a6e9a2409ee7ef54ec", "Big Forest"},
    {"727508b8e1f5d6c3ae401a4979bdb690054b7c52ed69bbec1a3f2376d8e95eb8", "Boss race 1"},
    {"72bf846cd1932a058f5a379f577dd50624fa94527150a1d6fc3e29cdd73013f8", "Dracula's Castle"},
    {"7dd312574cda7a61b0dc216b9667a29d9a65d09bde64a18b8830d8b895b8b103", "Tomb Temple"},
    {"82a6d81942050ad29370033ccabc111b1731f24cf42e103afab12fd42a79f14c", "Boss race 2"},
    {"83406e87505616994f5b539666f1327597212058506c29d66396aac15448529e", "Snowy Valley"},
    {"8403e3f99ff4927accd15d6bb413181883316d2f1902b9e14bf2ab4eb3b1f0fa", "Toasty Terrace"},
    {"8e040bd120bd964134730cce2a9fa2869922e75bbe71f703b863a16c38b730c4", "Diwali"},
    {"9471beda2e86cbde461aa79ff8e9fec358cfdd1ae86760872a82f4a19202ca1f", "Dark Forest Dash"},
    {"985663bf855f45fd8b71fe23ada322effb738c5fcf55cbd6c2710f95c631b112", "Sid's House"},
    {"9bba2b381dc77f8dbe208ef140446c77f4119b3f9b713916c0e11f5b7ade3d8a", "Chasqui Rally"},
    {"a24c255c87d062b907dfd052a5691ff1b13875adac3515764ad8e64234705fd0", "Garden Speedway"},
    {"a4cfe29c0e3fc7e309d7cfb7c2eeb1fb38bf9359b343d236b46b0c2aedd709e2", "The Dairy Track"},
    {"abd0f1efa42350fe95727e06bb18c5616d8a9bf41a5cb4fd253effe09b62b1ad", "Acid Lake"},
    {"afe36db1745f65290c551090515bd742d1ecb0e84d97d7267ae330be9d3160e9", "Country Speedway"},
    {"b29fe06e98a103ae27a7b8fb1f34571e9ebfb8f59e913fa4f837d7b62ba0df00", "N64 Bowser Castle"},
    {"c7cb55dd24a5fdd2a868968a14e1881866e7e97c9cc9cefb208dd394298ebd17", "Specter Station"},
    {"e06ce795c969106e8e7fbd87b0c7025a65aacaafd6c71161f9af390f37e2e54a", "Boss race 4"},
    {"e22b87a73009b94251f5216424cf07875a02e076eb8461b902624c2b5277ad45", "Tizi Challenge"},
    {"fb7119f056178beff7b48ca13c63a5893b0146ef527973f71b5fc57b3cf80766", "Mario Circuit 1"},
    {"fe6a1495be36eef29724c17653211779e26acf85d730d22d6d015a0cbfb626c8", "Crow Forest"},
};

/* Curated names for adventure patches, keyed by the SHA-256 of the patch file and the
   BIGFILE index of the track (the "slot"). The patch hash is a stable identity: a
   renamed copy still matches, and the same LEV bytes in another pack do not (the
   LEV table above leaves these packs out because their shared hashes disagree).
   Source: tester feedback, entered by hand from the Arcade track list of each
   patch. Boss slots whose real name is not known are left out on purpose, as is
   every slot nobody listed; both keep the generic label. */
const struct { const char *patch; int slot; const char *name; } kCuratedTracks[] = {
    /* 30th Anniversary Adventure */
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 7, "Crazy In Kartmandu"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 15, "N-Gage Android Alley"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 23, "Tomb Temple"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 39, "Skull Cave"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 41, "Arctic Alleyway"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 47, "N-Gage Deep Sea Driving"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 49, "Wumpa World"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 57, "Drive-Thru Danger"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 79, "Crystal Caves"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 119, "Dark Ruins"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 127, "N-Gage Assembly Lane"},
    {"92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2", 135, "N-Gage Out of Time"},
    /* Nightmare Fueled */
    {"1e8a796f019c23f7de70c21a00d9fb20ba4c7be0d819c21de3bd84ffcd8397a1", 7, "Haunted Hovel"},
    {"1e8a796f019c23f7de70c21a00d9fb20ba4c7be0d819c21de3bd84ffcd8397a1", 23, "Dracula's Castle"},
    {"1e8a796f019c23f7de70c21a00d9fb20ba4c7be0d819c21de3bd84ffcd8397a1", 39, "Lunatic Laboratory"},
    {"1e8a796f019c23f7de70c21a00d9fb20ba4c7be0d819c21de3bd84ffcd8397a1", 55, "Sid's House"},
    {"1e8a796f019c23f7de70c21a00d9fb20ba4c7be0d819c21de3bd84ffcd8397a1", 79, "DS Luigi's Mansion"},
    {"1e8a796f019c23f7de70c21a00d9fb20ba4c7be0d819c21de3bd84ffcd8397a1", 119, "LEGO Knightmare-A-Thon"},
    {"1e8a796f019c23f7de70c21a00d9fb20ba4c7be0d819c21de3bd84ffcd8397a1", 135, "LEGO Dark Forest Dash"},
    /* Lockbox Adventure */
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 9, "Matsuri"},
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 31, "DK Shipwreck Shore"},
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 39, "Kong Dungeon"},
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 41, "Breeze Harbor"},
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 47, "Dreamy Heights"},
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 55, "Haven Stadium"},
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 71, "Skater's Retreat"},
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 103, "Neon Paradise"},
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 135, "Redrock Ravine"},
    {"836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180", 169, "Tribal Trials"},
    /* St. Patrick's Day Racing */
    {"748a18082ca0dbc6ff31321e7a75bfa58c3afac80102363fb1385b32e50196df", 31, "Hasty Hedges"},
    {"748a18082ca0dbc6ff31321e7a75bfa58c3afac80102363fb1385b32e50196df", 39, "Holland"},
    {"748a18082ca0dbc6ff31321e7a75bfa58c3afac80102363fb1385b32e50196df", 47, "Motor Sports Land"},
    {"748a18082ca0dbc6ff31321e7a75bfa58c3afac80102363fb1385b32e50196df", 135, "N-Gage Jungle Boogie"},
};

#ifdef CTR_XDELTA_TESTING
std::vector<std::tuple<std::string, int, std::string>> g_testCurated; /* Harness-only extra rows. */
#endif

const char *curated_track_name(const std::string &patchSha256, int bigfileIndex)
{
    for (const auto &k : kCuratedTracks)
        if (k.slot == bigfileIndex && patchSha256 == k.patch) return k.name;
#ifdef CTR_XDELTA_TESTING
    for (const auto &k : g_testCurated)
        if (std::get<1>(k) == bigfileIndex && std::get<0>(k) == patchSha256) return std::get<2>(k).c_str();
#endif
    return nullptr;
}

const char *known_track_name(const std::string &levSha256)
{
    for (const auto &k : kKnownTracks)
        if (levSha256 == k.lev) return k.name;
    return nullptr;
}

/* A readable pack label from a patch file name: separators become spaces,
   a trailing version like "v1.1" or "_2.0" is dropped, the first letter is
   capitalised. Empty when nothing readable is left. */
std::string pack_label(const std::string &stem)
{
    std::string out;
    for (unsigned char c : stem)
    {
        if (c < 32 || c == 127) continue;
        out += (c == '_' || c == '-') ? ' ' : (char)c;
    }
    size_t cut = out.rfind(' ');
    if (cut != std::string::npos && cut + 1 < out.size())
    {
        size_t i = cut + 1;
        const bool v = out[i] == 'v' || out[i] == 'V';
        if (v) i++;
        size_t digits = 0, dots = 0;
        bool only = i < out.size();
        for (size_t k = i; k < out.size(); k++)
        {
            if (std::isdigit((unsigned char)out[k])) digits++;
            else if (out[k] == '.') dots++;
            else only = false;
        }
        /* "v2", "1.1" and "v1.1" are versions; a bare number like "99" is part of the name. */
        if (only && digits && (v || dots)) out.resize(cut);
    }
    std::string squeezed;
    for (char c : out)
        if (!(c == ' ' && (squeezed.empty() || squeezed.back() == ' '))) squeezed += c;
    while (!squeezed.empty() && squeezed.back() == ' ') squeezed.pop_back();
    if (!squeezed.empty() && squeezed[0] >= 'a' && squeezed[0] <= 'z') squeezed[0] = (char)(squeezed[0] - 32);
    return squeezed;
}

struct Candidate
{
    uint32_t index;
    std::vector<unsigned char> lev, vrm;
    std::string originalLevHash;
    size_t originalLevBytes = 0;
    uint32_t followingVrm = 0;
    EmbeddedSource embedded;
};

std::string json_dump(const json &value) { return value.dump(-1, ' ', false, json::error_handler_t::strict); }

/* Build the package for one cut pair and acquire it through the verifier. */
Built build_package(const Candidate &c, const std::string &title, const std::string &version, const TrackMeta *meta,
                    const XdeltaImportSummary &summary, const std::string &author, std::string &failure)
{
    Built b;
    b.originalLevHash = c.originalLevHash;
    b.originalLevBytes = c.originalLevBytes;
    b.followingVrm = c.followingVrm;
    b.embedded = c.embedded;
    const std::string levHash = hex_sha256(c.lev.data(), c.lev.size());
    const std::string vrmHash = hex_sha256(c.vrm.data(), c.vrm.size());
    json source = {{"schema_version", 1}, {"provider", "xdelta"}, {"patch_sha256", summary.patchSha256},
                   {"patch_bytes", summary.patchBytes}, {"base_disc_sha256", summary.discSha256},
                   {"bigfile_index", c.index}, {"cutter_version", CTR_XDELTA_CUTTER_VERSION}};
    if (summary.descriptorSha256[0]) source["descriptor_sha256"] = summary.descriptorSha256;
    if (!c.originalLevHash.empty())
    {
        source["lev_normalizer_version"] = 1;
        source["original_lev_sha256"] = c.originalLevHash;
        source["original_lev_bytes"] = c.originalLevBytes;
        source["normalized_lev_sha256"] = levHash;
        source["normalized_lev_bytes"] = c.lev.size();
    }
    if (c.followingVrm) source["vrm_bigfile_index"] = c.followingVrm;
    if (!c.embedded.file.empty())
        source["disc_file"] = {{"path", c.embedded.file}, {"sha256", c.embedded.fileSha256},
                               {"lev_offset", c.embedded.levOffset}, {"map_offset", c.embedded.mapOffset},
                               {"vrm_offset", c.embedded.vrmOffset}};
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
    if (meta && meta->relic && meta->laps)
    {
        relic = json_dump({{"schema_version", 1}, {"lev_sha256", levHash}, {"vrm_sha256", vrmHash},
                           {"laps", meta->laps}, {"ticks_per_second", 960},
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
        const std::string patchStamp = file_stamp(patch.get());
        set_text(summary.patchSha256, sizeof summary.patchSha256, hash_stream(patch.get(), patchPath, &summary.patchBytes, cancel));
        /* One handle of the disc is hashed, walked and decoded from, so a swapped or edited file cannot differ between them. */
        Disc disc(discPath);
        const std::string discStamp = file_stamp(disc.file.get());
        const std::string discSha = hash_stream(disc.file.get(), discPath, nullptr, cancel);
        if (!request->skipRetailCheck && discSha != kRetailDiscSha256)
            throw std::runtime_error("Your disc is not the retail NTSC-U image this patch needs");
        set_text(summary.discSha256, sizeof summary.discSha256, discSha);
        store_flag(progress, 5);
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
        PassLayout layout;
        std::unique_ptr<Extractor> extractor;
        for (unsigned pass = 0;; pass++)
        {
            if (pass == 3) throw std::runtime_error("Patched disc layout could not be read in three passes");
            extractor.reset(new Extractor(disc, scratch.get(), layout));
            extractor->cancel = cancel;
            std::rewind(patch.get());
            DecodeContext context{patch.get(), disc.file.get(), summary.patchBytes, 0, extractor.get(), progress,
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
        /* Levels found inside changed disc files, under indexes past any BIGFILE
           (CTR_XDELTA_EMBEDDED_INDEX_BASE): VRM at an even index, LEV after it. */
        std::map<uint32_t, std::vector<unsigned char>> embeddedEntries;
        std::map<uint32_t, EmbeddedSource> embeddedSources;
        auto entry_bytes = [&](uint32_t index) -> std::vector<unsigned char> {
            auto virt = embeddedEntries.find(index);
            if (virt != embeddedEntries.end()) return virt->second;
            auto it = byIndex.find(index);
            if (it == byIndex.end()) return disc.entry(index); /* Unchanged: the player's own bytes. */
            if (it->second->scratch < 0) return {};
            std::vector<unsigned char> bytes(it->second->size);
            if (seek64(scratch.get(), (unsigned long long)it->second->scratch) ||
                std::fread(bytes.data(), 1, bytes.size(), scratch.get()) != bytes.size())
                throw std::runtime_error("Cannot read the import scratch file");
            return bytes;
        };
        /* Patched-table bytes for a neighbour: an entry the patch removed or
           emptied is empty, never the retail bytes it replaced. */
        auto patched_entry = [&](uint32_t index) -> std::vector<unsigned char> {
            if (index >= ex.entries.size() || !ex.entries[index].second) return {};
            return entry_bytes(index);
        };
        std::set<uint32_t> consumed;
        std::vector<XdeltaImportItem> unsupportedLevels;
        unsigned ordinal = 0;
        size_t offeredBytes = 0;
        std::map<std::pair<std::string, std::string>, std::string> seen;
        const std::string stem = limit_codepoints(path_stem(patchPath), 100);
        scan->patchStem = path_stem(patchPath);
        std::vector<std::pair<const ChangedEntry *, size_t>> deferred;
        bool deferredPass = false;
        size_t refusalSlot = 0, refusalsInserted = 0;
        auto level_label = [&](uint32_t index) -> std::string {
            auto it = embeddedSources.find(index);
            return it != embeddedSources.end() ? "Level in " + it->second.file : "Level " + std::to_string(index);
        };
        auto consider = [&](const ChangedEntry &c) {
            if (load_flag(cancel)) throw std::runtime_error("Cancelled");
            if (c.scratch < 0) return;
            auto lev = entry_bytes(c.index);
            std::string originalLevHash;
            size_t originalLevBytes = 0;
            bool normalized = false;
            if (unsupported_lev_map(lev.data(), lev.size()))
            {
                originalLevHash = hex_sha256(lev.data(), lev.size());
                originalLevBytes = lev.size();
                std::string why;
                normalized = NativeLev::normalize(lev, why) == NativeLev::Result::Normalized;
                if (!normalized)
                {
                    consumed.insert(c.index);
                    unsupportedLevels.push_back(skip_item((int)c.index, level_label(c.index),
                                                         "Level format not supported: " + why));
                    return;
                }
            }
            size_t levCut = cut_lev(lev.data(), lev.size());
            if (!levCut || c.index == 0)
            {
                if (normalized)
                {
                    consumed.insert(c.index);
                    unsupportedLevels.push_back(skip_item((int)c.index, level_label(c.index),
                        !levCut ? "Files failed checks: normalized level cutter" : "No preceding texture file"));
                }
                return;
            }
            /* A LEV pairs with the VRM before it in its BIGFILE group. A
               LEV-shaped entry without one (racer models and the like) is
               not a track and lands in the grouped "other" line. */
            auto vrm = entry_bytes(c.index - 1);
            size_t vrmCut = cut_vrm(vrm.data(), vrm.size());
            /* Some packs append a level and then its VRM. The entry after is
               tried only when the one before is not a texture file, the patch
               wrote it, it is exactly one VRM, and it does not already lead
               the next level. That pair is kept only when it is offered as a
               race; anything short of that keeps the original refusal. */
            const bool following = !vrmCut && byIndex.count(c.index + 1) && !lev_shaped(patched_entry(c.index + 2));
            /* Following pairs come after every ordinary pair, so they never
               renumber, rename or re-pin a track offered before this rule. */
            if (following && !deferredPass)
            {
                deferred.push_back({&c, unsupportedLevels.size()});
                return;
            }
            if (following)
            {
                vrm = patched_entry(c.index + 1);
                vrmCut = cut_vrm(vrm.data(), vrm.size());
                if (vrmCut != vrm.size() || lev_shaped(vrm)) vrmCut = 0;
            }
            auto refuse_texture = [&]() {
                if (!normalized) return;
                consumed.insert(c.index);
                XdeltaImportItem refusal = skip_item((int)c.index, level_label(c.index), "No valid preceding texture file");
                /* A deferred refusal keeps the diagnostic position it always had. */
                if (deferredPass) unsupportedLevels.insert(unsupportedLevels.begin() + (long)(refusalSlot + refusalsInserted++), refusal);
                else unsupportedLevels.push_back(refusal);
            };
            if (!vrmCut)
            {
                refuse_texture();
                return;
            }
            const uint32_t vrmIndex = following ? c.index + 1 : c.index - 1;
            consumed.insert(c.index);
            const TrackMeta *meta = nullptr;
            if (haveDescriptor && descriptor.tracks.count((int)c.index)) meta = &descriptor.tracks[(int)c.index];
            ordinal++;
            if (byIndex.count(vrmIndex)) consumed.insert(vrmIndex);
            /* A following pair that is not offered leaves no trace but the original refusal. */
            auto undo_following = [&]() {
                ordinal--;
                consumed.erase(c.index);
                consumed.erase(vrmIndex);
                refuse_texture();
            };
            Candidate candidate{c.index, std::vector<unsigned char>(lev.begin(), lev.begin() + (long)levCut),
                                std::vector<unsigned char>(vrm.begin(), vrm.begin() + (long)vrmCut),
                                originalLevHash, originalLevBytes, following ? vrmIndex : 0, {}};
            if (embeddedSources.count(c.index)) candidate.embedded = embeddedSources[c.index];
            lev.clear(); lev.shrink_to_fit(); vrm.clear(); vrm.shrink_to_fit();
            const std::string levHash = hex_sha256(candidate.lev.data(), candidate.lev.size());
            const std::string vrmHash = hex_sha256(candidate.vrm.data(), candidate.vrm.size());
            /* Name sources in order: the pack descriptor, the curated table of
               tester names (patch hash and slot), the known-track table,
               then a plain label the player can edit before installing. */
            std::string title;
            int nameSource = XDELTA_NAME_LABEL;
            if (meta && !meta->title.empty() && CustomPackage_ValidateTitle(meta->title.data(), meta->title.size()))
            {
                title = meta->title;
                nameSource = XDELTA_NAME_DESCRIPTOR;
            }
            else if (const char *curated = curated_track_name(summary.patchSha256, (int)c.index))
            {
                title = curated;
                nameSource = XDELTA_NAME_TESTER;
            }
            else if (const char *known = known_track_name(levHash))
            {
                title = known;
                nameSource = XDELTA_NAME_KNOWN;
            }
            if (title.empty())
            {
                std::string base = !descriptor.packTitle.empty() && haveDescriptor ? descriptor.packTitle : pack_label(stem);
                if (base.empty()) base = "Imported";
                title = limit_codepoints(base, 100) + " track " + std::to_string(ordinal);
                if (!CustomPackage_ValidateTitle(title.data(), title.size())) title = "Imported track " + std::to_string(ordinal);
            }
            auto duplicate = seen.find({levHash, vrmHash});
            if (duplicate != seen.end())
            {
                if (following) { undo_following(); return; }
                add_item(*scan, skip_item((int)c.index, title, "Same track as " + duplicate->second));
                return;
            }
            if (offeredBytes + candidate.lev.size() + candidate.vrm.size() > kOfferedBytesMax)
            {
                if (following) { undo_following(); return; }
                add_item(*scan, skip_item((int)c.index, title, "Too much for one import"));
                return;
            }
            std::string version = meta && !meta->version.empty() ? meta->version : "1.0.0";
            std::string author = meta && !meta->author.empty() ? meta->author : descriptor.packAuthor;
            std::string failure;
            bool retried = false;
            Built built = build_package(candidate, title, version, meta, summary, author, failure);
            if (!built.owned && meta && (meta->relic || meta->laps || !meta->version.empty()))
            {
                /* A descriptor value the package rules refuse never costs the track. */
                std::string retry;
                built = build_package(candidate, title, "1.0.0", nullptr, summary, author, retry);
                if (built.owned) { failure.clear(); retried = true; }
            }
            built.author = author;
            built.version = retried ? "1.0.0" : version;
            if (meta && !retried) { built.meta = *meta; built.hasMeta = true; }
            built.excludeRelic = meta && meta->excludeRelic;
            built.excludeTokens = meta && meta->excludeTokens;
            XdeltaImportItem item = skip_item((int)c.index, title, "");
            item.nameSource = nameSource;
            set_text(item.levSha256, sizeof item.levSha256, levHash);
            set_text(item.vrmSha256, sizeof item.vrmSha256, vrmHash);
            if (!built.owned)
            {
                if (following) { undo_following(); return; }
                set_text(item.reason, sizeof item.reason, limit_codepoints("Files failed checks: " + failure, 90));
                add_item(*scan, item);
                return;
            }
            CustomContentVerification report;
            CustomPackage_GetPairReport(built.owned, &report);
            char ttReason[64] = {}, arcadeReason[64] = {};
            int timeTrial = CustomOffline_PackageTimeTrialReason(built.owned, ttReason, sizeof ttReason);
            int arcade = CustomOffline_PackageArcadeReason(built.owned, arcadeReason, sizeof arcadeReason);
            const auto &m = report.measured;
            if (!timeTrial)
            {
                if (following) { CustomPackage_Free(&built.owned); undo_following(); return; }
                bool arena = !m.checkpoints && (m.crystals > 0 || (m.spawns >= 2 && m.spawns <= 4 && m.weaponCrates > 0));
                std::string reason = arena ? "Arena: not supported yet"
                                   : !m.checkpoints ? "Not a race track (no lap checkpoints)"
                                   : "Files failed checks: " + std::string(ttReason);
                set_text(item.reason, sizeof item.reason, reason);
                CustomPackage_Free(&built.owned);
                add_item(*scan, item);
                return;
            }
            /* A level from a disc file whose checkpoints never lead back to the
               start is a course its pack's own code ends (Coco Team Racing's
               TRIBAL.BIN runs 0 to 181 and stops). A lap race there cannot
               finish, so it is refused unless an exact finish rule exists. */
            if (embeddedSources.count(c.index) && m.finishGap == (unsigned long)-1 &&
                CustomFinish_Terrain(item.levSha256, item.vrmSha256) < 0)
            {
                set_text(item.reason, sizeof item.reason, "Course has no lap loop: its pack's code ends the race");
                CustomPackage_Free(&built.owned);
                add_item(*scan, item);
                return;
            }
            if (const char *blocked = CustomFinish_ImportBlock(item.levSha256, item.vrmSha256))
            {
                if (following) { CustomPackage_Free(&built.owned); undo_following(); return; }
                set_text(item.reason, sizeof item.reason, blocked);
                CustomPackage_Free(&built.owned);
                add_item(*scan, item);
                return;
            }
            item.finishGap = m.finishGap;
            if (m.finishGap > CTR_CCV_FINISH_GAP_MAX)
                std::snprintf(item.warning, sizeof item.warning, "Gap: %lu\n%s", m.finishGap,
                              CustomFinish_Terrain(item.levSha256, item.vrmSha256) >= 0 ? "1-lap finish" : "Check finish");
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
                if (following) { CustomPackage_Free(&built.owned); undo_following(); return; }
                set_text(item.reason, sizeof item.reason, "No folder name fits this title");
                CustomPackage_Free(&built.owned);
                add_item(*scan, item);
                return;
            }
            if (following && scan->summary.count >= CTR_XDELTA_ITEM_MAX - 1)
            {
                /* No room for its row: a following pair is then never counted. */
                CustomPackage_Free(&built.owned);
                undo_following();
                return;
            }
            item.offered = 1;
            offeredBytes += candidate.lev.size() + candidate.vrm.size();
            seen[{levHash, vrmHash}] = title;
            add_item(*scan, item, std::move(built));
        };
        for (const auto &c : ex.changed) consider(c);
        deferredPass = true;
        for (const auto &d : deferred)
        {
            refusalSlot = d.second;
            consider(*d.first);
        }

        /* 4b. A level inside a changed disc file, after every BIGFILE pair so
           no earlier track is renumbered, renamed or re-pinned. The one shape
           read: the level with an empty map of its own, then its tagged delta
           map at the next sector, then its VRM at the sector after the map,
           with only zero padding between them. The joined level then takes
           the normalizer, cutter, verifier and race checks every entry takes. */
        std::set<std::string> embeddedOffered;
        {
            std::vector<const EmbeddedFile *> files;
            for (const auto &e : ex.embedded)
                if (ex.fileChanged.count(e.path) && ex.fileChanged[e.path]) files.push_back(&e);
            std::sort(files.begin(), files.end(), [](const EmbeddedFile *a, const EmbeddedFile *b) { return a->path < b->path; });
            std::vector<ChangedEntry> virtualEntries;
            virtualEntries.reserve(files.size());
            uint32_t next = CTR_XDELTA_EMBEDDED_INDEX_BASE;
            for (const EmbeddedFile *e : files)
            {
                if (load_flag(cancel)) throw std::runtime_error("Cancelled");
                std::vector<unsigned char> b(e->size);
                if (seek64(scratch.get(), (unsigned long long)e->scratch) || std::fread(b.data(), 1, b.size(), scratch.get()) != b.size())
                    throw std::runtime_error("Cannot read the import scratch file");
                const uint32_t map = rd32(b.data());
                if (rd32(b.data() + 4 + map)) continue; /* Its own map is not empty: not this shape. */
                const uint32_t vrmIndex = next, levIndex = next + 1;
                next += 2;
                const std::string file = e->path.substr(1);
                auto zeros = [&](size_t from, size_t to) {
                    return from <= to && to <= b.size() && std::all_of(b.begin() + (long)from, b.begin() + (long)to, [](unsigned char x) { return !x; });
                };
                auto aligned = [](size_t at) { return (at + kSector - 1) / kSector * kSector; };
                const size_t mapAt = aligned(8ull + map);
                const size_t mapEnd = zeros(8ull + map, mapAt) ? NativeLev::tagged_stream_end(b.data(), b.size(), mapAt) : 0;
                if (!mapEnd)
                {
                    unsupportedLevels.push_back(skip_item((int)levIndex, "Level in " + file, "Level format not supported: no pointer map after it"));
                    continue;
                }
                const size_t vrmAt = aligned(mapEnd);
                size_t vrmCut = zeros(mapEnd, vrmAt) && vrmAt < b.size() ? cut_vrm(b.data() + vrmAt, b.size() - vrmAt) : 0;
                std::vector<unsigned char> vrm(b.begin() + (long)vrmAt, b.begin() + (long)(vrmAt + vrmCut));
                if (!vrmCut || lev_shaped(vrm))
                {
                    unsupportedLevels.push_back(skip_item((int)levIndex, "Level in " + file, "No valid texture file after its map"));
                    continue;
                }
                std::vector<unsigned char> lev(b.begin(), b.begin() + 4 + (long)map);
                lev.insert(lev.end(), b.begin() + (long)mapAt, b.begin() + (long)mapEnd);
                embeddedSources[levIndex] = {file, hex_sha256(b.data(), b.size()), 0, (uint32_t)mapAt, (uint32_t)vrmAt};
                embeddedEntries[vrmIndex] = std::move(vrm);
                const size_t levBytes = lev.size();
                embeddedEntries[levIndex] = std::move(lev);
                b.clear(); b.shrink_to_fit();
                ChangedEntry c;
                c.index = levIndex;
                c.size = (uint32_t)levBytes;
                c.scratch = 0;
                virtualEntries.push_back(c);
                const unsigned before = scan->summary.count;
                consider(virtualEntries.back());
                for (unsigned i = before; i < scan->summary.count; i++)
                    if (scan->summary.items[i].offered && scan->summary.items[i].bigfileIndex == (int)levIndex) embeddedOffered.insert(e->path);
                embeddedEntries.erase(vrmIndex);
                embeddedEntries.erase(levIndex);
            }
        }

        /* 4c. A replaced adventure hub: retail keeps each hub as VRM, LEV and
           an external pointer map. A hub has no laps, so it is not a race
           track; say so instead of counting it with other files. */
        {
            static const char *const hubs[] = {"Gem Stone Valley", "N. Sanity Beach", "The Lost Ruins", "Glacier Park", "Citadel City"};
            for (const auto &c : ex.changed)
            {
                if (c.index < kHubFirst || c.index >= kHubFirst + 3 * 5 || (c.index - kHubFirst) % 3 != 1 || consumed.count(c.index))
                    continue;
                const auto lev = patched_entry(c.index);
                const auto ptr = patched_entry(c.index + 1);
                if (lev.size() < 0x200 || !(rd32(lev.data()) & 0x80000000u) || ptr.size() < 8 || rd32(ptr.data()) & 3 ||
                    4ull + rd32(ptr.data()) != ptr.size())
                    continue;
                for (uint32_t i = c.index - 1; i <= c.index + 1; i++)
                    if (byIndex.count(i)) consumed.insert(i);
                unsupportedLevels.push_back(skip_item((int)c.index, std::string(hubs[(c.index - kHubFirst) / 3]) + " hub",
                                                      "Adventure hub: not a race track"));
            }
        }

        /* 5. Grouped lines for everything that is not a track. */
        std::vector<std::string> code;
        unsigned other = 0;
        for (const auto &f : ex.fileChanged)
            if (f.second && f.first != "/BIGFILE.BIG" && !embeddedOffered.count(f.first))
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
        /* Extra diagnostics must not displace existing rows, especially a
           later importable track when the review list reaches its limit. */
        for (const auto &item : unsupportedLevels) add_item(*scan, item);
        if (scan->dropped)
        {
            XdeltaImportItem more = skip_item(-1, std::to_string(scan->dropped) + " more items", "Too many items for one list");
            scan->summary.items[scan->summary.count++] = more;
            scan->built.push_back(Built());
        }
        store_flag(progress, 100);
        if (file_stamp(patch.get()) != patchStamp || file_stamp(disc.file.get()) != discStamp)
            throw std::runtime_error("The patch or the disc changed while it was being read");
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
   refused otherwise; nothing is ever replaced or removed. Both names are
   checked before anything is written. If the VRM still cannot be placed
   after the LEV was, the verified LEV stays and the error says so: the track
   shows Missing Files and importing again adds the VRM, the same rule as the
   Saphi installer (ap_custom_track_download.cpp). <key> and original/ may not
   be links; the tracks folder itself is the player's configuration and may
   be one, as for Saphi installs. */
/* The error for a pair that failed part way: says which new file stayed. */
std::string room_failure(const std::string &what, bool placedLev, bool placedVrm)
{
    if (placedVrm) return what + ". Its new LEV and VRM stay in place; import again to check them";
    if (placedLev) return what + ". The new LEV stays in place; import again to add the VRM";
    return what;
}

#ifdef CTR_XDELTA_TESTING
/* Harness only: fail the VRM after the LEV is placed. The harness links to it,
   so it stays out of the unnamed namespace. */
} // namespace
extern "C" { int g_xdeltaTestFailRoomVrm; }
namespace {
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
   be renamed or removed (and so swapped for a junction) while the install
   runs. FILE_LIST_DIRECTORY makes the open take part in sharing checks; an
   attributes-only open would not. Write sharing stays allowed because the
   final MoveFileEx opens original/ for writing. Not caught: a process that
   rewrites the target of an existing junction in place during the install,
   as for the Saphi installer. */
bool pin_directory(const std::string &path, bool create, bool allowLink, Handle &out)
{
    std::wstring p = wide(path);
    if (p.empty()) return false;
    if (create) _wmkdir(p.c_str());
    out.h = CreateFileW(p.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
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
                      : CreateFileW(p.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info;
    return out.h != INVALID_HANDLE_VALUE && GetFileInformationByHandle(out.h, &info) &&
           (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
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

/* placed turns true once the name holds the new file, before it is read back. */
void publish_room_file(const std::string &folder, const std::string &name, const std::vector<unsigned char> &bytes,
                       bool &placed)
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
    placed = true;
    if (room_name_state(folder + "/" + name, bytes) != 1) throw std::runtime_error("A track file changed while it was installed");
}

void publish_room_vrm(const std::string &folder, const std::string &name, const std::vector<unsigned char> &bytes,
                      bool &placed)
{
    XDELTA_TEST_FAIL_VRM();
    publish_room_file(folder, name, bytes, placed);
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
    bool placedLev = false, placedVrm = false;
    try
    {
        if (!state[0]) publish_room_file(original, names[0], lev, placedLev);
        if (!state[1]) publish_room_vrm(original, names[1], vrm, placedVrm);
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(room_failure(e.what(), placedLev, placedVrm));
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

/* placed turns true once the name holds the new file, before it is read back. */
void publish_room_file(int folder, const std::string &name, const std::vector<unsigned char> &bytes, bool &placed)
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
    placed = true;
    struct stat now;
    if (fstatat(folder, name.c_str(), &now, AT_SYMLINK_NOFOLLOW) || now.st_dev != mine.st_dev ||
        now.st_ino != mine.st_ino || !same_bytes(out.fd, bytes))
        throw std::runtime_error("A track file changed while it was installed");
}

void publish_room_vrm(int folder, const std::string &name, const std::vector<unsigned char> &bytes, bool &placed)
{
    XDELTA_TEST_FAIL_VRM();
    publish_room_file(folder, name, bytes, placed);
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
    bool placedLev = false, placedVrm = false;
    try
    {
        if (!state[0]) publish_room_file(original.fd, names[0], lev, placedLev);
        if (!state[1]) publish_room_vrm(original.fd, names[1], vrm, placedVrm);
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(room_failure(e.what(), placedLev, placedVrm));
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

/* XdeltaImport_Install, noting in `done` each item it installed. */
/* An import marks its package as the newest install of that track, also when
   the package was already there: the same patch track imported again under
   another name (or back under an earlier one) lists as one Play Solo row, the
   newest install's (A3-29). A package that cannot be marked stays listed. */
void mark_newest_install(const std::string &assets, const std::string &pin)
{
    std::error_code ec;
    const auto manifest = std::filesystem::u8path(assets) / "tracks" / "packages" / std::filesystem::u8path(pin) / "manifest.json";
    std::filesystem::last_write_time(manifest, std::filesystem::file_time_type::clock::now(), ec);
}

/* ---- A re-import replaces the old copy that still has alpha1's generic title
   (A3-20). Alpha1 titled a track without a name "<patch name> slot <BIGFILE
   index>" ("Imported track slot N" when that was no title). Only a copy of the
   same patch track (same package uuid, patch SHA-256 and slot) with such a
   title is replaced; a copy the player named is never touched. A copy a solo
   room still refers to stays, and the import review says so. */
std::string g_soloRoot;
std::mutex &solo_root_mutex() { static auto *m = new std::mutex(); return *m; }

std::string solo_root(const std::string &assets)
{
    {
        std::lock_guard<std::mutex> lock(solo_root_mutex());
        if (!g_soloRoot.empty()) return g_soloRoot;
    }
    size_t cut = assets.find_last_of("/\\");
    if (cut == std::string::npos) return "solo";
    return assets.substr(0, cut) + "/solo";
}

/* Alpha1's title of a track without a name: "<patch file name> slot <index>",
   or "Imported track slot <index>" when that was no valid title. A name the
   player typed that only ends in a slot number does not match. */
bool generic_slot_title(const std::string &title, int bigfileIndex, const std::string &patchStem)
{
    const std::string tail = " slot " + std::to_string(bigfileIndex);
    return title == "Imported track" + tail || title == patchStem + tail || title == limit_codepoints(patchStem, 100) + tail;
}

bool read_plain_file(const std::filesystem::path &path, size_t max, std::string &out)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, ec)) || ec) return false;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > max) return false;
    FILE *f = std::fopen(path.string().c_str(), "rb");
    if (!f) return false;
    out.assign((size_t)size, '\0');
    const bool ok = out.empty() || std::fread(&out[0], 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

struct OldCopy
{
    std::string pin, key, revision, lev, vrm;
    bool pinned = false; /* A room refers to it: it stays. */
};

/* A room refers to a copy when its room.json names the package uuid and the
   copy's track key. A room that cannot be read counts as referring to it. */
bool room_refers_to(const std::string &assets, const std::string &uuid, const std::string &key)
{
    std::error_code ec;
    const std::filesystem::path rooms = std::filesystem::u8path(solo_root(assets)) / "rooms";
    const auto roomsStatus = std::filesystem::symlink_status(rooms, ec);
    if (roomsStatus.type() == std::filesystem::file_type::not_found) return false;
    if (ec || !std::filesystem::is_directory(roomsStatus)) return true;
    int seen = 0;
    for (std::filesystem::directory_iterator it(rooms, ec), end; !ec && it != end; it.increment(ec), seen++)
    {
        if (seen >= 4096) return true;
        std::error_code de;
        const auto roomStatus = it->symlink_status(de);
        if (de) return true;
        if (!std::filesystem::is_directory(roomStatus)) continue;
        const std::filesystem::path file = it->path() / "room.json";
        const auto fileStatus = std::filesystem::symlink_status(file, de);
        if (fileStatus.type() == std::filesystem::file_type::not_found) continue;
        if (de) return true;
        std::string text;
        if (!read_plain_file(file, 16u << 20, text)) return true;
        if (text.find(uuid) != std::string::npos &&
            (text.find("\"" + key + "\"") != std::string::npos || text.find("package/" + key + "/") != std::string::npos))
            return true;
    }
    return !!ec;
}

/* The installed copies of this patch track other than `newPin` that still have
   the generic slot title. */
std::vector<OldCopy> generic_old_copies(const std::string &assets, const std::string &patchSha256, const std::string &patchStem, int bigfileIndex,
                                        const std::string &newPin, const std::string &newKey,
                                        const std::string &newRevision, const std::string &newLev, const std::string &newVrm)
{
    std::vector<OldCopy> found;
    char uuidText[37];
    XdeltaImport_PackageUuid(patchSha256.c_str(), bigfileIndex, uuidText);
    const std::string uuid = uuidText;
    std::error_code ec;
    const std::filesystem::path store = std::filesystem::u8path(assets) / "tracks" / "packages";
    int seen = 0;
    for (std::filesystem::directory_iterator it(store, ec), end; !ec && it != end && seen < 4096; it.increment(ec), seen++)
    {
        const std::string pin = it->path().filename().string();
        std::error_code de;
        if (pin.size() != 64 || pin == newPin || !std::filesystem::is_directory(it->symlink_status(de)) ||
            pin.find_first_not_of("0123456789abcdef") != std::string::npos)
            continue;
        std::string manifestText, sourceText;
        if (!read_plain_file(it->path() / "manifest.json", 1u << 20, manifestText) ||
            hex_sha256(manifestText.data(), manifestText.size()) != pin)
            continue;
        OldCopy c;
        try
        {
            const json m = json::parse(manifestText, nullptr, false);
            if (!m.is_object() || m.value("package_uuid", std::string()) != uuid) continue;
            const std::string title = m.value("title", std::string());
            if (!generic_slot_title(title, bigfileIndex, patchStem)) continue;
            if (!read_plain_file(it->path() / "metadata" / "xdelta-source.json", 1u << 20, sourceText)) continue;
            const json src = json::parse(sourceText, nullptr, false);
            if (!src.is_object() || src.value("patch_sha256", std::string()) != patchSha256 ||
                src.value("bigfile_index", -1) != bigfileIndex)
                continue;
            c.pin = pin;
            /* The room folder key: a pack descriptor's track_key, otherwise the title's. */
            c.key = src.value("track_key", std::string());
            if (c.key.empty() || !track_key_valid(c.key)) c.key = title_track_key(title);
            c.revision = m.value("version", std::string());
            if (m.contains("files") && m["files"].is_array())
                for (const auto &f : m["files"])
                {
                    if (f.value("role", std::string()) == "lev") c.lev = f.value("sha256", std::string());
                    if (f.value("role", std::string()) == "vrm") c.vrm = f.value("sha256", std::string());
                }
        }
        catch (const std::exception &) { continue; }
        /* The new copy answers the same pins when key, version and both files agree. */
        const bool answers = c.key == newKey && c.revision == newRevision && c.lev == newLev && c.vrm == newVrm;
        c.pinned = !answers && room_refers_to(assets, uuid, c.key);
        found.push_back(c);
    }
    return found;
}

/* XDELTA_OLD_* of an offered item: what installing it does to an old copy. */
int old_copy_state(const std::string &assets, const XdeltaImportSummary &summary, const std::string &patchStem,
                   const XdeltaImportItem &item, const Built &b)
{
    if (!item.offered || !b.owned || assets.empty()) return XDELTA_OLD_NONE;
    int state = XDELTA_OLD_NONE;
    for (const OldCopy &c : generic_old_copies(assets, summary.patchSha256, patchStem, item.bigfileIndex, item.pin, item.trackKey,
                                               b.version, item.levSha256, item.vrmSha256))
    {
        if (c.pinned) return XDELTA_OLD_KEEP;
        state = XDELTA_OLD_REPLACE;
    }
    return state;
}

struct ReplacedCopy { std::string oldKey, revision, uuid, newKey, newRevision; };
struct ReplaceOutcome { std::vector<ReplacedCopy> replaced; int kept = 0; };

/* ".replaced-<16 hex of the pin>-<8 hex>": the only folder names this feature stages. */
bool staged_name(const std::string &name)
{
    if (name.size() != 35 || name.compare(0, 10, ".replaced-") != 0 || name[26] != '-') return false;
    for (size_t i = 10; i < name.size(); i++)
        if (i != 26 && !std::isxdigit((unsigned char)name[i])) return false;
    return true;
}

/* Take the replaceable old copies of one installed item out of the store: the
   directory is renamed to a name the store scan ignores, then removed. A copy
   that cannot be taken out stays installed. */
void replace_old_copies(const std::string &assets, const XdeltaImportSummary &summary, const std::string &patchStem,
                        const XdeltaImportItem &item, const Built &b, ReplaceOutcome &out)
{
    char uuid[37];
    XdeltaImport_PackageUuid(summary.patchSha256, item.bigfileIndex, uuid);
    const std::filesystem::path store = std::filesystem::u8path(assets) / "tracks" / "packages";
    const std::vector<OldCopy> copies = generic_old_copies(assets, summary.patchSha256, patchStem, item.bigfileIndex, item.pin,
                                                           item.trackKey, b.version, item.levSha256, item.vrmSha256);
    if (std::any_of(copies.begin(), copies.end(), [](const OldCopy &c) { return !c.pinned; }))
    {
        /* Folders of this feature's own name left by a removal that failed part way. */
        std::error_code sweep;
        for (std::filesystem::directory_iterator it(store, sweep), end; !sweep && it != end; it.increment(sweep))
        {
            std::error_code de;
            if (staged_name(it->path().filename().string()) && std::filesystem::is_directory(it->symlink_status(de)))
                std::filesystem::remove_all(it->path(), de);
        }
    }
    for (const OldCopy &c : copies)
    {
        if (c.pinned) { out.kept++; continue; }
        std::error_code ec;
        /* A name nothing else uses: the rename fails rather than merge into a folder that exists. */
        std::random_device device;
        std::filesystem::path staged;
        for (int attempt = 0; attempt < 8; attempt++)
        {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "%08x", (unsigned)device());
            staged = store / (".replaced-" + c.pin.substr(0, 16) + "-" + suffix);
            if (!std::filesystem::exists(std::filesystem::symlink_status(staged, ec))) break;
            staged.clear();
        }
        if (staged.empty()) continue;
        std::filesystem::rename(store / c.pin, staged, ec);
        if (ec) continue;
        std::filesystem::remove_all(staged, ec);
        out.replaced.push_back({c.key, c.revision, uuid, item.trackKey, b.version});
    }
}

int install_items(const XdeltaImportScan *scan, const unsigned char *selected, const char *assets, int where,
                  char *error, size_t errorSize, std::vector<unsigned int> *done, ReplaceOutcome *replaced = nullptr)
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
        if (where & CTR_XDELTA_INSTALL_PACKAGES)
        {
            mark_newest_install(assets, scan->built[i].pin);
            ReplaceOutcome local;
            replace_old_copies(assets, scan->summary, scan->patchStem, item, scan->built[i], replaced ? *replaced : local);
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
                    /* The package store took this track before the room write failed. */
                    const char *kept = (where & CTR_XDELTA_INSTALL_PACKAGES) ? ". Its package is installed." : "";
                    if (installed) std::snprintf(error, errorSize, "Installed %d, then %s: %s%s", installed, item.title, e.what(), kept);
                    else std::snprintf(error, errorSize, "%s: %s%s", item.title, e.what(), kept);
                }
                return -1;
            }
        }
        installed++;
        if (done) done->push_back(i);
    }
    return installed;
}

/* The apworld's rule for a revision (worlds/ctr/custom_packages.py
   REVISION): a letter or digit, then letters, digits and . _ + -. */
bool revision_valid(const std::string &revision)
{
    if (revision.empty() || revision.size() > 64 || !std::isalnum((unsigned char)revision[0])) return false;
    for (char c : revision)
        if (!std::isalnum((unsigned char)c) && c != '.' && c != '_' && c != '+' && c != '-') return false;
    return true;
}

unsigned long long pinned_bytes(const CustomPackageManifest &manifest, const char *role)
{
    for (size_t i = 0; i < manifest.count; i++)
        if (!std::strcmp(manifest.files[i].role, role)) return manifest.files[i].bytes;
    return 0;
}

/* The author shown for a track whose pack descriptor names none (ruling of
   2026-11-02): "unknown", never a guessed name. */
const char *const kUnknownAuthor = "Unknown";

/* See XdeltaImport_PlanDescriptor. */
bool plan_descriptor(const XdeltaImportSummary &summary, const XdeltaImportItem &item, const Built &b,
                     std::string &out, std::string &why, bool &needsLaps)
{
    needsLaps = false;
    CustomPackageManifest manifest;
    CustomContentVerification report;
    unsigned int laps = 0;
    if (!item.offered || !b.owned || !CustomPackage_GetManifest(b.owned, &manifest) ||
        !CustomPackage_GetPairReport(b.owned, &report) || !report.loadable)
    {
        why = "its files are not verified";
        return false;
    }
    /* Laps and the author are the author's, never guessed (track package
       rule 4). Without laps the descriptor leaves them out and the player
       picks them on Play Solo's Custom Tracks page; without an author it
       says "Unknown" (ruling of 2026-11-02). */
    if (!CustomPackage_GetRaceLaps(b.owned, &laps, nullptr, 0) || laps < 1 || laps > 7)
    {
        laps = 0;
        needsLaps = true;
    }
    /* These exact packages have an authored one-lap finish in the patch
       code. It is known even when no race-settings sidecar was imported. */
    if (CustomFinish_Terrain(item.levSha256, item.vrmSha256) >= 0)
    {
        laps = 1;
        needsLaps = false;
    }
    const std::string author = b.author.empty() ? std::string(kUnknownAuthor) : b.author;
    if (author.size() > 128)
    {
        why = "its author is longer than 128 bytes";
        return false;
    }
    const std::string key = item.trackKey, revision = manifest.version, title = item.title;
    const std::string id = "package/" + key + "/" + revision;
    if (!track_key_valid(key) || !revision_valid(revision) || id.size() > 64 ||
        ("custom/" + key + "/" + revision).size() > 64)
    {
        why = "its track key and version do not fit a package id";
        return false;
    }
    if (title.empty() || title.size() > 128)
    {
        why = "its title is longer than 128 bytes";
        return false;
    }
    if (item.bigfileIndex >= CTR_XDELTA_EMBEDDED_INDEX_BASE)
    {
        why = "a seed cannot name a track inside a disc file yet";
        return false;
    }
    if (item.bigfileIndex < 0 || item.bigfileIndex > 4095)
    {
        why = "its BIGFILE index is out of range";
        return false;
    }
    /* Every capability is measured on the verified pair with the rules the
       client checks a descriptor against on load (ap_content_capability.h),
       so no claim can refuse the track. The pack descriptor may only take
       relic and ctr_tokens away. Relic times are set for the author's laps,
       so a track whose laps the player picks has no relic. */
    const CustomContentMeasurements &m = report.measured;
    CustomPackageRelicTargets targets = {};
    const bool relic = !needsLaps && report.fileAnalysis[CTR_CCV_RELIC_RACE].result == CTR_CCV_DETECTED && !b.excludeRelic &&
                       CustomPackage_GetRelicTargets(b.owned, &targets, nullptr, 0) &&
                       targets.ticks[0] > targets.ticks[1] && targets.ticks[1] > targets.ticks[2] &&
                       targets.ticks[2] >= 1 && targets.ticks[0] <= 960 * 60 * 60;
    const bool wumpa = m.fruitCrates <= 1000000ul && m.looseWumpa <= 1000000ul &&
                       m.fruitCrates * 5 + m.looseWumpa >= 10;
    json caps = {{"time_trial", CustomContentVerify_TimeTrialReason(&report, nullptr, 0) != 0},
                 {"arcade", CustomContentVerify_ArcadeRaceReason(&report, nullptr, 0) != 0},
                 {"relic", relic},
                 {"ctr_tokens", report.fileAnalysis[CTR_CCV_CTR_CHALLENGE].result == CTR_CCV_DETECTED && !b.excludeTokens},
                 {"wumpa", wumpa},
                 {"super_turbo_pads", m.superTurboPads > 0},
                 {"weapon_crates", m.weaponCrates},
                 {"fruit_crates", m.fruitCrates},
                 {"loose_fruit", m.looseWumpa},
                 {"ai_nav", m.navPaths > 0}};
    if (m.checkpoints >= 1 && m.checkpoints <= 255) caps["checkpoints"] = m.checkpoints;
    json files = json::array();
    files.push_back({{"role", "lev"}, {"sha256", item.levSha256}, {"bytes", pinned_bytes(manifest, "lev")},
                     {"format", "ctr-lev"}, {"format_version", 1}});
    files.push_back({{"role", "vrm"}, {"sha256", item.vrmSha256}, {"bytes", pinned_bytes(manifest, "vrm")},
                     {"format", "ctr-vrm"}, {"format_version", 1}});
    json d = {{"id", id}, {"content_id", key}, {"uuid", manifest.uuid}, {"revision", revision},
              {"display_name", title}, {"author", author}, {"capabilities", caps},
              {"files", files}, {"evidence", json::array()},
              {"locator", {{"kind", "xdelta"}, {"patch_sha256", summary.patchSha256}, {"bigfile_index", item.bigfileIndex}}}};
    if (!needsLaps)
        d["laps"] = laps;
    if (relic)
        d["relic_targets"] = {{"sapphire", targets.ticks[0]}, {"gold", targets.ticks[1]}, {"platinum", targets.ticks[2]}};
    out = json_dump(d);
    return true;
}

} // namespace

extern "C" int XdeltaImport_Install(const XdeltaImportScan *scan, const unsigned char *selected,
                                    const char *assets, int where, char *error, size_t errorSize)
{
    return install_items(scan, selected, assets, where, error, errorSize, nullptr);
}

extern "C" int XdeltaImport_PlanDescriptor(const XdeltaImportScan *scan, unsigned int item, char *out, size_t capacity,
                                           char contentId[65], char revision[65], char *why, size_t whySize)
{
    if (why && whySize) why[0] = 0;
    if (!scan || !out || !capacity || item >= scan->summary.count || item >= scan->built.size()) return 0;
    std::string text, reason;
    CustomPackageManifest manifest;
    bool needsLaps = false;
    if (!plan_descriptor(scan->summary, scan->summary.items[item], scan->built[item], text, reason, needsLaps) ||
        !CustomPackage_GetManifest(scan->built[item].owned, &manifest))
    {
        if (why && whySize) std::snprintf(why, whySize, "%s", reason.c_str());
        return 0;
    }
    if (text.size() >= capacity)
    {
        if (why && whySize) std::snprintf(why, whySize, "its descriptor is too long");
        return 0;
    }
    std::memcpy(out, text.c_str(), text.size() + 1);
    if (contentId) std::snprintf(contentId, 65, "%s", scan->summary.items[item].trackKey);
    if (revision) std::snprintf(revision, 65, "%s", manifest.version);
    return needsLaps ? 2 : 1;
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
struct KeptDescriptor { std::string contentId, revision, json; bool needsLaps; };

/* The Play Solo descriptors of the installed items `done`, one per track key
   and version; `note` names the first track without one and why, otherwise
   the first track whose laps the player picks. */
void collect_descriptors(const XdeltaImportScan *scan, const std::vector<unsigned int> &done,
                         std::vector<KeptDescriptor> &kept, std::string &note)
{
    std::set<std::pair<std::string, std::string>> names;
    std::string lapsNote;
    for (unsigned int i : done)
    {
        std::string json, why;
        CustomPackageManifest manifest;
        bool needsLaps = false;
        const XdeltaImportItem &item = scan->summary.items[i];
        if (plan_descriptor(scan->summary, item, scan->built[i], json, why, needsLaps) &&
            CustomPackage_GetManifest(scan->built[i].owned, &manifest))
        {
            /* One descriptor file per track key and version: a second track
               with the same pair would replace the first, and one seed
               cannot list both. The first keeps it. */
            if (names.insert({item.trackKey, manifest.version}).second)
            {
                kept.push_back({item.trackKey, manifest.version, json, needsLaps});
                if (needsLaps && lapsNote.empty())
                    lapsNote = limit_codepoints(std::string(item.title), 40) + ": pick its laps in Play Solo";
                continue;
            }
            why = "another track in this patch has the same track key";
        }
        if (note.empty())
            note = limit_codepoints(std::string(item.title), 40) + " is not on Play Solo: " + why;
    }
    if (note.empty())
        note = lapsNote;
}


/* ---- The record of applied patches (assets/tracks/import-record.json).
   One entry per patch, found by the patch file's SHA-256 so a renamed copy
   still counts; a scan that failed before the hash was known is found by
   file name and size. "tracks" holds the BIGFILE indexes already installed. */
struct PatchRecord
{
    std::string sha256, file;
    unsigned long long bytes = 0;
    unsigned offered = 0;
    std::set<int> tracks;
    std::string failed;
};

std::mutex &record_mutex() { static auto *m = new std::mutex(); return *m; }
std::string record_path(const std::string &assets) { return assets + "/tracks/import-record.json"; }

std::vector<PatchRecord> load_records(const std::string &assets)
{
    std::vector<PatchRecord> list;
    File file(fs_open(record_path(assets), "rb"));
    if (!file) return list;
    try
    {
        unsigned long long size = file_size(file.get());
        if (size > (1u << 20)) return list;
        std::vector<unsigned char> bytes((size_t)size);
        if (size && std::fread(bytes.data(), 1, bytes.size(), file.get()) != bytes.size()) return list;
        const json doc = json::parse(bytes.begin(), bytes.end());
        if (!doc.is_object() || !doc.contains("patches") || !doc["patches"].is_array()) return list;
        for (const auto &e : doc["patches"])
        {
            if (!e.is_object() || list.size() >= 512) continue;
            PatchRecord r;
            if (e.contains("sha256") && e["sha256"].is_string()) r.sha256 = e["sha256"].get<std::string>();
            if (e.contains("file") && e["file"].is_string()) r.file = e["file"].get<std::string>().substr(0, 200);
            if (e.contains("bytes") && e["bytes"].is_number_unsigned()) r.bytes = e["bytes"].get<unsigned long long>();
            if (e.contains("offered") && e["offered"].is_number_unsigned()) r.offered = e["offered"].get<unsigned>();
            if (e.contains("failed") && e["failed"].is_string()) r.failed = e["failed"].get<std::string>().substr(0, 90);
            if (e.contains("tracks") && e["tracks"].is_array())
                for (const auto &t : e["tracks"])
                    if (t.is_number_integer() && r.tracks.size() < 256) r.tracks.insert(t.get<int>());
            if (r.sha256.size() != 64 && !r.sha256.empty()) continue;
            list.push_back(std::move(r));
        }
    }
    catch (const std::exception &) { list.clear(); }
    return list;
}

bool save_records(const std::string &assets, const std::vector<PatchRecord> &list)
{
    json patches = json::array();
    for (const auto &r : list)
    {
        json e = {{"sha256", r.sha256}, {"file", r.file}, {"bytes", r.bytes}, {"offered", r.offered},
                  {"tracks", std::vector<int>(r.tracks.begin(), r.tracks.end())}};
        if (!r.failed.empty()) e["failed"] = r.failed;
        patches.push_back(std::move(e));
    }
    const std::string text = json_dump({{"version", 1}, {"patches", patches}});
    fs_mkdir(assets + "/tracks");
    const std::string path = record_path(assets), temp = path + ".tmp";
    fs_remove(temp); /* never write through a leftover link */
    {
        File out(fs_open(temp, "wb"));
        if (!out || std::fwrite(text.data(), 1, text.size(), out.get()) != text.size() || std::fflush(out.get())) return false;
    }
    if (fs_replace(temp, path)) return true;
    fs_remove(temp);
    return false;
}

void fill_state(XdeltaPatchState *state, const PatchRecord &r)
{
    *state = {};
    state->offered = (int)r.offered;
    state->imported = (int)r.tracks.size();
    state->state = r.tracks.empty() ? (r.failed.empty() ? XDELTA_STATE_NEW : XDELTA_STATE_FAILED)
                 : (r.offered && r.tracks.size() >= r.offered) ? XDELTA_STATE_IMPORTED
                 : XDELTA_STATE_PARTLY;
    set_text(state->reason, sizeof state->reason, r.failed);
}

/* The record entry that belongs to this patch, or null. */
PatchRecord *find_record(std::vector<PatchRecord> &list, const std::string &sha, const std::string &file, unsigned long long bytes)
{
    if (!sha.empty())
        for (auto &r : list)
            if (r.sha256 == sha) return &r;
    for (auto &r : list)
        if (r.sha256.empty() && r.file == file && r.bytes == bytes) return &r;
    return nullptr;
}

/* Note the outcome of a scan or install. `installed` are BIGFILE indexes this
   install wrote; `offered` is the review list's count (0 keeps the old one);
   `failure` is "" on success. Never throws. */
void record_outcome(const std::string &assets, const std::string &sha, const std::string &file, unsigned long long bytes,
                    unsigned offered, const std::vector<int> &installed, const std::string &failure)
{
    try
    {
        std::lock_guard<std::mutex> lock(record_mutex());
        auto list = load_records(assets);
        PatchRecord *r = find_record(list, sha, file, bytes);
        if (!r)
        {
            list.emplace_back();
            r = &list.back();
        }
        if (r->sha256.empty() && !sha.empty())
        {
            r->sha256 = sha;
            /* The hash is known now: an older entry kept by name and size is superseded. */
            for (size_t i = 0; i < list.size(); i++)
                if (&list[i] != r && list[i].sha256.empty() && list[i].file == file && list[i].bytes == bytes)
                {
                    const size_t at = (size_t)(r - list.data());
                    list.erase(list.begin() + (long)i);
                    r = &list[at > i ? at - 1 : at];
                    break;
                }
        }
        r->file = file.substr(0, 200);
        r->bytes = bytes;
        if (offered) r->offered = offered;
        for (int i : installed) r->tracks.insert(i);
        r->failed = limit_codepoints(failure, 90);
        if (list.size() > 256) list.erase(list.begin(), list.begin() + (long)(list.size() - 256));
        save_records(assets, list);
    }
    catch (const std::exception &) {}
}

/* "Imported before: 3 of 5 tracks" for a scanned patch, or "". */
std::string history_note(const std::string &assets, const std::string &sha)
{
    std::lock_guard<std::mutex> lock(record_mutex());
    auto list = load_records(assets);
    PatchRecord *r = find_record(list, sha, "", 0);
    if (!r || r->sha256 != sha || r->tracks.empty()) return "";
    return "Imported before: " + std::to_string(r->tracks.size()) + (r->offered ? " of " + std::to_string(r->offered) : std::string()) +
           " track" + (r->tracks.size() == 1 && !r->offered ? "" : "s");
}

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
    /* The last install's Play Solo descriptors, and why a track got none. */
    std::vector<KeptDescriptor> descriptors;
    std::string descriptorNote;
    /* The old slot-titled copies the last install replaced or kept (A3-20). */
    ReplaceOutcome replaceOutcome;
    /* The patch being imported, for the record of applied patches. */
    std::string patchName, assetsRoot;
    unsigned long long patchBytes = 0;
    /* Names the player typed, by review row, applied when the install starts. */
    std::map<unsigned, std::string> titles;
    /* The picker's per-file states. */
    std::future<void> folderJob;
    volatile int folderCancel = 0;
    int folderState = -1; /* -1 idle, 0 running, 1 ready */
    std::vector<XdeltaPatchState> folderResult;
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
    s.titles.clear();
    s.patchName = path_basename(patchPath);
    s.assetsRoot = assets;
    {
        long long size = 0, mtime = 0;
        s.patchBytes = fs_stat(patchPath, &size, &mtime) ? (unsigned long long)size : 0;
    }
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
            std::string name, sha;
            unsigned long long bytes;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                name = state.patchName;
                bytes = state.patchBytes;
            }
            if (ok && result)
            {
                sha = result->summary.patchSha256;
                set_text(result->summary.history, sizeof result->summary.history, history_note(root, sha));
                for (unsigned i = 0; i < result->summary.count && i < result->built.size(); i++)
                {
                    try { result->summary.items[i].oldCopy = old_copy_state(root, result->summary, result->patchStem, result->summary.items[i], result->built[i]); }
                    catch (const std::exception &) {}
                }
            }
            else if (std::strcmp(error, "Cancelled"))
                record_outcome(root, "", name, bytes, 0, {}, error);
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
    s.descriptors.clear();
    s.descriptorNote.clear();
    s.replaceOutcome = ReplaceOutcome();
    try
    {
        std::vector<unsigned char> chosen(selected, selected + count);
        std::string root(assets);
        std::map<unsigned, std::string> titles = s.titles;
        s.installJob = std::async(std::launch::async, [chosen, root, where, titles] {
            auto &state = job();
            char error[512];
            std::vector<unsigned int> done;
            ReplaceOutcome outcome;
            int n = 0;
            /* A name the player typed rebuilds that package under the new title first. */
            for (const auto &t : titles)
            {
                char why[96];
                if (t.first < chosen.size() && chosen[t.first] && !XdeltaImport_RenameItem(state.scan, t.first, t.second.c_str(), why, sizeof why))
                {
                    std::snprintf(error, sizeof error, "Name \"%.40s\": %s", t.second.c_str(), why);
                    n = -1;
                    break;
                }
            }
            if (n == 0) n = install_items(state.scan, chosen.data(), root.c_str(), where, error, sizeof error, &done, &outcome);
            {
                std::string name, sha = state.scan ? state.scan->summary.patchSha256 : "";
                unsigned long long bytes;
                unsigned offered = 0;
                std::vector<int> indexes;
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    name = state.patchName;
                    bytes = state.patchBytes;
                }
                if (state.scan)
                {
                    for (unsigned i = 0; i < state.scan->summary.count; i++) offered += state.scan->summary.items[i].offered != 0;
                    for (unsigned i : done) indexes.push_back(state.scan->summary.items[i].bigfileIndex);
                }
                record_outcome(root, sha, name, bytes, offered, indexes, n < 0 ? error : "");
            }
            /* Every installed package, including a partial batch, gets its
               Play Solo descriptor from the same verified snapshot. */
            std::vector<KeptDescriptor> kept;
            std::string note;
            collect_descriptors(state.scan, done, kept, note);
            std::lock_guard<std::mutex> lock(state.mutex);
            state.descriptors = std::move(kept);
            state.descriptorNote = note;
            state.replaceOutcome = std::move(outcome);
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


/* Replace one offered item's title: rebuild its package under the new name
   and recompute its pin and room folder. Returns 1, or 0 with a short why and
   nothing changed. */
extern "C" int XdeltaImport_RenameItem(XdeltaImportScan *scan, unsigned int item, const char *title, char *why, size_t whySize)
{
    auto refuse = [&](const char *text) {
        if (why && whySize) std::snprintf(why, whySize, "%s", text);
        return 0;
    };
    if (why && whySize) why[0] = 0;
    if (!scan || item >= scan->summary.count || !scan->summary.items[item].offered || !scan->built[item].owned || !title)
        return refuse("No such track");
    const std::string name(title);
    if (name.empty() || name.size() > 128) return refuse("Use 1 to 128 characters");
    if (!CustomPackage_ValidateTitle(name.data(), name.size())) return refuse("This name cannot be used");
    XdeltaImportItem &row = scan->summary.items[item];
    Built &old = scan->built[item];
    if (name == row.title) return 1;
    try
    {
        Candidate candidate{(uint32_t)row.bigfileIndex, package_file(old.owned, "lev"), package_file(old.owned, "vrm"),
                            old.originalLevHash, old.originalLevBytes, old.followingVrm, old.embedded};
        std::string key = old.hasMeta && !old.meta.trackKey.empty() ? old.meta.trackKey : title_track_key(name);
        if (!track_key_valid(key)) return refuse("No folder name fits this name");
        std::string failure;
        Built rebuilt = build_package(candidate, name, old.version.empty() ? "1.0.0" : old.version, old.hasMeta ? &old.meta : nullptr,
                                      scan->summary, old.author, failure);
        if (!rebuilt.owned) return refuse(limit_codepoints("Files failed checks: " + failure, 80).c_str());
        rebuilt.author = old.author;
        rebuilt.version = old.version;
        rebuilt.meta = old.meta;
        rebuilt.hasMeta = old.hasMeta;
        rebuilt.excludeRelic = old.excludeRelic;
        rebuilt.excludeTokens = old.excludeTokens;
        CustomPackage_Free(&old.owned);
        old = std::move(rebuilt);
        set_text(row.title, sizeof row.title, name);
        set_text(row.pin, sizeof row.pin, old.pin);
        set_text(row.trackKey, sizeof row.trackKey, key);
        row.nameSource = XDELTA_NAME_EDITED;
        return 1;
    }
    catch (const std::exception &) { return refuse("Could not rebuild the track"); }
}

/* Remember a name for a review row; applied when the install starts. */
extern "C" int XdeltaImport_SetItemTitle(unsigned int item, const char *title)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    if (s.scanState != 1 || !s.scan || s.installState == 0 || item >= s.scan->summary.count || !title) return 0;
    const std::string name(title);
    if (name.empty() || name.size() > 128 || !CustomPackage_ValidateTitle(name.data(), name.size())) return 0;
    if (name == s.scan->summary.items[item].title) s.titles.erase(item);
    else s.titles[item] = name;
    return 1;
}

extern "C" int XdeltaImport_PatchStates(const char *assets, char names[][CTR_XDELTA_NAME_MAX], int count,
                                        struct XdeltaPatchState *out, const volatile int *cancel)
{
    if (!assets || !names || !out || count <= 0) return 0;
    const std::string root(assets), folder = root + "/tracks/import";
    std::vector<PatchRecord> list;
    {
        std::lock_guard<std::mutex> lock(record_mutex());
        list = load_records(root);
    }
    for (int i = 0; i < count; i++)
    {
        out[i] = {};
        if (load_flag(cancel)) return 0;
        long long size = 0, mtime = 0;
        if (!fs_stat(folder + "/" + names[i], &size, &mtime)) continue;
        /* A record is a candidate when its size matches; the hash settles a renamed copy. */
        bool hashed = false, byName = false;
        std::string sha;
        for (const auto &r : list)
        {
            if (r.bytes != (unsigned long long)size) continue;
            if (r.sha256.empty()) { byName |= r.file == names[i]; continue; }
            if (!hashed)
            {
                try { sha = hash_file(folder + "/" + names[i], nullptr, cancel); }
                catch (const std::exception &) { sha.clear(); }
                hashed = true;
            }
            if (sha == r.sha256) { fill_state(&out[i], r); break; }
        }
        if (!out[i].state && !out[i].imported && byName)
            for (const auto &r : list)
                if (r.sha256.empty() && r.file == names[i] && r.bytes == (unsigned long long)size) fill_state(&out[i], r);
    }
    return 1;
}

extern "C" int XdeltaImport_StartPatchStates(const char *assets, char names[][CTR_XDELTA_NAME_MAX], int count)
{
    auto &s = job();
    if (!assets || !names || count <= 0 || count > CTR_XDELTA_FOLDER_MAX) return 0;
    store_flag(&s.folderCancel, 1);
    if (s.folderJob.valid()) s.folderJob.wait();
    std::lock_guard<std::mutex> guard(s.mutex);
    store_flag(&s.folderCancel, 0);
    s.folderState = 0;
    s.folderResult.clear();
    try
    {
        std::string root(assets);
        std::vector<std::string> list(names, names + count);
        s.folderJob = std::async(std::launch::async, [root, list] {
            auto &state = job();
            std::vector<char> flat(list.size() * CTR_XDELTA_NAME_MAX, 0);
            for (size_t i = 0; i < list.size(); i++) std::snprintf(&flat[i * CTR_XDELTA_NAME_MAX], CTR_XDELTA_NAME_MAX, "%s", list[i].c_str());
            std::vector<XdeltaPatchState> out(list.size());
            int ok = XdeltaImport_PatchStates(root.c_str(), (char (*)[CTR_XDELTA_NAME_MAX])flat.data(), (int)list.size(),
                                              out.data(), &state.folderCancel);
            std::lock_guard<std::mutex> lock(state.mutex);
            if (ok && !load_flag(&state.folderCancel))
            {
                state.folderResult = std::move(out);
                state.folderState = 1;
            }
            else state.folderState = -1;
        });
        return 1;
    }
    catch (const std::exception &) { s.folderState = -1; return 0; }
}

extern "C" int XdeltaImport_PollPatchStates(struct XdeltaPatchState *out, int max)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    if (s.folderState == 1 && out)
        for (int i = 0; i < max && i < (int)s.folderResult.size(); i++) out[i] = s.folderResult[(size_t)i];
    return s.folderState;
}

extern "C" void XdeltaImport_RecordResult(const char *assets, const char *patchName, const struct XdeltaImportSummary *summary,
                                          const int *installedIndexes, int installedCount, const char *failure)
{
    if (!assets || !summary) return;
    unsigned offered = 0;
    for (unsigned i = 0; i < summary->count; i++) offered += summary->items[i].offered != 0;
    std::vector<int> installed;
    for (int i = 0; installedIndexes && i < installedCount; i++) installed.push_back(installedIndexes[i]);
    record_outcome(assets, summary->patchSha256, patchName ? patchName : "", summary->patchBytes, offered, installed, failure ? failure : "");
}

#ifdef CTR_XDELTA_TESTING
/* Harness view of the install job's descriptor step: Install, then the
   descriptors it would keep. Returns the install result; *kept and note. */
extern "C" const char *XdeltaImportTest_CuratedName(const char *patchSha256, int bigfileIndex)
{
    return patchSha256 ? curated_track_name(patchSha256, bigfileIndex) : nullptr;
}
extern "C" void XdeltaImportTest_AddCurated(const char *patchSha256, int bigfileIndex, const char *name)
{
    g_testCurated.emplace_back(patchSha256, bigfileIndex, name);
}
extern "C" const char *XdeltaImportTest_KnownName(const char *levSha256) { return levSha256 ? known_track_name(levSha256) : nullptr; }
extern "C" void XdeltaImportTest_History(const char *assets, const char *sha, char *out, size_t size)
{
    std::snprintf(out, size, "%s", history_note(assets, sha).c_str());
}
extern "C" void XdeltaImportTest_PackLabel(const char *stem, char *out, size_t size)
{
    std::snprintf(out, size, "%s", pack_label(stem).c_str());
}

extern "C" int XdeltaImportTest_InstallDescriptors(const XdeltaImportScan *scan, const unsigned char *selected,
                                                   const char *assets, int *kept, int *needsLaps, char *note,
                                                   size_t noteSize)
{
    std::vector<unsigned int> done;
    std::vector<KeptDescriptor> list;
    std::string text;
    char error[512];
    int n = install_items(scan, selected, assets, CTR_XDELTA_INSTALL_ROOM, error, sizeof error, &done);
    collect_descriptors(scan, done, list, text);
    if (kept) *kept = (int)list.size();
    if (needsLaps)
    {
        *needsLaps = 0;
        for (const auto &d : list) *needsLaps += d.needsLaps ? 1 : 0;
    }
    if (note && noteSize) std::snprintf(note, noteSize, "%s", text.c_str());
    return n;
}
#endif

extern "C" int XdeltaImport_InstalledDescriptorCount(void)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    return s.installState == 0 ? 0 : (int)s.descriptors.size();
}

extern "C" int XdeltaImport_InstalledDescriptor(int index, char contentId[65], char revision[65], char *json,
                                                size_t capacity, int *needsLaps)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    if (s.installState == 0 || index < 0 || index >= (int)s.descriptors.size() || !contentId || !revision || !json ||
        s.descriptors[(size_t)index].json.size() >= capacity)
        return 0;
    const auto &d = s.descriptors[(size_t)index];
    std::snprintf(contentId, 65, "%s", d.contentId.c_str());
    std::snprintf(revision, 65, "%s", d.revision.c_str());
    std::memcpy(json, d.json.c_str(), d.json.size() + 1);
    if (needsLaps) *needsLaps = d.needsLaps ? 1 : 0;
    return 1;
}

#ifdef CTR_XDELTA_TESTING
/* Harness only: the XDELTA_OLD_* state the scan job gives an item. */
extern "C" int XdeltaImport_TestOldCopyState(const XdeltaImportScan *scan, unsigned int item, const char *assets)
{
    if (!scan || item >= scan->summary.count || item >= scan->built.size() || !assets) return -1;
    return old_copy_state(assets, scan->summary, scan->patchStem, scan->summary.items[item], scan->built[item]);
}
#endif

extern "C" void XdeltaImport_SetSoloRoot(const char *dir)
{
    std::string root = dir ? dir : "";
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    std::lock_guard<std::mutex> lock(solo_root_mutex());
    g_soloRoot = root;
}

extern "C" int XdeltaImport_ReplacedCount(void)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    return s.installState == 0 ? 0 : (int)s.replaceOutcome.replaced.size();
}

extern "C" int XdeltaImport_ReplacedCopy(int index, char oldKey[65], char revision[65], char uuid[37], char newKey[65], char newRevision[65])
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    if (s.installState == 0 || index < 0 || index >= (int)s.replaceOutcome.replaced.size() || !oldKey || !revision || !uuid || !newKey || !newRevision)
        return 0;
    const auto &r = s.replaceOutcome.replaced[(size_t)index];
    std::snprintf(oldKey, 65, "%s", r.oldKey.c_str());
    std::snprintf(revision, 65, "%s", r.revision.c_str());
    std::snprintf(uuid, 37, "%s", r.uuid.c_str());
    std::snprintf(newKey, 65, "%s", r.newKey.c_str());
    std::snprintf(newRevision, 65, "%s", r.newRevision.c_str());
    return 1;
}

extern "C" int XdeltaImport_KeptOldCopies(void)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    return s.installState == 0 ? 0 : s.replaceOutcome.kept;
}

extern "C" void XdeltaImport_InstalledDescriptorNote(char *note, size_t size)
{
    auto &s = job();
    std::lock_guard<std::mutex> guard(s.mutex);
    if (note && size) std::snprintf(note, size, "%s", s.installState == 0 ? "" : s.descriptorNote.c_str());
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
