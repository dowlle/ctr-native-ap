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
# A Done pad wears its grey X; a replayable one still takes the kart, a spare or
# finished Crystal pad does not.
assert "AP_PadState(levelID, warppadObj->levelID) == 5)\n\t{\n\t\twarppadObj->digit1s = 0;" in warp
assert "!AP_PadReplayable(ctr_cfg_warp_phys(warppadObj->levelID), warppadObj->levelID)" in tick
assert body(gates, "int AP_ContentTrackPadState(").count("AP_ContentPadPlainLeft(pad)") == 1
# A closing pad strands no check that belongs to no pad.
plain = body(pads, "int AP_ContentPadPlainLeft(")
assert "fam->wumpa == 1 && pad->wumpa_modes" in plain and "AP_HitEncounterOpportunity(" in plain and "AP_ItemsanityPadOpportunity(host)" in plain
# A start after a cancelled menu on the same pad is a new entry.
assert "apEntryRefusedDest = -1;\n\t\t\tdist = 0;" in tick

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


# Item 5 executed: the production enumerator over a pad with a locked stage 2.
def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


glow = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_content_modes.h"
static ctr_content_pad pad;
static int counts[15];
static long long done[8]; static int doneCount;
const ctr_content_pad *ap_content_plan_pad(int physical) { (void)physical; return &pad; }
int ap_net_location_checked(long code) { for (int i = 0; i < doneCount; i++) if (done[i] == code) return 1; return 0; }
static int checkedFn(int64_t code) { return ap_net_location_checked((long)code); }
int AP_ContentModeOpen(const ctr_content_pad *p, int mode) { return ap_content_mode_open(p, mode, counts, checkedFn); }
""" + function(pads, "static int AP_ContentPadGlowBits(") + r"""
static int has(const int *bits, int n, int bit) { for (int i = 0; i < n; i++) if (bits[i] == bit) return 1; return 0; }
int main(void) {
 int bits[32], n;
 memset(&pad, 0, sizeof pad);
 pad.physical = 3; pad.occupied = 1; pad.first_race_rule = 1; pad.mode_count = 3; pad.racer_lock = -1;
 pad.modes[0] = AP_CONTENT_MODE_TROPHY; pad.modes[1] = AP_CONTENT_MODE_CTR; pad.modes[2] = AP_CONTENT_MODE_RELIC;
 pad.stages[0] = 1; pad.stages[1] = pad.stages[2] = 2; pad.has_stage2 = 1; pad.stage2_mask = 1; pad.stage2_count = 1;
 pad.trophy_location = 10; pad.ctr_location = 11; pad.relic_locations[0] = 12;
 pad.letter_locations[0] = 13; pad.perfect_location = 14; pad.rung_locations[0] = 15; pad.box_count = 1; pad.box_locations[0] = 16;
 /* Locked later races: their checks are hidden, the pad state's list keeps them. */
 n = AP_ContentPadGlowBits(3, bits, 32, 1);
 assert(has(bits, n, AP_CONTENT_BIT_TROPHY + 3) && !has(bits, n, AP_CONTENT_BIT_CTR + 3) && !has(bits, n, AP_CONTENT_BIT_RELIC + 9));
 assert(!has(bits, n, AP_CONTENT_BIT_FAMILY + 3 * AP_CONTENT_FAMILY_STRIDE + 6));
 assert(!has(bits, n, AP_CONTENT_BIT_FAMILY + 3 * AP_CONTENT_FAMILY_STRIDE + AP_CONTENT_FAMILY_PERFECT));
 assert(has(bits, n, AP_CONTENT_BIT_FAMILY + 3 * AP_CONTENT_FAMILY_STRIDE) && has(bits, n, AP_CONTENT_BIT_BOX + 3 * AP_CONTENT_BOX_STRIDE));
 n = AP_ContentPadGlowBits(3, bits, 32, 0);
 assert(has(bits, n, AP_CONTENT_BIT_CTR + 3) && has(bits, n, AP_CONTENT_BIT_RELIC + 9));
 /* Stage 2 met but the first race not won: still hidden. */
 counts[0] = 1; n = AP_ContentPadGlowBits(3, bits, 32, 1);
 assert(!has(bits, n, AP_CONTENT_BIT_CTR + 3));
 /* First race won and stage 2 met: shown. */
 done[doneCount++] = 10; n = AP_ContentPadGlowBits(3, bits, 32, 1);
 assert(has(bits, n, AP_CONTENT_BIT_CTR + 3) && has(bits, n, AP_CONTENT_BIT_RELIC + 9) && !has(bits, n, AP_CONTENT_BIT_TROPHY + 3));
 /* ctr_first: the Trophy Race check glows with the CTR Challenge. */
 pad.ctr_first = 1; pad.mode_count = 2; pad.modes[0] = AP_CONTENT_MODE_CTR; pad.modes[1] = AP_CONTENT_MODE_RELIC; doneCount = 0; counts[0] = 0;
 n = AP_ContentPadGlowBits(3, bits, 32, 1);
 assert(has(bits, n, AP_CONTENT_BIT_CTR + 3) && has(bits, n, AP_CONTENT_BIT_TROPHY + 3) && !has(bits, n, AP_CONTENT_BIT_RELIC + 9));
 puts("PASS: the glow hides locked races");
 return 0;
}
"""
import subprocess
import tempfile
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "glow.c", Path(tmp) / "glow"
    src.write_text(glow)
    subprocess.run(["cc", "-std=c99", "-DCTR_AP", "-Wall", "-Wextra", "-Wno-unused-function", "-fsanitize=undefined",
                    "-I", str(root / "ap"), "-I", str(root), "-I", str(root / "include"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: plan pads production text")
