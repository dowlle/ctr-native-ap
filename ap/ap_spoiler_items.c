#ifdef CTR_AP
#include "ap_spoiler_items.h"
#include "ap_spoiler_items_logic.h"
#include "ap_hooks.h"   // AP_LogLine
#include "ap_seedcfg.h" // ctr_cfg_active, ctr_cfg.spoiler_items

static int ap_spoiler_preview_received;
static int ap_spoiler_box_colours_received;

void AP_SpoilerItemsReset(void)
{
	ap_spoiler_preview_received = 0;
	ap_spoiler_box_colours_received = 0;
}

void AP_SpoilerItemsReceive(int itemIndex)
{
	if (itemIndex == AP_WARP_PAD_PREVIEW_ITEM_INDEX)
	{
		if (!ap_spoiler_preview_received)
			AP_LogLine("[AP SPOILER] Warp Pad Preview received: pad reward previews shown\n");
		ap_spoiler_preview_received = 1;
	}
	else if (itemIndex == AP_BOX_COLOURS_ITEM_INDEX)
	{
		if (!ap_spoiler_box_colours_received)
			AP_LogLine("[AP SPOILER] AP Box Colours received: AP item box colours unlocked\n");
		ap_spoiler_box_colours_received = 1;
	}
}

static int AP_SpoilerOn(void)
{
	return ctr_cfg_active() && ctr_cfg.spoiler_items;
}

int AP_SpoilerPadPreviewShown(void)
{
	return AP_Spoiler_PadPreviewShown(AP_SpoilerOn(), ap_spoiler_preview_received);
}

// AP Box Colours exists only on a seed with coloured AP boxes (box_locations
// and color_boxes_by_item, the apworld's creation rule). Without AP boxes there
// is no item to wait for, so nothing is locked; with colours off the row keeps
// OFF (SEED) because the seed check comes first (AP_BoxColour_RowStateWithItem).
int AP_SpoilerBoxColoursUnlocked(void)
{
	return AP_Spoiler_BoxColoursUnlocked(AP_SpoilerOn() && ctr_cfg.seed_box_locations,
	                                     ap_spoiler_box_colours_received);
}
#endif
