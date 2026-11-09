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
int main(void)
{
    int look, s;
    for (look = AP_PAD_LOOK_CLOSED; look <= AP_PAD_LOOK_DONE; look++) {
        int green = look == AP_PAD_LOOK_OPEN || look == AP_PAD_LOOK_STAGE2_OPEN;
        int done = look == AP_PAD_LOOK_DONE;
        unsigned rgb = done ? 0x9AA4AEu : green ? 0x70E860u : 0xDA4230u;
        unsigned hue = done ? AP_PAD_BEAM_GREY : green ? AP_PAD_BEAM_GREEN : AP_PAD_BEAM_RED;
        unsigned filter = done ? 0 : green ? AP_PAD_BEAM_FILTER : AP_PAD_BEAM_RED_FILTER;
        EXPECT(AP_PadLookRGB(look) == rgb, "three progress colours");
        EXPECT(AP_PadLookRGB(AP_TrackerNodeColour(look)) == rgb, "tracker shares colour");
        EXPECT(AP_PadLookBeamHue(look) == hue && AP_PadLookWispHue(look) == hue, "beam and wisps share hue");
        EXPECT(AP_PadLookBeamFilter(look) == filter, "filter follows colour");
        EXPECT(AP_PadLookBeamAlpha(look) == 0xD9A, "state beam strength retained");
        EXPECT(AP_PadBeamHueWord(0, hue) == 0, "fade retained");
        EXPECT(AP_PadBeamHueWord(0x7fffffff, hue) == (0x7f000000u | (AP_PadLookB(hue)<<16) | (AP_PadLookG(hue)<<8) | AP_PadLookR(hue)), "full hue and command byte retained");
        {
            unsigned map = AP_PadLookMapVertex(look, 0), dim = AP_PadLookMapVertex(look, 1);
            EXPECT((map & 255) == (AP_PadLookR(rgb)+1)/2 && ((map>>8)&255) == (AP_PadLookG(rgb)+1)/2 &&
                   ((map>>16)&255) == (AP_PadLookB(rgb)+1)/2, "map texture modulation");
            EXPECT(dim != map && (dim&255)<=(map&255) && ((dim>>8)&255)<=((map>>8)&255) &&
                   ((dim>>16)&255)<=((map>>16)&255), "Map Flash retains hue");
        }
        EXPECT(AP_PadDoneBeam(look, 1) == done && AP_PadDoneBeam(look, 0) == 0, "Done beam only with replay");
    }
    EXPECT(AP_PadLookFromState(1,0)==AP_PAD_LOOK_CLOSED && AP_PadLookFromState(1,1)==AP_PAD_LOOK_RACER_LOCKED, "lock reasons retained");
    EXPECT(AP_PadLookFromState(2,0)==AP_PAD_LOOK_OPEN && AP_PadLookFromState(3,0)==AP_PAD_LOOK_STAGE2_WAITING &&
           AP_PadLookFromState(4,0)==AP_PAD_LOOK_STAGE2_OPEN && AP_PadLookFromState(5,0)==AP_PAD_LOOK_DONE, "lifecycle retained");
    for (s=2; s<=5; s++)
        EXPECT(AP_PadLookFromState(s,1)==AP_PadLookFromState(s,0), "racer only refines closed");
    EXPECT(AP_PadLookRGB(AP_PAD_LOOK_BOSS_BEATEN)==0xF2C248u && AP_PadLookBeamHue(AP_PAD_LOOK_BOSS_BEATEN)==0, "garage gold retained");
    EXPECT(AP_PadLookBeamHue(AP_PAD_LOOK_NONE)==0 && AP_PadLookRGB(AP_PAD_LOOK_NONE)==0, "vanilla retained");
    EXPECT(AP_PAD_BEAM_FILTER_ALPHA==0xB00, "filter strength retained");
    EXPECT(AP_PadTpageBlend(0x003a,2)==0x005a && AP_PadTpageBlend(0xffff,2)==0xffdf, "only blend bits change");
    printf("%d checks, %d failures\n", checks, fails);
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
assert 'use * AP_BEAM_PALETTES + palette' in slot and '#define AP_BEAM_PALETTES 3' in warp
assert 'look == AP_PAD_LOOK_DONE ? 2' in slot and 'AP_PAD_LOOK_STAGE2_OPEN) ? 0 : 1' in slot
assert 'AH_WarpPad_ApCopySlot(AP_BEAM_USE_BEAM, look)' in warp and 'AH_WarpPad_ApCopySlot(AP_BEAM_USE_WISP, look)' in warp
# A2-45: the Done branch births the beam only for AP_PadDoneBeam (Done look and
# AP_PadReplayable, the ThTick Done guard's condition), then returns; no tint.
done_at = build.index('WPIS_CLOSED_X] = newInst;')
done = build[done_at:build.index('\t\treturn;\n\t}', done_at)]
assert 'AP_PadDoneBeam(AP_PadLook(levelID, warppadObj->levelID),' in done
assert 'AP_PadReplayable(levelID, warppadObj->levelID)' in done
assert 'AH_WarpPad_ApBeamModel(gGT->modelPtr[STATIC_BEAM], AP_PAD_LOOK_DONE)' in done
assert 'INSTANCE_Birth3D(doneBeam, "beam", t);' in done
assert 'warppadObj->inst[WPIS_OPEN_BEAM] = newInst;' in done
assert 'colorRGBA' not in done and 'ApWispModel' not in done, 'shaped grey beam, no wisps'
assert 'beamFilter' not in done and 'AP_PadLookBeamAlpha(AP_PAD_LOOK_DONE)' in done, 'Done strength, no filter'
# The rebuild key follows enterability, so a Done pad that stops being enterable
# while its state stays 5 loses the beam on the next rebuild (A2-45; the A3-12
# birth key in ap_pad_birth.h keeps the replay bit, tools/test-pad-rebuild-production.py).
rebirth = body(warp, 'static int AH_WarpPad_RebirthKey(int physLevelID, int destLevelID)')
assert 'AP_PadReplayable(physLevelID, destLevelID));' in rebirth and 'ap_pad_birth_key(' in rebirth
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
