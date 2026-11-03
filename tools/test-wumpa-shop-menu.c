// gcc -std=c99 -Wall -Wextra -DCTR_AP -I ap -o /tmp/test-wumpa-shop-menu tools/test-wumpa-shop-menu.c && /tmp/test-wumpa-shop-menu
//
// Ruling of 2026-11-02: WUMPA SHOP is the first row of the hub save menu,
// every retail row keeps its order below it and maps back to its retail row;
// the hub line and the shop header read COLLECTED for the lifetime total; the
// received-fruit log says "held for the kart" for fruit that waits for a race
// and "banked" only for the Wumpa bank. Engine wiring is checked by
// tools/test-wumpa-shop-production.py.
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "ap_wumpa_shop.h"
#include "ap_wumpa_shop_menu.h"

struct Row
{
	short stringIndex;
	unsigned char up, down, left, right;
};

static int failures;
#define CHECK(label, expr) do { int ok_ = (expr); printf("%s  %s\n", ok_ ? "ok  " : "FAIL", label); failures += !ok_; } while (0)

// FONT_SMALL glyph width and the screen width the lines are centred on.
#define GLYPH 13
#define SCREEN 512

int main(void)
{
	static const struct Row rows[AP_WSHOP_GREEN_ROWS + 1] = AP_WSHOP_GREEN_ROWS_INIT;
	static const short retail[4] = {0xCB, 0xCC, 0xCD, 0xCE}; // LOAD, SAVE, DELETE, EXIT
	char line[128];
	int k, ok;

	CHECK("the shop row is first", rows[0].stringIndex == AP_LNG_WUMPA_SHOP && AP_WSHOP_GREEN_SHOP == 0);
	ok = 1;
	for (k = 0; k < 4; k++)
		ok &= rows[k + 1].stringIndex == retail[k];
	CHECK("LOAD, SAVE, DELETE, EXIT follow in retail order", ok);
	CHECK("EXIT is the last row", AP_WSHOP_GREEN_EXIT == 4 && rows[AP_WSHOP_GREEN_EXIT].stringIndex == 0xCE);
	CHECK("the table ends after five rows", AP_WSHOP_GREEN_ROWS == 5 && rows[5].stringIndex == -1);
	ok = 1;
	for (k = 0; k < AP_WSHOP_GREEN_ROWS; k++)
	{
		ok &= rows[k].up == (k > 0 ? k - 1 : 0);
		ok &= rows[k].down == (k < AP_WSHOP_GREEN_ROWS - 1 ? k + 1 : k);
		ok &= rows[k].left == k && rows[k].right == k;
	}
	CHECK("up and down step one row, stopping at both ends; left and right stay", ok);

	CHECK("the shop row is handled by the shop", AP_WumpaShopGreenToRetail(0) == AP_WSHOP_GREEN_HANDLED);
	ok = 1;
	for (k = 0; k < 4; k++)
		ok &= AP_WumpaShopGreenToRetail(k + 1) == k && AP_WumpaShopRetailToGreen(k) == k + 1;
	CHECK("rows 1 to 4 are retail rows 0 to 3, both ways", ok);
	CHECK("a backed-out row stays -1", AP_WumpaShopGreenToRetail(-1) == -1 && AP_WumpaShopRetailToGreen(-1) == -1);
	CHECK("the retail cursor on SAVE lands on SAVE", rows[AP_WumpaShopRetailToGreen(1)].stringIndex == 0xCC);

	AP_WumpaHubLine(line, (int)sizeof line, 120, 340);
	CHECK("hub line: WUMPA and COLLECTED", strcmp(line, "WUMPA 120  COLLECTED 340") == 0);
	AP_WumpaShopHeaderLine(line, (int)sizeof line, 2, 22);
	CHECK("shop header: BALANCE and COLLECTED", strcmp(line, "BALANCE 2  COLLECTED 22") == 0);
	AP_WumpaHubLine(line, (int)sizeof line, 999999, 999999);
	CHECK("hub line fits the screen with six-digit numbers", (int)strlen(line) * GLYPH <= SCREEN);
	AP_WumpaShopHeaderLine(line, (int)sizeof line, 999999, 999999);
	CHECK("shop header fits the screen with six-digit numbers", (int)strlen(line) * GLYPH <= SCREEN);
	AP_WumpaShopHeaderLine(line, (int)sizeof line, 1000000000LL, 1000000000LL);
	CHECK("the largest server wallet fits inside the panel", strcmp(line, "BALANCE 1e+09 COLLECTED 1e+09") == 0 && (int)strlen(line) * GLYPH <= 464);
	AP_WumpaShopHeaderLine(line, (int)sizeof line, LLONG_MAX, LLONG_MAX);
	CHECK("a locally accumulated extreme wallet fits the panel", (int)strlen(line) * GLYPH <= 464);
	AP_WumpaShopHeaderLine(line, (int)sizeof line, LLONG_MIN, LLONG_MAX);
	CHECK("an overspent extreme wallet fits the panel", (int)strlen(line) * GLYPH <= 464);
	AP_WumpaHubLine(line, (int)sizeof line, LLONG_MAX, LLONG_MAX);
	CHECK("the hub line fits even with extreme totals", (int)strlen(line) * GLYPH <= SCREEN);
	CHECK("no BANKED label left", strstr(line, "BANKED") == NULL);

	AP_WumpaReceiveLine(line, (int)sizeof line, 1, 1, 0, 0, 10);
	CHECK("one fruit outside a race is held for the kart", strcmp(line, "[AP WUMPA] held for the kart +1 (pending 1)\n") == 0);
	AP_WumpaReceiveLine(line, (int)sizeof line, 7, 10, 3, 0, 10);
	CHECK("past 10 with banking: held and banked",
	      strcmp(line, "[AP WUMPA] held for the kart +7 (pending 10), banked +3 past 10\n") == 0);
	AP_WumpaReceiveLine(line, (int)sizeof line, 0, 10, 3, 0, 10);
	CHECK("kart already full with banking: all banked", strcmp(line, "[AP WUMPA] banked +3 past 10 (kart full)\n") == 0);
	AP_WumpaReceiveLine(line, (int)sizeof line, 7, 10, 0, 3, 10);
	CHECK("past 10 without banking: held and discarded",
	      strcmp(line, "[AP WUMPA] held for the kart +7, discarded +3 (kart full at 10)\n") == 0);
	AP_WumpaReceiveLine(line, (int)sizeof line, 0, 10, 0, 3, 10);
	CHECK("kart already full without banking: discarded", strcmp(line, "[AP WUMPA] discarded +3 (kart full at 10)\n") == 0);

	printf("%s\n", failures ? "FAILED" : "PASS");
	return failures != 0;
}
