"""Green only for checks in logic (A3-37): the production wiring. The colour
decision runs in test-pad-logic-look.c; this pins that AP_PadLook applies it to
the pad's display set with the real logic, that it never feeds the pad state,
and that the beam, map dot, tracker and rebuild key all read the one look.
It cannot run the engine. Run from the repository root.
"""
from pathlib import Path

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
# Cached until the state generation moves, the same counter the rebuild reads.
assert "AP_StateGen()" in glow

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
print("pad logic look production wiring: ok")
