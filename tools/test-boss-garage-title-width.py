"""BG1: the boss garage title ("<RACER>'s Challenge" and "<RACER>'s Boss Cup") must fit the screen.

The garage title is drawn centred in FONT_BIG on the 512 px wide logical screen (4:3, 16:9, 16:10
and 21:9 all draw the HUD in that 512 px space). The title is measured with the production font
routine and the production width table for every racer name of the NTSC-U long-name table
(name_LNG_long), Penta Penguin under his short name Penta as AP_BossGarageTitle writes it.
Each title must end at least 16 px inside both screen edges, and the Boss Cup title must be no
wider than the Challenge title it replaces. Names that end in S would read "...S'S"; the code
keeps the plain "'s" form, so no name needs a rule, and this test fails if one is added.
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


def table(name):
    body = re.search(r"\." + name + r"\s*=\s*\{(.*?)\}", source("game/zGlobal_DATA.c"), re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    return [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|-?\d+", body)]


NAMES = ["CRASH BANDICOOT", "NEO CORTEX", "TINY TIGER", "COCO BANDICOOT", "N. GIN", "DINGODILE", "POLAR",
         "PURA", "PINSTRIPE", "PAPU PAPU", "RIPPER ROO", "KOMODO JOE", "N. TROPY", "Penta", "FAKE CRASH"]
assert not [n for n in NAMES if n.upper().endswith("S")], "a name ending in S needs a rule"
gates = source("ap/ap_hooks_gates.c")
assert "\"%s's Boss Cup\" : \"%s's Challenge\"" in gates, "title formats changed"
assert 'c == PENTA_PENGUIN ? "Penta"' in function("ap/ap_hooks_gates.c", "int AP_BossGarageTitle(int bossIdx, char *out, int cap)")
macros = source("include/macros.h")
screen_width = int(re.search(r"#define SCREEN_WIDTH\s+(\d+)", macros).group(1))
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
#define FONT_BIG 1
static struct { short font_charPixWidth[4], font_buttonPixWidth[4], font_puncPixWidth[4]; } data = {
    {%s}, {%s}, {%s} };
''' % tuple(", ".join(map(str, table(n))) for n in ("font_charPixWidth", "font_buttonPixWidth", "font_puncPixWidth"))
fixture += function("game/DecalFont.c", "int DecalFont_GetLineWidthStrlen(char *character, int len, int fontType)")
fixture += function("game/DecalFont.c", "int DecalFont_GetLineWidth(char *str, s16 fontType)")
names = ", ".join('"%s"' % n for n in NAMES)
fixture += r'''
int main(void) {
    const char *names[] = { %s };
    const int screen = %d, margin = 16;
    int widest = 0;
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        char a[96], b[96];
        snprintf(a, sizeof a, "%%s's Challenge", names[i]);
        snprintf(b, sizeof b, "%%s's Boss Cup", names[i]);
        int wa = DecalFont_GetLineWidth(a, FONT_BIG), wb = DecalFont_GetLineWidth(b, FONT_BIG);
        printf("%%-28s %%3d px | %%-26s %%3d px\n", a, wa, b, wb);
        assert(wa > 0 && wb > 0 && wb <= wa);
        assert(wa <= screen - 2 * margin && wb <= screen - 2 * margin);
        if (wa > widest) widest = wa;
    }
    printf("widest title %%d px of %%d\n", widest, screen);
    puts("PASS: every boss garage title fits the 512 px screen with a 16 px margin");
    return 0;
}
''' % (names, screen_width)
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "width.c"
    c.write_text(fixture)
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-o", str(Path(tmp) / "width"), str(c)], check=True)
    out = subprocess.run([str(Path(tmp) / "width")], check=True, capture_output=True, text=True).stdout
print(out, end="")
