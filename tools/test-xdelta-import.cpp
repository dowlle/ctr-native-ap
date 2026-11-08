// gcc -m32 -std=c++17 -Wall -Wextra -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DCTR_XDELTA_TESTING -DUTF8PROC_STATIC -I include -I . -I ap/vendor/json/include -I vendor/utf8proc
// -x c++ tools/test-xdelta-import.cpp platform/native_xdelta_import.cpp platform/native_custom_package.cpp platform/native_custom_package_files.cpp
// -x c platform/native_xdelta_decode.c platform/native_custom_package_owned.c platform/native_custom_content_verify.c platform/native_custom_offline.c platform/native_custom_unicode.c platform/native_custom_track_manager.c vendor/utf8proc/utf8proc.c -x none
// -lstdc++ -lm -lpthread -o /tmp/test-xdelta-import
//
// xdelta track importer (0.3.0). Everything here is synthetic: a small
// generated "disc" with an ISO 9660 directory and a fake BIGFILE, and VCDIFF
// patches written by a tiny encoder in this file (ADD, COPY and RUN with the
// default code table). No retail bytes are used.
//
// Pinned: decoder windows, source window larger than target, adler32 refusal,
// truncated patch, LZMA flag refusal, a patch for another disc; BIGFILE entry
// changed, grown and moved, new and changed files outside BIGFILE, the
// directory placed after the data (second pass), duplicate pairs; tail
// cutting; race, arena and other classification with every skip text; the
// descriptor (titles, laps, relic targets, wrong patch); determinism of pins
// and hashes; install and reinstall; the package UUID recipe.
#include <platform/native_xdelta_import.h>
#include <platform/native_xdelta_decode.h>
#include <platform/native_lev_normalize.hpp>
#include <platform/native_xdelta_review_layout.h>
#include <platform/native_custom_package.h>
extern "C" {
#include <platform/native_custom_track_manager.h>
#include <platform/native_custom_content_verify.h>
}
#include <platform/native_sha256.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>

extern "C" int g_xdeltaTestFailRoomVrm;
#include "ap/ap_solo_logic.hpp"

extern "C" int XdeltaImportTest_InstallDescriptors(const XdeltaImportScan *scan, const unsigned char *selected,
                                                   const char *assets, int *kept, int *needsLaps, char *note,
                                                   size_t noteSize);
extern "C" const char *XdeltaImportTest_KnownName(const char *levSha256);
extern "C" const char *XdeltaImportTest_CuratedName(const char *patchSha256, int bigfileIndex);
extern "C" int XdeltaImport_TestOldCopyState(const XdeltaImportScan *scan, unsigned int item, const char *assets);
extern "C" void XdeltaImportTest_AddCurated(const char *patchSha256, int bigfileIndex, const char *name);
extern "C" void XdeltaImportTest_History(const char *assets, const char *sha, char *out, size_t size);
extern "C" void XdeltaImportTest_PackLabel(const char *stem, char *out, size_t size);
using Bytes = std::vector<unsigned char>;
static int checks, failures;
#define CHECK(cond, name) do { checks++; if (!(cond)) { failures++; std::printf("FAIL %s (line %d)\n", name, __LINE__); } } while (0)

/* Exercise the production wrapper with the retail small-font advance. */
namespace diagnostic_layout {
enum { FONT_SMALL, FONT_BIG };
static std::vector<std::string> lines;
static int bottom;
static int DecalFont_GetLineWidth(const char *s, int) { return (int)std::strlen(s) * 13; }
static void DecalFont_DrawLineOT(const char *s, int, int y, int, int, uint32_t *) { lines.push_back(s); bottom = std::max(bottom, y + 12); }
#include "../game/230/MM_CustomText.c"
static void check(const XdeltaImportItem &item)
{
    const std::string title = " -  " + std::string(item.title);
    lines.clear();
    MM_CustomText_Draw(title.c_str(), 32, 90, 244, 1, 0, nullptr);
    CHECK(lines == std::vector<std::string>{title}, "diagnostic title and index fit the review row");
    lines.clear();
    bottom = 0;
    const auto layout = XdeltaReview_Layout(0, 0, 0, DecalFont_GetLineWidth(item.title, FONT_SMALL) <= 176);
    MM_CustomText_Draw(item.reason, 305, layout.reasonY, 176, layout.reasonLines, 0, nullptr);
    std::string joined;
    for (const auto &line : lines) { if (!joined.empty()) joined += ' '; joined += line; }
    CHECK(joined == item.reason, "complete diagnostic reason fits the detail panel without ellipsis");
    CHECK(bottom <= 64 + layout.panelHeight - 4 && 64 + layout.panelHeight <= 188,
          "refusal text stays inside its panel and above the footer");
    CHECK(70 + layout.titleLines * 12 <= layout.tagY && layout.tagY + 12 <= layout.reasonY,
          "title and Skipped label have their own space above the reason");
    for (const auto state : {XdeltaReview_Layout(1, 0, 0, 1), XdeltaReview_Layout(0, 1, 0, 1), XdeltaReview_Layout(0, 0, 1, 1)})
        CHECK(state.showStatus && state.reasonLines == 2 && state.panelHeight == 108,
              "offered rows, installing and transient feedback retain their status space");
}
}

static void put32(Bytes &b, size_t o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + i] = (unsigned char)(v >> (8 * i)); }
static void put16(Bytes &b, size_t o, uint32_t v) { b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8); }
static void put32be(Bytes &b, size_t o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + i] = (unsigned char)(v >> (24 - 8 * i)); }

/* ---- Synthetic LEV (the shape tools/test-custom-content-verify.c uses). */
enum { START = 0x15, FINISH = 0x23, PU_RANDOM = 8, STATIC_TIME1 = 0x5c, STATIC_TIME2 = 0x64,
       LETTER_C = 0x93, LETTER_T = 0x94, LETTER_R = 0x95, CRYSTAL = 0x60 };
#define BODY_INST 0x220
#define BODY_CHECK 0x500
#define BODY_NAV 0x700
#define BODY_MODELS 0xa80
#define BODY_PTRMAP 0x1200

struct LevSpec { bool race = true; bool crystals = false; bool letters = true; bool relic = true; unsigned seed = 1; size_t junk = 0; };

static Bytes make_lev(const LevSpec &s)
{
    std::vector<uint32_t> models = {START, FINISH, PU_RANDOM};
    if (s.relic) { models.push_back(STATIC_TIME1); models.push_back(STATIC_TIME2); }
    if (s.letters) { models.push_back(LETTER_C); models.push_back(LETTER_T); models.push_back(LETTER_R); }
    if (s.crystals) for (int i = 0; i < 3; i++) models.push_back(CRYSTAL);
    const uint32_t n = (uint32_t)models.size();
    std::vector<uint32_t> slots = {0x10, 0x14c, 0x188, BODY_NAV, BODY_NAV + 4, BODY_NAV + 8};
    for (uint32_t i = 0; i < n; i++) slots.push_back(BODY_INST + i * 0x40 + 0x10);
    const size_t size = 4 + BODY_PTRMAP + 4 + slots.size() * 4;
    Bytes b(size, 0);
    put32(b, 0, BODY_PTRMAP);
    put32(b, 4 + 0x0c, n);
    put32(b, 4 + 0x10, BODY_INST);
    put32(b, 4 + 0x148, s.race ? 35 : 0);
    for (uint32_t i = 0; i < n; i++)
    {
        const size_t model = BODY_MODELS + i * 0x20;
        put32(b, 4 + BODY_INST + i * 0x40 + 0x10, (uint32_t)model);
        put16(b, 4 + model + 0x10, models[i]);
        put32(b, 4 + BODY_INST + i * 0x40 + 0x3c, (models[i] == LETTER_T || models[i] == LETTER_R) ? LETTER_C : models[i]);
    }
    for (int i = 0; i < 8; i++) b[4 + 0x6c + i * 12] = (unsigned char)(i + 1);
    put32(b, 4 + 0x14c, BODY_CHECK);
    if (s.race)
        for (int i = 0; i < 35; i++) { std::memset(&b[4 + BODY_CHECK + i * 12 + 8], 0xff, 4); b[4 + BODY_CHECK + i * 12 + 8] = (unsigned char)((i + 1) % 35); }
    put32(b, 4 + 0x188, BODY_NAV);
    const uint32_t heads[] = {0x740, 0x880, 0x9c0};
    for (int i = 0; i < 3; i++) { put32(b, 4 + BODY_NAV + i * 4, heads[i]); b[4 + heads[i]] = 0xfd; b[4 + heads[i] + 1] = 0xec; b[4 + heads[i] + 2] = 4; }
    put32(b, 4 + BODY_PTRMAP, (uint32_t)slots.size() * 4);
    for (size_t i = 0; i < slots.size(); i++) put32(b, 4 + BODY_PTRMAP + 4 + i * 4, slots[i]);
    put32(b, 4 + 0x1100, s.seed); /* Distinguishes otherwise equal tracks. */
    for (size_t i = 0; i < s.junk; i++) b.push_back((unsigned char)(0xa5 ^ i)); /* Retail bytes left behind. */
    return b;
}

static Bytes make_vrm(unsigned seed, size_t junk)
{
    Bytes b(4, 0);
    put32(b, 0, 0x20);
    Bytes image(20 + 4 * 2 * 2, 0);
    put16(image, 16, 4); put16(image, 18, 2);
    image[20] = (unsigned char)seed;
    Bytes size(4, 0);
    put32(size, 0, (uint32_t)image.size());
    b.insert(b.end(), size.begin(), size.end());
    b.insert(b.end(), image.begin(), image.end());
    b.insert(b.end(), 4, 0);
    for (size_t i = 0; i < junk; i++) b.push_back((unsigned char)(0x5a + i));
    return b;
}

static Bytes alternate_lev(Bytes &ordinary)
{
    put32(ordinary, 4, 0x1f0);
    std::vector<uint32_t> slots{0};
    for (size_t at = BODY_PTRMAP + 8; at < ordinary.size(); at += 4)
        slots.push_back(NativeLev::word(ordinary.data() + at));
    std::sort(slots.begin(), slots.end());
    ordinary.resize(BODY_PTRMAP + 4);
    NativeLev::append(ordinary, uint32_t(slots.size() * 4));
    for (auto s : slots) NativeLev::append(ordinary, s);
    Bytes alt(ordinary.begin(), ordinary.begin() + BODY_PTRMAP + 4);
    NativeLev::append(alt, 0x80000000u | uint32_t(slots.size() * 4));
    uint32_t previous = 0;
    for (auto s : slots)
    {
        uint32_t d = s - previous;
        alt.push_back(static_cast<unsigned char>(d));
        alt.push_back(static_cast<unsigned char>(d >> 8));
        previous = s;
    }
    return alt;
}

/* ---- Synthetic raw MODE2/2352 disc. */
struct DiscSpec
{
    std::vector<Bytes> bigfile;
    std::map<std::string, Bytes> files; /* root files besides BIGFILE.BIG */
    bool dirAfterData = false;
};

static void put_sector(Bytes &disc, uint32_t lba, const unsigned char *data, size_t size)
{
    if (disc.size() < (size_t)(lba + 1) * 2352) disc.resize((size_t)(lba + 1) * 2352, 0);
    unsigned char *s = &disc[(size_t)lba * 2352];
    std::memset(s, 0, 2352);
    s[0] = 0; std::memset(s + 1, 0xff, 10); s[11] = 0;
    s[15] = 2;
    if (data) std::memcpy(s + 24, data, size);
}

static void put_extent(Bytes &disc, uint32_t lba, const Bytes &bytes)
{
    for (size_t at = 0; at < bytes.size() || at == 0; at += 2048)
    {
        size_t take = std::min<size_t>(2048, bytes.size() - at);
        put_sector(disc, lba + (uint32_t)(at / 2048), bytes.empty() ? nullptr : &bytes[at], take);
        if (bytes.empty()) break;
    }
}

static Bytes dir_record(const std::string &name, uint32_t lba, uint32_t size, bool dir)
{
    size_t nl = name.size();
    size_t ln = 33 + nl + ((33 + nl) & 1);
    Bytes r(ln, 0);
    r[0] = (unsigned char)ln;
    put32(r, 2, lba); put32be(r, 6, lba);
    put32(r, 10, size); put32be(r, 14, size);
    r[25] = dir ? 2 : 0;
    r[32] = (unsigned char)nl;
    std::memcpy(&r[33], name.data(), nl);
    return r;
}

static Bytes make_disc(const DiscSpec &spec)
{
    Bytes disc;
    for (uint32_t i = 0; i < 16; i++) put_sector(disc, i, nullptr, 0);
    /* BIGFILE: table, then the entries in order. */
    const uint32_t n = (uint32_t)spec.bigfile.size();
    const uint32_t tableSectors = (8 + 8 * n + 2047) / 2048;
    Bytes table(8 + 8 * n, 0);
    put32(table, 4, n);
    uint32_t at = tableSectors;
    Bytes body;
    for (uint32_t i = 0; i < n; i++)
    {
        put32(table, 8 + 8 * i, at);
        put32(table, 12 + 8 * i, (uint32_t)spec.bigfile[i].size());
        Bytes e = spec.bigfile[i];
        e.resize((e.size() + 2047) / 2048 * 2048, 0);
        body.insert(body.end(), e.begin(), e.end());
        at += (uint32_t)(e.size() / 2048);
    }
    Bytes big = table;
    big.resize((size_t)tableSectors * 2048, 0);
    big.insert(big.end(), body.begin(), body.end());
    const uint32_t bigSize = (uint32_t)(tableSectors * 2048 + body.size());
    uint32_t next = 24;
    uint32_t rootLba = spec.dirAfterData ? 0 : 18;
    std::map<std::string, std::pair<uint32_t, uint32_t>> placed;
    for (const auto &f : spec.files)
    {
        placed[f.first] = {next, (uint32_t)f.second.size()};
        put_extent(disc, next, f.second);
        next += (uint32_t)((f.second.size() + 2047) / 2048 + 1);
    }
    const uint32_t bigLba = next;
    put_extent(disc, bigLba, big);
    next = bigLba + (bigSize + 2047) / 2048 + 2;
    if (spec.dirAfterData) rootLba = next++;
    Bytes root;
    auto add = [&](const Bytes &r) { root.insert(root.end(), r.begin(), r.end()); };
    add(dir_record(std::string(1, '\0'), rootLba, 2048, true));
    add(dir_record(std::string(1, '\1'), rootLba, 2048, true));
    add(dir_record("BIGFILE.BIG;1", bigLba, bigSize, false));
    for (const auto &p : placed) add(dir_record(p.first + ";1", p.second.first, p.second.second, false));
    root.resize(2048, 0);
    put_extent(disc, rootLba, root);
    Bytes pvd(2048, 0);
    pvd[0] = 1; std::memcpy(&pvd[1], "CD001", 5);
    Bytes rec = dir_record(std::string(1, '\0'), rootLba, 2048, true);
    std::memcpy(&pvd[156], rec.data(), 34);
    put_sector(disc, 16, pvd.data(), pvd.size());
    put_sector(disc, 17, nullptr, 0);
    if (disc.size() < (size_t)next * 2352) disc.resize((size_t)next * 2352, 0);
    return disc;
}

/* ---- Minimal VCDIFF encoder (RFC 3284 default code table). */
static void varint(Bytes &out, uint64_t v)
{
    unsigned char tmp[10]; int n = 0;
    tmp[n++] = v & 0x7f; v >>= 7;
    while (v) { tmp[n++] = (unsigned char)(0x80 | (v & 0x7f)); v >>= 7; }
    while (n) out.push_back(tmp[--n]);
}
static size_t varint_size(uint64_t v) { size_t n = 1; while (v >>= 7) n++; return n; }
static uint32_t adler32(const unsigned char *p, size_t n)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) { a = (a + p[i]) % 65521; b = (b + a) % 65521; }
    return b << 16 | a;
}

struct EncodeOptions { size_t window = 64 * 1024; bool adler = true; bool corruptAdler = false; bool lzmaFlag = false;
                       size_t sourceSlack = 0; /* extra source bytes per window */ };

static Bytes encode(const Bytes &source, const Bytes &target, const EncodeOptions &o = EncodeOptions())
{
    Bytes out = {0xd6, 0xc3, 0xc4, 0x00, (unsigned char)(o.lzmaFlag ? 1 : 0)};
    if (o.lzmaFlag) out.push_back(2);
    for (size_t at = 0; at < target.size(); at += o.window)
    {
        const size_t len = std::min(o.window, target.size() - at);
        const unsigned char *t = &target[at];
        Bytes data, inst, addr;
        bool same = at + len <= source.size() && !std::memcmp(&source[at], t, len);
        bool zero = true;
        for (size_t i = 0; i < len; i++) if (t[i]) { zero = false; break; }
        size_t srcPos = 0, srcLen = 0;
        if (same)
        {
            srcPos = at >= o.sourceSlack ? at - o.sourceSlack : 0;
            srcLen = std::min(source.size() - srcPos, len + 2 * o.sourceSlack);
            inst.push_back(19); varint(inst, len); varint(addr, at - srcPos); /* COPY mode 0 */
        }
        else if (zero) { inst.push_back(0); varint(inst, len); data.push_back(0); } /* RUN */
        else { inst.push_back(1); varint(inst, len); data.insert(data.end(), t, t + len); } /* ADD */
        unsigned char ind = (srcLen ? 1 : 0) | (o.adler ? 4 : 0);
        out.push_back(ind);
        if (srcLen) { varint(out, srcLen); varint(out, srcPos); }
        size_t enclen = 1 + varint_size(len) + varint_size(data.size()) + varint_size(inst.size()) + varint_size(addr.size()) +
                        data.size() + inst.size() + addr.size() + (o.adler ? 4 : 0);
        varint(out, enclen);
        varint(out, len);
        out.push_back(0);
        varint(out, data.size()); varint(out, inst.size()); varint(out, addr.size());
        if (o.adler)
        {
            Bytes c(4, 0);
            put32be(c, 0, adler32(t, len) ^ (o.corruptAdler && at == 0 ? 1u : 0u));
            out.insert(out.end(), c.begin(), c.end());
        }
        out.insert(out.end(), data.begin(), data.end());
        out.insert(out.end(), inst.begin(), inst.end());
        out.insert(out.end(), addr.begin(), addr.end());
    }
    return out;
}

static std::string root;
static std::string write_file(const std::string &name, const Bytes &bytes)
{
    std::string path = root + "/" + name;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f || (bytes.size() && std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) || std::fclose(f)) std::exit(2);
    return path;
}

static Bytes read_file(const std::string &path)
{
    Bytes out;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    unsigned char buffer[65536];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) out.insert(out.end(), buffer, buffer + n);
    std::fclose(f);
    return out;
}
static void write_path(const std::string &path, const Bytes &bytes)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f || (bytes.size() && std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) || std::fclose(f)) std::exit(2);
}
static std::string sha_hex(const Bytes &bytes)
{
    NativeSha256Ctx context;
    unsigned char digest[32];
    char text[65];
    NativeSha256_Init(&context);
    NativeSha256_Update(&context, bytes.data(), bytes.size());
    NativeSha256_Final(&context, digest);
    NativeSha256_ToHex(digest, text);
    return text;
}

struct ScanResult { int ok = 0; std::string error; XdeltaImportScan *scan = nullptr; };
static ScanResult scan(const std::string &patch, const std::string &disc, const char *descriptor = nullptr)
{
    ScanResult r;
    XdeltaImportRequest request = {patch.c_str(), disc.c_str(), descriptor, root.c_str(), 1};
    char error[256] = {};
    volatile int progress = 0;
    r.ok = XdeltaImport_Scan(&request, &r.scan, &progress, nullptr, error, sizeof error);
    r.error = error;
    if (r.ok) CHECK(progress == 100, "progress ends at 100");
    return r;
}

static const XdeltaImportItem *item_for(const XdeltaImportSummary *s, int index)
{
    for (unsigned i = 0; i < s->count; i++) if (s->items[i].bigfileIndex == index) return &s->items[i];
    return nullptr;
}
static const XdeltaImportItem *line_with(const XdeltaImportSummary *s, const char *title)
{
    for (unsigned i = 0; i < s->count; i++) if (std::strstr(s->items[i].title, title)) return &s->items[i];
    return nullptr;
}

static bool scratch_clean()
{
    std::string cmd = "ls -a " + root + " | grep -q '^\\.import-'";
    return std::system(cmd.c_str()) != 0;
}

static void test_manifest_fixture()
{
    std::ifstream input("tools/fixtures/package-manifests.json");
    CHECK(input.good(), "shared manifest fixture exists");
    if (!input.good()) return;
    nlohmann::json fixture;
    input >> fixture;
    for (const auto &test : fixture["cases"])
    {
        auto manifest = fixture["base"];
        if (test.contains("set"))
            for (auto it = test["set"].begin(); it != test["set"].end(); ++it)
                manifest[nlohmann::json::json_pointer(it.key())] = it.value();
        if (test.contains("erase"))
            for (const auto &path : test["erase"])
                manifest.erase(path.get<std::string>().substr(1));
        std::sort(manifest["files"].begin(), manifest["files"].end(), [](const auto &a, const auto &b) {
            return a.value("role", "") == b.value("role", "") ? a.value("path", "") < b.value("path", "")
                                                       : a.value("role", "") < b.value("role", "");
        });
        const std::string serialized = manifest.dump();
        NativeSha256Ctx hash;
        unsigned char bytes[32];
        char pin[65], error[256];
        NativeSha256_Init(&hash);
        NativeSha256_Update(&hash, serialized.data(), serialized.size());
        NativeSha256_Final(&hash, bytes);
        NativeSha256_ToHex(bytes, pin);
        CustomPackageManifest parsed;
        const bool admitted = CustomPackage_ParseManifest(serialized.data(), serialized.size(), pin,
                                                          &parsed, error, sizeof error) != 0;
        CHECK(admitted == test["accept"].get<bool>(), test["name"].get<std::string>().c_str());
    }
}

int main()
{
    test_manifest_fixture();
    XdeltaImportItem longestReason = {};
    std::strcpy(longestReason.title, "Level 4095");
    std::strcpy(longestReason.reason, "Level format not supported: pointer slot overlaps map or exceeds payload");
    diagnostic_layout::check(longestReason);
    char tmpl[] = "/tmp/ctr-xdelta-XXXXXX";
    if (!mkdtemp(tmpl)) return 2;
    root = tmpl;

    /* Base disc: 12 groups of VRM, LEV plus filler entries up to the overlay range. */
    DiscSpec base;
    for (unsigned g = 0; g < 6; g++)
    {
        base.bigfile.push_back(make_vrm(100 + g, 0));
        LevSpec s; s.seed = 100 + g;
        base.bigfile.push_back(make_lev(s));
    }
    while (base.bigfile.size() < 230) base.bigfile.push_back(Bytes(300 + base.bigfile.size(), (unsigned char)base.bigfile.size()));
    base.files["SCUS_944.26"] = Bytes(5000, 0x11);
    base.files["MUSIC.XA"] = Bytes(9000, 0x22);
    const Bytes baseDisc = make_disc(base);

    /* Patched disc. */
    DiscSpec patched = base;
    LevSpec race; race.seed = 7; race.junk = 3000;              /* 1: new race track with a retail tail */
    patched.bigfile[0] = make_vrm(7, 900);
    patched.bigfile[1] = make_lev(race);
    LevSpec arena; arena.race = false; arena.crystals = true; arena.seed = 8; /* 3: arena */
    patched.bigfile[3] = make_lev(arena);
    LevSpec menu; menu.race = false; menu.letters = false; menu.relic = false; menu.seed = 9; /* 5: menu scene */
    patched.bigfile[5] = make_lev(menu);
    patched.bigfile[6] = make_vrm(7, 900);                        /* 7: same track as 1 */
    patched.bigfile[7] = make_lev(race);
    LevSpec grown; grown.seed = 10; grown.letters = false; grown.relic = false; grown.junk = 70000; /* 9: grows, moves later entries */
    patched.bigfile[9] = make_lev(grown);
    patched.bigfile[225] = Bytes(4000, 0x33);                     /* overlay 225 changed: code */
    patched.bigfile[150] = Bytes(1200, 0x44);                     /* other content */
    LevSpec model; model.seed = 11;                               /* 201: LEV-shaped, no VRM before it */
    patched.bigfile[201] = make_lev(model);
    patched.files["SCUS_944.26"][10] ^= 1;
    patched.files["ALL.BIN"] = Bytes(3000, 0x55);
    const Bytes patchedDisc = make_disc(patched);

    const std::string discPath = write_file("base.bin", baseDisc);

    /* A normalized course uses the ordinary checks, hashes and install path. */
    {
        DiscSpec changed = base;
        LevSpec spec; spec.seed = 43;
        Bytes ordinary = make_lev(spec);
        const Bytes alt = alternate_lev(ordinary);
        changed.bigfile[1] = alt;
        ScanResult r = scan(write_file("Normalized.xdelta", encode(baseDisc, make_disc(changed))), discPath);
        CHECK(r.ok, "valid alternate course scans");
        if (r.ok)
        {
            const auto *s = XdeltaImport_Summary(r.scan);
            const auto *it = item_for(s, 1);
            CHECK(it && it->offered && sha_hex(ordinary) == it->levSha256,
                  "verifier and identity use normalized bytes");
            if (it && it->offered)
            {
                const unsigned index = unsigned(it - s->items);
                char error[256];
                CHECK(XdeltaImport_RenameItem(r.scan, index, "Normalized course", error, sizeof error),
                      "normalizer provenance survives rename");
                s = XdeltaImport_Summary(r.scan);
                it = item_for(s, 1);
                std::vector<unsigned char> selected(s->count, 0); selected[index] = 1;
                const std::string assets = root + "/normalized-assets";
                mkdir(assets.c_str(), 0755);
                CHECK(XdeltaImport_Install(r.scan, selected.data(), assets.c_str(),
                                          CTR_XDELTA_INSTALL_PACKAGES | CTR_XDELTA_INSTALL_ROOM,
                                          error, sizeof error) == 1, "normalized package installs");
                const std::string folder = assets + "/tracks/packages/" + it->pin;
                CHECK(read_file(folder + "/original/bigfile-1.lev") == ordinary, "installed output equals ordinary reference");
                const Bytes source = read_file(folder + "/metadata/xdelta-source.json");
                const auto provenance = nlohmann::json::parse(source.begin(), source.end(), nullptr, false);
                CHECK(!provenance.is_discarded() && provenance["original_lev_sha256"] == sha_hex(alt) && provenance["original_lev_bytes"] == alt.size() &&
                      provenance["normalized_lev_sha256"] == sha_hex(ordinary) && provenance["normalized_lev_bytes"] == ordinary.size() &&
                      provenance["lev_normalizer_version"] == 1, "original and installed provenance are distinct");
                std::vector<CustomPackageStoreEntry> rows;
                CustomPackage_ScanStore((assets + "/tracks/packages").c_str(),
                    [](const CustomPackageStoreEntry *e, void *c) { if (e->verified) static_cast<std::vector<CustomPackageStoreEntry> *>(c)->push_back(*e); },
                    &rows, error, sizeof error);
                CHECK(rows.size() == 1 && rows[0].local.xdelta, "normalized package reloads and verifies");
            }
        }
        XdeltaImport_Free(&r.scan);
    }

    /* Normalization grants no mode capability and cannot repair bad payloads. */
    {
        DiscSpec changed = base;
        LevSpec spec; spec.seed = 44;
        Bytes ordinary = make_lev(spec);
        put32(ordinary, 4 + 0x188, 0); /* No AI table, TT remains allowed. */
        changed.bigfile[1] = alternate_lev(ordinary);
        spec.seed = 45; spec.race = false;
        ordinary = make_lev(spec);
        changed.bigfile[3] = alternate_lev(ordinary);
        spec.seed = 46; spec.race = true;
        ordinary = make_lev(spec);
        /* A null mapped model passes format checks but fails structure checks. */
        put32(ordinary, 4 + BODY_INST + 0x10, 0);
        changed.bigfile[5] = alternate_lev(ordinary);
        spec.seed = 47;
        ordinary = make_lev(spec);
        changed.bigfile[7] = alternate_lev(ordinary);
        changed.bigfile[6] = Bytes(20, 0); /* No preceding VRM. */
        ScanResult r = scan(write_file("NormalChecks.xdelta", encode(baseDisc, make_disc(changed))), discPath);
        CHECK(r.ok, "normalized refusal cases scan");
        if (r.ok)
        {
            const auto *s = XdeltaImport_Summary(r.scan);
            const auto *tt = item_for(s, 1);
            const auto *norace = item_for(s, 3);
            const auto *badmodel = item_for(s, 5);
            const auto *novrm = item_for(s, 7);
            CHECK(tt && tt->offered && std::strstr(tt->tags, "Race TT") && !std::strstr(tt->tags, "Arcade"),
                  "no AI remains TT-only under unchanged mode rules");
            CHECK(norace && !norace->offered && !std::strcmp(norace->reason, "Not a race track (no lap checkpoints)"),
                  "normal checks refuse absent lap graph");
            CHECK(badmodel && !badmodel->offered && std::strstr(badmodel->reason, "invalid instance model pointer"),
                  "normal verifier refuses invalid payload after normalization");
            CHECK(novrm && !novrm->offered && !std::strcmp(novrm->reason, "No valid preceding texture file"),
                  "normalization never guesses a following texture pair");
        }
        XdeltaImport_Free(&r.scan);
    }

    {
        DiscSpec changed = base;
        LevSpec spec; spec.seed = 48;
        Bytes ordinary = make_lev(spec);
        changed.bigfile[0] = alternate_lev(ordinary);
        ScanResult r = scan(write_file("FirstEntry.xdelta", encode(baseDisc, make_disc(changed))), discPath);
        CHECK(r.ok, "normalized first-entry scan succeeds");
        if (r.ok)
        {
            const auto *it = item_for(XdeltaImport_Summary(r.scan), 0);
            CHECK(it && !it->offered && !std::strcmp(it->reason, "No preceding texture file"),
                  "normalized first entry keeps a specific refusal row");
        }
        XdeltaImport_Free(&r.scan);
    }

    /* Appended level then VRM: the following entry pairs only when the one
       before is no texture, it is exactly one VRM the patch wrote, it does
       not lead the next level, and the pair is offered as a race. */
    {
        DiscSpec changed = base;
        const Bytes filler(500, 0x77);
        auto alt = [](LevSpec spec) { Bytes ordinary = make_lev(spec); return alternate_lev(ordinary); };
        LevSpec spec; spec.seed = 50;
        Bytes followOrdinary = make_lev(spec);
        changed.bigfile.push_back(alternate_lev(followOrdinary)); /* 230: normalized level, 229 is filler */
        changed.bigfile.push_back(make_vrm(50, 0));            /* 231: its VRM */
        changed.bigfile.push_back(filler);                     /* 232 */
        spec.seed = 51;
        changed.bigfile.push_back(make_lev(spec));             /* 233: ordinary map, same rule */
        changed.bigfile.push_back(make_vrm(51, 0));            /* 234 */
        changed.bigfile.push_back(filler);                     /* 235 */
        spec.seed = 52;
        changed.bigfile.push_back(alt(spec));                  /* 236: VRM with bytes after it */
        changed.bigfile.push_back(make_vrm(52, 10));           /* 237 */
        changed.bigfile.push_back(filler);                     /* 238 */
        spec.seed = 53;
        changed.bigfile.push_back(alt(spec));                  /* 239: next VRM leads level 241 */
        changed.bigfile.push_back(make_vrm(53, 0));            /* 240 */
        spec.seed = 54;
        changed.bigfile.push_back(make_lev(spec));             /* 241: ordinary pair with 240 */
        spec.seed = 55;
        Bytes badModel = make_lev(spec);
        put32(badModel, 4 + BODY_INST + 0x10, 0);
        changed.bigfile.push_back(alternate_lev(badModel));    /* 242: fails the package verifier */
        changed.bigfile.push_back(make_vrm(55, 0));            /* 243 */
        changed.bigfile.push_back(filler);                     /* 244 */
        spec.seed = 56; spec.race = false;
        changed.bigfile.push_back(alt(spec));                  /* 245: verifies, no lap checkpoints */
        changed.bigfile.push_back(make_vrm(56, 0));            /* 246 */
        changed.bigfile.push_back(filler);                     /* 247 */
        spec.seed = 57; spec.letters = false; spec.relic = false;
        changed.bigfile.push_back(make_lev(spec));             /* 248: ordinary, not a race: stays silent */
        changed.bigfile.push_back(make_vrm(57, 0));            /* 249 */
        changed.bigfile.push_back(filler);                     /* 250 */
        spec.seed = 58; spec.race = true; spec.letters = true; spec.relic = true;
        changed.bigfile.push_back(alt(spec));                  /* 251: next VRM leads normalized level 253 */
        changed.bigfile.push_back(make_vrm(58, 0));            /* 252 */
        spec.seed = 59;
        changed.bigfile.push_back(alt(spec));                  /* 253: normalized pair with 252 */
        changed.bigfile.push_back(changed.bigfile[230]);       /* 254: same files as 230 and 231 */
        changed.bigfile.push_back(make_vrm(50, 0));            /* 255 */
        changed.bigfile.push_back(filler);                     /* 256 */
        spec.seed = 61;
        changed.bigfile.push_back(alt(spec));                  /* 257: offered after the refused pairs */
        changed.bigfile.push_back(make_vrm(61, 0));            /* 258 */
        changed.bigfile.push_back(filler);                     /* 259 */
        CHECK(XdeltaImport_CutVrm(changed.bigfile[237].data(), changed.bigfile[237].size()) != changed.bigfile[237].size(),
              "fixture: VRM with trailing bytes cuts short");
        ScanResult r = scan(write_file("Following.xdelta", encode(baseDisc, make_disc(changed))), discPath);
        CHECK(r.ok, "following-texture scan succeeds");
        if (r.ok)
        {
            const auto *s = XdeltaImport_Summary(r.scan);
            const auto *normalized = item_for(s, 230);
            const auto *ordinary = item_for(s, 233);
            const auto *leader = item_for(s, 241);
            CHECK(normalized && normalized->offered && sha_hex(followOrdinary) == normalized->levSha256 &&
                  sha_hex(changed.bigfile[231]) == normalized->vrmSha256 && std::strstr(normalized->tags, "Race TT Arcade"),
                  "normalized level pairs with the VRM after it");
            CHECK(ordinary && ordinary->offered && sha_hex(changed.bigfile[234]) == ordinary->vrmSha256,
                  "ordinary level pairs with the VRM after it");
            CHECK(leader && leader->offered && sha_hex(changed.bigfile[240]) == leader->vrmSha256,
                  "a VRM that leads the next level stays with that level");
            const auto *normalLeader = item_for(s, 253);
            const auto *last = item_for(s, 257);
            CHECK(normalLeader && normalLeader->offered && sha_hex(changed.bigfile[252]) == normalLeader->vrmSha256,
                  "a VRM that leads a normalized level stays with that level");
            CHECK(leader && !std::strcmp(leader->title, "Following track 1") && normalLeader && !std::strcmp(normalLeader->title, "Following track 2") &&
                  normalized && !std::strcmp(normalized->title, "Following track 3") && ordinary && !std::strcmp(ordinary->title, "Following track 4"),
                  "following pairs are numbered after every ordinary pair");
            CHECK(last && last->offered && !std::strcmp(last->title, "Following track 5") && !std::strcmp(last->trackKey, "following-track-5"),
                  "a following pair after refused ones takes the next number");
            std::vector<int> refusals;
            for (unsigned i = 0; i < s->count; i++)
                if (!std::strcmp(s->items[i].reason, "No valid preceding texture file")) refusals.push_back(s->items[i].bigfileIndex);
            CHECK(refusals == std::vector<int>({236, 239, 242, 245, 251, 254}), "deferred refusals keep their diagnostic order");
            CHECK(line_with(s, "Following track 3") == normalized, "a duplicate following pair is refused, not listed as the same track");
            for (int slot : {236, 239, 242, 245, 251, 254})
            {
                const auto *it = item_for(s, slot);
                CHECK(it && !it->offered && !it->pin[0] && !std::strcmp(it->reason, "No valid preceding texture file") &&
                      std::string(it->title) == "Level " + std::to_string(slot),
                      "a following pair short of an offered race keeps the original refusal");
            }
            CHECK(!item_for(s, 248), "ordinary non-race level with a following VRM stays out of the list");
            for (int slot : {231, 234, 237, 240, 243, 246, 249, 252, 255, 258})
                CHECK(!item_for(s, slot), "a VRM entry never gets its own row");
            std::set<std::string> titles;
            unsigned offered = 0;
            for (unsigned i = 0; i < s->count; i++)
                if (s->items[i].offered) { offered++; titles.insert(s->items[i].title); }
            CHECK(offered == 5 && titles == std::set<std::string>({"Following track 1", "Following track 2", "Following track 3",
                                                                    "Following track 4", "Following track 5"}),
                  "refused following pairs use no track number");
            /* 237, 243, 246, 248, 249, 255 and seven fillers (232 is in the overlay range):
               refused pairs give their VRM back. */
            CHECK(line_with(s, "Other changed files (13)"), "refused following VRMs stay in the other count");
            if (normalized && normalized->offered)
            {
                const unsigned index = unsigned(normalized - s->items);
                char error[256];
                CHECK(XdeltaImport_RenameItem(r.scan, index, "Following course", error, sizeof error),
                      "following pair survives rename");
                s = XdeltaImport_Summary(r.scan);
                normalized = item_for(s, 230);
                std::vector<unsigned char> selected(s->count, 0); selected[index] = 1;
                const std::string assets = root + "/following-assets";
                mkdir(assets.c_str(), 0755);
                CHECK(XdeltaImport_Install(r.scan, selected.data(), assets.c_str(),
                                          CTR_XDELTA_INSTALL_PACKAGES | CTR_XDELTA_INSTALL_ROOM,
                                          error, sizeof error) == 1, "following pair installs");
                const std::string folder = assets + "/tracks/packages/" + normalized->pin;
                CHECK(read_file(folder + "/original/bigfile-230.lev") == followOrdinary &&
                      read_file(folder + "/original/bigfile-230.vrm") == changed.bigfile[231],
                      "installed files are the level and the VRM after it");
                const Bytes source = read_file(folder + "/metadata/xdelta-source.json");
                const auto provenance = nlohmann::json::parse(source.begin(), source.end(), nullptr, false);
                CHECK(!provenance.is_discarded() && provenance["bigfile_index"] == 230 && provenance["vrm_bigfile_index"] == 231 &&
                      provenance["lev_normalizer_version"] == 1, "provenance names the VRM entry after a rename");
                std::vector<CustomPackageStoreEntry> rows;
                CustomPackage_ScanStore((assets + "/tracks/packages").c_str(),
                    [](const CustomPackageStoreEntry *e, void *c) { if (e->verified) static_cast<std::vector<CustomPackageStoreEntry> *>(c)->push_back(*e); },
                    &rows, error, sizeof error);
                CHECK(rows.size() == 1 && rows[0].local.xdelta, "following pair reloads and verifies");
            }
        }
        XdeltaImport_Free(&r.scan);
    }

    /* The entry after must be written by the patch, and a level the patch
       removed two entries on no longer blocks the pair. */
    {
        DiscSpec before = base;
        const Bytes filler(500, 0x77);
        LevSpec spec; spec.seed = 72;
        before.bigfile.push_back(filler);                      /* 230 */
        before.bigfile.push_back(make_vrm(70, 0));             /* 231: retail-side VRM, unchanged */
        before.bigfile.push_back(filler);                      /* 232 */
        before.bigfile.push_back(filler);                      /* 233 */
        before.bigfile.push_back(filler);                      /* 234 */
        before.bigfile.push_back(make_lev(spec));              /* 235: a level the patch empties */
        const Bytes beforeDisc = make_disc(before);
        DiscSpec after = before;
        spec.seed = 71;
        Bytes ordinary = make_lev(spec);
        after.bigfile[230] = alternate_lev(ordinary);
        spec.seed = 73;
        ordinary = make_lev(spec);
        after.bigfile[233] = alternate_lev(ordinary);
        after.bigfile[234] = make_vrm(73, 0);
        after.bigfile[235] = Bytes();
        ScanResult r = scan(write_file("FollowingNeighbours.xdelta", encode(beforeDisc, make_disc(after))),
                            write_file("following-base.bin", beforeDisc));
        CHECK(r.ok, "following neighbour scan succeeds");
        if (r.ok)
        {
            const auto *s = XdeltaImport_Summary(r.scan);
            const auto *unchanged = item_for(s, 230);
            const auto *emptied = item_for(s, 233);
            CHECK(unchanged && !unchanged->offered && !std::strcmp(unchanged->reason, "No valid preceding texture file"),
                  "a VRM the patch did not write is never paired");
            CHECK(emptied && emptied->offered && sha_hex(after.bigfile[234]) == emptied->vrmSha256,
                  "a removed level two entries on does not block the pair");
        }
        XdeltaImport_Free(&r.scan);
    }

    /* A following pair with no room for its row leaves the overflow count as before. */
    {
        DiscSpec many;
        for (unsigned i = 0; i < 96; i++)
        {
            many.bigfile.push_back(make_vrm(i, 0));
            LevSpec spec; spec.seed = 1000 + i;
            many.bigfile.push_back(make_lev(spec));
        }
        Bytes before = make_disc(many);
        for (unsigned i = 0; i < 96; i++)
        {
            LevSpec spec; spec.seed = 2000 + i;
            many.bigfile[i * 2 + 1] = make_lev(spec);
        }
        LevSpec spec; spec.seed = 3000;
        many.bigfile.push_back(Bytes(500, 0x77));               /* 192 */
        many.bigfile.push_back(make_lev(spec));                 /* 193: ordinary map, VRM after it */
        many.bigfile.push_back(make_vrm(3000, 0));              /* 194 */
        many.bigfile.push_back(Bytes(500, 0x77));               /* 195 */
        ScanResult r = scan(write_file("CapacityFollowing.xdelta", encode(before, make_disc(many))),
                            write_file("capacity-base.bin", before));
        CHECK(r.ok, "following capacity scan succeeds");
        if (r.ok)
        {
            const auto *s = XdeltaImport_Summary(r.scan);
            unsigned offered = 0;
            for (unsigned i = 0; i < s->count; i++) offered += s->items[i].offered != 0;
            /* As on main: the 96th track and the other-files line overflow. */
            CHECK(offered == 95 && !item_for(s, 193) && line_with(s, "2 more items"),
                  "a following pair that cannot be listed is not counted as dropped");
        }
        XdeltaImport_Free(&r.scan);
    }

    /* Ordinary pairs never gain the following-entry provenance key. */
    {
        DiscSpec changed = base;
        LevSpec spec; spec.seed = 60;
        changed.bigfile[0] = make_vrm(60, 0);
        changed.bigfile[1] = make_lev(spec);
        ScanResult r = scan(write_file("Preceding.xdelta", encode(baseDisc, make_disc(changed))), discPath);
        CHECK(r.ok, "preceding-texture scan succeeds");
        if (r.ok)
        {
            const auto *s = XdeltaImport_Summary(r.scan);
            const auto *it = item_for(s, 1);
            CHECK(it && it->offered, "preceding pair is offered");
            if (it && it->offered)
            {
                std::vector<unsigned char> selected(s->count, 0); selected[unsigned(it - s->items)] = 1;
                const std::string assets = root + "/preceding-assets";
                mkdir(assets.c_str(), 0755);
                char error[256];
                CHECK(XdeltaImport_Install(r.scan, selected.data(), assets.c_str(), CTR_XDELTA_INSTALL_PACKAGES, error, sizeof error) == 1,
                      "preceding pair installs");
                const Bytes source = read_file(assets + "/tracks/packages/" + it->pin + "/metadata/xdelta-source.json");
                const auto provenance = nlohmann::json::parse(source.begin(), source.end(), nullptr, false);
                CHECK(!provenance.is_discarded() && !provenance.contains("vrm_bigfile_index"),
                      "preceding pair metadata is unchanged");
            }
        }
        XdeltaImport_Free(&r.scan);
    }

    /* Invalid alternate pointer maps get diagnostic rows, never packages.
       Keep ordinary track numbering independent of these diagnostic entries. */
    {
        DiscSpec alternate = base;
        LevSpec normal; normal.seed = 42;
        alternate.bigfile[1] = make_lev(normal);
        Bytes encoded = make_lev(normal);
        put32(encoded, 4, 0x1f0); /* plausible mesh info before the instance table */
        put32(encoded, 4 + BODY_PTRMAP, 0x80000020u);
        alternate.bigfile[3] = encoded;
        put32(encoded, 4 + BODY_PTRMAP, 0x20000020u);
        alternate.bigfile[5] = encoded; /* another high-bit encoding */
        put32(encoded, 4 + 0x10, BODY_PTRMAP - 4); /* instance table crosses the map */
        alternate.bigfile[7] = encoded;
        encoded.resize(0x200); /* truncated before its map header */
        alternate.bigfile[9] = encoded;
        encoded = make_lev(normal);
        put32(encoded, 4 + BODY_PTRMAP, 0x80000020u); /* no mesh shape */
        alternate.bigfile[11] = encoded;
        CHECK(XdeltaImport_CutLev(alternate.bigfile[3].data(), alternate.bigfile[3].size()) == 0,
              "alternate map cannot be cut as an ordinary LEV");
        ScanResult r = scan(write_file("Alternate.xdelta", encode(baseDisc, make_disc(alternate))), discPath);
        CHECK(r.ok, "alternate-map scan succeeds");
        if (r.ok)
        {
            const auto *s = XdeltaImport_Summary(r.scan);
            for (int slot : {3, 5})
            {
                const auto *it = item_for(s, slot);
                CHECK(it && !it->offered && !it->pin[0] && !it->levSha256[0] && !it->tags[0] &&
                      !std::strncmp(it->reason, "Level format not supported: ", std::strlen("Level format not supported: ")),
                      "alternate map has its own non-importable diagnostic");
                CHECK(it && std::string(it->title) == "Level " + std::to_string(slot),
                      "diagnostic identifies the BIGFILE entry");
                if (it) diagnostic_layout::check(*it);
            }
            const auto *ordinary = item_for(s, 1);
            CHECK(ordinary && ordinary->offered && !std::strcmp(ordinary->title, "Alternate track 1"),
                  "normal entry remains offered with its original ordinal");
            CHECK(!item_for(s, 7) && !item_for(s, 9) && !item_for(s, 11),
                  "flagged arbitrary or truncated data is not diagnosed as a level");
            CHECK(line_with(s, "Other changed files (3)"), "only diagnosed entries leave the other count");
            std::vector<unsigned char> selected(s->count, 0);
            for (unsigned i = 0; i < s->count; i++)
                if (s->items[i].bigfileIndex == 3 || s->items[i].bigfileIndex == 5) selected[i] = 1;
            char error[256];
            CHECK(XdeltaImport_Install(r.scan, selected.data(), (root + "/alternate-assets").c_str(),
                                      CTR_XDELTA_INSTALL_PACKAGES, error, sizeof error) == 0,
                  "selecting only unsupported rows installs nothing");
        }
        XdeltaImport_Free(&r.scan);
    }

    /* The last ordinary candidate must keep its row when diagnostics overflow. */
    {
        DiscSpec many;
        for (unsigned i = 0; i < 96; i++)
        {
            many.bigfile.push_back(make_vrm(i, 0));
            LevSpec spec; spec.seed = 1000 + i;
            many.bigfile.push_back(make_lev(spec));
        }
        Bytes before = make_disc(many);
        for (unsigned i = 0; i < 96; i++)
        {
            LevSpec spec; spec.seed = 2000 + i;
            many.bigfile[i * 2 + 1] = make_lev(spec);
        }
        put32(many.bigfile[189], 4, 0x1f0);
        put32(many.bigfile[189], 4 + BODY_PTRMAP, 0x80000020u);
        ScanResult r = scan(write_file("Capacity.xdelta", encode(before, make_disc(many))),
                            write_file("capacity-base.bin", before));
        CHECK(r.ok, "capacity-boundary scan succeeds");
        if (r.ok)
        {
            const auto *s = XdeltaImport_Summary(r.scan);
            unsigned offered = 0;
            for (unsigned i = 0; i < s->count; i++) offered += s->items[i].offered != 0;
            CHECK(offered == 95, "diagnostics never displace ordinary offered tracks");
            const auto *last = item_for(s, 191);
            CHECK(last && last->offered && !std::strcmp(last->title, "Capacity track 95"),
                  "ordinary track after an alternate map keeps its row and ordinal");
            CHECK(!item_for(s, 189) && line_with(s, "1 more items"), "overflow diagnostic uses the existing overflow row");
        }
        XdeltaImport_Free(&r.scan);
    }

    /* Decoder: windows, copy with a source window larger than the target, adler32. */
    {
        EncodeOptions o; o.window = 8192; o.sourceSlack = 20000;
        Bytes patch = encode(baseDisc, patchedDisc, o);
        struct Sink { Bytes out; const Bytes *src; size_t at = 0; } sink; sink.src = &baseDisc;
        size_t readAt = 0;
        struct Ctx { Bytes *patch; size_t *readAt; Sink *sink; } ctx{&patch, &readAt, &sink};
        NativeXdeltaIo io = {
            [](void *c, void *d, size_t n) -> long { auto *x = (Ctx *)c; size_t t = std::min(n, x->patch->size() - *x->readAt); std::memcpy(d, x->patch->data() + *x->readAt, t); *x->readAt += t; return (long)t; },
            [](void *c, unsigned long long off, void *d, size_t n) -> long { auto *x = (Ctx *)c; const Bytes &s = *x->sink->src; if (off >= s.size()) return 0; size_t t = std::min<size_t>(n, s.size() - off); std::memcpy(d, s.data() + off, t); return (long)t; },
            [](void *c, const void *b, size_t n) -> int { auto *x = (Ctx *)c; x->sink->out.insert(x->sink->out.end(), (const unsigned char *)b, (const unsigned char *)b + n); return 1; },
            &ctx, baseDisc.size(), nullptr};
        char error[256];
        CHECK(NativeXdelta_Decode(&io, error, sizeof error) == NATIVE_XDELTA_OK, "multi-window decode succeeds");
        CHECK(sink.out == patchedDisc, "decoded target equals the patched disc");
    }

    const std::string patchPath = write_file("Pack.xdelta", encode(baseDisc, patchedDisc));
    ScanResult a = scan(patchPath, discPath);
    if (!a.ok) std::printf("scan error: %s\n", a.error.c_str());
    CHECK(a.ok, "scan of the synthetic patch succeeds");
    if (a.ok)
    {
        const XdeltaImportSummary *s = XdeltaImport_Summary(a.scan);
        CHECK(s->passes == 1, "directory before data needs one pass");
        const XdeltaImportItem *t1 = item_for(s, 1);
        CHECK(t1 && t1->offered, "changed race LEV is offered");
        CHECK(t1 && !std::strcmp(t1->title, "Pack track 1"), "no descriptor: placeholder name from the patch name");
        CHECK(t1 && !std::strcmp(t1->tags, "Race TT Arcade CTR"), "tags: no Relic without descriptor times");
        Bytes cutLev = make_lev(race); cutLev.resize(cutLev.size() - race.junk);
        CHECK(XdeltaImport_CutLev(make_lev(race).data(), make_lev(race).size()) == cutLev.size(), "LEV cut at the end of its pointer map");
        Bytes vrm = make_vrm(7, 900);
        CHECK(XdeltaImport_CutVrm(vrm.data(), vrm.size()) == vrm.size() - 900, "VRM cut at its terminator");
        CHECK(XdeltaImport_CutLev(vrm.data(), vrm.size()) == 0, "a VRM is not a LEV");
        const XdeltaImportItem *t3 = item_for(s, 3);
        CHECK(t3 && !t3->offered && !std::strcmp(t3->reason, "Arena: not supported yet"), "arena skipped with its text");
        const XdeltaImportItem *t5 = item_for(s, 5);
        CHECK(t5 && !t5->offered && !std::strcmp(t5->reason, "Not a race track (no lap checkpoints)"), "menu scene skipped with its text");
        const XdeltaImportItem *t7 = item_for(s, 7);
        CHECK(t7 && !t7->offered && !std::strcmp(t7->reason, "Same track as Pack track 1"), "duplicate pair deduplicated");
        const XdeltaImportItem *t9 = item_for(s, 9);
        CHECK(t9 && t9->offered && !std::strcmp(t9->tags, "Race TT Arcade"), "grown entry offered; moved entries are not changes");
        CHECK(!item_for(s, 11), "unchanged LEV after a moved entry is not offered");
        const XdeltaImportItem *code = line_with(s, "Game code (");
        CHECK(code && !std::strcmp(code->reason, "Code does not run in this client"), "code line");
        CHECK(code && std::strstr(code->title, "ALL.BIN") && std::strstr(code->title, "SCUS_944.26") && std::strstr(code->title, "overlay 225"),
              "code line names the new file, the executable and the overlay");
        const XdeltaImportItem *other = line_with(s, "Other changed files (2)");
        CHECK(other && !std::strcmp(other->reason, "Not track content"), "other content line, with the unpaired LEV-shaped entry");
        CHECK(!item_for(s, 201), "a LEV without a VRM before it is not listed on its own");
        CHECK(!line_with(s, "MUSIC.XA"), "unchanged files are not listed");
        CHECK(scratch_clean(), "scratch folder removed after the scan");

        /* Manifest and provenance. */
        char manifest[8192]; size_t written = 0;
        unsigned idx1 = (unsigned)(t1 - s->items);
        CHECK(XdeltaImport_CopyManifest(a.scan, idx1, manifest, sizeof manifest, &written), "manifest copy");
        auto m = nlohmann::json::parse(std::string(manifest, written));
        char uuid[37];
        XdeltaImport_PackageUuid(s->patchSha256, 1, uuid);
        CHECK(m["package_uuid"] == uuid && uuid[14] == '5', "UUIDv5 over xdelta:<patch sha>:<index>");
        CHECK(m["version"] == "1.0.0", "default version");
        bool hasSource = false;
        for (auto &f : m["files"]) if (f["path"] == "metadata/xdelta-source.json" && f["role"] == "presentation") hasSource = true;
        CHECK(hasSource, "xdelta provenance file");
        CHECK(std::string(t1->levSha256).size() == 64, "LEV hash recorded");

        /* Determinism. */
        ScanResult b = scan(patchPath, discPath);
        CHECK(b.ok, "second scan");
        if (b.ok)
        {
            const XdeltaImportSummary *s2 = XdeltaImport_Summary(b.scan);
            bool same = s2->count == s->count;
            for (unsigned i = 0; same && i < s->count; i++)
                same = !std::strcmp(s->items[i].pin, s2->items[i].pin) && !std::strcmp(s->items[i].levSha256, s2->items[i].levSha256) &&
                       !std::strcmp(s->items[i].vrmSha256, s2->items[i].vrmSha256);
            CHECK(same, "same patch twice gives identical pins and hashes");
        }
        XdeltaImport_Free(&b.scan);

        /* Install, then reinstall reverifies. */
        std::string assets = root + "/assets";
        mkdir(assets.c_str(), 0755);
        std::vector<unsigned char> selected(s->count, 0);
        for (unsigned i = 0; i < s->count; i++) selected[i] = (unsigned char)s->items[i].offered;
        char error[512];
        const int both = CTR_XDELTA_INSTALL_PACKAGES | CTR_XDELTA_INSTALL_ROOM;
        CHECK(XdeltaImport_Install(a.scan, selected.data(), assets.c_str(), both, error, sizeof error) == 2, "two tracks installed");
        CHECK(XdeltaImport_Install(a.scan, selected.data(), assets.c_str(), both, error, sizeof error) == 2, "reinstall is harmless");

        /* No pack descriptor (ruling of 2026-11-02): a Play Solo descriptor
           without laps and with author "Unknown"; the player picks the laps
           on the Custom Tracks page. Everything else is measured. */
        {
            char solo[16384], key[65], revision[65], why[160];
            CHECK(XdeltaImport_PlanDescriptor(a.scan, idx1, solo, sizeof solo, key, revision, why, sizeof why) == 2 && !why[0],
                  "no pack descriptor: a descriptor that waits for laps");
            auto pd = nlohmann::json::parse(solo);
            CHECK(!pd.contains("laps") && pd["author"] == "Unknown", "laps left out, never guessed; author Unknown");
            CHECK(pd["display_name"] == "Pack track 1" && pd["capabilities"]["relic"] == false && !pd.contains("relic_targets"),
                  "placeholder name as title, no relic without the author's laps");
            CHECK(pd["files"][0]["sha256"] == t1->levSha256 && pd["files"][1]["sha256"] == t1->vrmSha256,
                  "files pinned as installed");
            const std::string name = AP_SoloDescriptorFileName(key, revision);
            CHECK(AP_SoloNeedsLapsDescriptor(pd, name), "Play Solo lists it as waiting for laps");
            CHECK(AP_SoloDescriptorWithLaps(pd, 0).empty() && AP_SoloDescriptorWithLaps(pd, 8).empty(),
                  "laps outside 1 to 7 are refused");
            auto picked = nlohmann::json::parse(AP_SoloDescriptorWithLaps(pd, AP_SOLO_LAPS_DEFAULT));
            nlohmann::json expected = pd;
            expected["laps"] = 3;
            CHECK(picked == expected && !AP_SoloNeedsLapsDescriptor(picked, name),
                  "picked laps added, nothing else changed");
            /* The apworld admission gate reads this file. */
            if (const char *out = std::getenv("XDELTA_TEST_PICKED_DESCRIPTOR"))
                if (FILE *f = std::fopen(out, "wb"))
                {
                    const std::string text = picked.dump(2);
                    std::fwrite(text.data(), 1, text.size(), f);
                    std::fclose(f);
                }
            int kept = -1, waiting = -1;
            char note[200];
            std::string jobAssets = root + "/assets-plain";
            mkdir(jobAssets.c_str(), 0755);
            CHECK(XdeltaImportTest_InstallDescriptors(a.scan, selected.data(), jobAssets.c_str(), &kept, &waiting, note,
                                                      sizeof note) == 2 &&
                      kept == 2 && waiting == 2 && !std::strcmp(note, "Pack track 1: pick its laps in Play Solo"),
                  "install keeps both, waiting for laps, and the note says where to pick them");
        }
        struct stat info;
        /* Decision 35: the room folder rooms and solo seeds read. */
        CHECK(!std::strcmp(t1->trackKey, "pack-track-1"), "no descriptor: room folder from the title (registry key rule)");
        const std::string stem = "xdelta-" + std::string(s->patchSha256).substr(0, 12) + "-1";
        const std::string original = assets + "/tracks/pack-track-1/original/";
        CHECK(read_file(original + stem + ".lev") == cutLev, "room folder holds the cut LEV");
        CHECK(sha_hex(read_file(original + stem + ".vrm")) == t1->vrmSha256, "room folder holds the cut VRM");
        {
            struct CustomTrackManagerStatus status;
            CustomTrackManager_LocateContentFiles(assets.c_str(), "pack-track-1", "Pack track 1", "1.0.0", t1->levSha256,
                                                  (unsigned long)cutLev.size(), t1->vrmSha256,
                                                  (unsigned long)read_file(original + stem + ".vrm").size(), &status);
            CHECK(status.state == CTR_CT_MANAGER_READY, "a room pinning these hashes finds the imported track");
        }
        {
            /* Room only (the player client): no package store is written. */
            std::string roomOnly = root + "/assets-room";
            mkdir(roomOnly.c_str(), 0755);
            CHECK(XdeltaImport_Install(a.scan, selected.data(), roomOnly.c_str(), CTR_XDELTA_INSTALL_ROOM, error, sizeof error) == 2,
                  "room-only install");
            CHECK(stat((roomOnly + "/tracks/packages").c_str(), &info) != 0, "room-only install writes no package store");
            /* A different file under the same name is never replaced. */
            std::string clash = roomOnly + "/tracks/pack-track-1/original/" + stem + ".vrm";
            write_path(clash, Bytes(16, 7));
            CHECK(XdeltaImport_Install(a.scan, selected.data(), roomOnly.c_str(), CTR_XDELTA_INSTALL_ROOM, error, sizeof error) == -1 &&
                  std::strstr(error, "in the way"), "a different file in the way fails the install");
            CHECK(read_file(clash) == Bytes(16, 7), "the file in the way is left alone");
            CHECK(XdeltaImport_Install(a.scan, selected.data(), roomOnly.c_str(), both, error, sizeof error) == -1 &&
                  std::strstr(error, "in the way") && std::strstr(error, "Its package is installed."),
                  "a failed room write says the package stayed");
            /* A folder that is a link is refused. */
            std::string linked = root + "/assets-link";
            mkdir(linked.c_str(), 0755);
            mkdir((linked + "/tracks").c_str(), 0755);
            mkdir((root + "/elsewhere").c_str(), 0755);
            symlink((root + "/elsewhere").c_str(), (linked + "/tracks/pack-track-1").c_str());
            CHECK(XdeltaImport_Install(a.scan, selected.data(), linked.c_str(), CTR_XDELTA_INSTALL_ROOM, error, sizeof error) == -1 &&
                  std::strstr(error, "is a link"), "a linked track folder is refused");
            CHECK(stat((root + "/elsewhere/original").c_str(), &info) != 0, "nothing written through the link");
            CHECK(XdeltaImport_Install(a.scan, selected.data(), linked.c_str(), 0, error, sizeof error) == -1, "no destination refused");
            /* A VRM in the way is found before anything is written: no lone LEV is left. */
            std::string pair = root + "/assets-pair";
            mkdir(pair.c_str(), 0755);
            mkdir((pair + "/tracks").c_str(), 0755);
            mkdir((pair + "/tracks/pack-track-1").c_str(), 0755);
            mkdir((pair + "/tracks/pack-track-1/original").c_str(), 0755);
            write_path(pair + "/tracks/pack-track-1/original/" + stem + ".vrm", Bytes(16, 9));
            CHECK(XdeltaImport_Install(a.scan, selected.data(), pair.c_str(), CTR_XDELTA_INSTALL_ROOM, error, sizeof error) == -1 &&
                  std::strstr(error, "in the way"), "a VRM in the way fails the pair");
            CHECK(stat((pair + "/tracks/pack-track-1/original/" + stem + ".lev").c_str(), &info) != 0, "no lone LEV left behind");
            /* The VRM fails after the LEV was placed: the verified LEV stays (as for
               Saphi installs), the error says so, and importing again adds the VRM. */
            std::string rollback = root + "/assets-rollback";
            mkdir(rollback.c_str(), 0755);
            g_xdeltaTestFailRoomVrm = 1;
            CHECK(XdeltaImport_Install(a.scan, selected.data(), rollback.c_str(), CTR_XDELTA_INSTALL_ROOM, error, sizeof error) == -1 &&
                  std::strstr(error, "Test failure") && std::strstr(error, "new LEV stays in place"), "VRM failure after the LEV is reported");
            g_xdeltaTestFailRoomVrm = 0;
            CHECK(read_file(rollback + "/tracks/pack-track-1/original/" + stem + ".lev") == cutLev, "the verified LEV stays");
            CHECK(stat((rollback + "/tracks/pack-track-1/original/" + stem + ".vrm").c_str(), &info) != 0, "no VRM was placed");
            {
                int leftovers = 0;
                if (DIR *d = opendir((rollback + "/tracks/pack-track-1/original").c_str()))
                {
                    while (dirent *e = readdir(d)) if (std::strncmp(e->d_name, ".xdelta", 7) == 0) leftovers++;
                    closedir(d);
                }
                CHECK(leftovers == 0, "no temp names left");
            }
            CHECK(XdeltaImport_Install(a.scan, selected.data(), rollback.c_str(), CTR_XDELTA_INSTALL_ROOM, error, sizeof error) == 2 &&
                  sha_hex(read_file(rollback + "/tracks/pack-track-1/original/" + stem + ".vrm")) == t1->vrmSha256,
                  "importing again keeps the LEV and adds the VRM");
            /* The tracks folder itself may be a link (player configuration, as for Saphi installs). */
            std::string viaLink = root + "/assets-tracks-link";
            mkdir(viaLink.c_str(), 0755);
            mkdir((root + "/shared-tracks").c_str(), 0755);
            symlink((root + "/shared-tracks").c_str(), (viaLink + "/tracks").c_str());
            CHECK(XdeltaImport_Install(a.scan, selected.data(), viaLink.c_str(), CTR_XDELTA_INSTALL_ROOM, error, sizeof error) == 2 &&
                  !stat((root + "/shared-tracks/pack-track-1/original/" + stem + ".lev").c_str(), &info), "a linked tracks folder is followed");
        }
        {
            char key[65];
            CHECK(XdeltaImport_TrackKey("Dracula's Castle", key) && !std::strcmp(key, "dracula-s-castle"), "key rule: apostrophe");
            /* The registry key of Sid's House is sids-house, not what the rule gives for the title, so the LRG2 pack descriptor names it. */
            CHECK(XdeltaImport_TrackKey("Sid's House", key) && !std::strcmp(key, "sid-s-house"), "key rule: apostrophe without a descriptor key");
            CHECK(XdeltaImport_TrackKey("Caf\xc3\xa9 \xef\xbc\xa1  Run!", key) && !std::strcmp(key, "cafe-a-run"), "key rule: NFKD then ASCII");
            CHECK(XdeltaImport_TrackKey("\xe6\x97\xa5\xe6\x9c\xac", key) && !std::strcmp(key, "track"), "key rule: nothing left is track");
            CHECK(XdeltaImport_TrackKey(std::string(60, 'a').c_str(), key) && std::strlen(key) == 48, "key rule: cut to 48");
        }
        CHECK(!stat((assets + "/tracks/packages/" + t1->pin + "/manifest.json").c_str(), &info), "package folder named by its pin");
        std::vector<CustomPackageStoreEntry> rows;
        CustomPackage_ScanStore((assets + "/tracks/packages").c_str(),
            [](const CustomPackageStoreEntry *e, void *c) { if (e->verified) ((std::vector<CustomPackageStoreEntry> *)c)->push_back(*e); },
            &rows, error, sizeof error);
        CHECK(rows.size() == 2, "installed packages verify in the store scan");
        for (const auto &e : rows)
        {
            CHECK(e.local.xdelta && !std::strcmp(e.local.patchSha256, s->patchSha256), "store scan reads xdelta provenance");
            if (e.local.bigfileIndex == 1)
            {
                CHECK(!e.local.relic && !std::strcmp(e.local.relicReason, "No relic times"), "relic needs descriptor times");
                CHECK(e.local.ctrChallenge && !e.local.ctrChallengeReason[0], "CTR Challenge from one C, T and R");
            }
            else CHECK(e.local.bigfileIndex == 9 && !e.local.ctrChallenge && !std::strcmp(e.local.ctrChallengeReason, "No CTR letters"),
                       "no letters, no CTR Challenge");
        }
    }

    /* Review fixes, 2026-11-02: relic times without laps write no guessed lap count; the disc hash follows the file's bytes. */
    if (a.ok)
    {
        const XdeltaImportSummary *s = XdeltaImport_Summary(a.scan);
        nlohmann::json d = {{"descriptor_version", 1}, {"patch_sha256", s->patchSha256},
                            {"tracks", {{"1", {{"title", "No Laps"}, {"relic_targets", {{"sapphire", 90000}, {"gold", 80000}, {"platinum", 70000}}}}}}}};
        std::string text = d.dump();
        write_file("Pack.ctrpack.json", Bytes(text.begin(), text.end()));
        ScanResult n = scan(patchPath, discPath);
        CHECK(n.ok, "scan with relic times and no laps");
        if (n.ok)
        {
            const XdeltaImportSummary *sn = XdeltaImport_Summary(n.scan);
            const XdeltaImportItem *t1 = item_for(sn, 1);
            char manifest[8192]; size_t written = 0;
            bool good = t1 && XdeltaImport_CopyManifest(n.scan, (unsigned)(t1 - sn->items), manifest, sizeof manifest, &written);
            auto m = good ? nlohmann::json::parse(std::string(manifest, written)) : nlohmann::json::object();
            int sidecars = 0;
            if (good) for (auto &f : m["files"]) if (f["role"] == "relic_targets" || f["role"] == "race_settings") sidecars++;
            CHECK(t1 && !std::strcmp(t1->title, "No Laps") && sn->descriptorSha256[0], "the descriptor with relic times was accepted");
            CHECK(good && sidecars == 0, "no relic file with a guessed lap count when the descriptor gives no laps");
            XdeltaImport_Free(&n.scan);
        }
        std::remove((root + "/Pack.ctrpack.json").c_str());
    }
    {
        Bytes edited = baseDisc;
        edited[100] ^= 0x5a; /* system area, never read */
        const std::string swapPath = write_file("swap.bin", baseDisc);
        struct stat info;
        stat(swapPath.c_str(), &info);
        ScanResult first = scan(patchPath, swapPath);
        std::string firstHash = first.ok ? XdeltaImport_Summary(first.scan)->discSha256 : "";
        write_file("swap.bin", edited);
        struct timeval times[2] = {{info.st_atime, 0}, {info.st_mtime, 0}};
        utimes(swapPath.c_str(), times);
        ScanResult second = scan(write_file("Swap.xdelta", encode(edited, patchedDisc)), swapPath);
        CHECK(first.ok && second.ok && firstHash == sha_hex(baseDisc) &&
              !std::strcmp(XdeltaImport_Summary(second.scan)->discSha256, sha_hex(edited).c_str()),
              "a changed disc with the same path, size and time is hashed again");
        if (first.ok) XdeltaImport_Free(&first.scan);
        if (second.ok) XdeltaImport_Free(&second.scan);
    }

    /* Descriptor: titles, laps, relic targets; one for another patch is ignored with a note. */
    if (a.ok)
    {
        const XdeltaImportSummary *s = XdeltaImport_Summary(a.scan);
        nlohmann::json d = {{"descriptor_version", 1}, {"patch_sha256", s->patchSha256}, {"pack_title", "Test Pack"},
                            {"pack_author", "Someone"},
                            {"tracks", {{"1", {{"title", "Snow Lane"}, {"track_key", "snow-lane-key"}, {"laps", 5}, {"relic_targets", {{"sapphire", 90000}, {"gold", 80000}, {"platinum", 70000}}}, {"excluded", {"ctr_tokens"}}}},
                                        {"9", {{"title", "Long Road"}}}}}};
        std::string text = d.dump();
        write_file("Pack.ctrpack.json", Bytes(text.begin(), text.end()));
        ScanResult c = scan(patchPath, discPath);
        CHECK(c.ok, "scan with a descriptor");
        if (c.ok)
        {
            const XdeltaImportSummary *s3 = XdeltaImport_Summary(c.scan);
            const XdeltaImportItem *t1 = item_for(s3, 1);
            CHECK(t1 && !std::strcmp(t1->title, "Snow Lane"), "descriptor title");
            CHECK(t1 && !std::strcmp(t1->tags, "Race TT Arcade Relic"), "relic tag with descriptor times; excluded letters drop CTR");
            CHECK(s3->descriptorSha256[0] && !s3->descriptorNote[0], "descriptor recorded");
            char manifest[8192]; size_t written = 0;
            XdeltaImport_CopyManifest(c.scan, (unsigned)(t1 - s3->items), manifest, sizeof manifest, &written);
            auto m = nlohmann::json::parse(std::string(manifest, written));
            int roles = 0;
            for (auto &f : m["files"]) if (f["role"] == "relic_targets" || f["role"] == "race_settings") roles++;
            CHECK(roles == 2, "relic_targets and race_settings sidecars written");
            const XdeltaImportItem *t7 = item_for(s3, 7);
            CHECK(t7 && !std::strcmp(t7->reason, "Same track as Snow Lane"), "duplicate names the descriptor title");
            std::string assets = root + "/assets-d";
            mkdir(assets.c_str(), 0755);
            std::vector<unsigned char> selected(s3->count, 0);
            for (unsigned i = 0; i < s3->count; i++) selected[i] = (unsigned char)s3->items[i].offered;
            char error[512];
            CHECK(XdeltaImport_Install(c.scan, selected.data(), assets.c_str(), CTR_XDELTA_INSTALL_PACKAGES | CTR_XDELTA_INSTALL_ROOM,
                                       error, sizeof error) == 2, "descriptor packages install");
            CHECK(!std::strcmp(t1->trackKey, "snow-lane-key"), "descriptor track_key names the room folder");
            CHECK(item_for(s3, 9) && !std::strcmp(item_for(s3, 9)->trackKey, "long-road"), "no track_key: key from the descriptor title");
            struct stat info;
            CHECK(!stat((assets + "/tracks/snow-lane-key/original").c_str(), &info), "room folder under the descriptor key");
            std::vector<CustomPackageStoreEntry> rows;
            CustomPackage_ScanStore((assets + "/tracks/packages").c_str(),
                [](const CustomPackageStoreEntry *e, void *c) { if (e->verified) ((std::vector<CustomPackageStoreEntry> *)c)->push_back(*e); },
                &rows, error, sizeof error);
            bool relic = false, excluded = false, laps = false;
            for (const auto &e : rows)
            {
                if (e.local.bigfileIndex == 1)
                {
                    relic = e.local.relic == 1; laps = e.local.laps == 5;
                    excluded = !e.local.ctrChallenge && !std::strcmp(e.local.ctrChallengeReason, "Letters excluded");
                }
                CHECK(!std::strcmp(e.local.author, "Someone"), "pack author shown");
            }
            CHECK(relic, "relic capability with descriptor times and crates");
            CHECK(laps, "descriptor laps reach race settings");
            CHECK(excluded, "descriptor exclusion clears CTR Challenge");

            /* One store revision supplies the same exact pair to rooms and
               Play Solo, including a track placed by hand then rescanned. */
            for (const auto &e : rows)
            {
                char key[65], revision[65], plan[16384], reason[256];
                int kind = CustomPackage_StoreDescriptor(assets.c_str(), e.pin, nullptr,
                    key, sizeof key, revision, sizeof revision, plan, sizeof plan, reason, sizeof reason);
                bool authoredLaps = false;
                for (unsigned int i = 0; i < e.manifest.count; i++)
                    authoredLaps |= !std::strcmp(e.manifest.files[i].role, "race_settings");
                CHECK(kind == (authoredLaps ? 1 : 2), "store descriptor uses authored laps or needs-laps");
                if (!kind) continue;
                auto d = nlohmann::json::parse(plan);
                CHECK(d["content_id"] == key && d["revision"] == revision && d["author"] == "Someone",
                      "store descriptor keeps key, revision and author");
                CHECK(d["files"].size() == 2 && d["files"][0]["sha256"] == e.local.levSha256,
                      "store descriptor pins verified LEV and VRM");
                char levPath[4096], vrmPath[4096];
                CHECK(CustomPackage_FindPinnedFiles(assets.c_str(), revision, e.manifest.uuid,
                    d["files"][0]["sha256"].get<std::string>().c_str(), d["files"][0]["bytes"],
                    d["files"][1]["sha256"].get<std::string>().c_str(), d["files"][1]["bytes"],
                    levPath, sizeof levPath, vrmPath, sizeof vrmPath), "shared resolver finds one verified pair");
                CHECK(!CustomPackage_FindPinnedFiles(assets.c_str(), revision, e.manifest.uuid,
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", d["files"][0]["bytes"],
                    d["files"][1]["sha256"].get<std::string>().c_str(), d["files"][1]["bytes"],
                    levPath, sizeof levPath, vrmPath, sizeof vrmPath), "seed pin mismatch refuses shared pair");
            }
            const std::string sharedAssets = root + "/assets-shared";
            mkdir(sharedAssets.c_str(), 0755);
            CHECK(XdeltaImport_Install(c.scan, selected.data(), sharedAssets.c_str(),
                                       CTR_XDELTA_INSTALL_PACKAGES, error, sizeof error) == 2,
                  "xdelta installs directly into the shared store");
            CHECK(stat((sharedAssets + "/tracks/snow-lane-key").c_str(), &info) != 0,
                  "shared install does not create a second room copy");
            std::vector<CustomPackageStoreEntry> sharedRows;
            CustomPackage_ScanStore((sharedAssets + "/tracks/packages").c_str(),
                [](const CustomPackageStoreEntry *e, void *c) { if (e->verified) ((std::vector<CustomPackageStoreEntry> *)c)->push_back(*e); },
                &sharedRows, error, sizeof error);
            CHECK(sharedRows.size() == 2, "shared store sees both installed xdelta revisions");

            /* A3-46: a filtered walk reads each manifest alone and acquires only
               what the filter accepts; an accepted revision is the same entry the
               full scan reports, and the pinned lookups keep their answers. */
            {
                struct Walk { std::vector<std::string> seen; std::string only; std::vector<CustomPackageStoreEntry> rows; } w;
                const std::string store = sharedAssets + "/tracks/packages";
                auto want = [](const char *pin, const CustomPackageManifest *m, void *c) -> int {
                    auto &walk = *static_cast<Walk *>(c);
                    walk.seen.push_back(m && !std::strcmp(m->sha256, pin) ? pin : "");
                    return walk.only == pin;
                };
                auto keep = [](const CustomPackageStoreEntry *e, void *c) { static_cast<Walk *>(c)->rows.push_back(*e); };
                CHECK(CustomPackage_ScanStoreMatching(store.c_str(), want, keep, &w, error, sizeof error) &&
                      w.seen.size() == 2 && !w.seen[0].empty() && !w.seen[1].empty() && w.rows.empty(),
                      "A3-46 a filtered walk sees every parsed manifest and acquires nothing it refuses");
                w = Walk(); w.only = sharedRows[1].pin;
                CHECK(CustomPackage_ScanStoreMatching(store.c_str(), want, keep, &w, error, sizeof error) &&
                      w.rows.size() == 1 && w.rows[0].verified && !std::strcmp(w.rows[0].pin, sharedRows[1].pin) &&
                      !std::memcmp(&w.rows[0].manifest, &sharedRows[1].manifest, sizeof w.rows[0].manifest) &&
                      !std::memcmp(&w.rows[0].local, &sharedRows[1].local, sizeof w.rows[0].local),
                      "A3-46 an accepted revision is the entry the full scan reports");
                CHECK(CustomPackage_ScanStoreMatching(store.c_str(), nullptr, keep, &w, error, sizeof error) && w.rows.size() == 3,
                      "A3-46 no filter is the full scan");

                /* Two copies of one pair: same version and pins, other manifest.
                   The first verified one in pin order answers, and a damaged
                   matching copy is passed over exactly as the full scan did. */
                const CustomPackageStoreEntry &base = sharedRows[0];
                const std::string from = store + "/" + base.pin;
                std::ifstream in(from + "/manifest.json");
                auto m = nlohmann::json::parse(in);
                m["title"] = std::string(base.manifest.title) + " copy";
                m["package_uuid"] = "00000000-0000-5000-8000-0000000a3046";
                const std::string text = m.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
                NativeSha256Ctx ctx; unsigned char digest[NATIVE_SHA256_DIGEST_BYTES]; char copyPin[NATIVE_SHA256_HEX_BYTES];
                NativeSha256_Init(&ctx); NativeSha256_Update(&ctx, text.data(), text.size());
                NativeSha256_Final(&ctx, digest); NativeSha256_ToHex(digest, copyPin);
                const std::string to = store + "/" + copyPin;
                CHECK(std::system(("cp -r '" + from + "' '" + to + "'").c_str()) == 0, "A3-46 copy of a verified revision");
                std::ofstream(to + "/manifest.json", std::ios::binary | std::ios::trunc) << text;
                const CustomPackageFile *lev = nullptr, *vrm = nullptr;
                for (unsigned int i = 0; i < base.manifest.count; i++)
                {
                    if (!std::strcmp(base.manifest.files[i].role, "lev")) lev = &base.manifest.files[i];
                    if (!std::strcmp(base.manifest.files[i].role, "vrm")) vrm = &base.manifest.files[i];
                }
                const std::string first = std::min<std::string>(base.pin, copyPin), second = std::max<std::string>(base.pin, copyPin);
                auto find = [&](const char *uuid, std::string &found) {
                    char levPath[4096], vrmPath[4096];
                    if (!CustomPackage_FindPinnedFiles(sharedAssets.c_str(), base.manifest.version, uuid, lev->sha256, lev->bytes,
                                                       vrm->sha256, vrm->bytes, levPath, sizeof levPath, vrmPath, sizeof vrmPath))
                        return false;
                    found = std::string(levPath).substr(store.size() + 1, 64);
                    return std::string(vrmPath).substr(store.size() + 1, 64) == found;
                };
                std::string found;
                CHECK(lev && vrm && find(nullptr, found) && found == first, "A3-46 the first verified copy in pin order answers");
                CHECK(find("00000000-0000-5000-8000-0000000a3046", found) && found == copyPin, "A3-46 a seed uuid picks its own copy");
                const std::string damaged = store + "/" + first + "/" + lev->path;
                char original = 0;
                {
                    std::fstream file(damaged, std::ios::in | std::ios::out | std::ios::binary);
                    file.get(original); file.seekp(0); file.put((char)(original ^ 0x5a));
                }
                CHECK(find(nullptr, found) && found == second, "A3-46 a damaged matching copy is passed over");
                CHECK(!find(first == base.pin ? base.manifest.uuid : "00000000-0000-5000-8000-0000000a3046", found),
                      "A3-46 a seed uuid whose only copy is damaged finds nothing");
                {
                    std::fstream file(damaged, std::ios::in | std::ios::out | std::ios::binary);
                    file.seekp(0); file.put(original);
                }
                std::system(("rm -rf '" + to + "'").c_str());
                CHECK(find(nullptr, found) && found == base.pin, "A3-46 the store is whole again");
            }

            /* The Play Solo descriptor (ruling of 2026-11-02): complete, and
               every fact measured on the installed room files. */
            char solo[16384], key[65], revision[65], why[160];
            CHECK(XdeltaImport_PlanDescriptor(c.scan, (unsigned)(t1 - s3->items), solo, sizeof solo, key, revision,
                                              why, sizeof why) == 1, "Play Solo descriptor for a track with laps and author");
            CHECK(!std::strcmp(key, "snow-lane-key") && !std::strcmp(revision, "1.0.0"), "descriptor file names key and revision");
            auto pd = nlohmann::json::parse(solo);
            CHECK(pd["id"] == "package/snow-lane-key/1.0.0" && pd["content_id"] == "snow-lane-key" && pd["revision"] == "1.0.0",
                  "descriptor identity");
            CHECK(pd["display_name"] == "Snow Lane" && pd["author"] == "Someone" && pd["laps"] == 5, "descriptor title, author and laps");
            {
                char uuid[37];
                XdeltaImport_PackageUuid(s3->patchSha256, 1, uuid);
                CHECK(pd["uuid"] == uuid, "descriptor uuid is the package UUID");
            }
            CHECK(pd["relic_targets"] == nlohmann::json({{"sapphire", 90000}, {"gold", 80000}, {"platinum", 70000}}),
                  "descriptor relic targets from the pack descriptor");
            CHECK(pd["locator"] == nlohmann::json({{"kind", "xdelta"}, {"patch_sha256", s3->patchSha256}, {"bigfile_index", 1}}),
                  "descriptor locator names the patch and slot");
            CHECK(pd["evidence"] == nlohmann::json::array() && pd["files"].size() == 2 && pd["files"][0]["role"] == "lev" &&
                  pd["files"][0]["sha256"] == t1->levSha256 && pd["files"][1]["sha256"] == t1->vrmSha256 &&
                  pd["files"][0]["format"] == "ctr-lev" && pd["files"][1]["format"] == "ctr-vrm",
                  "descriptor pins the two files");
            {
                std::string folder = assets + "/tracks/snow-lane-key/original/xdelta-" + std::string(s3->patchSha256).substr(0, 12) + "-1";
                CustomContentVerification v;
                char error2[256];
                CHECK(CustomContentVerify_Files((folder + ".lev").c_str(), (folder + ".vrm").c_str(), &v, error2, sizeof error2),
                      "installed room files measure");
                const auto &caps = pd["capabilities"];
                CHECK(pd["files"][0]["bytes"] == v.levBytes && pd["files"][1]["bytes"] == v.vrmBytes, "descriptor file sizes");
                CHECK(caps["time_trial"] == true && caps["relic"] == true, "Time Trial and relic measured");
                CHECK(caps["arcade"] == (CustomContentVerify_ArcadeRaceReason(&v, nullptr, 0) != 0), "arcade as measured");
                CHECK(caps["ctr_tokens"] == false, "excluded letters stay false");
                CHECK(caps["wumpa"] == (v.measured.fruitCrates * 5 + v.measured.looseWumpa >= 10), "wumpa as measured");
                CHECK(caps["super_turbo_pads"] == (v.measured.superTurboPads > 0), "super_turbo_pads written and measured");
                CHECK(caps["weapon_crates"] == v.measured.weaponCrates, "weapon_crates written and measured");
                CHECK(caps["checkpoints"] == v.measured.checkpoints && caps["ai_nav"] == (v.measured.navPaths > 0),
                      "boss venue facts measured");
            }
            const XdeltaImportItem *t9 = item_for(s3, 9);
            CHECK(t9 && XdeltaImport_PlanDescriptor(c.scan, (unsigned)(t9 - s3->items), solo, sizeof solo, key, revision,
                                                    why, sizeof why) == 2,
                  "no laps in the pack descriptor: the player picks them, never a guessed lap count");
            {
                auto lr = nlohmann::json::parse(solo);
                CHECK(!lr.contains("laps") && lr["author"] == "Someone" && !lr.contains("relic_targets"),
                      "the pack author stays; no laps, no relic times");
            }
            /* The install job's step: one descriptor, and the note names the
               track left off Play Solo. */
            {
                int kept = -1;
                char note[200];
                std::string jobAssets = root + "/assets-job";
                mkdir(jobAssets.c_str(), 0755);
                int waiting = -1;
                CHECK(XdeltaImportTest_InstallDescriptors(c.scan, selected.data(), jobAssets.c_str(), &kept, &waiting, note,
                                                          sizeof note) == 2 &&
                      kept == 2 && waiting == 1 && !std::strcmp(note, "Long Road: pick its laps in Play Solo"),
                      "install keeps both descriptors and names the track waiting for laps");
            }
        }
        XdeltaImport_Free(&c.scan);
        /* Two tracks with one track key and version: the first keeps the
           Play Solo descriptor, the second would replace its file. */
        {
            nlohmann::json same = d;
            same["tracks"]["9"]["track_key"] = "snow-lane-key";
            same["tracks"]["9"]["laps"] = 3;
            text = same.dump();
            write_file("Pack.ctrpack.json", Bytes(text.begin(), text.end()));
            ScanResult twin = scan(patchPath, discPath);
            CHECK(twin.ok, "scan with two tracks on one key");
            if (twin.ok)
            {
                const XdeltaImportSummary *st = XdeltaImport_Summary(twin.scan);
                std::vector<unsigned char> sel(st->count, 0);
                for (unsigned i = 0; i < st->count; i++) sel[i] = (unsigned char)st->items[i].offered;
                int kept = -1;
                char note[200];
                std::string twinAssets = root + "/assets-twin";
                mkdir(twinAssets.c_str(), 0755);
                CHECK(XdeltaImportTest_InstallDescriptors(twin.scan, sel.data(), twinAssets.c_str(), &kept, nullptr, note,
                                                          sizeof note) == 2 &&
                      kept == 1 && !std::strcmp(note, "Long Road is not on Play Solo: another track in this patch has the same track key"),
                      "one descriptor per track key and version");
            }
            XdeltaImport_Free(&twin.scan);
        }
        nlohmann::json badKey = d;
        badKey["tracks"]["1"]["track_key"] = "../escape";
        text = badKey.dump();
        write_file("Pack.ctrpack.json", Bytes(text.begin(), text.end()));
        ScanResult k = scan(patchPath, discPath);
        CHECK(k.ok && !std::strcmp(XdeltaImport_Summary(k.scan)->descriptorNote,
                                   "Pack descriptor not used: track_key must be a registry track key"),
              "a track_key that is not a registry key refuses the descriptor");
        XdeltaImport_Free(&k.scan);
        nlohmann::json wrong = d;
        wrong["patch_sha256"] = std::string(64, '0');
        text = wrong.dump();
        write_file("Pack.ctrpack.json", Bytes(text.begin(), text.end()));
        ScanResult w = scan(patchPath, discPath);
        CHECK(w.ok, "scan with a descriptor for another patch still works");
        if (w.ok)
        {
            const XdeltaImportSummary *s4 = XdeltaImport_Summary(w.scan);
            CHECK(!std::strcmp(s4->descriptorNote, "Pack descriptor not used: Descriptor is for another patch"), "descriptor note");
            CHECK(item_for(s4, 1) && !std::strcmp(item_for(s4, 1)->title, "Pack track 1"), "placeholder name without a usable descriptor");
        }
        XdeltaImport_Free(&w.scan);
        std::remove((root + "/Pack.ctrpack.json").c_str());
    }
    XdeltaImport_Free(&a.scan);

    /* Directory after the data: second pass. */
    {
        DiscSpec late = patched; late.dirAfterData = true;
        DiscSpec lateBase = base; lateBase.dirAfterData = true;
        const Bytes lb = make_disc(lateBase), lp = make_disc(late);
        std::string d = write_file("late-base.bin", lb);
        std::string p = write_file("Late.xdelta", encode(lb, lp));
        ScanResult r = scan(p, d);
        CHECK(r.ok, "late directory scan succeeds");
        if (r.ok)
        {
            const XdeltaImportSummary *s = XdeltaImport_Summary(r.scan);
            CHECK(s->passes == 2, "late directory takes a second pass");
            CHECK(item_for(s, 1) && item_for(s, 1)->offered, "second pass still finds the track");
        }
        XdeltaImport_Free(&r.scan);
    }

    /* A patched disc cut off inside a BIGFILE table that spans sectors. */
    {
        DiscSpec wide = base;
        while (wide.bigfile.size() < 600) wide.bigfile.push_back(Bytes(40, 7));
        wide.dirAfterData = true;
        Bytes full = make_disc(wide);
        size_t bigSector = 0;
        for (size_t lba = 17; lba * 2352 < full.size(); lba++)
            if (full[lba * 2352 + 24 + 4] == 600 % 256 && full[lba * 2352 + 24 + 5] == 600 / 256) { bigSector = lba; break; }
        CHECK(bigSector != 0, "fixture finds its BIGFILE table");
        Bytes cut(full.begin(), full.begin() + (long)((bigSector + 1) * 2352));
        ScanResult r = scan(write_file("Cut.xdelta", encode(baseDisc, cut)), discPath);
        CHECK(!r.ok, "a disc cut inside its BIGFILE table is refused, not read past the table");
        XdeltaImport_Free(&r.scan);
    }


    /* A2-19: track names, the editable name, and the record of applied patches. */
    {
        char label[160];
        const char *stems[][2] = {{"Lego_Adventure_v1.1", "Lego Adventure"}, {"crash-30anniversary-adventure", "Crash 30anniversary adventure"},
                                  {"Ninas_Nightmare_99", "Ninas Nightmare 99"}, {"__x__", "X"}, {"track_v2", "Track"}, {"", ""}};
        for (auto &pair : stems)
        {
            XdeltaImportTest_PackLabel(pair[0], label, sizeof label);
            CHECK(!std::strcmp(label, pair[1]), (std::string("pack label ") + pair[0]).c_str());
        }
        CHECK(!std::strcmp(XdeltaImportTest_KnownName("83406e87505616994f5b539666f1327597212058506c29d66396aac15448529e"), "Snowy Valley"),
              "a known LEV hash has its name");
        CHECK(!XdeltaImportTest_KnownName("00") && !XdeltaImportTest_KnownName(nullptr), "an unknown hash has no name");

        ScanResult r = scan(write_file("Lego_Adventure_v1.1.xdelta", encode(baseDisc, patchedDisc)), discPath);
        CHECK(r.ok, "scan of a patch with a version in its name");
        if (r.ok)
        {
            const XdeltaImportSummary *s = XdeltaImport_Summary(r.scan);
            const XdeltaImportItem *t1 = item_for(s, 1);
            CHECK(t1 && !std::strcmp(t1->title, "Lego Adventure track 1") && t1->nameSource == XDELTA_NAME_LABEL,
                  "no descriptor, no known hash: a clean label with the track number");
            unsigned index = 0;
            for (unsigned i = 0; i < s->count; i++) if (s->items[i].bigfileIndex == 1) index = i;
            char why[96], oldPin[65];
            std::snprintf(oldPin, sizeof oldPin, "%s", t1->pin);
            XdeltaImportScan *scanned = r.scan;
            CHECK(!XdeltaImport_RenameItem(scanned, index, "", why, sizeof why) && why[0], "an empty name is refused with a reason");
            CHECK(!XdeltaImport_RenameItem(scanned, index, std::string(129, 'a').c_str(), why, sizeof why), "a 129 byte name is refused");
            CHECK(!XdeltaImport_RenameItem(scanned, index, "Bad\nName", why, sizeof why), "a name with a control character is refused");
            CHECK(!std::strcmp(item_for(XdeltaImport_Summary(scanned), 1)->title, "Lego Adventure track 1") &&
                  !std::strcmp(item_for(XdeltaImport_Summary(scanned), 1)->pin, oldPin), "a refused name changes nothing");
            const XdeltaImportItem *skipped = item_for(s, 3);
            CHECK(skipped && !XdeltaImport_RenameItem(scanned, (unsigned)(skipped - s->items), "Arena", why, sizeof why),
                  "a skipped item cannot be renamed");
            CHECK(XdeltaImport_RenameItem(scanned, index, "Sunset Raceway", why, sizeof why), "a valid name is applied");
            const XdeltaImportItem *renamed = item_for(XdeltaImport_Summary(scanned), 1);
            CHECK(!std::strcmp(renamed->title, "Sunset Raceway") && renamed->nameSource == XDELTA_NAME_EDITED &&
                  !std::strcmp(renamed->trackKey, "sunset-raceway") && std::strcmp(renamed->pin, oldPin), "title, source, room folder and pin follow the name");
            char manifest[4096];
            size_t written = 0;
            CHECK(XdeltaImport_CopyManifest(scanned, index, manifest, sizeof manifest - 1, &written), "the rebuilt package has a manifest");
            manifest[written] = 0;
            CHECK(std::strstr(manifest, "\"title\":\"Sunset Raceway\"") != nullptr, "the manifest carries the typed title");
            std::vector<unsigned char> chosen(s->count, 0);
            chosen[index] = 1;
            const std::string installed = root + "/rename-assets";
            mkdir(installed.c_str(), 0755);
            char error[256];
            CHECK(XdeltaImport_Install(scanned, chosen.data(), installed.c_str(), CTR_XDELTA_INSTALL_ROOM, error, sizeof error) == 1,
                  "the renamed track installs");
            struct stat info;
            CHECK(!stat((installed + "/tracks/sunset-raceway/original").c_str(), &info), "its room folder uses the new name");
            CHECK(XdeltaImport_RenameItem(scanned, index, "Sunset Raceway", why, sizeof why), "the same name again is a no-op");
        }
        XdeltaImport_Free(&r.scan);
    }
    /* A3-29: a track imported under the player's own name is one installed
       package, one room folder and one Play Solo entry, whatever descriptor
       (curated track key or none) the pack came with. */
    for (int curated = 0; curated < 2; curated++)
    {
        const std::string tag = curated ? "curated" : "plain";
        const Bytes patchBytes = encode(baseDisc, patchedDisc);
        const std::string patch = write_file("Anniv-" + tag + ".xdelta", patchBytes);
        nlohmann::json d = {{"descriptor_version", 1}, {"patch_sha256", sha_hex(patchBytes)}, {"pack_title", "Anniv"},
                            {"tracks", {{"1", {{"title", "Generic Slot One"}, {"laps", 3}}}}}};
        if (curated) d["tracks"]["1"]["track_key"] = "crash-cove";
        const std::string text = d.dump();
        const std::string descriptorPath = write_file("Anniv-" + tag + ".ctrpack.json", Bytes(text.begin(), text.end()));
        ScanResult r = scan(patch, discPath, descriptorPath.c_str());
        CHECK(r.ok, ("A3-29 scan " + tag).c_str());
        if (!r.ok) continue;
        const XdeltaImportSummary *s = XdeltaImport_Summary(r.scan);
        const XdeltaImportItem *t1 = item_for(s, 1);
        unsigned index = (unsigned)(t1 - s->items);
        char why[96];
        CHECK(XdeltaImport_RenameItem(r.scan, index, "My Custom Name", why, sizeof why), "A3-29 rename applies");
        std::vector<unsigned char> chosen(s->count, 0);
        chosen[index] = 1;
        const std::string assets = root + "/a329-" + tag;
        mkdir(assets.c_str(), 0755);
        char error[256];
        CHECK(XdeltaImport_Install(r.scan, chosen.data(), assets.c_str(), CTR_XDELTA_INSTALL_PACKAGES | CTR_XDELTA_INSTALL_ROOM, error, sizeof error) == 1,
              "A3-29 renamed track installs");
        std::vector<CustomPackageStoreEntry> rows;
        CustomPackage_ScanStore((assets + "/tracks/packages").c_str(),
            [](const CustomPackageStoreEntry *e, void *c) { if (e->verified) ((std::vector<CustomPackageStoreEntry> *)c)->push_back(*e); },
            &rows, error, sizeof error);
        char planKey[65], planRev[65], plan[16384], reason[256];
        int planKind = XdeltaImport_PlanDescriptor(r.scan, index, plan, sizeof plan, planKey, planRev, reason, sizeof reason);
        CHECK(rows.size() == 1, "A3-29 a renamed import is one installed package");
        for (const auto &e : rows)
        {
            char key[65], revision[65], json[16384];
            int kind = CustomPackage_StoreDescriptor(assets.c_str(), e.pin, nullptr, key, sizeof key, revision, sizeof revision,
                                                     json, sizeof json, reason, sizeof reason);
            CHECK(kind == planKind && !std::strcmp(key, planKey) && !std::strcmp(revision, planRev),
                  "A3-29 the store and the importer name one Play Solo entry");
        }
        XdeltaImport_Free(&r.scan);
    }
    /* The same patch track imported again under another name: two packages of
       one uuid, and the newest import is the one marked newest, also when it
       is an earlier name coming back. */
    {
        const Bytes patchBytes = encode(baseDisc, patchedDisc);
        const std::string patch = write_file("Again.xdelta", patchBytes);
        const std::string assets = root + "/a329-again";
        mkdir(assets.c_str(), 0755);
        auto importAs = [&](const char *name, char pin[65], char uuid[37]) {
            ScanResult r = scan(patch, discPath);
            CHECK(r.ok, "A3-29 scan for a second import");
            if (!r.ok) return;
            const XdeltaImportSummary *s = XdeltaImport_Summary(r.scan);
            unsigned index = (unsigned)(item_for(s, 1) - s->items);
            char why[96], error[256];
            if (name) CHECK(XdeltaImport_RenameItem(r.scan, index, name, why, sizeof why), "A3-29 rename for a second import");
            std::vector<unsigned char> chosen(s->count, 0);
            chosen[index] = 1;
            CHECK(XdeltaImport_Install(r.scan, chosen.data(), assets.c_str(), CTR_XDELTA_INSTALL_PACKAGES, error, sizeof error) >= 1,
                  "A3-29 second import installs");
            std::snprintf(pin, 65, "%s", s->items[index].pin);
            XdeltaImport_PackageUuid(s->patchSha256, 1, uuid);
            XdeltaImport_Free(&r.scan);
        };
        auto mtime = [&](const char *pin) {
            struct stat info;
            return stat((assets + "/tracks/packages/" + pin + "/manifest.json").c_str(), &info) ? 0 : (long long)info.st_mtime;
        };
        auto age = [&](const char *pin) {
            struct timeval times[2] = {{time(nullptr) - 7200, 0}, {time(nullptr) - 7200, 0}};
            utimes((assets + "/tracks/packages/" + pin + "/manifest.json").c_str(), times);
        };
        char firstPin[65] = {}, secondPin[65] = {}, againPin[65] = {}, uuid[37] = {};
        importAs(nullptr, firstPin, uuid);
        importAs("Second Name", secondPin, uuid);
        CHECK(std::strcmp(firstPin, secondPin) != 0, "A3-29 a new name is a new package");
        age(firstPin);
        age(secondPin);
        importAs(nullptr, againPin, uuid);
        CHECK(!std::strcmp(againPin, firstPin), "A3-29 the first name again is the first package");
        CHECK(mtime(firstPin) > mtime(secondPin) + 3600, "A3-29 importing an earlier name again makes it the newest install");
        std::vector<CustomPackageStoreEntry> rows;
        char error[256];
        CustomPackage_ScanStore((assets + "/tracks/packages").c_str(),
            [](const CustomPackageStoreEntry *e, void *c) { if (e->verified) ((std::vector<CustomPackageStoreEntry> *)c)->push_back(*e); },
            &rows, error, sizeof error);
        int sameUuid = 0;
        for (const auto &e : rows) sameUuid += !std::strcmp(e.manifest.uuid, uuid);
        CHECK(sameUuid == 2, "A3-29 the two names are two packages of one uuid, which Play Solo folds to the newest");
    }
    /* A3-20: a re-import replaces the old copy that still has alpha1's
       generic "slot" title, and nothing else. */
    {
        const Bytes patchBytes = encode(baseDisc, patchedDisc);
        const std::string patch = write_file("Replace.xdelta", patchBytes);
        DiscSpec changed = base;
        LevSpec otherSpec; otherSpec.seed = 77;
        changed.bigfile[1] = make_lev(otherSpec);
        const Bytes otherBytes = encode(baseDisc, make_disc(changed));
        const std::string otherPatch = write_file("Different.xdelta", otherBytes);
        int serial = 0;
        auto fresh = [&]() {
            const std::string home = root + "/a320-" + std::to_string(++serial);
            mkdir(home.c_str(), 0755);
            const std::string assets = home + "/assets";
            mkdir(assets.c_str(), 0755);
            return assets;
        };
        /* Installs the patch's track 1 under `name` (default label when null); returns its pin. */
        auto importAs = [&](const std::string &assets, const std::string &patchFile, const char *name, int expectState = -1) {
            std::string pin;
            ScanResult r = scan(patchFile, discPath);
            CHECK(r.ok, "A3-20 scan");
            if (!r.ok) return pin;
            const XdeltaImportSummary *s = XdeltaImport_Summary(r.scan);
            unsigned index = (unsigned)(item_for(s, 1) - s->items);
            char why[96], error[256];
            if (name) CHECK(XdeltaImport_RenameItem(r.scan, index, name, why, sizeof why), "A3-20 rename");
            if (expectState >= 0)
                CHECK(XdeltaImport_TestOldCopyState(r.scan, index, assets.c_str()) == expectState, "A3-20 the review state of the item");
            std::vector<unsigned char> chosen(s->count, 0);
            chosen[index] = 1;
            CHECK(XdeltaImport_Install(r.scan, chosen.data(), assets.c_str(), CTR_XDELTA_INSTALL_PACKAGES, error, sizeof error) == 1,
                  "A3-20 install");
            pin = s->items[index].pin;
            XdeltaImport_Free(&r.scan);
            return pin;
        };
        auto packages = [&](const std::string &assets) {
            std::vector<std::string> titles;
            std::vector<CustomPackageStoreEntry> rows;
            char error[256];
            CustomPackage_ScanStore((assets + "/tracks/packages").c_str(),
                [](const CustomPackageStoreEntry *e, void *c) { if (e->verified) ((std::vector<CustomPackageStoreEntry> *)c)->push_back(*e); },
                &rows, error, sizeof error);
            for (const auto &e : rows) titles.push_back(e.manifest.title);
            return titles;
        };
        auto exists = [&](const std::string &assets, const std::string &pin) {
            struct stat info;
            return !stat((assets + "/tracks/packages/" + pin + "/manifest.json").c_str(), &info);
        };
        auto room = [&](const std::string &assets, const char *id, const std::string &text) {
            const std::string solo = assets.substr(0, assets.rfind('/')) + "/solo";
            mkdir(solo.c_str(), 0755);
            mkdir((solo + "/rooms").c_str(), 0755);
            mkdir((solo + "/rooms/" + id).c_str(), 0755);
            write_path(solo + "/rooms/" + id + "/room.json", Bytes(text.begin(), text.end()));
        };
        char uuid[37];
        {
            ScanResult probe = scan(patch, discPath);
            if (probe.ok) XdeltaImport_PackageUuid(XdeltaImport_Summary(probe.scan)->patchSha256, 1, uuid);
            XdeltaImport_Free(&probe.scan);
        }

        /* An alpha1-titled copy is replaced: one package, with the new name. */
        {
            const std::string assets = fresh();
            const std::string oldPin = importAs(assets, patch, "Replace slot 1");
            CHECK(packages(assets).size() == 1, "A3-20 the old copy is installed");
            mkdir((assets + "/tracks/packages/.replaced-archive").c_str(), 0755);
            write_path(assets + "/tracks/packages/.replaced-archive/keep.txt", Bytes(3, 'x'));
            const std::string newPin = importAs(assets, patch, nullptr, XDELTA_OLD_REPLACE);
            struct stat kept;
            CHECK(!stat((assets + "/tracks/packages/.replaced-archive/keep.txt").c_str(), &kept), "A3-20 a folder of the player's with a similar name is left alone");
            const auto titles = packages(assets);
            CHECK(titles.size() == 1 && titles[0] != "Replace slot 1" && !exists(assets, oldPin) && exists(assets, newPin),
                  "A3-20 an alpha1-titled copy is replaced by the new one");
            const std::string again = importAs(assets, patch, nullptr, XDELTA_OLD_NONE);
            CHECK(again == newPin && packages(assets).size() == 1, "A3-20 importing the new copy again changes nothing");
        }
        /* "Imported track slot 1" is alpha1's title when the patch name gave none. */
        {
            const std::string assets = fresh();
            const std::string oldPin = importAs(assets, patch, "Imported track slot 1");
            const std::string newPin = importAs(assets, patch, "Brand New Name");
            CHECK(packages(assets).size() == 1 && !exists(assets, oldPin) && exists(assets, newPin),
                  "A3-20 the generic title without a patch name is replaced too");
        }
        /* A copy the player named is kept; so is one whose slot number is another track's. */
        {
            const std::string assets = fresh();
            const std::string mine = importAs(assets, patch, "My Hand Named Copy");
            const std::string wrongSlot = importAs(assets, patch, "Replace slot 7");
            const std::string typed = importAs(assets, patch, "My Favorite slot 1");
            const std::string newPin = importAs(assets, patch, nullptr, XDELTA_OLD_NONE);
            CHECK(packages(assets).size() == 4 && exists(assets, mine) && exists(assets, wrongSlot) && exists(assets, typed) && exists(assets, newPin),
                  "A3-20 a renamed copy is never replaced, also one that ends in the matching slot");
        }
        /* A room that names the old copy keeps it. */
        {
            const std::string assets = fresh();
            const std::string oldPin = importAs(assets, patch, "Replace slot 1");
            room(assets, "aaaa", std::string("{\"custom_tracks\":[{\"id\":\"package/replace-slot-1/1.0.0\",\"content_id\":\"replace-slot-1\",\"uuid\":\"") + uuid + "\"}]}");
            const std::string newPin = importAs(assets, patch, nullptr, XDELTA_OLD_KEEP);
            CHECK(packages(assets).size() == 2 && exists(assets, oldPin) && exists(assets, newPin),
                  "A3-20 a copy a room refers to is kept");
        }
        /* A room about another track, or an unreadable room.json, decides correctly. */
        {
            const std::string assets = fresh();
            const std::string oldPin = importAs(assets, patch, "Replace slot 1");
            room(assets, "bbbb", std::string("{\"custom_tracks\":[{\"content_id\":\"somebody-else\",\"uuid\":\"") + uuid + "\"}]}");
            room(assets, "cccc", "{\"custom_tracks\":[{\"content_id\":\"replace-slot-1\",\"uuid\":\"00000000-0000-5000-8000-000000000000\"}]}");
            const std::string newPin = importAs(assets, patch, nullptr, XDELTA_OLD_REPLACE);
            CHECK(packages(assets).size() == 1 && !exists(assets, oldPin) && exists(assets, newPin),
                  "A3-20 a room about other tracks does not hold the old copy");
        }
        {
            const std::string assets = fresh();
            const std::string oldPin = importAs(assets, patch, "Replace slot 1");
            const std::string home = assets.substr(0, assets.rfind('/'));
            mkdir((home + "/solo").c_str(), 0755);
            mkdir((home + "/solo/rooms").c_str(), 0755);
            mkdir((home + "/solo/rooms/dddd").c_str(), 0755);
            mkdir((home + "/solo/rooms/dddd/room.json").c_str(), 0755);
            const std::string newPin = importAs(assets, patch, nullptr, XDELTA_OLD_KEEP);
            CHECK(packages(assets).size() == 2 && exists(assets, oldPin), "A3-20 a room that cannot be read keeps the old copy");
        }
        /* A room that names the old copy by its descriptor's track key keeps it. */
        {
            const std::string assets = fresh();
            nlohmann::json d = {{"descriptor_version", 1}, {"patch_sha256", sha_hex(patchBytes)},
                                {"tracks", {{"1", {{"title", "Replace slot 1"}, {"laps", 3}, {"track_key", "chosen-by-pack"}}}}}};
            const std::string text = d.dump();
            const std::string descriptorPath = write_file("Replace.ctrpack.json", Bytes(text.begin(), text.end()));
            ScanResult r = scan(patch, discPath, descriptorPath.c_str());
            CHECK(r.ok, "A3-20 scan with a track key");
            if (r.ok)
            {
                const XdeltaImportSummary *s = XdeltaImport_Summary(r.scan);
                std::vector<unsigned char> chosen(s->count, 0);
                chosen[(unsigned)(item_for(s, 1) - s->items)] = 1;
                char error[256];
                CHECK(XdeltaImport_Install(r.scan, chosen.data(), assets.c_str(), CTR_XDELTA_INSTALL_PACKAGES, error, sizeof error) == 1,
                      "A3-20 install with a track key");
                XdeltaImport_Free(&r.scan);
            }
            std::remove(descriptorPath.c_str());
            room(assets, "ffff", std::string("{\"x\":\"chosen-by-pack\",\"u\":\"") + uuid + "\"}");
            const std::string newPin = importAs(assets, patch, nullptr, XDELTA_OLD_KEEP);
            CHECK(packages(assets).size() == 2 && exists(assets, newPin), "A3-20 a room naming the descriptor's track key keeps the old copy");
        }
        /* A damaged manifest in the store neither stops the scan nor is touched. */
        {
            const std::string assets = fresh();
            importAs(assets, patch, "Replace slot 1");
            const std::string text = "{\"package_uuid\": 5, \"title\": 7}";
            const std::string bad = sha_hex(Bytes(text.begin(), text.end()));
            mkdir((assets + "/tracks/packages/" + bad).c_str(), 0755);
            write_path(assets + "/tracks/packages/" + bad + "/manifest.json", Bytes(text.begin(), text.end()));
            const std::string newPin = importAs(assets, patch, nullptr, XDELTA_OLD_REPLACE);
            CHECK(exists(assets, newPin) && exists(assets, bad), "A3-20 a damaged manifest in the store is skipped");
        }
        /* A different patch is untouched, even with the same slot and a slot title. */
        {
            const std::string assets = fresh();
            const std::string other = importAs(assets, otherPatch, "Different slot 1");
            const std::string mine = importAs(assets, patch, nullptr, XDELTA_OLD_NONE);
            CHECK(packages(assets).size() == 2 && exists(assets, other) && exists(assets, mine),
                  "A3-20 a different patch's slot-titled copy is untouched");
        }
        /* The solo folder named by the client is the one read. */
        {
            const std::string assets = fresh();
            const std::string elsewhere = root + "/a320-solo-elsewhere";
            mkdir(elsewhere.c_str(), 0755);
            mkdir((elsewhere + "/rooms").c_str(), 0755);
            mkdir((elsewhere + "/rooms/eeee").c_str(), 0755);
            const std::string text = std::string("{\"x\":\"replace-slot-1\",\"u\":\"") + uuid + "\"}";
            write_path(elsewhere + "/rooms/eeee/room.json", Bytes(text.begin(), text.end()));
            XdeltaImport_SetSoloRoot(elsewhere.c_str());
            const std::string oldPin = importAs(assets, patch, "Replace slot 1");
            const std::string newPin = importAs(assets, patch, nullptr, XDELTA_OLD_KEEP);
            XdeltaImport_SetSoloRoot(nullptr);
            CHECK(packages(assets).size() == 2 && exists(assets, oldPin) && exists(assets, newPin), "A3-20 SetSoloRoot names the folder read for rooms");
        }
    }
    /* A3-15: the curated table of tester names, keyed by patch hash and slot. */
    {
        const char *patches[4] = {
            "92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2",
            "1e8a796f019c23f7de70c21a00d9fb20ba4c7be0d819c21de3bd84ffcd8397a1",
            "836d551d5d213d0aa704083f3c75ed8c0cfe58c9a6c01ef81b4d8a15fdf67180",
            "748a18082ca0dbc6ff31321e7a75bfa58c3afac80102363fb1385b32e50196df"};
        struct Row { int patch, slot; const char *name; }; /* name nullptr: a boss slot, generic. */
        const Row rows[] = {
            {0, 7, "Crazy In Kartmandu"},
            {0, 15, "N-Gage Android Alley"},
            {0, 23, "Tomb Temple"},
            {0, 39, "Skull Cave"},
            {0, 41, "Arctic Alleyway"},
            {0, 47, "N-Gage Deep Sea Driving"},
            {0, 49, "Wumpa World"},
            {0, 57, "Drive-Thru Danger"},
            {0, 79, "Crystal Caves"},
            {0, 119, "Dark Ruins"},
            {0, 127, "N-Gage Assembly Lane"},
            {0, 135, "N-Gage Out of Time"},
            {1, 7, "Haunted Hovel"},
            {1, 9, nullptr},
            {1, 23, "Dracula's Castle"},
            {1, 39, "Lunatic Laboratory"},
            {1, 49, nullptr},
            {1, 55, "Sid's House"},
            {1, 79, "DS Luigi's Mansion"},
            {1, 119, "LEGO Knightmare-A-Thon"},
            {1, 135, "LEGO Dark Forest Dash"},
            {1, 153, nullptr},
            {1, 169, nullptr},
            {2, 9, "Matsuri"},
            {2, 31, "DK Shipwreck Shore"},
            {2, 39, "Kong Dungeon"},
            {2, 41, "Breeze Harbor"},
            {2, 47, "Dreamy Heights"},
            {2, 55, "Haven Stadium"},
            {2, 71, "Skater's Retreat"},
            {2, 103, "Neon Paradise"},
            {2, 135, "Redrock Ravine"},
            {2, 169, "Tribal Trials"},
            {3, 31, "Hasty Hedges"},
            {3, 39, "Holland"},
            {3, 47, "Motor Sports Land"},
            {3, 135, "N-Gage Jungle Boogie"},
        };
        for (const Row &row : rows)
        {
            const char *got = XdeltaImportTest_CuratedName(patches[row.patch], row.slot);
            const std::string label = "curated slot " + std::to_string(row.patch) + ":" + std::to_string(row.slot);
            CHECK(row.name ? (got && !std::strcmp(got, row.name)) : !got, label.c_str());
        }
        CHECK(sizeof rows / sizeof rows[0] == 37, "the tester table has 37 rows");
        for (int patch = 0; patch < 4; patch++)
        {
            int listed = 0;
            for (int slot = 0; slot < 256; slot++)
            {
                bool inTable = false;
                for (const Row &row : rows) inTable = inTable || (row.patch == patch && row.slot == slot && row.name);
                const char *got = XdeltaImportTest_CuratedName(patches[patch], slot);
                CHECK(inTable == (got != nullptr), "only listed, named slots have a curated name");
                listed += got != nullptr;
            }
            CHECK(listed > 0, "each of the four patches maps slots");
        }
        CHECK(!XdeltaImportTest_CuratedName("00", 7) && !XdeltaImportTest_CuratedName(nullptr, 7), "an unknown patch has no curated names");

        /* End to end on the synthetic patch: its hash is registered for slot 1 only. */
        ScanResult plain = scan(patchPath, discPath);
        CHECK(plain.ok, "scan before registering the curated row");
        if (plain.ok)
        {
            char sha[65];
            std::snprintf(sha, sizeof sha, "%s", XdeltaImport_Summary(plain.scan)->patchSha256);
            XdeltaImportTest_AddCurated(sha, 1, "Curated Raceway");
            ScanResult r = scan(patchPath, discPath);
            CHECK(r.ok, "scan with a curated row");
            if (r.ok)
            {
                const XdeltaImportSummary *s = XdeltaImport_Summary(r.scan);
                const XdeltaImportItem *t1 = item_for(s, 1), *t9 = item_for(s, 9);
                CHECK(t1 && !std::strcmp(t1->title, "Curated Raceway") && t1->nameSource == XDELTA_NAME_TESTER,
                      "a listed slot takes its curated name");
                CHECK(t9 && item_for(XdeltaImport_Summary(plain.scan), 9) &&
                          !std::strcmp(t9->title, item_for(XdeltaImport_Summary(plain.scan), 9)->title) &&
                          std::strstr(t9->title, "Pack track ") == t9->title && t9->nameSource == XDELTA_NAME_LABEL,
                      "an unlisted slot keeps the generic name");
                unsigned index = 0;
                for (unsigned i = 0; i < s->count; i++) if (s->items[i].bigfileIndex == 1) index = i;
                char why[96];
                CHECK(XdeltaImport_RenameItem(r.scan, index, "My Own Name", why, sizeof why), "the player renames a curated track");
                const XdeltaImportItem *renamed = item_for(XdeltaImport_Summary(r.scan), 1);
                CHECK(renamed && !std::strcmp(renamed->title, "My Own Name") && renamed->nameSource == XDELTA_NAME_EDITED,
                      "the player's name wins over the curated name");
                char manifest[4096];
                size_t written = 0;
                CHECK(XdeltaImport_CopyManifest(r.scan, index, manifest, sizeof manifest - 1, &written), "the renamed package has a manifest");
                manifest[written] = 0;
                CHECK(std::strstr(manifest, "\"title\":\"My Own Name\"") != nullptr && !std::strstr(manifest, "Curated Raceway"),
                      "the manifest keeps the player's name");
            }
            XdeltaImport_Free(&r.scan);
        }
        XdeltaImport_Free(&plain.scan);
    }
    {
        const std::string assets = root + "/record-assets";
        mkdir(assets.c_str(), 0755);
        mkdir((assets + "/tracks").c_str(), 0755);
        mkdir((assets + "/tracks/import").c_str(), 0755);
        const Bytes patchBytes = encode(baseDisc, patchedDisc);
        write_path(assets + "/tracks/import/Pack.xdelta", patchBytes);
        write_path(assets + "/tracks/import/Renamed copy.xdelta", patchBytes);
        Bytes other = patchBytes;
        other.push_back(0);
        write_path(assets + "/tracks/import/Other.xdelta", other);
        write_path(assets + "/tracks/import/Failed.xdelta", Bytes(100, 'x'));
        char names[CTR_XDELTA_FOLDER_MAX][CTR_XDELTA_NAME_MAX] = {};
        std::snprintf(names[0], CTR_XDELTA_NAME_MAX, "%s", "Failed.xdelta");
        std::snprintf(names[1], CTR_XDELTA_NAME_MAX, "%s", "Other.xdelta");
        std::snprintf(names[2], CTR_XDELTA_NAME_MAX, "%s", "Pack.xdelta");
        std::snprintf(names[3], CTR_XDELTA_NAME_MAX, "%s", "Renamed copy.xdelta");
        XdeltaPatchState st[4];
        CHECK(XdeltaImport_PatchStates(assets.c_str(), names, 4, st, nullptr), "states without a record");
        CHECK(st[0].state == XDELTA_STATE_NEW && st[2].state == XDELTA_STATE_NEW, "nothing is imported before a record exists");

        ScanResult r = scan(patchPath, discPath);
        CHECK(r.ok, "scan for the record");
        if (r.ok)
        {
            const XdeltaImportSummary *s = XdeltaImport_Summary(r.scan);
            std::vector<int> offered;
            for (unsigned i = 0; i < s->count; i++) if (s->items[i].offered) offered.push_back(s->items[i].bigfileIndex);
            CHECK(offered.size() >= 2, "the fixture offers two or more tracks");
            XdeltaImport_RecordResult(assets.c_str(), "Pack.xdelta", s, offered.data(), 1, "");
            XdeltaImport_PatchStates(assets.c_str(), names, 4, st, nullptr);
            CHECK(st[2].state == XDELTA_STATE_PARTLY && st[2].imported == 1 && st[2].offered == (int)offered.size(), "one of several tracks is partly imported");
            CHECK(st[3].state == XDELTA_STATE_PARTLY, "a renamed copy of the same bytes counts, found by hash");
            CHECK(st[1].state == XDELTA_STATE_NEW, "a different file of the same name or size does not");
            char note[96];
            XdeltaImportTest_History(assets.c_str(), s->patchSha256, note, sizeof note);
            CHECK(std::strstr(note, "Imported before: 1 of") != nullptr, "the review shows what was imported before");
            XdeltaImport_RecordResult(assets.c_str(), "Pack.xdelta", s, offered.data(), (int)offered.size(), "");
            XdeltaImport_PatchStates(assets.c_str(), names, 4, st, nullptr);
            CHECK(st[2].state == XDELTA_STATE_IMPORTED && st[2].imported == (int)offered.size(), "every offered track installed is imported");
            XdeltaImport_RecordResult(assets.c_str(), "Pack.xdelta", s, nullptr, 0, "Disk full");
            XdeltaImport_PatchStates(assets.c_str(), names, 4, st, nullptr);
            CHECK(st[2].state == XDELTA_STATE_IMPORTED && !std::strcmp(st[2].reason, "Disk full"), "a later failed retry does not undo a complete import");
            /* A scan that failed before its hash was known is found by name and size. */
            XdeltaImportSummary none = {}; none.patchBytes = 100;
            XdeltaImport_RecordResult(assets.c_str(), "Failed.xdelta", &none, nullptr, 0, "Not an xdelta patch");
            XdeltaImport_PatchStates(assets.c_str(), names, 4, st, nullptr);
            CHECK(st[0].state == XDELTA_STATE_FAILED && !std::strcmp(st[0].reason, "Not an xdelta patch"), "a failed scan shows its reason");
            /* The worker returns the same states. */
            CHECK(XdeltaImport_StartPatchStates(assets.c_str(), names, 4), "the state job starts");
            XdeltaPatchState polled[CTR_XDELTA_FOLDER_MAX];
            int pollState = 0;
            for (int i = 0; i < 2000 && (pollState = XdeltaImport_PollPatchStates(polled, CTR_XDELTA_FOLDER_MAX)) == 0; i++) usleep(5000);
            CHECK(pollState == 1 && polled[0].state == XDELTA_STATE_FAILED && polled[2].state == XDELTA_STATE_IMPORTED, "the worker reports the same states");
            /* A corrupt record is ignored, not fatal. */
            write_path(assets + "/tracks/import-record.json", Bytes(20, '{'));
            CHECK(XdeltaImport_PatchStates(assets.c_str(), names, 4, st, nullptr) && st[2].state == XDELTA_STATE_NEW, "a damaged record reads as empty");
            XdeltaImport_RecordResult(assets.c_str(), "Pack.xdelta", s, offered.data(), 1, "");
            XdeltaImport_PatchStates(assets.c_str(), names, 4, st, nullptr);
            CHECK(st[2].state == XDELTA_STATE_PARTLY, "the record is rewritten after damage");
            XdeltaImport_RecordResult(assets.c_str(), "Pack.xdelta", s, nullptr, 0, "Disk full");
            XdeltaImport_PatchStates(assets.c_str(), names, 4, st, nullptr);
            CHECK(st[2].state == XDELTA_STATE_PARTLY && st[2].imported == 1 && !std::strcmp(st[2].reason, "Disk full"),
                  "a failure after some tracks keeps them and says why");
        }
        XdeltaImport_Free(&r.scan);
    }

    /* Refusals. */
    {
        EncodeOptions bad; bad.corruptAdler = true;
        ScanResult r = scan(write_file("Bad.xdelta", encode(baseDisc, patchedDisc, bad)), discPath);
        CHECK(!r.ok && r.error == "Patch does not match this disc", "bad adler32 refused");
        CHECK(scratch_clean(), "scratch removed after a failure");
        Bytes truncated = encode(baseDisc, patchedDisc);
        truncated.resize(truncated.size() - 7);
        r = scan(write_file("Short.xdelta", truncated), discPath);
        CHECK(!r.ok && r.error.rfind("Damaged patch", 0) == 0, "truncated patch refused");
        EncodeOptions bare; bare.adler = false;
        r = scan(write_file("Bare.xdelta", encode(baseDisc, patchedDisc, bare)), discPath);
        CHECK(!r.ok && r.error == "Patch has no checksums", "windows without adler32 refused");
        EncodeOptions lzma; lzma.lzmaFlag = true;
        r = scan(write_file("Lzma.xdelta", encode(baseDisc, patchedDisc, lzma)), discPath);
        CHECK(!r.ok && r.error == "Unsupported patch compression", "LZMA refused");
        r = scan(write_file("Text.xdelta", Bytes(100, 'x')), discPath);
        CHECK(!r.ok && r.error == "Not an xdelta patch", "not a patch");
        /* A sector the fine-grained patch copies from the disc: change it on the "other" disc. */
        EncodeOptions fine; fine.window = 2352;
        const Bytes finePatch = encode(baseDisc, patchedDisc, fine);
        size_t copied = 0;
        for (size_t lba = 20; lba * 2352 + 2352 <= std::min(baseDisc.size(), patchedDisc.size()) && !copied; lba++)
        {
            bool same = !std::memcmp(&baseDisc[lba * 2352], &patchedDisc[lba * 2352], 2352), zero = true;
            for (size_t i = 0; i < 2352 && zero; i++) zero = !baseDisc[lba * 2352 + i];
            if (same && !zero) copied = lba;
        }
        CHECK(copied != 0, "fixture has a copied sector");
        Bytes otherDisc = baseDisc;
        otherDisc[copied * 2352 + 2000] ^= 0xff;
        const std::string fineP = write_file("Fine.xdelta", finePatch);
        r = scan(fineP, discPath);
        CHECK(r.ok, "sector-sized windows decode too");
        XdeltaImport_Free(&r.scan);
        r = scan(fineP, write_file("other.bin", otherDisc));
        CHECK(!r.ok && r.error == "Patch does not match this disc", "patch for another disc refused");
        {
            const Bytes shortSrc(baseDisc.begin(), baseDisc.begin() + (long)(copied * 2352));
            struct Ctx { const Bytes *patch; size_t at; const Bytes *src; } ctx{&finePatch, 0, &shortSrc};
            NativeXdeltaIo io = {
                [](void *c, void *d, size_t n) -> long { auto *x = (Ctx *)c; size_t t = std::min(n, x->patch->size() - x->at); std::memcpy(d, x->patch->data() + x->at, t); x->at += t; return (long)t; },
                [](void *c, unsigned long long off, void *d, size_t n) -> long { auto *x = (Ctx *)c; if (off >= x->src->size()) return 0; size_t t = std::min<size_t>(n, x->src->size() - off); std::memcpy(d, x->src->data() + off, t); return (long)t; },
                [](void *, const void *, size_t) -> int { return 1; },
                &ctx, (unsigned long long)copied * 2352, nullptr};
            char error[256];
            CHECK(NativeXdelta_Decode(&io, error, sizeof error) != NATIVE_XDELTA_OK, "patch reading past the end of the disc refused");
        }
        XdeltaImportRequest real = {patchPath.c_str(), discPath.c_str(), nullptr, root.c_str(), 0};
        XdeltaImportScan *none = nullptr; char error[256];
        CHECK(!XdeltaImport_Scan(&real, &none, nullptr, nullptr, error, sizeof error) &&
              !std::strcmp(error, "Your disc is not the retail NTSC-U image this patch needs"), "non-retail disc refused");
    }

    std::string cleanup = "rm -rf " + root;
    if (std::system(cleanup.c_str())) {}
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
