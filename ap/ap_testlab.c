#ifdef CTR_AP

#include <common.h> // struct Driver + sdata + TurboType / CollStepFlags / BTN_*

#include "ap_testlab.h"

#include "platform/native_config.h" // g_config.testLab* (the OPTIONS menu rows)

// ============================================================================
// CAPABILITY TEST LAB -- implementation. See ap_testlab.h for what this is for
// and where each toggle's semantics come from. Everything reads g_config
// directly, so the harness is live the moment a row is changed and needs no
// server, no slot_data and no received items.
// ============================================================================

// The local human player. Single-player Adventure / arcade: drivers[0]. Same
// idiom (and same guards) as the trap framework's own scoping helper at
// ap/ap_traps.c:126-135; duplicated rather than shared so neither module's
// call-sites depend on the other's lifecycle.
static struct Driver *AP_TestLabLocalDriver(void)
{
	if (sdata == 0 || sdata->gGT == 0)
		return 0;
	return sdata->gGT->drivers[0];
}

static int AP_TestLabIsLocal(struct Driver *driver)
{
	return driver != 0 && driver == AP_TestLabLocalDriver();
}

// ── Boost tier ──────────────────────────────────────────────────────────────
//
// VehFire_Increment turns its fireLevel argument into a boost-speed cap with one
// linear map (game/Vehicle/VehFire.c:359-360):
//
//   fireSpeedCap = singleTurbo + ((fireLevel * (sacredFire - singleTurbo)) >> 8)
//
// so fireLevel 0x100 lands exactly on const_SacredFireSpeed (red fire) and the
// super turbo pad's 0x800 overshoots it by a factor of eight -- the engine itself
// defines USF as a cap above sacred (VehFire.c:342-345). That makes 0x100 the
// natural "capped max boost speed" for the BOOST tier: every vanilla grant except
// the super pad already sits at or below it, so clamping there removes USF speed
// and nothing else.
#define AP_TESTLAB_CAP_FIRELEVEL 0x100

// A normal turbo pad's payload (game/Vehicle/VehPhysForce.c:557). A super pad
// banks far less time (0x78, VehPhysForce.c:551) because its cap is meant to be
// spent at once, so demoting one to "acts as a normal pad" has to hand back the
// normal pad's reserves as well as its cap -- otherwise the demotion would be a
// downgrade below a normal pad rather than an equivalence.
#define AP_TESTLAB_PAD_RESERVES 0x3c0

int AP_TestLab_FireGrant(struct Driver *driver, int *reserves, uint32_t type, int *fireLevel)
{
	const int tier = g_config.testLabBoost;

	if (tier == AP_TESTLAB_BOOST_VANILLA || !AP_TestLabIsLocal(driver))
		return 1;

	// Tier NONE: only turbo pads grant. Everything else in the game is boost the
	// player earns for themselves and is suppressed -- the powerslide/mini-turbo
	// chain (VehPhysProc.c:1889), hang time off a jump (UI/UI_Meter.c:60), the
	// start-line rev boost (VehStuckProc.c:780), the Turbo pickup
	// (VehPickupItem.c:394) and the 10-wumpa Super Engine (VehPhysProc.c:885).
	// #12's comment names slides, hang time and reserve items explicitly; the
	// rule applied here is the design's own invariant read the other way round --
	// pads keep working at every tier, so at this tier pads are all that is left.
	if (tier == AP_TESTLAB_BOOST_NONE && (type & TURBO_PAD) == 0)
		return 0;

	if (tier < AP_TESTLAB_BOOST_USF)
	{
		if (*fireLevel > AP_TESTLAB_CAP_FIRELEVEL)
			*fireLevel = AP_TESTLAB_CAP_FIRELEVEL;

		// Identify a genuine super-pad grant from the collision flag the grant was
		// raised off (VehPhysForce.c:549-551) rather than from its fireLevel, so
		// the CHEAT_TURBOPAD path (VehPhysForce.c:561), which hands a NORMAL pad a
		// super pad's numbers, is correctly left as a normal pad.
		if ((type & TURBO_PAD) != 0 &&
		    (driver->stepFlagSet & COLL_STEP_TRIGGER_SUPER_TURBO_PAD) != 0)
			*reserves = AP_TESTLAB_PAD_RESERVES;
	}

	return 1;
}

// ── Gas pedal ───────────────────────────────────────────────────────────────

void AP_TestLab_DriveInput(struct Driver *driver, uint32_t *buttonsHeld, uint32_t *cross)
{
	if (g_config.testLabGas || !AP_TestLabIsLocal(driver))
		return;

	// Cross only. The analog sticks are deliberately left alone: reverse is a
	// separate branch keyed off stickLY (VehPhysProc.c:686/735-745) and is the
	// whole point of driving without a throttle. The reserves auto-throttle
	// (VehPhysProc.c:641-675) re-forces cross later in this same function
	// whenever reserves are banked, which is also intended -- banked boost should
	// still carry the kart forward with the pedal locked out.
	*cross = 0;
	*buttonsHeld &= ~BTN_CROSS;
}

// ── Stats floor ─────────────────────────────────────────────────────────────
//
// The kart's stat constants are written once at driver birth by
// VehBirth_SetConsts (game/Vehicle/VehBirth.c:529-568), which scatters
// data.metaPhys[] rows into the driver by byte offset, picking each row's column
// from the character's engine class (BALANCED / ACCEL / SPEED / TURN).
//
// #13's settled design says a seed starts with ALL stats at the lowest level and
// that the chains override the character stat table with absolute per-tier
// values, but neither the issue body nor its comments pin what the bottom tier's
// numbers actually are. Rather than invent them, the floor here is the engine's
// own minimum stat preset: for each stat, the lowest value any engine class has
// in data.metaPhys (game/zGlobal_DATA.c:7176-7209). No class is globally weakest
// -- SPEED has the worst acceleration and turn rate, TURN the worst top speed --
// so the floor is a per-stat minimum across classes, which is strictly at or
// below every character and is exactly "all stats at the lowest level".
//
// Six stats are covered, spanning the three axes #13 names (top speed,
// acceleration, handling). Turn Response is const_TurnInputDelay, whose name is
// misleading: it is not a delay but the per-frame RATE at which the kart's
// rotation converges on the steering target -- it is added to or subtracted from
// rotationSpinRate each frame at game/Vehicle/VehPhysGeneral.c:78, 95 and 108.
// Higher is snappier, which the class table confirms (TURN class highest, SPEED
// class lowest, matching how those classes handle). That direction is read off
// the code, not off the field's name.
//
// Three class-varying stats are deliberately NOT covered: TURN_DECREASE_RATE
// (4080 9080 5666 7252), PRE_TURBO (1000 750 500 1250) and COLLISION_WEIGHT
// (256 256 300 200). Their class values do not order with kart quality, so
// "lowest" has no defensible meaning for them and guessing a direction would put
// an invented value into a measurement instrument.
#define AP_TESTLAB_FLOOR_ACCEL       448   // zGlobal_DATA.c:7176, SPEED class
#define AP_TESTLAB_FLOOR_SPEED       12950 // zGlobal_DATA.c:7178, TURN class
#define AP_TESTLAB_FLOOR_ACCELSPEED  14450 // zGlobal_DATA.c:7179, TURN class
#define AP_TESTLAB_FLOOR_TURNRATE    24    // zGlobal_DATA.c:7185, SPEED class
#define AP_TESTLAB_FLOOR_DRIFTTURN   5     // zGlobal_DATA.c:7209, SPEED class
#define AP_TESTLAB_FLOOR_TURNRESP    4000  // zGlobal_DATA.c:7188, SPEED class

// Birth-time snapshot of exactly the stats this module overwrites, so VANILLA has
// a base to restore to and a mid-race mode change is reversible.
//
// Keyed by driverID. Slot 0 is the local player and nothing else: MainInit_Drivers
// spawns the humans as VehBirth_Player(i) for i = numPlyrCurrGame-1 down to 0
// (game/MAIN/MainInit.c:374), bots take ids above them, and the two ghost drivers
// are explicitly born with driverID 1 and 2 (game/GhostReplay.c:416), so nothing
// else can land in slot 0. The driver pointer is stored alongside and checked on
// read regardless -- if it ever fails to match, we have no trustworthy base and
// write nothing, rather than restoring another kart's numbers.
#define AP_TESTLAB_SNAP_SLOTS 8

typedef struct
{
	struct Driver *driver;
	s16 accel;
	s16 speed;
	s16 accelSpeed;
	s16 turnResponse;
	u8  turnRate;
	s8  driftTurn;
} TestLabStatSnapshot;

static TestLabStatSnapshot g_statSnap[AP_TESTLAB_SNAP_SLOTS];

void AP_TestLab_SnapshotStats(struct Driver *driver)
{
	if (driver == 0)
		return;

	const int id = driver->driverID;
	if (id < 0 || id >= AP_TESTLAB_SNAP_SLOTS)
		return;

	TestLabStatSnapshot *s = &g_statSnap[id];
	s->driver       = driver;
	s->accel        = driver->const_Accel_ClassStat;
	s->speed        = driver->const_Speed_ClassStat;
	s->accelSpeed   = driver->const_AccelSpeed_ClassStat;
	s->turnResponse = driver->const_TurnInputDelay;
	s->turnRate     = driver->const_TurnRate;
	s->driftTurn    = driver->const_DriftTurnBase;
}

// One writer for all three modes, so none of them can leave a kart half-converted
// with some stats from one mode and some from another.
static void AP_TestLabWriteStats(struct Driver *driver, int accel, int speed, int accelSpeed,
                                 int turnResponse, int turnRate, int driftTurn)
{
	driver->const_Accel_ClassStat      = (s16)accel;
	driver->const_Speed_ClassStat      = (s16)speed;
	driver->const_AccelSpeed_ClassStat = (s16)accelSpeed;
	driver->const_TurnInputDelay       = (s16)turnResponse;
	driver->const_TurnRate             = (u8)turnRate;
	driver->const_DriftTurnBase        = (s8)driftTurn;
}

void AP_TestLab_Stats(struct Driver *driver)
{
	if (!AP_TestLabIsLocal(driver))
		return;

	const int mode = g_config.testLabStats;

	if (mode == AP_TESTLAB_STATS_FLOOR)
	{
		AP_TestLabWriteStats(driver, AP_TESTLAB_FLOOR_ACCEL, AP_TESTLAB_FLOOR_SPEED,
		                     AP_TESTLAB_FLOOR_ACCELSPEED, AP_TESTLAB_FLOOR_TURNRESP,
		                     AP_TESTLAB_FLOOR_TURNRATE, AP_TESTLAB_FLOOR_DRIFTTURN);
		return;
	}

	if (mode == AP_TESTLAB_STATS_CUSTOM)
	{
		AP_TestLabWriteStats(driver, g_config.testLabAccel, g_config.testLabTopSpeed,
		                     g_config.testLabBoostSpeed, g_config.testLabTurnResponse,
		                     g_config.testLabTurnRate, g_config.testLabDriftTurn);
		return;
	}

	// VANILLA: hand the character's own numbers back. Written every frame rather
	// than once on the mode change -- six stores, no "have I already restored"
	// flag to get wrong, and it self-heals. Every mode writes absolute values, so
	// switching between them mid-race is instant in both directions.
	const int id = driver->driverID;
	if (id < 0 || id >= AP_TESTLAB_SNAP_SLOTS)
		return;

	const TestLabStatSnapshot *s = &g_statSnap[id];
	if (s->driver != driver)
		return;

	AP_TestLabWriteStats(driver, s->accel, s->speed, s->accelSpeed,
	                     s->turnResponse, s->turnRate, s->driftTurn);
}

#endif // CTR_AP
