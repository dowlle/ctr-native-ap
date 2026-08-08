#ifdef CTR_AP

#include <common.h> // struct Driver / GameTracker / Level, sdata, data, LevelID, BTN_*
#include <stddef.h> // offsetof
#include <stdio.h>
#include <string.h>

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
// EVERYTHING BELOW IS DEV-GATED. AP_DevKeysEnabled() (ap-config.txt dev_keys=1)
// plus "the adventure hub is open" plus "not paused". A normal seed cannot
// reach it.
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
// ---------------------------------------------------------------------------
struct AP_CharTile
{
	short posX;
	short posY;
	signed char next[4]; // up, down, left, right (tile indices)
	short characterID;
};

#define AP_CS_TILES 16

static const struct AP_CharTile ap_cs_tiles[AP_CS_TILES] = {
    /*  0 Crash      */ {0x080, 0x60, {0, 4, 8, 1}, CRASH_BANDICOOT},
    /*  1 Cortex     */ {0x0C0, 0x60, {1, 5, 0, 2}, NEO_CORTEX},
    /*  2 Tiny       */ {0x100, 0x60, {2, 6, 1, 3}, TINY_TIGER},
    /*  3 Coco       */ {0x140, 0x60, {3, 7, 2, 9}, COCO_BANDICOOT},
    /*  4 N. Gin     */ {0x080, 0x87, {0, 12, 10, 5}, N_GIN},
    /*  5 Dingodile  */ {0x0C0, 0x87, {1, 13, 4, 6}, DINGODILE},
    /*  6 Polar      */ {0x100, 0x87, {2, 14, 5, 7}, POLAR},
    /*  7 Pura       */ {0x140, 0x87, {3, 14, 6, 11}, PURA},
    /*  8 N. Tropy   */ {0x040, 0x60, {8, 10, 8, 0}, N_TROPY},
    /*  9 Pinstripe  */ {0x180, 0x60, {9, 11, 3, 9}, PINSTRIPE},
    /* 10 Ripper Roo */ {0x040, 0x87, {8, 10, 10, 4}, RIPPER_ROO},
    /* 11 Papu Papu  */ {0x180, 0x87, {9, 11, 7, 11}, PAPU_PAPU},
    /* 12 Komodo Joe */ {0x0A0, 0xAE, {4, 12, 12, 13}, KOMODO_JOE},
    /* 13 Penta      */ {0x0E0, 0xAE, {5, 13, 12, 14}, PENTA_PENGUIN},
    /* 14 Fake Crash */ {0x120, 0xAE, {6, 14, 13, 15}, FAKE_CRASH},
    /* 15 N. Oxide   */ {0x160, 0xAE, {7, 15, 14, 15}, NITROS_OXIDE},
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

static int ap_cs_vanillaValue(int statIndex, int characterID)
{
	int row = ap_cs_rowForOffset(ap_cs_stats[statIndex].driverOffset);
	int engineID;

	if (row < 0)
		return -1;

	engineID = data.MetaDataCharacters[characterID].engineID;
	if ((unsigned)engineID >= NUM_CLASSES)
		return -1;

	return data.metaPhys[row].value[engineID];
}

// ---------------------------------------------------------------------------
// Test packages.
//
// >>> SEAM: progressive_stats / editable_stats item + option storage. <<<
// Neither the AP item plumbing (12 shared-global / 192 per-character
// Progressive copies) nor the YAML options exist yet, so this prototype does
// NOT read slot_data and does NOT invent a wire contract. It carries two local
// test modes with the ruled PRECEDENCE, which is the part that needs proving:
//
//   progressive != off              -> ownership label GLOBAL / PER-CHARACTER,
//                                      values are received-driven, READ ONLY,
//                                      no edit control at all.
//   progressive == off, edit != off -> ownership label GLOBAL / PER-CHARACTER,
//                                      values are the custom package, EDITABLE.
//   both off                        -> ownership label VANILLA, read only.
// ---------------------------------------------------------------------------
enum
{
	AP_CS_MODE_OFF = 0,
	AP_CS_MODE_GLOBAL = 1,
	AP_CS_MODE_PERCHAR = 2,
};

static int ap_cs_progMode = AP_CS_MODE_OFF;
static int ap_cs_editMode = AP_CS_MODE_OFF;

// Editable package: deltas over vanilla.
static short ap_cs_editGlobal[AP_CS_STATS];
static short ap_cs_editPerChar[AP_CS_TILES][AP_CS_STATS];

// Progressive package: a stand-in for "received Progressive copies". Per
// character it is deterministic and DISTINCT so a swap is falsifiable on
// screen: if the live driver still shows the previous character's numbers, the
// swap did not re-birth.
static int ap_cs_progDelta(int statIndex, int characterID)
{
	if (ap_cs_progMode == AP_CS_MODE_OFF)
		return 0;
	if (ap_cs_progMode == AP_CS_MODE_GLOBAL)
		return ap_cs_stats[statIndex].step * 4; // one shared package for everyone
	// per-character: characterID-keyed so all 16 read differently
	return ap_cs_stats[statIndex].step * (characterID + 1);
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

static int ap_cs_packageActive(void)
{
	return (ap_cs_progMode != AP_CS_MODE_OFF) || (ap_cs_editMode != AP_CS_MODE_OFF);
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
// Unlock state.
//
// >>> SEAM: AP character-unlock items. <<< R4 mints 15 unlock items and R17
// makes their classification conditional on racer-locks; none of that exists.
// The prototype uses vanilla's own starter set plus a dev "test all" so every
// one of the 16 can actually be swapped to, which is what the ticket asks the
// prototype to demonstrate.
// ---------------------------------------------------------------------------
static int ap_cs_testAllUnlocked = 1;

static int ap_cs_isUnlocked(int characterID)
{
	if (ap_cs_testAllUnlocked)
		return 1;
	if (characterID == data.characterIDs[0])
		return 1;
	return (characterID >= CRASH_BANDICOOT) && (characterID <= PURA); // the 8 adventure starters
}

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
	// value the Garage confirm writes (game/233/CS_Garage.c:360), so the hub
	// re-enters in exactly the state a fresh Garage pick would produce.
	//
	// >>> SEAM: persistence. <<< The 2026-07-23 ruling routes "current
	// character" to per-slot AP data storage (the ctr_difficulty_<slot>
	// precedent), NOT slot_data. That is not wired here; a reconnect will still
	// reapply whatever the save carries.
	data.characterIDs[0] = (short)characterID;
	sdata->advProgress.characterID = (s16)characterID;

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
			{
				ap_cs_open = 0;
				ap_cs_editFocus = 0;
				ap_cs_setFreeze(gGT, 0);
			}
			else if (ap_cs_safeToOpen(gGT))
			{
				ap_cs_open = 1;
				ap_cs_editFocus = 0;
				ap_cs_cursor = ap_cs_tileForCharacter(data.characterIDs[0]);
				ap_cs_setFreeze(gGT, 1);
				ap_cs_logPackage("open", gGT->drivers[0], data.characterIDs[0]);
			}
		}
		else if (k == 1)
		{
			ap_cs_progMode = (ap_cs_progMode + 1) % 3;
			if (ap_cs_progMode != AP_CS_MODE_OFF)
				ap_cs_editFocus = 0; // ruled precedence: no edit control at all
			ap_cs_reapplyLive(gGT);
		}
		else
		{
			ap_cs_editMode = (ap_cs_editMode + 1) % 3;
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

	OtherFX_Play(0, 1);
}

static void ap_cs_input(struct GameTracker *gGT)
{
	unsigned int tap = sdata->buttonTapPerPlayer[0];
	int characterID;

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

		sdata->buttonTapPerPlayer[0] = 0;
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

	sdata->buttonTapPerPlayer[0] = 0;
}

void AP_CharSwap_Tick(struct GameTracker *gGT)
{
	if (gGT == NULL)
		return;

	if (!AP_DevKeysEnabled())
		return;

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
		return;
	}

	ap_cs_devKeys(gGT);

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

	// Cursor highlight.
	r.x = ap_cs_tiles[ap_cs_cursor].posX + 3;
	r.y = ap_cs_tiles[ap_cs_cursor].posY + 2;
	r.w = AP_CS_TILE_W - 6;
	r.h = AP_CS_TILE_H - 4;
	col = MakeColorCode(0x30, 0x50, 0xa0, 0);
	CTR_Box_DrawSolidBox(&r, col, ot);

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
			// so a portrait may simply not be resident here. Fall back to the
			// short name and SAY SO in the log rather than drawing nothing.
			DecalFont_DrawLine(sdata->lngStrings[data.MetaDataCharacters[characterID].name_LNG_short],
			                   ap_cs_tiles[i].posX + (AP_CS_TILE_W / 2), ap_cs_tiles[i].posY + 8,
			                   FONT_SMALL, (JUSTIFY_CENTER | ORANGE));
		}
	}

	// Title.
	DecalFont_DrawLine(sdata->lngStrings[LNG_SELECT_CHARACTER], 0x100, 0x40, FONT_BIG, (JUSTIFY_CENTER | ORANGE));

	// Highlighted-character panel.
	panelY = 0xD8;
	DecalFont_DrawLine(sdata->lngStrings[data.MetaDataCharacters[selectedChar].name_LNG_long],
	                   0x100, panelY, FONT_BIG, (JUSTIFY_CENTER | WHITE));

	snprintf(line, sizeof line, "%s%s", ap_cs_ownershipLabel(),
	         ap_cs_editorAvailable() ? "  [EDITABLE]" : "  (READ-ONLY)");
	DecalFont_DrawLine(line, 0x100, panelY + 0x10, FONT_SMALL, (JUSTIFY_CENTER | ORANGE));

	if (!ap_cs_isUnlocked(selectedChar))
	{
		DecalFont_DrawLine("LOCKED", 0x100, panelY + 0x1e, FONT_SMALL, (JUSTIFY_CENTER | RED));
	}

	for (i = 0; i < AP_CS_STATS; i++)
	{
		int want = ap_cs_effectiveValue(i, selectedChar);
		int have = ap_cs_liveValue(live, i);
		const char *marker = (ap_cs_editFocus && (i == ap_cs_statRow)) ? ">" : " ";

		snprintf(line, sizeof line, "%s%-7s %6d %-9s  live %6d", marker,
		         ap_cs_stats[i].label, want, ap_cs_rankName(i, want), have);
		DecalFont_DrawLine(line, 0x40, panelY + 0x2c + i * 10, FONT_SMALL,
		                   (i == ap_cs_statRow && ap_cs_editFocus) ? WHITE : ORANGE);
	}

	// "live" is the value the running driver is actually carrying. For the
	// character you are standing as, live must equal the promised value; for
	// any other portrait it will not, and that difference is the whole point of
	// the proof: after a swap it must flip.
	snprintf(line, sizeof line, "X CONFIRM   %s   TRIANGLE CLOSE   [np9/np0/np.]",
	         ap_cs_editorAvailable() ? "SQUARE EDIT" : "----");
	DecalFont_DrawLine(line, 0x100, panelY + 0x2c + AP_CS_STATS * 10 + 4, FONT_SMALL,
	                   (JUSTIFY_CENTER | ORANGE));
}

#endif // CTR_AP
