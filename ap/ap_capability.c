#ifdef CTR_AP

#include <common.h> // struct Driver + data + sdata + TurboType / CollStepFlags

#include <stdio.h>

#include "ap_capability.h"
#include "ap_hooks.h"    // AP_LogLine
#include "ap_seedcfg.h"  // ctr_cfg + ctr_cfg_active()

// ============================================================================
// PROGRESSIVE BOOST (#12) + PROGRESSIVE STATS (#13) -- native consumer.
// See ap_capability.h for the ladders, where their semantics were ruled, and
// what this module deliberately does not do.
// ============================================================================

// Received copies per chain, rebuilt from the authoritative ReceivedItems replay.
static int g_cap_recv[AP_CAP_CHAIN_COUNT];

// One line per connect when a per-character receipt is declined, so a seed that
// somehow carries them is visible in the log without spamming it.
static int g_cap_pc_logged;

void AP_CapabilityReset(void)
{
	int i;
	for (i = 0; i < AP_CAP_CHAIN_COUNT; i++)
		g_cap_recv[i] = 0;
	g_cap_pc_logged = 0;
}

void AP_CapabilityReceive(int chain)
{
	if (chain >= 0 && chain < AP_CAP_CHAIN_COUNT)
		g_cap_recv[chain]++;
}

void AP_CapabilityReceivePerCharacter(int blockIndex)
{
	char msg[288];

	if (blockIndex < 0 || blockIndex >= AP_CAPABILITY_PC_ITEM_COUNT)
		return;
	if (g_cap_pc_logged)
		return;

	g_cap_pc_logged = 1;
	snprintf(msg, sizeof msg,
	         "[AP CAP] per-character capability item received (block index %d) -- this "
	         "build has no per-character mapping and applies NOTHING for it. No apworld "
	         "on main generates these (per_character raises OptionError pending "
	         "dowlle/ctr-native-ap#71).\n",
	         blockIndex);
	AP_LogLine(msg);
}

// ── Is a pack active for this seed? ─────────────────────────────────────────
//
// Mode 2 (per_character) is a RESERVED wire value: the apworld refuses to
// generate it, and this build has no ruled roster mapping, so it is treated as
// OFF rather than quietly given shared-global behaviour. Anything else unknown
// is treated as OFF for the same reason -- a mode we do not understand must not
// silently change the kart.
static int AP_CapabilityBoostActive(void)
{
	return ctr_cfg_active() && ctr_cfg.boost_mode == AP_CAP_MODE_SHARED_GLOBAL;
}

static int AP_CapabilityStatsActive(void)
{
	return ctr_cfg_active() && ctr_cfg.stats_mode == AP_CAP_MODE_SHARED_GLOBAL;
}

// The local human player. Adventure / arcade single player: drivers[0]. Same
// idiom and same guards as ap_surface.c's AP_SurfaceTerrain and ap_traps.c's
// AP_TrapIsLocal -- duplicated rather than shared so no module's call sites
// depend on another's lifecycle.
static int AP_CapabilityIsLocal(struct Driver *driver)
{
	if (driver == 0 || sdata == 0 || sdata->gGT == 0)
		return 0;
	return driver == sdata->gGT->drivers[0];
}

int AP_CapabilityBoostTier(void)
{
	int ceiling;
	int tier;

	if (!AP_CapabilityBoostActive())
		return -1;

	// Without the capstone toggle the chain is 2 copies (none -> boost -> USF);
	// with it, 3 (adds blue fire). Derived from the enum, not restated.
	ceiling = ctr_cfg.boost_blue_fire ? AP_CAP_BOOST_BLUEFIRE : AP_CAP_BOOST_USF;

	tier = g_cap_recv[AP_CAP_CHAIN_BOOST];
	if (tier > ceiling)
		tier = ceiling;
	return tier;
}

int AP_CapabilityStatRankFor(int chain)
{
	int rank;

	if (!AP_CapabilityStatsActive())
		return -1;
	if (chain <= AP_CAP_CHAIN_BOOST || chain >= AP_CAP_CHAIN_COUNT)
		return -1;

	rank = g_cap_recv[chain];
	if (rank > AP_CAP_STAT_COPIES)
		rank = AP_CAP_STAT_COPIES;
	return rank;
}

// ── Boost tier ──────────────────────────────────────────────────────────────
//
// VehFire_Increment turns its fireLevel argument into a boost-speed cap with one
// linear map (game/Vehicle/VehFire.c):
//
//   fireSpeedCap = singleTurbo + ((fireLevel * (sacredFire - singleTurbo)) >> 8)
//
// so fireLevel 0x100 lands exactly on const_SacredFireSpeed (red fire) and the
// super turbo pad's 0x800 overshoots it eightfold -- the engine itself defines
// USF as a cap above sacred. That makes 0x100 the natural "capped max boost
// speed" for the BOOST tier: every vanilla grant except the super pad already
// sits at or below it, so clamping there removes USF speed and nothing else.
#define AP_CAP_CAP_FIRELEVEL 0x100

// A normal turbo pad's payload (game/Vehicle/VehPhysForce.c, the
// COLL_STEP_TRIGGER_TURBO_PAD branch). A super pad banks far less time (0x78)
// because its cap is meant to be spent at once, so demoting one to "acts as a
// normal pad" has to hand back the normal pad's reserves as well as its cap --
// otherwise the demotion would be a downgrade BELOW a normal pad rather than an
// equivalence.
#define AP_CAP_PAD_RESERVES 0x3c0

int AP_CapabilityFireGrant(struct Driver *driver, int *reserves, uint32_t type, int *fireLevel)
{
	const int tier = AP_CapabilityBoostTier();

	if (tier < 0 || !AP_CapabilityIsLocal(driver))
		return 1;

	// Tier NONE: only genuine turbo PADS grant. Everything else in the game is
	// boost the player earns for themselves and is suppressed -- the
	// powerslide/mini-turbo chain, hang time off a jump, the start-line rev
	// boost, the Turbo pickup and the 10-wumpa Super Engine.
	//
	// The predicate needs BOTH bits. TURBO_PAD alone is not "this is a pad": the
	// Super Engine grant raises (TURBO_PAD | SUPER_ENGINE) at
	// game/Vehicle/VehPhysProc.c, so a TURBO_PAD-only test lets the 10-wumpa
	// boost through at a tier that is supposed to have no self-earned boost at
	// all. Excluding SUPER_ENGINE is exact rather than heuristic: those are the
	// only two grant sites in the whole game that set TURBO_PAD, and the pad
	// sites (VehPhysForce.c) never set SUPER_ENGINE.
	//
	// NOTE FOR THE FIELD NOTES: the Test Lab prototype tests TURBO_PAD alone, so
	// its tier-None runs still granted the Super Engine despite its header
	// claiming otherwise. Any matrix row measured at tier None with 10+ wumpa in
	// hand may have had a boost the shipped ladder will not give.
	if (tier == AP_CAP_BOOST_NONE &&
	    ((type & TURBO_PAD) == 0 || (type & SUPER_ENGINE) != 0))
		return 0;

	if (tier < AP_CAP_BOOST_USF)
	{
		if (*fireLevel > AP_CAP_CAP_FIRELEVEL)
			*fireLevel = AP_CAP_CAP_FIRELEVEL;

		// Identify a genuine super-pad grant from the collision flag the grant was
		// raised off (VehPhysForce.c) rather than from its fireLevel, so the
		// CHEAT_TURBOPAD path -- which hands a NORMAL pad a super pad's numbers --
		// is correctly left as a normal pad.
		if ((type & TURBO_PAD) != 0 &&
		    (driver->stepFlagSet & COLL_STEP_TRIGGER_SUPER_TURBO_PAD) != 0)
			*reserves = AP_CAP_PAD_RESERVES;
	}

	// AP_CAP_BOOST_USF and AP_CAP_BOOST_BLUEFIRE both let the full payload
	// through untouched. Blue fire is tracked as its own tier but carries no
	// extra physics in this build -- its values were to come from CTR Unlimited's
	// Retro Fueled mode and have never been sourced. See ap_capability.h.
	return 1;
}

// ── Stat ranks ──────────────────────────────────────────────────────────────
//
// The kart's stat constants are written once per driver at birth by
// VehBirth_SetConsts (game/Vehicle/VehBirth.c), which scatters data.metaPhys[]
// rows into the driver by byte offset, picking each row's column from the
// character's engine class. This module writes the SAME rows the same way, every
// frame, from the received rank instead of the class column -- which is exactly
// what #13 asks for ("the chains override the character stat table with absolute
// per-tier values, meaning character choice becomes cosmetic").
//
// Reading the four anchors out of data.metaPhys at runtime rather than copying
// them into a table here is deliberate (Lessons Learned #5: if a constant exists,
// derive from it). The Test Lab prototype hardcodes its copy; this does not.

// The six class-varying stats the three chains drive, by their metaPhys row
// offset. Every one of these has four distinct per-class values that ORDER with
// kart quality (higher is a better kart on that axis, TURN_INPUT_DELAY included
// -- despite the name it is the per-frame rate at which rotation converges on
// the steering target, so higher is snappier), which is what makes "sort the
// anchors and call them VERY LOW..HIGH" meaningful.
typedef struct
{
	int chain;
	int offset;
} AP_CapStatRow;

static const AP_CapStatRow AP_CAP_STAT_ROWS[] = {
	{AP_CAP_CHAIN_TOP_SPEED, SPEED_CLASS_STAT_OFFSET},       // top speed
	{AP_CAP_CHAIN_TOP_SPEED, ACCEL_SPEED_CLASS_STAT_OFFSET}, // boosted top speed
	{AP_CAP_CHAIN_ACCEL,     ACCEL_CLASS_STAT_OFFSET},       // acceleration
	{AP_CAP_CHAIN_TURNING,   TURN_RATE_OFFSET},              // turn rate
	{AP_CAP_CHAIN_TURNING,   DRIFT_TURN_BASE_OFFSET},        // drift turn
	{AP_CAP_CHAIN_TURNING,   TURN_INPUT_DELAY_OFFSET},       // turn response
};

#define AP_CAP_STAT_ROW_COUNT ((int)(sizeof(AP_CAP_STAT_ROWS) / sizeof(AP_CAP_STAT_ROWS[0])))

// The metaPhys row for an offset, or NULL. Linear over the 65-row table, called
// six times a frame for the local player only; the table has no index by offset.
static const struct MetaPhys *AP_CapabilityMetaRow(int offset)
{
	int i;
	for (i = 0; i < 65; i++)
	{
		if (data.metaPhys[i].offset == offset)
			return &data.metaPhys[i];
	}
	return 0;
}

// The value one rank of the ladder holds for a metaPhys row.
//
// Ranks 0..3 (VERY LOW / LOW / MEDIUM / HIGH) are the row's own four engine-class
// values sorted weakest-first -- the engine's numbers, not ours.
//
// Rank 4 (VERY HIGH) is the ruled above-vanilla step (Stef, 2026-08-08: "add
// VERY HIGH above the best vanilla HIGH anchor as a real extra unlockable
// step"). **Its MAGNITUDE has never been ruled**, only its existence, so this
// build takes the smallest defensible choice and continues the table's own top
// step: VERY HIGH = HIGH + (HIGH - MEDIUM). On the retail anchors that is top
// speed 14280, boosted top speed 15780, acceleration 576, turn rate 32, drift
// turn 22, turn response 6000. It is a BALANCE number, it is Stef's call, and it
// lives in this one expression so changing it is a one-line change rather than a
// hunt through six constants.
static int AP_CapabilityStatRankValue(const struct MetaPhys *row, int rank)
{
	int v[NUM_CLASSES];
	int i, j, t;

	for (i = 0; i < NUM_CLASSES; i++)
		v[i] = row->value[i];

	// Insertion sort, ascending. Four elements, once per row per frame.
	for (i = 1; i < NUM_CLASSES; i++)
	{
		t = v[i];
		for (j = i - 1; j >= 0 && v[j] > t; j--)
			v[j + 1] = v[j];
		v[j + 1] = t;
	}

	if (rank < 0)
		rank = 0;
	if (rank < NUM_CLASSES)
		return v[rank];

	return v[NUM_CLASSES - 1] + (v[NUM_CLASSES - 1] - v[NUM_CLASSES - 2]);
}

// Write one stat into the driver exactly the way VehBirth_SetConsts does -- by
// the row's own offset and size -- so a u8 / s8 / s16 field lands byte-identical
// to an engine write and no field is named twice.
static void AP_CapabilityWriteStat(struct Driver *driver, const struct MetaPhys *row, int value)
{
	u8 *dst = (u8 *)driver + row->offset;
	u32 raw = (u32)value;

	if (row->size == 1)
	{
		dst[0] = (u8)raw;
		return;
	}
	if (row->size == 2)
	{
		dst[0] = (u8)raw;
		dst[1] = (u8)(raw >> 8);
		return;
	}
	if (row->size == 4)
	{
		dst[0] = (u8)raw;
		dst[1] = (u8)(raw >> 8);
		dst[2] = (u8)(raw >> 16);
		dst[3] = (u8)(raw >> 24);
	}
}

void AP_CapabilityStats(struct Driver *driver)
{
	int i;

	if (!AP_CapabilityStatsActive() || !AP_CapabilityIsLocal(driver))
		return;

	// Written every frame rather than once on a rank change: six stores, no
	// "have I already applied this" flag to get wrong, and it self-heals after a
	// rebirth (a retry, a level reload, a character change) without needing to
	// observe one. Every value is absolute, so a mid-race receipt takes effect on
	// the next frame.
	for (i = 0; i < AP_CAP_STAT_ROW_COUNT; i++)
	{
		const struct MetaPhys *row = AP_CapabilityMetaRow(AP_CAP_STAT_ROWS[i].offset);
		int rank;

		if (row == 0 || row->size <= 0)
			continue; // table shape changed under us: leave the engine's own value

		rank = AP_CapabilityStatRankFor(AP_CAP_STAT_ROWS[i].chain);
		if (rank < 0)
			continue;

		AP_CapabilityWriteStat(driver, row, AP_CapabilityStatRankValue(row, rank));
	}
}

#endif // CTR_AP
