"""Stage 2 line under the pad name (A4-59): the production wiring. The line's
logic runs in test-pad-stage2-line.c; this pins that the glue reads the gate's
own count and the server's checked state, and that AH_WarpPad draws the line
under the name in the A4-10 colours, in the shared A4-44 scrolling box, with
the name, racer line and portrait lifted together when two lines show.
It cannot run the engine. Run from the repository root.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
pads = (root / "ap/ap_hooks_pads.c").read_text(encoding="utf-8")
warp = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")
hooks_c = (root / "ap/ap_hooks.c").read_text(encoding="utf-8")
hooks_h = (root / "ap/ap_hooks.h").read_text(encoding="utf-8")
box = (root / "include/text_box.h").read_text(encoding="utf-8")
map_header = (root / "include/ovr_232.h").read_text(encoding="utf-8")
push_buffer = (root / "game/PushBuffer.c").read_text(encoding="utf-8")
character = (root / "ap/ap_charswap.c").read_text(encoding="utf-8")


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
assert "const RECT *padView = &gGT->pushBuffer[padPlayer].rect;" in block
assert "ap_pad_title_block_lift(apContentMissing ? 2 : (apStage2 != AP_S2LINE_NONE) + (apBoxesLeft > 0)" in block
assert "padView->h - 30 - apLift, FONT_BIG" in warp
stage2_draw = block.split("DecalFont_DrawLineBox(apStage2Line,", 1)[1].split(");", 1)[0]
assert "TEXT_MARQUEE, padTextSlot, NULL" in stage2_draw
for slot in ("TITLE", "RACER", "INSTALL"):
    assert f"padPlayer ? TEXT_SLOT_PAD_{slot}_P2 : TEXT_SLOT_PAD_{slot}" in block
assert "padPlayer ? TEXT_SLOT_PAD_P2 : TEXT_SLOT_PAD" in block
assert "padPlayer ? TEXT_SLOT_PAD_CREDIT_P2 : TEXT_SLOT_TRACK_CREDIT" in block
assert re.search(r"padX,\s+requirementY,\s+requirementWidth, FONT_SMALL", stage2_draw)
assert "int requirementWidth = padView->w - 160;" in block
assert "apStage2 == AP_S2LINE_MET ? LIGHT_GREEN" in block
assert "apStage2 == AP_S2LINE_DONE ? GRAY : RED" in block
# Stage 2 sits directly under the name; AP BOXES LEFT keeps the bottom line.
assert "const int requirementY = padBottom - 15 - apLift;" in block
assert block.count("requirementY,\n") == 2
assert block.count("padBottom - 15,\n") == 1
assert block.count("padView->h - 15,\n") == 1
assert "apMissingPad->occupied && !AP_ContentPadReady(apMissingPhys)" in block
assert "if (apBoxesLeft > 0 && !apContentMissing)" in block
# Racer line and portrait move with the name.
assert "int racerLift = apLift;" in block and "if (credit) racerLift += 11;" in block
assert "padView->h - 82 - racerLift" in block and "padBottom - 45 - racerLift" in block
assert "apCy - data.font_charPixHeight[FONT_SMALL] - 4" in block
assert "AP_FeedHubPadTitle(apPadFeedY)" in block
assert "padBottom - apLift - 30 - 11" in block and "padBottom - apLift - 23" in block
# Both branches share the retail center. Catch a later reassignment or a
# separately shifted title, credit, requirement, racer line or portrait.
assert re.findall(r"\bpadX\s*=(?!=)\s*([^;]+);", block) == ["padView->x + padView->w / 2"]
assert "DecalFont_DrawLineBox(warppadLNG, padX, titleY, titleWidth," in block
assert "UI_DrawTrackCreditInSlot(credit, padX, creditY, creditWidth," in block
assert block.count("AH_WarpPad_TextWidth(gGT, padX,") == 4
assert "requirementWidth = AH_WarpPad_TextWidth(gGT, padX, requirementY, requirementWidth);" in block
assert re.search(r'DecalFont_DrawLine\(apBoxLine,\s+padX,', block)
assert "short apCx = (short)padX;" in block
assert "centerPortrait = credit != NULL;" in block
assert re.search(r"AP_CharSwap_DrawPortraitAt\(apLockRacer,\s+apCx, apCy, !apHave, centerPortrait\);", block)
assert "DecalFont_DrawLineBox(racerLine, apCx, racerY, racerWidth," in block
# The pad and credit clocks stay outside the feed's five contiguous slots.
assert "TEXT_SLOT_PAD, TEXT_SLOT_FEED," in box
assert "TEXT_SLOT_TRACK_CREDIT = TEXT_SLOT_FEED + 5, TEXT_SLOT_PAD_TITLE," in box
# A non-AP build keeps the retail layout.
assert "#else\n\t\t\t\tint apLift = 0;\n#endif" in warp

# Run the production width policy against the wide hub map and its marker
# clearance. A retail-sized line on the bottom row fits in full. When the
# boxes row pushes it up beside the map, the marquee reaches its last glyph.
layout = body(warp, "static int AH_WarpPad_TextWidth(") + "\n}\n"
portrait = body(character, "void AP_CharSwap_DrawPortraitAt(") + "\n}\n"
map_position = re.search(r"enum \{ AH_MAP_RIGHT .*?\};", map_header).group()
viewport = re.findall(r"#define SIZEY_1P\s+0x[0-9A-Fa-f]+", push_buffer)[-1]
c = """
#include <assert.h>
#include <stddef.h>
#include <text_box_logic.h>
#define AP_CS_TILES 16
#define FONT_SMALL 2
#define JUSTIFY_CENTER 0x1000
#define ORANGE 1
#define TRANS_50_DECAL 1
#define FP(value) ((int)((value) * 4096))
struct Icon { struct { int u0, u1; } texLayout; };
struct DB { int primMem; } db;
struct GameTracker {
    struct Icon *ptrIcons[0x88];
    struct DB *backBuffer;
    struct { void *ptrOT; } pushBuffer_UI;
} game;
static struct { struct GameTracker *gGT; } state = { &game }, *sdata = &state;
static struct { struct { int iconID; } MetaDataCharacters[AP_CS_TILES]; } data;
static int portraitX, fallbackX;
static void RECTMENU_DrawPolyGT4(struct Icon *icon, int x, int y, int *primMem,
                                void *ot, unsigned a, unsigned b, unsigned c,
                                unsigned d, int transparency, int scale)
{
    portraitX = x;
}
static void DecalFont_DrawLine(char *text, int x, int y, int font, int flags)
{
    assert(flags & JUSTIFY_CENTER);
    fallbackX = x;
}
static const char *ap_cs_shortName(int characterID) { return "COCO"; }
static void ap_cs_noteMissingPortrait(int characterID, int iconID) {}
""" + viewport + "\n" + map_position + layout + portrait + """
int main(void)
{
    struct Icon top = {{12, 155}}, bottom = {{16, 116}};
    struct Icon racer = {{8, 51}};
    const int center = 256, retailWidth = 352;
    game.ptrIcons[3] = &top;
    game.ptrIcons[4] = &bottom;
    const int regularY = SIZEY_1P - 15;
    const int regular = AH_WarpPad_TextWidth(&game, center, regularY, retailWidth);
    const int raised = AH_WarpPad_TextWidth(&game, center, regularY - 8, retailWidth);
    const int goldRelics = 24 * 13 - 6; /* STAGE 2: 4/4 GOLD RELICS */
    const int longLine = 32 * 13 - 6; /* STAGE 2 DONE: 3 GREEN CTR TOKENS */
    assert(regular == retailWidth);
    assert(raised > 0 && center + raised / 2 == AH_MAP_RIGHT - 143 - 16);
    assert(TextBox_Cycle(goldRelics, regular) == 0);
    assert(TextBox_Offset(longLine, regular, 0) == 0);
    assert(TextBox_Offset(longLine, regular, TextBox_Cycle(longLine, regular) - 1)
           + regular == longLine);
    assert(TextBox_Offset(goldRelics, raised, TextBox_Cycle(goldRelics, raised) - 1)
           + raised == goldRelics);

    game.backBuffer = &db;
    game.ptrIcons[0] = &racer;
    AP_CharSwap_DrawPortraitAt(0, center, 123, 0, 1);
    assert(portraitX * 2 + 43 == center * 2 + 1);
    AP_CharSwap_DrawPortraitAt(0, center, 123, 0, 0);
    assert(portraitX == center - 16);
    game.ptrIcons[0] = NULL;
    AP_CharSwap_DrawPortraitAt(0, center, 123, 0, 1);
    assert(fallbackX == center);
    AP_CharSwap_DrawPortraitAt(0, center, 123, 0, 0);
    assert(fallbackX == center - 16);
}
"""
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "pad-layout"
    subprocess.run(["cc", "-std=c99", "-I", str(root / "include"), "-x", "c",
                    "-", "-o", str(exe)], input=c, text=True, check=True)
    subprocess.run([str(exe)], check=True)
print("pad stage 2 line production wiring: ok")
