#ifdef CTR_AP

// ============================================================================
// The Wumpa Shop page (#249). Opened from the WUMPA SHOP row of a hub's green
// load/save menu. While open it owns input and drawing the way the enlarged
// tracker does (RECTMENU_ProcessState returns early), so the load/save menu
// underneath neither runs nor sees a press. Triangle or Circle goes back to
// that menu.
//
// One line per slot of this hub: slot number, the item and its owner when it
// is for another player, coloured by classification, and the price, SOLD, or
// the lifetime total that puts it on sale. The header shows the balance and the
// lifetime banked total. Up and Down select, Cross buys.
//
// Buying needs the room: the wallet keys must have been read on this
// connection. The page then shows SHOP NEEDS A CONNECTION or a short wait
// message and refuses Cross. A refused `wumpa_shop` block shows SHOP
// UNAVAILABLE. Single unity member after ap_hooks_feed.c and ap_wumpa.c.
// ============================================================================

#include "ap_wumpa_shop.h"
#include "ap_wumpa_shop_menu.h"

#define AP_WSHOP_VISIBLE 9
#define AP_WSHOP_ROW_H 0xd
#define AP_WSHOP_MSG_FRAMES 150

static struct
{
	int open;
	int hub;
	int cursor;
	int top;
	int msgFrames;
	char msg[48];
	long long buying;     // code of the purchase in flight, -1 none
	int hinted[AP_WUMPA_SHOP_HUBS];
} ap_wshop = {0, 0, 0, 0, 0, "", -1, {0}};

// The green load/save rows with WUMPA SHOP above EXIT. Same wiring as retail:
// up and down step, left and right stay.
static struct MenuRow ap_wshop_green_rows[AP_WSHOP_GREEN_ROWS + 1] = {
	{0xCB, 0, 1, 0, 0},                     // LOAD
	{0xCC, 0, 2, 1, 1},                     // SAVE
	{0xCD, 1, 3, 2, 2},                     // DELETE
	{AP_LNG_WUMPA_SHOP, 2, 4, 3, 3},        // WUMPA SHOP
	{0xCE, 3, 4, 4, 4},                     // EXIT
	{-1, 0, 0, 0, 0},
};

int AP_WumpaShopRowShown(void)
{
	return ctr_cfg_active() && !ctr_cfg.schema_newer &&
	       ctr_wumpa_shop.state != AP_WUMPA_SHOP_ABSENT;
}

void AP_WumpaShopPrepareGreenMenu(struct RectMenu *menu)
{
	if (menu == 0)
		return;
	if (AP_WumpaShopRowShown())
	{
		if (menu->rows != ap_wshop_green_rows && menu->rowSelected == 3)
			menu->rowSelected = AP_WSHOP_GREEN_EXIT;
		menu->rows = ap_wshop_green_rows;
	}
	else
	{
		if (menu->rows == ap_wshop_green_rows && menu->rowSelected >= AP_WSHOP_GREEN_SHOP)
			menu->rowSelected = 3;
		menu->rows = &data.rowsGreenLoadSave[0];
	}
}

int AP_WumpaShopGreenExitRow(const struct RectMenu *menu)
{
	return (menu != 0 && menu->rows == ap_wshop_green_rows) ? AP_WSHOP_GREEN_EXIT : 3;
}

static void AP_WShopSay(const char *text)
{
	snprintf(ap_wshop.msg, sizeof ap_wshop.msg, "%s", text);
	ap_wshop.msgFrames = AP_WSHOP_MSG_FRAMES;
}

static void AP_WShopHint(int hub)
{
	long long codes[AP_WUMPA_SHOP_MAX_SLOTS];
	int n = 0, k;
	if (!ctr_wumpa_shop.hints || ap_wshop.hinted[hub] || !ap_net_is_connected() ||
	    !AP_WumpaShopUsable(&ctr_wumpa_shop))
		return;
	for (k = 0; k < ctr_wumpa_shop.slots; k++)
	{
		long long code = ctr_wumpa_shop.slot[hub][k].code;
		if (!ap_net_location_checked(code) && !ap_net_wumpa_bought(code))
			codes[n++] = code;
	}
	ap_wshop.hinted[hub] = 1;
	if (n > 0)
		ap_net_wumpa_hint(codes, n);
}

int AP_WumpaShopGreenMenuRow(struct RectMenu *menu, int row)
{
	struct GameTracker *gt = sdata->gGT;
	int hub;
	if (menu == 0 || menu->rows != ap_wshop_green_rows)
		return row;
	if (row == AP_WSHOP_GREEN_EXIT)
		return 3;
	if (row != AP_WSHOP_GREEN_SHOP)
		return row;
	hub = gt ? AP_WumpaShopHubOfLevel(gt->levelID) : -1;
	if (hub < 0)
		return AP_WSHOP_GREEN_HANDLED;
	ap_wshop.open = 1;
	ap_wshop.hub = hub;
	ap_wshop.cursor = 0;
	ap_wshop.top = 0;
	ap_wshop.msg[0] = '\0';
	ap_wshop.msgFrames = 0;
	AP_LogLine("[AP WUMPA SHOP] page opened\n");
	AP_WShopHint(hub);
	return AP_WSHOP_GREEN_HANDLED;
}

static int AP_WShopSlotState(int hub, int k)
{
	const AP_WumpaShopSlot *s = &ctr_wumpa_shop.slot[hub][k];
	return AP_WumpaSlotState(s, ap_net_wumpa_lifetime(), ap_net_wumpa_balance(),
	                         ap_net_wumpa_bought(s->code), ap_net_location_checked(s->code),
	                         ap_wshop.buying == s->code);
}

static void AP_WShopBuy(void)
{
	int hub = ap_wshop.hub, k = ap_wshop.cursor, r;
	const AP_WumpaShopSlot *s;
	char line[48];
	if (!AP_WumpaShopUsable(&ctr_wumpa_shop))
	{
		AP_WShopSay("SHOP UNAVAILABLE");
		return;
	}
	if (!ap_net_is_connected())
	{
		AP_WShopSay("SHOP NEEDS A CONNECTION");
		return;
	}
	s = &ctr_wumpa_shop.slot[hub][k];
	r = ap_net_wumpa_buy(s->code, s->price, s->unlock);
	switch (r)
	{
	case 0:
		ap_wshop.buying = s->code;
		AP_WShopSay("BUYING...");
		OtherFX_Play(1, 1);
		return;
	case 1:
		AP_WShopSay(ap_net_is_connected() ? "LOADING YOUR WALLET" : "SHOP NEEDS A CONNECTION");
		break;
	case 2:
		AP_WShopSay("PLEASE WAIT");
		break;
	case 3:
		snprintf(line, sizeof line, "ON SALE AT %d BANKED", s->unlock);
		AP_WShopSay(line);
		break;
	case 4:
		AP_WShopSay("NOT ENOUGH WUMPA");
		break;
	default:
		AP_WShopSay("SOLD");
		break;
	}
	OtherFX_Play(5, 1);
}

static void AP_WShopClose(void)
{
	ap_wshop.open = 0;
	ap_wshop.buying = -1;
}

static void AP_WShopDrawRow(int hub, int k, int y, int selected)
{
	const AP_WumpaShopSlot *s = &ctr_wumpa_shop.slot[hub][k];
	char itemRaw[64], playerRaw[64], itemS[40], playerS[24], name[64], right[24];
	long long item = 0;
	int player = -1, state, color;
	unsigned flags = 0;
	int known = ap_net_scout_known(s->code, &item, &player, &flags);

	if (known && ap_net_scout_text(s->code, itemRaw, (int)sizeof itemRaw, playerRaw, (int)sizeof playerRaw))
	{
		AP_CeremonySanitize(itemRaw, itemS, (int)sizeof itemS);
		AP_CeremonySanitize(playerRaw, playerS, (int)sizeof playerS);
	}
	else
	{
		itemS[0] = '\0';
		playerS[0] = '\0';
	}
	{
		char alias[40];
		if (known && AP_ItemDisplayAlias(item, alias, (int)sizeof alias))
			AP_CeremonySanitize(alias, itemS, (int)sizeof itemS);
	}
	if (AP_CeremonyNameIsBlank(itemS))
		snprintf(itemS, sizeof itemS, "%s", "UNKNOWN ITEM");
	if (known && player != ap_net_self_slot() && playerS[0] != '\0')
		snprintf(name, sizeof name, "%s (%s)", itemS, playerS);
	else
		snprintf(name, sizeof name, "%s", itemS);
	if ((int)strlen(name) > 34)
	{
		name[33] = '.';
		name[34] = '\0';
	}

	state = AP_WShopSlotState(hub, k);
	color = known ? AP_ClassFontColor(flags) : WHITE;
	switch (state)
	{
	case AP_WUMPA_SLOT_SOLD:
		snprintf(right, sizeof right, "SOLD");
		color = GRAY;
		break;
	case AP_WUMPA_SLOT_PENDING:
		snprintf(right, sizeof right, "...");
		break;
	case AP_WUMPA_SLOT_LOCKED:
		snprintf(right, sizeof right, "AT %d", s->unlock);
		color = GRAY;
		break;
	default:
		snprintf(right, sizeof right, "%d", s->price);
		break;
	}

	{
		char num[12];
		snprintf(num, sizeof num, "%d", k + 1);
		DecalFont_DrawLine(num, 0x3c, y, FONT_SMALL, (selected ? WHITE : ORANGE) | JUSTIFY_RIGHT);
	}
	DecalFont_DrawLine(name, 0x44, y, FONT_SMALL, color);
	DecalFont_DrawLine(right, 0x1d0, y, FONT_SMALL,
	                   (state == AP_WUMPA_SLOT_TOO_POOR ? RED : selected ? WHITE : ORANGE) | JUSTIFY_RIGHT);
}

static void AP_WShopDraw(void)
{
	struct GameTracker *gt = sdata->gGT;
	int hub = ap_wshop.hub, k, y;
	char line[64];
	RECT box;

	snprintf(line, sizeof line, "%s", sdata->lngStrings[data.metaDataLEV[AP_WumpaShopHubLevel(hub)].name_LNG]);
	DecalFont_DrawLine("WUMPA SHOP", 0x100, 0x16, FONT_BIG, ORANGE | JUSTIFY_CENTER);
	DecalFont_DrawLine(line, 0x100, 0x28, FONT_SMALL, WHITE | JUSTIFY_CENTER);

	if (!AP_WumpaShopUsable(&ctr_wumpa_shop))
	{
		DecalFont_DrawLine("SHOP UNAVAILABLE", 0x100, 0x60, FONT_SMALL, RED | JUSTIFY_CENTER);
		DecalFont_DrawLine("UPDATE THE CLIENT OR THE SEED", 0x100, 0x70, FONT_SMALL, WHITE | JUSTIFY_CENTER);
	}
	else
	{
		if (!ap_net_is_connected())
			snprintf(line, sizeof line, "SHOP NEEDS A CONNECTION");
		else if (!ap_net_wumpa_ready())
			snprintf(line, sizeof line, "LOADING YOUR WALLET");
		else
			snprintf(line, sizeof line, "BALANCE %lld   BANKED %lld",
			         ap_net_wumpa_balance(), ap_net_wumpa_lifetime());
		DecalFont_DrawLine(line, 0x100, 0x36, FONT_SMALL, ORANGE | JUSTIFY_CENTER);

		for (k = ap_wshop.top, y = 0x48; k < ctr_wumpa_shop.slots && k < ap_wshop.top + AP_WSHOP_VISIBLE;
		     k++, y += AP_WSHOP_ROW_H)
		{
			if (k == ap_wshop.cursor && gt != 0)
			{
				box.x = 0x24;
				box.y = (s16)(y - 2);
				box.w = 0x1b8;
				box.h = AP_WSHOP_ROW_H - 1;
				CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Green, TRANS_50_DECAL,
				                     gt->backBuffer->otMem.uiOT);
			}
			AP_WShopDrawRow(hub, k, y, k == ap_wshop.cursor);
		}
		if (ap_wshop.top > 0)
			DecalFont_DrawLine("^", 0x100, 0x40, FONT_SMALL, WHITE | JUSTIFY_CENTER);
		if (ap_wshop.top + AP_WSHOP_VISIBLE < ctr_wumpa_shop.slots)
			DecalFont_DrawLine("v", 0x100, 0x48 + AP_WSHOP_VISIBLE * AP_WSHOP_ROW_H, FONT_SMALL,
			                   WHITE | JUSTIFY_CENTER);
	}

	if (ap_wshop.msgFrames > 0)
		DecalFont_DrawLine(ap_wshop.msg, 0x100, 0xc0, FONT_SMALL, WHITE | JUSTIFY_CENTER);
	else
		DecalFont_DrawLine("X BUY   TRIANGLE BACK", 0x100, 0xc0, FONT_SMALL, ORANGE | JUSTIFY_CENTER);

	if (gt != 0)
	{
		box.x = 0x18;
		box.y = 0x10;
		box.w = 0x1d0;
		box.h = 0xc0;
		RECTMENU_DrawInnerRect(&box, 0x10, gt->backBuffer->otMem.uiOT);
	}
}

int AP_WumpaShopMenuFrame(void)
{
	struct GameTracker *gt = sdata->gGT;
	int tap = sdata->buttonTapPerPlayer[0];
	int n;

	if (!ap_wshop.open)
		return 0;
	if (gt == 0 || !AP_WumpaShopRowShown() || !LOAD_IsOpen_AdvHub() ||
	    sdata->ptrActiveMenu != &data.menuGreenLoadSave || sdata->ptrDesiredMenu != 0 ||
	    AP_WumpaShopHubOfLevel(gt->levelID) != ap_wshop.hub)
	{
		AP_WShopClose();
		return 0;
	}

	// A purchase in flight settles when the room answers.
	if (ap_wshop.buying >= 0 && !ap_net_wumpa_pending())
	{
		if (ap_net_wumpa_bought(ap_wshop.buying))
		{
			AP_WShopSay("BOUGHT!");
			OtherFX_Play(0x41, 1);
		}
		else
			AP_WShopSay("SOMEONE SPENT THAT FRUIT FIRST, TRY AGAIN");
		ap_wshop.buying = -1;
	}
	if (ap_wshop.msgFrames > 0)
		ap_wshop.msgFrames--;

	if (tap & (BTN_TRIANGLE | BTN_CIRCLE | BTN_START))
	{
		OtherFX_Play(2, 1);
		AP_WShopClose();
		return 1;
	}
	n = AP_WumpaShopUsable(&ctr_wumpa_shop) ? ctr_wumpa_shop.slots : 0;
	if (n > 0 && (tap & (BTN_UP | BTN_DOWN)))
	{
		ap_wshop.cursor = (ap_wshop.cursor + ((tap & BTN_UP) ? n - 1 : 1)) % n;
		if (ap_wshop.cursor < ap_wshop.top)
			ap_wshop.top = ap_wshop.cursor;
		if (ap_wshop.cursor >= ap_wshop.top + AP_WSHOP_VISIBLE)
			ap_wshop.top = ap_wshop.cursor - AP_WSHOP_VISIBLE + 1;
		OtherFX_Play(0, 1);
	}
	if (tap & BTN_CROSS)
		AP_WShopBuy();

	AP_WShopDraw();
	return 1;
}

#endif // CTR_AP
