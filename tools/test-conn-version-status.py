"""Draw both refusal versions with the production Connection menu and font."""
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


root = Path(__file__).resolve().parents[1]
menu = (root / "game/230/MM_ConfigMenu.c").read_text()
font = (root / "game/DecalFont.c").read_text()
data = (root / "game/zGlobal_DATA.c").read_text()
width = re.search(r"#define CONN_STATUS_LINE_WIDTH\s+(\S+)", menu)[1]
fields = ("font_charPixWidth", "font_buttonPixWidth", "font_puncPixWidth")
tables = [re.search(r"\." + field + r"\s*=\s*\{([^}]+)\}", data)[1] for field in fields]

fixture = f"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef short s16;
typedef unsigned char u8;
typedef unsigned int u32;
#define UsaRetail 926
#define EurRetail 1020
#define JpnRetail 1111
#define JpnTrial 1006
#define BUILD UsaRetail
#define FONT_SMALL 2
#define WHITE 0
#define JUSTIFY_CENTER 1
#define CONN_STATUS_LINE_WIDTH {width}
static struct {{ short font_charPixWidth[4], font_buttonPixWidth[4], font_puncPixWidth[4]; }} data = {{
    {{{tables[0]}}}, {{{tables[1]}}}, {{{tables[2]}}}
}};
"""
fixture += function(font, "int DecalFont_GetLineWidthStrlen(char *character, int len, int fontType)")
fixture += function(font, "int DecalFont_GetLineWidth(char *str, s16 fontType)")
fixture += """
static char drawn[3][128];
static int count;
static void DecalFont_DrawLineOT(char *text, int x, int y, int font, int color, uint32_t *ot)
{
    (void)x; (void)y; (void)color; (void)ot;
    assert(count < 3 && DecalFont_GetLineWidth(text, font) <= CONN_STATUS_LINE_WIDTH);
    strcpy(drawn[count++], text);
}
"""
fixture += function(menu, "static const char *Conn_RoomVersionDivider(")
fixture += function(menu, "static void Conn_DrawLongStatus(")
fixture += """
static void expect_rows(const char *message, const char *room)
{
    const char *divider = Conn_RoomVersionDivider(message);
    assert(divider);
    count = 0;
    Conn_DrawLongStatus(message, 144, 176, 14, 2, divider, NULL);
    assert(count == 3);
    assert(strcmp(drawn[1], room) == 0);
    assert(strcmp(drawn[2], "THIS BUILD IS 0.3.0-ALPHA5.") == 0);
}
int main(void)
{
    expect_rows("Error: GENERATED WITH 0.3.0. THIS BUILD IS 0.3.0-ALPHA5.", "GENERATED WITH 0.3.0.");
    expect_rows("Error: ROOM BUILD 0.3.0-CONTENT-DEV.123. THIS BUILD IS 0.3.0-ALPHA5.",
                "ROOM BUILD 0.3.0-CONTENT-DEV.123.");
}
"""
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    source.write_text(fixture)
    subprocess.run(["cc", "-std=c99", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
