"""Hint filters (A2-23): the production wiring. The pure decisions run in
test-hint-filters.cpp; this pins that the pad hook and the shop hint apply the
filters before anything is latched, and that the three seed keys are reset every
parse and read only from an admitted plan's envelope. Run from the repository root.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
pads = (root / "ap/ap_hooks_pads.c").read_text(encoding="utf-8")
shop = (root / "ap/ap_wumpa_shop_ui.c").read_text(encoding="utf-8")
seed = (root / "ap/ap_seedcfg.cpp").read_text(encoding="utf-8")
hooks = (root / "ap/ap_hooks.c").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at)]


KEYS = ("pad_hints_wait_for_spoiler", "pad_hints_progression_only", "shop_hints_progression_only")
assert '#include "ap_hint_filters_logic.h"' in hooks

hook = body(pads, "void AP_PadApproachHints(int levelID)")
garage = body(pads, "void AP_BossGarageApproachHints(int hubID)")
ready = body(pads, "static int AP_ApproachHintsReady(void)")
send = body(pads, "static void AP_ApproachSendHints(")
assert "!AP_HintFilter_PadWaits(ctr_cfg.pad_hints_wait_for_spoiler, ctr_cfg.spoiler_items," in ready
assert "AP_SpoilerPadPreviewShown()" in ready
# The wait returns before any glow enumeration or send, so nothing is latched.
assert hook.index("if (!AP_ApproachHintsReady()) return;") < hook.index("AP_ContentPadGlowBits(")
assert hook.index("AP_ContentPadGlowBits(") < hook.index("AP_ApproachSendHints(")
assert garage.index("!AP_ApproachHintsReady()") < garage.index("AP_ApproachSendHints(")
# The progression filter sits in the per-code loop, before the send that latches.
assert "AP_HintFilter_Progression(ctr_cfg.pad_hints_progression_only" in send
assert send.index("ap_net_scout_known(codes[i]") < send.index("AP_HintFilter_Progression(")
assert send.index("AP_HintFilter_Progression(") < send.index("ap_net_pad_approach_hint(")

sh = body(shop, "static void AP_WShopHint(int hub)")
assert "AP_HintFilter_Progression(ctr_cfg.shop_hints_progression_only" in sh
# An unscouted slot returns before the hub is marked hinted, so it is retried.
assert "ap_net_scout_known(code, NULL, NULL, &flags), flags)" not in sh  # no argument-order dependence
assert sh.index("known = ap_net_scout_known(") < sh.index("AP_HintFilter_Progression(")
assert sh.index("AP_HINT_LATER") < sh.index("ap_wshop.hinted[hub] = 1;")

for key in KEYS:
    assert f"ctr_cfg.{key} = 0;" in seed, key
    assert f'json_int(j["ctr_options"], "{key}", 0)' in seed, key
    assert f'"{key}"' in seed[seed.index("kAdditiveContentFields"):seed.index("bool strip_additive")], key
print("hint filters production: ok")
