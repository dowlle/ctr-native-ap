#!/usr/bin/env python3
"""Run production pad requirement selection and digit display for every type.

Covers the hub Key floor plus stage 1 and stage 2 for 15 individual mask
bits and the three any-of families, counts 1 to 300: 37 rows, 11,100 cases.
Checks the requirement icon and full gate count, the displayed count capped
at 99, and header bounds against a fake eight-header digit model.
For counts above 99, also runs 14,874 production gate decisions immediately
below and at each required count, so the display cap cannot lower the gate.

Compiles AP_ContentMaskToReq, AP_ReqToUnlock, ap_pad_gate_layer, the content
plan birth block, gate condition and per-frame digit pass from production source. The
birth split and digit model choices are pinned to their production call
sites. This host test does not load retail assets or render a played hub.
Run from the repository root, with no arguments.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ap_dir = root / "ap"
warp = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")


def func(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at) + 3]


mask_to_req = func(warp, "static void AP_ContentMaskToReq(")
req_to_unlock = func(warp, "static void AP_ReqToUnlock(")
anchor = warp.index("unlockItem_numNeeded = pad ? pad->keys : 2147483647;")
assert warp.count("unlockItem_numNeeded = pad ? pad->keys : 2147483647;") == 1
plan_start = warp.rindex("\tif (ap_content_plan_active())\n\t{\n\t\tconst ctr_content_pad *pad", 0, anchor)
plan_end = warp.index("goto ContentPad_BuildGate;", plan_start)
plan_block = warp[plan_start:plan_end] + "goto ContentPad_BuildGate;\n\t}\n"
assert "pad->rule_count" in plan_block and "pad->stage2_count" in plan_block and "pad->keys" in plan_block

gate_start = warp.index("\tif (unlockItem_numOwned", warp.index("ContentPad_BuildGate:"))
gate_condition = warp[gate_start:warp.index("\n\t{", gate_start)]

allnum = func(warp, "void AH_WarpPad_AllWarppadNum()")
s = allnum.index("#ifdef CTR_AP\n")
digit_pass = allnum[s + len("#ifdef CTR_AP\n"):allnum.index("#else", s)]
assert "ap_warppad_split_digits(unlockItem_numNeeded, &warppadObj->digit10s, &warppadObj->digit1s);" in warp
assert 'INSTANCE_Birth3D(gGT->modelPtr[ap_warppad_digit_model(warppadObj->digit10s)], "warpnum", t);' in warp
assert "i = ap_warppad_digit_model(warppadObj->digit1s);" in warp
split = "ap_warppad_split_digits(unlockItem_numNeeded, &wp->digit10s, &wp->digit1s);"
tens_model = "ap_warppad_digit_model(wp->digit10s)"
ones_model = "ap_warppad_digit_model(wp->digit1s)"
include = '#include "ap_warppad_digits_logic.h"'

layer_selector = func((ap_dir / "ap_pad_birth.h").read_text(encoding="utf-8"),
                      "static inline int ap_pad_gate_layer(")

program = r"""
#include <stdio.h>
#include <string.h>
#define CTR_AP 1
""" + include + r"""
typedef short s16;
typedef struct { int type; int count; int colour; } ctr_req;
enum { STATIC_GEM = 0x5F, STATIC_RELIC = 0x61, STATIC_TROPHY = 0x62, STATIC_KEY = 0x63,
       STATIC_TOKEN = 0x7D, STATIC_BIG1 = 0x38 };
enum { AP_PAD_GATE_FLOOR = 0, AP_PAD_GATE_RULE = 1, AP_PAD_GATE_STAGE2 = 2 };
enum { AP_IDX_TROPHY = 0, AP_IDX_TOKEN_RED = 4, AP_IDX_KEY = 14 };
typedef struct { int keys, has_rule, rule_count; unsigned rule_mask;
                 int has_stage2, stage2_count; unsigned stage2_mask; } ctr_content_pad;

/* Keep each tested requirement unmet so the locked pad shows its count. */
static ctr_content_pad g_pad;
static int g_state;
static int g_keys;
static int g_owned;
static int AP_PadReplayable(int a, int b) { (void)a; (void)b; return 0; }
static int ap_content_plan_active(void) { return 1; }
static const ctr_content_pad *ap_content_plan_pad(int id) { (void)id; return &g_pad; }
static int AP_PadState(int a, int b) { (void)a; (void)b; return g_state; }
static int AP_GateCount(int idx) { return idx == AP_IDX_KEY ? g_keys : 0; }
static int AP_GateCountTokenColour(int c) { (void)c; return 0; }
static int AP_GateCountRelicTier(int c) { (void)c; return 0; }
static int AP_GateCountGemColour(int c) { (void)c; return 0; }
static int AP_GateCountTokenSum(void) { return 0; }
static int AP_GateCountRelicSum(void) { return 0; }
static int AP_GateCountGemSum(void) { return 0; }
static int AP_ContentPadRuleOwned(const ctr_content_pad *p) { (void)p; return g_owned; }
static int AP_ContentStage2Owned(const ctr_content_pad *p) { (void)p; return g_owned; }
static int AP_ReqRelicTintTier(const ctr_req *r) { return r->colour; }
static int AP_ReqGemColour(const ctr_req *r) { return r->colour; }
static int AP_ReqTokenColour(const ctr_req *r) { return r->colour; }
""" + layer_selector + req_to_unlock + mask_to_req + r"""

/* What the production block hands to the gate builder. */
static void select_requirement(int levelID, int *modelOut, int *neededOut, int *openOut)
{
	int unlockItem_modelID = 0, unlockItem_numOwned = 0, unlockItem_numNeeded = -1;
	int reqRelicTint = -1, reqGemColour = -1, reqTokenColour = -2;
""" + plan_block + r"""
ContentPad_BuildGate:
	(void)reqRelicTint; (void)reqGemColour; (void)reqTokenColour;
	*modelOut = unlockItem_modelID;
	*neededOut = unlockItem_numNeeded;
	*openOut = 0;
""" + gate_condition + r"""
		*openOut = 1;
}

enum { WPIS_CLOSED_ITEM = 0, WPIS_CLOSED_X, WPIS_CLOSED_1S, WPIS_CLOSED_10S, WPIS_NUM_INSTANCES = 10 };
struct ModelHeader { int tag; };
struct Model { short id; short numHeaders; struct ModelHeader *headers; };
struct Instance { struct Model *model; int shownHeader; };
struct WarpPad { struct Instance *inst[WPIS_NUM_INSTANCES]; short digit10s; short digit1s; };
static struct ModelHeader g_hdr[8];
static struct Model g_models[3];
static int g_overrun;

static void AH_WarpPad_SetNumModelData(struct Instance *inst, struct ModelHeader *mh)
{
	long idx = mh - inst->model->headers;
	if (idx < 0 || idx >= inst->model->numHeaders)
	{
		g_overrun = 1; /* the garbage ptrCurrFrame the alpha4 hub crashed on */
		inst->shownHeader = -999;
		return;
	}
	inst->shownHeader = (int)idx;
}

static struct Model *model_of(int id)
{
	if (id == 0x38) return &g_models[0];
	if (id == 0x6d) return &g_models[1];
	if (id == 0x6e) return &g_models[2];
	return 0;
}

static void digit_pass(struct WarpPad *wp)
{
	// This fixture only feeds live digits. The digits production harness
	// exercises the engine's pool membership check itself.
#define AH_WarpPad_LiveInstances(thread, live, hideRejected) memcpy(live, wp->inst, sizeof(live))
	for (; wp; wp = NULL)
	{
		struct Instance *inst;
		struct ModelHeader *mh = 0;
""" + digit_pass + r"""
	}
#undef AH_WarpPad_LiveInstances
}

static int drawn(const struct Instance *inst)
{
	if (inst->model == &g_models[1]) return 0;
	if (inst->model == &g_models[2]) return 9;
	if (inst->shownHeader < 0) return -1;
	return inst->shownHeader + 1;
}

/* Returns 1 when the per-frame pass selects a header outside the model. */
static int show(int unlockItem_numNeeded, int *shownOut)
{
	struct WarpPad wpS, *wp = &wpS;
	struct Instance ones, tens;
	memset(wp, 0, sizeof(*wp)); memset(&ones, 0, sizeof(ones)); memset(&tens, 0, sizeof(tens));
	g_overrun = 0;
	""" + split + r"""
	ones.model = model_of(""" + ones_model + r""");
	wp->inst[WPIS_CLOSED_1S] = &ones;
	if (wp->digit10s != 0)
	{
		tens.model = model_of(""" + tens_model + r""");
		wp->inst[WPIS_CLOSED_10S] = &tens;
	}
	if (!ones.model || (wp->digit10s && !tens.model)) { *shownOut = -2; return 0; }
	digit_pass(wp);
	digit_pass(wp);
	*shownOut = drawn(&ones) + (wp->digit10s ? 10 * drawn(&tens) : 0);
	return g_overrun;
}

static const char *bitname[15] = { "Trophy", "Sapphire Relic", "Gold Relic", "Platinum Relic",
	"Red CTR Token", "Green CTR Token", "Blue CTR Token", "Yellow CTR Token", "Purple CTR Token",
	"Red Gem", "Green Gem", "Blue Gem", "Yellow Gem", "Purple Gem", "Key" };
static int expected_model(unsigned mask)
{
	if (mask & 1u) return STATIC_TROPHY;
	if (mask & 0xEu) return STATIC_RELIC;
	if (mask & 0x1F0u) return STATIC_TOKEN;
	if (mask & 0x3E00u) return STATIC_GEM;
	return STATIC_KEY;
}

int main(void)
{
	static const struct { const char *name; unsigned mask; } fam[] = {
		{ "AnyRelic", 0xEu }, { "AnyCtrToken", 0x1F0u }, { "AnyGem", 0x3E00u } };
	struct { const char *name; unsigned mask; } types[18];
	int ntypes = 0, i, layer, n, fail = 0, cases = 0, gate_cases = 0;
	g_models[0].id = 0x38; g_models[0].numHeaders = 8; g_models[0].headers = g_hdr;
	g_models[1].id = 0x6d; g_models[1].numHeaders = 1; g_models[1].headers = g_hdr;
	g_models[2].id = 0x6e; g_models[2].numHeaders = 1; g_models[2].headers = g_hdr;
	for (i = 0; i < 15; i++) { types[ntypes].name = bitname[i]; types[ntypes].mask = 1u << i; ntypes++; }
	for (i = 0; i < 3; i++) { types[ntypes].name = fam[i].name; types[ntypes].mask = fam[i].mask; ntypes++; }

	for (layer = 0; layer < 3; layer++)
	{
		static const char *lname[3] = { "hub Key floor", "stage 1 rule", "stage 2" };
		int t;
		for (t = 0; t < (layer == 0 ? 1 : ntypes); t++)
		{
			int first_overrun = -1, overruns = 0;
			for (n = 1; n <= 300; n++)
			{
				int model, needed, shown, over, want, opened;
				memset(&g_pad, 0, sizeof(g_pad));
				g_owned = 0;
				if (layer == 0) { g_pad.keys = n; g_keys = 0; g_state = 1; }
				else if (layer == 1) { g_pad.keys = 0; g_keys = 0; g_state = 1; g_pad.has_rule = 1;
				                       g_pad.rule_mask = types[t].mask; g_pad.rule_count = n; }
				else { g_keys = 0; g_state = 3; g_pad.has_rule = 1; g_pad.rule_mask = 1u; g_pad.rule_count = 1;
				       g_pad.has_stage2 = 1; g_pad.stage2_mask = types[t].mask; g_pad.stage2_count = n; }
				select_requirement(3, &model, &needed, &opened);
				cases++;
				if (opened) { printf("FAIL unmet pad opens at %d\n", n); fail = 1; }
				if (needed != n)
				{ printf("FAIL %s %s count %d: block asks %d\n", lname[layer], layer ? types[t].name : "Key", n, needed); fail = 1; continue; }
				if (model != (layer ? expected_model(types[t].mask) : STATIC_KEY))
				{ printf("FAIL %s %s: icon model 0x%x\n", lname[layer], types[t].name, model); fail = 1; }
				over = show(needed, &shown);
				want = n > 99 ? 99 : n;
				if (over) { overruns++; if (first_overrun < 0) first_overrun = n; }
				if (over || shown != want)
				{ printf("FAIL %s %s count %d shows %d (overrun %d)\n", lname[layer], layer ? types[t].name : "Key", n, shown, over); fail = 1; }
				/* Display saturation must not lower the production gate threshold. */
				if (n > 99)
				{
					int owned;
					for (owned = n - 1; owned <= n; owned++)
					{
						g_owned = owned;
						if (layer == 0) g_keys = owned;
						select_requirement(3, &model, &needed, &opened);
						gate_cases++;
						if (needed != n || opened != (owned >= n))
						{ printf("FAIL %s %s gate needs %d, owned %d: count %d open %d\n", lname[layer], layer ? types[t].name : "Key", n, owned, needed, opened); fail = 1; }
					}
				}
			}
			printf("%-14s %-17s counts 1..300: %s", lname[layer], layer ? types[t].name : "Key",
			       overruns ? "per-frame pass reads past the 8 digit headers" : "number drawn, never past the digit headers");
			if (overruns) printf(" from %d (%d counts)", first_overrun, overruns);
			printf("\n");
		}
	}
	if (cases != 11100) { printf("FAIL case count %d, want 11100\n", cases); fail = 1; }
	if (gate_cases != 14874) { printf("FAIL gate case count %d, want 14874\n", gate_cases); fail = 1; }
	printf("warppad requirement types: %d type x layer x count cases, %s\n", cases, fail ? "FAIL" : "ok");
	printf("production gate: %d below/at-threshold checks above 99, %s\n", gate_cases, fail ? "FAIL" : "ok");
	return fail;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "types.c"
    exe = Path(tmp) / "types"
    c.write_text(program, encoding="utf-8")
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-label", "-Wno-unused-function",
                    "-I", str(ap_dir), "-o", str(exe), str(c)], check=True)
    sys.exit(subprocess.run([str(exe)]).returncode)
