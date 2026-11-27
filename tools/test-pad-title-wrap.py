"""CN1: the whole custom track name is drawn at once on the warp pad.

The pad used to draw a custom track name in a clipped scrolling window, so a long name
('NINA'S NIGHTMARE') lost its first and last letters. ap/ap_pad_title_wrap.h now wraps the name
at word boundaries (or uses the small font for a very long name) and AH_WarpPad.c draws every
line. This harness runs the production layout and the production font width routine on the
whole roster of custom track names the repository knows (the named-block registry, the
offline identity snapshot, the xdelta names and the Saphi and patch source tables), the
clash-suffix forms the apworld builds (custom_track_titles.py) and the longest room title it
allows (105 bytes), and checks for each name that:

  * every line is no wider than the box and nothing is cut (no overflow),
  * the lines hold every letter of the name in order (the name is visible in full),
  * the block sits inside the box beside the hub map and clear of its drawn rectangle,
  * a name that fits keeps its single line and its old height (no lift),
  * the lines above the name lift by exactly the height the wrapped block adds.

It also pins the wiring in AH_WarpPad.c: the credit keeps its scroll, the retail name keeps the
old single line, the racer line and the feed move with the name. Run from the repository root.
"""
from pathlib import Path
import json
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def source(path):
    return (root / path).read_text(encoding="utf-8")


def function(path, signature):
    text = source(path)
    start = text.index(signature + "\n{")
    return text[start:text.index("\n}\n", start) + 3]


def table(name):
    body = re.search(r"\." + name + r"\s*=\s*\{(.*?)\}", source("game/zGlobal_DATA.c"), re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    return [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|-?\d+", body)]


def fold_banner(name):
    """UI_TrackName: ASCII fold (accents dropped) and upper case, as the pad shows it."""
    import unicodedata
    text = unicodedata.normalize("NFKD", name)
    text = "".join(c for c in text if not unicodedata.combining(c))
    text = text.replace("’", "'").replace("‘", "'")
    return "".join(c if 0x20 <= ord(c) < 0x7F else "?" for c in text.upper())


# ---- roster -------------------------------------------------------------------------------
names = set()
registry = re.search(r'R"CTRREG\((.*)\)CTRREG"', source("ap/ap_content_registry.hpp"), re.S).group(1)
for row in json.loads(registry)["named_blocks"]:
    names.add(row["name"])
for m in re.finditer(r'\{"[a-z0-9-]+", "([^"]+)"\}', source("platform/native_track_identity_data.hpp")):
    names.add(m.group(1))
for m in re.finditer(r'\{"[0-9a-f]{64}", "([^"]+)"\}', source("platform/native_xdelta_names.hpp")):
    names.add(m.group(1))
for m in re.finditer(r'TS_(?:SAPHI|PATCH)\("[0-9a-f]{64}", "[0-9a-f]{64}", "([^"]+)"', source("include/platform/native_track_sources.inc")):
    names.add(m.group(1))
roster = sorted({fold_banner(n) for n in names if n.strip()})
assert len(roster) > 120, len(roster)
assert "NINA'S NIGHTMARE" in roster
# The live registry (fetched 2026-11-26) is newer than the frozen snapshot: its longest titles.
roster = sorted(set(roster) | {"ST PATRICKS DAY HUB WORLD", "LEGO ALIEN RALLY ASTEROID"})

# The apworld builds generic-slot titles as `<title (40 chars)> (Custom, <author (24 chars)>)`.
author = "A" * 24
longest = sorted(roster, key=len, reverse=True)[:8]
suffixed = [fold_banner(f"{n[:40]} (Custom, {author})") for n in longest]
suffixed += ["NINA'S NIGHTMARE (CUSTOM, LOCKHEART)", "NINA'S NIGHTMARE (CUSTOM)", "NINA'S NIGHTMARE (CUSTOM, APPIE 2)"]
# ROOM_TITLE_MAX_BYTES: a 105 byte room title, with the widest glyphs and with many short words.
worst = [("AMAZING " * 13).strip(), "A " + "ABCDEFGHIJKLMNOPQ " * 5 + "A", "STROODLYDOODLES CUP", "YELLOW GEM CUP",
         "W" * 105, ("MM " * 35).strip(), "ABCDEFGHIJKLMNOPQRSTUVWXYZ" * 4,
         "X" * 40 + " " + "Y" * 40, ("A " * 53)[:105].strip()]
for t in suffixed + worst:
    assert len(t) <= 105, t

# ---- the production font and width routines -------------------------------------------------
macros = source("include/macros.h")
screen_width = int(re.search(r"#define SCREEN_WIDTH\s+(\d+)", macros).group(1))
assert screen_width == 512
map_header = source("include/ovr_232.h")
map_position = re.search(r"enum \{ AH_MAP_RIGHT .*?\};", map_header).group()
heights = table("font_charPixHeight")
assert heights[1] == 17 and heights[2] == 8
width_fn = function("game/232/AH_WarpPad.c", "static int AH_WarpPad_TextWidth(const struct GameTracker *gGT, int center, int y, int width)")

fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef short s16; typedef unsigned char u8; typedef unsigned int u32;
#define UsaRetail 926
#define EurRetail 1020
#define JpnRetail 1111
#define JpnTrial 1006
#define BUILD UsaRetail
#define FONT_BIG 1
#define FONT_SMALL 2
static struct { short font_charPixWidth[4], font_buttonPixWidth[4], font_puncPixWidth[4], font_charPixHeight[4]; } data = {
    {%s}, {%s}, {%s}, {%s} };
struct Icon { struct { int u0, u1; } texLayout; };
struct GameTracker { struct Icon *ptrIcons[0x88]; };
%s
''' % (", ".join(map(str, table("font_charPixWidth"))), ", ".join(map(str, table("font_buttonPixWidth"))),
       ", ".join(map(str, table("font_puncPixWidth"))), ", ".join(map(str, heights)), map_position)
fixture += function("game/DecalFont.c", "int DecalFont_GetLineWidthStrlen(char *character, int len, int fontType)")
fixture += width_fn
fixture += '\n#include "ap/ap_pad_title_wrap.h"\n'
fixture += r'''
static int measure(const char *text, int len, int font) { return DecalFont_GetLineWidthStrlen((char *)text, len, font); }

/* Stand at the layout the pad uses: centre 256 (4:3 and 16:9 both draw the HUD in 512 px), the name
   on the credited block's line (viewport bottom 216 - 30 - 11), the hub map as wide as it gets. */
static int check(const char *name, int boxWidth, int centre, int mapRight, int mapWidth, int *linesOut, int *fontOut)
{
    ap_pad_title_layout l;
    ap_pad_title_layout_for(name, boxWidth, FONT_BIG, FONT_SMALL, measure, &l);
    assert(!l.overflow);
    assert(l.count >= 1 && l.count <= AP_PAD_TITLE_MAX_LINES);
    /* every letter, in order: the lines concatenated without spaces equal the name without spaces */
    char joined[1024] = "", plain[1024] = "";
    for (int i = 0; i < l.count; i++) {
        assert(l.len[i] > 0);
        assert(l.start[i] >= 0 && l.start[i] + l.len[i] <= (int)strlen(name));
        assert(name[l.start[i]] != ' ' && name[l.start[i] + l.len[i] - 1] != ' ');
        assert(measure(name + l.start[i], l.len[i], l.font) <= boxWidth);
        strncat(joined, name + l.start[i], (size_t)l.len[i]);
        if (i) assert(l.start[i] >= l.start[i - 1] + l.len[i - 1]);
    }
    for (const char *c = name; *c; c++) if (*c != ' ') strncat(plain, c, 1);
    char nojoin[1024] = "";
    for (const char *c = joined; *c; c++) if (*c != ' ') strncat(nojoin, c, 1);
    assert(!strcmp(plain, nojoin));
    /* beside the hub map: the box is centred on `centre`, right edge at least 16 px left of the map */
    int right = centre + boxWidth / 2;
    assert(right <= mapRight - mapWidth - 16);
    assert(centre - boxWidth / 2 >= 8);
    /* a fitting name stays one big line with no lift */
    int h = ap_pad_title_block_height(&l, data.font_charPixHeight[l.font]);
    int lift = ap_pad_title_extra_lift(&l, data.font_charPixHeight[l.font], data.font_charPixHeight[FONT_BIG]);
    if (measure(name, (int)strlen(name), FONT_BIG) <= boxWidth) { assert(l.count == 1 && l.font == FONT_BIG && lift == 0); }
    else assert(l.count >= 2 && lift > 0);
    assert(h - lift == data.font_charPixHeight[FONT_BIG] || lift == 0);
    *linesOut = l.count; *fontOut = l.font;
    return lift;
}

int main(int argc, char **argv)
{
    (void)argc;
    /* argv: file with one name per line, then the report is printed */
    struct Icon top = {{12, 155}}, bottom = {{16, 116}}; /* the widest hub map in the stage 2 harness: 143 px */
    struct GameTracker game = {0};
    game.ptrIcons[3] = &top; game.ptrIcons[4] = &bottom;
    const int centre = 256, requirementWidth = 352, titleY = 216 - 30 - 11;
    const int box = AH_WarpPad_TextWidth(&game, centre, titleY, requirementWidth);
    assert(box > 0 && box < requirementWidth);
    /* a narrower map leaves a wider box, a wider one a narrower box; check both ends */
    struct Icon narrowTop = {{0, 100}}, narrowBottom = {{0, 100}};
    struct GameTracker narrow = {0};
    narrow.ptrIcons[3] = &narrowTop; narrow.ptrIcons[4] = &narrowBottom;
    const int boxNarrow = AH_WarpPad_TextWidth(&narrow, centre, titleY, requirementWidth);
    assert(boxNarrow > box);
    /* below the map the full width is kept (the retail single line has no limit either) */
    assert(AH_WarpPad_TextWidth(&game, centre, AH_MAP_BOTTOM + 5, requirementWidth) == requirementWidth);
    printf("box %d px (map 143 wide), %d px (map 100 wide), centre %d\n", box, boxNarrow, centre);

    FILE *f = fopen(argv[1], "r");
    char line[512];
    int n = 0, maxLift = 0, maxLines = 0, small = 0, wrapped = 0, single = 0;
    char widest[512] = "";
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        int lines, font, a;
        check(line, boxNarrow, centre, AH_MAP_RIGHT, 100, &lines, &font);
        check(line, 236, centre, AH_MAP_RIGHT, 110, &lines, &font); /* the box measured in the hub */
        check(line, 274, centre, AH_MAP_RIGHT, 91, &lines, &font);  /* the map icons as the client logs them: 91 px */
        a = check(line, box, centre, AH_MAP_RIGHT, 143, &lines, &font);
        n++;
        if (lines == 1) single++; else wrapped++;
        if (font == FONT_SMALL) small++;
        if (a > maxLift) { maxLift = a; strcpy(widest, line); }
        if (lines > maxLines) maxLines = lines;
    }
    printf("%d names: %d single line, %d wrapped (%d in the small font), at most %d lines, largest lift %d px (%s)\n",
           n, single, wrapped, small, maxLines, maxLift, widest);
    /* the nominal cases */
    ap_pad_title_layout l;
    ap_pad_title_layout_for("NINA'S NIGHTMARE", box, FONT_BIG, FONT_SMALL, measure, &l);
    printf("NINA'S NIGHTMARE at %d px: %d line(s), font %d\n", box, l.count, l.font);
    ap_pad_title_layout_for("SHORT", box, FONT_BIG, FONT_SMALL, measure, &l);
    assert(l.count == 1 && l.font == FONT_BIG && ap_pad_title_extra_lift(&l, 17, 17) == 0);
    /* a name wider than the box with one long word is cut between letters, never clipped */
    ap_pad_title_layout_for("SUPERCALIFRAGILISTICEXPIALIDOCIOUS", box, FONT_BIG, FONT_SMALL, measure, &l);
    assert(!l.overflow && l.count >= 2);
    /* the box can never be wider than the box handed in, even when it is tiny: it reports overflow */
    ap_pad_title_layout_for("ABCDEFGH", 9, FONT_BIG, FONT_SMALL, measure, &l);
    assert(l.overflow);
    /* balancing never cuts a word that fits the box */
    ap_pad_title_layout_for("STROODLYDOODLES CUP", 274, FONT_BIG, FONT_SMALL, measure, &l);
    assert(l.count == 2 && l.start[0] == 0 && l.len[0] == 15);
    /* empty input leaves defined offsets */
    ap_pad_title_layout_for("", box, FONT_BIG, FONT_SMALL, measure, &l);
    assert(l.count == 1 && l.start[0] == 0 && l.len[0] == 0);
    puts("PASS: every custom track name is drawn in full inside the box beside the hub map");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / "names.txt").write_text("\n".join(roster + suffixed + worst) + "\n", encoding="utf-8")
    (tmp / "pad.c").write_text(fixture, encoding="utf-8")
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-I", str(root),
                    "-I", str(root / "include"), "-o", str(tmp / "pad"), str(tmp / "pad.c")], check=True)
    out = subprocess.run([str(tmp / "pad"), str(tmp / "names.txt")], check=True, capture_output=True, text=True).stdout
print(out, end="")
print("roster: %d names (%d registry, identity, xdelta and source tables), %d suffixed, %d worst case" % (len(roster), len(roster), len(suffixed), len(worst)))

# ---- wiring in AH_WarpPad.c ---------------------------------------------------------------
warp = source("game/232/AH_WarpPad.c")
header = source("ap/ap_pad_title_wrap.h")
assert '#include "../../ap/ap_pad_title_wrap.h"' in warp
draw = function("game/232/AH_WarpPad.c", "static int AH_WarpPad_DrawCustomTitle(const char *name, int x, int bottomY, int width)")
assert "ap_pad_title_layout_for(name, width, FONT_BIG, FONT_SMALL," in draw
assert "TEXT_ELLIPSIS" in draw and "TEXT_MARQUEE" not in draw, "the name never scrolls"
assert "JUSTIFY_CENTER | ORANGE" in draw
custom = function("game/232/AH_WarpPad.c", "static int AH_WarpPad_CustomPadTitle(int levelID)")
assert "ap_content_pad_custom_title(ap_content_plan_pad(levelID))" in custom
predicate = function("ap/ap_content_plan.h", "static inline int ap_content_pad_custom_title(const struct ctr_content_pad *pad)")
assert "pad->cup ? !pad->gem_cup : pad->retail_id < 0" in predicate, "cups are classified first; Gem Cups keep the retail line"
site = warp[warp.index("if (credit || AH_WarpPad_CustomPadTitle(levelID))"):warp.index("// midpoint X,")]
assert "UI_DrawTrackCredit(credit, padX, creditY, creditWidth, TEXT_MARQUEE);" in site, "the credit keeps its scroll"
assert "AH_WarpPad_DrawCustomTitle(warppadLNG, padX, titleY, titleWidth)" in site
assert "padBottom - apLift - 30 - (credit ? 11 : 0)" in site
assert "requirementWidth = AH_WarpPad_TextWidth(gGT, padX, requirementY, requirementWidth);" in site
assert "apPadFeedY" in site and "apTitleExtra" in site
# Retail and no-name pads keep the retail line exactly.
tail = warp[warp.index("// midpoint X,"):]
assert "DecalFont_DrawLine(warppadLNG, padX," in tail[:400] and "- 30 - apLift, FONT_BIG, (JUSTIFY_CENTER | ORANGE))" in tail[:400]
assert "racerLift += apTitleExtra;" in warp
assert "TEXT_SLOT_PAD_TITLE" in draw
print("pad title wiring: ok")

# CN1b: a lifted hub feed never stacks a row above the content-warning line.
feed = source("ap/ap_hooks_feed.c")
floor = feed[feed.index("// CN1b: a tall custom pad title"):feed.index("// Promote a pending line into a free slot")]
assert "warningY = AP_FEED_CONTENT_WARNING_Y;" in floor and "if (rows < visibleLimit) visibleLimit = rows;" in floor
print("hub feed floor: ok")

# ---- CN2: the same whole-name layout on the fly-in and the race start banner -----------------
# The production draw routines (UI_DrawRaceTitleUp / UI_DrawRaceTitleDown in UI_TrackName.c) run
# on the host with the production font widths; DecalFont_DrawLineBox is a recorder. Screen
# geometry, in the 512 px wide HUD space both 4:3 and 16:9 draw in: the fly-in box is the view
# width minus 64 (448) and its bar is 30 px; the banner box is 384 (1P) or 320 (2P), its name
# starts at y 76 below the countdown lights (they end at y 64) and the minimap reaches up to
# y 106 on the right.
race_fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef short s16; typedef unsigned char u8; typedef unsigned int u32;
#define UsaRetail 926
#define EurRetail 1020
#define JpnRetail 1111
#define JpnTrial 1006
#define BUILD UsaRetail
#define FONT_BIG 1
#define FONT_SMALL 2
#define JUSTIFY_CENTER 0x1000
#define ORANGE 0x2
enum { TEXT_ELLIPSIS, TEXT_MARQUEE, TEXT_BANNER_MARQUEE };
enum { TEXT_SLOT_RACE_TITLE = 11 };
static struct { short font_charPixWidth[4], font_buttonPixWidth[4], font_puncPixWidth[4], font_charPixHeight[4]; } data = {
    {%s}, {%s}, {%s}, {%s} };
static struct { char text[700]; int x, y, w, font, mode, slot; } rec[16];
static int nrec;
void DecalFont_DrawLineBox(const char *text, int x, int y, int width, short font, int flags, int mode, int slot, unsigned int *extra)
{
    (void)flags; (void)extra;
    assert(nrec < 16);
    strncpy(rec[nrec].text, text, 699); rec[nrec].text[699] = 0;
    rec[nrec].x = x; rec[nrec].y = y; rec[nrec].w = width; rec[nrec].font = font; rec[nrec].mode = mode; rec[nrec].slot = slot;
    nrec++;
}
''' % (", ".join(map(str, table("font_charPixWidth"))), ", ".join(map(str, table("font_buttonPixWidth"))),
       ", ".join(map(str, table("font_puncPixWidth"))), ", ".join(map(str, heights)))
race_fixture += function("game/DecalFont.c", "int DecalFont_GetLineWidthStrlen(char *character, int len, int fontType)")
race_fixture += '\n#include "ap/ap_pad_title_wrap.h"\n'
for sig in ("static int UI_RaceTitleWidth(const char *text, int len, int font)",
            "static void UI_RaceTitleDrawLines(const char *name, const ap_pad_title_layout *layout, int x, int top, int width)",
            "int UI_DrawRaceTitleUp(const char *name, int x, int bottomY, int width)",
            "int UI_DrawRaceTitleDown(const char *name, int x, int topY, int width)"):
    race_fixture += function("game/UI/UI_TrackName.c", sig) + "\n"
race_fixture += r'''
static int measure(const char *text, int len, int font) { return DecalFont_GetLineWidthStrlen((char *)text, len, font); }

/* every letter once, in order, no line wider than `width`, no cut word that fits, lines in reading order */
static void whole(const char *name, int width)
{
    char plain[1024] = "", joined[1024] = "";
    for (const char *c = name; *c; c++) if (*c != ' ') strncat(plain, c, 1);
    int prevY = -1000;
    for (int i = 0; i < nrec; i++) {
        assert(rec[i].text[0]);
        assert(rec[i].mode == TEXT_ELLIPSIS && rec[i].slot == TEXT_SLOT_RACE_TITLE && rec[i].w == width);
        assert(measure(rec[i].text, (int)strlen(rec[i].text), rec[i].font) <= width);
        assert(rec[i].font == rec[0].font && rec[i].y > prevY);
        prevY = rec[i].y;
        for (const char *c = rec[i].text; *c; c++) if (*c != ' ') strncat(joined, c, 1);
    }
    assert(!strcmp(plain, joined));
}

static int tall; /* names that need more than three lines on the banner */

int main(int argc, char **argv)
{
    (void)argc;
    FILE *f = fopen(argv[1], "r");
    const int real = atoi(argv[2]); /* the first `real` names are roster and suffixed forms, the rest synthetic worst cases */
    char line[512];
    const int bigH = data.font_charPixHeight[FONT_BIG], smallH = data.font_charPixHeight[FONT_SMALL];
    int n = 0, overReal = 0, flyWrapped = 0, flySmall = 0, bannerSmall = 0, bannerOne = 0, maxFly = 0, maxBannerBottom = 0, over3 = 0;
    char widest[512] = "", tallest[512] = "";
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        const int fits448 = measure(line, (int)strlen(line), FONT_BIG) <= 448;
        n++;
        /* fly-in: box 448 (credited and uncredited alike), the name ends where the single line stood */
        for (int credited = 0; credited < 2; credited++) {
            const int bottomY = 216 - (credited ? 29 : 23);
            nrec = 0;
            const int lift = UI_DrawRaceTitleUp(line, 256, bottomY, 448);
            whole(line, 448);
            const int glyphH = data.font_charPixHeight[rec[0].font];
            assert(rec[nrec - 1].y == bottomY + (bigH - glyphH)); /* the last line keeps the old position */
            assert(rec[0].y >= 40);                               /* below the 30 px top bar */
            if (fits448) {
                assert(nrec == 1 && rec[0].font == FONT_BIG && lift == 0 && rec[0].y == bottomY && rec[0].x == 256);
            } else {
                assert(lift > 0);
                assert(lift == (nrec - 1) * (glyphH + 2) + glyphH - bigH || (nrec - 1) * (glyphH + 2) + glyphH <= bigH);
            }
            if (!credited) {
                if (nrec > 1) flyWrapped++;
                if (rec[0].font == FONT_SMALL) flySmall++;
                if (lift > maxFly) { maxFly = lift; strcpy(widest, line); }
            }
        }
        /* race start banner: 1P box 384, 2P box 320 */
        for (int two = 0; two < 2; two++) {
            const int width = two ? 320 : 384, x = two ? 160 : 256;
            const int fitsOne = measure(line, (int)strlen(line), FONT_BIG) <= width;
            nrec = 0;
            const int lift = UI_DrawRaceTitleDown(line, x, 76, width);
            whole(line, width);
            assert(rec[0].y == 76 - 0 + (rec[0].font == FONT_BIG ? 0 : 0));
            if (fitsOne) {
                assert(nrec == 1 && rec[0].font == FONT_BIG && lift == 0 && rec[0].x == x);
            } else {
                assert(rec[0].font == FONT_SMALL && lift >= 0);
            }
            const int glyphH = data.font_charPixHeight[rec[0].font];
            const int bottom = 76 + (nrec - 1) * (glyphH + 2) + glyphH;
            if (!two) {
                if (fitsOne) bannerOne++; else bannerSmall++;
                if (nrec > 3 && n <= real) overReal++;
                if (nrec > 3) { over3++; if (nrec > tall) { tall = nrec; strcpy(tallest, line); } }
                else if (bottom > maxBannerBottom) maxBannerBottom = bottom;
                /* up to three lines end above the minimap (y 106); the credit then follows at 94 + lift */
                if (nrec <= 3) { assert(bottom <= 104); assert(94 + lift + smallH <= 114); }
                /* the credit narrows to 198 once it would reach the minimap, so its right edge stays 16 px left of x 371 */
                if (lift >= 5) assert(256 + 198 / 2 <= 371 - 16);
            }
        }
    }
    printf("race title: %d names. Fly-in (box 448): %d wrapped (%d in the small font), tallest lift %d px (%s).\n",
           n, flyWrapped, flySmall, maxFly, widest);
    printf("race title: banner (box 384): %d one big line, %d small font; %d need more than 3 lines (at most %d: %s); three lines or fewer end at y <= %d\n",
           bannerOne, bannerSmall, over3, tall, tallest, maxBannerBottom);
    /* the nominal cases */
    nrec = 0; assert(UI_DrawRaceTitleUp("NINA'S NIGHTMARE", 256, 187, 448) == 0 && nrec == 1);
    nrec = 0; assert(UI_DrawRaceTitleDown("NINA'S NIGHTMARE", 256, 76, 384) == 0 && nrec == 1 && rec[0].font == FONT_BIG);
    nrec = 0; assert(UI_DrawRaceTitleDown("ST PATRICKS DAY HUB WORLD", 256, 76, 384) >= 0 && rec[0].font == FONT_SMALL);
    nrec = 0; assert(UI_DrawRaceTitleUp("ST PATRICKS DAY HUB WORLD", 256, 187, 448) == 0 && nrec == 1); /* 425 px: fits the fly-in box */
    nrec = 0; assert(UI_DrawRaceTitleUp("ST PATRICKS DAY HUB WORLD (CUSTOM, LOCKHEART)", 256, 187, 448) > 0 && nrec == 2 && rec[0].font == FONT_BIG);
    puts("PASS: the fly-in and the race start banner draw every custom name in full");
    printf("race title: %d roster or suffixed names need more than 3 banner lines\n", overReal);
    return overReal == 0 ? 0 : 3;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / "names.txt").write_text("\n".join(roster + suffixed + worst) + "\n", encoding="utf-8")
    (tmp / "race.c").write_text(race_fixture, encoding="utf-8")
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-I", str(root),
                    "-I", str(root / "include"), "-o", str(tmp / "race"), str(tmp / "race.c")], check=True)
    done = subprocess.run([str(tmp / "race"), str(tmp / "names.txt"), str(len(roster) + len(suffixed))], capture_output=True, text=True)
print(done.stdout, end="")
assert done.returncode == 0, "race title layout failed (exit %d) %s" % (done.returncode, done.stderr)

# ---- wiring of the two screens ---------------------------------------------------------------
flow = source("game/UI/UI_RaceFlow.c")
site = flow[flow.index("int titleLift = 0;"):flow.index("// same for all")]
assert "if (UI_TrackNameIsCustom())" in site
assert "UI_DrawRaceTitleUp(UI_TrackName(-1), center, bottom - (credit ? 29 : 23), view->w - 64)" in site
assert "UI_DrawTrackCredit(credit, center, bottom - 11, view->w - 64, TEXT_BANNER_MARQUEE)" in site, "the fly-in credit keeps its scroll"
# the retail and the old credited draws are the else branches, unchanged
assert "else if (credit)" in site and "238, FONT_BIG, JUSTIFY_CENTER | ORANGE, TEXT_MARQUEE, TEXT_SLOT_BANNER, NULL);" in site
assert "- titleLift;" in flow and "rect.h = 0x1e + titleLift;" in flow
frame = source("game/MAIN/MainFrame_RenderFrame.c")
banner = frame[frame.index("if (UI_TrackNameIsCustom())"):frame.index("#endif\n\t\t\t\t\t}\n\n\t\t\t\t\t// if crystal challenge")]
assert "UI_DrawRaceTitleDown(UI_TrackName(-1), x, 76, width)" in banner
assert "UI_DrawTrackCredit(credit, x, 94 + lift, creditWidth, TEXT_BANNER_MARQUEE)" in banner, "the banner credit keeps its scroll"
assert "DecalFont_DrawLineBox(UI_TrackName(-1), x, 76, width, FONT_BIG," in banner and "UI_DrawTrackCredit(credit, x, 94, width, TEXT_BANNER_MARQUEE);" in banner, "retail draw unchanged"
custom = function("game/UI/UI_TrackName.c", "int UI_TrackNameIsCustom(void)")
assert "AP_ContentRaceDisplayName() != NULL" in custom and "MainRaceTrack_OfflineCustomTitle() != NULL" in custom
cups = function("ap/ap_hooks_content_cups.c", "const char *AP_ContentCupTrackName(void)")
assert "!ap_content_leg_custom_title(&pad->legs[leg])" in cups
print("race title wiring: ok")
