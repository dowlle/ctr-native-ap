#ifdef CTR_AP

#include <common.h> // structs Driver/GameTracker/Instance/InstDef/Level + sdata + level ids

#include "ap_tizi.h"
#include "ap_tizi_logic.h" // the pure activation table + row selection rule
#include "ap_hooks.h"      // AP_LogLine (non-static log shim)
#include "ap_net.h"        // ap_net_location_exists (itemsanity membership probe)
#include "ap_seedcfg.h"    // ctr_cfg_active

// ============================================================================
// Tizi Helper (#223) -- implementation. ap_tizi.h holds the design contract and
// ap_tizi_logic.h the two pure rules; this file is only the engine glue.
// ============================================================================

// The itemsanity location block's first code. Membership in the slot's location
// set is the authoritative "itemsanity is on for this seed" signal.
#define AP_TIZI_ITEMSANITY_PROBE_CODE 35016000L

// Weapon box model id (zGlobal_DATA.c 0x08 - PU_RANDOM_CRATE, whose LInC is
// RB_CrateWeapon_LInC).
#define AP_TIZI_WEAPON_CRATE_MODEL PU_RANDOM_CRATE

// Census cap. Papu's Pyramid holds nowhere near this many weapon boxes; the cap
// exists so a malformed or modded LEV cannot walk off a fixed array. Hitting it
// is REPORTED and REFUSES the feature -- a truncated census could select the
// wrong four, and a silent truncation is exactly the failure mode Lessons
// Learned #1 is about.
#define AP_TIZI_MAX_CRATES 64

static int g_tizi_helper_received = 0;
static int g_tizi_mask_received = 0;

// Level-load capture. Not walked here: resolution waits for the first hit, by
// which point the checkpoint chain is certainly loaded.
static struct InstDef *g_tizi_defs = NULL;
static int g_tizi_def_count = 0;

// Resolution state for the current level. -1 = not attempted yet, 0 = attempted
// and refused (stay vanilla, do not retry or re-log), 1 = four crates selected.
static int g_tizi_resolved = -1;
static struct Instance *g_tizi_row[AP_TIZI_ROW_SIZE];

// A helper box armed this roll for the local player.
static int g_tizi_pending = 0;

void AP_TiziReset(void)
{
	g_tizi_helper_received = 0;
	g_tizi_mask_received = 0;
	g_tizi_pending = 0;
}

void AP_TiziReceiveHelper(void)
{
	g_tizi_helper_received = 1;
}

void AP_TiziReceiveMask(void)
{
	g_tizi_mask_received = 1;
}

void AP_TiziLevelInstances(struct InstDef *defs, int count)
{
	g_tizi_defs = defs;
	g_tizi_def_count = (defs != NULL && count > 0) ? count : 0;
	// A new level invalidates the selection AND any half-finished roll: the
	// crate that armed it does not exist any more.
	g_tizi_resolved = -1;
	g_tizi_pending = 0;
}

// Is itemsanity on for this seed? Server location membership, same probe the
// #145 native half uses. Absent slot_data or a pre-0.2.0 seed answers "off",
// which is correct: neither can carry the weapon items either.
static int AP_TiziItemsanityOn(void)
{
	return ctr_cfg_active() &&
	       ap_net_location_exists(AP_TIZI_ITEMSANITY_PROBE_CODE);
}

static int AP_TiziActive(void)
{
	return AP_TiziHelperActive(g_tizi_helper_received, AP_TiziItemsanityOn(),
	                           g_tizi_mask_received);
}

// Squared 3D distance between an instance position and a checkpoint node.
// Positions are s16 per axis, so a squared component reaches ~4.3e9 and the sum
// three times that: 64-bit, not int.
static long long AP_TiziDistSq(const SVec3 *a, const SVec3 *b)
{
	long long dx = (long long)a->x - (long long)b->x;
	long long dy = (long long)a->y - (long long)b->y;
	long long dz = (long long)a->z - (long long)b->z;
	return dx * dx + dy * dy + dz * dz;
}

// Lap progress for a world position: the distance-to-finish of the nearest
// checkpoint node. High near the start line, falling toward the finish.
static long AP_TiziProgressAt(struct Level *level, const SVec3 *pos)
{
	long best = -1;
	long long bestDist = 0;
	int i;

	for (i = 0; i < level->cnt_restart_points; i++)
	{
		long long dist = AP_TiziDistSq(pos, &level->ptr_restart_points[i].pos);
		if (best < 0 || dist < bestDist)
		{
			bestDist = dist;
			best = (long)level->ptr_restart_points[i].distToFinish;
		}
	}
	return best;
}

// Build the crate census and run the selection rule. Runs at most once per level
// load; every exit logs exactly one line, so an in-game pass can read the result
// (and, on a refusal, the evidence) straight out of the AP log.
static void AP_TiziResolve(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Level *level;
	struct InstDef *def;
	struct Instance *insts[AP_TIZI_MAX_CRATES];
	long progress[AP_TIZI_MAX_CRATES];
	int index[AP_TIZI_ROW_SIZE];
	char msg[256];
	long trackLength;
	int count = 0;
	int overflow = 0;
	int rc;
	int i;

	g_tizi_resolved = 0; // refuse unless everything below succeeds

	if (gGT == NULL || (int)gGT->levelID != PAPU_PYRAMID)
		return; // silent: every other track is simply not this feature's business

	level = gGT->level1;
	if (level == NULL || level->ptr_restart_points == NULL ||
	    level->cnt_restart_points <= 0 || g_tizi_def_count <= 0)
	{
		AP_LogLine("[AP TIZI] refused: no checkpoint chain or no level instances\n");
		return;
	}

	trackLength = (long)level->ptr_restart_points[0].distToFinish;

	def = g_tizi_defs;
	for (i = 0; i < g_tizi_def_count; i++, def++)
	{
		if (def->model == NULL ||
		    (def->model->id & 0xffff) != AP_TIZI_WEAPON_CRATE_MODEL)
			continue;
		if (count >= AP_TIZI_MAX_CRATES)
		{
			overflow = 1;
			break;
		}
		insts[count] = def->ptrInstance;
		progress[count] = AP_TiziProgressAt(level, &def->pos);
		count++;
	}

	if (overflow)
	{
		snprintf(msg, sizeof msg,
		         "[AP TIZI] refused: more than %d weapon crates on levelID %d, "
		         "census would be truncated\n",
		         AP_TIZI_MAX_CRATES, (int)gGT->levelID);
		AP_LogLine(msg);
		return;
	}

	rc = AP_TiziSelectRow(progress, count, trackLength, index);
	if (rc != AP_TIZI_ROW_OK)
	{
		snprintf(msg, sizeof msg,
		         "[AP TIZI] refused: rc=%d crates=%d trackLen=%ld -- the four-box "
		         "row could not be identified, every box stays vanilla\n",
		         rc, count, trackLength);
		AP_LogLine(msg);
		// The census itself, so a refusal is an evidence handoff rather than a
		// dead end: one line per crate, in LEV order.
		for (i = 0; i < count; i++)
		{
			snprintf(msg, sizeof msg,
			         "[AP TIZI]   crate %d distToFinish=%ld\n", i, progress[i]);
			AP_LogLine(msg);
		}
		return;
	}

	for (i = 0; i < AP_TIZI_ROW_SIZE; i++)
		g_tizi_row[i] = insts[index[i]];

	snprintf(msg, sizeof msg,
	         "[AP TIZI] row selected on levelID %d from %d crates (trackLen=%ld): "
	         "distToFinish %ld/%ld/%ld/%ld\n",
	         (int)gGT->levelID, count, trackLength,
	         progress[index[0]], progress[index[1]],
	         progress[index[2]], progress[index[3]]);
	AP_LogLine(msg);
	g_tizi_resolved = 1;
}

static int AP_TiziIsRowCrate(struct Instance *crateInst)
{
	int i;
	if (g_tizi_resolved < 0)
		AP_TiziResolve();
	if (g_tizi_resolved != 1 || crateInst == NULL)
		return 0;
	for (i = 0; i < AP_TIZI_ROW_SIZE; i++)
		if (g_tizi_row[i] == crateInst)
			return 1;
	return 0;
}

void AP_TiziTick(struct GameTracker *gGT)
{
	struct Driver *local;

	if (!g_tizi_pending)
		return;

	// A pending Mask belongs to exactly one roll. If the local player is no
	// longer rolling and the roll never reached the filter (a restart, a mode
	// change, a driver that stopped existing), drop it: handing this Mask to
	// whatever box comes next is the one way this feature could leak.
	local = (gGT != NULL) ? gGT->drivers[0] : NULL;
	if (local == NULL || local->heldItemID != 0x10)
		g_tizi_pending = 0;
}

void AP_TiziOnWeaponBoxHit(struct Driver *driver, struct Instance *crateInst)
{
	struct GameTracker *gGT = sdata->gGT;

	// Local player only, same isolation every other AP hook uses: an AI racer
	// breaking one of the four is none of our business.
	if (gGT == NULL || driver == NULL || driver != gGT->drivers[0])
		return;
	if (!AP_TiziActive() || !AP_TiziIsRowCrate(crateInst))
		return;

	g_tizi_pending = 1;
}

int AP_TiziFilterRoll(struct Driver *driver, int rolled)
{
	struct GameTracker *gGT = sdata->gGT;

	if (gGT == NULL || driver == NULL || driver != gGT->drivers[0])
		return rolled;

	// Any resolved roll for the local player consumes the arm, ours or not, so
	// a pending Mask can never outlive the roll session that armed it.
	if (!g_tizi_pending)
		return rolled;
	g_tizi_pending = 0;

	// Re-check activation at USE time, not only at arm time: a slot switch or a
	// disconnect between the box and the roll must not still pay out.
	if (!AP_TiziActive())
		return rolled;

	return AP_TIZI_HELD_ITEM_MASK;
}

#endif // CTR_AP
