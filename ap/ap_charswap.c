#ifdef CTR_AP

#include <common.h> // struct Driver / GameTracker / Level, sdata, data, LevelID, BTN_*
#include <stddef.h> // offsetof
#include <stdio.h>
#include <string.h>

#include "ap_charname.h" // portrait-fallback name choice, shared with the harness
#include "ap_charseat.h" // deterministic stored-racer seat state machine
#include "ap_charswap.h"
#include "ap_hooks.h" // AP_LogLine, AP_DevKeysEnabled

// ============================================================================
// HUB CHARACTER PICKER + HUB SWAP -- feasibility prototype
//
// WHY THE SWAP IS A HUB RELOAD AND NOT AN IN-PLACE MUTATION
// --------------------------------------------------------
// In the adventure hub the engine loads exactly ONE driver asset pack:
// LOAD_DriverMPK queues BI_ADVENTUREPACK + data.characterIDs[0]
// (game/LOAD/LOAD_Assets.c:118-128) and nothing else; data.driverModelExtras[]
// is cleared at load stage 4 and never refilled on that path
// (game/LOAD/LOAD_TenStages.c:318-324). VehBirth_GetModelByName
// (game/Vehicle/VehBirth.c:490-526) therefore only ever finds the CURRENT
// character's model in the hub. Mutating data.characterIDs[0] in place and
// re-birthing would dereference a NULL struct Model*; mutating it WITHOUT a
// re-birth would leave the old physics constants in place, because
// VehBirth_SetConsts (VehBirth.c:529) runs once, at birth.
//
// So the swap re-requests the CURRENT hub level. That re-runs the whole ten-
// stage load: the new character's MPK is fetched, MainInit_Drivers re-births
// the player through VehBirth_Player -> VehBirth_NonGhost -> VehBirth_SetConsts
// (MainInit.c:374, VehBirth.c:710). Model, stats, engine audio index and kart
// ownership all come from the tested path; this prototype adds no new lifetime.
//
// Cost: a load screen. Route B (hot-loading a second adventure MPK mid-hub and
// re-birthing only the driver) is NOT built here -- see the research note for
// why it is a much larger surface (mempack swap, LibraryOfModels_Clear,
// LOAD_GlobalModelPtrs_MPK, DecalGlobal_Store, icon-group rebind).
//
// GATING (productionised from the spike, #54/#209). The picker is a real
// feature now, not a dev toy: it opens on any seed whose slot_data carries the
// character phase (ctr_cfg.character_unlocks, a racer lock, or a non-vanilla
// stat source), plus the same safe-hub-state conditions the spike established.
// The dev-key path survives ONLY as an extra way in when no such seed is
// connected, so the prototype's manual test matrix is still runnable.
// ============================================================================

int Platform_InputRawKeyDown(int scancode); // native_input.c (CTR_AP only)

// Numpad. 89-94 are the trap dev keys (ap_traps.c), 95/96 are Shortcutless
// (ap_shortcut.c). 97-99 are free.
#define AP_CS_KEY_PICKER 97 // Numpad 9 : open / close the picker
#define AP_CS_KEY_PROG   98 // Numpad 0 : cycle progressive_stats test mode
#define AP_CS_KEY_EDIT   99 // Numpad . : cycle editable_stats  test mode

// ---------------------------------------------------------------------------
// Tile layout.
//
// AP-owned copy of overlay 230's D230.csm_1P2P (game/230/D230.c:385-415), which
// is NOT reachable from the hub: the hub runs overlay 232 and D230's data
// segment is not resident. Copying the 15-tile grid is the whole "extraction"
// cost of reusing the Arcade picker's presentation -- every drawing primitive
// it uses (RECTMENU_*, CTR_Box_DrawSolidBox, DecalFont_DrawLine) lives in the
// EXE, not in 230, and so does data.MetaDataCharacters / gGT->ptrIcons.
//
// The 16th tile is new: retail's grid is 15 wide because Oxide has no arcade
// select entry. He drops into the free slot on the bottom row at x=0x160,
// which needs no layout redesign -- the row already has 0xA0/0xE0/0x120.
//
// THE ROWS ARE RAISED FROM RETAIL'S 0x60 / 0x87 / 0xAE.
// The draw environment is 512 x 216, not 512 x 240: MainMain.c:534-537 calls
// SetDefDrawEnv / SetDefDispEnv with h = 0xd8 = 216. Retail's grid ends at
// 0xAE + 0x21 = 207, which leaves nine lines under it -- so the character panel
// this picker owes the player had nowhere to go, and the first cut put it at
// y = 0xD8, exactly the bottom edge, where every line of it fell outside the
// framebuffer and nothing drew at all. MM_Characters can afford the low grid
// because its own stat panel lives inside the per-player 3D windows, which this
// picker does not have.
//
// So the grid moves up to 22 / 58 / 94 (pitch 36, tiles 0x21 = 33 tall, three
// lines of gap) and ends at 127, leaving 128..216 for the panel. Retail's X
// columns are untouched; only Y moves.
// ---------------------------------------------------------------------------
struct AP_CharTile
{
	short posX;
	short posY;
	signed char next[4]; // up, down, left, right (tile indices)
	short characterID;
};

#define AP_CS_TILES 16

#define AP_CS_ROW0 22
#define AP_CS_ROW1 58
#define AP_CS_ROW2 94

static const struct AP_CharTile ap_cs_tiles[AP_CS_TILES] = {
    /*  0 Crash      */ {0x080, AP_CS_ROW0, {0, 4, 8, 1}, CRASH_BANDICOOT},
    /*  1 Cortex     */ {0x0C0, AP_CS_ROW0, {1, 5, 0, 2}, NEO_CORTEX},
    /*  2 Tiny       */ {0x100, AP_CS_ROW0, {2, 6, 1, 3}, TINY_TIGER},
    /*  3 Coco       */ {0x140, AP_CS_ROW0, {3, 7, 2, 9}, COCO_BANDICOOT},
    /*  4 N. Gin     */ {0x080, AP_CS_ROW1, {0, 12, 10, 5}, N_GIN},
    /*  5 Dingodile  */ {0x0C0, AP_CS_ROW1, {1, 13, 4, 6}, DINGODILE},
    /*  6 Polar      */ {0x100, AP_CS_ROW1, {2, 14, 5, 7}, POLAR},
    /*  7 Pura       */ {0x140, AP_CS_ROW1, {3, 14, 6, 11}, PURA},
    /*  8 N. Tropy   */ {0x040, AP_CS_ROW0, {8, 10, 8, 0}, N_TROPY},
    /*  9 Pinstripe  */ {0x180, AP_CS_ROW0, {9, 11, 3, 9}, PINSTRIPE},
    /* 10 Ripper Roo */ {0x040, AP_CS_ROW1, {8, 10, 10, 4}, RIPPER_ROO},
    /* 11 Papu Papu  */ {0x180, AP_CS_ROW1, {9, 11, 7, 11}, PAPU_PAPU},
    /* 12 Komodo Joe */ {0x0A0, AP_CS_ROW2, {4, 12, 12, 13}, KOMODO_JOE},
    /* 13 Penta      */ {0x0E0, AP_CS_ROW2, {5, 13, 12, 14}, PENTA_PENGUIN},
    /* 14 Fake Crash */ {0x120, AP_CS_ROW2, {6, 14, 13, 15}, FAKE_CRASH},
    /* 15 N. Oxide   */ {0x160, AP_CS_ROW2, {7, 15, 14, 15}, NITROS_OXIDE},
};

// Navigation deliberately does NOT skip locked tiles, unlike
// MM_Characters_GetNextDriver (game/230/MM_Characters.c:53-78). Ruling
// 2026-08-08: all 16 portraits are presented, a locked racer may reveal its
// name and effective stats, and only CONFIRM is refused.

// ---------------------------------------------------------------------------
// Stat rows.
//
// Resolved by matching struct MetaPhys::offset against offsetof(struct Driver,
// field) instead of hardcoding row indices into data.metaPhys[65]. Lessons
// Learned #5: derive constants, never restate them. VehBirth_SetConsts writes
// &((u8 *)driver)[metaPhys->offset], so the offsets are struct Driver offsets.
// ---------------------------------------------------------------------------
struct AP_CsStatDef
{
	const char *label;
	int driverOffset;
	int step; // edit-mode increment
	int cap;  // hard clamp applied by the package writer
};

#define AP_CS_STATS 4

static const struct AP_CsStatDef ap_cs_stats[AP_CS_STATS] = {
    {"ACCEL", (int)offsetof(struct Driver, const_Accel_ClassStat), 32, 32767},
    // 0x6400 is the engine's own hard ceiling on usable top speed
    // (VehPhysGeneral_GetBaseSpeed, game/Vehicle/VehPhysGeneral.c:1130-1215).
    {"SPEED", (int)offsetof(struct Driver, const_Speed_ClassStat), 128, 0x6400},
    {"ACCSPD", (int)offsetof(struct Driver, const_AccelSpeed_ClassStat), 128, 0x6400},
    // const_TurnRate is a u8 (namespace_Vehicle.h:1409); vanilla is 24..30 and
    // the field WRAPS with no engine clamp, so the package writer owns the cap.
    {"TURN", (int)offsetof(struct Driver, const_TurnRate), 1, 60},
};

static int ap_cs_rowForOffset(int driverOffset)
{
	int i;
	for (i = 0; i < 65; i++)
	{
		if (data.metaPhys[i].offset == driverOffset)
			return i;
	}
	return -1;
}

// Penta PAL/NTSC (ruling R15) and the starting racer's stat-class override.
//
// Both only exist while VANILLA class stats are in play: the moment progressive
// or editable stats own the table, every racer -- Penta included -- reads that
// package instead, and these two are inert. The apworld says so in its own
// option text and `forced_options` warns when a player sets one anyway.
static int ap_cs_vanillaStatsInPlay(void)
{
	return !ctr_cfg_active() || ctr_cfg.stat_source == 0;
}

static int ap_cs_pentaUsesMaxClass(int characterID)
{
	if (characterID != PENTA_PENGUIN)
		return 0;
	if (!ctr_cfg_active())
		return 0;
	if (!ap_cs_vanillaStatsInPlay())
		return 0;
	return ctr_cfg.penta_stats == 1; // 0 pal (his own class) / 1 ntsc (MAX)
}

// Whether a HIGHER value is better on this stat's axis. Every row in the four
// normal classes is picked by this rule to produce the MAX column, and TURN is
// the one where "best" is the largest number too (TURN class is 30 against
// SPEED class's 24). Stated per stat rather than assumed uniform, because the
// underlying metaPhys table has 65 rows and only these four are read here.
static int ap_cs_maxClassValue(int row, int statIndex)
{
	int best = data.metaPhys[row].value[0];
	int c;
	(void)statIndex;
	for (c = 1; c < NUM_CLASSES; c++)
	{
		if (data.metaPhys[row].value[c] > best)
			best = data.metaPhys[row].value[c];
	}
	return best;
}

// The engine class a racer effectively drives with. Normally their own; the
// seed's starting racer may be forced onto a different one by
// `starting_stat_class` (1 balanced / 2 acceleration / 3 speed / 4 turning,
// 0 = leave it alone). Scoped to the STARTING racer on purpose: the option's
// own text promises exactly that, and widening it to a racer you unlocked would
// make the promise false.
static int ap_cs_effectiveEngineClass(int characterID)
{
	int engineID = data.MetaDataCharacters[characterID].engineID;

	if (!ctr_cfg_active() || !ap_cs_vanillaStatsInPlay())
		return engineID;
	if (ctr_cfg.starting_stat_class <= 0)
		return engineID;
	if (characterID != ctr_cfg.starting_character)
		return engineID;

	switch (ctr_cfg.starting_stat_class)
	{
	case 1: return BALANCED;
	case 2: return ACCEL;
	case 3: return SPEED;
	case 4: return TURN;
	default: return engineID;
	}
}

static int ap_cs_vanillaValue(int statIndex, int characterID)
{
	int row = ap_cs_rowForOffset(ap_cs_stats[statIndex].driverOffset);
	int engineID;

	if (row < 0)
		return -1;

	engineID = ap_cs_effectiveEngineClass(characterID);
	if ((unsigned)engineID >= NUM_CLASSES)
		return -1;

	// Penta on the NTSC setting reads the PAL/JP fifth "MAX" class, which this
	// build does not have as a metaPhys column (NUM_CLASSES is 4 here; the
	// PAL/JP EXE ships 5, as regionsEXE.h's own "4 for max, in pal" comment
	// says). Rather than widen the table -- which would move engineID, and
	// engineID doubles as the engine-AUDIO index (HOWL_Engine.c:147) -- resolve
	// MAX as what it demonstrably IS: a per-axis best-of the four normal
	// classes. Verified against the 65-row PAL table shipped in
	// CTR-ModSDK mods/Patches/USAUnlimitedPenta/assets/stats.bin, whose first
	// four columns reproduce this build's table exactly and whose fifth column
	// is, row for row, the best of those four on that row's own axis.
	if (ap_cs_pentaUsesMaxClass(characterID))
		return ap_cs_maxClassValue(row, statIndex);

	return data.metaPhys[row].value[engineID];
}

// ---------------------------------------------------------------------------
// Stat packages. (Spike seam 2, now real.)
//
// The ruled precedence between progressive and editable stats is NOT decided
// here and must never be: the apworld resolves it in exactly one function
// (worlds/ctr/characters.py effective_stat_config) and sends the OUTCOME as
// ctr_options.stat_source / stat_owner / stat_editing_allowed. This file reads
// those three and does what they say. That is deliberate -- Lessons Learned #12
// is about the text, the rules and the engine each holding their own copy of a
// rule and drifting apart.
//
//   stat_source progressive -> label GLOBAL / PER-CHARACTER, values come from
//                              received chains, READ ONLY, no edit control.
//   stat_source editable    -> label GLOBAL / PER-CHARACTER, values are the
//                              player's own package, editable.
//   stat_source vanilla     -> label VANILLA, read only.
//
// APPLY OWNERSHIP, so nothing is written twice: progressive stats are applied
// by ap_capability.c (AP_CapabilityStats, per-frame from VehPhysProc), which
// landed with the #12/#13 native consumer. This file applies only what that one
// does not: the editable package, the Penta PAL/NTSC table, and the starting
// racer's stat-class override. AP_CharSwap_ApplyStatPackage is a hard no-op
// while stat_source is progressive.
// ---------------------------------------------------------------------------
enum
{
	AP_CS_MODE_OFF = 0,
	AP_CS_MODE_GLOBAL = 1,
	AP_CS_MODE_PERCHAR = 2,
};

// Wire-derived. Dev overrides survive only for the prototype's manual matrix
// (tests 16-19) and only when no seed is driving the panel.
static int ap_cs_devProgMode = AP_CS_MODE_OFF;
static int ap_cs_devEditMode = AP_CS_MODE_OFF;

static int ap_cs_seedDrivesStats(void)
{
	return ctr_cfg_active() && ctr_cfg.stat_source != 0;
}

#define ap_cs_progMode ap_cs_resolvedProgMode()
#define ap_cs_editMode ap_cs_resolvedEditMode()

static int ap_cs_resolvedProgMode(void)
{
	if (ap_cs_seedDrivesStats())
		return (ctr_cfg.stat_source == 1) ? ctr_cfg.stat_owner : AP_CS_MODE_OFF;
	if (ctr_cfg_active())
		return AP_CS_MODE_OFF; // a live seed said "vanilla"; a dev key must not override it
	return ap_cs_devProgMode;
}

static int ap_cs_resolvedEditMode(void)
{
	if (ap_cs_seedDrivesStats())
	{
		// stat_editing_allowed is the apworld's own answer, already accounting
		// for the precedence. Never infer it from stat_source alone.
		if (ctr_cfg.stat_source == 2 && ctr_cfg.stat_editing_allowed)
			return ctr_cfg.stat_owner;
		return AP_CS_MODE_OFF;
	}
	if (ctr_cfg_active())
		return AP_CS_MODE_OFF;
	return ap_cs_devEditMode;
}

// Editable package: deltas over vanilla.
static short ap_cs_editGlobal[AP_CS_STATS];
static short ap_cs_editPerChar[AP_CS_TILES][AP_CS_STATS];

// Per-slot persistence of that package (issues #54/#209). The 2026-07-23 ruling
// routes edited stats to the AP server, not the local save, for the same
// machine-agnostic reason as the current racer.
//
// WIRE LAYOUT, owned here because this file is the only writer and the only
// reader: index 0..3 are the four global deltas, then 16 racers x 4 in engine
// character-id order. 4 + 64 = 68 = AP_NET_EDITSTAT_COUNT, which the network
// side length-checks so a truncated package is rejected rather than applied
// halfway.
//
// Tied to that length at COMPILE time rather than by the comment above: the pack
// and unpack loops below are driven by AP_CS_STATS / AP_CS_TILES while the
// buffers they fill are AP_NET_EDITSTAT_COUNT long, so if the two ever disagreed
// they would run off the end. There is one wire length and this is where the
// game-side layout is checked against it.
typedef char ap_cs_editLayoutMatchesWireLength
    [(AP_CS_STATS + AP_CS_TILES * AP_CS_STATS == AP_NET_EDITSTAT_COUNT) ? 1 : -1];

static unsigned ap_cs_editRestoredRev = 0;

static void ap_cs_editPack(int *out)
{
	int i, c;
	for (i = 0; i < AP_CS_STATS; i++)
		out[i] = ap_cs_editGlobal[i];
	for (c = 0; c < AP_CS_TILES; c++)
	{
		for (i = 0; i < AP_CS_STATS; i++)
			out[AP_CS_STATS + c * AP_CS_STATS + i] = ap_cs_editPerChar[c][i];
	}
}

static void ap_cs_editUnpack(const int *in)
{
	int i, c;
	for (i = 0; i < AP_CS_STATS; i++)
		ap_cs_editGlobal[i] = (short)in[i];
	for (c = 0; c < AP_CS_TILES; c++)
	{
		for (i = 0; i < AP_CS_STATS; i++)
			ap_cs_editPerChar[c][i] = (short)in[AP_CS_STATS + c * AP_CS_STATS + i];
	}
}

// Write the package after an edit. Only when the seed actually gives the player
// an editor: writing a package for a seed whose stats are owned by progressive
// chains would store state that seed can never use, and would then be restored
// onto a later seed that CAN.
static void ap_cs_editPersist(void)
{
	int packed[AP_NET_EDITSTAT_COUNT];

	if (!ctr_cfg_active() || !ctr_cfg.character_phase_present)
		return;
	if (!ctr_cfg.stat_editing_allowed)
		return;
	ap_cs_editPack(packed);
	ap_net_editstats_set(packed, AP_NET_EDITSTAT_COUNT);
}

// Apply a package that arrived from the server, once per revision. Same
// asynchronous-`Get` reasoning as the racer seat: the reply lands some frames
// after the subscribe, so a one-shot keyed on "have I restored yet" would miss
// it entirely.
static void ap_cs_editRestore(void)
{
	int packed[AP_NET_EDITSTAT_COUNT];
	unsigned rev;

	if (!ctr_cfg_active() || !ctr_cfg.character_phase_present)
		return;
	// Restore ONLY when the editable package is what owns the stats this seed.
	// A stored package from an earlier, editable seed must not quietly re-tune a
	// kart whose stats now come from progressive chains or from vanilla.
	if (!ctr_cfg.stat_editing_allowed)
		return;
	rev = ap_net_editstats_revision();
	if (rev == ap_cs_editRestoredRev)
		return;
	if (!ap_net_editstats_known(packed, AP_NET_EDITSTAT_COUNT))
		return;
	ap_cs_editRestoredRev = rev;
	ap_cs_editUnpack(packed);
	AP_LogLine("[AP CHARSWAP] editable stat package restored from AP data storage\n");
}

// Progressive package: a stand-in for "received Progressive copies". Per
// character it is deterministic and DISTINCT so a swap is falsifiable on
// screen: if the live driver still shows the previous character's numbers, the
// swap did not re-birth.
// The progressive contribution, for DISPLAY only.
//
// With a seed connected this reads the real received ranks out of
// ap_capability.c rather than restating the ladder, so the panel cannot promise
// a number the kart does not carry. The three panel stats that have a chain map
// onto AP_CAP_CHAIN_TOP_SPEED / ACCEL / TURNING; ACCSPD has no chain of its own
// and follows SPEED, which is what the engine does too (the accel-speed delta is
// read against const_Speed_ClassStat in VehPhysGeneral).
//
// This value is NOT written to the driver -- see ap_cs_packageActive.
static int ap_cs_progChainForStat(int statIndex)
{
	switch (statIndex)
	{
	case 0: return AP_CAP_CHAIN_ACCEL;     // ACCEL
	case 1: return AP_CAP_CHAIN_TOP_SPEED; // SPEED
	case 2: return AP_CAP_CHAIN_TOP_SPEED; // ACCSPD follows top speed
	case 3: return AP_CAP_CHAIN_TURNING;   // TURN
	default: return -1;
	}
}

static int ap_cs_progDelta(int statIndex, int characterID)
{
	int chain;
	int rank;

	if (ap_cs_progMode == AP_CS_MODE_OFF)
		return 0;

	if (!ctr_cfg_active())
	{
		// Dev fallback only (no seed). Per-character is characterID-keyed so
		// all 16 read differently and a swap is falsifiable on screen.
		if (ap_cs_progMode == AP_CS_MODE_GLOBAL)
			return ap_cs_stats[statIndex].step * 4;
		return ap_cs_stats[statIndex].step * (characterID + 1);
	}

	chain = ap_cs_progChainForStat(statIndex);
	if (chain < 0)
		return 0;
	rank = (ap_cs_progMode == AP_CS_MODE_PERCHAR)
	           ? AP_CapabilityStatRankForCharacter(chain, characterID)
	           : AP_CapabilityStatRankFor(chain);
	if (rank <= 0)
		return 0;
	return ap_cs_stats[statIndex].step * rank;
}

static int ap_cs_editDelta(int statIndex, int characterID)
{
	if (ap_cs_progMode != AP_CS_MODE_OFF)
		return 0; // progressive takes precedence; editor is inert
	if (ap_cs_editMode == AP_CS_MODE_GLOBAL)
		return ap_cs_editGlobal[statIndex];
	if (ap_cs_editMode == AP_CS_MODE_PERCHAR)
		return ap_cs_editPerChar[characterID][statIndex];
	return 0;
}

static int ap_cs_clamp(int statIndex, int value)
{
	int cap = ap_cs_stats[statIndex].cap;
	if (value < 0)
		value = 0;
	if (value > cap)
		value = cap;
	return value;
}

// The effective value the picker promises for this character.
static int ap_cs_effectiveValue(int statIndex, int characterID)
{
	int base = ap_cs_vanillaValue(statIndex, characterID);
	if (base < 0)
		return -1;
	return ap_cs_clamp(statIndex, base + ap_cs_progDelta(statIndex, characterID) + ap_cs_editDelta(statIndex, characterID));
}

// Is there anything for THIS file to write at driver birth?
//
// Progressive stats are deliberately NOT in this list: ap_capability.c owns
// them (AP_CapabilityStats, per-frame from VehPhysProc) and writing them here
// too would apply the same package twice through two different clamps. What is
// left is the editable package, Penta's MAX table and the starting racer's
// class override -- all three of which only exist while the stat source is
// vanilla or editable.
static int ap_cs_packageActive(void)
{
	if (ap_cs_editMode != AP_CS_MODE_OFF)
		return 1;
	if (!ctr_cfg_active())
		return ap_cs_progMode != AP_CS_MODE_OFF; // dev fallback, no seed connected
	if (ctr_cfg.stat_source == 1)
		return 0; // progressive: ap_capability.c owns the write
	if (ctr_cfg.penta_stats == 1)
		return 1; // Penta's MAX table has to be written by someone
	if (ctr_cfg.starting_stat_class > 0)
		return 1; // so does a forced starting class
	return 0;
}

// The value the LIVE driver is currently carrying, read straight out of the
// struct the vanilla loop writes. This is the proof surface: after a swap the
// live numbers must equal the picker's promised numbers for the new character.
static int ap_cs_liveValue(struct Driver *d, int statIndex)
{
	unsigned char *base;
	int off = ap_cs_stats[statIndex].driverOffset;
	int row = ap_cs_rowForOffset(off);

	if ((d == NULL) || (row < 0))
		return -1;

	base = (unsigned char *)d + off;

	if (data.metaPhys[row].size == 1)
		return (int)base[0];
	if (data.metaPhys[row].size == 2)
		return (int)(short)((unsigned short)base[0] | ((unsigned short)base[1] << 8));
	return (int)((unsigned)base[0] | ((unsigned)base[1] << 8) | ((unsigned)base[2] << 16) | ((unsigned)base[3] << 24));
}

// Post-pass over VehBirth_SetConsts. Local player only: SetConsts runs for
// every driver including bots and the ghost (GhostReplay.c:441), and an
// ungated package would hand the whole field the player's upgrades.
void AP_CharSwap_ApplyStatPackage(struct Driver *driver)
{
	struct GameTracker *gGT = sdata->gGT;
	int characterID;
	int i;

	if ((driver == NULL) || (gGT == NULL))
		return;
	if (!ap_cs_packageActive())
		return;
	if (driver->driverID != 0)
		return; // local player only
	if (gGT->drivers[0] != driver)
		return; // and only the real P1 driver object, never the ghost

	characterID = data.characterIDs[0];
	if ((unsigned)characterID >= AP_CS_TILES)
		return;

	for (i = 0; i < AP_CS_STATS; i++)
	{
		int row = ap_cs_rowForOffset(ap_cs_stats[i].driverOffset);
		int value = ap_cs_effectiveValue(i, characterID);
		unsigned char *dst;

		if ((row < 0) || (value < 0))
			continue;

		dst = (unsigned char *)driver + ap_cs_stats[i].driverOffset;

		// Write exactly as wide as the metaPhys row says, and never wider:
		// const_TurnRate is one byte and the engine does not clamp it.
		if (data.metaPhys[row].size == 1)
		{
			if (value > 255)
				value = 255;
			dst[0] = (unsigned char)value;
		}
		else if (data.metaPhys[row].size == 2)
		{
			if (value > 32767)
				value = 32767;
			dst[0] = (unsigned char)value;
			dst[1] = (unsigned char)(value >> 8);
		}
	}
}

// ---------------------------------------------------------------------------
// Unlock state. (Spike seam 1, now real.)
//
// The 15 unlock items exist (apworld characters.py, item indices 123..138) and
// AP_CharacterUnlocked in ap_capability.c is the single answer to "can I be
// this racer": it already folds in the seed's starting racer, the ruled
// all-unlocked comfort mode, and the received items. Nothing is re-derived
// here, so the picker and the pad gate can never disagree about who is
// playable -- which is the whole point, because a pad that demands a racer you
// cannot select is an unwinnable seed.
//
// The dev override stays for the prototype's manual matrix (test 4 explicitly
// wants a locked-tile presentation on demand) and defaults OFF, so a real seed
// is never affected by it.
// ---------------------------------------------------------------------------
static int ap_cs_devUnlockAll = 0;

static int ap_cs_isUnlocked(int characterID)
{
	if (ap_cs_devUnlockAll && AP_DevKeysEnabled())
		return 1;
	return AP_CharacterUnlocked(characterID);
}

// Defined further down, next to the seat/restore half it pairs with; declared
// here because the swap request below fires before that point in the file.
static void ap_cs_persistCharacter(int characterID);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static int ap_cs_open = 0;
static int ap_cs_cursor = 0;   // tile index
static int ap_cs_editFocus = 0;// editing the stat panel rather than the grid
static int ap_cs_statRow = 0;  // highlighted stat row while editing
static int ap_cs_frozeDriver = 0;

static int ap_cs_pendingSwap = 0;  // reload requested this frame
static int ap_cs_restorePos = 0;   // restore hub position after the reload
static int ap_cs_savedPos[3];
static int ap_cs_savedRotY;
static int ap_cs_savedLevel = -1;
static int ap_cs_fromCharacter = -1;

static int ap_cs_keyPrev[3];

int AP_CharSwap_PickerOpen(void)
{
	return ap_cs_open;
}

// The adventure-hub pause menu asked for the picker (#238).
//
// It cannot be opened from inside the pause, and should not be:
// ap_cs_safeToOpen refuses while PAUSE_1 or sdata->pause_state is set, because
// the pause owns the vehicle-freeze bits and its RectMenu owns input, and two
// modal surfaces fighting over both is the interaction hazard the spike's matrix
// row 22 flagged. So the pause row resumes the game through the vanilla RESUME
// path and leaves this request behind; AP_CharSwap_Tick picks it up on the first
// frame that is genuinely safe, a frame or two later once the unpause settles.
//
// A request is one bit with no deadline, on the same reasoning ap_charseat.h
// gives for the seat deferral: it is honoured when the hub is safe and dropped
// when the hub goes away, so nothing has to guess how many frames an unpause
// takes. It cannot leak into a later session, because leaving the hub clears it.
static int ap_cs_pauseRequest = 0;

void AP_CharSwap_RequestPickerFromPause(void)
{
	if (!AP_CharSwap_PauseRowLive())
		return;
	ap_cs_pauseRequest = 1;
}

// Whether the pause menu should carry the row at all. Deliberately the same
// condition AP_CharSwap_Tick gates itself on, so a row can never be offered on a
// seed where selecting it would do nothing.
int AP_CharSwap_PauseRowLive(void)
{
	return AP_CharSwap_FeatureLive() || AP_DevKeysEnabled();
}

static int ap_cs_tileForCharacter(int characterID)
{
	int i;
	for (i = 0; i < AP_CS_TILES; i++)
	{
		if (ap_cs_tiles[i].characterID == characterID)
			return i;
	}
	return 0;
}

static int ap_cs_hubReady(struct GameTracker *gGT)
{
	if (gGT == NULL)
		return 0;
	if ((gGT->gameMode1 & ADVENTURE_ARENA) == 0)
		return 0;
	if (LOAD_IsOpen_AdvHub() == 0)
		return 0;
	if (sdata->Loading.stage != LOAD_IDLE)
		return 0;
	if (gGT->drivers[0] == NULL)
		return 0;
	return 1;
}

// A swap is only offered where the hub is genuinely idle. Cutscene / boss-key
// door / mask-hint states already own the freeze bits and the camera, and the
// podium freeze means a reward ceremony is running.
static int ap_cs_safeToOpen(struct GameTracker *gGT)
{
	if (!ap_cs_hubReady(gGT))
		return 0;
	if ((gGT->gameMode1 & (PAUSE_1 | LOADING | GAME_CUTSCENE)) != 0)
		return 0;
	if ((gGT->gameMode2 & GAME_MODE2_VEH_FREEZE_MASK) != 0)
		return 0; // a door / podium sequence already owns the freeze
	if ((gGT->gameMode2 & SPAWN_AT_BOSS) != 0)
		return 0;
	if (sdata->AkuAkuHintState != 0)
		return 0;
	if (sdata->pause_state != 0)
		return 0;
	// No RectMenu may still own the screen.
	//
	// This gate was missing, and the #238 pause row is what exposed the gap: that
	// row resumes the game and leaves a request behind, and PAUSE_1 is cleared by
	// the RESUME branch immediately while the menu itself closes over several
	// frames (RECTMENU_Hide only sets NEEDS_TO_CLOSE). Without this test the
	// picker could open while the pause menu was still live, so both surfaces
	// owned the screen at once, which is the whole thing the request mechanism
	// exists to avoid. `ptrActiveMenu != NULL` is the engine's own way of asking
	// this: MainFreeze_IfPressStart refuses to raise the pause menu on exactly
	// that test.
	if (sdata->ptrActiveMenu != NULL)
		return 0;
	return 1;
}

static void ap_cs_setFreeze(struct GameTracker *gGT, int on)
{
	if (on)
	{
		gGT->gameMode2 |= VEH_FREEZE_DOOR;
		ap_cs_frozeDriver = 1;
	}
	else if (ap_cs_frozeDriver)
	{
		gGT->gameMode2 &= ~VEH_FREEZE_DOOR;
		ap_cs_frozeDriver = 0;
	}
}

static void ap_cs_logPackage(const char *tag, struct Driver *d, int characterID)
{
	char msg[192];
	snprintf(msg, sizeof msg,
	         "[AP CHARSWAP] %s char=%d prog=%d edit=%d live A=%d S=%d AS=%d T=%d want A=%d S=%d AS=%d T=%d\n",
	         tag, characterID, ap_cs_progMode, ap_cs_editMode,
	         ap_cs_liveValue(d, 0), ap_cs_liveValue(d, 1), ap_cs_liveValue(d, 2), ap_cs_liveValue(d, 3),
	         ap_cs_effectiveValue(0, characterID), ap_cs_effectiveValue(1, characterID),
	         ap_cs_effectiveValue(2, characterID), ap_cs_effectiveValue(3, characterID));
	AP_LogLine(msg);
}

// Re-apply the package to the live driver without a re-birth. Only legitimate
// for the STAT package (the model does not change), and it is exactly what
// CTR-ModSDK's PracticeROM does for its in-race engine swap: write the source
// field, then call VehBirth_SetConsts(player) again
// (mods/Standalones/PracticeROM/src/p_rom.c:386-395).
static void ap_cs_reapplyLive(struct GameTracker *gGT)
{
	struct Driver *d = gGT->drivers[0];
	if (d == NULL)
		return;
	VehBirth_SetConsts(d);
	AP_CharSwap_ApplyStatPackage(d);
}

// ---------------------------------------------------------------------------
// The swap itself
// ---------------------------------------------------------------------------
static void ap_cs_requestSwap(struct GameTracker *gGT, int characterID)
{
	struct Driver *d = gGT->drivers[0];
	char msg[160];

	if (d == NULL)
		return;

	ap_cs_savedPos[0] = d->posCurr.x;
	ap_cs_savedPos[1] = d->posCurr.y;
	ap_cs_savedPos[2] = d->posCurr.z;
	ap_cs_savedRotY = d->rotCurr.y;
	ap_cs_savedLevel = gGT->levelID;
	ap_cs_fromCharacter = data.characterIDs[0];

	// The character the hub reloads with. advProgress.characterID is the same
	// value the Garage confirm writes (the two commit sites in CS_Garage_MenuProc, game/233/CS_Garage.c:479-480 and :576-577), so the hub
	// re-enters in exactly the state a fresh Garage pick would produce.
	//
	// Persistence (spike seam 3) is wired: ap_cs_persistCharacter writes the
	// choice to per-slot AP data storage, and AP_CharSwap_SeatStartingCharacter
	// reads it back on the next connect. slot_data is deliberately not the
	// carrier -- it is frozen and sent once, and this value is mutable.
	data.characterIDs[0] = (short)characterID;
	sdata->advProgress.characterID = (s16)characterID;
	// Persist before the reload, not after: the reload tears the level down and
	// a crash or a quit mid-load must not lose the choice the player just made.
	ap_cs_persistCharacter(characterID);

	snprintf(msg, sizeof msg, "[AP CHARSWAP] request lvl=%d %d -> %d pos=%d,%d,%d\n",
	         (int)gGT->levelID, ap_cs_fromCharacter, characterID,
	         ap_cs_savedPos[0], ap_cs_savedPos[1], ap_cs_savedPos[2]);
	AP_LogLine(msg);

	ap_cs_logPackage("before", d, ap_cs_fromCharacter);

	ap_cs_setFreeze(gGT, 0);
	ap_cs_open = 0;
	ap_cs_editFocus = 0;
	ap_cs_pendingSwap = 1;
	ap_cs_restorePos = 1;

	// Re-request the hub we are standing in. LOAD_LevelFile then re-runs the
	// full ten-stage load, so LOAD_DriverMPK re-queues BI_ADVENTUREPACK for the
	// new characterID and MainInit_Drivers re-births the player.
	MainRaceTrack_RequestLoad(gGT->levelID);
}

// After the reload the player would spawn wherever VehBirth_TeleportSelf put
// them. Put them back where the picker was opened. VehBirth_TeleportSelf with
// spawnFlag 0 re-runs the BSP ground search from posCurr, which is the engine's
// own "settle me at my current position" primitive (VehBirth.c:160-168).
static void ap_cs_completeSwap(struct GameTracker *gGT)
{
	struct Driver *d = gGT->drivers[0];
	char msg[160];

	if (d == NULL)
		return;

	if (gGT->levelID == ap_cs_savedLevel)
	{
		d->posCurr.x = ap_cs_savedPos[0];
		d->posCurr.y = ap_cs_savedPos[1];
		d->posCurr.z = ap_cs_savedPos[2];
		d->rotCurr.y = (short)ap_cs_savedRotY;
		VehBirth_TeleportSelf(d, 0, 0);
	}

	// The package post-pass normally rides on the birth-time SetConsts call.
	// Re-assert it here so the restored driver is definitely carrying it.
	AP_CharSwap_ApplyStatPackage(d);

	snprintf(msg, sizeof msg, "[AP CHARSWAP] complete lvl=%d char=%d restoredPos=%d\n",
	         (int)gGT->levelID, (int)data.characterIDs[0],
	         (gGT->levelID == ap_cs_savedLevel) ? 1 : 0);
	AP_LogLine(msg);

	ap_cs_logPackage("after", d, data.characterIDs[0]);

	ap_cs_restorePos = 0;
	ap_cs_pendingSwap = 0;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
// Opening the picker, in one place. Both ways in (the dev key and the #238 pause
// row) go through here, so a safety condition can never be enforced on one entry
// point and forgotten on the other.
static int ap_cs_tryOpen(struct GameTracker *gGT)
{
	if (!ap_cs_safeToOpen(gGT))
		return 0;

	ap_cs_open = 1;
	ap_cs_editFocus = 0;
	ap_cs_cursor = ap_cs_tileForCharacter(data.characterIDs[0]);
	ap_cs_setFreeze(gGT, 1);
	ap_cs_logPackage("open", gGT->drivers[0], data.characterIDs[0]);
	return 1;
}

static void ap_cs_close(struct GameTracker *gGT)
{
	ap_cs_open = 0;
	ap_cs_editFocus = 0;
	ap_cs_setFreeze(gGT, 0);
}

static void ap_cs_devKeys(struct GameTracker *gGT)
{
	static const int keys[3] = {AP_CS_KEY_PICKER, AP_CS_KEY_PROG, AP_CS_KEY_EDIT};
	int k;

	for (k = 0; k < 3; k++)
	{
		int down = Platform_InputRawKeyDown(keys[k]);
		int tapped = down && !ap_cs_keyPrev[k];
		ap_cs_keyPrev[k] = down;

		if (!tapped)
			continue;

		if (k == 0)
		{
			if (ap_cs_open)
				ap_cs_close(gGT);
			else
				ap_cs_tryOpen(gGT);
		}
		// The two mode-cycle dev keys only move the DEV fallbacks, and only
		// matter while no seed is connected: with a seed live, the resolvers
		// above ignore them entirely. A dev key that could override a seed's
		// own stat configuration would be a way to desync the client from the
		// generation that produced it.
		else if (k == 1)
		{
			ap_cs_devProgMode = (ap_cs_devProgMode + 1) % 3;
			if (ap_cs_progMode != AP_CS_MODE_OFF)
				ap_cs_editFocus = 0; // ruled precedence: no edit control at all
			ap_cs_reapplyLive(gGT);
		}
		else
		{
			ap_cs_devEditMode = (ap_cs_devEditMode + 1) % 3;
			if (ap_cs_editMode == AP_CS_MODE_OFF)
				ap_cs_editFocus = 0;
			ap_cs_reapplyLive(gGT);
		}
	}
}

static int ap_cs_editorAvailable(void)
{
	// The ruled matrix in one line: the editor exists only when progressive
	// stats are OFF and editable stats are not OFF. Progressive mode can never
	// expose editing.
	return (ap_cs_progMode == AP_CS_MODE_OFF) && (ap_cs_editMode != AP_CS_MODE_OFF);
}

static void ap_cs_adjust(struct GameTracker *gGT, int dir)
{
	int characterID = ap_cs_tiles[ap_cs_cursor].characterID;
	short *slot;
	int base, next;

	if (!ap_cs_editorAvailable())
		return;

	slot = (ap_cs_editMode == AP_CS_MODE_GLOBAL) ? &ap_cs_editGlobal[ap_cs_statRow] : &ap_cs_editPerChar[characterID][ap_cs_statRow];

	base = ap_cs_vanillaValue(ap_cs_statRow, characterID);
	if (base < 0)
		return;

	next = base + *slot + dir * ap_cs_stats[ap_cs_statRow].step;
	next = ap_cs_clamp(ap_cs_statRow, next);
	*slot = (short)(next - base);

	// Only the character you are standing as can be applied live; the others
	// take effect when you swap to them.
	if (characterID == data.characterIDs[0])
		ap_cs_reapplyLive(gGT);

	// Persist on every edit rather than on close. There is no "close" event the
	// player is obliged to reach -- they can quit, crash or lose the connection
	// with the panel open, and a tune that only survives a graceful exit is not
	// really persisted.
	ap_cs_editPersist();

	OtherFX_Play(0, 1);
}

// Where the picker's navigation input comes from, and why NOT buttonTapPerPlayer.
//
// The first cut read sdata->buttonTapPerPlayer[0]. That array is dead in the
// adventure hub. Its ONLY writer is RECTMENU_CollectInput (game/RECTMENU.c:735),
// and the only call to it is guarded:
//
//     if ((sdata->ptrActiveMenu != 0) || ((gGT->gameMode1 & END_OF_RACE) != 0))
//         RECTMENU_CollectInput();                 // game/MAIN/MainFrame_RenderFrame.c:49-52
//
// Standing in the hub there is no active RectMenu and the race is not over, so
// neither disjunct holds and the array is never refreshed. It therefore holds
// whatever the last menu left -- and this function used to zero it on the way
// out, so after one frame it was permanently 0. Every tap test failed, which is
// exactly the observed symptom: the grid drew, the kart stayed frozen, and no
// input of ANY kind moved the cursor. Controller and keyboard both feed the same
// GamepadBuffer, so a dead read explains both being dead at once where a
// device-mapping fault would not.
//
// The pad buffer itself is written every frame with no menu gating:
// GAMEPAD_ProcessTapRelease computes buttonsTapped as
// `~buttonsHeldPrevFrame & buttonsHeldCurrFrame` (game/GAMEPAD.c:695) and is
// reached unconditionally through GAMEPAD_ProcessAnyoneVars (game/GAMEPAD.c:925,
// called from game/MAIN/MainMain.c:345). Reading it directly is the established
// idiom for code that runs outside a menu -- game/233/CS_Camera.c:303,
// game/UI/UI_RenderFrame.c:81, game/230/MM_ConfigMenu.c:342 and
// game/232/AH_MaskHint.c:433 all do exactly this.
//
// ONE FRAME OF LATENCY, DELIBERATELY ACCEPTED. AP_OnFrame runs at
// MainMain.c:323, ahead of the GAMEPAD_ProcessAnyoneVars call at :345, so at
// frame N we read the word computed during frame N-1. Each frame's value is
// still read exactly once, so every tap is seen exactly once and none is lost or
// doubled; the cursor simply moves one frame after the press.
//
// We do NOT clear the bits we consume. The pad word is recomputed wholesale at
// :345, later in this same frame, so a clear here would be overwritten before
// any other consumer looked at it -- suppression is simply not achievable from
// this slot. The picker's hold on the kart comes from VEH_FREEZE_DOOR instead,
// which VehPhysProc_Driving_PhysLinear honours
// (game/Vehicle/VehPhysProc.c:407-411), and the one other consumer that mattered
// is refused at its own source rather than by stealing its input:
// MainFreeze_IfPressStart returns early while the picker is open, so Start
// cannot raise the pause menu on top of it (#238, and the spike matrix's row 22).
static void ap_cs_input(struct GameTracker *gGT)
{
	unsigned int tap;
	int characterID;

	if (sdata->gGamepads == NULL || sdata->gGamepads->numGamepadsConnected <= 0)
		return;

	tap = sdata->gGamepads->gamepad[0].buttonsTapped;

	if (tap == 0)
		return;

	characterID = ap_cs_tiles[ap_cs_cursor].characterID;

	if (ap_cs_editFocus)
	{
		if ((tap & BTN_UP) != 0)
			ap_cs_statRow = (ap_cs_statRow + AP_CS_STATS - 1) % AP_CS_STATS;
		else if ((tap & BTN_DOWN) != 0)
			ap_cs_statRow = (ap_cs_statRow + 1) % AP_CS_STATS;
		else if ((tap & BTN_LEFT) != 0)
			ap_cs_adjust(gGT, -1);
		else if ((tap & BTN_RIGHT) != 0)
			ap_cs_adjust(gGT, 1);
		else if ((tap & (BTN_SQUARE_one | BTN_TRIANGLE)) != 0)
			ap_cs_editFocus = 0;

		return;
	}

	if ((tap & BTN_UP) != 0)
		ap_cs_cursor = ap_cs_tiles[ap_cs_cursor].next[0];
	else if ((tap & BTN_DOWN) != 0)
		ap_cs_cursor = ap_cs_tiles[ap_cs_cursor].next[1];
	else if ((tap & BTN_LEFT) != 0)
		ap_cs_cursor = ap_cs_tiles[ap_cs_cursor].next[2];
	else if ((tap & BTN_RIGHT) != 0)
		ap_cs_cursor = ap_cs_tiles[ap_cs_cursor].next[3];

	if ((tap & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) != 0)
		OtherFX_Play(0, 1);

	if ((tap & BTN_SQUARE_one) != 0)
	{
		if (ap_cs_editorAvailable())
			ap_cs_editFocus = 1;
	}
	else if ((tap & (BTN_CROSS_one | BTN_CIRCLE)) != 0)
	{
		if (characterID == data.characterIDs[0])
		{
			// already this character; treat as a cancel rather than a reload
			ap_cs_open = 0;
			ap_cs_setFreeze(gGT, 0);
			OtherFX_Play(2, 1);
		}
		else if (ap_cs_isUnlocked(characterID))
		{
			OtherFX_Play(1, 1);
			ap_cs_requestSwap(gGT, characterID);
		}
		else
		{
			OtherFX_Play(2, 1); // locked: name + stats visible, confirm refused
		}
	}
	else if ((tap & BTN_TRIANGLE) != 0)
	{
		ap_cs_open = 0;
		ap_cs_setFreeze(gGT, 0);
		OtherFX_Play(2, 1);
	}
}

// Does this seed carry the character phase at all?
//
// Keyed on PRESENCE of the character-phase wire keys, never on their values.
// The two cases that has to separate are otherwise identical on the wire:
//
//   * an OLD seed, from an apworld that predates the feature, carrying none of
//     the keys. It has no unlock items and no roster concept, so the picker
//     must stay shut -- a pre-0.2.0 seed has to keep behaving like one.
//   * a NEW seed that set `character_unlocks: false` (the ruled all-unlocked
//     comfort mode) and left everything else at its default: Crash as the
//     starter, vanilla stats, no locks. Every scalar reads default, and yet all
//     16 racers ARE available and the hub picker is the ONLY way to choose
//     among them.
//
// An earlier cut of this function tested the VALUES and so answered "no
// feature" to the second case, which deleted the picker on exactly the
// configuration that exists to give you the whole roster. ctr_cfg tracks key
// presence for this reason; do not re-derive it from a defaulted scalar.
int AP_CharSwap_FeatureLive(void)
{
	if (!ctr_cfg_active())
		return 0;
	return ctr_cfg.character_phase_present != 0;
}

// Apply the seed's starting racer once per session (spike seam 4's sibling).
//
// The apworld picks the racer in YAML and never places it as an item, so the
// client has to seat it -- otherwise a seed that says "you are Ripper Roo"
// starts you as whoever the save file holds, and on a racer-locked seed that is
// the difference between a solvable and an unsolvable run. Written to the same
// two places a Garage confirm writes (the two commit sites in CS_Garage_MenuProc, game/233/CS_Garage.c:479-480 and :576-577), so the hub
// enters in exactly the state a fresh pick would produce.
//
// Runs once per authoritative answer: after the application the player owns
// their choice through the picker, and re-seating it every frame would fight
// every swap. WHICH racer, and WHEN it is safe to apply, is decided by the pure
// state machine in ap_charseat.c -- see that file's header for the connect
// orderings it exists to get right. This file only supplies live inputs and
// performs the writes it is told to perform.
//
// It supplies NO notion of "the item replay has finished", because the client
// has none to give: apclientpp reports each ReceivedItems packet's starting
// index and nothing else, so a stored racer whose unlock has not arrived stays
// pending indefinitely rather than being abandoned on a timer.
static struct AP_SeatState ap_cs_seat;

// The seat machine carries its own roster size (AP_SEAT_ROSTER) so it can stay
// free of engine and AP headers and be compiled on its own by the harness. This
// is the one place all three roster counts are visible at once, so it is where
// that copy is pinned to the engine's roster and the capability tracker's rather
// than left to drift.
typedef char ap_cs_seatRosterMatchesEngine
    [(AP_SEAT_ROSTER == AP_CAP_ROSTER_COUNT && AP_SEAT_ROSTER == NITROS_OXIDE + 1) ? 1 : -1];

// Re-arm the one-shot seat and drop every scrap of the previous slot's state.
// Called from the fresh-connect path so a reconnect or a slot switch re-applies
// the AUTHORITATIVE racer and the AUTHORITATIVE stat package instead of keeping
// whatever the previous connection left in memory.
void AP_CharSwap_ConnectReset(void)
{
	AP_SeatReset(&ap_cs_seat);
	ap_cs_editRestoredRev = 0;

	// Zero the live edit tables, not just the revision that tracks them.
	//
	// Clearing only ap_cs_editRestoredRev leaves the previous slot's deltas
	// populated and in effect. If the slot we are connecting to has no
	// ctr_editstats_<slot> key at all, no restore ever runs, nothing overwrites
	// them, and the player silently drives slot A's tune on slot B. Zeroing here
	// -- synchronously, in the connect handler -- also closes the window before
	// the asynchronous stored package can arrive: a new slot starts at zero
	// deltas and only a package that actually belongs to it can move them.
	memset(ap_cs_editGlobal, 0, sizeof ap_cs_editGlobal);
	memset(ap_cs_editPerChar, 0, sizeof ap_cs_editPerChar);
}

// Persist the racer the player just chose (spike seam 3, now real).
//
// The selected racer is mutable non-item state, so it rides neither slot_data
// (frozen, sent once) nor the local save. It goes to per-slot AP data storage,
// the same path the AI-difficulty override uses -- the 2026-07-23 ruling picked
// that deliberately: "our whole build so far has been machine-agnostic".
static void ap_cs_persistCharacter(int characterID)
{
	if (!ctr_cfg_active() || !ctr_cfg.character_phase_present)
		return;
	ap_net_character_set(characterID);
	// The player's own pick outranks anything still pending from storage. Without
	// this, an unlock arriving a few frames later would let a deferred stored
	// racer yank them back off the racer they just chose.
	AP_SeatLocalChoice(&ap_cs_seat, ap_net_character_revision());
}

// Write a racer into the two places a Garage confirm writes
// (the two commit sites in CS_Garage_MenuProc, game/233/CS_Garage.c:479-480 and :576-577), so the hub enters exactly the state a fresh pick
// would produce. Silent when the racer is already seated.
static void ap_cs_seatApply(int characterID, const char *source)
{
	char msg[160];

	if (characterID < 0 || characterID > NITROS_OXIDE)
		return;
	if (data.characterIDs[0] == (short)characterID)
		return; // already there; nothing to do and nothing to say

	snprintf(msg, sizeof msg, "[AP CHARSWAP] seating racer %d -> %d (%s)\n",
	         (int)data.characterIDs[0], characterID, source);
	AP_LogLine(msg);

	data.characterIDs[0] = (short)characterID;
	sdata->advProgress.characterID = (s16)characterID;
}

// Which racer the adventure-start Garage must commit, or -1 when it should run
// exactly as it always has (#54/#209).
//
// WHY THE GARAGE NEEDS GATING AT ALL. Starting a new Adventure loads
// ADVENTURE_GARAGE (game/230/MM_Title.c:138, the only such request in the tree)
// and the garage's own picker commits whatever the player highlighted, writing
// data.characterIDs[0] and sdata->advProgress.characterID at
// game/233/CS_Garage.c:380-381 and again at :466-467. Those are the same two
// fields this file seats the seed's racer into, and the garage writes them
// LATER, so on a seed with character_unlocks on, a player could pick any of the
// eight vanilla starters and actually become them. That is an unlock-enforcement
// bypass straight through the vanilla entry path, and it was observed live: a
// seed whose starting racer was Neo Cortex loaded in as Dingodile.
//
// The answer is the seat machine's, not a second opinion: AP_SeatResolve applies
// the same precedence AP_SeatStep does, so a stored racer from a previous
// session wins over the seed's starting racer exactly as it does everywhere
// else, and a stored racer whose unlock has not arrived falls back to the
// starter while the deferral keeps its claim.
//
// -1 for a seed that does not carry the character phase, so a pre-0.2.0 seed and
// an offline game keep the retail garage untouched. Note this is FeatureLive
// only, deliberately not AP_DevKeysEnabled: with no seed connected there is no
// authoritative racer to seat, so there is nothing to enforce.
int AP_CharSwap_GarageRacer(void)
{
	struct AP_SeatInput in;
	int                 i;

	if (!AP_CharSwap_FeatureLive())
		return -1;

	in.rev          = 0;
	in.stored       = 0;
	in.known        = ap_net_character_known(&in.stored);
	in.startingChar = ctr_cfg.starting_character;
	in.busy         = 0;
	in.hubReady     = 0;
	in.unlockedMask = 0u;
	for (i = 0; i < AP_SEAT_ROSTER; i++)
	{
		if (AP_CharacterUnlocked(i))
			in.unlockedMask |= (1u << i);
	}

	return AP_SeatResolve(&in);
}

void AP_CharSwap_SeatStartingCharacter(struct GameTracker *gGT)
{
	struct AP_SeatInput in;
	struct AP_SeatAction act;
	char                 msg[192];
	unsigned             rev;
	int                  i;

	if (!AP_CharSwap_FeatureLive())
		return;

	// Cheap per-frame gate: on the ordinary frame there is no revision to
	// resolve and no deferral outstanding, so we never build the unlock mask.
	// A deferral holds this open, which is the standing cost of never dropping a
	// stored racer on a timer: sixteen array lookups a frame and no writes.
	rev = ap_net_character_revision();
	if (AP_SeatIdle(&ap_cs_seat, rev))
		return;

	in.rev          = rev;
	in.stored       = 0;
	in.known        = ap_net_character_known(&in.stored);
	in.startingChar = ctr_cfg.starting_character;
	in.busy         = (ap_cs_open || ap_cs_pendingSwap || ap_cs_restorePos);
	// Safe-frame test for a DEFERRED restore: the adventure hub open and idle
	// with a live local driver. A deferral has no deadline, so its restore can
	// land at any time, including mid-race, and this is what keeps it from
	// changing the racer there. Server-driven seats are not gated on it (see
	// ap_charseat.h): they must land before the hub's own load births the player.
	in.hubReady     = ap_cs_hubReady(gGT);
	in.unlockedMask = 0u;
	for (i = 0; i < AP_SEAT_ROSTER; i++)
	{
		if (AP_CharacterUnlocked(i))
			in.unlockedMask |= (1u << i);
	}

	AP_SeatStep(&ap_cs_seat, &in, &act);

	switch (act.action)
	{
	case AP_SEAT_ACT_SEAT:
		ap_cs_seatApply(act.character, act.fromStore ? "restored from AP data storage"
		                                             : "seed starting racer");
		break;

	case AP_SEAT_ACT_DEFER:
		// Only a racer the seed actually lets you be may be seated. A stored
		// value can also simply be EARLY: its unlock item is still in the batched
		// connect-time replay. Keep the stored choice pending with no deadline,
		// seat the starting racer meanwhile when this is the first seat of the
		// connection so play is never blocked, and say so exactly once.
		if (act.character != AP_SEAT_NONE)
		{
			snprintf(msg, sizeof msg,
			         "[AP CHARSWAP] stored racer %d is not unlocked (yet); seating the "
			         "starting racer %d and restoring it if its unlock arrives\n",
			         ap_cs_seat.pending, ctr_cfg.starting_character);
			AP_LogLine(msg);
			ap_cs_seatApply(act.character, "seed starting racer (stored racer pending)");
		}
		else
		{
			snprintf(msg, sizeof msg,
			         "[AP CHARSWAP] stored racer %d is not unlocked (yet); keeping racer %d "
			         "and restoring it if its unlock arrives\n",
			         ap_cs_seat.pending, (int)data.characterIDs[0]);
			AP_LogLine(msg);
		}
		break;

	case AP_SEAT_ACT_RESTORE:
		// The unlock arrived. Note that this writes the same two fields a Garage
		// confirm writes, so an in-hub restore takes visible effect at the next
		// driver birth rather than re-loading the level under the player.
		ap_cs_seatApply(act.character, "restored from AP data storage");
		break;

	default:
		break;
	}
}

void AP_CharSwap_Tick(struct GameTracker *gGT)
{
	if (gGT == NULL)
		return;

	// Productionised gate (#54/#209). The picker is a real feature on any seed
	// that carries the character phase; the dev-key path survives only as a way
	// in when no such seed is connected, so the prototype's manual matrix stays
	// runnable on a bare build.
	if (!AP_CharSwap_FeatureLive() && !AP_DevKeysEnabled())
		return;

	// Apply any editable-stat package that has arrived from the server since the
	// last frame. Cheap (a revision compare) and outside the hub gate below,
	// because the package has to be in effect before the next driver birth
	// wherever that happens, not only while standing in a hub.
	ap_cs_editRestore();

	// A swap is in flight: wait for the hub to come back, then restore.
	if (ap_cs_restorePos)
	{
		if (ap_cs_hubReady(gGT))
			ap_cs_completeSwap(gGT);
		return;
	}

	if (!ap_cs_hubReady(gGT))
	{
		if (ap_cs_open)
		{
			ap_cs_open = 0;
			ap_cs_editFocus = 0;
			ap_cs_frozeDriver = 0; // the level changed; the bit went with it
		}
		// A pause-menu request does not survive leaving the hub. The player asked
		// to pick a character in THIS hub session; carrying the bit across a level
		// load would pop the picker open somewhere they did not ask for it.
		ap_cs_pauseRequest = 0;
		return;
	}

	ap_cs_devKeys(gGT);

	// Honour a pending #238 pause-menu request. The row resumed the game and left
	// this behind, so the first frames after it are still unsafe (PAUSE_1 is
	// cleared by the RESUME path but sdata->pause_state and the freeze bits take
	// a moment to settle, and MainFreeze sets a 5-frame unpause cooldown). No
	// timer waits that out: the request simply survives until ap_cs_safeToOpen
	// agrees, which is the same "no deadline" reasoning the seat machine uses.
	if (ap_cs_pauseRequest && !ap_cs_open)
	{
		if (ap_cs_tryOpen(gGT))
			ap_cs_pauseRequest = 0;
	}

	if (!ap_cs_open)
		return;

	// Something else took the game (pause, cutscene, mask hint) -> get out of
	// the way rather than fight it.
	if (!ap_cs_safeToOpen(gGT) && !ap_cs_frozeDriver)
	{
		ap_cs_open = 0;
		return;
	}

	ap_cs_setFreeze(gGT, 1); // re-assert every frame: AH_Door / AH_MaskHint clear it
	ap_cs_input(gGT);
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
#define AP_CS_TILE_W 0x34
#define AP_CS_TILE_H 0x21

static const char *ap_cs_rankName(int statIndex, int value)
{
	// Placeholder presentation only. The ruled ladder is
	// VERY LOW -> LOW -> MEDIUM -> HIGH -> VERY HIGH over four received
	// upgrades; with no progressive items in the build there is nothing to
	// rank against, so the bands here are laid over the four vanilla class
	// values for the same stat. Replace when the real chains land.
	int row = ap_cs_rowForOffset(ap_cs_stats[statIndex].driverOffset);
	int lo, hi, i, span;

	if ((row < 0) || (value < 0))
		return "?";

	lo = data.metaPhys[row].value[0];
	hi = lo;
	for (i = 1; i < NUM_CLASSES; i++)
	{
		int v = data.metaPhys[row].value[i];
		if (v < lo)
			lo = v;
		if (v > hi)
			hi = v;
	}

	if (value < lo)
		return "VERY LOW";
	if (value > hi)
		return "VERY HIGH";

	span = hi - lo;
	if (span <= 0)
		return "MEDIUM";
	if (value - lo <= span / 3)
		return "LOW";
	if (value - lo <= (2 * span) / 3)
		return "MEDIUM";
	return "HIGH";
}

// Pick the label for a tile whose portrait is not resident.
//
// Pure, and deliberately separated from the table lookups so the harness can
// drive it: the whole point of the fallback is that it draws SOMETHING, and the
// case that matters is the one where the preferred string is itself missing.
// Split out into ap/ap_charname.h so tools/test-character-fallback.cpp compiles
// the same code production does rather than a copy of it.
//
// One line per character, ever. The draw runs every frame while the picker is
// open, so an unconditional log would be thousands of lines a minute; the first
// cut's comment promised a log and emitted none at all, which is the reason a
// real residency failure would have been invisible in the logs.
static unsigned ap_cs_portraitReported = 0;

static void ap_cs_noteMissingPortrait(int characterID, int iconID)
{
	char msg[160];

	if ((unsigned)characterID >= AP_CS_TILES)
		return;
	if ((ap_cs_portraitReported & (1u << characterID)) != 0)
		return;
	ap_cs_portraitReported |= (1u << characterID);

	snprintf(msg, sizeof msg,
	         "[AP CHARSWAP] portrait for racer %d (iconID %d) is not resident in this "
	         "hub's icon table; drawing the name instead\n",
	         characterID, iconID);
	AP_LogLine(msg);
}

static const char *ap_cs_shortName(int characterID)
{
	const char *localised = NULL;

	if ((unsigned)characterID < AP_CS_TILES)
	{
		int lng = data.MetaDataCharacters[characterID].name_LNG_short;
		if (lng >= 0)
			localised = sdata->lngStrings[lng];
		return AP_CharName_Pick(localised, data.MetaDataCharacters[characterID].name_Debug);
	}

	return AP_CharName_Pick(NULL, NULL);
}

static const char *ap_cs_ownershipLabel(void)
{
	if (ap_cs_progMode == AP_CS_MODE_GLOBAL)
		return "GLOBAL";
	if (ap_cs_progMode == AP_CS_MODE_PERCHAR)
		return "PER-CHARACTER";
	if (ap_cs_editMode == AP_CS_MODE_GLOBAL)
		return "GLOBAL";
	if (ap_cs_editMode == AP_CS_MODE_PERCHAR)
		return "PER-CHARACTER";
	return "VANILLA";
}

void AP_CharPicker_Draw(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *live;
	unsigned int *ot;
	Color col;
	RECT r;
	char line[96];
	int i;
	int selectedChar;
	int panelY;

	if (!ap_cs_open || (gGT == NULL))
		return;

	ot = gGT->backBuffer->otMem.uiOT;
	live = gGT->drivers[0];
	selectedChar = ap_cs_tiles[ap_cs_cursor].characterID;

	// Backing plates first, icons on top -- the same two-pass order
	// MM_Characters_MenuProc uses (game/230/MM_Characters.c:1024, 1157).
	for (i = 0; i < AP_CS_TILES; i++)
	{
		r.x = ap_cs_tiles[i].posX;
		r.y = ap_cs_tiles[i].posY;
		r.w = AP_CS_TILE_W;
		r.h = AP_CS_TILE_H;
		RECTMENU_DrawInnerRect(&r, 0, ot);
	}

	// Cursor highlight: an OUTLINE on the tile border, never a filled box.
	//
	// The first cut drew CTR_Box_DrawSolidBox over the cursor tile, and that box
	// is an opaque PolyF4 (game/CTR/CTR_Box.c:117-143) submitted to the UI
	// ordering table, while the portraits go to gGT->pushBuffer_UI.ptrOT further
	// down. The two tables are not composited in add order, so the fill landed on
	// top of the portrait and the highlighted tile rendered as a flat blue square
	// with no art and no name. It read as a missing portrait; it was the cursor
	// painting over a portrait that was resident all along, which is also why the
	// short-name fallback below never appeared -- its icon was never NULL.
	//
	// MM_Characters has the same problem to solve and solves it with an outline:
	// RECTMENU_DrawOuterRect_HighLevel in D230.characterSelect_Outline over the
	// tile bounds (game/230/MM_Characters.c:1154). Following that idiom puts the
	// highlight on the border, where it cannot occlude anything, and matches how
	// the game's own picker looks.
	r.x = ap_cs_tiles[ap_cs_cursor].posX;
	r.y = ap_cs_tiles[ap_cs_cursor].posY;
	r.w = AP_CS_TILE_W;
	r.h = AP_CS_TILE_H;
	// Field-by-field rather than through MakeColorCode. The macro is a compound
	// literal into ColorCode's anonymous struct, and ASSIGNING one (as opposed
	// to initialising a fresh variable with it, which is how every other caller
	// uses it -- UI_Meter.c:283 and friends) draws -Wmissing-braces on this
	// toolchain. This build holds a zero-new-warning gate, and quietly adding
	// the project's only such warning to buy one line is not a trade worth
	// making.
	col.r = 0xf0;
	col.g = 0xd0;
	col.b = 0x40;
	col.code.code = 0;
	RECTMENU_DrawOuterRect_HighLevel(&r, col, 0, ot);

	for (i = 0; i < AP_CS_TILES; i++)
	{
		int characterID = ap_cs_tiles[i].characterID;
		int iconID = data.MetaDataCharacters[characterID].iconID;
		struct Icon *icon = ((unsigned)iconID < 0x88) ? gGT->ptrIcons[iconID] : NULL;
		unsigned int tint;

		// A locked racer is drawn dimmed but IS drawn; the current character is
		// drawn bright. Ruling 2026-08-08: the roster is not a secret.
		if (characterID == data.characterIDs[0])
			tint = 0xffffff;
		else if (ap_cs_isUnlocked(characterID))
			tint = 0x808080;
		else
			tint = 0x404040;

		if (icon != NULL)
		{
			RECTMENU_DrawPolyGT4(icon, (short)(ap_cs_tiles[i].posX + 6), (short)(ap_cs_tiles[i].posY + 4),
			                     &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT,
			                     tint, tint, tint, tint, TRANS_50_DECAL, FP(1.0));
		}
		else
		{
			// The hub's icon table is populated from the hub LEV plus the
			// resident adventure MPK (DecalGlobal_Store, game/DecalGlobal.c:19),
			// so a portrait may simply not be resident here. Fall back to a name
			// and SAY SO in the log rather than drawing nothing.
			//
			// Two hardenings over the first cut, which claimed to log and did not,
			// and which would itself have drawn nothing if lngStrings were the
			// thing that was missing. ap_cs_shortName resolves the localised
			// string first and falls back to MetaDataCHAR::name_Debug, a plain
			// char* in the EXE (include/regionsEXE.h:311) that is resident
			// whenever the character table is, so SOMETHING always draws.
			DecalFont_DrawLine((char *)ap_cs_shortName(characterID),
			                   ap_cs_tiles[i].posX + (AP_CS_TILE_W / 2), ap_cs_tiles[i].posY + 8,
			                   FONT_SMALL, (JUSTIFY_CENTER | ORANGE));
			ap_cs_noteMissingPortrait(characterID, iconID);
		}
	}

	// Title, raised with the grid. FONT_BIG is 17 tall
	// (data.font_charPixHeight, game/zGlobal_DATA.c:2586-2592), so this ends at
	// 19 and clears the first tile row at 22.
	DecalFont_DrawLine(sdata->lngStrings[LNG_SELECT_CHARACTER], 0x100, 2, FONT_BIG, (JUSTIFY_CENTER | ORANGE));

	// ------------------------------------------------------------------
	// Highlighted-character panel.
	//
	// The stats are the point of it: the ruling is that switching racer has to
	// be an informed choice, so the numbers the resolver would ACTUALLY apply to
	// this character are shown, whichever package owns them. ap_cs_effectiveValue
	// is that resolver, and it already implements the ruled precedence
	// (progressive when active, else editable when active, else the vanilla class
	// value, with Penta's PAL/NTSC case folded in), so the panel reads it rather
	// than restating the rule -- there is one owner of the number.
	//
	// The whole panel must fit between the bottom tile row (ends at 127) and the
	// draw environment's 216-line floor (MainMain.c:534-537). Budget, all
	// FONT_BIG 17 / FONT_SMALL 8:
	//     130  name          (BIG,   ends 147)
	//     150  ownership     (SMALL, ends 158)
	//     162..192 stat rows (SMALL, four rows of 10)
	//     198  controls      (SMALL, ends 206)
	// The first cut started this block at 0xD8 = 216, which IS the floor, so
	// every line of it fell outside the framebuffer and the player saw no name,
	// no ownership label and no stats at all.
	// ------------------------------------------------------------------
	panelY = 130;
	DecalFont_DrawLine(sdata->lngStrings[data.MetaDataCharacters[selectedChar].name_LNG_long],
	                   0x100, panelY, FONT_BIG, (JUSTIFY_CENTER | WHITE));

	// Ownership, editability and LOCKED share one line. They used to take two,
	// which no longer fits, and LOCKED belongs beside the ownership label anyway:
	// both answer "what am I allowed to do with this racer".
	snprintf(line, sizeof line, "%s%s%s", ap_cs_ownershipLabel(),
	         ap_cs_editorAvailable() ? "  [EDITABLE]" : "  (READ-ONLY)",
	         ap_cs_isUnlocked(selectedChar) ? "" : "   LOCKED");
	DecalFont_DrawLine(line, 0x100, panelY + 20, FONT_SMALL,
	                   (JUSTIFY_CENTER | (ap_cs_isUnlocked(selectedChar) ? ORANGE : RED)));

	for (i = 0; i < AP_CS_STATS; i++)
	{
		int want = ap_cs_effectiveValue(i, selectedChar);
		const char *marker = (ap_cs_editFocus && (i == ap_cs_statRow)) ? ">" : " ";

		// The "live" column -- what the running driver is actually carrying right
		// now -- is the spike's swap proof: for the racer you are standing as it
		// must equal the promised value, for any other portrait it will not, and
		// after a confirmed swap it must flip. That is a diagnostic, not player
		// information, and at FONT_SMALL's 13-pixel cell
		// (data.font_charPixWidth, zGlobal_DATA.c:2577-2583) carrying it costs
		// more than a third of the 512-pixel line. It stays, behind dev keys,
		// where the manual matrix can still use it.
		if (AP_DevKeysEnabled())
		{
			snprintf(line, sizeof line, "%s%-7s %6d %-9s live %6d", marker,
			         ap_cs_stats[i].label, want, ap_cs_rankName(i, want),
			         ap_cs_liveValue(live, i));
		}
		else
		{
			snprintf(line, sizeof line, "%s%-7s %6d %-9s", marker,
			         ap_cs_stats[i].label, want, ap_cs_rankName(i, want));
		}

		DecalFont_DrawLine(line, 0x70, panelY + 32 + i * 10, FONT_SMALL,
		                   (i == ap_cs_statRow && ap_cs_editFocus) ? WHITE : ORANGE);
	}

	snprintf(line, sizeof line, "X CONFIRM   %s   TRIANGLE CLOSE",
	         ap_cs_editorAvailable() ? "SQUARE EDIT" : "----");
	DecalFont_DrawLine(line, 0x100, panelY + 68, FONT_SMALL, (JUSTIFY_CENTER | ORANGE));
}

#endif // CTR_AP
