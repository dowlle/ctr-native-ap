"""Stage 2 line under the pad name (A4-59): the production wiring. The line's
logic runs in test-pad-stage2-line.c; this pins that the glue reads the gate's
own count and the server's checked state, and that AH_WarpPad draws the line
under the name in the A4-10 colours, in the shared A4-44 scrolling box, with
the name, racer line and portrait lifted together when two lines show.
It cannot run the engine. Run from the repository root.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
pads = (root / "ap/ap_hooks_pads.c").read_text(encoding="utf-8")
warp = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")
hooks_c = (root / "ap/ap_hooks.c").read_text(encoding="utf-8")
hooks_h = (root / "ap/ap_hooks.h").read_text(encoding="utf-8")
box = (root / "include/text_box.h").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at)]


assert '#include "ap_pad_stage2_line.h"' in hooks_c
assert "int AP_PadStage2Line(int levelID, char *out, int cap);" in hooks_h

glue = body(pads, "int AP_PadStage2Line(int levelID, char *out, int cap)")
# Only on a plan room; the gate's own count and the server checked state.
assert "ctr_cfg_active()" in glue and "ap_content_plan_active()" in glue
assert "AP_ContentStage2Owned(pad)" in glue
assert "ap_pad_stage2_left(pad, AP_ContentCodeChecked)" in glue
assert "AP_PadState(ctr_cfg_warp_phys(levelID), levelID)" in glue
assert "ap_pad_stage2_line_text(pad, state, owned, out, (size_t)cap)" in glue
# Display only: the glue changes no state.
for word in ("ap_net_send", "AP_StateGenBump", "ctr_cfg." ):
    assert word not in glue, word

# The drawing site: inside the Aku-silent title block, computed before the name.
title = warp.index("AP_PadStage2Line(levelID, apStage2Line, (int)sizeof apStage2Line)")
name = warp.index("DecalFont_DrawLine(warppadLNG")
assert title < name
block = warp[title:warp.index("// if track is unlocked, ignore all other ELSE-IFs", title)]
assert "ap_pad_title_block_lift((apStage2 != AP_S2LINE_NONE) + (apBoxesLeft > 0)" in block
assert "rect.h - 30 - apLift, FONT_BIG" in warp
assert "TEXT_MARQUEE, TEXT_SLOT_PAD, NULL" in block
assert "apStage2 == AP_S2LINE_MET ? LIGHT_GREEN" in block
assert "apStage2 == AP_S2LINE_DONE ? GRAY : RED" in block
# Stage 2 sits directly under the name; AP BOXES LEFT keeps the bottom line.
assert "rect.h - 15 - apLift,\n" in block
assert block.count("rect.h - 15,\n") == 1
# Racer line and portrait move with the name.
assert "rect.h - 82 - apLift" in block and "rect.h - 45 - apLift" in block
# The text slot exists and keeps the feed's five slots last.
assert "TEXT_SLOT_PAD, TEXT_SLOT_FEED," in box
assert "TEXT_SLOT_COUNT = TEXT_SLOT_FEED + 5" in box
# A non-AP build keeps the retail layout.
assert "#else\n\t\t\t\tint apLift = 0;\n#endif" in warp
print("pad stage 2 line production wiring: ok")
