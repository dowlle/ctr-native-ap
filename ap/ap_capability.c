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
// g_cap_recv is the shared_global pack (mode 1); g_cap_recv_pc is the
// per_character pack (mode 2), one row per roster slot. Both are always tracked
// from the wire -- the seed's mode decides which one the kart reads, not which
// one is counted, so a mode the seed never uses simply stays at zero.
static int g_cap_recv[AP_CAP_CHAIN_COUNT];
static int g_cap_recv_pc[AP_CAP_ROSTER_COUNT][AP_CAP_CHAIN_COUNT];

// One line per connect the first time a per-character receipt lands, so the wire
// is visibly consumed without spamming the log with 64 lines.
static int g_cap_pc_logged;

// One line per connect if per_character mode is live but the current character
// cannot be identified. That path fails open to vanilla, and a bound that drops
// behaviour silently is the exact pattern Lessons Learned #4 is about.
static int g_cap_pc_unknown_logged;

// ── Roster order: WIRE order, not engine order ──────────────────────────────
//
// The per-character item block is laid out in the apworld's own
// `progressive_capability.ROSTER` order (worlds/ctr/progressive_capability.py):
//
//   Crash Bandicoot, Coco Bandicoot, Polar, Pura, Neo Cortex, N. Tropy,
//   Ripper Roo, Papu Papu, Komodo Joe, Pinstripe, Dingodile, Tiny Tiger,
//   N. Gin, Fake Crash, Nitros Oxide, Penta Penguin
//
// The engine's own `enum Characters` (include/namespace_Vehicle.h) numbers the
// SAME sixteen racers in a COMPLETELY DIFFERENT order (Crash, Cortex, Tiny,
// Coco, N. Gin, Dingodile, Polar, Pura, Pinstripe, Papu, Roo, Komodo, Tropy,
// Penta, Fake Crash, Oxide), and the grand-prix champion table in
// game/zGlobal_DATA.c walks them in a third order again (Dingodile, Komodo,
// Penta, Crash, Pura, Papu, Roo, Pinstripe, Tropy, Fake Crash, Cortex, N. Gin,
// Polar, Oxide, Coco, Tiny). All three are the same SET of sixteen; only the set
// was ever verified, in the spine-1 build note, and none of the three orders
// agrees with either other one.
//
// So the roster slot a wire item carries CANNOT be used as an engine character
// ID, and the engine character ID cannot be used as a wire index. This table is
// the only place the two are reconciled: read it as "wire slot N belongs to this
// engine character". Getting it wrong would not crash or warn -- it would
// silently apply Dingodile's upgrades to Coco, which is why it is spelled out
// name-by-name rather than computed.
static const int AP_CAP_ROSTER_CHARACTER[AP_CAP_ROSTER_COUNT] = {
	CRASH_BANDICOOT, // 0  Crash Bandicoot  (item idx 31..34)
	COCO_BANDICOOT,  // 1  Coco Bandicoot   (35..38)
	POLAR,           // 2  Polar            (39..42)
	PURA,            // 3  Pura             (43..46)
	NEO_CORTEX,      // 4  Neo Cortex       (47..50)
	N_TROPY,         // 5  N. Tropy         (51..54)
	RIPPER_ROO,      // 6  Ripper Roo       (55..58)
	PAPU_PAPU,       // 7  Papu Papu        (59..62)
	KOMODO_JOE,      // 8  Komodo Joe       (63..66)
	PINSTRIPE,       // 9  Pinstripe        (67..70)
	DINGODILE,       // 10 Dingodile        (71..74)
	TINY_TIGER,      // 11 Tiny Tiger       (75..78)
	N_GIN,           // 12 N. Gin           (79..82)
	FAKE_CRASH,      // 13 Fake Crash       (83..86)
	NITROS_OXIDE,    // 14 Nitros Oxide     (87..90)
	PENTA_PENGUIN,   // 15 Penta Penguin    (91..94)
};

// Compile-time: the table and the block size must stay in step. A 17th racer or
// a fifth chain has to break the build here rather than silently shift every
// mapping by one.
typedef char ap_cap_roster_table_size_check
    [(sizeof(AP_CAP_ROSTER_CHARACTER) / sizeof(AP_CAP_ROSTER_CHARACTER[0])) == AP_CAP_ROSTER_COUNT
         ? 1
         : -1];

// ── Character unlocks (#54/#209) ───────────────────────────────────────────
// One flag per ENGINE character id, not per roster slot: every consumer (the
// pad gate, the hub picker, the roster enforcement at birth) asks about a
// character the engine names, so the roster->engine translation happens exactly
// once, on receipt.
static int g_character_unlocked[AP_CAP_ROSTER_COUNT];

int AP_CapabilityRosterCharacter(int rosterSlot)
{
	if (rosterSlot < 0 || rosterSlot >= AP_CAP_ROSTER_COUNT)
		return -1;
	return AP_CAP_ROSTER_CHARACTER[rosterSlot];
}

void AP_CharacterReceive(int rosterSlot)
{
	int characterID = AP_CapabilityRosterCharacter(rosterSlot);
	if (characterID < 0)
		return;
	if (g_character_unlocked[characterID])
		return; // one copy each; a re-send on reconnect is not news
	g_character_unlocked[characterID] = 1;
	{
		char msg[128];
		snprintf(msg, sizeof msg,
		         "[AP CHARACTER] unlocked %s (roster slot %d -> character %d)\n",
		         data.MetaDataCharacters[characterID].name_Debug
		             ? data.MetaDataCharacters[characterID].name_Debug
		             : "?",
		         rosterSlot, characterID);
		AP_LogLine(msg);
	}
}

int AP_CharacterUnlocked(int characterID)
{
	if (characterID < 0 || characterID >= AP_CAP_ROSTER_COUNT)
		return 0;

	// No character phase on this seed (or no seed at all): answer the way a
	// pre-0.2.0 client does -- the eight racers vanilla lets into Adventure,
	// plus whoever is currently being driven. Never widen the roster on a seed
	// that did not ask for it.
	//
	// Keyed on character_phase_present, NOT on ctr_cfg_active(): an older seed
	// is active and simply carries none of these keys, and `character_unlocks`
	// would default to 0 there -- which the all-unlocked branch below reads as
	// "every racer is available". That would hand a pre-0.2.0 seed the whole
	// roster, the exact opposite of leaving it alone.
	if (!ctr_cfg_active() || !ctr_cfg.character_phase_present)
		return characterID <= PURA || characterID == data.characterIDs[0];

	// The racer the seed starts you as is always yours.
	if (characterID == ctr_cfg.starting_character)
		return 1;

	// All-unlocked comfort mode: the seed created no unlock items at all, so
	// every racer is available immediately.
	if (!ctr_cfg.character_unlocks)
		return 1;

	return g_character_unlocked[characterID] != 0;
}

void AP_CapabilityReset(void)
{
	int i, j;
	for (i = 0; i < AP_CAP_CHAIN_COUNT; i++)
		g_cap_recv[i] = 0;
	for (i = 0; i < AP_CAP_ROSTER_COUNT; i++)
		for (j = 0; j < AP_CAP_CHAIN_COUNT; j++)
			g_cap_recv_pc[i][j] = 0;
	g_cap_pc_logged = 0;
	g_cap_pc_unknown_logged = 0;
	// Character unlocks reset with everything else: a reconnect replays the
	// full ReceivedItems list, so a stale flag from a previous session would
	// otherwise survive into a seed that never granted that racer.
	for (i = 0; i < AP_CAP_ROSTER_COUNT; i++)
		g_character_unlocked[i] = 0;
}

void AP_CapabilityReceive(int chain)
{
	if (chain >= 0 && chain < AP_CAP_CHAIN_COUNT)
		g_cap_recv[chain]++;
}

void AP_CapabilityReceivePerCharacter(int blockIndex)
{
	int slot;
	int chain;

	if (blockIndex < 0 || blockIndex >= AP_CAPABILITY_PC_ITEM_COUNT)
		return;

	// Character-major, exactly as data/items.json mints indices 31..94:
	// four consecutive chains per racer, roster order.
	slot = blockIndex / AP_CAP_CHAIN_COUNT;
	chain = blockIndex % AP_CAP_CHAIN_COUNT;
	g_cap_recv_pc[slot][chain]++;

	if (!g_cap_pc_logged)
	{
		char msg[192];
		g_cap_pc_logged = 1;
		snprintf(msg, sizeof msg,
		         "[AP CAP] per-character capability items are arriving (first: roster slot "
		         "%d, chain %d); the applied chain follows the character being driven\n",
		         slot, chain);
		AP_LogLine(msg);
	}
}

// ── Is a pack active for this seed, and in which mode? ──────────────────────
//
// Both live modes are read independently per pack, so boost can run
// per_character while stats stay shared_global. Anything else -- 0, or a mode
// value a later apworld invents that this build predates -- is treated as OFF: a
// mode we do not understand must not silently change the kart.
static int AP_CapabilityModeLive(int mode)
{
	return mode == AP_CAP_MODE_SHARED_GLOBAL || mode == AP_CAP_MODE_PER_CHARACTER;
}

static int AP_CapabilityBoostActive(void)
{
	return ctr_cfg_active() && AP_CapabilityModeLive(ctr_cfg.boost_mode);
}

static int AP_CapabilityStatsActive(void)
{
	return ctr_cfg_active() && AP_CapabilityModeLive(ctr_cfg.stats_mode);
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

static int AP_CapabilityRosterSlotForCharacter(int characterID);

// The roster slot of the character the local player is CURRENTLY driving, or -1
// if that cannot be established.
//
// `data.characterIDs[driver->driverID]` is the engine's own answer to "who is
// this driver", and is precisely the expression VehBirth_SetConsts uses to pick
// the class stat column -- so the capability ladder keys off the same source of
// truth as the vanilla stats it overrides, rather than a second opinion that
// could drift from it.
//
// Because this is resolved on every call rather than cached at connect, a hub
// character swap needs no notification and no invalidation hook: the next frame
// simply reads the new character and re-derives from that racer's own row.
static int AP_CapabilityCurrentRosterSlot(void)
{
	struct Driver *driver;
	int characterID;

	if (sdata == 0 || sdata->gGT == 0)
		return -1;

	driver = sdata->gGT->drivers[0];
	if (driver == 0)
		return -1;

	// characterIDs is an 8-entry table; driverID indexes it directly.
	if (driver->driverID >= 8)
		return -1;

	characterID = data.characterIDs[driver->driverID];

	return AP_CapabilityRosterSlotForCharacter(characterID);
}

// Translate an engine character ID to the apworld roster row. Kept separate
// from the live-driver lookup because menus can preview a character before a
// Driver exists.
static int AP_CapabilityRosterSlotForCharacter(int characterID)
{
	int i;

	for (i = 0; i < AP_CAP_ROSTER_COUNT; i++)
	{
		if (AP_CAP_ROSTER_CHARACTER[i] == characterID)
			return i;
	}
	return -1; // -1 (no character yet, menus) or a modded id: apply nothing
}

// Same mode dispatch as AP_CapabilityChainCount, with an explicit character
// for UI surfaces that are not backed by the current Driver.
static int AP_CapabilityChainCountForCharacter(int mode, int chain, int characterID)
{
	int slot;

	if (chain < 0 || chain >= AP_CAP_CHAIN_COUNT)
		return -1;

	if (mode == AP_CAP_MODE_PER_CHARACTER)
	{
		slot = AP_CapabilityRosterSlotForCharacter(characterID);
		if (slot < 0)
			return -1;
		return g_cap_recv_pc[slot][chain];
	}

	return g_cap_recv[chain];
}

// The received count for one chain under the seed's mode for that pack. Returns
// -1 when the pack is off, or when per_character mode cannot name the current
// character -- callers turn that into "leave the engine alone", which fails open
// to the vanilla kart rather than closed to the bottom of the ladder.
static int AP_CapabilityChainCount(int mode, int chain)
{
	int slot;

	if (chain < 0 || chain >= AP_CAP_CHAIN_COUNT)
		return -1;

	if (mode == AP_CAP_MODE_PER_CHARACTER)
	{
		slot = AP_CapabilityCurrentRosterSlot();
		if (slot < 0)
		{
			if (!g_cap_pc_unknown_logged)
			{
				g_cap_pc_unknown_logged = 1;
				AP_LogLine("[AP CAP] per_character mode is live but the current character could "
				           "not be identified -- applying NOTHING (vanilla kart) until it can\n");
			}
			return -1;
		}
		return g_cap_recv_pc[slot][chain];
	}

	return g_cap_recv[chain];
}

int AP_CapabilityBoostTier(void)
{
	int ceiling;
	int tier;

	if (!AP_CapabilityBoostActive())
		return -1;

	tier = AP_CapabilityChainCount(ctr_cfg.boost_mode, AP_CAP_CHAIN_BOOST);
	if (tier < 0)
		return -1;

	// Without the capstone toggle the chain is 2 copies (none -> boost -> USF);
	// with it, 3 (adds blue fire). Derived from the enum, not restated. The
	// ceiling is per chain, so in per_character mode every racer climbs the same
	// ladder on their own copies.
	ceiling = ctr_cfg.boost_blue_fire ? AP_CAP_BOOST_BLUEFIRE : AP_CAP_BOOST_USF;

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

	rank = AP_CapabilityChainCount(ctr_cfg.stats_mode, chain);
	if (rank < 0)
		return -1;

	if (rank > AP_CAP_STAT_COPIES)
		rank = AP_CAP_STAT_COPIES;
	return rank;
}

int AP_CapabilityStatRankForCharacter(int chain, int characterID)
{
	int rank;

	if (!AP_CapabilityStatsActive())
		return -1;
	if (chain <= AP_CAP_CHAIN_BOOST || chain >= AP_CAP_CHAIN_COUNT)
		return -1;

	rank = AP_CapabilityChainCountForCharacter(ctr_cfg.stats_mode, chain, characterID);
	if (rank < 0)
		return -1;

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

// The absolute value the ladder holds at `rank` for the stat row at `offset`,
// or -1 when the offset has no metaPhys row. Public so DISPLAY surfaces (the
// hub picker, issue #251) can promise exactly what this module writes instead
// of reimplementing the ladder -- the picker's vanilla-plus-delta model was
// precisely that reimplementation drifting from the physics.
int AP_CapabilityRankValueForOffset(int offset, int rank)
{
	const struct MetaPhys *row = AP_CapabilityMetaRow(offset);
	if (row == 0)
		return -1;
	return AP_CapabilityStatRankValue(row, rank);
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
