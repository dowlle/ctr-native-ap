"""Pad Approach Hints (#124): the production wiring. The pure decisions and
the once-ever latch run in test-pad-approach-hints.cpp; this pins that the pad
draw calls the hook where the name shows, that the hook hints exactly what the
glow lists with openOnly on an open plan pad, that the seed key is read only
from an admitted plan's envelope, and that the network sends a hint scout only
after the room's latch is read. It cannot run the engine or a server.
Run from the repository root.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
warp = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")
pads = (root / "ap/ap_hooks_pads.c").read_text(encoding="utf-8")
hooks = (root / "ap/ap_hooks.c").read_text(encoding="utf-8")
net = (root / "ap/ap_net.cpp").read_text(encoding="utf-8")
seed = (root / "ap/ap_seedcfg.cpp").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at)]


# The call sits right after the pad name is drawn, once, and nowhere else.
name = warp.index("DecalFont_DrawLine(warppadLNG,")
call = warp.index("AP_PadApproachHints(levelID);")
# A4-59 computes the lines under the name before it; the next drawing after
# the call is the stage 2 line.
assert name < call < warp.index("if (apStage2 != AP_S2LINE_NONE)", call)
assert warp.count("AP_PadApproachHints(") == 1
assert '#include "ap_pad_hints_logic.h"' in hooks

# The hook: option, latch read, plan pad, occupied, content ready, open (racer
# lock included in AP_ContentPadOpen); then the glow enumeration with openOnly.
hook = body(pads, "void AP_PadApproachHints(int levelID)")
assert "ctr_cfg.pad_approach_hints" in hook and "ap_net_pad_hints_ready()" in hook
assert "AP_PadHint_Eligible(" in hook and "AP_ContentPadOpen(pad)" in hook and "pad->occupied" in hook
assert "AP_ContentPadReady(levelID)" in hook
assert "AP_ContentPadGlowBits(levelID, bits, codes, AP_PAD_DISPLAY_BITS_MAX, 1)" in hook
assert "AP_LocationInLogic(codes[i])" in hook
assert "ap_net_pad_approach_hint(levelID, codes, eligible)" in hook
logic = (root / "ap/ap_win_logic.c").read_text(encoding="utf-8")
in_logic = body(logic, "int AP_LocationInLogic(long long code)")
assert "AP_WinEnvNow()" in in_logic and "AP_PadHint_InLogic(&ctr_win_logic, &env, code)" in in_logic
racer = body((root / "ap/ap_hooks_gates.c").read_text(encoding="utf-8"), "int AP_ContentPadOpen(")
assert "AP_CharacterUnlocked(pad->racer_lock)" in racer

# Every glow bit carries its code: no bare push is left in the enumerator.
glow = body(pads, "static int AP_ContentPadGlowBits(")
assert "outBits[count++]" not in glow and glow.count("AP_GlowPush(") >= 8
# The glow and the display still pass no codes.
assert "AP_ContentPadGlowBits(destLevelID, outBits, NULL, cap, 0)" in pads
assert "AP_ContentPadGlowBits(destLevelID, outBits, NULL, cap, 1)" in pads

# Seed key: reset every parse, read only after an admitted plan.
assert "ctr_cfg.pad_approach_hints = 0;" in seed
assert '"pad_approach_hints"};' in seed
admitted = seed[seed.index("if (content_result == 2)\n\t\t{"):]
assert 'json_int(j["ctr_options"], "pad_approach_hints", 0)' in admitted[:600]

# Network: the latch is read at connect only when the option is on; the send
# filters through it, latches, and scouts with create_as_hint 2.
connect = net[net.index("g_pad_hints.connect("):]
assert connect.index("if (ctr_cfg.pad_approach_hints)") < connect.index("g_ap->Get({g_pad_hints.key});")
send = body(net, 'extern "C" int ap_net_pad_approach_hint(')
assert "ap_net_pad_hints_ready()" in send and "AP_PadHint_Select(" in send
assert send.index("g_ap->LocationScouts(locs, 2);") < send.index("g_pad_hints.mark(")
assert 'op.operation = "update";' in send
assert body(net, 'extern "C" int ap_net_pad_hints_ready(').count("g_pad_hints.ready()") == 1
assert net.count("g_pad_hints.disconnected();") == 3
assert "g_pad_hints.retrieved(" in net and "g_pad_hints.reply(value)" in net
print("pad approach hints production: ok")
