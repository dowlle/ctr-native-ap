"""In-race Kart Setup (2026-11-18): execute the production race pause row swap.

Extracts MainFreeze_APKartSetupRows from game/MAIN/MainFreeze.c and drives it
against the retail Adventure race and cup row tables: KART SETUP sits directly
below RESUME, the up/down wiring wraps through every row, the highlight is
carried by row in both directions, and the retail table comes back unchanged
when the row is not live.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/MAIN/MainFreeze.c").read_text()
start = source.index("static struct MenuRow ap_ksRowsAdvRace")
end = source.index("\n#endif", start)
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <common.h>
#include "ap/ap_kartsetup_logic.h"
struct sData sdata_static;
static int live = 1;
int AP_KartSetupRowLive(void) { return live; }
'''
fixture += source[start:end]
fixture += r'''
static struct MenuRow race[5] = {{0x2,3,1,0,0},{0x1,0,2,1,1},{0xD,1,3,2,2},{0xE,2,0,3,3},{-1,0,0,0,0}};
static struct MenuRow cup[4] = {{0x2,2,1,0,0},{0xD,0,2,1,1},{0xE,1,0,2,2},{-1,0,0,0,0}};

static void check(struct MenuRow *retail, int retailCount, struct MenuRow *ap) {
    struct RectMenu m = {0};
    m.rows = retail;
    m.rowSelected = 0;
    live = 1;
    assert(MainFreeze_APKartSetupRows(&m, retail, ap) == &m);
    assert(m.rows == ap);
    int count = retailCount + 1;
    assert(ap[0].stringIndex == 0x2 && ap[1].stringIndex == AP_LNG_KART_SETUP);
    for (int r = 2; r < count; r++) assert(ap[r].stringIndex == retail[r - 1].stringIndex);
    assert(ap[count].stringIndex == -1);
    for (int r = 0; r < count; r++) {
        assert(ap[r].rowOnPressUp == (r + count - 1) % count);
        assert(ap[r].rowOnPressDown == (r + 1) % count);
        assert(ap[r].rowOnPressLeft == r && ap[r].rowOnPressRight == r);
    }
    /* Every row is reached by repeated DOWN from RESUME. */
    int seen = 0, r = 0;
    for (int i = 0; i < count; i++) { seen |= 1 << r; r = ap[r].rowOnPressDown; }
    assert(seen == (1 << count) - 1 && r == 0);
    /* Retail data untouched. */
    assert(retail[0].stringIndex == 0x2 && retail[retailCount].stringIndex == -1);
    /* Carry: retail OPTIONS (last row) -> AP OPTIONS; AP set rebuilt in place keeps the row. */
    m.rows = retail; m.rowSelected = (s16)(retailCount - 1);
    MainFreeze_APKartSetupRows(&m, retail, ap);
    assert(ap[m.rowSelected].stringIndex == 0xE);
    MainFreeze_APKartSetupRows(&m, retail, ap);
    assert(ap[m.rowSelected].stringIndex == 0xE);
    /* Row disappears (disconnect): OPTIONS stays OPTIONS, KART SETUP lands on RESUME. */
    live = 0;
    MainFreeze_APKartSetupRows(&m, retail, ap);
    assert(m.rows == retail && retail[m.rowSelected].stringIndex == 0xE);
    live = 1; MainFreeze_APKartSetupRows(&m, retail, ap);
    m.rowSelected = 1; live = 0;
    MainFreeze_APKartSetupRows(&m, retail, ap);
    assert(m.rows == retail && m.rowSelected == 0);
    /* Not live and on retail: untouched. */
    MainFreeze_APKartSetupRows(&m, retail, ap);
    assert(m.rows == retail && m.rowSelected == 0);
    /* A junk savestate row lands on RESUME. */
    live = 1; m.rowSelected = 99;
    MainFreeze_APKartSetupRows(&m, retail, ap);
    assert(m.rowSelected == 0);
    m.rowSelected = 99;
    MainFreeze_APKartSetupRows(&m, retail, ap);
    assert(m.rowSelected == 0);
}

int main(void) {
    check(race, 4, ap_ksRowsAdvRace);
    check(cup, 3, ap_ksRowsAdvCup);
    /* Pure gate: hub, 2P, Arcade and a dead selector carry no row. */
    assert(AP_KartSetup_RowLive(AP_KS_ADVENTURE_MODE, 1, 1, 1));
    assert(!AP_KartSetup_RowLive(AP_KS_ADVENTURE_MODE | AP_KS_ADVENTURE_ARENA, 1, 1, 1));
    assert(!AP_KartSetup_RowLive(AP_KS_ADVENTURE_MODE, 2, 1, 1));
    assert(!AP_KartSetup_RowLive(AP_KS_ADVENTURE_MODE, 1, 0, 1));
    assert(!AP_KartSetup_RowLive(AP_KS_ADVENTURE_MODE, 1, 1, 0));
    assert(!AP_KartSetup_RowLive(0, 1, 1, 1));
    puts("kart setup race pause rows: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "t.c"
    exe = Path(tmp) / "t"
    src.write_text(fixture)
    subprocess.run(["cc", "-m32", "-std=gnu99", "-Wall", "-Wno-unused-function", "-DCTR_AP", "-DCTR_NATIVE",
                    "-DBUILD=926", "-I", str(root / "include"), "-I", str(root), "-I", str(root / "ap"),
                    "-I", str(root / "externals/SDL/include"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# The page is reached through the row's action and owns the menu pass first.
menu = source[source.index("void MainFreeze_MenuPtrDefault"):]
assert "stringID == AP_LNG_KART_SETUP" in menu and "AP_KartSetupRequestOpen();" in menu
rect = (root / "game/RECTMENU.c").read_text()
hook = rect.index("if (AP_KartSetupMenuFrame()) return;")
assert hook < rect.index("struct RectMenu *currMenu;", hook)
assert 'index == AP_LNG_KART_SETUP' in rect and '"KART SETUP"' in rect
# One owner for the rules: both surfaces step through the same function.
swap = (root / "ap/ap_charswap.c").read_text()
assert swap.count("ap_cs_adjustFor(") == 3, "definition, hub caller, race caller"
assert "ap_cs_drawPanel(gGT, selectedChar" in swap and "ap_cs_drawPanel(gGT, characterID, panelY" in swap
print("kart setup wiring: PASS")
