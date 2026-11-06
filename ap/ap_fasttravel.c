/* Hub fast travel (A3-16, 0.3.0-alpha3).
 *
 * The hub pause menu grows a TRAVEL row (ap_pauserow.h, MainFreeze.c) that
 * opens the five-row selector below. A confirmed row resumes the hub exactly
 * like a tracker start (MainFreeze_APResumeFromTracker), then the trip in
 * ap_fasttravel_logic.h waits for an idle hub, asks for the destination hub's
 * load and lets the level's own birth place the kart at the save screen
 * (VehBirth_TeleportSelf through AP_FastTravelSpawnAt). Every destination,
 * the current hub included, takes the same reload path. */
#include "ap_fasttravel.h"
#include "ap_fasttravel_logic.h"
#include "ap_tracker.h"  /* AP_TrackerLaunchPending, AP_HubSafeForRequest, MainFreeze_APResumeFromTracker */

static ap_ft_request ap_ft;
static int ap_ft_ready;
static int ap_ft_spawnHub = -1; /* armed destination for the next hub birth */
static int ap_ft_spawned;       /* the birth placed the kart */
static int ap_ft_yielded;       /* the birth kept its door or boss-scene spawn */

static ap_ft_request *AP_FTState(void)
{
	if (!ap_ft_ready)
	{
		AP_FTReset(&ap_ft);
		ap_ft_ready = 1;
	}
	return &ap_ft;
}

static void AP_FTLog(const char *what, const ap_ft_request *r)
{
	char line[160];
	snprintf(line, sizeof line, "[AP TRAVEL] %s: hub=%d\n", what, r->hub);
	AP_LogLine(line);
}

static int AP_FTLevelHub(void)
{
	struct GameTracker *gt = sdata ? sdata->gGT : 0;
	int hub;
	if (!gt || !(gt->gameMode1 & ADVENTURE_ARENA) || !LOAD_IsOpen_AdvHub())
		return -1;
	hub = gt->levelID - GEM_STONE_VALLEY;
	return hub >= 0 && hub < AP_FT_HUBS ? hub : -1;
}

int AP_FastTravelAvailable(void)
{
	return ctr_cfg_active() && AP_FTLevelHub() >= 0;
}

int AP_FastTravelPending(void)
{
	return AP_FTState()->stage != AP_FT_IDLE;
}

/* ── the hub selector ───────────────────────────────────────────────────────
 * A plain RectMenu in the pause menu's shape (menuQuit): one row per hub in
 * hub order, retail hub names, a locked row for a hub whose door Keys are
 * missing (the 0x8000 row bit: grey, womp, never confirmed). Row index == hub
 * index, so the proc needs no mapping. */
static struct MenuRow ap_ft_rows[AP_FT_HUBS + 1];

static void AP_FastTravelMenuProc(struct RectMenu *menu);

static struct RectMenu ap_ft_menu = {
	.stringIndexTitle = 0xFFFF,
	.posX_curr = 0x100,
	.posY_curr = 0x6c,
	.state = 0x8C83,
	.rows = &ap_ft_rows[0],
	.funcPtr = AP_FastTravelMenuProc,
	.drawStyle = 4,
};

static void AP_FastTravelMenuProc(struct RectMenu *menu)
{
	int row;
	if (menu->unk1e != 0)
	{
		/* per-frame state while the selector is up: the pause pages are gone,
		 * as for the QUIT menu */
		MainFreeze_SafeAdvDestroy();
		return;
	}
	row = menu->rowSelected;
	/* Back, or a begin that refused (the gate closed since the menu was
	 * drawn): return to the pause menu. */
	if (row < 0 || row >= AP_FT_HUBS || !ctr_cfg_active() ||
	    !AP_FTBegin(AP_FTState(), row, AP_GateCount(AP_IDX_KEY), AP_TrackerLaunchPending()))
	{
		sdata->ptrDesiredMenu = MainFreeze_GetMenuPtr();
		return;
	}
	AP_FTLog("begins", AP_FTState());
	/* Leave the pause exactly as a tracker start does; the trip runs from
	 * AP_FastTravelFrame on the unpaused hub. */
	MainFreeze_APResumeFromTracker();
}

void AP_FastTravelShowMenu(void)
{
	int i, keys = AP_GateCount(AP_IDX_KEY), cursor = -1;
	for (i = 0; i < AP_FT_HUBS; i++)
	{
		int open = AP_FT_HubOpen(i, keys);
		ap_ft_rows[i].stringIndex = (s16)((open ? 0 : 0x8000) | (LNG_GEM_STONE_VALLEY + i));
		ap_ft_rows[i].rowOnPressUp = (char)(i ? i - 1 : AP_FT_HUBS - 1);
		ap_ft_rows[i].rowOnPressDown = (char)(i + 1 < AP_FT_HUBS ? i + 1 : 0);
		ap_ft_rows[i].rowOnPressLeft = (char)i;
		ap_ft_rows[i].rowOnPressRight = (char)i;
		/* the cursor starts on the first hub the player can reach */
		if (open && cursor < 0)
			cursor = i;
	}
	ap_ft_rows[AP_FT_HUBS].stringIndex = -1;
	ap_ft_rows[AP_FT_HUBS].rowOnPressUp = ap_ft_rows[AP_FT_HUBS].rowOnPressDown = 0;
	ap_ft_rows[AP_FT_HUBS].rowOnPressLeft = ap_ft_rows[AP_FT_HUBS].rowOnPressRight = 0;
	ap_ft_menu.rowSelected = (s16)(cursor >= 0 ? cursor : 0);
	sdata->ptrDesiredMenu = &ap_ft_menu;
}

/* ── the trip ─────────────────────────────────────────────────────────────── */

void AP_FastTravelFrame(struct GameTracker *gt)
{
	static const char *const why[] = {"", "dropped (timeout)", "dropped (gate closed)",
		"dropped (no save screen spawn)", "the boss scene or door spawn kept its entry",
		"dropped (seed ended)"};
	ap_ft_request *r = AP_FTState(), copy = *r;
	int levelHub, hold, wantLoad, drop, spawned, yielded;
	if (r->stage == AP_FT_IDLE)
	{
		ap_ft_spawned = 0; /* a stale spawn flag dies with the trip that armed it */
		ap_ft_yielded = 0;
		return;
	}
	levelHub = AP_FTLevelHub();
	/* The same genuinely-idle-hub test a tracker start waits for, plus the
	 * character picker owning the screen. */
	hold = sdata->Loading.stage != LOAD_IDLE || !gt || levelHub < 0 || !AP_HubSafeForRequest(gt) ||
	       AP_CharSwap_PickerOpen();
	spawned = ap_ft_spawned;
	ap_ft_spawned = 0;
	yielded = ap_ft_yielded;
	ap_ft_yielded = 0;
	drop = AP_FTFrame(r, levelHub, hold, AP_GateCount(AP_IDX_KEY), ctr_cfg_active(), spawned, yielded, &wantLoad);
	if (drop != AP_FT_DROP_NONE)
	{
		/* a dropped trip keeps no armed spawn: the next ordinary hub load
		 * must not land the kart at a save screen */
		ap_ft_spawnHub = -1;
		AP_FTLog(why[drop], &copy);
		return;
	}
	if (spawned)
	{
		/* the destination's birth placed the kart at its save screen */
		AP_FTLog("arrived", &copy);
		return;
	}
	if (wantLoad)
	{
		/* Arm the birth before the load: the destination's VehBirth claims
		 * this spawn. */
		ap_ft_spawnHub = r->hub;
		MainRaceTrack_RequestLoad((s16)(GEM_STONE_VALLEY + r->hub));
		AP_FTLog("requests the hub", r);
	}
}

int AP_FastTravelSpawnAt(struct Level *level1, int levelID, SVec3 *posBottom, s16 *rotY)
{
	struct InstDef *instDef;
	int i, a, pos[3], out[3];
	if (ap_ft_spawnHub < 0 || levelID != GEM_STONE_VALLEY + ap_ft_spawnHub)
		return 0;
	/* One trip arms one spawn: disarm whatever happens below. */
	i = ap_ft_spawnHub;
	ap_ft_spawnHub = -1;
	/* The Key gate once more at the handoff: a reconnect that landed between
	 * the last frame check and this birth must not place the kart. */
	if (!AP_FT_HubOpen(i, AP_GateCount(AP_IDX_KEY)))
	{
		AP_LogLine("[AP TRAVEL] the Keys went away during the load; the kart keeps its normal spawn\n");
		return 0;
	}
	if (level1 == NULL)
		return 0;
	for (i = 0, instDef = level1->ptrInstDefs; i < (int)level1->numInstances; i++, instDef++)
		if (instDef->modelID == STATIC_SAVEOBJ)
			break;
	if (i >= (int)level1->numInstances)
	{
		AP_LogLine("[AP TRAVEL] WARNING: the destination hub has no save object; the kart keeps its normal spawn\n");
		return 0;
	}
	a = instDef->rot.y;
	pos[0] = instDef->pos.x;
	pos[1] = instDef->pos.y;
	pos[2] = instDef->pos.z;
	AP_FT_Landing(pos, MATH_Cos(a), -MATH_Sin(a), out);
	posBottom->x = (s16)out[0];
	posBottom->y = (s16)CTR_MipsAddLo(out[1], 0x80);
	posBottom->z = (s16)out[2];
	*rotY = (s16)AP_FT_Facing(a);
	ap_ft_spawned = 1;
	{
		char line[160];
		snprintf(line, sizeof line, "[AP TRAVEL] spawn: hub=%d save=(%d,%d,%d) landing=(%d,%d,%d)\n",
		         levelID - GEM_STONE_VALLEY, pos[0], pos[1], pos[2], out[0], out[1], out[2]);
		AP_LogLine(line);
	}
	return 1;
}

void AP_FastTravelSpawnYield(int levelID)
{
	if (ap_ft_spawnHub < 0 || levelID != GEM_STONE_VALLEY + ap_ft_spawnHub)
		return;
	ap_ft_spawnHub = -1;
	ap_ft_yielded = 1;
	AP_LogLine("[AP TRAVEL] the birth kept its door or boss-scene spawn; the trip ends at the normal spawn\n");
}
