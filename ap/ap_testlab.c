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
// Only the fields where "lowest" is unambiguous are floored: the three axes #13
// names (top speed, acceleration, handling). The other class-varying rows
// (TURN_DECREASE_RATE, TURN_INPUT_DELAY, PRE_TURBO, COLLISION_WEIGHT) are left
// at the character's own values because a smaller number is not obviously a
// worse kart for any of them, and guessing a direction would put an invented
// value into a measurement instrument.
#define AP_TESTLAB_FLOOR_ACCEL       448   // zGlobal_DATA.c:7176, SPEED class
#define AP_TESTLAB_FLOOR_SPEED       12950 // zGlobal_DATA.c:7178, TURN class
#define AP_TESTLAB_FLOOR_ACCELSPEED  14450 // zGlobal_DATA.c:7179, TURN class
#define AP_TESTLAB_FLOOR_TURNRATE    24    // zGlobal_DATA.c:7185, SPEED class
#define AP_TESTLAB_FLOOR_DRIFTTURN   5     // zGlobal_DATA.c:7209, SPEED class

void AP_TestLab_Stats(struct Driver *driver)
{
	if (g_config.testLabStats != AP_TESTLAB_STATS_FLOOR || !AP_TestLabIsLocal(driver))
		return;

	// Re-applied every frame rather than patched once at birth: the constants are
	// only ever written by VehBirth_SetConsts, so a per-frame write is idempotent
	// and survives a re-birth without this module having to know when one
	// happened. The flip side is that there is no restore -- turning the row back
	// to VANILLA takes effect from the next race, when birth re-runs and hands
	// the kart its character's own numbers again. That is the intended workflow
	// here (pick a configuration, then drive a track), and it avoids caching a
	// snapshot that a character change would silently make wrong.
	if (driver->const_Accel_ClassStat > AP_TESTLAB_FLOOR_ACCEL)
		driver->const_Accel_ClassStat = AP_TESTLAB_FLOOR_ACCEL;
	if (driver->const_Speed_ClassStat > AP_TESTLAB_FLOOR_SPEED)
		driver->const_Speed_ClassStat = AP_TESTLAB_FLOOR_SPEED;
	if (driver->const_AccelSpeed_ClassStat > AP_TESTLAB_FLOOR_ACCELSPEED)
		driver->const_AccelSpeed_ClassStat = AP_TESTLAB_FLOOR_ACCELSPEED;
	if (driver->const_TurnRate > AP_TESTLAB_FLOOR_TURNRATE)
		driver->const_TurnRate = AP_TESTLAB_FLOOR_TURNRATE;
	if ((s8)driver->const_DriftTurnBase > AP_TESTLAB_FLOOR_DRIFTTURN)
		driver->const_DriftTurnBase = AP_TESTLAB_FLOOR_DRIFTTURN;
}

#endif // CTR_AP
