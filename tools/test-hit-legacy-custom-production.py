"""Compile the production Hit Character pad opportunity for legacy custom content (#133 PR 5c).

Since PR 5c the same Hit rule holds on custom tracks in every room: the
legacy Cortex Vortex pad track (destination 110, raced on host 13) and a Gem
Cup a legacy custom track took over (its one custom race) seat the encounter
field, so their pads keep a Hit opportunity while an eligible target is
unhit. The production AP_HitPadOpportunity (ap/ap_hooks_pads.c) is compiled
with the pure decisions it calls (ap/ap_hit_policy.h) and the real custom
destination owner (ap/ap_custom_pad_logic.h); the encounter gather is a
controlled seam that records which draw destination it was asked about.
The pure seat count of a cup load (AP_HitCupSeatsPure) is pinned too.
"""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
source = ap_hooks_unit.read()


def function(signature, text=source):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CTR_CUSTOM_TRACKS 1
#include "ap/ap_hit_policy.h"
#include "ap/ap_custom_pad_logic.h"
#define CORTEX_TRACK_HOST_LEVEL 13
ctr_seed_config ctr_cfg;
static int enabled = 1, cortexValid, askedDest, opportunity;
int AP_HitEncounterEnabled(void) { return enabled; }
static int AP_CortexDestValid(void) { return cortexValid; }
static int AP_HitEffectivePlayer(int phys) { (void)phys; return 0; }
int AP_HitEncounterOpportunity(int dest, int player) { (void)player; askedDest = dest; return opportunity; }
void AP_LogLine(const char *s) { (void)s; }
'''
fixture += function("int AP_HitPadOpportunity(int physLevelID, int destLevelID)\n{") + "\n"
fixture += r'''
static int ask(int dest) { askedDest = -1; return AP_HitPadOpportunity(5, dest); }
int main(void) {
 int cases = 0;
 memset(&ctr_cfg, 0, sizeof ctr_cfg);
 ctr_cfg.trial_track_valid[0] = ctr_cfg.trial_track_valid[1] = 1;
 opportunity = 3;
 /* Retail track: its own destination, unchanged. */
 assert(ask(3) == 1 && askedDest == 3); cases++;
 /* Legacy Cortex Vortex pad track: drawn on host 13, only while valid. */
 cortexValid = 0; assert(ask(110) == 0 && askedDest == -1); cases++;
 cortexValid = 1; assert(ask(110) == 1 && askedDest == 13); cases++;
 /* A retail Gem Cup is never a pad Hit opportunity. */
 assert(ask(104) == 0 && askedDest == -1); cases++;
 /* The Purple cup a legacy custom track took over: its own cup order. */
 ctr_cfg.custom_tracks_ok = 1; ctr_cfg.custom_track.slot = 1; ctr_cfg.custom_track.replaces_cup_level_id = 104;
 /* A package without weapon crates hosts no Hit. */
 assert(ask(104) == 0 && askedDest == -1); cases++;
 ctr_cfg.custom_track.flags.crates = 1;
 assert(ask(104) == 1 && askedDest == 104); cases++;
 assert(ask(103) == 0 && askedDest == -1); cases++;
 /* No eligible unhit target: no opportunity. */
 opportunity = -1; assert(ask(104) == 0 && askedDest == 104); cases++;
 /* Feature off: nothing. */
 opportunity = 3; enabled = 0; assert(ask(110) == 0 && askedDest == -1); cases++;
 enabled = 1;
 /* An invalid trial row still refuses. */
 ctr_cfg.trial_track_valid[0] = 0; assert(ask(16) == 0); cases++;
 /* Cup seats: Purple four, others seven, a legacy custom event race its
    measured field minus the player, at most seven. */
 assert(AP_HitCupSeatsPure(4, 0) == 4 && AP_HitCupSeatsPure(0, 0) == 7 && AP_HitCupSeatsPure(3, 8) == 7); cases++;
 assert(AP_HitCupSeatsPure(4, 5) == 4 && AP_HitCupSeatsPure(4, 6) == 5 && AP_HitCupSeatsPure(4, 8) == 7); cases++;
 assert(AP_HitCupSeatsPure(4, 9) == 7 && AP_HitCupSeatsPure(4, 1) == 4); cases++;
 printf("PASS %d legacy custom Hit cases\n", cases);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "t.c"
    exe = Path(tmp) / "t"
    src.write_text(fixture, encoding="utf-8")
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wno-unused-function", "-DCTR_AP", "-I", str(root),
                    "-I", str(root / "ap"), "-I", str(root / "include"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
