// Signed in-game updater, game-thread glue (issue #301). Part of the AP unity
// build (game/game_unity.h), after ap_link.c, so it uses the log, feed, menu
// and room-link helpers defined there.
//
// What the player sees: on the settled top-level main menu, and never in a
// race or a hub, a prompt offers a newer stable release once the background
// check found one. Nothing downloads before CONFIRM. The download shows its
// progress and can be cancelled. A verified update is staged beside the game
// and installed only on an explicit RESTART. A Steam launch is then told to
// start the game again from Steam (a direct relaunch would bypass the Steam
// shortcut and its controller setup); a direct launch starts the new build
// itself. The first start of the new build at the main menu deletes the
// rollback copy (<exe>.old).
//
// The trust decisions are not here: see ap/ap_update_core.h.

#if !defined(CTR_AP_AUTHORING)

#include "ap_update.h"
#include "platform/native_steam_route.h"
#include <platform/native_assets.h>

static struct RectMenu *ap_upd_menu;
static int ap_upd_menu_frame;
static int ap_upd_prompting;
static int ap_upd_started;
static int ap_upd_confirm_pending; // first start after an update: confirm at the menu
static int ap_upd_snoozed;         // READY deferred until the main menu is entered again
static int ap_upd_last_status = -1;
static int ap_upd_finished;        // 1 = installed, waiting for the QUIT press
static int ap_upd_steam;           // this process was started by Steam

static void AP_UpdateLog(const char *what)
{
	char detail[160];
	char line[224];
	ap_update_detail(detail, sizeof detail);
	snprintf(line, sizeof line, "[AP UPDATE] %s%s%s\n", what, detail[0] ? ": " : "", detail);
	AP_AppendLog(line);
}

void AP_UpdateTick(struct GameTracker *gGT)
{
	int status;

	if (!ap_upd_started)
	{
		unsigned long long steamId = 0;
		int startup;

		ap_update_set_asset_dir(NativeAssets_GetAssetDir());
		startup = ap_update_startup(CTR_AP_COMPAT_VERSION);

		ap_upd_started = 1;
		ap_upd_steam = NativeSteamRoute_ParseGameId(getenv("SteamGameId"), &steamId);
		if (startup == 1)
		{
			ap_upd_confirm_pending = 1;
			AP_AppendLog("[AP UPDATE] first start after an update; rollback copy kept until the main menu\n");
		}
		else if (startup == 2)
		{
			AP_AppendLog("[AP UPDATE] an earlier build was restored by hand\n");
		}
		else if (startup == 3)
		{
			AP_AppendLog("[AP UPDATE] an interrupted install was found; the previous files were restored\n");
		}
		else if (startup == 4)
		{
			AP_AppendLog("[AP UPDATE] an interrupted install was found and some files could not be restored; "
			             "see SETUP.md (files ending in .old)\n");
		}
		// [Archipelago] update_check = false turns the network check off.
		if (g_config.updateCheck)
			ap_update_check_start(CTR_AP_COMPAT_VERSION, g_config.updateDeclined);
	}

	status = ap_update_status(NULL, 0, NULL);
	if (status != ap_upd_last_status)
	{
		static const char *const names[] = {"idle", "checking", "no update", "offer", "downloading",
		                                    "verifying", "ready", "failed verification", "download failed",
		                                    "install failed", "installed", "restore failed"};
		if (status >= 0 && status < (int)(sizeof names / sizeof names[0]) && status != AP_UPD_DOWNLOADING)
			AP_UpdateLog(names[status]);
		ap_upd_last_status = status;
	}

	if (gGT != NULL && (gGT->gameMode1 & MAIN_MENU) == 0)
	{
		ap_upd_snoozed = 0;
		ap_upd_prompting = 0;
	}
}

void AP_UpdateMainMenuSettled(struct RectMenu *menu)
{
	ap_upd_menu = menu;
	ap_upd_menu_frame = sdata->frameCounter;
	if (ap_upd_confirm_pending)
	{
		ap_upd_confirm_pending = 0;
		ap_update_confirm_started();
		AP_AppendLog("[AP UPDATE] new build reached the main menu; rollback copy removed\n");
	}
}

// Same gate as the room-link prompt: settled top-level main menu, nothing loading.
static int AP_UpdateSafe(struct GameTracker *gGT)
{
	if (gGT == NULL || (gGT->gameMode1 & MAIN_MENU) == 0 || (gGT->gameMode1 & LOADING) != 0)
		return 0;
	if (gGT->levelID != MAIN_MENU_LEVEL || sdata->Loading.stage != LOAD_IDLE)
		return 0;
	if (ap_upd_menu == NULL || sdata->ptrActiveMenu != ap_upd_menu || sdata->ptrDesiredMenu != NULL)
		return 0;
	if ((ap_upd_menu->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0)
		return 0;
	return ap_upd_prompting || (sdata->frameCounter - ap_upd_menu_frame) <= 1;
}

static void AP_UpdateDraw(const char *title, const char *l1, const char *l2, const char *l3, const char *keys)
{
	struct GameTracker *gGT = sdata->gGT;
	uint32_t *ot = gGT->backBuffer->otMem.uiOT;
	RECT bg = {0x30, 0x46, 0x1A0, 0x64};

	DecalFont_DrawLineOT((char *)title, 0x100, 0x50, FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
	if (l1)
		DecalFont_DrawLineOT((char *)l1, 0x100, 0x60, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	if (l2)
		DecalFont_DrawLineOT((char *)l2, 0x100, 0x6E, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	if (l3)
		DecalFont_DrawLineOT((char *)l3, 0x100, 0x7C, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	DecalFont_DrawLineOT((char *)keys, 0x100, 0x96, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	RECTMENU_DrawInnerRect(&bg, 4, ot);
}

static void AP_UpdateQuit(void)
{
	AP_Net_Shutdown();
	exit(0);
}

static void AP_UpdateRestart(void)
{
	if (!ap_update_apply())
		return; // FAILED_INSTALL: the next frame shows it; the running build is in place
	AP_UpdateLog("installed");
	NativeConfig_Save();
	if (!ap_upd_steam)
	{
		// The new process must be able to take this install's primary lock.
		NativeLinkHost_ReleasePrimary(AP_LinkHost());
		if (ap_update_relaunch())
		{
			AP_AppendLog("[AP UPDATE] started the new build\n");
			AP_UpdateQuit();
		}
		AP_AppendLog("[AP UPDATE] could not start the new build; asking for a manual start\n");
	}
	ap_upd_finished = 1;
}

int AP_UpdateMenuFrame(void)
{
	struct GameTracker *gGT = sdata->gGT;
	char tag[32];
	char title[64];
	char line[48];
	int permille = -1;
	int tap;
	int status;

	if (!ap_upd_started || gGT == NULL || !AP_UpdateSafe(gGT))
	{
		ap_upd_prompting = 0;
		return 0;
	}

	status = ap_update_status(tag, sizeof tag, &permille);
	tap = sdata->buttonTapPerPlayer[0];

	if (ap_upd_finished)
	{
		ap_upd_prompting = 1;
		gGT->demoCountdownTimer = 900;
		AP_UpdateDraw("UPDATE INSTALLED", "Start the game again",
		              ap_upd_steam ? "from Steam to use it." : "to use it.", NULL, "*: QUIT");
		if (tap & BTN_CROSS)
			AP_UpdateQuit();
		return 1;
	}

	switch (status)
	{
	case AP_UPD_OFFER:
		snprintf(title, sizeof title, "UPDATE %s AVAILABLE", tag);
		snprintf(line, sizeof line, "You have %s.", CTR_AP_COMPAT_VERSION);
		ap_upd_prompting = 1;
		gGT->demoCountdownTimer = 900;
		AP_UpdateDraw(title, line, "Updates this game client.", "Hosts need the new apworld.",
		              "*: DOWNLOAD   ^: SKIP VERSION");
		if (tap & BTN_CROSS)
		{
			AP_UpdateLog("player confirmed the download");
			ap_update_download_start();
		}
		else if (tap & BTN_TRIANGLE)
		{
			snprintf(g_config.updateDeclined, sizeof g_config.updateDeclined, "%s", tag);
			NativeConfig_Save();
			ap_update_dismiss();
			AP_AppendLog("[AP UPDATE] player declined this version\n");
		}
		return 1;

	case AP_UPD_DOWNLOADING:
	case AP_UPD_VERIFYING:
		snprintf(title, sizeof title, "%s %s", status == AP_UPD_VERIFYING ? "VERIFYING" : "DOWNLOADING", tag);
		if (status == AP_UPD_VERIFYING)
			snprintf(line, sizeof line, "Checking the signature.");
		else if (permille >= 0)
			snprintf(line, sizeof line, "%d percent", permille / 10);
		else
			snprintf(line, sizeof line, "Connecting.");
		ap_upd_prompting = 1;
		gGT->demoCountdownTimer = 900;
		AP_UpdateDraw(title, line, NULL, NULL, "^: CANCEL");
		if (tap & BTN_TRIANGLE)
		{
			ap_update_cancel();
			AP_AppendLog("[AP UPDATE] player cancelled the download\n");
		}
		return 1;

	case AP_UPD_READY:
		if (ap_upd_snoozed)
			break;
		snprintf(title, sizeof title, "UPDATE %s READY", tag);
		ap_upd_prompting = 1;
		gGT->demoCountdownTimer = 900;
		AP_UpdateDraw(title, "Verified and ready.", "Restart to install it.", NULL, "*: RESTART   ^: LATER");
		if (tap & BTN_CROSS)
			AP_UpdateRestart();
		else if (tap & BTN_TRIANGLE)
			ap_upd_snoozed = 1;
		return 1;

	case AP_UPD_FAILED_VERIFY:
	case AP_UPD_FAILED_DOWNLOAD:
	case AP_UPD_FAILED_INSTALL:
	case AP_UPD_FAILED_RESTORE:
		ap_upd_prompting = 1;
		gGT->demoCountdownTimer = 900;
		if (status == AP_UPD_FAILED_RESTORE)
			AP_UpdateDraw("UPDATE FAILED HALFWAY", "Some files could not be put back.",
			              "Copies of your old files end in .old.", "Next start tries again; see SETUP.md.",
			              "*: OK");
		else
			AP_UpdateDraw(status == AP_UPD_FAILED_VERIFY     ? "update failed verification"
			              : status == AP_UPD_FAILED_DOWNLOAD ? "UPDATE DOWNLOAD FAILED"
			                                                 : "UPDATE NOT INSTALLED",
			              "Nothing was changed.", NULL, NULL, "*: OK");
		if (tap & BTN_CROSS)
			ap_update_dismiss();
		return 1;

	default:
		break;
	}

	ap_upd_prompting = 0;
	return 0;
}

#else // CTR_AP_AUTHORING: the box authoring download never updates itself.

void AP_UpdateTick(struct GameTracker *gGT)
{
	(void)gGT;
}

void AP_UpdateMainMenuSettled(struct RectMenu *menu)
{
	(void)menu;
}

int AP_UpdateMenuFrame(void)
{
	return 0;
}

#endif
