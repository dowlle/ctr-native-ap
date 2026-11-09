"""Compile the production shop row and capture its rendered text and colour.

Network lookups and the font are stubbed; name selection, alias ordering,
owner suffix, clipping and scrolling run from ap_wumpa_shop_ui.c itself.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'ap/ap_wumpa_shop_ui.c').read_text()
start = source.index('static int AP_WShopTextWidth(')
row = source[start:source.index('static void AP_WShopDraw(void)', start)]
prelude = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap/ap_trap_display_logic.h"
#include "ap/ap_text_clip.h"
#include "ap/ap_wumpa_shop.h"
#define FONT_SMALL 0
#define WHITE 10
#define ORANGE 11
#define GRAY 12
#define RED 13
#define JUSTIFY_RIGHT 256
#define AP_WSHOP_NAME_X 0x44
#define AP_WSHOP_RIGHT_X 0x1d0
static AP_WumpaShopConfig ctr_wumpa_shop;
static struct { int trap_display; unsigned trap_display_salt; } ctr_cfg;
static struct { int marqueeFrame; } ap_wshop;
static int known = 1, text_known = 1, owner = 1, state = AP_WUMPA_SLOT_FOR_SALE;
static unsigned real_flags = AP_ITEM_FLAG_TRAP;
static char drawn[128];
static int drawn_color;
static const char *real_name = "LIMP MODE";
static int ap_net_self_slot(void) { return 1; }
static int ap_net_scout_known(long long code, long long *item, int *player, unsigned *flags)
{ (void)code; *item = 35010212; *player = owner; *flags = real_flags; return known; }
static int ap_net_scout_text(long long code, char *item, int ni, char *player, int np)
{ (void)code; snprintf(item, ni, "%s", real_name); snprintf(player, np, "OTHER"); return text_known; }
static void AP_CeremonySanitize(const char *s, char *out, int n) { snprintf(out, n, "%s", s); }
static int AP_CeremonyNameIsBlank(const char *s) { return !*s; }
// Deliberately provide an alias that would expose the trap if applied last.
static int AP_ItemDisplayAlias(long long item, char *out, int n)
{ (void)item; snprintf(out, n, "%s", "TRAP ALIAS"); return 1; }
static int AP_WShopSlotState(int hub, int k) { (void)hub; (void)k; return state; }
static int AP_ClassFontColor(unsigned flags) { return AP_ItemFlagsClass(flags); }
static int DecalFont_GetLineWidthStrlen(char *s, int n, int font)
{ (void)font; return (n < 0 ? (int)strlen(s) : n) * 7; }
static void DecalFont_DrawLine(char *s, int x, int y, int font, int color)
{ (void)y; (void)font; if (x == AP_WSHOP_NAME_X) { snprintf(drawn, sizeof drawn, "%s", s); drawn_color = color; } }
'''
tests = r'''
int main(void)
{
    long long loc;
    int selected;
    ctr_cfg.trap_display = 1;
    ctr_cfg.trap_display_salt = 12345;
    for (selected = 0; selected <= 1; selected++)
    {
        int states[] = {AP_WUMPA_SLOT_FOR_SALE, AP_WUMPA_SLOT_LOCKED,
                        AP_WUMPA_SLOT_TOO_POOR, AP_WUMPA_SLOT_PENDING};
        unsigned i;
        for (i = 0; i < sizeof states / sizeof states[0]; i++)
        {
            state = states[i];
            AP_WShopDrawRow(0, 0, 0, selected);
            assert(!strcmp(drawn, "PROGRESSION ITEM"));
            assert(drawn_color == (state == AP_WUMPA_SLOT_LOCKED ? GRAY : AP_ITEM_CLASS_PROGRESSION));
        }
    }
    state = AP_WUMPA_SLOT_FOR_SALE;
    owner = 2;
    AP_WShopDrawRow(0, 0, 0, 1);
    assert(!strcmp(drawn, "PROGRESSION ITEM (OTHER)"));
    owner = 1;
    text_known = 0;
    AP_WShopDrawRow(0, 0, 0, 1);
    assert(!strcmp(drawn, "PROGRESSION ITEM"));
    text_known = 1;
    ctr_cfg.trap_display = 2;
    for (loc = 35028600; loc < 35028800; loc++)
    {
        unsigned flags = AP_TrapDisplayFlags(2, 12345, loc, real_flags);
        ctr_wumpa_shop.slot[0][0].code = loc;
        for (selected = 0; selected <= 1; selected++)
        {
            ap_wshop.marqueeFrame = 600; // would scroll a long real trap name
            AP_WShopDrawRow(0, 0, 0, selected);
            assert(!strcmp(drawn, flags & AP_ITEM_FLAG_PROGRESSION ? "PROGRESSION ITEM" : "FILLER ITEM"));
            assert(drawn_color == AP_ClassFontColor(flags));
        }
    }
    state = AP_WUMPA_SLOT_SOLD;
    AP_WShopDrawRow(0, 0, 0, 1);
    assert(!strcmp(drawn, "TRAP ALIAS") && drawn_color == GRAY);
    state = AP_WUMPA_SLOT_FOR_SALE;
    ctr_cfg.trap_display = 0;
    AP_WShopDrawRow(0, 0, 0, 1);
    assert(!strcmp(drawn, "TRAP ALIAS") && drawn_color == AP_ITEM_CLASS_TRAP);
    ctr_cfg.trap_display = 1;
    real_flags = AP_ITEM_FLAG_PROGRESSION;
    AP_WShopDrawRow(0, 0, 0, 1);
    assert(!strcmp(drawn, "TRAP ALIAS")); // non-traps retain ordinary aliases
    known = 0;
    AP_WShopDrawRow(0, 0, 0, 1);
    assert(!strcmp(drawn, "UNKNOWN ITEM") && drawn_color == WHITE);
    puts("PASS: production shop names, colours, owners, pending, sold, missing scouts and mixed locations");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ctr-trap-shop-') as tmp:
    src = Path(tmp) / 'shop.c'
    exe = Path(tmp) / 'shop'
    src.write_text(prelude + row + tests)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(root), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
