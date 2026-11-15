#ifndef AP_TIZI_MADNESS_LOGIC_H
#define AP_TIZI_MADNESS_LOGIC_H

// Pure rules of the Tizi Madness Mask (A4-96), with no engine, network or
// config dependency, so ap_tizi_madness.c and the host harness
// (tools/test-tizi-madness.c) run the exact same rules.
//
// THE RULING (ruling of 2026-11-14, 23:03 to 23:06 CET). Tizi Madness is a special
// training track for the Tiziano shortcut. While it is raced, in any mode, the
// player always holds a Mask: the weapon slot is refilled with a Mask whenever
// it is empty or the Mask was used, no box needed. Item boxes otherwise work as
// usual. "On itemsanity seeds it would require the mask item, and on
// non-itemsanity seeds it will just keep giving you masks as well."

// Engine held-item ids. 0x7 is the Mask (Aku Aku / Uka Uka, the same id the
// Tizi Helper hands out), 0xf an empty slot, 0x10 a roll in progress.
#define AP_TIZI_MADNESS_HELD_MASK 0x7
#define AP_TIZI_MADNESS_HELD_NONE 0xf

// Does the refill apply? Only on Tizi Madness. With Itemsanity on it waits for
// the Mask weapon item, the conservative reading of "require the mask item":
// the logic already asks for that item on every check of the track, so the
// refill and the logic agree. Itemsanity off, or no AP seed at all (the custom
// Arcade and Time Trial pages), applies at once.
static int AP_TiziMadnessRefillOn(int onTiziMadness, int itemsanityOn, int maskReceived)
{
	if (!onTiziMadness)
		return 0;
	return itemsanityOn ? (maskReceived != 0) : 1;
}

// Is the weapon slot free to take a Mask this frame? The same tests the Turbo
// Grant uses (ap_turbogrant.c AP_TurboGrantSlotFree), which are the engine's own
// refusals for a weapon box roll: an empty slot with nothing stacked, no item
// still expiring out of it, no fire committed this frame, no item-roll cloud and
// no N. Tropy clock being received. Holding anything, a Mask included, or a
// roll in progress (0x10) means the slot is not free, so a box roll that is
// already running finishes as normal.
static int AP_TiziMadnessSlotFree(int heldItemID, int numHeldItems, int noItemTimer,
                                  int fireRequested, int rollCloud, int clockReceive)
{
	return heldItemID == AP_TIZI_MADNESS_HELD_NONE && numHeldItems == 0 &&
	       noItemTimer == 0 && !fireRequested && !rollCloud && clockReceive == 0;
}

#endif // AP_TIZI_MADNESS_LOGIC_H
