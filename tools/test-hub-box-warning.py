"""Measure the hub item-box warning against the screen with the production font metrics.

The warning is drawn at x = AP_FEED_X on a 512 px wide logical screen (4:3, 16:9,
16:10 and 21:9 all draw the HUD in that 512 px space; the window size only scales
it). Every line must end at least AP_FEED_X px before the right edge, and the
two lines must stay in the 0x48..0x54 band above the three feed lines at 96..120.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def source(path):
    return (root / path).read_text()


def function(path, signature):
    text = source(path)
    start = text.index(signature + "\n{")
    return text[start:text.index("\n}\n", start) + 3]


def define(name):
    return re.search(r"#define\s+" + name + r"\s+(.+)", source("ap/ap_hooks_feed.c")).group(1).strip()


def table(name):
    body = re.search(r"\." + name + r"\s*=\s*\{(.*?)\}", source("game/zGlobal_DATA.c"), re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    return [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|-?\d+", body)]


macros = source("include/macros.h")
screen_width = int(re.search(r"#define SCREEN_WIDTH\s+(\d+)", macros).group(1))
feed_x = int(re.search(r"#define AP_FEED_X\s+(0x[0-9A-Fa-f]+|\d+)", source("ap/ap_hooks_feed.c")).group(1), 0)
line_h = int(re.search(r"#define AP_FEED_LINE_H\s+(0x[0-9A-Fa-f]+|\d+)", source("ap/ap_hooks_feed.c")).group(1), 0)
assert screen_width == 512

fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef short s16; typedef unsigned char u8; typedef unsigned int u32;
#define UsaRetail 926
#define EurRetail 1020
#define JpnRetail 1111
#define JpnTrial 1006
#define BUILD UsaRetail
#define FONT_SMALL 2
static struct { short font_charPixWidth[4], font_buttonPixWidth[4], font_puncPixWidth[4]; } data = {
    {%s}, {%s}, {%s} };
''' % tuple(", ".join(map(str, table(n))) for n in ("font_charPixWidth", "font_buttonPixWidth", "font_puncPixWidth"))
fixture += function("game/DecalFont.c", "int DecalFont_GetLineWidthStrlen(char *character, int len, int fontType)")
fixture += function("game/DecalFont.c", "int DecalFont_GetLineWidth(char *str, s16 fontType)")
fixture += r'''
int main(void) {
    const char *lines[2] = { %s, %s };
    const int screen = %d, x = %d, margin = %d;
    for (int i = 0; i < 2; i++) {
        int w = DecalFont_GetLineWidth((char *)lines[i], FONT_SMALL);
        printf("line %%d: %%d px, ends at %%d of %%d (%%s)\n", i + 1, w, x + w, screen, lines[i]);
        assert(w > 0);
        assert(x + w <= screen - margin);
    }
    /* The text that shipped first (533 px) must stay rejected by this check. */
    assert(x + DecalFont_GetLineWidth((char *)"!! ITEM BOX CHECKS OFF ON SOME TRACKS !!", FONT_SMALL) > screen - margin);
    puts("PASS: hub item-box warning fits inside the 512 px screen with an equal right margin");
}
''' % (define("AP_BOX_WARN_LINE1"), define("AP_BOX_WARN_LINE2"), screen_width, feed_x, feed_x)

# Vertical band, read from the production draw calls: the schema lines, the
# custom-content lines, this warning and the hub feed must not overlap. Lines are
# 8 px tall (FONT_SMALL).
feed = source("ap/ap_hooks_feed.c")
custom = source("ap/ap_hooks_custom_content.c")
base_y = int(re.search(r"#define AP_FEED_BASE_Y\s+(0x[0-9A-Fa-f]+|\d+)", feed).group(1), 0)
warn_y = int(re.search(r"DecalFont_DrawLine\(boxWarn1, AP_FEED_X, (0x[0-9A-Fa-f]+|\d+),", feed).group(1), 0)
assert "boxWarn2, AP_FEED_X, %#x + AP_FEED_LINE_H" % warn_y in feed
schema_y = int(re.search(r"DecalFont_DrawLine\(warn1, AP_FEED_X, (0x[0-9A-Fa-f]+),", feed).group(1), 0)
custom_ys = [int(v, 0) + (line_h if plus else 0) for v, plus in re.findall(r"AP_FEED_X, (0x[0-9A-Fa-f]+)( \+ AP_FEED_LINE_H)?, FONT_SMALL", custom)]
visible = int(re.search(r"const int visibleLimit = [^?]*\? (\d+) :", feed).group(1))
assert "ap_feed_hub_pad_title ? 120 : AP_FEED_BASE_Y" in feed
glyph = 8
assert schema_y + line_h + glyph <= min(custom_ys), (schema_y, custom_ys)
assert max(custom_ys) + line_h + glyph <= warn_y, (custom_ys, warn_y)
for anchor in (120, base_y):
    feed_top = anchor - (visible - 1) * line_h
    assert warn_y + line_h + glyph <= feed_top, (warn_y, anchor, feed_top)
print("bands: schema %#x, custom %s, warning %#x-%#x, feed top %d / %d" % (schema_y, [hex(v) for v in custom_ys], warn_y, warn_y + line_h, 120 - (visible - 1) * line_h, base_y - (visible - 1) * line_h))

with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-I", str(root), str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
