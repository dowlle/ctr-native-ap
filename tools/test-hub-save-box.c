// cc -std=c99 -Wall -Wextra -DCTR_AP -I ap -o /tmp/test-hub-save-box tools/test-hub-save-box.c && /tmp/test-hub-save-box
//
// A4-58: the hub save box never outlives its hub. Two player reports:
// B1 (alpha3): pause near the save screen, TRAVEL; the box opens while the
// trip's load is pending, stays up in the next hub over a kart that drives,
// rides a warp pad into the Purple Gem Cup and crashes on EXIT there.
// B2 (alpha2): pause near the save screen, SELECT CHARACTER; the box opens
// under the picker, the hub reloads, and the profile page shows over driving.
//
// The model below replays both with the decisions of ap_hub_savebox_logic.h
// standing in for the engine glue (ap_hub_savebox.c; wiring checked by
// tools/test-hub-save-box-production.py). Build with -DA458_BEFORE to replay
// the behaviour before the fix: every scenario check fails.
#include <stdio.h>
#include "ap_hub_savebox_logic.h"

static int failures;
#define CHECK(label, expr) do { int ok_ = (expr); printf("%s  %s\n", ok_ ? "ok  " : "FAIL", label); failures += !ok_; } while (0)

#ifdef A458_BEFORE
#define MAY_OPEN(l, t, s, p) ((void)(l), (void)(t), (void)(s), (void)(p), 1)
#define LOAD_ACTIONS(h, a, d, i, s) ((void)(h), (void)(a), (void)(d), (void)(i), (void)(s), 0)
#else
#define MAY_OPEN AP_HubSaveBoxMayOpen
#define LOAD_ACTIONS AP_HubSaveBoxLoadActions
#endif

enum { MENU_NONE, MENU_GREEN, MENU_PROFILES, MENU_PAUSE, MENU_QUEUE_HUB };
enum { LEVEL_HOT_AIR = 7, LEVEL_GSV = 25, LEVEL_NSB = 26, LEVEL_RUINS = 27 };

// The engine state the bug lives in: globals that survive a level load, and
// the per-level save object that does not.
static struct
{
	int level;
	int loadIdle, travelPending, launchPending, pickerOpen;
	int active, desired;      // sdata->ptrActiveMenu / ptrDesiredMenu
	int iconsLive;            // sdata->ptrLoadSaveObj != 0
	int iconsLevel;           // the level whose pools hold those icons
	int shopOpen;
	int kartFrozen;           // the save object's VehBirth_NullThread hold
	// AH_SaveObj thread of the current level
	int flags, scan, camFrames;
	int inRange, slow;        // dist^2 <= 0x8ffff, speed < 0x80
	int staleDestroy;         // SelectProfile_Destroy ran on another level's pools
} g;

static void level_born(int level)
{
	g.level = level;
	g.flags = 0;
	g.scan = 0;
	g.camFrames = 0;
	g.kartFrozen = 0; // a new kart thread
	g.inRange = g.slow = 0;
}

// One AH_SaveObj_ThTick as it decides (camera travel shortened to 3 frames).
static void save_tick(void)
{
	int may = MAY_OPEN(g.loadIdle, g.travelPending, g.launchPending, g.pickerOpen);
	if (g.level < LEVEL_GSV)
		return; // no save object outside a hub
	if ((g.flags & 1) == 0)
	{
		if (!g.inRange || !may)
			return;
		if (g.slow && --g.scan < 0)
		{
			g.flags |= 1;
			g.kartFrozen = 1;
			g.camFrames = 3;
		}
		return;
	}
	if (g.camFrames > 0 && --g.camFrames == 0 && (g.flags & 2) == 0)
	{
		g.flags |= 2;
		if (!may)
			return; // camera returns, no menu
		g.active = MENU_GREEN;
		g.iconsLive = 1;
		g.iconsLevel = g.level;
	}
}

static void exit_row(void)
{
	// EXIT: RECTMENU_Hide + SelectProfile_Destroy
	if (g.iconsLive && g.iconsLevel != g.level)
		g.staleDestroy = 1;
	g.active = MENU_NONE;
	g.iconsLive = 0;
}

static void start_load(int level)
{
	int a = LOAD_ACTIONS(g.level >= LEVEL_GSV && g.level <= LEVEL_GSV + 4,
	                     g.active == MENU_GREEN || g.active == MENU_PROFILES,
	                     g.desired == MENU_GREEN || g.desired == MENU_PROFILES, g.iconsLive, g.shopOpen);
	if (a & AP_HSB_CLOSE_SHOP)
		g.shopOpen = 0;
	if (a & AP_HSB_CLEAR_DESIRED)
		g.desired = MENU_NONE;
	if (a & AP_HSB_CLEAR_ACTIVE)
		g.active = MENU_NONE;
	if (a & AP_HSB_DESTROY_ICONS)
		g.iconsLive = 0;
	level_born(level);
	g.loadIdle = 1;
}

// A load requested from the hub floor: the hub ticks a few frames under the
// incoming flag before the load starts.
static void request_and_load(int level)
{
	int i;
	g.loadIdle = 0;
	g.travelPending = g.launchPending = 0;
	for (i = 0; i < 40; i++)
		save_tick();
	start_load(level);
}

static void park_near_save_screen(void)
{
	g.inRange = 1;
	g.slow = 1;
	g.flags = 0;  // the kart left the range fast once: retail rearms
	g.scan = 15;
}

static void scenario_b1(void)
{
	int i;
	level_born(LEVEL_RUINS);
	g.loadIdle = 1;
	park_near_save_screen();
	// pause, TRAVEL: the trip waits for an idle hub, then requests the load
	g.travelPending = 1;
	for (i = 0; i < 40; i++)
		save_tick();
	CHECK("B1: the save screen stays shut while a TRAVEL trip is pending", g.active == MENU_NONE);
	request_and_load(LEVEL_NSB);
	CHECK("B1: no save box in N. Sanity Beach after TRAVEL", g.active == MENU_NONE);
	CHECK("B1: no load/save icon thread carried into N. Sanity Beach", !g.iconsLive);
	// the kart lands 1024 units out and drives off; a warp pad takes it
	g.shopOpen = g.active == MENU_GREEN; // the player opened the shop row
	request_and_load(LEVEL_HOT_AIR);
	CHECK("B1: no save box in the Purple Gem Cup", g.active == MENU_NONE);
	CHECK("B1: no Wumpa Shop page in the Purple Gem Cup", !g.shopOpen);
	if (g.active == MENU_GREEN)
		exit_row();
	CHECK("B1: EXIT never destroys another level's instances", !g.staleDestroy);
}

static void scenario_b2(void)
{
	int i;
	level_born(LEVEL_RUINS);
	g.loadIdle = 1;
	park_near_save_screen();
	// pause, SELECT CHARACTER: the picker owns the screen with the kart held
	g.pickerOpen = 1;
	for (i = 0; i < 40; i++)
		save_tick();
	CHECK("B2: the save screen stays shut under the character picker", g.active == MENU_NONE);
	g.pickerOpen = 0;
	// the swap reloads the hub; the player has pressed LOAD meanwhile
	if (g.active == MENU_GREEN)
		g.desired = MENU_PROFILES;
	request_and_load(LEVEL_RUINS);
	CHECK("B2: no profile page after the character reload", g.active != MENU_PROFILES && g.desired != MENU_PROFILES);
	CHECK("B2: no save box after the character reload", g.active == MENU_NONE);
}

// A box that is up when a load starts anyway (a request from elsewhere).
static void scenario_open_at_load(void)
{
	level_born(LEVEL_GSV);
	g.loadIdle = 1;
	g.active = MENU_GREEN;
	g.iconsLive = 1;
	g.iconsLevel = LEVEL_GSV;
	g.shopOpen = 1;
	start_load(LEVEL_HOT_AIR);
	CHECK("load start: an open save box closes with its hub", g.active == MENU_NONE && !g.iconsLive && !g.shopOpen);
}

static void scenario_approach_then_load(void)
{
	level_born(LEVEL_GSV);
	g.loadIdle = 1;
	park_near_save_screen();
	while ((g.flags & 1) == 0)
		save_tick();
	g.loadIdle = 0; // a load requested during the camera approach
	save_tick(); save_tick(); save_tick();
	CHECK("approach: a load requested mid-approach skips the menu", g.active == MENU_NONE);
	CHECK("approach: the camera goes back (flags & 2)", (g.flags & 2) != 0);
}

int main(void)
{
	// The decision tables themselves.
	CHECK("may open on an idle hub", AP_HubSaveBoxMayOpen(1, 0, 0, 0));
	CHECK("refused while a load is pending", !AP_HubSaveBoxMayOpen(0, 0, 0, 0));
	CHECK("refused while TRAVEL is pending", !AP_HubSaveBoxMayOpen(1, 1, 0, 0));
	CHECK("refused while a tracker start is pending", !AP_HubSaveBoxMayOpen(1, 0, 1, 0));
	CHECK("refused under the character picker", !AP_HubSaveBoxMayOpen(1, 0, 0, 1));
	CHECK("a load from a non-hub level touches nothing", AP_HubSaveBoxLoadActions(0, 1, 1, 1, 1) == 0);
	CHECK("a load from a hub with nothing open does nothing", AP_HubSaveBoxLoadActions(1, 0, 0, 0, 0) == 0);
	CHECK("a load from a hub closes every part",
	      AP_HubSaveBoxLoadActions(1, 1, 1, 1, 1) ==
	          (AP_HSB_CLEAR_ACTIVE | AP_HSB_CLEAR_DESIRED | AP_HSB_DESTROY_ICONS | AP_HSB_CLOSE_SHOP));
	CHECK("the profile thread alone is destroyed", AP_HubSaveBoxLoadActions(1, 0, 0, 1, 0) == AP_HSB_DESTROY_ICONS);

	scenario_b1();
	scenario_b2();
	scenario_open_at_load();
	scenario_approach_then_load();

	printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
	return failures != 0;
}
