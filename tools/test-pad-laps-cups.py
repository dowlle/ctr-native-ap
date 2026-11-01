"""One generator row 8: first lap checks in the pad glow, the tracker row that
names stage 2, and a cup that counts its tracks' boxes, Wumpa and laps. Runs the
production glow enumerator and the pure decisions, and pins the calls. It cannot
run the engine: the glow on screen, the row on the page and a cup's look need a
played room. Run from the repository root.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
pads = (root / "ap/ap_hooks_pads.c").read_text(encoding="utf-8")
hooks = (root / "ap/ap_hooks.c").read_text(encoding="utf-8")
tracker = (root / "ap/ap_tracker.c").read_text(encoding="utf-8")
launch = (root / "ap/ap_tracker_launch.c").read_text(encoding="utf-8")
race = (root / "ap/ap_hooks_race.c").read_text(encoding="utf-8")
boxes = (root / "ap/ap_boxes.c").read_text(encoding="utf-8")
menu = (root / "game/230/MM_ConfigMenu.c").read_text(encoding="utf-8")


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


# The state counts every entry the enumerator can list, the display bits resolve.
state = function(pads, "int AP_PadState(int physLevelID, int destLevelID)")
assert "int bits[6+AP_CONTENT_CUP_TRACK_CHECKS];" in state
assert "AP_PadUncollectedGlowBits(physLevelID,bits,6+AP_CONTENT_CUP_TRACK_CHECKS) ? 2 : 5" in state
assert "ap_content_cup_lap_bit_code(ap_content_plan_pad(contentPad), contentTier)" in hooks
assert "if(ap_content_cup_lap_bit_decode(globalBit,&contentPad,&contentTier))\n            return 0;" in pads
# The tracker names stage 2 and a cup's node counts its tracks.
assert "ap_content_requirement_text(pad->stage2_mask, pad->stage2_count, out->stage2, sizeof out->stage2);" in launch
node = function(tracker, "static void AP_TrackerDrawContentNode(")
assert "ap_content_cup_track_checks(pad, ap_content_plan_pad, rows)" in node and "ap_content_cup_box_pending(" in node
# A plan cup leg earns its track's Wumpa check; a leg's box gate reads the pad on that track.
wumpa = function(race, "static long AP_StandaloneWumpaCode(void)")
assert "cup->legs[leg].track_pad" in wumpa and "context && context->cup" in wumpa
policy = function(boxes, "static int AP_BoxesPadRoutePolicy(")
assert "pad->retail_id == level" in policy
assert "cup->legs[race->leg].track_pad" in policy and "return AP_BOX_PRESENT_NONE;" in policy
seat = function(boxes, "static void AP_BoxesSeatCustom(")
assert "pad->legs[race->leg].track_pad" in seat
footer = tracker[tracker.index("if(ap_tracker.pinned && ap_tracker.count) {"):]
assert "ap_content_cup_box_pending(plan,ap_content_plan_pad,AP_ContentCodeChecked)" in footer and "int bits[96]" in footer
# Every cup pad knows its legs' track pads, boss cups too: 0 would be a real pad.
parser = (root / "ap/ap_content_plan_impl.hpp").read_text(encoding="utf-8")
assert "for (auto &cup : out.pads) seat_track_pads(cup);" in parser and "for (auto &cup : out.garage_cups) seat_track_pads(cup);" in parser
# A status cut on a very short word still ends in dots.
assert 'memcpy(line + (end >= 3 ? end - 3 : end), "...", 4);' in menu

fixture = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_content_modes.h"
#include "ap_pad_activity_logic.h"
static ctr_content_pad track_a, track_b, cup;
static int counts[15];
static int64_t done[64]; static int doneCount;
const ctr_content_pad *ap_content_plan_pad(int physical)
{
    return physical == 3 ? &track_a : physical == 5 ? &track_b : physical == 100 ? &cup : NULL;
}
int ap_net_location_checked(long code) { for (int i = 0; i < doneCount; i++) if (done[i] == code) return 1; return 0; }
static int checkedFn(int64_t code) { return ap_net_location_checked((long)code); }
int AP_ContentModeOpen(const ctr_content_pad *p, int mode) { return ap_content_mode_open(p, mode, counts, checkedFn); }
static int shut_b;
int AP_ContentPadOpen(const ctr_content_pad *p) { return !(shut_b && p == &track_b); }
""" + function(pads, "static int AP_ContentPadGlowBits(") + r"""
static int has(const int *bits, int n, int bit) { for (int i = 0; i < n; i++) if (bits[i] == bit) return 1; return 0; }
static void track(ctr_content_pad *p, int physical, int64_t base)
{
    memset(p, 0, sizeof *p);
    p->physical = physical; p->occupied = 1; p->first_race_rule = 1; p->mode_count = 3; p->racer_lock = -1;
    p->modes[0] = AP_CONTENT_MODE_TROPHY; p->modes[1] = AP_CONTENT_MODE_CTR; p->modes[2] = AP_CONTENT_MODE_RELIC;
    p->stages[0] = 1; p->stages[1] = p->stages[2] = 2; p->has_stage2 = 1; p->stage2_mask = 1; p->stage2_count = 1;
    p->trophy_location = base; p->ctr_location = base + 1;
    p->lap_location = base + 2; p->wumpa_location = base + 3; p->wumpa_modes = 3;
    p->box_count = 2; p->box_locations[0] = base + 4; p->box_locations[1] = base + 5;
}
static int count_of(int want_bit, const int *bits, int n) { int c = 0; for (int i = 0; i < n; i++) c += bits[i] == want_bit; return c; }
#define LAP_BIT(pad) (AP_CONTENT_BIT_FAMILY + (pad) * AP_CONTENT_FAMILY_STRIDE + AP_CONTENT_FAMILY_LAP)
#define WUMPA_BIT(pad) (AP_CONTENT_BIT_FAMILY + (pad) * AP_CONTENT_FAMILY_STRIDE + 5)
#define BOX_BIT(pad, b) (AP_CONTENT_BIT_BOX + (pad) * AP_CONTENT_BOX_STRIDE + (b))
#define CUP_LAP_BIT(leg) (AP_CONTENT_BIT_CUP_LAP + 100 * AP_CONTENT_CUP_LAP_STRIDE + (leg))
int main(void)
{
    int bits[96], n, physical, slot;
    char text[64], lines[2][112];
    track(&track_a, 3, 100); track(&track_b, 5, 200);
    /* Item 2: the first lap shows once, with the first race, and rides the race slot. */
    n = AP_ContentPadGlowBits(3, bits, 96, 1);
    assert(count_of(LAP_BIT(3), bits, n) == 1 && has(bits, n, AP_CONTENT_BIT_TROPHY + 3));
    assert(!has(bits, n, AP_CONTENT_BIT_CTR + 3));
    n = AP_ContentPadGlowBits(3, bits, 96, 0);
    assert(count_of(LAP_BIT(3), bits, n) == 1);
    assert(ap_content_family_bit_decode(LAP_BIT(3), &physical, &slot) && physical == 3 && slot == AP_CONTENT_FAMILY_LAP);
    assert(ap_content_family_bit_code(&track_a, slot) == 102 && ap_content_family_reward_group(slot) == 0);
    { int64_t codes[AP_CONTENT_FAMILY_SLOTS]; int slots[AP_CONTENT_FAMILY_SLOTS];
      assert(ap_content_mode_family_codes(&track_a, AP_CONTENT_MODE_RELIC, codes, slots) == 1 && codes[0] == 102); }
    /* A checked lap leaves the glow. */
    done[doneCount++] = 102;
    n = AP_ContentPadGlowBits(3, bits, 96, 0);
    assert(!has(bits, n, LAP_BIT(3)));
    doneCount = 0;
    /* Item 4: a cup lists each of its tracks once. */
    memset(&cup, 0, sizeof cup);
    cup.physical = 100; cup.occupied = 1; cup.cup = 1; cup.mode_count = 0; cup.racer_lock = -1; cup.trophy_location = 300;
    strcpy(cup.legs[0].track_id, "retail:1"); cup.legs[0].track_pad = 3; cup.legs[0].lap = 102;
    strcpy(cup.legs[1].track_id, "retail:1"); cup.legs[1].track_pad = 3; cup.legs[1].lap = 102;
    strcpy(cup.legs[2].track_id, "retail:2"); cup.legs[2].track_pad = 5; cup.legs[2].lap = 202;
    strcpy(cup.legs[3].track_id, "retail:3"); cup.legs[3].track_pad = -1; cup.legs[3].lap = 302;
    n = AP_ContentPadGlowBits(100, bits, 96, 0);
    assert(n == 1 + 3 + 2 + 4);
    assert(has(bits, n, CUP_LAP_BIT(0)) && !has(bits, n, CUP_LAP_BIT(1)) && has(bits, n, CUP_LAP_BIT(2)) && has(bits, n, CUP_LAP_BIT(3)));
    assert(has(bits, n, WUMPA_BIT(3)) && has(bits, n, WUMPA_BIT(5)) && has(bits, n, BOX_BIT(3, 1)) && has(bits, n, BOX_BIT(5, 0)));
    assert(ap_content_cup_lap_bit_decode(CUP_LAP_BIT(3), &physical, &slot) && physical == 100 && slot == 3);
    assert(ap_content_cup_lap_bit_code(&cup, 3) == 302 && ap_content_cup_lap_bit_code(&track_a, 3) == -1);
    assert(ap_content_box_bit_code(ap_content_plan_pad(5), 0) == 204);
    assert(ap_content_family_bit_code(ap_content_plan_pad(5), 5) == 203);
    /* The pad state counts a shut pad's boxes; the glow keeps them hidden. */
    shut_b = 1;
    n = AP_ContentPadGlowBits(100, bits, 96, 1);
    assert(!has(bits, n, BOX_BIT(5, 0)) && has(bits, n, BOX_BIT(3, 0)) && has(bits, n, WUMPA_BIT(5)));
    n = AP_ContentPadGlowBits(100, bits, 96, 0);
    assert(has(bits, n, BOX_BIT(5, 0)));
    shut_b = 0;
    /* A cup with only a leg's box left still has a check: no grey X. */
    { int64_t all[] = {300, 102, 202, 302, 103, 203, 104, 105, 205};
      for (unsigned i = 0; i < sizeof all / sizeof all[0]; ++i) done[doneCount++] = all[i]; }
    assert(AP_ContentPadGlowBits(100, bits, 96, 0) == 1);
    assert(ap_content_cup_box_pending(&cup, ap_content_plan_pad, checkedFn) == 1);
    done[doneCount++] = 204;
    assert(AP_ContentPadGlowBits(100, bits, 96, 0) == 0);
    assert(ap_content_cup_box_pending(&cup, ap_content_plan_pad, checkedFn) == 0);
    doneCount = 0;
    /* The worst cup fits the state's list and the glow. */
    assert(AP_CONTENT_CUP_TRACK_CHECKS + 6 <= 96);
    /* Item 3: the row names stage 2. */
    { static const struct { unsigned mask; int count; const char *want; } cases[] = {
          {1u << 11, 2, "2 Blue Gems"}, {1u << 9, 2, "2 Red Gems"}, {1u << 9, 1, "1 Red Gem"},
          {1u << 14, 1, "1 Key"}, {1u << 14, 3, "3 Keys"}, {1u, 1, "1 Trophy"}, {1u, 4, "4 Trophies"},
          {1u << 1, 5, "5 Sapphire Relics"}, {1u << 6, 2, "2 Blue CTR Tokens"},
          {0x000eu, 5, "5 Any Relics"}, {0x01f0u, 2, "2 Any CTR Tokens"}, {0x3e00u, 3, "3 Any Gems"},
          {0x0201u, 2, "2 Items"}};
      for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
          ap_content_requirement_text(cases[i].mask, cases[i].count, text, sizeof text);
          assert(!strcmp(text, cases[i].want));
      }
      /* The longest name fits the list's field. */
      assert(ap_content_requirement_text(1u << 7, 20, text, sizeof text) < (int)sizeof ((ap_padact_list *)0)->stage2); }
    assert(AP_PadActivityReasonLines(AP_PADACT_BIT(AP_PADACT_LOCK_STAGE2), 34, "2 Red Gems", lines) == 1);
    assert(!strcmp(lines[0], "2 Red Gems"));
    assert(AP_PadActivityReasonLines(AP_PADACT_BIT(AP_PADACT_LOCK_STAGE2), 34, "", lines) == 1);
    assert(!strcmp(lines[0], "SECOND REQUIREMENT"));
    assert(AP_PadActivityReasonLines(AP_PADACT_BIT(AP_PADACT_LOCK_TROPHY) | AP_PADACT_BIT(AP_PADACT_LOCK_STAGE2), 34, "2 Red Gems", lines) == 1);
    assert(!strcmp(lines[0], "WIN TROPHY RACE FIRST, 2 Red Gems"));
    puts("First lap glow, cup track checks and the stage 2 row passed");
    return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(fixture)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-fsanitize=undefined", "-DCTR_AP", "-I", str(root / "ap"), "-I", str(root),
                    "-I", str(root / "include"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
