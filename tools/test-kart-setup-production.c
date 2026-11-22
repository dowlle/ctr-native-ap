// cc -m32 -std=gnu99 -ffunction-sections -fdata-sections -Wl,--gc-sections -Wno-unused-function -DCTR_AP -DCTR_NATIVE -DBUILD=926 -Iinclude -I. -Iap -Iexternals/SDL/include tools/test-kart-setup-production.c -o /tmp/test-kart-setup-production -lm
/*
 * In-race Kart Setup (ruling of 2026-11-18: "The kart setup options
 * should essentially follow the character selector screen and have the same
 * rules as there.")
 *
 * Runs the PRODUCTION hub picker (ap/ap_charswap.c), the capability selector
 * (ap/ap_capability.c) and the race pause page (ap/ap_kartsetup.c) together,
 * with engine and network stubs only. Every case compares the race page with
 * the hub picker on the same seed state:
 *   - rows offered (read-only vanilla, editable global and per-racer,
 *     Progressive Stats and Boost) are the picker's rows for the driven racer;
 *   - one LEFT/RIGHT on either surface changes the one stored selection, with
 *     the same bounds (rank values, VERY LOW .. received ceiling) and the same
 *     persistence writes;
 *   - lowering a received Boost rank to NONE and restoring it stores FOLLOW
 *     again, so the next upgrade is followed; a lowered stat stays lowered;
 *   - the selection survives a physics refresh (driver re-birth), a reconnect
 *     (reset and data-storage restore) and is the hub's state afterwards;
 *   - a mid-race edit rewrites only the const_* stat fields: lap, timer and
 *     speed state are untouched, and PAUSE_1 stays set until RESUME;
 *   - the page owns Start while open (Back, never unpause), and only exists in
 *     a single-player Adventure race or cup pause menu.
 */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <common.h>
#include "ap/ap_hooks.h"
#include "ap/ap_net.h"
#include "ap/ap_capability.h"
#include "ap/ap_seedcfg.h"
#include "ap/ap_item_aliases.h"

#include "ap/ap_capability.c"
#include "ap/ap_charswap.c"
#include "ap/ap_kartsetup.c"
#include "ap/ap_charseat.c"

struct sData sdata_static;
struct Data data;
ctr_seed_config ctr_cfg;
static int recorded_stat_changes;
void AP_NavRec_NotePhysicsEffect(void) { recorded_stat_changes++; }
static int cfg_active = 1, dev_keys;
static struct GameTracker gt;
static struct Driver drv;
static struct GamepadSystem pads;
static struct DB db;
static char *lng[0x400];
static u32 colors[4];
static u32 icon_group_mem[0x200];

// ---- engine / platform stubs -------------------------------------------------
int ctr_cfg_active(void) { return cfg_active; }
int AP_DevKeysEnabled(void) { return dev_keys; }
void AP_LogLine(const char *s) { (void)s; }
static int sfx;
int OtherFX_Play(u32 id, int flags) { (void)id; (void)flags; sfx++; return 0; }
static int setconsts_calls;
// Same loop as game/Vehicle/VehBirth.c VehBirth_SetConsts, including its AP
// post-pass: it writes every metaPhys offset and nothing else.
void VehBirth_SetConsts(struct Driver *d)
{
	int engineID = data.MetaDataCharacters[data.characterIDs[d->driverID]].engineID;
	setconsts_calls++;
	for (int i = 0; i < 65; i++)
	{
		struct MetaPhys *m = &data.metaPhys[i];
		u8 *dst = (u8 *)d + m->offset;
		u32 raw = (u32)m->value[engineID];
		if (m->size == 1)
			dst[0] = (u8)raw;
		else if (m->size == 2)
		{
			dst[0] = (u8)raw;
			dst[1] = (u8)(raw >> 8);
		}
	}
	AP_CharSwap_ApplyStatPackage(d);
}
int AP_TrapWeakenedActive(struct Driver *d) { (void)d; return 0; }
void MainFreeze_MenuPtrDefault(struct RectMenu *m) { (void)m; }
static int syncs;
int MainFreeze_APKartSetupSync(void) { syncs++; return 0; }
void RECTMENU_ClearInput(void)
{
	sdata->AnyPlayerTap = 0;
	sdata->buttonTapPerPlayer[0] = 0;
}
char *NativeMenu_Labels(const char *t) { return (char *)t; }
static char last_lines[16][96];
static int line_count;
// Record hooks: a local capture build (KS_CAPTURE_HOOKS) supplies its own to
// replay the page's draw calls through the retail font; the harness ignores them.
#ifndef KS_CAPTURE_HOOKS
static void ks_record_text(const char *t, int x, int y, int font, int flags)
{ (void)t; (void)x; (void)y; (void)font; (void)flags; }
static void ks_record_box(char kind, int x, int y, int w, int h)
{ (void)kind; (void)x; (void)y; (void)w; (void)h; }
int DecalFont_GetLineWidth(char *t, s16 f) { (void)f; return (int)strlen(t) * 8; }
#endif
void DecalFont_DrawLine(char *t, int x, int y, s16 f, int flags)
{
	if (line_count < 16)
		snprintf(last_lines[line_count++], 96, "%s", t);
	ks_record_text(t, x, y, f, flags);
}
void RECTMENU_DrawInnerRect(RECT *r, int type, uint32_t *ot) { (void)type; (void)ot; ks_record_box('P', r->x, r->y, r->w, r->h); }
void AP_StatBar_Draw(int x, int y, int len, const unsigned int *c, void *ot, struct PrimMem *pm)
{ (void)c; (void)ot; (void)pm; ks_record_box('B', x, y, len, 7); }
void DecalHUD_Arrow2D(struct Icon *i, s16 x, s16 y, struct PrimMem *p, uint32_t *ot, u32 a, u32 b, u32 c, u32 d, char e,
                      int f, u16 g)
{ (void)i; (void)p; (void)ot; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; ks_record_box(g ? 'L' : 'R', x, y, 0, 0); }

// ---- AP data storage stubs ------------------------------------------------------
static int store_edit[AP_NET_EDITSTAT_COUNT], store_edit_known, edit_writes;
static unsigned store_edit_rev;
void ap_net_editstats_set(const int *v, int n)
{
	memcpy(store_edit, v, sizeof(int) * (size_t)n);
	store_edit_known = 1;
	edit_writes++;
}
int ap_net_editstats_known(int *out, int n)
{
	if (!store_edit_known)
		return 0;
	memcpy(out, store_edit, sizeof(int) * (size_t)n);
	return 1;
}
unsigned ap_net_editstats_revision(void) { return store_edit_rev; }
static int store_sel[AP_NET_STATSELECT_COUNT], store_sel_known, sel_writes;
static unsigned store_sel_rev;
void ap_net_statselect_set(const int *v, int n)
{
	memcpy(store_sel, v, sizeof(int) * (size_t)n);
	store_sel_known = 1;
	sel_writes++;
}
int ap_net_statselect_known(int *out, int n)
{
	if (!store_sel_known)
		return 0;
	memcpy(out, store_sel, sizeof(int) * (size_t)n);
	return 1;
}
unsigned ap_net_statselect_revision(void) { return store_sel_rev; }

// ---- fixture ------------------------------------------------------------------
const unsigned int AP_STATBAR_COLORS[7];
static int failures;
#define EXPECT(c, msg)                                                                                                \
	do                                                                                                                \
	{                                                                                                                 \
		if (c)                                                                                                        \
			printf("ok    %s\n", msg);                                                                                \
		else                                                                                                          \
		{                                                                                                             \
			printf("FAIL  %s  (line %d)\n", msg, __LINE__);                                                           \
			failures++;                                                                                               \
		}                                                                                                             \
	} while (0)

static void set_row(int i, int offset, int size, int a, int b, int c, int d)
{
	data.metaPhys[i].offset = offset;
	data.metaPhys[i].size = size;
	data.metaPhys[i].value[0] = a;
	data.metaPhys[i].value[1] = b;
	data.metaPhys[i].value[2] = c;
	data.metaPhys[i].value[3] = d;
}

static void reset_world(void)
{
	memset(&data, 0, sizeof data);
	memset(&sdata_static, 0, sizeof sdata_static);
	memset(&ctr_cfg, 0, sizeof ctr_cfg);
	memset(&gt, 0, sizeof gt);
	memset(&drv, 0, sizeof drv);
	// Unused metaPhys rows point at a scratch field so the SetConsts loop has a
	// harmless home for them.
	for (int i = 0; i < 65; i++)
		set_row(i, (int)offsetof(struct Driver, const_Accel_ClassStat), 0, 0, 0, 0, 0);
	// Class order BALANCED, ACCEL, SPEED, TURN.
	set_row(0, (int)offsetof(struct Driver, const_Speed_ClassStat), 2, 12800, 12400, 13600, 12000);
	set_row(1, (int)offsetof(struct Driver, const_AccelSpeed_ClassStat), 2, 14300, 13900, 15100, 13500);
	set_row(2, (int)offsetof(struct Driver, const_Accel_ClassStat), 2, 448, 512, 384, 416);
	set_row(3, (int)offsetof(struct Driver, const_TurnRate), 1, 26, 25, 24, 28);
	for (int c = 0; c < 16; c++)
		data.MetaDataCharacters[c].engineID = (s16)(c % 4);
	data.characterIDs[0] = CRASH_BANDICOOT;
	gt.drivers[0] = &drv;
	drv.driverID = 0;
	gt.numPlyrCurrGame = 1;
	gt.gameMode1 = ADVENTURE_MODE;
	sdata->gGT = &gt;
	sdata->Loading.stage = LOAD_IDLE;
	for (int i = 0; i < 0x400; i++)
		lng[i] = (char *)"NAME";
	sdata->lngStrings = lng;
	gt.backBuffer = &db;
	gt.iconGroup[4] = (struct IconGroup *)icon_group_mem;
	for (int i = 0; i < NUM_COLORS; i++)
		data.ptrColor[i] = colors;
	sdata->gGamepads = &pads;
	ctr_cfg.character_phase_present = 1;
	ctr_cfg.character_unlocks = 0;
	AP_CapabilityReset();
	AP_CapabilitySelectConnectReset();
	memset(ap_cs_editGlobal, 0, sizeof ap_cs_editGlobal);
	memset(ap_cs_editPerChar, 0, sizeof ap_cs_editPerChar);
	ap_cs_editRestoredRev = 0;
	store_edit_known = store_sel_known = 0;
	edit_writes = sel_writes = 0;
	memset(&ap_ks, 0, sizeof ap_ks);
}

// The hub picker's rows for a racer, through the picker's own cursor.
static unsigned hub_rows(int characterID)
{
	ap_cs_cursor = ap_cs_tileForCharacter(characterID);
	return ap_cs_panelRowsAvailable(characterID);
}

// One hub LEFT/RIGHT on panel row `row` for racer `characterID`, exactly as the
// picker's edit focus does it.
static void hub_press(int characterID, int row, int dir)
{
	ap_cs_cursor = ap_cs_tileForCharacter(characterID);
	ap_cs_panelRow = row;
	ap_cs_adjust(&gt, dir);
}

static int shown(int stat, int characterID) { return ap_cs_effectiveValue(stat, characterID); }

// Open the race pause menu and the Kart Setup page through the real request.
static void pause_race(struct RectMenu *menu)
{
	gt.gameMode1 |= PAUSE_1;
	menu->funcPtr = MainFreeze_MenuPtrDefault;
	menu->state = 0;
	sdata->ptrActiveMenu = menu;
	sdata->ptrDesiredMenu = NULL;
}
static int frame(int tap)
{
	sdata->buttonTapPerPlayer[0] = tap;
	sdata->AnyPlayerTap = tap;
	line_count = 0;
	return AP_KartSetupMenuFrame();
}

#define ROW_BOOST 0
#define ROW_SPEED 1
#define ROW_ACCEL 2
#define ROW_TURN 3
#define STAT_ACCEL 0
#define STAT_SPEED 1
#define STAT_TURN 3

int main(void)
{
	reset_world();
	ctr_cfg.stats_mode = AP_CAP_MODE_SHARED_GLOBAL;
	ctr_cfg.boost_mode = AP_CAP_MODE_SHARED_GLOBAL;
	AP_CapabilityStats(&drv);
	recorded_stat_changes = 0;
	AP_CapabilityStats(&drv);
	EXPECT(recorded_stat_changes == 0, "identical physics writes keep the current lap clean");
	AP_CapabilityReceive(AP_CAP_CHAIN_TOP_SPEED);
	AP_CapabilityStats(&drv);
	EXPECT(recorded_stat_changes == 1, "a live increase in effective Top Speed dirties the lap");
	hub_press(CRASH_BANDICOOT, ROW_SPEED, -1);
	AP_CapabilityStats(&drv);
	recorded_stat_changes = 0;
	AP_CapabilityReceive(AP_CAP_CHAIN_TOP_SPEED);
	AP_CapabilityStats(&drv);
	EXPECT(recorded_stat_changes == 0, "receiving a ceiling increase above a fixed selection keeps the lap clean");
	AP_CapabilityReceive(AP_CAP_CHAIN_BOOST);
	AP_CapabilityStats(&drv);
	EXPECT(recorded_stat_changes == 1, "a live increase in the effective Progressive Boost tier dirties the lap");
	AP_CapabilityStats(&drv);
	EXPECT(recorded_stat_changes == 1, "holding the same effective boost tier keeps the lap clean");

	// ── Row gate: single-player Adventure race or cup only ──
	reset_world();
	EXPECT(AP_KartSetupRowLive(), "Adventure race with the character phase carries KART SETUP");
	gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_CUP;
	EXPECT(AP_KartSetupRowLive(), "Adventure cup race carries KART SETUP");
	gt.gameMode1 = ADVENTURE_ARENA;
	EXPECT(!AP_KartSetupRowLive(), "hub keeps SELECT CHARACTER only");
	gt.gameMode1 = RELIC_RACE;
	EXPECT(AP_KartSetupRowLive(), "Relic Race without ADVENTURE_MODE still drives the room's stats: row");
	gt.gameMode1 = ARCADE_MODE;
	EXPECT(!AP_KartSetupRowLive(), "Arcade (vanilla stats room option) has no KART SETUP");
	gt.gameMode1 = ARCADE_MODE | RELIC_RACE;
	EXPECT(!AP_KartSetupRowLive(), "Arcade with a stale Relic bit has no KART SETUP");
	gt.gameMode1 = TIME_TRIAL;
	EXPECT(!AP_KartSetupRowLive(), "Time Trial has no KART SETUP");
	gt.gameMode1 = ADVENTURE_MODE;
	gt.numPlyrCurrGame = 2;
	EXPECT(!AP_KartSetupRowLive(), "two local players: no KART SETUP");
	gt.numPlyrCurrGame = 1;
	cfg_active = 0;
	EXPECT(!AP_KartSetupRowLive(), "no seed and no dev keys: no KART SETUP (the picker's gate)");
	cfg_active = 1;

	// ── Read-only stats (vanilla source): same as the hub's read-only panel ──
	reset_world();
	EXPECT(AP_CharSwap_KartSetupRows() == 0 && hub_rows(CRASH_BANDICOOT) == 0, "vanilla stats are read-only in both");
	pause_race(&data.menuAdvRace);
	AP_KartSetupRequestOpen();
	EXPECT(frame(BTN_CROSS_one) == 1 && AP_KartSetupOpen(), "row request opens the page inside the pause");
	EXPECT((sdata->AnyPlayerTap & BTN_START) == 0, "opening frame owns Start");
	{
		int s0 = shown(STAT_SPEED, CRASH_BANDICOOT);
		frame(BTN_RIGHT);
		EXPECT(shown(STAT_SPEED, CRASH_BANDICOOT) == s0 && edit_writes == 0 && sel_writes == 0,
		       "LEFT/RIGHT changes nothing on a read-only panel");
		int footer = 0;
		for (int i = 0; i < line_count; i++)
			footer |= strcmp(last_lines[i], "^: BACK") == 0;
		EXPECT(footer, "read-only page shows only the Back footer");
	}
	frame(BTN_START);
	EXPECT(!AP_KartSetupOpen() && (gt.gameMode1 & PAUSE_1) && (sdata->AnyPlayerTap & BTN_START) == 0,
	       "Start goes back to the pause menu and does not unpause");
	EXPECT(frame(0) == 0, "pause menu owns the next frame again");
	EXPECT(syncs > 0, "every menu pass re-syncs the race pause rows");
	// A Relic Race without ADVENTURE_MODE pauses into the Arcade menu.
	gt.gameMode1 = RELIC_RACE | PAUSE_1;
	pause_race(&data.menuArcadeRace);
	AP_KartSetupRequestOpen();
	EXPECT(frame(BTN_CROSS_one) == 1 && AP_KartSetupOpen(), "page opens from the Arcade menu a Relic Race uses");
	frame(BTN_TRIANGLE);
	gt.gameMode1 = ADVENTURE_MODE;

	// ── Editable stats, global scope ──
	reset_world();
	ctr_cfg.stat_source = 2;
	ctr_cfg.stat_owner = AP_CS_MODE_GLOBAL;
	ctr_cfg.stat_editing_allowed = 1;
	EXPECT(AP_CharSwap_KartSetupRows() == 0xE && hub_rows(CRASH_BANDICOOT) == 0xE,
	       "editable: SPEED, ACCEL, TURN rows in both, no Boost row");
	VehBirth_SetConsts(&drv);
	drv.lapIndex = 1;
	drv.distanceToFinish_curr = 4321;
	drv.speedApprox = 9000;
	gt.elapsedEventTime = 77777;
	pause_race(&data.menuAdvRace);
	AP_KartSetupRequestOpen();
	frame(BTN_CROSS_one);
	EXPECT(ap_ks.row == ROW_SPEED, "page focuses the first editable row, as the picker does");
	{
		int before = shown(STAT_SPEED, CRASH_BANDICOOT);
		frame(BTN_RIGHT);
		int after = shown(STAT_SPEED, CRASH_BANDICOOT);
		EXPECT(after > before, "RIGHT raises SPEED one rank value");
		EXPECT(drv.const_Speed_ClassStat == after, "mid-race edit is applied to the live kart at once");
		EXPECT(drv.lapIndex == 1 && drv.distanceToFinish_curr == 4321 && drv.speedApprox == 9000 &&
		           gt.elapsedEventTime == 77777,
		       "lap progress, race timer and speed are untouched by the edit");
		EXPECT((gt.gameMode1 & PAUSE_1) != 0, "race stays paused while editing");
		EXPECT(edit_writes == 1, "edit persisted on the press, like the picker");
		EXPECT(shown(STAT_SPEED, COCO_BANDICOOT) == after, "global scope: every racer drives the edited value");
		// The hub sees the same stored value and steps from it.
		hub_press(CRASH_BANDICOOT, ROW_SPEED, -1);
		EXPECT(shown(STAT_SPEED, CRASH_BANDICOOT) == before, "hub LEFT steps back from the race page's edit");
		frame(BTN_RIGHT);
		EXPECT(shown(STAT_SPEED, CRASH_BANDICOOT) == after, "race page steps from the hub's edit");
	}
	// Bounds: the top and bottom rank values, as in the hub.
	for (int i = 0; i < 10; i++)
		frame(BTN_RIGHT);
	int top_race = shown(STAT_SPEED, CRASH_BANDICOOT);
	for (int i = 0; i < 10; i++)
		frame(BTN_LEFT);
	int bottom_race = shown(STAT_SPEED, CRASH_BANDICOOT);
	EXPECT(top_race == AP_CapabilityRankValueForOffset(ap_cs_stats[STAT_SPEED].driverOffset, 4),
	       "upper bound is VERY HIGH");
	EXPECT(bottom_race == AP_CapabilityRankValueForOffset(ap_cs_stats[STAT_SPEED].driverOffset, 0),
	       "lower bound is VERY LOW");
	for (int i = 0; i < 10; i++)
		hub_press(CRASH_BANDICOOT, ROW_SPEED, 1);
	EXPECT(shown(STAT_SPEED, CRASH_BANDICOOT) == top_race, "hub has the same upper bound");
	// Navigation in the picker's row order, wrapping.
	frame(BTN_DOWN);
	EXPECT(ap_ks.row == ROW_ACCEL, "DOWN: SPEED -> ACCEL");
	frame(BTN_DOWN);
	EXPECT(ap_ks.row == ROW_TURN, "DOWN: ACCEL -> TURN");
	frame(BTN_DOWN);
	EXPECT(ap_ks.row == ROW_SPEED, "DOWN wraps past the unavailable Boost row");
	frame(BTN_UP);
	EXPECT(ap_ks.row == ROW_TURN, "UP wraps");
	// Physics refresh: a re-birth (restart, next race) applies the stored edit.
	{
		int want = shown(STAT_SPEED, CRASH_BANDICOOT);
		drv.const_Speed_ClassStat = 1;
		VehBirth_SetConsts(&drv);
		EXPECT(drv.const_Speed_ClassStat == want, "physics refresh keeps the selection");
	}
	// Reconnect: stored package restores into the same slots.
	{
		int want = shown(STAT_SPEED, CRASH_BANDICOOT);
		memset(ap_cs_editGlobal, 0, sizeof ap_cs_editGlobal);
		drv.const_Speed_ClassStat = 1;
		store_edit_rev++;
		ap_cs_editRestore();
		EXPECT(shown(STAT_SPEED, CRASH_BANDICOOT) == want, "reconnect restores the selection");
		EXPECT(drv.const_Speed_ClassStat == want, "a restored package is applied to the running kart");
		// Slot switch to a slot with no stored package: the kart drops the tune.
		AP_CharSwap_ConnectReset();
		EXPECT(drv.const_Speed_ClassStat == shown(STAT_SPEED, CRASH_BANDICOOT) &&
		           drv.const_Speed_ClassStat == data.metaPhys[0].value[data.MetaDataCharacters[CRASH_BANDICOOT].engineID],
		       "a connect reset puts the running kart back on the panel's (vanilla) values");
	}

	// ── Editable stats, per-racer scope ──
	reset_world();
	ctr_cfg.stat_source = 2;
	ctr_cfg.stat_owner = AP_CS_MODE_PERCHAR;
	ctr_cfg.stat_editing_allowed = 1;
	pause_race(&data.menuAdvRace);
	AP_KartSetupRequestOpen();
	frame(BTN_CROSS_one);
	{
		int coco = shown(STAT_SPEED, COCO_BANDICOOT);
		int crash = shown(STAT_SPEED, CRASH_BANDICOOT);
		frame(BTN_RIGHT);
		EXPECT(shown(STAT_SPEED, CRASH_BANDICOOT) > crash, "per-racer: the driven racer changes");
		EXPECT(shown(STAT_SPEED, COCO_BANDICOOT) == coco, "per-racer: other racers keep their own setup");
		EXPECT(ap_cs_editPerChar[CRASH_BANDICOOT][STAT_SPEED] != 0, "stored in the hub's per-racer slot");
	}

	// ── Progressive Stats (shared) with progressive caps ──
	reset_world();
	ctr_cfg.stat_source = 1;
	ctr_cfg.stat_owner = AP_CS_MODE_GLOBAL;
	ctr_cfg.stats_mode = AP_CAP_MODE_SHARED_GLOBAL;
	EXPECT(AP_CharSwap_KartSetupRows() == 0 && hub_rows(CRASH_BANDICOOT) == 0,
	       "no stat items received: nothing to select in either");
	AP_CapabilityReceive(AP_CAP_CHAIN_TOP_SPEED);
	AP_CapabilityReceive(AP_CAP_CHAIN_TOP_SPEED);
	EXPECT(AP_CharSwap_KartSetupRows() == (1u << ROW_SPEED) && hub_rows(CRASH_BANDICOOT) == (1u << ROW_SPEED),
	       "two Top Speed items: only SPEED is selectable, in both");
	pause_race(&data.menuAdvRace);
	AP_KartSetupRequestOpen();
	frame(BTN_CROSS_one);
	frame(BTN_RIGHT);
	EXPECT(AP_CapabilityAppliedStatRankFor(AP_CAP_CHAIN_TOP_SPEED) == 2 && sel_writes == 0,
	       "cannot go above the received ceiling");
	frame(BTN_LEFT);
	EXPECT(AP_CapabilityAppliedStatRankFor(AP_CAP_CHAIN_TOP_SPEED) == 1 && sel_writes == 1,
	       "LEFT lowers the driven rank and persists it");
	EXPECT(AP_CapabilitySelectionLowered(AP_CAP_CHAIN_TOP_SPEED, CRASH_BANDICOOT), "the hub reads it as lowered");
	AP_CapabilityReceive(AP_CAP_CHAIN_TOP_SPEED);
	EXPECT(AP_CapabilityAppliedStatRankFor(AP_CAP_CHAIN_TOP_SPEED) == 1, "a lowered stat stays put on upgrade");
	hub_press(CRASH_BANDICOOT, ROW_SPEED, 1);
	hub_press(CRASH_BANDICOOT, ROW_SPEED, 1);
	EXPECT(AP_CapabilityAppliedStatRankFor(AP_CAP_CHAIN_TOP_SPEED) == 3 &&
	           !AP_CapabilitySelectionLowered(AP_CAP_CHAIN_TOP_SPEED, CRASH_BANDICOOT),
	       "hub restores the race page's lowered rank to the ceiling");
	// Reconnect: selection reset, ceiling replayed, stored selection restored.
	frame(BTN_LEFT);
	AP_CapabilityReset();
	AP_CapabilitySelectConnectReset();
	for (int i = 0; i < 3; i++)
		AP_CapabilityReceive(AP_CAP_CHAIN_TOP_SPEED);
	store_sel_rev++;
	AP_CapabilitySelectRestore();
	EXPECT(AP_CapabilityAppliedStatRankFor(AP_CAP_CHAIN_TOP_SPEED) == 2, "reconnect keeps the race page's lowering");

	// ── Progressive Boost: lower a received rank and restore it ──
	reset_world();
	ctr_cfg.boost_mode = AP_CAP_MODE_SHARED_GLOBAL;
	ctr_cfg.boost_blue_fire = 1;
	AP_CapabilityReceive(AP_CAP_CHAIN_BOOST);
	AP_CapabilityReceive(AP_CAP_CHAIN_BOOST);
	EXPECT(AP_CharSwap_KartSetupRows() == (1u << ROW_BOOST) && hub_rows(CRASH_BANDICOOT) == (1u << ROW_BOOST),
	       "received Boost: the Boost row is selectable, in both");
	pause_race(&data.menuAdvCup);
	AP_KartSetupRequestOpen();
	frame(BTN_CROSS_one);
	EXPECT(ap_ks.row == ROW_BOOST, "cup pause page focuses the Boost row");
	frame(BTN_LEFT);
	frame(BTN_LEFT);
	frame(BTN_LEFT);
	EXPECT(AP_CapabilityAppliedBoostTier() == AP_CAP_BOOST_NONE, "Boost lowered to NONE, not below");
	frame(BTN_RIGHT);
	frame(BTN_RIGHT);
	EXPECT(AP_CapabilityAppliedBoostTier() == AP_CAP_BOOST_USF &&
	           !AP_CapabilitySelectionLowered(AP_CAP_CHAIN_BOOST, CRASH_BANDICOOT),
	       "Boost restored to the received USF rank");
	AP_CapabilityReceive(AP_CAP_CHAIN_BOOST);
	EXPECT(AP_CapabilityAppliedBoostTier() == AP_CAP_BOOST_BLUEFIRE, "restored Boost follows the next upgrade");
	frame(BTN_TRIANGLE);
	EXPECT(!AP_KartSetupOpen() && (gt.gameMode1 & PAUSE_1), "Triangle goes back to the pause menu");

	// ── Per-character Boost follows the driven racer only ──
	reset_world();
	ctr_cfg.boost_mode = AP_CAP_MODE_PER_CHARACTER;
	AP_CapabilityReceivePerCharacter(0 * AP_CAP_CHAIN_COUNT + AP_CAP_CHAIN_BOOST); // Crash
	EXPECT(AP_CharSwap_KartSetupRows() == (1u << ROW_BOOST), "Crash has a Boost copy: Boost row");
	data.characterIDs[0] = COCO_BANDICOOT;
	EXPECT(AP_CharSwap_KartSetupRows() == 0 && hub_rows(COCO_BANDICOOT) == 0, "Coco has none: no row, in both");

	// ── Context loss closes the page ──
	reset_world();
	ctr_cfg.stat_source = 2;
	ctr_cfg.stat_owner = AP_CS_MODE_GLOBAL;
	ctr_cfg.stat_editing_allowed = 1;
	pause_race(&data.menuAdvRace);
	AP_KartSetupRequestOpen();
	frame(BTN_CROSS_one);
	gt.gameMode1 &= ~PAUSE_1;
	EXPECT(frame(0) == 1 && !AP_KartSetupOpen(), "an unpause closes the page (that frame swallowed)");
	EXPECT(frame(0) == 0, "and leaves the menu pass alone afterwards");
	// The row goes away while the page is open (a disconnect): the press of that
	// frame is swallowed rather than reaching RESUME underneath.
	pause_race(&data.menuAdvRace);
	AP_KartSetupRequestOpen();
	frame(BTN_CROSS_one);
	cfg_active = 0;
	EXPECT(frame(BTN_CROSS_one) == 1 && !AP_KartSetupOpen() && sdata->AnyPlayerTap == 0 &&
	           sdata->buttonTapPerPlayer[0] == 0,
	       "losing the row closes the page and swallows that frame's press");
	EXPECT(frame(BTN_CROSS_one) == 0, "the pause menu has input again on the next frame");
	cfg_active = 1;
	// Same when the row disappears between its confirm and the page opening.
	pause_race(&data.menuAdvRace);
	AP_KartSetupRequestOpen();
	cfg_active = 0;
	EXPECT(frame(BTN_CROSS_one) == 1 && !AP_KartSetupOpen() && sdata->AnyPlayerTap == 0,
	       "a pending open that loses its row also swallows that frame's press");
	cfg_active = 1;
	pause_race(&data.menuAdvRace);
	sdata->ptrActiveMenu = &data.menuQuit;
	AP_KartSetupRequestOpen();
	frame(BTN_CROSS_one);
	EXPECT(!AP_KartSetupOpen(), "never opens over another menu");
	EXPECT(frame(BTN_CROSS_one) == 0 && !AP_KartSetupOpen(), "the other menu keeps its input");

	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
