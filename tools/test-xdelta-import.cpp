// gcc -m32 -std=c++17 -Wall -Wextra -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DUTF8PROC_STATIC -I include -I . -I ap/vendor/json/include -I vendor/utf8proc
// -x c++ tools/test-xdelta-import.cpp platform/native_xdelta_import.cpp platform/native_custom_package.cpp platform/native_custom_package_files.cpp
// -x c platform/native_xdelta_decode.c platform/native_custom_package_owned.c platform/native_custom_content_verify.c platform/native_custom_offline.c platform/native_custom_unicode.c vendor/utf8proc/utf8proc.c -x none
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
#include <platform/native_custom_package.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

using Bytes = std::vector<unsigned char>;
static int checks, failures;
#define CHECK(cond, name) do { checks++; if (!(cond)) { failures++; std::printf("FAIL %s (line %d)\n", name, __LINE__); } } while (0)

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

int main()
{
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
        CHECK(t1 && !std::strcmp(t1->title, "Pack slot 1"), "no descriptor: slot name from the patch name");
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
        CHECK(t7 && !t7->offered && !std::strcmp(t7->reason, "Same track as Pack slot 1"), "duplicate pair deduplicated");
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
        CHECK(XdeltaImport_Install(a.scan, selected.data(), assets.c_str(), error, sizeof error) == 2, "two tracks installed");
        CHECK(XdeltaImport_Install(a.scan, selected.data(), assets.c_str(), error, sizeof error) == 2, "reinstall is harmless");
        struct stat info;
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

    /* Descriptor: titles, laps, relic targets; one for another patch is ignored with a note. */
    if (a.ok)
    {
        const XdeltaImportSummary *s = XdeltaImport_Summary(a.scan);
        nlohmann::json d = {{"descriptor_version", 1}, {"patch_sha256", s->patchSha256}, {"pack_title", "Test Pack"},
                            {"pack_author", "Someone"},
                            {"tracks", {{"1", {{"title", "Snow Lane"}, {"laps", 5}, {"relic_targets", {{"sapphire", 90000}, {"gold", 80000}, {"platinum", 70000}}}, {"excluded", {"ctr_tokens"}}}},
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
            CHECK(XdeltaImport_Install(c.scan, selected.data(), assets.c_str(), error, sizeof error) == 2, "descriptor packages install");
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
        }
        XdeltaImport_Free(&c.scan);
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
            CHECK(item_for(s4, 1) && !std::strcmp(item_for(s4, 1)->title, "Pack slot 1"), "slot name without a usable descriptor");
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
