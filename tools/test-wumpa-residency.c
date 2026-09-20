// Behavioral assertions for the Wumpa Fruit residency gate and compact loader
// (#222). Drives the REAL production code: the freestanding decision core in
// ap/ap_wumpa_residency_logic.h, the pointer-map rule in
// ap/ap_dram_ptr_map_logic.h, and the production harvest/register unit
// ap/ap_retail_wumpa.c (included directly, with the engine read and log stubbed).
//
// cc -Wall -Wextra -m32 -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I . -I include -I ap
//    -ffunction-sections -fdata-sections -Wl,--gc-sections
//    -o /tmp/test-wumpa-residency tools/test-wumpa-residency.c
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// WHAT THIS EXISTS TO PREVENT
// ---------------------------
// Lessons Learned §24: the pad reassigns a LIVE instance's model pointer, and
// gGT->modelPtr[] is refilled per level, so on the hub the retail fruit model is
// not loaded. A display resolver that hands back a model id nobody checked
// leaves the slot showing whatever it showed a moment earlier. This harness
// exists so that a Wumpa package which cannot reach the fruit model resolves to
// the marker CONCRETELY, and so the harvest that supplies the fruit is bounded:
// every malformed source, every failed allocation, and every out-of-span pointer
// is refused with a marker fallback and exactly one redacted log line, never a
// crash or a stale model.
//
// The synthetic fixture is built in the harness, so CI needs no disc, no
// display, no seed and no network. It reproduces the measured entry-117 shape:
// 12,760 bytes, four named headers, one 32-frame spin each, 11,520 animation
// bytes, no texture references, and the 21 pointer slots at their fixed offsets.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <common.h>
#include "../ap/ap_hooks.h"
#include "../ap/ap_dram_ptr_map_logic.h"
#include "../ap/ap_retail_wumpa.c"

// ── engine stubs ────────────────────────────────────────────────────────────
struct sData sdata_static;
ctr_seed_config ctr_cfg;

static int g_logCount;
static char g_lastLog[256];

void AP_LogLine(const char *line)
{
	g_logCount++;
	snprintf(g_lastLog, sizeof g_lastLog, "%s", line ? line : "");
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

// ── fixture layout, mirroring the measured entry-117 span ───────────────────
// The span begins at the model struct, so the model is at span offset 0 and the
// header array at span offset 0x18.
#define FIX_BODY_SIZE 0x8000u
#define FIX_SPAN_OFF 0x1000u
#define FIX_MODEL_ARRAY_OFF 0x100u

static const unsigned int FIX_CMD_TARGET[4] = {0x13c, 0x1340, 0x2158, 0x2b8c};
static const unsigned int FIX_COL_TARGET[4] = {0x234, 0x13e8, 0x21c0, 0x2bac};
static const unsigned int FIX_ANIMARR_TARGET[4] = {0x138, 0x133c, 0x2154, 0x2b88};
static const unsigned int FIX_ANIM_TARGET[4] = {0x2a4, 0x143c, 0x21f0, 0x2bc0};
static const int FIX_FRAME_SIZE[4] = {132, 104, 76, 48};

static void put32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xffu);
	p[1] = (unsigned char)((v >> 8) & 0xffu);
	p[2] = (unsigned char)((v >> 16) & 0xffu);
	p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static void put16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xffu);
	p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static void putname(unsigned char *p, const char *name)
{
	int i;
	for (i = 0; i < 0x10; i++)
		p[i] = (unsigned char)((name[i] != '\0') ? name[i] : '\0');
}

// Build a valid DRAM body at `body`, with the given 32-bit base address that the
// fixup would have added. Returns the fruit model offset (== FIX_SPAN_OFF).
static unsigned int build_valid_body(unsigned char *body, unsigned int base)
{
	unsigned char *span = body + FIX_SPAN_OFF;
	unsigned int spanBase = base + FIX_SPAN_OFF;
	int h, i;

	memset(body, 0, FIX_BODY_SIZE);

	// Level header: numModels at +0x14, model array pointer at +0x18.
	put32(body + 0x14, 1u);
	put32(body + 0x18, base + FIX_MODEL_ARRAY_OFF);
	put32(body + FIX_MODEL_ARRAY_OFF, spanBase); // model[0] -> span start

	// struct Model at span offset 0. The retail model id 0x02 is named `fruit`
	// (measured from entry 117; "wumpa" is NOT the stored name).
	putname(span + 0x00, "fruit");
	put16(span + 0x10, 0x0002u); // PU_WUMPA_FRUIT
	put16(span + 0x12, 4u);
	put32(span + 0x14, spanBase + 0x18u); // headers

	for (h = 0; h < 4; h++)
	{
		unsigned char *hdr = span + 0x18u + (unsigned int)h * 0x40u;
		unsigned int anim = FIX_ANIM_TARGET[h];

		putname(hdr, AP_WUMPA_HEADER_NAMES[h]);
		put32(hdr + 0x20, spanBase + FIX_CMD_TARGET[h]);     // command list
		put32(hdr + 0x28, spanBase + FIX_COL_TARGET[h]);     // texture layouts
		put32(hdr + 0x2c, spanBase + FIX_COL_TARGET[h]);     // colours
		put32(hdr + 0x34, 1u);                               // one animation
		put32(hdr + 0x38, spanBase + FIX_ANIMARR_TARGET[h]); // animation array

		// Command list: one command, then the 0xffffffff terminator, no texture.
		put32(span + FIX_CMD_TARGET[h], 1u);
		put32(span + FIX_CMD_TARGET[h] + 4u, 0xffffffffu);

		// Colour table: one word, sized by the command count.
		put32(span + FIX_COL_TARGET[h], 0x00000000u);

		// Animation array: one pointer to the animation struct.
		put32(span + FIX_ANIMARR_TARGET[h], spanBase + anim);

		// Animation struct: name, 32 frames of the measured size.
		putname(span + anim, "spin");
		put16(span + anim + 0x10, 32u);
		put16(span + anim + 0x12, (unsigned int)FIX_FRAME_SIZE[h]);
	}

	// The 21 pointer slots, at their fixed offsets, to their measured targets.
	for (i = 0; i < AP_WUMPA_SLOT_COUNT; i++)
		put32(span + AP_WUMPA_SLOT_OFFSETS[i], spanBase + AP_WUMPA_SLOT_TARGETS[i]);

	return FIX_SPAN_OFF;
}

// ── read stub with scenarios ────────────────────────────────────────────────
enum
{
	SCEN_VALID = 0,
	SCEN_READ_FAIL,
	SCEN_ENTRY_ABSENT,
	SCEN_SHAPE_FRAME,
	SCEN_BOUNDS_HEADER,
	SCEN_HEADER_COUNT
};

static int g_scenario;
static int g_lastBodySize;

unsigned char *AP_RetailAsset_ReadSubfile(int subfileIndex, int isDramFile, unsigned char *dst, int dstSize, int *outSize)
{
	unsigned char *body = dst + 4; // mimic the DRAM descriptor word at dst[0]
	unsigned int base = (unsigned int)(unsigned long)body;
	unsigned int fruit;

	(void)subfileIndex;
	(void)isDramFile;

	if (g_scenario == SCEN_READ_FAIL)
		return 0;

	fruit = build_valid_body(body, base);

	if (g_scenario == SCEN_ENTRY_ABSENT)
		put16(body + 0x14, 0u); // no models

	if (g_scenario == SCEN_SHAPE_FRAME)
		put16(body + fruit + FIX_ANIM_TARGET[0] + 0x10, 31u); // 31 frames

	if (g_scenario == SCEN_BOUNDS_HEADER)
		put32(body + fruit + 0x14, base + FIX_BODY_SIZE + 0x100u); // header ptr out of body

	if (g_scenario == SCEN_HEADER_COUNT)
		put16(body + fruit + 0x12, 3u); // three headers

	g_lastBodySize = (int)FIX_BODY_SIZE;
	if (outSize)
		*outSize = g_lastBodySize;
	(void)dstSize;
	return body;
}

// ── allocation seam counters ────────────────────────────────────────────────
static int g_allocCount;
static int g_freeCount;
static int g_allocFail;

static void *test_alloc(unsigned long n)
{
	g_allocCount++;
	if (g_allocFail)
		return 0;
	return malloc((size_t)n);
}

static void test_free(void *p)
{
	g_freeCount++;
	free(p);
}

static void reset_module(void)
{
	s_wumpaState = AP_WUMPA_RESIDENCY_UNKNOWN;
	s_wumpaModel = 0;
	s_wumpaModelOffset = 0;
	memset(s_wumpaSpan, 0, AP_WUMPA_SPAN_BYTES);
	g_allocCount = g_freeCount = 0;
	g_allocFail = 0;
	g_logCount = 0;
	g_lastLog[0] = '\0';
	g_scenario = SCEN_VALID;
}

// ── category truth table (retained from the evidence probe) ─────────────────
static void expect_wumpa(long long index, int residency, int levelPresent, int wantDrawable, int wantModel, int wantTint)
{
	long long id = AP_ITEM_BASE + index;
	AP_ItemCat cat = AP_ItemCategory(id);
	int drawable = AP_WumpaModelDrawable(residency, levelPresent);
	AP_ItemCat eff = AP_WumpaEffectiveCategory(cat, drawable);
	int model = AP_WUMPA_MODEL_UNCHANGED;
	int assigned = AP_WumpaAssignModel(cat, drawable, &model);

	expect((int)cat, (int)AP_CAT_WUMPA, "Wumpa category");
	expect(drawable, wantDrawable, "Wumpa drawable");
	expect((int)eff, wantDrawable ? (int)AP_CAT_WUMPA : (int)AP_CAT_NONE, "Wumpa effective category");
	expect(assigned, 1, "Wumpa always takes the slot");
	expect(model, wantModel, "Wumpa assigned model");
	expect_true(model != AP_WUMPA_MODEL_UNCHANGED, "Wumpa model is never the unchanged sentinel");
	expect(AP_WumpaResolvedTint(cat, drawable, 0x12345678), wantTint, "Wumpa own tint");
}

static void test_category_truth_table(void)
{
	expect_wumpa(15, AP_WUMPA_RESIDENCY_READY, 0, 1, AP_MODEL_WUMPA, 0);
	expect_wumpa(120, AP_WUMPA_RESIDENCY_READY, 0, 1, AP_MODEL_WUMPA, 0);
	expect_wumpa(121, AP_WUMPA_RESIDENCY_READY, 0, 1, AP_MODEL_WUMPA, 0);

	// A loose-fruit track already carries the model: the client steps aside.
	expect_wumpa(15, AP_WUMPA_RESIDENCY_UNKNOWN, 1, 1, AP_MODEL_WUMPA, 0);
	expect_wumpa(15, AP_WUMPA_RESIDENCY_FAILED, 1, 1, AP_MODEL_WUMPA, 0);

	// The missing-model fallback: the case that must not silently keep a model.
	expect_wumpa(15, AP_WUMPA_RESIDENCY_FAILED, 0, 0, STATIC_AP, 0x12345678);
	expect_wumpa(120, AP_WUMPA_RESIDENCY_FAILED, 0, 0, STATIC_AP, 0x12345678);
	expect_wumpa(121, AP_WUMPA_RESIDENCY_FAILED, 0, 0, STATIC_AP, 0x12345678);

	// Not tried yet behaves like failure until the harvest lands.
	expect_wumpa(15, AP_WUMPA_RESIDENCY_UNKNOWN, 0, 0, STATIC_AP, 0x12345678);

	// Non-Wumpa categories are untouched.
	expect(AP_WumpaAssignModel(AP_CAT_CRYSTAL, 1, &(int){0}), 0, "crystal declines the Wumpa slot");
	expect(AP_WumpaAssignModel(AP_CAT_WUMPA, 1, 0), 0, "null out pointer refuses");
	expect(AP_WumpaEffectiveCategory(AP_CAT_CRYSTAL, 0), AP_CAT_CRYSTAL, "crystal passes through");
	expect(AP_WumpaEffectiveCategory(AP_CAT_NONE, 0), AP_CAT_NONE, "none passes through");
	expect(AP_WumpaResolvedTint(AP_CAT_WUMPA, 0, 0x0000ff00), 0x0000ff00, "degraded Wumpa uses class tint");
	expect_true(AP_WumpaResolvedTint(AP_CAT_WUMPA, 0, 0) != 0, "degraded Wumpa tint is never 0");
	expect(AP_MODEL_WUMPA, 0x02, "AP_MODEL_WUMPA is PU_WUMPA_FRUIT");
}

// ── pointer-map validation ──────────────────────────────────────────────────
static void test_pointer_map_validation(void)
{
	unsigned char body[0x100];
	int n;

	// A valid two-entry map at offset 0x10.
	memset(body, 0, sizeof body);
	put32(body + 0x10, 8u);        // numBytes = two offsets
	put32(body + 0x14, 0x40u);     // patch offset
	put32(body + 0x18, 0x44u);
	n = -1;
	expect(AP_DramPtrMap_Validate(body, sizeof body, 0x10, &n), 1, "valid pointer map accepted");
	expect(n, 2, "valid pointer map count");

	// Header does not fit.
	expect(AP_DramPtrMap_Validate(body, sizeof body, 0xff, &n), 0, "header past body rejected");
	expect(AP_DramPtrMap_Validate(body, sizeof body, -4, &n), 0, "negative map offset rejected");

	// numBytes negative.
	put32(body + 0x10, 0x80000000u);
	expect(AP_DramPtrMap_Validate(body, sizeof body, 0x10, &n), 0, "negative numBytes rejected");

	// numBytes not a multiple of four.
	put32(body + 0x10, 6u);
	expect(AP_DramPtrMap_Validate(body, sizeof body, 0x10, &n), 0, "unaligned numBytes rejected");

	// Offsets array runs past the body.
	put32(body + 0x10, 0x1000u);
	expect(AP_DramPtrMap_Validate(body, sizeof body, 0x10, &n), 0, "oversized offsets array rejected");

	// An unaligned stored offset is aligned DOWN, exactly as LOAD_RunPtrMap masks
	// it, so 0x41 names the word at 0x40 and is accepted.
	put32(body + 0x10, 4u);
	put32(body + 0x14, 0x41u);
	expect(AP_DramPtrMap_Validate(body, sizeof body, 0x10, &n), 1, "unaligned stored offset aligns down");

	// A word ending exactly at the body end is still a complete word.
	put32(body + 0x14, 0xfcu);
	expect(AP_DramPtrMap_Validate(body, sizeof body, 0x10, &n), 1, "word ending at body end accepted");

	// Patch offset naming a word that starts at or past the body end.
	put32(body + 0x14, 0x100u);
	expect(AP_DramPtrMap_Validate(body, sizeof body, 0x10, &n), 0, "patch offset past the last word rejected");

	// Patch offset past the body.
	put32(body + 0x14, 0x104u);
	expect(AP_DramPtrMap_Validate(body, sizeof body, 0x10, &n), 0, "patch offset past body rejected");
}

// ── shape validation on a synthetic fixture ─────────────────────────────────
static void test_shape_validation(void)
{
	static unsigned char body[FIX_BODY_SIZE];
	unsigned int lo, hi, fruit;
	int i;

	// The valid fixture is accepted and has exactly the measured span.
	fruit = build_valid_body(body, 0u);
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_NONE,
	       "valid fixture accepted");
	expect(hi - lo, AP_WUMPA_SPAN_BYTES, "valid fixture span");

	// Wrong model id.
	put16(body + fruit + 0x10, 0x03u);
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_SHAPE,
	       "wrong model id rejected");
	put16(body + fruit + 0x10, 0x02u);

	// Wrong model name: the same id under a name that is not the measured
	// `fruit` is refused. "wumpa" is the deliberate wrong case here.
	putname(body + fruit + 0x00, "wumpa");
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_SHAPE,
	       "wrong model name rejected");
	putname(body + fruit + 0x00, "fruit");

	// A name field with no terminator inside its 16 bytes is refused even when
	// the leading bytes match.
	for (i = 0; i < 0x10; i++)
		body[fruit + (unsigned int)i] = 'f';
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_SHAPE,
	       "unterminated model name rejected");
	putname(body + fruit + 0x00, "fruit");

	// Wrong header count.
	put16(body + fruit + 0x12, 3u);
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_HEADER,
	       "wrong header count rejected");
	put16(body + fruit + 0x12, 4u);

	// Wrong header name.
	putname(body + fruit + 0x18, "fruit_xx");
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_HEADER,
	       "wrong header name rejected");
	putname(body + fruit + 0x18, "fruit_hi");

	// Wrong animation count.
	put32(body + fruit + 0x18 + 0x34, 2u);
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_SHAPE,
	       "wrong animation count rejected");
	put32(body + fruit + 0x18 + 0x34, 1u);

	// Wrong frame count.
	put16(body + fruit + FIX_ANIM_TARGET[0] + 0x10, 31u);
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_SHAPE,
	       "wrong frame count rejected");
	put16(body + fruit + FIX_ANIM_TARGET[0] + 0x10, 32u);

	// Wrong animation name.
	putname(body + fruit + FIX_ANIM_TARGET[0], "spin2");
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_SHAPE,
	       "wrong animation name rejected");
	putname(body + fruit + FIX_ANIM_TARGET[0], "spin");

	// A texture reference above zero.
	put32(body + fruit + FIX_CMD_TARGET[0] + 4u, 0x00010001u);
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_SHAPE,
	       "texture index above zero rejected");
	put32(body + fruit + FIX_CMD_TARGET[0] + 4u, 0xffffffffu);

	// A different span size (frame size changes the animation end).
	put16(body + fruit + FIX_ANIM_TARGET[3] + 0x12, 47u);
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_SHAPE,
	       "wrong span size rejected");
	put16(body + fruit + FIX_ANIM_TARGET[3] + 0x12, 48u);

	// A header pointer outside the body.
	put32(body + fruit + 0x14, FIX_BODY_SIZE + 0x100u);
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, 0u, fruit, &lo, &hi), AP_WUMPA_FAIL_BOUNDS,
	       "header pointer out of body rejected");
}

// ── relocation on a synthetic fixture ───────────────────────────────────────
static void test_relocation(void)
{
	static unsigned char body[FIX_BODY_SIZE];
	static unsigned char dst[AP_WUMPA_SPAN_BYTES];
	unsigned int fruit, lo, hi, i;
	const unsigned int srcBase = 0x10000000u;
	const unsigned int dstBase = 0x20000000u;

	fruit = build_valid_body(body, srcBase);
	expect(AP_WumpaValidateShape(body, FIX_BODY_SIZE, srcBase, fruit, &lo, &hi), AP_WUMPA_FAIL_NONE,
	       "relocation fixture accepted");
	expect(AP_WumpaRelocate(body + lo, srcBase + lo, hi - lo, dst, dstBase), AP_WUMPA_FAIL_NONE,
	       "valid relocation accepted");

	// Every slot points at dstBase + its target.
	for (i = 0; i < AP_WUMPA_SLOT_COUNT; i++)
	{
		unsigned int got = AP_WumpaReadU32(dst + AP_WUMPA_SLOT_OFFSETS[i]);
		expect(got, dstBase + AP_WUMPA_SLOT_TARGETS[i], "relocated slot target");
	}
	// The model id survives the copy.
	expect(AP_WumpaReadS16(dst + (fruit - lo) + 0x10), 0x02, "copied model id");

	// Each slot corrupted below and above the span is refused, and the
	// destination is left untouched.
	for (i = 0; i < AP_WUMPA_SLOT_COUNT; i++)
	{
		unsigned int off = AP_WUMPA_SLOT_OFFSETS[i];
		unsigned int save = AP_WumpaReadU32(body + lo + off);
		unsigned char before[8];
		int rejected;

		memcpy(before, dst + off, 8);

		put32(body + lo + off, (srcBase + lo) - 4u); // below
		rejected = (AP_WumpaRelocate(body + lo, srcBase + lo, hi - lo, dst, dstBase) != AP_WUMPA_FAIL_NONE);
		expect_true(rejected, "slot below span rejected");
		expect_true(memcmp(before, dst + off, 8) == 0, "destination untouched on below-span rejection");

		put32(body + lo + off, srcBase + lo + AP_WUMPA_SPAN_BYTES); // above
		rejected = (AP_WumpaRelocate(body + lo, srcBase + lo, hi - lo, dst, dstBase) != AP_WUMPA_FAIL_NONE);
		expect_true(rejected, "slot above span rejected");

		// A target inside the span but not matching the measured table.
		put32(body + lo + off, srcBase + lo + 0x08u);
		rejected = (AP_WumpaRelocate(body + lo, srcBase + lo, hi - lo, dst, dstBase) != AP_WUMPA_FAIL_NONE);
		expect_true(rejected, "wrong target rejected");

		put32(body + lo + off, save);
	}

	// The valid map still relocates after the round trip.
	expect(AP_WumpaRelocate(body + lo, srcBase + lo, hi - lo, dst, dstBase), AP_WUMPA_FAIL_NONE,
	       "relocation still valid after corruption round trip");
}

// ── production harvest driver ───────────────────────────────────────────────
static struct GameTracker g_gt;

static void setup_driver(void)
{
	memset(&g_gt, 0, sizeof g_gt);
	sdata_static.gGT = &g_gt;
	sdata_static.ptrBigfile1 = (struct BigHeader *)0x1;
	sdata_static.Loading.stage = LOAD_IDLE;
	s_wumpaAlloc = test_alloc;
	s_wumpaFree = test_free;
}

static void test_driver_success_and_lifecycle(void)
{
	unsigned char copy[AP_WUMPA_SPAN_BYTES];
	unsigned int allocAfter;

	setup_driver();
	reset_module();

	AP_RetailWumpa_Register(&g_gt);
	expect(s_wumpaState, AP_WUMPA_RESIDENCY_READY, "harvest ready");
	expect(AP_RetailWumpa_IsReady(), 1, "IsReady");
	expect(g_allocCount, 1, "one allocation on success");
	expect(g_freeCount, 1, "one free on success");
	expect_true(g_gt.modelPtr[AP_MODEL_WUMPA] != 0, "fruit model parked");
	expect_true(g_gt.modelPtr[AP_MODEL_WUMPA] == s_wumpaModel, "parked pointer is the compact model");
	expect_true(strstr(g_lastLog, "harvested retail") != NULL, "success log line");
	expect_true(strchr(g_lastLog, '/') == NULL, "success log is path-free");

	// The compact copy is stable: snapshot it, force a re-park, compare.
	memcpy(copy, s_wumpaSpan, sizeof copy);
	allocAfter = (unsigned int)g_allocCount;

	// Simulate a level transition clearing the slot, then several idle frames.
	g_gt.modelPtr[AP_MODEL_WUMPA] = 0;
	AP_RetailWumpa_Register(&g_gt);
	expect_true(g_gt.modelPtr[AP_MODEL_WUMPA] == s_wumpaModel, "model reparked after a slot clear");
	expect(g_allocCount, (int)allocAfter, "no second harvest after ready");
	expect_true(memcmp(copy, s_wumpaSpan, sizeof copy) == 0, "compact copy unchanged across repark");

	// Repeated registration (reconnect / save reload) does not reset module state.
	{
		int i;
		for (i = 0; i < 5; i++)
			AP_RetailWumpa_Register(&g_gt);
	}
	expect(g_allocCount, (int)allocAfter, "no harvest on repeated registration");
	expect(s_wumpaState, AP_WUMPA_RESIDENCY_READY, "state survives repeated registration");

	// A level-owned retail model is never overwritten.
	{
		static struct Model levelModel;
		g_gt.modelPtr[AP_MODEL_WUMPA] = &levelModel;
		AP_RetailWumpa_Register(&g_gt);
		expect_true(g_gt.modelPtr[AP_MODEL_WUMPA] == &levelModel, "level-owned fruit not overwritten");
		expect(AP_RetailWumpa_IsDrawable(&g_gt), 1, "drawable with a level-owned model");
	}
}

static void test_driver_alloc_failure(void)
{
	setup_driver();
	reset_module();
	g_allocFail = 1;

	AP_RetailWumpa_Register(&g_gt);
	expect(s_wumpaState, AP_WUMPA_RESIDENCY_FAILED, "allocation failure is sticky");
	expect_true(g_gt.modelPtr[AP_MODEL_WUMPA] == 0, "no model published on allocation failure");
	expect(g_logCount, 1, "exactly one log line on allocation failure");
	expect_true(strstr(g_lastLog, "allocation") != NULL, "allocation reason logged");
	expect_true(strchr(g_lastLog, '/') == NULL, "allocation log is path-free");
	expect(g_freeCount, 0, "nothing freed when allocation failed");

	// Sticky: no retry on later frames.
	AP_RetailWumpa_Register(&g_gt);
	expect(g_allocCount, 1, "no retry after allocation failure");
}

static void test_driver_failure_paths(void)
{
	static const struct
	{
		int scenario;
		const char *reason;
	} cases[] = {
	    {SCEN_READ_FAIL, "read"},
	    {SCEN_ENTRY_ABSENT, "entry"},
	    {SCEN_HEADER_COUNT, "header"},
	    {SCEN_SHAPE_FRAME, "shape"},
	    {SCEN_BOUNDS_HEADER, "bounds"},
	};
	unsigned int c;

	for (c = 0; c < sizeof cases / sizeof cases[0]; c++)
	{
		setup_driver();
		reset_module();
		g_scenario = cases[c].scenario;

		AP_RetailWumpa_Register(&g_gt);
		expect(s_wumpaState, AP_WUMPA_RESIDENCY_FAILED, cases[c].reason);
		expect_true(g_gt.modelPtr[AP_MODEL_WUMPA] == 0, "no model published on failure");
		expect(g_logCount, 1, "exactly one log line per failed harvest");
		expect_true(strstr(g_lastLog, "harvest failed") != NULL, "failure log is the harvest line");
		expect_true(strstr(g_lastLog, cases[c].reason) != NULL, "failure reason is reported");
		expect_true(strchr(g_lastLog, '/') == NULL, "failure log is path-free");
		expect(g_allocCount, 1, "one allocation attempt");
		expect(g_freeCount, 1, "temporary buffer freed on every failure path");

		// The failed category resolves to the marker concretely.
		expect(AP_WumpaEffectiveCategory(AP_CAT_WUMPA, AP_RetailWumpa_IsDrawable(&g_gt)), AP_CAT_NONE,
		       "failed harvest resolves to marker category");

		// Sticky: no retry.
		AP_RetailWumpa_Register(&g_gt);
		expect(g_allocCount, 1, "no retry after failure");
		expect(g_logCount, 1, "no second log line after failure");
	}
}

// ── animation transition ────────────────────────────────────────────────────
static void test_animation_transition(void)
{
	unsigned char animIndex;
	short animFrame, vertSplit;
	unsigned int flags;
	int tick;

	// Entering the fruit from a marker resets all four fields exactly once.
	expect(AP_WumpaShouldResetAnim(STATIC_AP, AP_MODEL_WUMPA), 1, "marker -> fruit is a transition");
	expect(AP_WumpaShouldResetAnim(AP_MODEL_WUMPA, AP_MODEL_WUMPA), 0, "fruit -> fruit is not a transition");
	expect(AP_WumpaShouldClearAnim(AP_MODEL_WUMPA, STATIC_AP), 1, "fruit -> marker clears");
	expect(AP_WumpaShouldClearAnim(AP_MODEL_WUMPA, AP_MODEL_WUMPA), 0, "fruit -> fruit does not clear");

	animIndex = 9;
	animFrame = 77;
	vertSplit = 5;
	flags = AP_WUMPA_ANIM_STOP_AT_END; // the stale ping-pong bit
	AP_WumpaApplyFruitAnim(&animIndex, &animFrame, &vertSplit, &flags);
	expect(animIndex, 0, "fruit entry resets animIndex");
	expect(animFrame, 0, "fruit entry resets animFrame");
	expect(vertSplit, 0, "fruit entry resets vertSplit");
	expect_true((flags & AP_WUMPA_ANIM_LOOP) != 0, "fruit entry sets ANIM_LOOP");
	expect_true((flags & AP_WUMPA_ANIM_STOP_AT_END) == 0, "fruit entry clears ANIM_STOP_AT_END");

	// Advancing frames must not be reset every tick: the predicate is false once
	// the instance is already the fruit.
	animFrame = 3;
	for (tick = 0; tick < 10; tick++)
	{
		if (AP_WumpaShouldResetAnim(AP_MODEL_WUMPA, AP_MODEL_WUMPA))
			animFrame = 0;
	}
	expect(animFrame, 3, "fruit animation is not reset every tick");

	// Leaving the fruit clears both animation bits.
	flags = AP_WUMPA_ANIM_LOOP | AP_WUMPA_ANIM_STOP_AT_END;
	AP_WumpaClearFruitAnim(&flags);
	expect((flags & (AP_WUMPA_ANIM_LOOP | AP_WUMPA_ANIM_STOP_AT_END)), 0, "fruit exit clears both anim bits");
}

int main(void)
{
	test_category_truth_table();
	test_pointer_map_validation();
	test_shape_validation();
	test_relocation();
	test_driver_success_and_lifecycle();
	test_driver_alloc_failure();
	test_driver_failure_paths();
	test_animation_transition();

	if (g_failures != 0)
	{
		printf("\n%d of %d assertion(s) FAILED\n", g_failures, g_checks);
		return 1;
	}

	printf("all %d assertions held\n", g_checks);
	return 0;
}
