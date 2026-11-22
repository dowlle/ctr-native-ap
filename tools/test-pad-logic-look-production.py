"""Green only for checks in logic (A3-37): the production wiring. The colour
decision runs in test-pad-logic-look.c; this pins that AP_PadLook applies it to
the pad's display set with the real logic, that it never feeds the pad state,
and that the beam, map dot, tracker and rebuild key all read the one look.
It cannot run the engine. Run from the repository root.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
pads = (root / "ap/ap_hooks_pads.c").read_text(encoding="utf-8")
logic = (root / "ap/ap_win_logic.c").read_text(encoding="utf-8")
warp = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")
amap = (root / "game/232/AH_Map.c").read_text(encoding="utf-8")
tracker = (root / "ap/ap_tracker.c").read_text(encoding="utf-8")
hooks_h = (root / "ap/ap_hooks.h").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at)]


look = body(pads, "int AP_PadLook(int physLevelID")
assert "AP_PadLogicLook(physLevelID, AP_PadLookFromState(state, racerMissing))" in look

glow = body(pads, "static int AP_PadLogicLook(")
# Only the two open looks are judged, on a plan, from the display set (what the
# player can act on now), through the three-way classifier.
assert "AP_PAD_LOOK_OPEN" in glow and "AP_PAD_LOOK_STAGE2_OPEN" in glow
assert "ap_content_plan_active()" in glow
assert "AP_ContentPadGlowBits(physLevelID, bits, codes, AP_PAD_DISPLAY_BITS_MAX, 1)" in glow
assert "AP_LocationLogicState((long long)codes[i])" in glow
assert "AP_PadLookByLogic(look, n + sharedCount, in, unknown)" in glow
# Asked fresh, never cached: a server update or boss-win reply moves no local
# counter, so a cached answer could stay green.
assert "static" not in glow.split("{", 1)[1] and "AP_StateGen" not in glow
# The rebuild watches the state generation plus those two inputs.
redraw = body((root / "ap/ap_hooks_goal.c").read_text(encoding="utf-8"), "unsigned AP_PadRedrawGen(void)")
assert "ap_state_gen" in redraw and "ap_net_locations_revision()" in redraw and "ap_net_boss_won_bits()" in redraw
assert "unsigned rbGen = AP_PadRedrawGen();" in warp and "unsigned rbGen = AP_StateGen();" not in warp
net = (root / "ap/ap_net.cpp").read_text(encoding="utf-8")
assert "g_locs_revision++;" in body(net, "static void ap_locs_invalidate(void)")

# The pad state, the gates and the glow never read the logic look.
state = body(pads, "int AP_PadState(int physLevelID")
assert "AP_PadLogicLook" not in state and "AP_LocationLogicState" not in state
assert pads.count("AP_PadLogicLook(") == 2  # the definition and its one caller

cls = body(logic, "int AP_LocationLogicState(long long code)")
assert "AP_PadLogicStateOf(&ctr_win_logic, &env, (long)code)" in cls and "AP_WinEnvNow()" in cls
assert "#include \"ap_pad_logic_look.h\"" in hooks_h

# The three surfaces and the rebuild key read AP_PadLook, nothing private.
assert "AP_PadLook(physLevelID, destLevelID)" in amap
assert "AP_PadLook(n->physical,n->destination)" in tracker
rk = body(warp, "static int AH_WarpPad_RebirthKey(")
assert "AP_PadLook(physLevelID, destLevelID)" in rk
birth = (root / "ap/ap_pad_birth.h").read_text(encoding="utf-8")
assert "((look & 15) << 4)" in birth

# Compile the production collector as well as checking its engine wiring. A
# pad holding only blocked sanity checks must not stay green merely because
# those checks intentionally have no glow entries.
program = r'''
#include <assert.h>
#include <stdint.h>
#include "ap_pad_logic_look.h"
#include "ap_content_modes.h"
#define AP_PAD_DISPLAY_BITS_MAX 256
#define AP_PAD_SHARED_CHECKS_MAX 39
typedef struct { int64_t code; } ap_pad_shared_check;
static int active = 1, glow_count, states[105], checked[105], mode_open;
static ctr_content_pad pad = {.mode_count = 1, .modes = { AP_CONTENT_MODE_CTR }};
int ap_content_plan_active(void) { return active; }
const ctr_content_pad *ap_content_plan_pad(int physical) { assert(physical == 3); return &pad; }
static int AP_ContentModeOpen(const ctr_content_pad *p, int mode) {
    assert(p == &pad && mode == AP_CONTENT_MODE_CTR); return mode_open;
}
static int AP_ContentPadSharedChecks(const ctr_content_pad *p, unsigned modes, ap_pad_shared_check *out) {
    assert(p == &pad);
    if (modes != (1u << AP_CONTENT_MODE_CTR) || checked[104]) return 0;
    out[0].code = 104; return 1;
}
static int AP_ContentPadGlowBits(int physical, int *bits, int64_t *codes, int cap, int open) {
    (void)physical; (void)bits; (void)cap; assert(open == 1);
    codes[0] = 1; return glow_count;
}
int ap_content_plan_sanity_pad(int physical, int family, int64_t *codes, int cap) {
    (void)physical; assert(cap >= 1); codes[0] = 101 + family; return 1;
}
static int AP_ContentCodeChecked(int64_t code) { return checked[code]; }
static int AP_LocationLogicState(long long code) { return states[code]; }
'''
program += glow + '\n}\n'
program += r'''
int main(void) {
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_STAGE2_WAITING);
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_STAGE2_OPEN) == AP_PAD_LOOK_STAGE2_WAITING);
    for (int family = 0; family < 3; ++family) {
        states[101 + family] = AP_PAD_LOGIC_IN;
        assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN);
        checked[101 + family] = 1;
        assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_STAGE2_WAITING);
        checked[101 + family] = 0; states[101 + family] = AP_PAD_LOGIC_OUT;
    }
    states[103] = AP_PAD_LOGIC_UNKNOWN;
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN);
    checked[101] = checked[102] = checked[103] = 1;
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN);
    mode_open = 1; /* Roo's Tubes: a shared weapon use is the only open check. */
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_STAGE2_WAITING);
    states[104] = AP_PAD_LOGIC_IN;
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN);
    checked[104] = 1;
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_DONE) == AP_PAD_LOOK_DONE);
    glow_count = 1;
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_STAGE2_WAITING);
    states[1] = AP_PAD_LOGIC_IN;
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN);
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_CLOSED) == AP_PAD_LOOK_CLOSED);
    active = 0; states[1] = AP_PAD_LOGIC_OUT;
    assert(AP_PadLogicLook(3, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ctr-pad-sanity-') as folder:
    source, executable = Path(folder) / 'test.c', Path(folder) / 'test'
    source.write_text(program)
    subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-I', str(root / 'ap'),
                    str(source), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("pad logic look production wiring: ok")

# Execute the same shared-route collector used by the lifecycle, colour and
# activity list. Room membership and collected codes stand in for AP replies.
items = (root / "ap/ap_hooks_itemsanity.c").read_text(encoding="utf-8")
launch = (root / "ap/ap_tracker_launch.c").read_text(encoding="utf-8")
tracked = body(launch, "int AP_PadActivities(")
assert "AP_ContentPadSharedChecks(pad, 1u << sel.mode[i], NULL)" in tracked
assert tracker.count("AP_ContentPadSharedChecks(plan, AP_CONTENT_PLAIN_MODES, NULL)") == 1
program = r'''
#define CTR_AP
#include <assert.h>
#include "ap_hooks.h"
#include "ap_itemsanity_logic.h"
#include "ap_pad_activity_logic.h"
static unsigned char ap_itemsanity_owned[AP_ITEMSANITY_WEAPON_COUNT];
static ctr_content_checks families;
static ctr_content_package package;
static ctr_hit_encounters encounter;
static int64_t plain = 35016002, juiced = 35016003;
static int plain_done, juiced_done, have_pickups;
static unsigned hit_mask;
static int ap_net_location_exists(long long code) { return code == plain || code == juiced; }
int AP_ContentCodeChecked(int64_t code) {
    if (code == plain) return plain_done;
    if (code == juiced) return juiced_done;
    return code != families.wumpa_global;
}
int AP_SanityPadPending(int physical, int family) {
    assert(physical == 9 || physical == 6 || physical == 102);
    return family == 0 && have_pickups;
}
const ctr_content_checks *ap_content_plan_checks(void) { return &families; }
const ctr_content_package *ap_content_plan_package(int index) { return index == 0 ? &package : NULL; }
const ctr_hit_encounters *ap_seedcfg_hit_encounters(void) { return &encounter; }
static int AP_HitEffectivePlayer(int physical) { (void)physical; return 0; }
static unsigned AP_HitEncounterPendingMask(int host, int player) {
    assert((host == 6 || host == 8) && player == 0);
    return hit_mask;
}
static int AP_ItemsanityActive(void) { return 1; }
'''
for implementation, signature in [
    (items, "int AP_ItemsanityPadPending("),
    (pads, "int AP_ContentPadSharedChecks("),
    (pads, "int AP_ContentPadPickupsLeft("),
    (pads, "int AP_ContentPadPlainLeft("),
]:
    program += body(implementation, signature) + '\n}\n'
program += r'''
int main(void) {
    const unsigned ctr = 1u << AP_CONTENT_MODE_CTR, relic = 1u << AP_CONTENT_MODE_RELIC;
    const int ids[] = {102, 6, 9}; /* Baby T Park, Sewer Speedway and Roo's Tubes */
    const int hosts[] = {-1, 8, 6};
    int counts[15] = {0};
    ap_pad_shared_check rows[AP_PAD_SHARED_CHECKS_MAX];
    package.weapon_crates = 8;
    for (int i = 0; i < 3; ++i) {
        ctr_content_pad pad = {
            .physical = ids[i], .occupied = 1, .retail_id = hosts[i],
            .package_index = i == 0 ? 0 : -1, .racer_lock = -1,
            .mode_count = 2, .modes = {AP_CONTENT_MODE_CTR, AP_CONTENT_MODE_RELIC},
            .stages = {1, 2}, .has_stage2 = 1, .ctr_first = 1, .trophy_location = 1,
            .ctr_location = 2, .relic_locations = {3}
        };
        plain_done = juiced_done = 0;
        ap_itemsanity_owned[1] = 0;
        assert(!AP_ContentPadPlainLeft(&pad)); /* no owned pending weapon */
        ap_itemsanity_owned[1] = 1;
        assert(AP_ContentPadPlainLeft(&pad));
        assert(ap_content_track_pad_state(&pad, counts, AP_ContentCodeChecked, AP_ContentPadPlainLeft(&pad)) == 4);
        assert(AP_ContentPadSharedChecks(&pad, ctr, rows) == 2);
        assert(rows[0].code == plain && rows[1].code == juiced && rows[1].family == AP_PAD_SHARED_ITEMSANITY);
        assert(!AP_ContentPadSharedChecks(&pad, relic, NULL));
        plain_done = 1;
        assert(AP_ContentPadSharedChecks(&pad, ctr, rows) == 1 && rows[0].code == juiced);
        juiced_done = 1;
        assert(!AP_ContentPadPlainLeft(&pad));
        assert(ap_content_track_pad_state(&pad, counts, AP_ContentCodeChecked, AP_ContentPadPlainLeft(&pad)) == 5);
        have_pickups = 1;
        assert(AP_ContentPadPlainLeft(&pad) && !AP_ContentPadSharedChecks(&pad, ctr, NULL));
        have_pickups = 0;
        families.wumpa = 1; families.wumpa_global = 35013000; pad.wumpa_modes = ctr;
        assert(AP_ContentPadSharedChecks(&pad, ctr, rows) == 1 && rows[0].family == AP_PAD_SHARED_WUMPA);
        assert(!AP_ContentPadSharedChecks(&pad, relic, NULL));
        families.wumpa = 0;
        hit_mask = 2u; encounter.locations[1] = 35025001;
        assert(AP_ContentPadSharedChecks(&pad, ctr, rows) == 1 && rows[0].code == 35025001);
        hit_mask = 0;
        if (i == 0) {
            package.weapon_crates = 0; plain_done = 0;
            assert(!AP_ContentPadPlainLeft(&pad)); /* crate-free custom package */
            package.weapon_crates = 8;
        }
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ctr-pad-shared-') as folder:
    source, executable = Path(folder) / 'test.c', Path(folder) / 'test'
    source.write_text(program)
    subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-Wno-unused-function',
                    '-I', str(root / 'ap'), '-I', str(root / 'include'),
                    str(source), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("shared pad checks: custom and retail routes, per-mode eligibility, pickups, completion, global Wumpa and Hit Character pass")
