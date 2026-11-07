"""A3-12: a pad rebuilt in the hub equals a pad born fresh for the same state.

AH_WarpPad_ThTick rebuilds a pad through AH_WarpPad_BuildInstances when the
pad's birth key (ap/ap_pad_birth.h) differs from the key it was built for. This
compiles the production requirement choice of the birth (the content plan block
of AH_WarpPad_BuildInstances with AP_ReqToUnlock and AP_ContentMaskToReq) next
to the production key, and checks on random plan pads that:

1. two states with one key draw the same pad (state, look, requirement icon,
   count, tints, open or closed), so an unchanged key never hides a change;
2. a pad kept up to date by the rebuild rule, through random received items,
   checks and state changes, always equals the pad a fresh birth draws;
3. the tester case: a cup pad behind the Cups Room Key floor with its own
   stage 1 rule. The second Key keeps the pad Locked (state 1) but swaps its
   "Key x2" icon for the rule; the old state-only key missed it.

It also pins the engine text that calls the key. It cannot run the engine: the
rebuilt instances on screen need a played hub. Run from the repository root.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
warp = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")
gates = (root / "ap/ap_hooks_gates.c").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at) + 3]


build = body(warp, "static void AH_WarpPad_BuildInstances(struct Thread *t)\n{")
tick = body(warp, "void AH_WarpPad_ThTick(struct Thread *t)")
rebirth = body(warp, "static int AH_WarpPad_RebirthKey(int physLevelID, int destLevelID)")
phys = body(warp, "static int AH_WarpPad_PhysOf(struct Thread *t)")

# Engine text: the birth records the key, ThTick compares it under the same
# physical index, and the birth's requirement choice is the key's.
assert "s_padRebirthState[levelID] = ctr_cfg_active() ? AH_WarpPad_RebirthKey(levelID, warppadObj->levelID) : 0;" in build
assert "int rbPhys = AH_WarpPad_PhysOf(t);" in tick and "ctr_cfg_warp_phys(warppadObj->levelID);\n\t\tif (rbPhys" not in tick
assert "int rbState = AH_WarpPad_RebirthKey(rbPhys, warppadObj->levelID);" in tick
assert "if (rbState != s_padRebirthState[rbPhys])" in tick
assert "AH_WarpPad_ThDestroy(t);" in tick and "AH_WarpPad_BuildInstances(t);" in tick
assert "for (i = 8; inst->name[i] != 0; i++)" in build and "for (i = 8; t->inst->name[i] != 0; i++)" in phys
assert "counts[i] = AP_GateCount(i);" in rebirth and "ap_pad_birth_key(ap_content_plan_pad(physLevelID), counts, state," in rebirth
assert "AP_PadLook(physLevelID, destLevelID)" in rebirth and "AP_PadReplayable(physLevelID, destLevelID));" in rebirth
assert warp.count("ap_pad_gate_layer(") == 1 and warp.count("AH_WarpPad_RebirthKey(") == 3

head = "if (ap_content_plan_active())\n\t{\n\t\tconst ctr_content_pad *pad = ap_content_plan_pad(levelID);"
plan = build[build.index(head):]
plan = plan[:plan.index("goto ContentPad_BuildGate;\n\t}") + len("goto ContentPad_BuildGate;\n\t}")]
assert "ap_pad_gate_layer(pad, unlockItem_numOwned, AP_PadState(levelID, warppadObj->levelID))" in plan
gate = build[build.index("ContentPad_BuildGate:"):]
assert gate.index("if (unlockItem_numOwned >= unlockItem_numNeeded") < 120
assert "|| AP_PadReplayable(levelID, warppadObj->levelID)" in gate[:200]


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


program = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ap_content_plan.h"
#include "ap_pad_birth.h"
#include "ap_pad_colour.h"
typedef struct { int type, count, colour; } ctr_req;
enum { STATIC_TROPHY = 1, STATIC_KEY, STATIC_TOKEN, STATIC_RELIC, STATIC_GEM };
enum { AP_IDX_TROPHY = 0, AP_IDX_SAPPHIRE = 1, AP_IDX_TOKEN_RED = 4, AP_IDX_GEM_RED = 9, AP_IDX_KEY = 14 };
static ctr_content_pad thePad;
static int counts[15], theState, theReplay, theRacerMissing;
static int sum(int a, int n) { int s = 0; for (int i = 0; i < n; i++) s += counts[a + i]; return s; }
int AP_GateCount(int i) { return counts[i]; }
int AP_GateCountTokenColour(int c) { return counts[AP_IDX_TOKEN_RED + c]; }
int AP_GateCountRelicTier(int c) { return counts[AP_IDX_SAPPHIRE + (c > 0 ? c : 0)]; }
int AP_GateCountGemColour(int c) { return counts[AP_IDX_GEM_RED + c]; }
int AP_GateCountTokenSum(void) { return sum(AP_IDX_TOKEN_RED, 5); }
int AP_GateCountRelicSum(void) { return sum(AP_IDX_SAPPHIRE, 3); }
int AP_GateCountGemSum(void) { return sum(AP_IDX_GEM_RED, 5); }
int ap_content_plan_active(void) { return 1; }
const ctr_content_pad *ap_content_plan_pad(int physical) { (void)physical; return &thePad; }
int AP_PadState(int a, int b) { (void)a; (void)b; return theState; }
int AP_PadReplayable(int a, int b) { (void)a; (void)b; return theReplay; }
int AP_PadLook(int a, int b) { (void)a; (void)b; return AP_PadLookFromState(theState, theRacerMissing); }
int AP_ContentPadRuleOwned(const ctr_content_pad *p) { return p && p->has_rule ? ap_content_rule_owned(p, counts) : 0; }
int AP_ContentStage2Owned(const ctr_content_pad *p) { return p && p->has_stage2 ? ap_content_stage2_owned(p, counts) : 0; }
""" + function(gates, "int AP_ReqRelicTintTier(const ctr_req *r)") \
    + function(gates, "int AP_ReqGemColour(const ctr_req *r)") \
    + function(gates, "int AP_ReqTokenColour(const ctr_req *r)") \
    + function(warp, "static void AP_ReqToUnlock(const ctr_req *r, int *modelID, int *numOwned, int *numNeeded)") \
    + function(warp, "static void AP_ContentMaskToReq(unsigned mask, int count, ctr_req *r)") + r"""
/* What the birth draws: the Done look, or the open beam in the pad's look, or
   the closed requirement icon with its count and tints. */
typedef struct { int state, look, done_beam, open, model, needed, relic, gem, token; } Drawn;
struct WarpPadStub { int levelID; };

static Drawn birth(void)
{
	Drawn d;
	struct WarpPadStub wp = {7}, *warppadObj = &wp;
	int levelID = 7;
	int unlockItem_modelID = 0, unlockItem_numOwned = 0, unlockItem_numNeeded = -1;
	int reqRelicTint = -1, reqGemColour = -1, reqTokenColour = -2;
	memset(&d, 0, sizeof d);
	d.state = theState;
	d.look = AP_PadLook(levelID, warppadObj->levelID);
	if (theState == 5) { d.done_beam = theReplay; return d; }
	""" + plan.replace("goto ContentPad_BuildGate;", "goto Gate;") + r"""
Gate:
	d.open = unlockItem_numOwned >= unlockItem_numNeeded || AP_PadReplayable(levelID, warppadObj->levelID);
	if (!d.open)
	{
		d.model = unlockItem_modelID; d.needed = unlockItem_numNeeded;
		d.relic = reqRelicTint; d.gem = reqGemColour; d.token = reqTokenColour;
	}
	return d;
}

static int key(void)
{
	int c[15];
	for (int i = 0; i < 15; i++) c[i] = AP_GateCount(i);
	return ap_pad_birth_key(ap_content_plan_pad(7), c, theState, AP_PadLook(7, 7), theReplay);
}

static unsigned rng = 12345;
static int roll(int n) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % (unsigned)n); }
static unsigned mask(void) { unsigned m = 0; int b = 1 + roll(3); while (b--) m |= 1u << roll(14); return m; }
static void randomPad(void)
{
	memset(&thePad, 0, sizeof thePad);
	thePad.physical = 7; thePad.occupied = 1; thePad.racer_lock = -1;
	thePad.keys = roll(5);
	thePad.has_rule = roll(3) != 0;
	if (thePad.has_rule) { thePad.rule_mask = mask(); thePad.rule_count = 1 + roll(6); thePad.rule_distinct = roll(4) == 0; }
	thePad.has_stage2 = roll(2);
	if (thePad.has_stage2) { thePad.stage2_mask = mask(); thePad.stage2_count = 1 + roll(4); }
}
static void randomWorld(void)
{
	for (int i = 0; i < 15; i++) counts[i] = roll(i == AP_IDX_KEY ? 5 : 7);
	theState = 1 + roll(5); theReplay = roll(3) == 0; theRacerMissing = roll(4) == 0;
}

int main(void)
{
	int pads, i, j, steps;
	/* 1. One key, one drawing. */
	for (pads = 0; pads < 3000; pads++)
	{
		enum { N = 64 };
		int keys[N]; Drawn drawn[N];
		randomPad();
		for (i = 0; i < N; i++) { randomWorld(); keys[i] = key(); drawn[i] = birth(); }
		for (i = 0; i < N; i++)
			for (j = 0; j < i; j++)
				if (keys[i] == keys[j] && memcmp(&drawn[i], &drawn[j], sizeof(Drawn)))
				{
					fprintf(stderr, "key %x draws two pads (state %d/%d model %d/%d needed %d/%d open %d/%d)\n", keys[i],
					        drawn[i].state, drawn[j].state, drawn[i].model, drawn[j].model, drawn[i].needed,
					        drawn[j].needed, drawn[i].open, drawn[j].open);
					return 1;
				}
	}
	/* 2. Rebuild by the key through random events equals a fresh birth. */
	for (pads = 0; pads < 3000; pads++)
	{
		Drawn shown; int built;
		randomPad(); randomWorld();
		shown = birth(); built = key();
		for (steps = 0; steps < 40; steps++)
		{
			int what = roll(4);
			if (what == 0) counts[roll(15)]++;
			else if (what == 1) theState = 1 + roll(5);
			else if (what == 2) theReplay = !theReplay;
			else theRacerMissing = !theRacerMissing;
			if (key() != built) { shown = birth(); built = key(); }
			Drawn fresh = birth();
			assert(!memcmp(&shown, &fresh, sizeof(Drawn)));
		}
	}
	/* 3. The tester case: Cups Room floor of 2 Keys, a rule of 2 Sapphire Relics. */
	{
		Drawn before, after;
		int k1, k2;
		memset(&thePad, 0, sizeof thePad); memset(counts, 0, sizeof counts);
		thePad.physical = 100; thePad.occupied = 1; thePad.racer_lock = -1; thePad.keys = 2;
		thePad.has_rule = 1; thePad.rule_mask = 1u << AP_IDX_SAPPHIRE; thePad.rule_count = 2;
		theState = 1; theReplay = 0; theRacerMissing = 0;
		counts[AP_IDX_KEY] = 1; counts[AP_IDX_SAPPHIRE] = 1;
		before = birth(); k1 = key();
		assert(!before.open && before.model == STATIC_KEY && before.needed == 2);
		counts[AP_IDX_KEY] = 2; /* the Wumpa Shop Key arrives; the pad stays Locked */
		after = birth(); k2 = key();
		assert(theState == 1 && !after.open && after.model == STATIC_RELIC && after.needed == 2 && after.relic == 0);
		assert(k1 != k2);
		puts("PASS: the second Key swaps Key x2 for the pad's rule; the state-only key stayed 1 and missed it");
	}
	puts("PASS: one birth key, one drawn pad; a rebuilt pad equals a fresh birth over 120000 random events");
	return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "rebuild.c", Path(tmp) / "rebuild"
    src.write_text(program)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Wno-unused-function", "-fsanitize=undefined,address",
                    "-I", str(root / "ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: pad rebuild production text")
