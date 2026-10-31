"""One generator row 6: the production text behind plan pads. The pure decisions
run in test-plan-pads.c; this pins that the engine calls them, and that a room
without a plan cannot reach any of it. It cannot run the engine: the warp, the
menu on screen, the floating requirement and the hints need a played room.
Run from the repository root.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
warp = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")
pads = (root / "ap/ap_hooks_pads.c").read_text(encoding="utf-8")
gates = (root / "ap/ap_hooks_gates.c").read_text(encoding="utf-8")
seedcfg = (root / "ap/ap_seedcfg.cpp").read_text(encoding="utf-8")
winlogic = (root / "ap/ap_win_logic.c").read_text(encoding="utf-8")
race = (root / "ap/ap_hooks_race.c").read_text(encoding="utf-8")
menu = (root / "game/230/MM_ConfigMenu.c").read_text(encoding="utf-8")
pages = (root / "include/ctr_menu_pages.h").read_text(encoding="utf-8")
config = (root / "platform/native_config.c").read_text(encoding="utf-8")
rect = (root / "game/RECTMENU.c").read_text(encoding="utf-8")
tracker = (root / "ap/ap_tracker.c").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at)]


tick = body(warp, "void AH_WarpPad_ThTick(struct Thread *t)")
plan = tick[tick.index("if (ap_content_plan_active())\n\t{\n\t\tconst ctr_content_pad *pad = ap_content_plan_pad(physLevelID);"):]
plan = plan[:plan.index("goto WarpPad_RequestLoad;")]

# Item 2, the race type menu: on plan pads only, from the pad's own rows.
assert "AP_ContentMenuRows(pad, &apRsRows);" in plan
assert "AP_RaceSelectorDecide(&apRsRows, AP_RaceTypeSelectorSetting())" in plan
assert "RECTMENU_Show(&apRaceTypeMenu);" in plan
assert "if (!pad->crystal && !pad->cup)" in plan
assert warp.count("AP_ContentMenuRows(") == 1 and warp.count("RECTMENU_Show(&apRaceTypeMenu)") == 1
assert "AP_RaceSelectorReplayMask(rows)" in warp and "RECTMENU_APSetReplayRows(0, 0);" in plan
assert "ADV_MASK_HINT_ID_CTR_TOKEN_CHALLENGE" in plan and "ADV_MASK_HINT_ID_RELIC_CHALLENGE" in plan
assert "apContentModeMenu" not in warp and "AP_PadUsesSelector" not in warp
# The ordinary pad path is the base's: no menu there, and no replay of a Done pad.
ordinary = tick[tick.index("goto WarpPad_RequestLoad;"):]
assert "apRaceTypeMenu" not in ordinary and "AP_ContentMenuRows" not in ordinary
replay = body(pads, "int AP_PadReplayable(")
assert "ap_content_plan_active() ? ap_content_plan_pad(physLevelID) : NULL" in replay and "!pad->crystal" in replay
sel = body(gates, "int AP_RaceTypeSelectorSetting(void)")
assert "AP_RaceSelectorEffective(g_config.raceTypeSelector" in sel and "client->present ?" in sel
assert '"race_type_selector"' in pages and '"Race Type Menu"' in config and "s_raceMenuNames" in menu
assert "RECTMENU_APReplayRow(menu, sVar6)" in rect

# Item 3, presentation: AP BOXES LEFT and no info line.
draw = tick[:tick.index("// spawn P1 in the back")]
for gone in ("Incomplete", "Complete (replay)", "Install content", "win %s first", "locked, needs", "apLockLift"):
    assert gone not in warp, gone
assert "apBoxesLeft = AP_ContentBoxesLine(ap_content_plan_pad(levelID));" in draw
assert "AP BOXES LEFT: %d" in draw

# Item 4, the pad closes again and floats stage 2.
closed = plan.index("AP_PadState(physLevelID, levelID) == 3")
assert closed < plan.index("AH_WarpPad_WarpCapture(gGT)") and "goto WarpPad_AnimateOpen;" in plan[closed:closed + 80]
build = body(warp, "static void AH_WarpPad_BuildInstances(struct Thread *t)")
gate = warp[warp.index("if (pad && AP_PadState(levelID, warppadObj->levelID) == 3)"):]
gate = gate[:gate.index("goto ContentPad_BuildGate;")]
assert "AP_ContentMaskToReq(pad->stage2_mask, pad->stage2_count, &rule);" in gate
assert "unlockItem_numNeeded = pad->stage2_count;" in gate and "AP_ContentStage2Owned(pad)" in gate
# A replayable Done pad is built open and taken, a spare or finished Crystal pad is not.
assert "!AP_PadReplayable(levelID, warppadObj->levelID)" in warp
assert "!AP_PadReplayable(ctr_cfg_warp_phys(warppadObj->levelID), warppadObj->levelID)" in tick
assert body(gates, "int AP_ContentTrackPadState(").count("AP_ContentPadPlainLeft(pad)") == 1

# Item 5, the glow lists open races and the pad state every location.
assert "return AP_ContentPadGlowBits(destLevelID, outBits, cap, 1);" in body(pads, "int AP_PadUncollectedDisplayBits(")
assert "return AP_ContentPadGlowBits(destLevelID, outBits, cap, 0);" in pads
assert "if(openOnly && !AP_ContentModeOpen(pad,pad->modes[n]))continue;" in body(pads, "static int AP_ContentPadGlowBits(")

# Item 6, stakes through the exported logic on a plan room.
parse = seedcfg[seedcfg.index("content_result = parse_content_plan(j);"):]
parse = parse[:parse.index("if (content_result != 1)")]
assert "if (content_result == 2)\n\t\tap_seedcfg_parse_win_logic(j);" in parse
decide = body(winlogic, "static int AP_WinDecideContent(")
assert "AP_ContentWinSet(set)" in decide and "AP_WinStakesDecide(&ctr_win_logic, &env, set, n)" in decide
winset = body(race, "int AP_ContentWinSet(long set[AP_WS_WINSET_MAX])")
assert "pad->relic_locations[k]" in winset and "ap_content_mode_paired_trophy(pad, context->mode)" in winset
assert "AP_ContentWinFacts" not in race and "AP_WinContentCapMet" not in winlogic
row = body((root / "ap/ap_deathlink.c").read_text(encoding="utf-8"), "int AP_DeathLinkRowValue(int row)")
assert "ap_content_plan_active()" in row and "AP_DeathLinkRowShown(row, ctr_cfg.death_link, AP_DeathLinkStateNow().recv)" in row
assert "s_dlinkNames[Dlink_Index(AP_DeathLinkRowValue(*(int *)e->valuePtr))]" in menu

# Item 1, the tracker reads the pad on a plan and nothing changes without one.
nodes = body(tracker, "static void AP_TrackerBuildNodes(void)")
assert "if (ap_content_plan_active()) {" in nodes and "n->destination = physical;" in nodes
assert "if (plan) { AP_TrackerDrawContentNode(n,x,y); return; }" in tracker
assert "ctr_cfg.tracker_pad_launch" not in (root / "ap/ap_tracker_launch.c").read_text(encoding="utf-8")
print("PASS: plan pads production text")
