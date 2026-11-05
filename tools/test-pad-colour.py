#!/usr/bin/env python3
"""One pad state, one colour, on all three surfaces (alpha2 pad colours).

The warp pad beam (game/232/AH_WarpPad.c), the hub map dot (game/232/AH_Map.c)
and the Adventure tracker node (ap/ap_tracker.c) read one table,
ap/ap_pad_colour.h. The Familiar palette was chosen on 2026-11-04.

1. Compile the freestanding header and pin the table: each pad state maps to
   one display state, each display state to its Familiar RGB, and the beam tint,
   the map vertex colour and the tracker colour are all derived from that RGB.
2. Check the wiring, so a surface cannot go back to a private colour list.

Run from the repository root: python3 tools/test-pad-colour.py
"""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]

# ── 1. the table ────────────────────────────────────────────────────────────
program = r'''
#include <stdio.h>
#include "ap_pad_colour.h"
#include "ap_tracker_boss_node_logic.h"

static int checks, fails;
#define EXPECT(c, why) do { checks++; if (!(c)) { printf("FAIL %s\n", why); fails++; } } while (0)

/* The Familiar column of the proposal's table, in its order. */
static const struct { int look; unsigned rgb; const char *name; } familiar[] = {
	{AP_PAD_LOOK_CLOSED, 0xDA4230u, "closed"},
	{AP_PAD_LOOK_OPEN, 0x70E860u, "open"},
	{AP_PAD_LOOK_STAGE2_OPEN, 0x93ADFAu, "stage 2 open"},
	{AP_PAD_LOOK_STAGE2_WAITING, 0xF49228u, "stage 2 waiting"},
	{AP_PAD_LOOK_RACER_LOCKED, 0xBA4E22u, "racer-locked"},
	{AP_PAD_LOOK_DONE, 0x9AA4AEu, "done"},
	{AP_PAD_LOOK_BOSS_BEATEN, 0xF2C248u, "boss beaten"},
};
#define N (int)(sizeof familiar / sizeof familiar[0])

static unsigned beam_rgb(unsigned packed)
{
	return (((packed >> 20) & 0xff) << 16) | (((packed >> 12) & 0xff) << 8) | ((packed >> 4) & 0xff);
}

int main(void)
{
	int i, j, s;
	char why[160];

	/* pad state -> display state */
	EXPECT(AP_PadLookFromState(1, 0) == AP_PAD_LOOK_CLOSED, "state 1 is closed");
	EXPECT(AP_PadLookFromState(1, 1) == AP_PAD_LOOK_RACER_LOCKED, "state 1 without its racer is racer-locked");
	EXPECT(AP_PadLookFromState(2, 0) == AP_PAD_LOOK_OPEN, "state 2 is open");
	EXPECT(AP_PadLookFromState(3, 0) == AP_PAD_LOOK_STAGE2_WAITING, "state 3 is stage 2 waiting");
	EXPECT(AP_PadLookFromState(4, 0) == AP_PAD_LOOK_STAGE2_OPEN, "state 4 is stage 2 open");
	EXPECT(AP_PadLookFromState(5, 0) == AP_PAD_LOOK_DONE, "state 5 is done");
	EXPECT(AP_PadLookFromState(5, 1) == AP_PAD_LOOK_DONE, "done stays done with a racer missing");
	for (s = 2; s <= 5; s++)
		EXPECT(AP_PadLookFromState(s, 1) == AP_PadLookFromState(s, 0), "the racer only refines a closed pad");
	EXPECT(AP_PadLookFromState(0, 0) == AP_PAD_LOOK_NONE, "no AP state keeps the retail look");
	EXPECT(AP_PadLookFromState(AP_TRACKER_NODE_BEATEN, 0) == AP_PAD_LOOK_BOSS_BEATEN, "beaten garage node");

	for (i = 0; i < N; i++) {
		int look = familiar[i].look;
		unsigned rgb = AP_PadLookRGB(look), map, beam;
		snprintf(why, sizeof why, "%s is Familiar #%06X", familiar[i].name, familiar[i].rgb);
		EXPECT(rgb == familiar[i].rgb, why);

		/* tracker: exact RGB through the node mapping */
		snprintf(why, sizeof why, "tracker %s uses the table", familiar[i].name);
		EXPECT(AP_PadLookRGB(AP_TrackerNodeColour(look)) == rgb, why);

		/* map: half the target on every channel (0x80 leaves the white icon
		 * unchanged), 0x00BBGGRR; the dim flicker half keeps the hue */
		map = AP_PadLookMapVertex(look, 0);
		snprintf(why, sizeof why, "map %s is half its table colour", familiar[i].name);
		EXPECT((map & 0xff) == (AP_PadLookR(rgb) + 1) / 2 && ((map >> 8) & 0xff) == (AP_PadLookG(rgb) + 1) / 2 &&
		       ((map >> 16) & 0xff) == (AP_PadLookB(rgb) + 1) / 2 && (map >> 24) == 0, why);
		snprintf(why, sizeof why, "map %s dim half is darker, same hue", familiar[i].name);
		{
			unsigned d = AP_PadLookMapVertex(look, 1);
			EXPECT((d & 0xff) <= (map & 0xff) && ((d >> 8) & 0xff) <= ((map >> 8) & 0xff) &&
			       ((d >> 16) & 0xff) <= ((map >> 16) & 0xff) && d != map, why);
		}

		/* beam: only the states that can carry a beam get a colour, and it
		 * is the table colour at the beam level */
		beam = AP_PadLookBeamTint(look);
		if (look == AP_PAD_LOOK_OPEN || look == AP_PAD_LOOK_STAGE2_OPEN) {
			unsigned b = beam_rgb(beam);
			snprintf(why, sizeof why, "beam %s is its table colour at the beam level", familiar[i].name);
			EXPECT(beam != 0 && (beam & 0xf000000fu) == 0 &&
			       ((b >> 16) & 0xff) == AP_PadLookR(rgb) * AP_PAD_BEAM_LEVEL / 256 &&
			       ((b >> 8) & 0xff) == AP_PadLookG(rgb) * AP_PAD_BEAM_LEVEL / 256 &&
			       (b & 0xff) == AP_PadLookB(rgb) * AP_PAD_BEAM_LEVEL / 256, why);
		} else {
			snprintf(why, sizeof why, "%s has no beam colour (no beam, or not a pad)", familiar[i].name);
			EXPECT(beam == 0, why);
		}

		for (j = 0; j < i; j++) {
			snprintf(why, sizeof why, "%s and %s differ", familiar[i].name, familiar[j].name);
			EXPECT(AP_PadLookRGB(familiar[j].look) != rgb, why);
		}
	}
	EXPECT(AP_PadLookRGB(AP_PAD_LOOK_NONE) == 0 && AP_PadLookBeamTint(AP_PAD_LOOK_NONE) == 0,
	       "no state: no colour, retail beam");
	EXPECT(AP_TrackerNodeColour(AP_PAD_LOOK_NONE) == AP_PAD_LOOK_OPEN, "a node with no state draws open, as before");
	EXPECT(N + 1 == AP_PAD_LOOK_COUNT, "every display state is in the table");

	/* Gold only for a beaten garage. */
	for (s = 0; s < 7; s++)
		EXPECT(s == AP_TRACKER_NODE_BEATEN || AP_PadLookFromState(s, 0) != AP_PAD_LOOK_BOSS_BEATEN,
		       "no pad state is boss gold");

	printf("%d checks, %d failures\n", checks, fails);
	puts(fails ? "FAIL pad colour table" : "ok pad colour table");
	return fails != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'pad_colour.c').write_text(program)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-I', str(root / 'ap'), str(tmp / 'pad_colour.c'), '-o', str(tmp / 'pad_colour')], check=True)
    subprocess.run([str(tmp / 'pad_colour')], check=True)

# ── 2. wiring ───────────────────────────────────────────────────────────────
def body(text, head):
    at = text.index(head)
    return text[at:text.index('\n}\n', at)]

warp = (root / 'game/232/AH_WarpPad.c').read_text(encoding='utf-8')
amap = (root / 'game/232/AH_Map.c').read_text(encoding='utf-8')
tracker = (root / 'ap/ap_tracker.c').read_text(encoding='utf-8')
pads = (root / 'ap/ap_hooks_pads.c').read_text(encoding='utf-8')

# The display state is AP_PadState plus the racer fact, nothing else.
look = body(pads, 'int AP_PadLook(int physLevelID, int destLevelID)')
assert 'AP_PadState(physLevelID, destLevelID)' in look and 'AP_ContentPadRacerMet(pad)' in look
assert 'AP_PadLookFromState(state, racerMissing)' in look

# Beam: the one beam birth takes the table's tint for its pad, full strength.
build = warp[warp.index('void AH_WarpPad_LInB(struct Instance *inst)'):]
beam_at = build.index('INSTANCE_Birth3D(gGT->modelPtr[STATIC_BEAM], "beam", t);')
beam = build[beam_at:build.index('warppadObj->inst[WPIS_OPEN_BEAM] = newInst;', beam_at)]
assert 'AP_PadLookBeamTint(AP_PadLook(levelID, warppadObj->levelID))' in beam
assert 'newInst->colorRGBA = apBeamTint;' in beam and 'newInst->alphaScale = AP_PAD_BEAM_ALPHA;' in beam
assert warp.count('INSTANCE_Birth3D(gGT->modelPtr[STATIC_BEAM]') == 1, 'one beam birth'

# Map: the AP block draws the table colour, not a font palette slot.
warppads = body(amap, 'void AH_Map_Warppads(s16 *ptrMap, struct Thread *warppadThread, s16 *param_3)')
ap_block = warppads[warppads.index('#ifdef CTR_AP\n\t\t// AP Warp-Pad State Model v2'):]
ap_block = ap_block[:ap_block.index('#endif')]
assert 'AP_PadLook(physLevelID, destLevelID)' in ap_block
assert 'AP_PadLookMapVertex(look, dim)' in ap_block
assert 'AP_MapFlashOn()' in ap_block, 'Map Flash still governs the flicker'
for slot in ('RED', 'LIGHT_GREEN', 'FOREST_GREEN', 'ORANGE', 'PERIWINKLE', 'BLUE', 'GRAY'):
    assert not re.search(r'color\s*=\s*' + slot + r'\b', ap_block), slot
assert 'AH_Map_DrawPadIconRGB(ptrMap, (int *)&warppadInst->matrix.t[0], apMapVertex);' in warppads

# Tracker: the node dot is the table colour.
draw = body(tracker, 'static void AP_TrackerDraw(void)')
assert 'AP_PadLook(n->physical,n->destination)' in draw
assert 'AP_PadLookRGB(AP_TrackerNodeColour(look))' in draw
assert 'AP_TrackerCircle(n->x,n->y,8,color,0);' in draw
assert 'colours[]' not in draw and 'TRACKER_PAD_ORANGE' not in tracker

print('ok pad colour wiring')
