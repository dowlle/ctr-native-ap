#include <platform/native_menu.h>
#ifdef CTR_AP

// ============================================================================
// The Wumpa Shop page (#249). Opened from the WUMPA SHOP row of a hub's green
// load/save menu. While open it owns input and drawing the way the enlarged
// tracker does (RECTMENU_ProcessState returns early), so the load/save menu
// underneath neither runs nor sees a press. Triangle or Start goes back to
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
#include "ap_text_clip.h"
#include "ap_trap_display_logic.h" // Trap Display: how a trap location looks

#define AP_WSHOP_VISIBLE 9
#define AP_WSHOP_ROW_H 0xd
#define AP_WSHOP_NAME_X 0x44
#define AP_WSHOP_RIGHT_X 0x1d0
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
	char hintedSeed[96]; // the seed and slot the hinted flags belong to
} ap_wshop = {.buying = -1};

// The green load/save rows with WUMPA SHOP first (ap_wumpa_shop_menu.h).
static struct MenuRow ap_wshop_green_rows[AP_WSHOP_GREEN_ROWS + 1] = AP_WSHOP_GREEN_ROWS_INIT;

int AP_WumpaShopRowShown(void)
{
	return ctr_cfg_active() && !ctr_cfg.schema_newer &&
	       ctr_wumpa_shop.state != AP_WUMPA_SHOP_ABSENT;
}

// Called right after SelectProfile_GetTrackID set the retail cursor (SAVE),
// including on a second visit with the shop rows still in place.
void AP_WumpaShopPrepareGreenMenu(struct RectMenu *menu)
{
	int showShop;
	if (menu == 0)
		return;
	showShop = AP_WumpaShopRowShown();
	menu->rowSelected = (s16)AP_WumpaShopOpeningRow(menu->rowSelected, showShop);
	if (showShop)
	{
		menu->rows = ap_wshop_green_rows;
	}
	else
		menu->rows = &data.rowsGreenLoadSave[0];
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
	char seed[40], slot[24], room[32], id[96];
	int n = 0, k;
	seed[0] = slot[0] = room[0] = '\0';
	ap_net_seed_name(seed, (int)sizeof seed);
	ap_net_slot_name(slot, (int)sizeof slot);
	ap_net_room_endpoint(room, (int)sizeof room);
	snprintf(id, sizeof id, "%s/%s/%s/%d", room, seed, slot, ap_net_self_slot());
	if (strcmp(id, ap_wshop.hintedSeed) != 0)
	{
		// Another seed or slot: its shops have not been hinted yet.
		memset(ap_wshop.hinted, 0, sizeof ap_wshop.hinted);
		snprintf(ap_wshop.hintedSeed, sizeof ap_wshop.hintedSeed, "%s", id);
	}
	if (!ctr_wumpa_shop.hints || ap_wshop.hinted[hub] || !ap_net_is_connected() ||
	    !AP_WumpaShopUsable(&ctr_wumpa_shop))
		return;
	for (k = 0; k < ctr_wumpa_shop.slots; k++)
	{
		long long code = ctr_wumpa_shop.slot[hub][k].code;
		unsigned flags = 0;
		int known, keep;
		if (ap_net_location_checked(code) || ap_net_wumpa_bought(code))
			continue;
		// A2-23: progression only. Items not scouted yet: try again next open.
		// The lookup is its own statement: C leaves argument order unspecified.
		known = ap_net_scout_known(code, NULL, NULL, &flags);
		keep = AP_HintFilter_Progression(ctr_cfg.shop_hints_progression_only, known, flags);
		if (keep == AP_HINT_LATER)
			return;
		if (keep == AP_HINT_KEEP)
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
	if (AP_WumpaShopGreenToRetail(row) != AP_WSHOP_GREEN_HANDLED)
		return AP_WumpaShopGreenToRetail(row);
	hub = gt ? AP_WumpaShopHubOfLevel(gt->levelID) : -1;
	if (hub < 0)
		return AP_WSHOP_GREEN_HANDLED;
	ap_wshop.open = 1;
	ap_wshop.hub = hub;
	ap_wshop.cursor = 0;
	ap_wshop.top = 0;
	DecalFont_ResetLineBox(TEXT_SLOT_SHOP);
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

// #249 tracker line for the hub at engine level `level` (decision 21,
// 2026-10-28). 0 when the seed has no usable shop or the wallet is not loaded.
int AP_WumpaShopHubTrackerLine(int level, char *out, int cap)
{
	int hub = AP_WumpaShopHubOfLevel(level), states[AP_WUMPA_SHOP_MAX_SLOTS], k;
	if (out == 0 || cap <= 0 || hub < 0 || !AP_WumpaShopUsable(&ctr_wumpa_shop))
		return 0;
	if (!ap_net_wumpa_ready())
		return 0;
	for (k = 0; k < ctr_wumpa_shop.slots; k++)
		states[k] = AP_WShopSlotState(hub, k);
	AP_WumpaShopTrackerLine(out, cap, AP_WumpaShopTallyStates(states, ctr_wumpa_shop.slots));
	return 1;
}

static void AP_WShopBuy(void)
{
	if (!AP_LocationChecksAllowed() || !(sdata->gGT->gameMode1 & ADVENTURE_ARENA))
		return;
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
		snprintf(line, sizeof line, "ON SALE AT %d COLLECTED", s->unlock);
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

// Width of `len` characters of `s` (the whole string for len < 0) in the shop's
// font, from the font's own tables.
static int AP_WShopTextWidth(const char *s, int len, void *ctx)
{
	(void)ctx;
	return DecalFont_GetLineWidthStrlen((char *)s, len, FONT_SMALL);
}

static void AP_WShopDrawRow(int hub, int k, int y, int selected)
{
	const AP_WumpaShopSlot *s = &ctr_wumpa_shop.slot[hub][k];
	char itemRaw[64], playerRaw[64], itemS[40], playerS[24], name[72], right[24];
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
	state = AP_WShopSlotState(hub, k);
	if (known)
	{
		const char *disguise = AP_TrapDisplayShopName(ctr_cfg.trap_display, ctr_cfg.trap_display_salt,
		                                            s->code, flags, state == AP_WUMPA_SLOT_SOLD);
		if (disguise)
			snprintf(itemS, sizeof itemS, "%s", disguise);
	}
	if (AP_CeremonyNameIsBlank(itemS))
		snprintf(itemS, sizeof itemS, "%s", "UNKNOWN ITEM");
	if (known && player != ap_net_self_slot() && playerS[0] != '\0')
		snprintf(name, sizeof name, "%s (%s)", itemS, playerS);
	else
		snprintf(name, sizeof name, "%s", itemS);
	color = known ? AP_ClassFontColor(AP_TrapDisplayShopFlags(ctr_cfg.trap_display, ctr_cfg.trap_display_salt,
	                                                          s->code, flags))
	              : WHITE;
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
	{
		// The name column ends one character cell before the price column, so
		// the name never prints over the price or SOLD. Unselected rows clip;
		// the selected row scrolls its whole name.
		int avail = AP_WSHOP_RIGHT_X - AP_WSHOP_NAME_X - AP_WShopTextWidth(right, -1, 0) -
		            AP_WShopTextWidth(" ", 1, 0);
		DecalFont_DrawLineBox(name, AP_WSHOP_NAME_X, y, avail, FONT_SMALL, color,
		    selected ? TEXT_MARQUEE : TEXT_ELLIPSIS, TEXT_SLOT_SHOP, NULL);
	}
	DecalFont_DrawLine(right, AP_WSHOP_RIGHT_X, y, FONT_SMALL,
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
			AP_WumpaShopHeaderLine(line, (int)sizeof line, ap_net_wumpa_balance(), ap_net_wumpa_lifetime());
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
			DecalFont_DrawLine("v", 0x1e0, 0xb0, FONT_SMALL,
			                   WHITE | JUSTIFY_CENTER);
	}

	if (ap_wshop.msgFrames > 0)
		DecalFont_DrawLine(ap_wshop.msg, 0x100, 0xc0, FONT_SMALL, WHITE | JUSTIFY_CENTER);
	else
		DecalFont_DrawLine(NativeMenu_Labels("*: BUY   ^: BACK"), 0x100, 0xc0, FONT_SMALL, ORANGE | JUSTIFY_CENTER);

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

	if (tap & (BTN_TRIANGLE | BTN_START))
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
		DecalFont_ResetLineBox(TEXT_SLOT_SHOP);
		OtherFX_Play(0, 1);
	}
	if (tap & (BTN_CROSS | BTN_CIRCLE))
		AP_WShopBuy();

	AP_WShopDraw();
	return 1;
}

#endif // CTR_AP
