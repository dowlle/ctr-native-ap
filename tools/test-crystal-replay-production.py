"""Run the production replay gate and tracker fact gathering for every arena.

Engine/network seams are stubbed. This does not establish Steam acceptance.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def body(path, head):
    text = (root / path).read_text()
    at = text.index(head)
    return text[at:text.index("\n}\n", at) + 3]


fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_content_modes.h"
#include "ap_pad_activity_logic.h"
#include "ap_pad_colour.h"
#define CTR_CUSTOM_TRACKS 1
#define AP_IDX_KEY 14
static ctr_content_pad p;
static int active = 1, ready = 1, racer = 1, left, ap_recv_count[200];
static int sanity_left;
int AP_SanityPadPending(int physical, int family) { (void)physical; return family == 0 ? sanity_left : 0; }
int ap_content_plan_active(void) { return active; }
const ctr_content_pad *ap_content_plan_pad(int physical) {
    return active && physical == p.physical ? &p : NULL;
}
int AP_ContentPadReady(int physical) { return physical == p.physical && ready; }
int AP_CharacterUnlocked(int id) { (void)id; return racer; }
int AP_ContentCodeChecked(int64_t code) { (void)code; return !left; }
int AP_PadUncollectedGlowBits(int physical, int *bits, int cap) {
    (void)bits; (void)cap; assert(physical == p.physical); return left;
}
int AP_ContentTrackPadState(const ctr_content_pad *pad) {
    return ap_content_track_pad_state(pad, ap_recv_count, AP_ContentCodeChecked, 0);
}
int AP_TrackerLaunchEnabled(void) { return active; }
int AP_GateCount(int index) { return ap_recv_count[index]; }
void AP_ContentSelectorBuild(const ctr_content_pad *pad, ap_content_selector *sel) {
    ap_content_selector_build(pad, ap_recv_count, AP_ContentCodeChecked, sel);
}
int AP_ContentPadPlainLeft(const ctr_content_pad *pad) { (void)pad; return 0; }
int AP_ContentStage2Owned(const ctr_content_pad *pad) {
    (void)pad; assert(0 && "Crystal Challenges have no second stage"); return 0;
}
'''

production = "\n".join([
    body("ap/ap_hooks_gates.c", "int AP_ContentPadOpen("),
    body("ap/ap_hooks_gates.c", "int AP_ContentPadRacerMet("),
    body("ap/ap_hooks_gates.c", "int AP_ContentPadRuleOwned("),
    body("ap/ap_hooks_pads.c", "int AP_PadState("),
    body("ap/ap_hooks_pads.c", "int AP_PadReplayable("),
    body("ap/ap_tracker_launch.c", "int AP_PadActivities("),
])

tests = r'''
int main(void) {
    const int arenas[] = {18, 19, 20, 21, 22, 23, 24};
    int cases = 0;
    for (int a = 0; a < 7; a++) {
        memset(&p, 0, sizeof p);
        p.physical = a == 6 ? 100 : a; /* also an arena on a cup's physical pad */
        p.retail_id = arenas[a]; p.crystal = 1; p.racer_lock = 0;
        p.keys = a == 6 ? 2 : 1;
        p.has_rule = 1; p.rule_mask = 1; p.rule_count = 2;
        for (int mask = 0; mask < 32; mask++) {
            ap_padact_list list;
            memset(ap_recv_count, 0, sizeof ap_recv_count);
            p.occupied = !!(mask & 1); ready = !!(mask & 2);
            racer = !!(mask & 4); ap_recv_count[0] = mask & 8 ? 2 : 1;
            ap_recv_count[AP_IDX_KEY] = mask & 16 ? p.keys : p.keys - 1;
            for (left = 0; left <= 1; left++) {
                int open = mask == 31;
                assert(AP_PadReplayable(p.physical, p.retail_id) == (open && !left));
                assert(AP_PadActivities(1, p.physical, p.retail_id, &list));
                assert(list.count == (p.occupied ? 1 : 0));
                if (p.occupied) assert(list.activity[0] == AP_PADACT_CRYSTAL);
                assert(AP_PadActivityStillOpen(&list, AP_PADACT_CRYSTAL, -1) == open);
                if (open) {
                    int state = AP_PadState(p.physical, p.retail_id);
                    assert(state == (left ? 2 : 5));
                    assert(list.left[0] == left && !list.stage2[0]);
                    assert(AP_PadDoneBeam(AP_PadLookFromState(state, 0),
                                         AP_PadReplayable(p.physical, p.retail_id)) == !left);
                    if (!left) assert(AP_PadLookRGB(AP_PadLookFromState(state, 0)) == 0x9AA4AEu);
                }
                cases++;
            }
        }
        /* Gate changes during a tracker launch must revoke the replay. */
        left = 0; active = 0;
        assert(!AP_PadReplayable(p.physical, p.retail_id));
        active = 1;
        assert(!AP_PadReplayable(p.physical + 1, p.retail_id));
    }
    memset(&p, 0, sizeof p);
    p.physical = 3; p.occupied = p.cup = ready = active = 1; p.racer_lock = -1; left = 0;
    sanity_left = 1;
    assert(AP_PadState(3, 0) == 2); /* Sanity keeps a cup open outside the glow cycle. */
    sanity_left = 0;
    assert(AP_PadState(3, 0) == 5);
    printf("PASS: %d Crystal Challenge replay/tracker gate cases across seven arenas, plus cup sanity state\n", cases);
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "crystal-replay.c", Path(tmp) / "crystal-replay"
    src.write_text(fixture + production + tests)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-fsanitize=undefined",
                    "-I", str(root / "ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
