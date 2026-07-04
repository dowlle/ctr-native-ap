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

#define AP_SC_KEY_TOGGLE 64 // F7: dev toggle Shortcutless
#define AP_SC_KEY_MARK   65 // F8: log/mark current quad blockID

static int g_shortcutless = 0;   // enforcement toggle (the Shortcut Unlock item)
static int g_mode = AP_SC_ALLOWED;
static int g_allow_switching = 0; // allow_shortcut_switching (future in-game switch)
static int g_capture_log = 0;    // passive per-touch blockID logging (verify tool)

// Dev capture set: blockIDs marked live with F8, honoured by AP_QuadIsShortcut so a
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

static int abs_i(int v) { return v < 0 ? -v : v; }

static void AP_ScTriggerGrab(struct Driver *d)
{
	if (g_lastValid_prev != 0)
		d->lastValid = g_lastValid_prev; // rewind so respawn lands at the last legit checkpoint
	d->collisionFlags |= DRIVER_COLL_FLAG_MASK_GRAB_REQUEST;
	AP_LogLine("[AP SHORTCUT] SKIP blocked -> mask grab\n");
}

void AP_ShortcutSkipTick(struct GameTracker *gGT)
{
	struct Driver *d;
	int levelID, maxCheckpoint, cur, prev, maxSkip;

	if (gGT == 0)
		return;

	// Reset per-race state during the light countdown (mirror the mod).
	if (gGT->trafficLightsTimer > 0)
	{
		g_lastValid_prev = 0;
		return;
	}

	if (!g_shortcutless)
		return;

	levelID = (int)gGT->levelID;
	if (levelID > TURBO_TRACK) // race tracks only (0..TURBO_TRACK)
		return;
	if (gGT->level1 == 0)
		return;

	d = gGT->drivers[0]; // local player
	if (d == 0 || d->lastValid == 0)
	{
		g_lastValid_prev = 0;
		return;
	}

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

	// F7 is a DEV toggle -- always works so the effect can be tested regardless of
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
