#ifndef AP_WUMPA_SHOP_MENU_H
#define AP_WUMPA_SHOP_MENU_H

// The hub save screen's green load/save menu with the #249 WUMPA SHOP row
// first (ruling of 2026-11-02): WUMPA SHOP, LOAD, SAVE, DELETE, EXIT. Retail rows
// are LOAD 0, SAVE 1, DELETE 2, EXIT 3; with the shop row each sits one lower,
// and the menu procedure maps it back so retail code sees its own row.

// MenuRow.stringIndex of the WUMPA SHOP row. No retail string index comes near
// it; RECTMENU_RowString resolves it, like AP_LNG_TRACKER (0x7f00).
#define AP_LNG_WUMPA_SHOP 0x7f01

#define AP_WSHOP_GREEN_SHOP 0
#define AP_WSHOP_GREEN_EXIT 4
#define AP_WSHOP_GREEN_ROWS 5
// AP_WumpaShopGreenMenuRow's answer when the row was the shop's own.
#define AP_WSHOP_GREEN_HANDLED (-2)

// The rows as MenuRow initialisers {stringIndex, up, down, left, right}. Same
// wiring as retail: up and down step, left and right stay.
#define AP_WSHOP_GREEN_ROWS_INIT                                   \
	{                                                              \
		{AP_LNG_WUMPA_SHOP, 0, 1, 0, 0}, /* WUMPA SHOP */          \
		{0xCB, 0, 2, 1, 1},              /* LOAD */                \
		{0xCC, 1, 3, 2, 2},              /* SAVE */                \
		{0xCD, 2, 4, 3, 3},              /* DELETE */              \
		{0xCE, 3, 4, 4, 4},              /* EXIT */                \
		{-1, 0, 0, 0, 0},                                          \
	}

// A shop menu row as the retail row it stands for: the shop row has none
// (AP_WSHOP_GREEN_HANDLED), a negative row (backed out) stays as it is.
static inline int AP_WumpaShopGreenToRetail(int row)
{
	if (row == AP_WSHOP_GREEN_SHOP)
		return AP_WSHOP_GREEN_HANDLED;
	return row > AP_WSHOP_GREEN_SHOP ? row - 1 : row;
}

// A retail row as its row in the shop menu, so the cursor stays on the same
// entry when the shop row is added.
static inline int AP_WumpaShopRetailToGreen(int row)
{
	return row >= 0 ? row + 1 : row;
}

#endif
