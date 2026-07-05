#ifdef CTR_AP

#include <common.h> // struct Driver / QuadBlock / Level / GameTracker, sdata, DRIVER_COLL_FLAG_*, TERRAIN_*, LevelID
#include <stdio.h>
#include <string.h>

#include "ap_shortcut.h"
#include "ap_hooks.h" // AP_LogLine

// ============================================================================
// SHORTCUTLESS -- implementation. See ap_shortcut.h for the design + provenance
// (ported from the GPL kkv0n/Penguin-MODSK ShortcutLess mode). Three layers:
//   1. off-road terrain      -> AP_QuadIsShortcut (per-touch, COLL.c hook)
//   2. checkpoint-% gap skip -> AP_ShortcutSkipTick (per-frame, AP_OnFrame)
//   3. per-track blockID table -> AP_QuadIsShortcut
// Enforcement reuses the engine mask-grab respawn. No persistent level mutation.
// ============================================================================

int Platform_InputRawKeyDown(int scancode); // native_input.c (CTR_AP only)

// Numpad, not F-keys: the engine's CTR_INTERNAL dev handlers own F1-F12
// (native_platform.c, F8 = savestate LOAD) and fire regardless of AP keys.
#define AP_SC_KEY_TOGGLE 95 // Numpad 7: dev toggle Shortcutless
#define AP_SC_KEY_MARK   96 // Numpad 8: log/mark current quad blockID

static int g_shortcutless = 0;   // enforcement toggle (the Shortcut Unlock item)
static int g_mode = AP_SC_ALLOWED;
static int g_allow_switching = 0; // allow_shortcut_switching (future in-game switch)
static int g_capture_log = 0;    // passive per-touch blockID logging (verify tool)

// Dev capture set: blockIDs marked live with Numpad 8, honoured by AP_QuadIsShortcut so a
// candidate table entry can be tested against REAL track data before it is baked in.
#define AP_SC_CAPTURE_CAP 64
static s16 g_captured[AP_SC_CAPTURE_CAP];
static int g_captured_n = 0;

static int g_last_logged_block = -1; // passive-log dedup

int AP_ShortcutlessActive(void)      { return g_shortcutless; }
int AP_ShortcutMode(void)            { return g_mode; }
int AP_ShortcutSwitchingAllowed(void){ return g_allow_switching; }

void AP_ShortcutlessSet(int on)
{
	if (g_shortcutless != (on ? 1 : 0))
	{
		g_shortcutless = on ? 1 : 0;
		AP_LogLine(g_shortcutless ? "[AP SHORTCUT] Shortcutless ON\n"
		                          : "[AP SHORTCUT] Shortcutless OFF\n");
	}
}

static struct Driver *AP_ScLocalDriver(void)
{
	if (sdata == 0 || sdata->gGT == 0)
		return 0;
	return sdata->gGT->drivers[0];
}

static int AP_ScLevelID(void)
{
	if (sdata == 0 || sdata->gGT == 0)
		return -1;
	return (int)sdata->gGT->levelID;
}

// ── Layer 1: off-road terrain ───────────────────────────────────────────────
// Grass / dirt / snow are off the racing line on every track; water is off-road
// only on Dragon Mines (elsewhere water is a normal surface). Matches Penguin-MODSK
// RemoveOffRoadCHK's terrain set.
static int AP_ScTerrainIsOffRoad(int terrain, int levelID)
{
	switch (terrain)
	{
	case TERRAIN_GRASS:
	case TERRAIN_DIRT:
	case TERRAIN_SNOW:
	case TERRAIN_SLOWGRASS:
	case TERRAIN_SLOWDIRT:
		return 1;
	case TERRAIN_WATER:
		return (levelID == DRAGON_MINES) ? 1 : 0;
	default:
		return 0;
	}
}

// ── Layer 3: per-track on-road-shortcut blockID table ────────────────────────
// Ported verbatim from Penguin-MODSK ShortcutLess (NeedsMaskGrab + the inline
// N.Gin Labs case). VERIFY-FIRST: these are the mod's retail-disc blockIDs; confirm
// against our level data in-game (shortcut_capture=1) before trusting per track.
static int AP_ScBlockInTable(int levelID, s16 blockID)
{
	static const s16 CRASH_COVE_M[]     = {1538, 1531, 1215, 1530};
	static const s16 CORTEX_CASTLE_M[]  = {2572};
	static const s16 HOT_AIR_SKYWAY_M[] = {466, 467};
	static const s16 SLIDE_COLISEUM_M[] = {1402, 1469, 1468, 1492, 1491, 805, 806,
	                                        848, 637, 636, 582, 421, 420, 419, 388};
	static const s16 PAPU_PYRAMID_M[]   = {180, 540, 457, 456, 496};
	static const s16 SEWER_SPEEDWAY_M[] = {1651, 1650};
	static const s16 POLAR_PASS_M[]     = {802, 814};
	static const s16 N_GIN_LABS_M[]     = {694, 685};

	const s16 *ids = 0;
	int n = 0, i;

	switch (levelID)
	{
	case CRASH_COVE:     ids = CRASH_COVE_M;     n = 4;  break;
	case CORTEX_CASTLE:  ids = CORTEX_CASTLE_M;  n = 1;  break;
	case HOT_AIR_SKYWAY: ids = HOT_AIR_SKYWAY_M; n = 2;  break;
	case SLIDE_COLISEUM: ids = SLIDE_COLISEUM_M; n = 15; break;
	case PAPU_PYRAMID:   ids = PAPU_PYRAMID_M;   n = 5;  break;
	case SEWER_SPEEDWAY: ids = SEWER_SPEEDWAY_M; n = 2;  break;
	case POLAR_PASS:     ids = POLAR_PASS_M;     n = 2;  break;
	case N_GIN_LABS:     ids = N_GIN_LABS_M;     n = 2;  break;
	default:             return 0;
	}
	for (i = 0; i < n; i++)
		if (blockID == ids[i])
			return 1;
	return 0;
}

static int AP_ScBlockCaptured(s16 blockID)
{
	int i;
	for (i = 0; i < g_captured_n; i++)
		if (g_captured[i] == blockID)
			return 1;
	return 0;
}

int AP_QuadIsShortcut(struct QuadBlock *quad)
{
	int levelID;
	if (quad == 0)
		return 0;
	if ((quad->quadFlags & AP_QUADBLOCK_FLAG_SHORTCUT) != 0)
		return 1; // baked annotation (future asset/track-data pass)
	levelID = AP_ScLevelID();
	if (AP_ScTerrainIsOffRoad((int)quad->terrain_type, levelID))
		return 1; // layer 1
	if (AP_ScBlockInTable(levelID, quad->blockID))
		return 1; // layer 3
	return AP_ScBlockCaptured(quad->blockID); // live dev-captured set
}

int AP_ShortcutCheck(struct Driver *driver, struct QuadBlock *quad)
{
	if (driver == 0 || quad == 0)
		return 0;
	if (driver != AP_ScLocalDriver())
		return 0; // enforce on the local player only

	// Verify tool: log each distinct quad the player touches so a shortcut's
	// blockIDs can be read off the log while driving it (shortcut_capture=1).
	if (g_capture_log && quad->blockID != g_last_logged_block)
	{
		char msg[128];
		snprintf(msg, sizeof msg,
		         "[AP SHORTCUT] touch blockID=%d terrain=%d checkpoint=%d flags=0x%04x\n",
		         (int)quad->blockID, (int)quad->terrain_type,
		         (int)quad->checkpointIndex, (unsigned)quad->quadFlags);
		AP_LogLine(msg);
		g_last_logged_block = quad->blockID;
	}

	if (!g_shortcutless)
		return 0;
	return AP_QuadIsShortcut(quad) ? 1 : 0;
}

// ── Layer 2: checkpoint-% gap-skip detector ──────────────────────────────────
// Ported from Penguin-MODSK PreventSkip. If lastValid's checkpointIndex jumps more
// than a per-track % of the checkpoint count between frames, the player crossed a
// gap-jump shortcut that stayed on-road -> rewind lastValid to the previous one and
// request the mask grab (engine replants at that checkpoint). Local player only, so
// AI pathing is untouched.
static struct QuadBlock *g_lastValid_prev = 0; // previous frame's local lastValid
static int g_scless_stale_logged = 0;          // rising-edge latch for the stale-lastValid diag log

static int abs_i(int v) { return v < 0 ? -v : v; }

static void AP_ScTriggerGrab(struct Driver *d)
{
	if (g_lastValid_prev != 0)
		d->lastValid = g_lastValid_prev; // rewind so respawn lands at the last legit checkpoint
	d->collisionFlags |= DRIVER_COLL_FLAG_MASK_GRAB_REQUEST;
	AP_LogLine("[AP SHORTCUT] SKIP blocked -> mask grab\n");
}

// A driver's lastValid is set only when the birth/collision BSP probe HITS a
// quadblock (VehBirth.c:236); on a miss it keeps whatever it held before -- after
// a level load that is the freed previous level's mesh (mempack popped), non-null
// but dangling. Reading ->checkpointIndex off it is the Crash Cove / Roo's Tubes
// race-entry crash (ap_shortcut.c:245, minidumps 2026-07-04). Validate the pointer
// against the live level's quadblock array -- the same array the engine indexes a
// quadblock against elsewhere (CAM.c:2342, MainFrame.c:622) -- before trusting it.
static int AP_LastValidInMesh(struct GameTracker *gGT, struct QuadBlock *qb)
{
	struct mesh_info *mesh;
	unsigned char *base, *p;
	unsigned long span;

	if (gGT == 0 || gGT->level1 == 0 || qb == 0)
		return 0;
	mesh = gGT->level1->ptr_mesh_info;
	if (mesh == 0 || mesh->ptrQuadBlockArray == 0 || mesh->numQuadBlock <= 0)
		return 0;
	base = (unsigned char *)mesh->ptrQuadBlockArray;
	p = (unsigned char *)qb;
	if (p < base)
		return 0;
	span = (unsigned long)(p - base);
	if (span % sizeof(struct QuadBlock) != 0)            // must land on a block boundary
		return 0;
	if (span / sizeof(struct QuadBlock) >= (unsigned long)mesh->numQuadBlock)
		return 0;
	return 1;
}

void AP_ShortcutSkipTick(struct GameTracker *gGT)
{
	struct Driver *d;
	int levelID, maxCheckpoint, cur, prev, maxSkip;

	if (gGT == 0)
		return;

	// Only sample checkpoints inside a live race window: mid-race, lights out, not
	// loading/menu/cutscene/EOR (same test as AP_TrapTick; anchor PlayLevel.c:338).
	// Outside that window drivers[0] can point at a not-yet-born Driver whose
	// lastValid still holds arena leftovers, non-null but dangling (the Crash Cove
	// race-load crash at ap_shortcut.c:227, minidumps 2026-07-04). Every early-out
	// below also clears g_lastValid_prev so no pointer survives a context change.
	if ((gGT->gameMode1 &
	     (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) != 0 ||
	    gGT->trafficLightsTimer > 0)
	{
		g_lastValid_prev = 0;
		return;
	}

	if (!g_shortcutless)
	{
		g_lastValid_prev = 0;
		return;
	}

	levelID = (int)gGT->levelID;
	if (levelID > TURBO_TRACK) // race tracks only (0..TURBO_TRACK)
	{
		g_lastValid_prev = 0;
		return;
	}
	if (gGT->level1 == 0)
	{
		g_lastValid_prev = 0;
		return;
	}

	d = gGT->drivers[0]; // local player
	if (d == 0 || d->lastValid == 0)
	{
		g_lastValid_prev = 0;
		g_scless_stale_logged = 0;
		return;
	}

	// lastValid non-null but not pointing into the live mesh -> stale probe-miss
	// pointer (see AP_LastValidInMesh). Bail before the ->checkpointIndex read that
	// crashes, and log once per episode so the Artemis build proves WHEN it fires:
	// load!=IDLE or gm1 & LOADING(0x40000000) => loading window; else post-birth.
	if (!AP_LastValidInMesh(gGT, d->lastValid))
	{
		if (!g_scless_stale_logged)
		{
			char m[176];
			snprintf(m, sizeof m,
			         "[AP SHORTCUT] stale lastValid=%p out-of-mesh -> skip "
			         "(load=%d gm1=0x%08x lvl=%d tlt=%d)\n",
			         (void *)d->lastValid, (int)sdata->Loading.stage,
			         (unsigned)gGT->gameMode1, (int)gGT->levelID,
			         (int)gGT->trafficLightsTimer);
			AP_LogLine(m);
			g_scless_stale_logged = 1;
		}
		g_lastValid_prev = 0;
		return;
	}
	g_scless_stale_logged = 0;

	maxCheckpoint = gGT->level1->cnt_restart_points - 1;
	if (maxCheckpoint <= 0)
	{
		g_lastValid_prev = d->lastValid;
		return;
	}
	cur = (int)(unsigned char)d->lastValid->checkpointIndex;

	// Per-track skip tolerance as a % of checkpoint count (Penguin-MODSK constants).
	if (levelID == PAPU_PYRAMID || levelID == OXIDE_STATION)
		maxSkip = (maxCheckpoint * 7) / 100;
	else if (levelID == SEWER_SPEEDWAY)
		maxSkip = (maxCheckpoint * 25) / 100;
	else if (levelID == HOT_AIR_SKYWAY)
		maxSkip = (maxCheckpoint * 9) / 100;
	else if (levelID == POLAR_PASS)
		maxSkip = (maxCheckpoint * 17) / 100;
	else
		maxSkip = (maxCheckpoint * 13) / 100;

	// g_lastValid_prev was validated when saved, but a context change since can have
	// freed it; re-validate before this deref so a stale prev can't crash here either.
	if (g_lastValid_prev != 0 && !AP_LastValidInMesh(gGT, g_lastValid_prev))
		g_lastValid_prev = 0;

	if (g_lastValid_prev != 0 && cur != (int)(unsigned char)g_lastValid_prev->checkpointIndex)
	{
		prev = (int)(unsigned char)g_lastValid_prev->checkpointIndex;

		// Zone-specific refinements (Penguin-MODSK).
		if (levelID == POLAR_PASS && prev > 130)      maxSkip = (maxCheckpoint * 6) / 100;
		else if (levelID == POLAR_PASS && prev > 86)  maxSkip = (maxCheckpoint * 4) / 100;
		else if (levelID == POLAR_PASS && prev > 56)  maxSkip = (maxCheckpoint * 9) / 100;
		if (levelID == HOT_AIR_SKYWAY && prev > 145)  maxSkip = (maxCheckpoint * 3) / 100;

		// Lap skip (No-Man's-Zone abuse): landing on the final checkpoint from a
		// mid-track checkpoint with a big jump.
		if (cur == maxCheckpoint && prev > 5 && abs_i(cur - prev) > 15)
		{
			AP_ScTriggerGrab(d);
		}
		// General gap-skip.
		else if (prev != 0xFF && cur != 0xFF && cur != 0 &&
		         prev < maxCheckpoint - 1 && prev > 5 &&
		         abs_i(cur - prev) > maxSkip)
		{
			AP_ScTriggerGrab(d);
		}
		// Tiny Arena nitro lap-skip special case.
		else if (levelID == TINY_ARENA &&
		         (prev == maxCheckpoint || prev == maxCheckpoint - 1) &&
		         cur == 141)
		{
			AP_ScTriggerGrab(d);
		}
	}

	g_lastValid_prev = d->lastValid;
}

// ── Dev keys + config ────────────────────────────────────────────────────────
static void AP_ScMarkCurrentQuad(void)
{
	struct Driver *d = AP_ScLocalDriver();
	struct QuadBlock *q;
	char msg[128];

	if (d == 0)
		return;
	q = d->currBlockTouching;
	if (q == 0)
		q = d->lastValid;
	if (q == 0)
	{
		AP_LogLine("[AP SHORTCUT] mark: no current quad\n");
		return;
	}
	// Always log the blockID (the verify readout), whether or not it is new.
	snprintf(msg, sizeof msg,
	         "[AP SHORTCUT] quad blockID=%d terrain=%d checkpoint=%d (table=%d)\n",
	         (int)q->blockID, (int)q->terrain_type, (int)q->checkpointIndex,
	         AP_ScBlockInTable(AP_ScLevelID(), q->blockID));
	AP_LogLine(msg);

	if (AP_ScBlockCaptured(q->blockID))
		return;
	if (g_captured_n >= AP_SC_CAPTURE_CAP)
	{
		AP_LogLine("[AP SHORTCUT] capture set full\n");
		return;
	}
	g_captured[g_captured_n++] = q->blockID;
}

void AP_ShortcutKeys(void)
{
	static int prevToggle = 0, prevMark = 0;
	int toggle = Platform_InputRawKeyDown(AP_SC_KEY_TOGGLE);
	int mark = Platform_InputRawKeyDown(AP_SC_KEY_MARK);

	// Numpad 7 is a DEV toggle -- always works so the effect can be tested regardless of
	// mode / allow_shortcut_switching (the real player switch is the future menu).
	if (toggle && !prevToggle)
		AP_ShortcutlessSet(!g_shortcutless);
	if (mark && !prevMark)
		AP_ScMarkCurrentQuad();

	prevToggle = toggle;
	prevMark = mark;
}

static int AP_ScStartsWith(const char *s, const char *pfx, const char **rest)
{
	size_t n = strlen(pfx);
	if (!strncmp(s, pfx, n))
	{
		*rest = s + n;
		return 1;
	}
	return 0;
}

int AP_ShortcutConfigLine(const char *line)
{
	const char *v;

	if (AP_ScStartsWith(line, "shortcuts=", &v))
	{
		if (!strcmp(v, "forbidden"))       { g_mode = AP_SC_FORBIDDEN;  AP_ShortcutlessSet(1); }
		else if (!strcmp(v, "unlockable")) { g_mode = AP_SC_UNLOCKABLE; AP_ShortcutlessSet(1); }
		else                               { g_mode = AP_SC_ALLOWED;    AP_ShortcutlessSet(0); }
		return 1;
	}
	if (AP_ScStartsWith(line, "allow_shortcut_switching=", &v))
	{
		g_allow_switching = (v[0] == '1');
		return 1;
	}
	if (AP_ScStartsWith(line, "shortcut_capture=", &v))
	{
		g_capture_log = (v[0] == '1');
		return 1;
	}
	// Legacy alias: shortcutless=1 == forbidden-style enforcement on.
	if (AP_ScStartsWith(line, "shortcutless=", &v))
	{
		AP_ShortcutlessSet(v[0] == '1');
		return 1;
	}
	return 0;
}

#endif // CTR_AP
