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

		/* beam: only the states that can carry a beam get a hue, and each
		 * gets the three parts: hue on the beam and the wisps, a filter.
		 * Open is the beam green, stage 2 open the beam violet; the map dot
		 * and the tracker keep the table colour. */
		beam = AP_PadLookBeamHue(look);
		if (look == AP_PAD_LOOK_OPEN) {
			snprintf(why, sizeof why, "beam %s is the beam green, wisps too, with a filter", familiar[i].name);
			EXPECT(beam == AP_PAD_BEAM_GREEN && AP_PadLookWispHue(look) == AP_PAD_BEAM_GREEN &&
			       AP_PadLookBeamFilter(look) == AP_PAD_BEAM_FILTER && AP_PadLookBeamAlpha(look) == AP_PAD_BEAM_STATE_ALPHA, why);
		} else if (look == AP_PAD_LOOK_STAGE2_OPEN) {
			snprintf(why, sizeof why, "beam %s is the beam violet, wisps too, with its own filter", familiar[i].name);
			EXPECT(beam == AP_PAD_BEAM_VIOLET && AP_PadLookWispHue(look) == AP_PAD_BEAM_VIOLET &&
			       AP_PadLookBeamFilter(look) == AP_PAD_BEAM_VIOLET_FILTER &&
			       AP_PadLookBeamAlpha(look) == AP_PAD_BEAM_STATE_ALPHA && rgb == 0x93ADFAu, why);
		} else {
			snprintf(why, sizeof why, "%s has no beam colour (retail beam, no beam, or not a pad)", familiar[i].name);
			EXPECT(beam == 0 && AP_PadLookWispHue(look) == 0 && AP_PadLookBeamFilter(look) == 0 &&
			       AP_PadLookBeamAlpha(look) == 0xC00, why);
		}

		for (j = 0; j < i; j++) {
			snprintf(why, sizeof why, "%s and %s differ", familiar[i].name, familiar[j].name);
			EXPECT(AP_PadLookRGB(familiar[j].look) != rgb, why);
		}
	}
	/* A2-45: a Done pad births the retail light blue beam only while it can be
	 * entered for a replay. Stage 2 waiting and every other state are untouched
	 * (their beam comes from the unlocked or replayable build path, not this). */
	EXPECT(AP_PadDoneBeam(AP_PAD_LOOK_DONE, 1) == 1, "Done and enterable: beam");
	EXPECT(AP_PadDoneBeam(AP_PAD_LOOK_DONE, 0) == 0, "Done and not enterable: no beam");
	for (i = 0; i < AP_PAD_LOOK_COUNT; i++)
		if (i != AP_PAD_LOOK_DONE)
			EXPECT(AP_PadDoneBeam(i, 1) == 0 && AP_PadDoneBeam(i, 0) == 0, "only a Done pad takes the Done beam");
	EXPECT(AP_PadLookBeamHue(AP_PAD_LOOK_DONE) == 0 && AP_PadLookWispHue(AP_PAD_LOOK_DONE) == 0 &&
	       AP_PadLookBeamFilter(AP_PAD_LOOK_DONE) == 0 && AP_PadLookHasBeamColour(AP_PAD_LOOK_DONE) == 0,
	       "the Done beam keeps the retail colour: no hue, no filter");
	EXPECT(AP_PadLookBeamHue(AP_PAD_LOOK_STAGE2_WAITING) == 0 && AP_PadLookWispHue(AP_PAD_LOOK_STAGE2_WAITING) == 0 &&
	       AP_PadLookBeamFilter(AP_PAD_LOOK_STAGE2_WAITING) == 0, "stage 2 waiting beam stays retail light blue");
	EXPECT(AP_PadLookRGB(AP_PAD_LOOK_DONE) == 0x9AA4AEu, "the Done map dot stays grey");
	EXPECT(AP_PadLookRGB(AP_PAD_LOOK_NONE) == 0 && AP_PadLookBeamHue(AP_PAD_LOOK_NONE) == 0,
	       "no state: no colour, retail beam");
	EXPECT(AP_TrackerNodeColour(AP_PAD_LOOK_NONE) == AP_PAD_LOOK_OPEN, "a node with no state draws open, as before");
	EXPECT(N + 1 == AP_PAD_LOOK_COUNT, "every display state is in the table");

	/* Gold only for a beaten garage. */
	for (s = 0; s < 7; s++)
		EXPECT(s == AP_TRACKER_NODE_BEATEN || AP_PadLookFromState(s, 0) != AP_PAD_LOOK_BOSS_BEATEN,
		       "no pad state is boss gold");

	/* shaped beam: the hue at each vertex's own brightness, fade kept */
	EXPECT(AP_PadBeamHueWord(0x00000000u, 0x70E860u) == 0, "black stays black");
	EXPECT(AP_PadBeamHueWord(0x00FFD2B1u, 0x70E860u) == 0x0060E870u, "brightest beam colour takes the full hue");
	EXPECT(AP_PadBeamHueWord(0x00520000u, 0xFFFFFFu) == 0x00525252u, "brightness is the strongest channel");
	EXPECT(AP_PadBeamHueWord(0x7F000000u, 0x70E860u) == 0x7F000000u, "the top byte is kept");
	EXPECT(AP_PAD_BEAM_STATE_ALPHA > 0xC00 && AP_PAD_BEAM_STATE_ALPHA < 0x1000, "softer than the retail beam, still visible");
	/* ruled 2026-11-05: green 60% plus filter 31%, stage 2 the same way */
	EXPECT(AP_PAD_BEAM_STATE_ALPHA == 0xD9A, "state beams at 60% of the retail strength");
	EXPECT(AP_PAD_BEAM_FILTER_ALPHA == 0xB00, "filter at 31% of its colours");
	EXPECT(AP_PAD_BEAM_GREEN == 0x20FF20u && AP_PAD_BEAM_FILTER == 0x6000FFu, "open beam green and its filter");
	EXPECT(AP_PAD_BEAM_VIOLET == 0x6A4CFFu && AP_PAD_BEAM_VIOLET_FILTER == 0xA0FF00u, "stage 2 beam violet and its filter");
	EXPECT(AP_PAD_BEAM_FILTER_ALPHA > 0 && AP_PAD_BEAM_FILTER_ALPHA < 0x1000, "the filter is partial, still visible");
	EXPECT(((AP_PAD_BEAM_GREEN >> 8) & 0xff) == 0xff && (AP_PAD_BEAM_GREEN >> 16) < 0x40 && (AP_PAD_BEAM_GREEN & 0xff) < 0x40,
	       "beam green is a pure green");
	EXPECT(((AP_PAD_BEAM_FILTER >> 8) & 0xff) == 0 && (AP_PAD_BEAM_FILTER & 0xff) > 0,
	       "the filter takes out blue and never green");
	EXPECT((AP_PAD_BEAM_VIOLET & 0xff) == 0xff && (AP_PAD_BEAM_VIOLET >> 16) > ((AP_PAD_BEAM_VIOLET >> 8) & 0xff) &&
	       (AP_PAD_BEAM_VIOLET >> 16) < 0x80, "beam violet is a blue with some red, more red than green");
	EXPECT((AP_PAD_BEAM_VIOLET_FILTER & 0xff) == 0 && ((AP_PAD_BEAM_VIOLET_FILTER >> 8) & 0xff) == 0xff &&
	       (AP_PAD_BEAM_VIOLET_FILTER >> 16) > 0, "the violet filter takes out green and some red, never blue");
	EXPECT(AP_PAD_BEAM_VIOLET != AP_PadLookRGB(AP_PAD_LOOK_STAGE2_OPEN) && AP_PAD_BEAM_GREEN != AP_PadLookRGB(AP_PAD_LOOK_OPEN),
	       "the beam hues are beam-only; map dot and tracker keep the table");
	EXPECT(AP_PadTpageBlend(0x003a, 2) == 0x005a && AP_PadTpageBlend(0x005a, 1) == 0x003a && AP_PadTpageBlend(0xffff, 2) == 0xffdf,
	       "blend mode is tpage bits 5 and 6, nothing else changes");

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

# Beam: the one beam birth takes the shaped state model for its pad.
build = warp[warp.index('void AH_WarpPad_LInB(struct Instance *inst)'):]
beam_at = build.index('INSTANCE_Birth3D(beamModel, "beam", t);')
pre = build[build.rindex('if (gGT->modelPtr[STATIC_BEAM] != 0)', 0, beam_at):beam_at]
assert 'AP_PadLook(levelID, warppadObj->levelID)' in pre
assert 'AH_WarpPad_ApBeamModel(beamModel, apBeamLook)' in pre
beam = build[beam_at:build.index('warppadObj->inst[WPIS_OPEN_BEAM] = newInst;', beam_at)]
assert 'newInst->alphaScale = (s16)AP_PadLookBeamAlpha(apBeamLook);' in beam
assert 'colorRGBA' not in beam, 'the shaped beam keeps the retail far colour'
assert warp.count('"beam", t);') == 2, 'open beam birth and the Done replay beam'
# The state beam's subtractive copy is born after the retail children (a full
# pool drops only the copy), in the beam's place, at the filter strength, once.
wisp_end = build.index('warppadObj->inst[WPIS_OPEN_RING1 + i] = newInst;')
filt_at = build.index('INSTANCE_Birth3D(filterModel, "beamFilter", t);')
assert wisp_end < filt_at
filt = build[build.rindex('#ifdef CTR_AP', 0, filt_at):filt_at + 400]
assert 'int apFilterLook = AP_PadLook(levelID, warppadObj->levelID);' in filt
assert 'AP_PadLookBeamFilter(apFilterLook)' in filt and 'AH_WarpPad_ApCopySlot(AP_BEAM_USE_FILTER, apFilterLook), 1);' in filt
assert 'AH_WarpPad_ApFilterOf(t) == 0' in filt and 'if (f != 0)' in filt
# The filter leaves room in the pool for the prizes born after it (their
# births are not null-checked).
assert 'gGT->JitPools.instance.free.count > AP_BEAM_FILTER_POOL_RESERVE' in filt
assert '#define AP_BEAM_FILTER_POOL_RESERVE 3' in warp
assert 'f->matrix = warppadObj->inst[WPIS_OPEN_BEAM]->matrix;' in filt and 'f->alphaScale = AP_PAD_BEAM_FILTER_ALPHA;' in filt
assert warp.count('"beamFilter", t);') == 1, 'one filter birth'
# No pointer to it outside the instance pool: it is found by thread and name.
assert not re.search(r's_apBeamFilter\b', warp), 'no filter pointer table'
finder = body(warp, 'static struct Instance *AH_WarpPad_ApFilterOf(struct Thread *t)\n{')
assert 'JitPools.instance.taken.first' in finder and 'it->thread == t' in finder and '"beamFilter"' in finder
# ...turns with the beam and dies with it.
tick_spin = warp[warp.index('ConvertRotToMatrix(&instArr[WPIS_OPEN_BEAM]->matrix, &warppadObj->spinRot_Beam);'):]
tick_spin = tick_spin[:tick_spin.index('wispRiseRate = 0x20;')]
assert 'AH_WarpPad_ApFilterOf(t)' in tick_spin and 'ConvertRotToMatrix(&apFilter->matrix, &warppadObj->spinRot_Beam);' in tick_spin
# A savestate restore can bring back instances that point at copies built
# later from another hub's models: a pad with a state beam rebuilds stale copies.
assert 'AH_WarpPad_ApRefreshCopies(sdata->gGT);' in tick_spin
refresh = body(warp, 'static void AH_WarpPad_ApRefreshCopies(struct GameTracker *gGT)\n{')
assert 's_apBeamHueSource[slot][0] == (uintptr_t)retail' in refresh and 'retail->headers[0].ptrCommandList' in refresh
assert 'AH_WarpPad_ApHueModel(retail, rgb, slot, use == AP_BEAM_USE_FILTER)' in refresh
assert 's_apBeamHueModel[slot].numHeaders = 0;' in refresh
destroy = body(warp, 'void AH_WarpPad_ThDestroy(struct Thread *t)')
beam_death = destroy[destroy.index('INSTANCE_Death(instArr[WPIS_OPEN_BEAM]);'):]
beam_death = beam_death[:beam_death.index('for (i = WPIS_OPEN_RING1')]
assert 'AH_WarpPad_ApFilterOf(t)' in beam_death and 'INSTANCE_Death(apFilter);' in beam_death
# The wisps take the state wisp model; open and stage 2 open keep apart copies.
assert 'AH_WarpPad_ApWispModel(wispModel, AP_PadLook(levelID, warppadObj->levelID))' in build
assert 'INSTANCE_Birth3D(wispModel, "bottomRing", t);' in build
assert warp.count('"bottomRing", t);') == 1, 'one wisp birth'
model = body(warp, 'static struct Model *AH_WarpPad_ApHueModel(struct Model *retail, u32 rgb, int slot, int subtract)')
assert 'AP_PadBeamHueWord(mh->ptrColors[i], rgb)' in model
assert 'count = cmd[0];' in model
assert 'AP_PadTpageBlend(s_apBeamFilterTex[h][i].tpage, 2)' in model
assert '(cmd[i] >> 16) != 0 && (int)(cmd[i] & 0x1ffu) > textures' in model, 'colour-only commands carry no texture index'
slot = body(warp, 'static int AH_WarpPad_ApCopySlot(int use, int look)')
assert 'use * 2 + (look == AP_PAD_LOOK_STAGE2_OPEN ? 1 : 0)' in slot and '#define AP_BEAM_COPIES (AP_BEAM_USES * 2)' in warp
assert 'AH_WarpPad_ApCopySlot(AP_BEAM_USE_BEAM, look)' in warp and 'AH_WarpPad_ApCopySlot(AP_BEAM_USE_WISP, look)' in warp
# A2-45: the Done branch births the beam only for AP_PadDoneBeam (Done look and
# AP_PadReplayable, the ThTick Done guard's condition), then returns; no tint.
done_at = build.index('WPIS_CLOSED_X] = newInst;')
done = build[done_at:build.index('\t\treturn;\n\t}', done_at)]
assert 'AP_PadDoneBeam(AP_PadLook(levelID, warppadObj->levelID),' in done
assert 'AP_PadReplayable(levelID, warppadObj->levelID)' in done
assert 'INSTANCE_Birth3D(gGT->modelPtr[STATIC_BEAM], "beam", t);' in done
assert 'warppadObj->inst[WPIS_OPEN_BEAM] = newInst;' in done
assert 'colorRGBA' not in done and 'ApBeamModel' not in done and 'ApWispModel' not in done, 'retail colour, no tint'
assert 'beamFilter' not in done and 'alphaScale = 0xc00;' in done, 'retail strength, no filter'
# The rebuild key follows enterability, so a Done pad that stops being enterable
# while its state stays 5 loses the beam on the next rebuild.
rebirth = body(warp, 'static int AH_WarpPad_RebirthKey(int physLevelID, int destLevelID)')
assert 'state == 5 && AP_PadReplayable(physLevelID, destLevelID)' in rebirth
assert warp.count('AH_WarpPad_RebirthKey(') == 3 and 'int rbState = AH_WarpPad_RebirthKey(' in warp
assert 's_padRebirthState[levelID] = ctr_cfg_active() ? AH_WarpPad_RebirthKey(' in warp
guard = warp[warp.index('AP_PadState(ctr_cfg_warp_phys(warppadObj->levelID), warppadObj->levelID) == 5 &&'):]
assert '!AP_PadReplayable(ctr_cfg_warp_phys(warppadObj->levelID), warppadObj->levelID)' in guard[:300], 'same condition as the Done guard'

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
