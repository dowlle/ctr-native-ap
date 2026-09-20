// Focused integration assertions for the REAL shared BIGFILE reader,
// AP_RetailAsset_ReadSubfile in ap/ap_retail_asset.c (#256, shared with the
// #219 crystal harvest and #222 Wumpa harvest).
//
// The reader is included directly, so every decision it makes is production
// code: the entry/size bounds, the whole-sector read contract, the DRAM vs
// texture shape split, the pointer-map validation in ap_dram_ptr_map_logic.h,
// and the call into the engine fixup. Only the sector read BENEATH it is
// stubbed: the asset-source layer (NativeAssets_OpenHostBigfile /
// NativeDiscImage_*) hands the synthetic fixture bytes to the real reader. No
// reimplementation of the reader's logic is under test.
//
//   cc -Wall -Wextra -m32 -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I . -I include -I ap
//      -ffunction-sections -fdata-sections -Wl,--gc-sections
//      -o /tmp/test-retail-asset-reader tools/test-retail-asset-reader.c
//   /tmp/test-retail-asset-reader
//
// -m32 is required: the engine headers static-assert retail structure sizes,
// and the DRAM fixup stores 32-bit pointers. Exit 0 = every assertion held.
//
// WHAT THIS EXISTS TO PREVENT
// ---------------------------
// The Wumpa harvest harness (tools/test-wumpa-residency.c) stubs this reader, so
// it never executes the production sector reader, the pointer-map validator, or
// the LOAD_RunPtrMap integration. This harness closes that gap with a synthetic
// DRAM fixture. The specific regression it pins is the review's correction 1:
// when AP_DramPtrMap_Validate rejects a map, the reader must FAIL (return 0), not
// hand back an unrelocated body as success. A texture (non-DRAM) read must stay
// completely unaffected, since it never has a pointer map.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <common.h>

#include "../ap/ap_retail_asset.c"

// ── engine stubs ────────────────────────────────────────────────────────────
struct sData sdata_static;

// The production unity translation unit cannot be linked into this single-TU
// harness (a second TU would collide on the engine globals), so the engine fixup
// is mirrored here EXACTLY as it ships in game/LOAD/LOAD_Assets.c:16-28. The real
// reader calls it, so the reader's integration with the fixup is still what the
// assertions exercise.
void LOAD_RunPtrMap(char *origin, int *patchArr, int numPtrs)
{
	int *ptrCurrOffset;

	for (ptrCurrOffset = &patchArr[0]; ptrCurrOffset < &patchArr[numPtrs]; ptrCurrOffset++)
	{
		int offset = (*ptrCurrOffset >> 2) << 2;
		*(int *)&origin[offset] = *(int *)&origin[offset] + (int)origin;
	}
}

// ── the stubbed sector read (the only thing beneath the real reader) ─────────
static unsigned char g_sector[0x1000];
static int           g_sectorReads;

FILE *NativeAssets_OpenHostBigfile(const char *mode)
{
	(void)mode;
	return 0; // no loose BIGFILE.BIG: fall through to the disc-image path
}

int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut)
{
	(void)path;
	if (fileOut != 0)
	{
		fileOut->lba = 0;
		fileOut->size = 0;
	}
	return 1;
}

int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst)
{
	(void)file;
	(void)sector;
	g_sectorReads++;
	memcpy(dst, g_sector, (size_t)sectorCount * 2048u);
	return 1;
}

// ── test bookkeeping ────────────────────────────────────────────────────────
static int g_checks;
static int g_failures;

static void expect(int got, int want, const char *what)
{
	g_checks++;
	if (got != want)
	{
		g_failures++;
		printf("FAIL %s: got %d (want %d)\n", what, got, want);
	}
}

static void expect_true(int cond, const char *what)
{
	g_checks++;
	if (!cond)
	{
		g_failures++;
		printf("FAIL %s\n", what);
	}
}

// ── synthetic DRAM subfile layout ───────────────────────────────────────────
//   dst[0..3]           pointer-map offset, relative to the body at dst+4
//   body = dst+4
//   body[0x20..]        the map: numBytes, then the offsets array
//   body[0x40]          pointer word A (pre-fixup relative value 0x100)
//   body[0x44]          pointer word B (pre-fixup relative value 0x200)
//   body[0x50]          a non-pointer marker that must never be touched
//
// The reader writes whole sectors, so the source is sector-sized (two sectors)
// and the stub above copies it into dst.
#define RD_SUBFILE_SIZE 0x1000
#define RD_DST_BYTES    0x1000
#define RD_MAP_OFF      0x20
#define RD_PTR_A        0x40
#define RD_PTR_B        0x44
#define RD_MARKER       0x50
#define RD_REL_A        0x100u
#define RD_REL_B        0x200u

static struct
{
	struct BigHeader header;
	struct BigEntry  entry[1];
} g_bigfile;

static void put32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xffu);
	p[1] = (unsigned char)((v >> 8) & 0xffu);
	p[2] = (unsigned char)((v >> 16) & 0xffu);
	p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static unsigned int read32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void setup_reader(void)
{
	g_bigfile.header.cdpos = 0;
	g_bigfile.header.numEntry = 1;
	g_bigfile.entry[0].offset = 0; // sector index; the stub ignores it
	g_bigfile.entry[0].size = RD_SUBFILE_SIZE;

	memset(&sdata_static, 0, sizeof sdata_static);
	sdata_static.ptrBigfile1 = (struct BigHeader *)&g_bigfile;
	g_sectorReads = 0;
}

// A valid two-entry pointer map, with relative pointer words at the two slots.
static void build_valid_subfile(void)
{
	memset(g_sector, 0, sizeof g_sector);

	put32(g_sector + 0, RD_MAP_OFF); // dst[0]: ptrMapOffset, relative to body
	put32(g_sector + 4 + RD_MAP_OFF, 8u); // numBytes: two four-byte offsets
	put32(g_sector + 4 + RD_MAP_OFF + 4, RD_PTR_A);
	put32(g_sector + 4 + RD_MAP_OFF + 8, RD_PTR_B);

	put32(g_sector + 4 + RD_PTR_A, RD_REL_A);
	put32(g_sector + 4 + RD_PTR_B, RD_REL_B);

	g_sector[4 + RD_MARKER] = 0xAA;
}

// ── the valid DRAM read ─────────────────────────────────────────────────────
static void test_valid_dram_read(void)
{
	static unsigned char dst[RD_DST_BYTES];
	unsigned char       *ret;
	unsigned char       *body;
	unsigned int         base;
	int                  outSize = -1;

	setup_reader();
	build_valid_subfile();

	ret = AP_RetailAsset_ReadSubfile(0, 1, dst, (int)sizeof dst, &outSize);
	body = dst + 4;
	base = (unsigned int)(uintptr_t)body;

	expect_true(ret == body, "valid DRAM read returns the body at dst+4");
	expect(outSize, RD_SUBFILE_SIZE - 4, "valid DRAM read reports the body size");
	expect(g_sectorReads, 1, "valid DRAM read performs exactly one sector read");

	// The accepted map ran the fixup: each pointer word gained the body base.
	expect(read32(dst + 4 + RD_PTR_A), (int)(base + RD_REL_A), "valid map relocates pointer word A");
	expect(read32(dst + 4 + RD_PTR_B), (int)(base + RD_REL_B), "valid map relocates pointer word B");
	expect(read32(dst + 4 + RD_MAP_OFF), 8u, "valid map header is left intact");

	// A non-pointer byte is not touched by the fixup.
	expect(dst[4 + RD_MARKER], 0xAA, "non-pointer byte survives the fixup");
}

// The stored patch offset is masked to four bytes by the engine fixup, so an
// unaligned offset that names the word at 0x40 must still relocate 0x40.
static void test_align_down(void)
{
	static unsigned char dst[RD_DST_BYTES];
	unsigned char       *ret;
	unsigned int         base;
	int                  outSize = -1;

	setup_reader();
	build_valid_subfile();
	put32(g_sector + 4 + RD_MAP_OFF + 4, RD_PTR_A + 1u); // stored 0x41 -> 0x40

	ret = AP_RetailAsset_ReadSubfile(0, 1, dst, (int)sizeof dst, &outSize);
	base = (unsigned int)(uintptr_t)(dst + 4);

	expect_true(ret != 0, "unaligned stored patch offset is accepted");
	expect(read32(dst + 4 + RD_PTR_A), (int)(base + RD_REL_A), "stored patch offset aligns down");
}

// ── rejected maps: fail and leave the pointer words unmodified ──────────────
static void reject_count(void)
{
	put32(g_sector + 4 + RD_MAP_OFF, 6u); // numBytes not a multiple of four
}

static void reject_extent(void)
{
	put32(g_sector + 4 + RD_MAP_OFF, 0x2000u); // offsets array runs past the body
}

static void reject_patch_offset(void)
{
	put32(g_sector + 4 + RD_MAP_OFF, 4u); // one offset...
	put32(g_sector + 4 + RD_MAP_OFF + 4, 0x2000u); // ...naming a word past the body
}

static void run_rejected_case(void (*mutate)(void), const char *what)
{
	static unsigned char dst[RD_DST_BYTES];
	unsigned char       *ret;
	int                  outSize = 0x12345678;

	setup_reader();
	build_valid_subfile();
	mutate();

	ret = AP_RetailAsset_ReadSubfile(0, 1, dst, (int)sizeof dst, &outSize);

	expect_true(ret == 0, what);
	expect(outSize, 0x12345678, "outSize is untouched on a rejected map");

	// The sector read still ran, so the buffer holds the fixture; the fixup did
	// NOT, so the pointer words keep their pre-fixup relative values. Any
	// relocation here would mean the reader returned an unfixed body as success.
	expect(read32(dst + 4 + RD_PTR_A), RD_REL_A, "pointer word A unmodified on rejection");
	expect(read32(dst + 4 + RD_PTR_B), RD_REL_B, "pointer word B unmodified on rejection");
	expect(dst[4 + RD_MARKER], 0xAA, "marker unmodified on rejection");
}

static void test_rejected_maps(void)
{
	run_rejected_case(reject_count, "rejected count fails the read");
	run_rejected_case(reject_extent, "rejected extent fails the read");
	run_rejected_case(reject_patch_offset, "rejected patch offset fails the read");
}

// ── a texture (non-DRAM) read is unaffected ─────────────────────────────────
static void test_non_dram_read(void)
{
	static unsigned char dst[RD_DST_BYTES];
	unsigned char       *ret;
	int                  outSize = -1;

	setup_reader();
	build_valid_subfile();
	put32(g_sector + 0, 0x20u); // texture-shaped first word
	put32(g_sector + 4 + RD_PTR_A, 0x0BADF00Du);
	put32(g_sector + 4 + RD_PTR_B, 0x0BADF00Du);

	ret = AP_RetailAsset_ReadSubfile(0, 0, dst, (int)sizeof dst, &outSize);

	expect_true(ret == dst, "non-DRAM read returns the buffer itself");
	expect(outSize, RD_SUBFILE_SIZE, "non-DRAM read reports the whole size");
	expect(read32(dst), 0x20u, "non-DRAM first word is not interpreted");
	expect(read32(dst + 4 + RD_PTR_A), 0x0BADF00Du, "non-DRAM pointer word A untouched");
	expect(read32(dst + 4 + RD_PTR_B), 0x0BADF00Du, "non-DRAM pointer word B untouched");
}

int main(void)
{
	test_valid_dram_read();
	test_align_down();
	test_rejected_maps();
	test_non_dram_read();

	if (g_failures != 0)
	{
		printf("\n%d of %d assertion(s) FAILED\n", g_failures, g_checks);
		return 1;
	}

	printf("all %d assertions held\n", g_checks);
	return 0;
}
