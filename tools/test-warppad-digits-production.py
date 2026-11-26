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
4. refresh and teardown use the live instance list when cached children
   have been freed, reused by another pad or replaced by an invalid pointer.

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
assert warp.count("AH_WarpPad_RecordChildSlots(t->object);") == 2

program = r"""
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "ap_warppad_digits_logic.h"
#define CTR_AP 1
#define HIDE_MODEL 0x80

enum { WPIS_CLOSED_ITEM, WPIS_CLOSED_X, WPIS_CLOSED_1S, WPIS_CLOSED_10S,
       WPIS_OPEN_BEAM, WPIS_OPEN_RING1, WPIS_OPEN_RING2,
       WPIS_OPEN_PRIZE1, WPIS_OPEN_PRIZE2, WPIS_OPEN_PRIZE3, WPIS_NUM_INSTANCES };
struct ModelHeader { int tag; };
struct Model { short id; short numHeaders; struct ModelHeader *headers; };
struct Instance {
	struct Model *model;
	int shownHeader;
	struct Instance *next;
	struct Thread *thread;
	const char *name;
	unsigned flags;
};
struct WarpPad {
	struct Instance *inst[WPIS_NUM_INSTANCES];
	unsigned short childSlots;
	short digit10s;
	short digit1s;
};
struct Thread { struct WarpPad *object; struct Instance *inst; };
struct JitPool {
	struct { struct Instance *first; } taken;
	struct { int count; } free;
};
static struct GameTracker { struct { struct JitPool instance; } JitPools; } tracker;
static struct { struct GameTracker *gGT; } data = { &tracker }, *sdata = &data;
static struct Instance **taken = &tracker.JitPools.instance.taken.first;
static int rebuilds;

static void INSTANCE_Death(struct Instance *inst)
{
	struct Instance **link = taken;
	while (*link != inst)
	{
		assert(*link != NULL);
		link = &(*link)->next;
	}
	*link = inst->next;
	inst->next = NULL;
	tracker.JitPools.instance.free.count++;
}

static void AH_WarpPad_BuildInstances(struct Thread *t)
{
	assert(tracker.JitPools.instance.free.count >= WPIS_NUM_INSTANCES - WPIS_OPEN_BEAM);
	assert(t->object->childSlots == 0);
	rebuilds++;
}

""" + (
    body(warp, "static void AH_WarpPad_RecordChildSlots(")
    + body(warp, "static int AH_WarpPad_LiveInstances(")
    + body(warp, "void AH_WarpPad_ThDestroy(struct Thread *t)\n")
    + body(warp, "static int AH_WarpPad_TryRebuild(")
) + r"""

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

static void digit_pass(struct Thread *t)
{
	for (; t; t = NULL)
	{
		struct WarpPad *wp = t->object;
		struct Instance *inst;
		struct ModelHeader *mh = 0;
""" + digit_pass + r"""
	}
}

static void verify_pad_lifetime(void)
{
	struct WarpPad changed = {0}, other = {0};
	struct Thread pad = {&changed, NULL}, neighbour = {&other, NULL};
	struct Instance mesh = {.thread = &pad, .name = "warppad#3"};
	struct Instance filter = {.thread = &pad, .name = "beamFilter", .next = &mesh};
	struct Instance ring = {.thread = &pad, .name = "bottomRing", .next = &filter};
	struct Instance reused = {.thread = &neighbour, .name = "warpnum", .next = &ring};
	struct Instance freed = {.thread = &pad, .name = "warpnum"};
	struct Instance cross = {.thread = &pad, .name = "x", .next = &mesh};
	struct Instance requirement = {.thread = &pad, .name = "reqObj", .next = &cross};
	struct Instance prize = {.thread = &pad, .name = "prize1", .next = &mesh};
	struct Instance *live[WPIS_NUM_INSTANCES];

	pad.inst = &mesh;
	*taken = &reused;
	changed.inst[WPIS_CLOSED_ITEM] = &freed;
	changed.inst[WPIS_CLOSED_X] = &reused;
	changed.inst[WPIS_CLOSED_1S] = (struct Instance *)(uintptr_t)0xffff;
	changed.inst[WPIS_CLOSED_10S] = &mesh;
	changed.inst[WPIS_OPEN_RING1] = &ring;
	changed.inst[WPIS_OPEN_RING2] = &ring;
	changed.inst[WPIS_OPEN_PRIZE1] = &filter;
	assert(!AH_WarpPad_LiveInstances(&pad, live, 1));
	assert((ring.flags & HIDE_MODEL) && !(reused.flags & HIDE_MODEL));
	for (int i = 0; i < WPIS_NUM_INSTANCES; i++)
		assert(live[i] == NULL);
	digit_pass(&pad);
	tracker.JitPools.instance.free.count = 3;
	assert(!AH_WarpPad_TryRebuild(&pad));
	assert(rebuilds == 0 && reused.next == &ring);
	tracker.JitPools.instance.free.count++;
	assert(AH_WarpPad_TryRebuild(&pad) && rebuilds == 1);
	assert(*taken == &reused && reused.next == &mesh && mesh.next == NULL);
	assert(AH_WarpPad_LiveInstances(&pad, live, 0));
	for (int i = 0; i < WPIS_NUM_INSTANCES; i++)
		assert(live[i] == NULL);

	*taken = &requirement;
	changed.inst[WPIS_CLOSED_ITEM] = &requirement;
	changed.inst[WPIS_CLOSED_X] = &cross;
	changed.inst[WPIS_CLOSED_1S] = &freed;
	AH_WarpPad_RecordChildSlots(&changed);
	*taken = &cross;
	changed.inst[WPIS_CLOSED_ITEM] = NULL;
	changed.inst[WPIS_CLOSED_1S] = NULL;
	assert(!AH_WarpPad_LiveInstances(&pad, live, 0));
	assert(live[WPIS_CLOSED_X] == &cross);

	*taken = &prize;
	changed.inst[WPIS_CLOSED_X] = NULL;
	changed.inst[WPIS_OPEN_PRIZE1] = &prize;
	AH_WarpPad_RecordChildSlots(&changed);
	assert(AH_WarpPad_LiveInstances(&pad, live, 0));
	*taken = &mesh;
	changed.inst[WPIS_OPEN_PRIZE1] = NULL;
	assert(!AH_WarpPad_LiveInstances(&pad, live, 0));

	*taken = &filter;
	AH_WarpPad_RecordChildSlots(&changed);
	assert(!AH_WarpPad_LiveInstances(&pad, live, 0));
	*taken = NULL;
	puts("warppad lifetime: stale slots, aliases and incomplete child sets rejected; rebuild checks pool capacity");
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
	verify_pad_lifetime();
	g_models[0].id = AP_WARPPAD_DIGIT_MODEL_1TO8; g_models[0].numHeaders = 8; g_models[0].headers = g_big1Headers;
	g_models[1].id = AP_WARPPAD_DIGIT_MODEL_0; g_models[1].numHeaders = 1; g_models[1].headers = g_big1Headers;
	g_models[2].id = AP_WARPPAD_DIGIT_MODEL_9; g_models[2].numHeaders = 1; g_models[2].headers = g_big1Headers;

	for (n = -5; n <= 300; n++)
	{
		struct WarpPad wp;
		struct Instance ones, tens;
		struct Thread pad = {&wp, NULL};
		int want = n < 0 ? 0 : (n > 99 ? 99 : n), shown;
		memset(&wp, 0, sizeof(wp));
		memset(&ones, 0, sizeof(ones));
		memset(&tens, 0, sizeof(tens));
		ap_warppad_split_digits(n, &wp.digit10s, &wp.digit1s);

		/* Birth: ones always, tens only when nonzero; each starts on header 0. */
		ones.model = model_of(ap_warppad_digit_model(wp.digit1s));
		ones.shownHeader = 0;
		ones.thread = &pad;
		ones.name = "warpnum";
		*taken = &ones;
		wp.inst[WPIS_CLOSED_1S] = &ones;
		if (wp.digit10s != 0)
		{
			tens.model = model_of(ap_warppad_digit_model(wp.digit10s));
			tens.shownHeader = 0;
			tens.thread = &pad;
			tens.name = "warpnum";
			ones.next = &tens;
			wp.inst[WPIS_CLOSED_10S] = &tens;
		}

		AH_WarpPad_RecordChildSlots(&wp);
		digit_pass(&pad);
		digit_pass(&pad); /* every frame: stable */

		shown = drawn(&ones);
		if (wp.digit10s != 0)
			shown += 10 * drawn(&tens);
		if (shown != want || ones.shownHeader == -999 || tens.shownHeader == -999)
		{
			printf("FAIL count %d shows %d, want %d\n", n, shown, want);
			fail = 1;
		}
		if (n == 10)
		{
			struct Instance *live[WPIS_NUM_INSTANCES];
			wp.inst[WPIS_CLOSED_1S] = &tens;
			wp.inst[WPIS_CLOSED_10S] = &ones;
			assert(!AH_WarpPad_LiveInstances(&pad, live, 0));
			assert(live[WPIS_CLOSED_1S] == NULL && live[WPIS_CLOSED_10S] == NULL);
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
		struct Thread pad = {&wp, NULL};
		memset(&wp, 0, sizeof(wp));
		memset(&ones, 0, sizeof(ones));
		ones.model = &small;
		ones.thread = &pad;
		ones.name = "warpnum";
		*taken = &ones;
		ones.shownHeader = -7; /* untouched marker: the pass must skip it */
		wp.inst[WPIS_CLOSED_1S] = &ones;
		AH_WarpPad_RecordChildSlots(&wp);
		wp.digit1s = 7;
		digit_pass(&pad);
		if (ones.shownHeader != -7)
		{
			printf("FAIL short model: header %d selected\n", ones.shownHeader);
			fail = 1;
		}
		/* and a digit the short model has is still selected */
		wp.digit1s = 2;
		digit_pass(&pad);
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
