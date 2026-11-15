"""A5-1: a closed warp pad's floating number never reads past the digit model.

A player seed asked for 67 relics on a pad. The retail split in
AH_WarpPad_LInB made that "1" and "57", and AH_WarpPad_AllWarppadNum, every
frame, copied header 56 of the eight-header "1" to "8" digit model into the
instance. The garbage frame pointer crashed the hub render in
RenderBucket_PrepareDrawContext.

This harness compiles the production digit pass of AH_WarpPad_AllWarppadNum
(its CTR_AP branch) with ap/ap_warppad_digits_logic.h and a fake digit model
that has exactly eight headers, then for every count from -5 to 300 checks
that:

1. the birth picks, for both digits, the model the digit needs, and the two
   digits read the count (capped at 99);
2. the per-frame pass only ever selects one of the eight headers, the header
   for that digit, and leaves the "0" and "9" models alone;
3. counts 0 to 19 split exactly as retail did.

It also pins the engine text that uses the helper. It cannot run the engine:
the drawn digits need a played hub. Run from the repository root.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
warp = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at) + 3]


allnum = body(warp, "void AH_WarpPad_AllWarppadNum()")
start = allnum.index("#ifdef CTR_AP\n")
digit_pass = allnum[start + len("#ifdef CTR_AP\n"):allnum.index("#else", start)]

# Engine text: the birth splits with the helper and births both digits with
# the helper's model; the per-frame pass is the block compiled below.
assert "ap_warppad_split_digits(unlockItem_numNeeded, &warppadObj->digit10s, &warppadObj->digit1s);" in warp
assert 'INSTANCE_Birth3D(gGT->modelPtr[ap_warppad_digit_model(warppadObj->digit10s)], "warpnum", t);' in warp
assert "i = ap_warppad_digit_model(warppadObj->digit1s);" in warp
assert '#include "../../ap/ap_warppad_digits_logic.h"' in warp
assert "MainFrame_RenderFrame" not in digit_pass and "ap_warppad_digit_header(" in digit_pass

program = r"""
#include <stdio.h>
#include <string.h>
#include "ap_warppad_digits_logic.h"

enum { WPIS_CLOSED_ITEM = 0, WPIS_CLOSED_X, WPIS_CLOSED_1S, WPIS_CLOSED_10S, WPIS_COUNT = 8 };
struct ModelHeader { int tag; };
struct Model { short id; short numHeaders; struct ModelHeader *headers; };
struct Instance { struct Model *model; int shownHeader; };
struct WarpPad { struct Instance *inst[WPIS_COUNT]; short digit10s; short digit1s; };

static struct ModelHeader g_big1Headers[8];
static struct Model g_models[3];

static void AH_WarpPad_SetNumModelData(struct Instance *inst, struct ModelHeader *mh)
{
	long idx = mh - inst->model->headers;
	if (idx < 0 || idx >= inst->model->numHeaders)
	{
		printf("FAIL header %ld outside 0..%d\n", idx, inst->model->numHeaders - 1);
		inst->shownHeader = -999;
		return;
	}
	inst->shownHeader = (int)idx;
}

static struct Model *model_of(int id)
{
	if (id == AP_WARPPAD_DIGIT_MODEL_1TO8) return &g_models[0];
	if (id == AP_WARPPAD_DIGIT_MODEL_0) return &g_models[1];
	if (id == AP_WARPPAD_DIGIT_MODEL_9) return &g_models[2];
	return 0;
}

static void digit_pass(struct WarpPad *wp)
{
	struct Instance *inst;
	struct ModelHeader *mh = 0;
""" + digit_pass + r"""
}

/* The digit an instance draws: its model, or its STATIC_BIG1 header. */
static int drawn(const struct Instance *inst)
{
	if (inst->model == &g_models[1]) return 0;
	if (inst->model == &g_models[2]) return 9;
	if (inst->shownHeader < 0) return -1;
	return inst->shownHeader + 1;
}

int main(void)
{
	int fail = 0, n;
	g_models[0].id = AP_WARPPAD_DIGIT_MODEL_1TO8; g_models[0].numHeaders = 8; g_models[0].headers = g_big1Headers;
	g_models[1].id = AP_WARPPAD_DIGIT_MODEL_0; g_models[1].numHeaders = 1; g_models[1].headers = g_big1Headers;
	g_models[2].id = AP_WARPPAD_DIGIT_MODEL_9; g_models[2].numHeaders = 1; g_models[2].headers = g_big1Headers;

	for (n = -5; n <= 300; n++)
	{
		struct WarpPad wp;
		struct Instance ones, tens;
		int want = n < 0 ? 0 : (n > 99 ? 99 : n), shown;
		memset(&wp, 0, sizeof(wp));
		memset(&ones, 0, sizeof(ones));
		memset(&tens, 0, sizeof(tens));
		ap_warppad_split_digits(n, &wp.digit10s, &wp.digit1s);

		/* Birth: ones always, tens only when nonzero; each starts on header 0. */
		ones.model = model_of(ap_warppad_digit_model(wp.digit1s));
		ones.shownHeader = 0;
		wp.inst[WPIS_CLOSED_1S] = &ones;
		if (wp.digit10s != 0)
		{
			tens.model = model_of(ap_warppad_digit_model(wp.digit10s));
			tens.shownHeader = 0;
			wp.inst[WPIS_CLOSED_10S] = &tens;
		}

		digit_pass(&wp);
		digit_pass(&wp); /* every frame: stable */

		shown = drawn(&ones);
		if (wp.digit10s != 0)
			shown += 10 * drawn(&tens);
		if (shown != want || ones.shownHeader == -999 || tens.shownHeader == -999)
		{
			printf("FAIL count %d shows %d, want %d\n", n, shown, want);
			fail = 1;
		}

		/* Retail split for the counts retail could ask for. */
		if (n >= 0 && n < 20)
		{
			int rt = n < 10 ? 0 : 1, ro = n < 10 ? n : n - 10;
			if (wp.digit10s != rt || wp.digit1s != ro)
			{
				printf("FAIL count %d split %d/%d, retail %d/%d\n", n, wp.digit10s, wp.digit1s, rt, ro);
				fail = 1;
			}
		}
	}

	/* A model with fewer headers than digits is never read past its end. */
	{
		struct ModelHeader few[3];
		struct Model small = { AP_WARPPAD_DIGIT_MODEL_1TO8, 3, few };
		struct WarpPad wp;
		struct Instance ones;
		memset(&wp, 0, sizeof(wp));
		memset(&ones, 0, sizeof(ones));
		ones.model = &small;
		ones.shownHeader = -7; /* untouched marker: the pass must skip it */
		wp.inst[WPIS_CLOSED_1S] = &ones;
		wp.digit1s = 7;
		digit_pass(&wp);
		if (ones.shownHeader != -7)
		{
			printf("FAIL short model: header %d selected\n", ones.shownHeader);
			fail = 1;
		}
		/* and a digit the short model has is still selected */
		wp.digit1s = 2;
		digit_pass(&wp);
		if (ones.shownHeader != 1)
		{
			printf("FAIL short model: digit 2 gave header %d\n", ones.shownHeader);
			fail = 1;
		}
	}

	if (!fail)
		printf("warppad digits: counts -5..300 draw within the digit headers, capped at 99\n");
	return fail;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "digits.c"
    exe = Path(tmp) / "digits"
    src.write_text(program, encoding="utf-8")
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-I", str(root / "ap"), "-o", str(exe), str(src)], check=True)
    subprocess.run([str(exe)], check=True)
