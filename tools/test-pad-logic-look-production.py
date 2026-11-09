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
assert "AP_PadLookByLogic(look, n, in, unknown)" in glow
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
#define AP_PAD_DISPLAY_BITS_MAX 256
static int active = 1, glow_count, states[104], checked[104];
static int ap_content_plan_active(void) { return active; }
static int AP_ContentPadGlowBits(int physical, int *bits, int64_t *codes, int cap, int open) {
    (void)physical; (void)bits; (void)cap; assert(open == 1);
    codes[0] = 1; return glow_count;
}
static int ap_content_plan_sanity_pad(int physical, int family, int64_t *codes, int cap) {
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
