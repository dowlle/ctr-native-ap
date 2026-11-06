// cc -m32 -std=gnu99 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DCTR_AP -DBUILD=926 -I ap -I include -I . tools/test-update-menu.c
// -lm -o /tmp/test-update-menu
//
// The signed updater's main-menu prompts (issue #301). The real ap/ap_update.c
// runs on a fake worker (the ap_update_* surface below) and a fake main menu:
//
// - prompts appear only on the settled top-level main menu: never in a race, a
//   hub, a load, or with a submenu open;
// - nothing downloads before CROSS; declining remembers the tag;
// - while Play Solo runs the offer waits, and a verified update says "Play Solo
//   is running." and installs nothing, after LATER and on a second visit too;
// - every failure names itself; failed verification uses the Rulebook's words;
// - a Steam launch never relaunches, a direct launch does;
// - the rollback copy is confirmed only when the new build reaches the menu.
#include <stdio.h>
#include "menu-label-stub.h"
#include <stdlib.h>
#include <string.h>
#include <common.h>
#include "ap_hooks.h"
#include "ap_version.h"
#include "ap_link.h"
#include "platform/native_config.h"
#include "ap_update.h"

struct sData sdata_static;
struct Data data;
NativeConfig g_config;

// ── the worker, faked ──
static int fake_status = AP_UPD_NONE;
static int fake_solo, fake_startup, fake_apply_result = 1, fake_relaunch = 1;
static int checks_started, downloads, cancels, dismissals, applies, relaunches, confirms, saves, quits;
static char checked_declined[32], base_dir[32];

void ap_update_set_asset_dir(const char *dir) { (void)dir; }
void ap_update_set_base_dir(const char *dir) { snprintf(base_dir, sizeof base_dir, "%s", dir); }
int ap_update_startup(const char *running_tag) { (void)running_tag; return fake_startup; }
void ap_update_confirm_started(void) { ++confirms; }
void ap_update_check_start(const char *running_tag, const char *declined)
{
	(void)running_tag;
	++checks_started;
	snprintf(checked_declined, sizeof checked_declined, "%s", declined);
}
int ap_update_status(char *tag, int tag_bytes, int *progress_permille)
{
	if (tag)
		snprintf(tag, (size_t)tag_bytes, "v9.9.9");
	if (progress_permille)
		*progress_permille = 500;
	return fake_status;
}
void ap_update_detail(char *out, int out_bytes) { snprintf(out, (size_t)out_bytes, "x"); }
void ap_update_download_start(void) { ++downloads; fake_status = AP_UPD_DOWNLOADING; }
void ap_update_cancel(void) { ++cancels; fake_status = AP_UPD_NONE; }
void ap_update_dismiss(void) { ++dismissals; fake_status = AP_UPD_NONE; }
int ap_update_solo_busy(void) { return fake_solo; }
int ap_update_apply(void)
{
	++applies;
	if (fake_solo)
		return -1;
	if (fake_apply_result == 1)
		fake_status = AP_UPD_INSTALLED;
	else if (fake_apply_result == 0)
		fake_status = AP_UPD_FAILED_INSTALL;
	return fake_apply_result;
}
int ap_update_relaunch(void) { ++relaunches; return fake_relaunch; }

// ── the game around it ──
static char screen[8][64];
static int lines;
void DecalFont_DrawLineOT(char *str, int posX, int posY, s16 fontType, int flags, uint32_t *ot)
{
	(void)posX; (void)posY; (void)fontType; (void)flags; (void)ot;
	if (lines < 8)
		snprintf(screen[lines++], sizeof screen[0], "%s", str);
}
void RECTMENU_DrawInnerRect(RECT *r, int x, uint32_t *ot) { (void)r; (void)x; (void)ot; }
void AP_AppendLog(const char *line) { (void)line; }
void NativeConfig_Save(void) { ++saves; }
NativeLinkHost *AP_LinkHost(void) { return NULL; }
void NativeLinkHost_ReleasePrimary(NativeLinkHost *host) { (void)host; }
const char *NativeAssets_GetAssetDir(void) { return "assets"; }
const char *NativeAssets_GetBaseDir(void) { return "/game"; }
int NativeSteamRoute_ParseGameId(const char *text, unsigned long long *out)
{
	if (!text || !*text)
		return 0;
	*out = 1;
	return 1;
}
#include <setjmp.h>
static jmp_buf quit_jump;
static int quit_armed;
void AP_Net_Shutdown(void)
{
	++quits;
	if (quit_armed)
		longjmp(quit_jump, 1);
}
#define exit(code) ((void)(code))

#include "ap/ap_update.c"
#undef exit

static int checks, failures;
static void expect(int good, const char *why)
{
	++checks;
	if (!good)
	{
		++failures;
		printf("FAIL: %s\n", why);
	}
}

static struct GameTracker gt;
static struct DB db;
static struct RectMenu menu;

static int shown(const char *text)
{
	for (int i = 0; i < lines; ++i)
		if (strstr(screen[i], text))
			return 1;
	return 0;
}

// One frame on the settled main menu. Returns what RECTMENU would see.
static int frame(int tap)
{
	lines = 0;
	sdata->frameCounter++;
	sdata->buttonTapPerPlayer[0] = (u32)tap;
	AP_UpdateTick(&gt);
	AP_UpdateMainMenuSettled(&menu);
	return AP_UpdateMenuFrame();
}

static void at_menu(void)
{
	gt.gameMode1 = MAIN_MENU;
	gt.levelID = MAIN_MENU_LEVEL;
	sdata->Loading.stage = LOAD_IDLE;
	sdata->ptrActiveMenu = &menu;
	sdata->ptrDesiredMenu = NULL;
	menu.state = 0;
}

static void reset_all(void)
{
	static const struct RectMenu zero;
	menu = zero;
	ap_upd_started = ap_upd_prompting = ap_upd_snoozed = ap_upd_finished = ap_upd_confirm_pending = 0;
	ap_upd_steam = 0;
	fake_solo = fake_startup = 0;
	fake_apply_result = fake_relaunch = 1;
	downloads = cancels = dismissals = applies = relaunches = confirms = saves = quits = checks_started = 0;
	g_config.updateCheck = true;
	g_config.updateDeclined[0] = 0;
	unsetenv("SteamGameId");
	at_menu();
}

static void test_gates(void)
{
	reset_all();
	fake_status = AP_UPD_OFFER;
	expect(frame(0) == 1 && shown("v9.9.9 AVAILABLE") && shown("You have " CTR_AP_COMPAT_VERSION),
	       "the offer shows on the settled main menu with both versions");
	expect(downloads == 0, "nothing downloads before the player confirms");

	gt.gameMode1 = 0;
	lines = 0;
	sdata->frameCounter++;
	AP_UpdateTick(&gt);
	expect(AP_UpdateMenuFrame() == 0 && lines == 0, "no prompt in a race or a hub");
	at_menu();
	gt.gameMode1 |= LOADING;
	lines = 0;
	sdata->frameCounter++;
	expect(AP_UpdateMenuFrame() == 0 && lines == 0, "no prompt while loading");
	at_menu();
	sdata->Loading.stage = 1;
	expect(frame(0) == 0 && lines == 0, "no prompt during a load stage");
	at_menu();
	gt.levelID = MAIN_MENU_LEVEL + 1;
	expect(frame(0) == 0 && lines == 0, "no prompt on another level");
	at_menu();
	menu.state = DRAW_NEXT_MENU_IN_HIERARCHY;
	expect(frame(0) == 0 && lines == 0, "no prompt with a submenu open");
	at_menu();
	sdata->ptrDesiredMenu = &menu;
	expect(frame(0) == 0 && lines == 0, "no prompt while the menu is changing");
	at_menu();
	lines = 0;
	sdata->frameCounter += 5;
	ap_upd_prompting = 0;
	expect(AP_UpdateMenuFrame() == 0 && lines == 0, "no prompt on a frame the menu did not settle");
}

static void test_offer(void)
{
	reset_all();
	fake_status = AP_UPD_OFFER;
	frame(0);
	expect(checks_started == 1, "the check starts once on launch");
	frame(0);
	expect(checks_started == 1, "and not again");
	frame(BTN_TRIANGLE);
	expect(strcmp(g_config.updateDeclined, "v9.9.9") == 0 && saves == 1 && dismissals == 1,
	       "declining saves the tag");
	expect(downloads == 0, "declining downloads nothing");

	reset_all();
	snprintf(g_config.updateDeclined, sizeof g_config.updateDeclined, "v1.2.3");
	fake_status = AP_UPD_NONE;
	frame(0);
	expect(strcmp(checked_declined, "v1.2.3") == 0, "the check gets the declined tag");

	reset_all();
	g_config.updateCheck = false;
	fake_status = AP_UPD_IDLE;
	frame(0);
	expect(checks_started == 0, "update_check off stops the check");

	reset_all();
	fake_status = AP_UPD_OFFER;
	frame(0);
	g_config.updateCheck = false;
	expect(frame(BTN_CROSS) == 0 && lines == 0 && downloads == 0, "update_check off hides an offer already found");

	reset_all();
	fake_status = AP_UPD_OFFER;
	frame(BTN_CROSS);
	expect(downloads == 1, "CROSS starts the download");
	expect(frame(0) == 1 && shown("DOWNLOADING") && shown("50 percent"), "progress is shown");
	frame(BTN_TRIANGLE);
	expect(cancels == 1, "the download can be cancelled");
	fake_status = AP_UPD_VERIFYING;
	expect(frame(0) == 1 && shown("VERIFYING"), "verification is shown");
}

static void test_solo(void)
{
	reset_all();
	fake_status = AP_UPD_OFFER;
	fake_solo = 1;
	expect(frame(0) == 0 && lines == 0, "the offer waits while Play Solo runs");
	expect(frame(BTN_CROSS) == 0 && downloads == 0, "and the menu keeps its input");
	fake_solo = 0;
	expect(frame(0) == 1, "the offer returns when Play Solo stops");

	reset_all();
	fake_status = AP_UPD_READY;
	fake_solo = 1;
	expect(frame(0) == 1 && shown("Play Solo is running.") && shown("Stop it to install the update."),
	       "RESTART says Play Solo is running");
	expect(frame(BTN_CROSS) == 1 && applies == 0, "CROSS installs nothing while Play Solo runs");
	frame(BTN_TRIANGLE);
	expect(frame(0) == 0, "LATER hides the prompt");
	gt.gameMode1 = 0;
	frame(0);
	at_menu();
	expect(frame(0) == 1 && shown("Play Solo is running."), "it returns on the next visit to the menu");
	fake_solo = 0;
	expect(frame(0) == 1 && shown("RESTART"), "RESTART appears once Play Solo stops");

	// The session starts between the frame that drew RESTART and the press.
	reset_all();
	fake_status = AP_UPD_READY;
	fake_solo = 1;
	AP_UpdateRestart();
	expect(applies == 1 && ap_upd_finished == 0 && fake_status == AP_UPD_READY && quits == 0,
	       "a refused install stays READY");
}

static void test_failures(void)
{
	static const struct { int status; const char *text; } rows[] = {
		{AP_UPD_FAILED_VERIFY, "update failed verification"},
		{AP_UPD_FAILED_DOWNLOAD, "UPDATE DOWNLOAD FAILED"},
		{AP_UPD_FAILED_INSTALL, "UPDATE NOT INSTALLED"},
		{AP_UPD_FAILED_RESTORE, "UPDATE FAILED HALFWAY"},
	};
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i)
	{
		reset_all();
		fake_status = rows[i].status;
		expect(frame(0) == 1 && shown(rows[i].text), rows[i].text);
		expect(rows[i].status == AP_UPD_FAILED_RESTORE ? shown(".old") : shown("Nothing was changed."),
		       "the failure says what happened to the files");
		frame(BTN_CROSS);
		expect(dismissals == 1 && frame(0) == 0, "the failure can be dismissed");
	}
	reset_all();
	fake_status = AP_UPD_CHECKING;
	expect(frame(0) == 0 && lines == 0, "a running check shows nothing");
	fake_status = AP_UPD_NONE;
	expect(frame(0) == 0 && lines == 0, "a failed check is silent");
}

static void test_restart(void)
{
	reset_all();
	fake_status = AP_UPD_READY;
	expect(frame(0) == 1 && shown("Verified and ready."), "a verified update asks for RESTART");
	quit_armed = 1;
	if (setjmp(quit_jump) == 0)
		frame(BTN_CROSS);
	quit_armed = 0;
	expect(applies == 1 && relaunches == 1 && quits == 1, "a direct launch starts the new build");
	expect(saves == 0, "installing never writes config.ini");
	expect(strcmp(base_dir, "/game") == 0, "the worker learns the base folder");

	reset_all();
	setenv("SteamGameId", "123", 1);
	fake_status = AP_UPD_READY;
	frame(0);
	expect(ap_upd_steam == 1, "the Steam launch is seen");
	frame(BTN_CROSS);
	expect(applies == 1 && relaunches == 0 && quits == 0, "a Steam launch never starts the build itself");
	expect(frame(0) == 1 && shown("from Steam"), "it asks to start from Steam");
	quit_armed = 1;
	if (setjmp(quit_jump) == 0)
		frame(BTN_CROSS);
	quit_armed = 0;
	expect(quits == 1, "CROSS quits");

	reset_all();
	fake_status = AP_UPD_READY;
	fake_relaunch = 0;
	frame(0);
	frame(BTN_CROSS);
	expect(relaunches == 1 && quits == 0 && frame(0) == 1 && shown("Start the game again"),
	       "a failed relaunch asks for a manual start");

	reset_all();
	fake_status = AP_UPD_READY;
	fake_apply_result = 0;
	frame(0);
	frame(BTN_CROSS);
	expect(relaunches == 0 && quits == 0 && frame(0) == 1 && shown("UPDATE NOT INSTALLED"),
	       "a failed install keeps running the old build");
}

static void test_first_start(void)
{
	reset_all();
	fake_status = AP_UPD_NONE;
	fake_startup = 1;
	gt.gameMode1 = 0;
	AP_UpdateTick(&gt);
	expect(confirms == 0, "the rollback copy stays until the main menu");
	at_menu();
	frame(0);
	expect(confirms == 1, "the main menu confirms the new build");
	frame(0);
	expect(confirms == 1, "once");

	reset_all();
	fake_startup = 0;
	frame(0);
	expect(confirms == 0, "a normal start confirms nothing");
}

int main(void)
{
	sdata->gGT = &gt;
	gt.backBuffer = &db;
	test_gates();
	test_offer();
	test_solo();
	test_failures();
	test_restart();
	test_first_start();
	reset_all(); fake_status = AP_UPD_OFFER;
	frame(BTN_CIRCLE); expect(downloads == 1, "Circle starts the download");
	frame(BTN_START); expect(cancels == 1, "Start cancels the download");
	reset_all(); fake_status = AP_UPD_OFFER;
	frame(BTN_START); expect(downloads == 0 && g_config.updateDeclined[0] != 0, "Start skips the version");
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
