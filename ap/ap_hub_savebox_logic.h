#ifndef AP_HUB_SAVEBOX_LOGIC_H
#define AP_HUB_SAVEBOX_LOGIC_H

// The hub save box never outlives its hub (A4-58). Freestanding so
// tools/test-hub-save-box.c pins it out of engine; the engine glue is
// ap/ap_hub_savebox.c and tools/test-hub-save-box-production.py checks it is
// wired.
//
// The hub save box is the green load/save menu (WUMPA SHOP / LOAD / SAVE /
// DELETE / EXIT), its four-slot profile page ("CHOOSE A GAME TO LOAD"), the
// Wumpa Shop page drawn over it, and the load/save icon thread SelectProfile
// keeps in sdata->ptrLoadSaveObj. AH_SaveObj_ThTick opens it when a slow kart
// is within 767 units of the hub's save screen.
//
// Two gaps let it survive a level change:
//
// 1. A hub keeps ticking after a level load is requested, until the
//    checkered flag covers the screen (MainMain, Loading.stage
//    LOAD_REQUESTED). A kart parked in the trigger range then opens the box
//    during that window. Retail never requests a load from the hub floor;
//    AP does: TRAVEL, the tracker's pad start and SELECT CHARACTER all
//    resume the hub first and then reload it.
// 2. Nothing closes the box when the load starts. sdata->ptrActiveMenu is a
//    global, so the menu stays active in the next level, over a kart that is
//    no longer frozen, and sdata->ptrLoadSaveObj points into the thread and
//    instance pools the load rebuilds. EXIT then runs SelectProfile_Destroy,
//    which kills whatever instances now sit in those slots.
//
// The fix answers both: the save screen does not wake while a load or an AP
// hub move is pending (AP_HubSaveBoxMayOpen), and the start of every load
// from a hub closes whatever part of the box is still up
// (AP_HubSaveBoxLoadActions).

// May the save screen begin its approach, or show its menu, this frame?
// loadIdle: sdata->Loading.stage == LOAD_IDLE. The pending flags are the AP
// requests that will reload or leave the hub: a TRAVEL trip, a tracker pad
// start, and the character picker (its commit reloads the hub).
static inline int AP_HubSaveBoxMayOpen(int loadIdle, int travelPending, int launchPending, int pickerOpen)
{
	return loadIdle && !travelPending && !launchPending && !pickerOpen;
}

// What the start of a level load must close. Bits of the answer:
enum
{
	AP_HSB_CLEAR_ACTIVE = 1,     // sdata->ptrActiveMenu is a save box menu: clear it
	AP_HSB_CLEAR_DESIRED = 2,    // sdata->ptrDesiredMenu is a save box menu: clear it
	AP_HSB_DESTROY_ICONS = 4,    // SelectProfile_Destroy while its thread is still valid
	AP_HSB_CLOSE_SHOP = 8        // the Wumpa Shop page
};

// fromHub: the level being left is an adventure hub. Other levels keep the
// retail flow untouched (the main menu's own profile pages load a hub through
// the same menus). activeIsBox / desiredIsBox: the menu pointer is the green
// load/save menu or the four-slot profile page. iconsLive:
// sdata->ptrLoadSaveObj != 0. shopOpen: the Wumpa Shop page is open.
static inline int AP_HubSaveBoxLoadActions(int fromHub, int activeIsBox, int desiredIsBox, int iconsLive, int shopOpen)
{
	int a = 0;
	if (!fromHub)
		return 0;
	if (activeIsBox)
		a |= AP_HSB_CLEAR_ACTIVE;
	if (desiredIsBox)
		a |= AP_HSB_CLEAR_DESIRED;
	if (iconsLive)
		a |= AP_HSB_DESTROY_ICONS;
	if (shopOpen)
		a |= AP_HSB_CLOSE_SHOP;
	return a;
}

#endif
