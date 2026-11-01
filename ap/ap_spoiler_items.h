#ifndef AP_SPOILER_ITEMS_H
#define AP_SPOILER_ITEMS_H
// Spoiler Items (#459): receipt state for the two reveal items. The rules are
// in ap_spoiler_items_logic.h.

#define AP_WARP_PAD_PREVIEW_ITEM_INDEX 204 // 35010204
#define AP_BOX_COLOURS_ITEM_INDEX      205 // 35010205

// Both receipts are booleans rebuilt from zero by every fresh connect's
// authoritative ReceivedItems replay, like Tizi Helper's.
void AP_SpoilerItemsReset(void);
void AP_SpoilerItemsReceive(int itemIndex);

// The live answers for the current seed and receipts.
int AP_SpoilerPadPreviewShown(void);
int AP_SpoilerBoxColoursUnlocked(void);

#endif // AP_SPOILER_ITEMS_H
