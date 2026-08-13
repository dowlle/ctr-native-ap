#ifdef CTR_AP

#include <common.h> // structs Driver/GameTracker/GamepadBuffer/CameraDC + sdata + enums
#include <stdio.h>

#include "ap_traps.h"
#include "ap_trap_sched.h" // the freestanding registry/timing state machine
#include "ap_hooks.h"      // AP_LogLine (non-static log shim)

// ============================================================================
// AP TRAP FRAMEWORK -- implementation. See ap_traps.h for the design contract
// (the priming rule, the effect set, the engine call-sites). Everything here is a
// single translation unit member (unity build): the engine sites call the AP_Trap*
// hooks declared in the header; the definitions live below.
// ============================================================================

// Raw keyboard probe for the debug keybinds (platform/native_input.c, CTR_AP
// only). Same declaration ap_hooks.c uses -- keeps this module SDL-header-free.
int Platform_InputRawKeyDown(int scancode);

// Debug test-fire keys (SDL scancodes, externals/SDL/include/SDL3/SDL_scancode.h:
// KP_1=89..KP_6=94). Numpad, not F-keys: the engine's CTR_INTERNAL dev handlers
// own F1-F12 (native_platform.c: wireframe/savestates/controller-cycling) and fire
// regardless of AP keys. Not in the gameplay input map (native_input.c defaults),
// so they never disturb driving.
//
// Numpad 1..5 DELIVER one trap exactly as the server would, through AP_TrapReceive
// -- press one mid-race and it lands on the real mid-race delay, press one in the
// hub and it primes for lap 2/3. Numpad 6 delivers all five. There is deliberately
// no key left that bypasses the schedule: the previous instant-fire keys tested a
// path the product no longer has, and the point of a dev key is to reproduce what
// a player gets.
#define AP_TRAP_KEY_ICY   89 // Numpad 1
#define AP_TRAP_KEY_GRAV  90 // Numpad 2
#define AP_TRAP_KEY_USF   91 // Numpad 3
#define AP_TRAP_KEY_BOOST 92 // Numpad 4
#define AP_TRAP_KEY_FP    93 // Numpad 5
#define AP_TRAP_KEY_PRIME 94 // Numpad 6 -> prime all

// First-person camera mode selector. cameraMode 0x10 = "first person" with yaw
// taken from the kart heading (d->angle) -- the stable hood-cam forward view
// (game/zGlobal_DATA.c:1037; implemented at game/CAM.c:2194-2210). 0 = normal
// chase. Restoring to chase also raises CAMERA_FLAG_DIRECTION_CHANGED so the chase
// cam re-seats instead of lerping from the head position (game/CAM.c convention).
#define AP_CAM_FIRSTPERSON 0x10
#define AP_CAM_CHASE       0

// Effect durations, in milliseconds of firing time. Flat 20 s for every trap
// (issue #116): the old 4-6 s windows were over before the player registered
// them. Deliberately a single hardcoded table -- no YAML option yet; per-trap
// tuning or an option promotion can come later if 20 s proves too brutal.
static const int AP_TRAP_DURATION_MS[AP_TRAP_COUNT] = {
    20000, // icy
    20000, // low gravity
    20000, // USF no-brake
    20000, // boost
    20000, // first person
};

// The fire delays themselves live in ap_trap_sched.h (AP_TRAP_LAP_DELAY_*_MS for
// a trap received out of a race, AP_TRAP_MIDRACE_DELAY_MS for one received during
// one), together with the state machine that spends them.

// Physics tuning. Gravity/friction are scaled by /256 fixed point.
#define AP_TRAP_GRAV_SCALE     74   // 74/256 ~= 0.29. NOTE: deliberately floatier
                                    //  than the engine's per-quad low-gravity
                                    //  factor, which computes 41/100 (~0.41):
                                    //  ((g<<2)+g)<<3 + g, all over 100
                                    //  (VehPhysForce.c:104-105)
#define AP_TRAP_FRICTION_SCALE 40   // 40/256 ~= 0.16 grip -> icy
// Reserves FLOOR while a boost/USF trap fires: reserves are only RAISED to this
// value when below it (never overwritten downward), so the per-frame decrement /
// resets in VehPhysProc (VehPhysProc.c:111/628/2159) can never zero the boost,
// while a player's banked reserves above the floor -- and any reserves earned
// during the 20 s window -- survive the trap instead of being deleted by a
// hard per-frame pin.
#define AP_TRAP_BOOST_RESERVES 1200
// fireLevel of a super turbo pad (VehPhysForce.c:551) -- the input that makes
// VehFire_Increment (VehFire.c:358) compute a genuinely USF-tier fireSpeedCap.
#define AP_TRAP_USF_FIRELEVEL  0x800
#define AP_STICK_NEUTRAL       0x80 // analog centre (native_input.c:125-128)

// ── Registry ──
// State and timing belong to the freestanding scheduler; this module only feeds it
// the frame and spends its answers.
static struct ApTrapSched g_sched;
static int                g_sched_ready = 0;

static struct ApTrapSched *AP_TrapSched(void)
{
	if (!g_sched_ready)
	{
		ap_trap_sched_init(&g_sched, AP_TRAP_DURATION_MS);
		g_sched_ready = 1;
	}
	return &g_sched;
}

// First-person camera latch: 1 while we are forcing the FP camera, so we know to
// give the camera back exactly once when the effect ends.
static int g_fp_applied = 0;

// Countdown-observed latch, the same mechanism (and for the same two reasons) as
// ap_wumpa.c's -- named apart because ap/ is a unity build and each module owns its
// own race-window state: set once a lights sequence is OBSERVED running on a live track,
// cleared off-track and at END_OF_RACE. It is what closes the RACE-LOAD GAP, in
// which the stale free-roam gameMode1/trafficLightsTimer values pass a naive
// flag/timer test for a few frames after a track load, before race init has run --
// observed in the 2026-07-17 batch smoke, and the frames in which drivers[0] can
// still hold not-yet-born leftovers (ap_hooks.c's own LOAD_IDLE gate on the finish
// capture, and ap_shortcut.c's note on the Crash Cove race-load crash, both name
// the same window). A countdown can never be observed inside that gap.
static int g_trap_countdown_seen = 0;

// The local human player. Single-player Adventure: drivers[0]. Guarded so the
// physics hooks stay safe outside a race (drivers[] not yet populated).
static struct Driver *AP_TrapLocalDriver(void)
{
	if (sdata == 0 || sdata->gGT == 0)
		return 0;
	return sdata->gGT->drivers[0];
}

static int AP_TrapIsLocal(struct Driver *driver)
{
	return driver != 0 && driver == AP_TrapLocalDriver();
}

static const char *AP_TrapName(int effect)
{
	switch (effect)
	{
	case AP_TRAP_ICY:         return "icy road";
	case AP_TRAP_LOWGRAV:     return "low gravity";
	case AP_TRAP_USF_NOBRAKE: return "USF no-brake";
	case AP_TRAP_BOOST:       return "boost";
	case AP_TRAP_FIRSTPERSON: return "first-person";
	default:                  return "?";
	}
}

// Are we on a live track at all -- as opposed to the hub, a menu, a load or a
// ceremony? LOAD_IsOpen_RacingOrBattle() is the stock overlay-1 test the mask-grab
// subsystem uses for the same question (VehStuckProc.c:451).
static int AP_TrapOnTrack(void)
{
	return sdata != 0 && sdata->Loading.stage == LOAD_IDLE && LOAD_IsOpen_RacingOrBattle();
}

// Is this frame SAFE TO MUTATE: a live race frame, lights out, level loaded, the
// local driver born, nothing ceremonial in progress? Shared by the receive path
// (does this trap arrive mid-race?), by the fire gate, and by the physics apply
// flags, so the three can never drift apart.
//
// THE FLAG/TIMER TEST ALONE IS NOT ENOUGH, and this is not a guess -- ap_wumpa.c
// documents both holes against observed evidence, and this predicate used to have
// both:
//   1. THE ADVENTURE HUB PASSES IT. MainGameStart stamps START_OF_RACE +
//      trafficLightsTimer on hub entry and both clear a few seconds later, so a
//      trap received while free-roaming the hub read as "mid-race" and applied hub
//      physics and a forced hub camera. Closed by AP_TrapOnTrack().
//   2. THE RACE-LOAD GAP PASSES IT. Stale flags/timer for a few frames after a
//      track load, before race init -- the frames in which drivers[0] can hold
//      not-yet-born leftovers. Closed by the countdown latch.
//
// BOSS RACES ARE DELIBERATELY IN SCOPE. gameMode2's SPAWN_AT_BOSS is not tested
// here and must not be: a boss race is an ordinary race for trap purposes (Stef,
// 2026-08-13). What the boss race needs is not exclusion but the same safety the
// rest of this predicate gives, which it now has -- and the camera guard below is
// what keeps a boss encounter's own camera work out of the trap's reach.
//
// Anchors: gameMode flags namespace_Main.h; trafficLightsTimer < 1 = lights out,
// which is the same test PlayLevel.c:338 uses.
static int AP_TrapRaceActive(struct GameTracker *gGT)
{
	if (gGT == 0 || !AP_TrapOnTrack() || !g_trap_countdown_seen)
		return 0;
	if ((gGT->gameMode1 &
	     (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) != 0)
		return 0;
	if (gGT->trafficLightsTimer >= 1)
		return 0;
	return gGT->drivers[0] != 0;
}

// May we hold the forced first-person camera on this frame? Deliberately WIDER
// than the fire window: it tolerates the countdown and a pause, so a firing FP
// trap does not flicker to chase and back every time the player pauses.
//
// It is narrower in the one direction that matters. AP_TrapTick runs every frame
// in every game mode, so before this guard a FIRING trap kept writing cameraMode
// into the podium, the hub and the menus -- and cameraDC[0].cameraMode is read as
// state by the engine, not just written: CS_Camera.c:229 (CS_Camera_ThTick_Podium)
// treats `cameraMode != 3` as "the podium camera is not seated" and, while that
// holds, force-advances D233.cutsceneState to CS_WAIT_INPUT and raises
// PodiumInitUnk3 every frame. A 20 s trap still firing at the finish line held
// that true for the whole ceremony. Excluding END_OF_RACE, GAME_CUTSCENE and
// MAIN_MENU is what hands those modes back their own camera.
static int AP_TrapCameraSafe(struct GameTracker *gGT)
{
	if (gGT == 0 || !AP_TrapOnTrack() || !g_trap_countdown_seen)
		return 0;
	if ((gGT->gameMode1 & (END_OF_RACE | MAIN_MENU | GAME_CUTSCENE)) != 0)
		return 0;
	// VEH_FREEZE_PODIUM/VEH_FREEZE_DOOR mark the frames where the engine has taken
	// the kart away from the player for a ceremony; CAM.c:1852 already refuses the
	// manual zoom toggle under the podium bit, and a trap has no better claim.
	if ((gGT->gameMode2 & GAME_MODE2_VEH_FREEZE_MASK) != 0)
		return 0;
	return gGT->drivers[0] != 0;
}

// ── AP item pipeline seam ──
void AP_TrapReceive(int effect)
{
	struct ApTrapSched *s = AP_TrapSched();
	int                 slot;
	int                 midRace;
	char                msg[128];

	if (effect < 0 || effect >= AP_TRAP_COUNT)
		return;

	// WHERE the trap lands decides its lifecycle, and the answer is captured here,
	// at receive time, rather than inferred later -- by the time the tick runs,
	// "was a race running when this arrived" is no longer knowable.
	midRace = AP_TrapRaceActive(sdata != 0 ? sdata->gGT : 0);

	slot = ap_trap_sched_receive(s, effect, midRace,
	                             sdata != 0 ? (unsigned)sdata->frameCounter : 0u);
	if (slot < 0)
	{
		AP_LogLine("[AP TRAP] registry full -- trap dropped\n");
		return;
	}

	if (midRace)
		snprintf(msg, sizeof msg, "[AP TRAP] received mid-race: %s, firing in %d ms (slot %d)\n",
		         AP_TrapName(effect), AP_TRAP_MIDRACE_DELAY_MS, slot);
	else
		snprintf(msg, sizeof msg, "[AP TRAP] primed: %s (slot %d)\n",
		         AP_TrapName(effect), slot);
	AP_LogLine(msg);
}

void AP_Trap_ConnectReset(void)
{
	struct ApTrapSched *s = AP_TrapSched();
	int                 had = ap_trap_sched_occupied(s);

	ap_trap_sched_reset(s);

	if (had > 0)
	{
		char msg[96];
		snprintf(msg, sizeof msg,
		         "[AP TRAP] fresh connect: dropped %d trap instance(s) from the previous session\n",
		         had);
		AP_LogLine(msg);
	}
}

// ── Config test trigger ──
int AP_TrapConfigLine(const char *line)
{
	const char *v;
	if (strncmp(line, "debug_trap=", 11) != 0)
		return 0;
	v = line + 11;
	if      (!strcmp(v, "icy"))     AP_TrapReceive(AP_TRAP_ICY);
	else if (!strcmp(v, "lowgrav")) AP_TrapReceive(AP_TRAP_LOWGRAV);
	else if (!strcmp(v, "usf"))     AP_TrapReceive(AP_TRAP_USF_NOBRAKE);
	else if (!strcmp(v, "boost"))   AP_TrapReceive(AP_TRAP_BOOST);
	else if (!strcmp(v, "fp"))      AP_TrapReceive(AP_TRAP_FIRSTPERSON);
	else if (!strcmp(v, "all"))
	{
		int e;
		for (e = 0; e < AP_TRAP_COUNT; e++)
			AP_TrapReceive(e);
	}
	return 1;
}

// ── Debug keybinds ──
void AP_TrapDebugKeys(void)
{
	// Rising-edge latch per key (6 keys).
	static int prev[6] = {0};
	struct
	{
		int scancode;
		int effect; // -1 = prime-all
	} keys[6] = {
	    {AP_TRAP_KEY_ICY, AP_TRAP_ICY},
	    {AP_TRAP_KEY_GRAV, AP_TRAP_LOWGRAV},
	    {AP_TRAP_KEY_USF, AP_TRAP_USF_NOBRAKE},
	    {AP_TRAP_KEY_BOOST, AP_TRAP_BOOST},
	    {AP_TRAP_KEY_FP, AP_TRAP_FIRSTPERSON},
	    {AP_TRAP_KEY_PRIME, -1},
	};
	int k;

	// DEAD unless ap-config.txt dev_keys=1 (shared gate with the Shortcutless dev
	// keys, AP_DevKeysEnabled). Unguarded these fire traps from ANY numpad press
	// in any mode -- same leak class as issue #16.
	if (!AP_DevKeysEnabled())
		return;

	for (k = 0; k < 6; k++)
	{
		int down = Platform_InputRawKeyDown(keys[k].scancode);
		if (down && !prev[k])
		{
			if (keys[k].effect < 0)
			{
				int e;
				for (e = 0; e < AP_TRAP_COUNT; e++)
					AP_TrapReceive(e);
			}
			else
			{
				AP_TrapReceive(keys[k].effect); // the real receive path, no bypass
			}
		}
		prev[k] = down;
	}
}

// ── Camera application (first-person trap) ──
// Forced every frame while an FP instance is FIRING AND the frame is one we are
// allowed to touch, because user L2 zoom / start-of-race paths also write
// cameraMode and would otherwise clobber it (game/CAM.c). Runs from AP_TrapTick,
// which fires before the camera PROC ticks this frame, so the camera update sees
// the forced mode.
//
// The give-back is conditional, and that is the second half of the fix. The old
// restore wrote chase unconditionally whenever the latch was set, so a trap whose
// 20 s ran out in the garage, on the title screen, at character select or during
// the podium overwrote a camera those modes had deliberately set to mode 3
// (MM_Title.c:561, MM_Characters.c:273, CS_Garage.c:64, CS_Podium.c:628,
// MainInit.c:694, VehStuckProc.c:1640 all write 3). We only take the camera back
// if it is still the mode we wrote; anything else already owns it.
static void AP_TrapApplyCamera(struct GameTracker *gGT, int wantFP, int cameraSafe)
{
	if (gGT == 0)
		return;

	if (wantFP && cameraSafe)
	{
		gGT->cameraDC[0].cameraMode = AP_CAM_FIRSTPERSON;
		g_fp_applied = 1;
		return;
	}

	if (!g_fp_applied)
		return;

	if (gGT->cameraDC[0].cameraMode == AP_CAM_FIRSTPERSON)
	{
		gGT->cameraDC[0].cameraMode = AP_CAM_CHASE;
		gGT->cameraDC[0].flags |= CAMERA_FLAG_DIRECTION_CHANGED;
	}
	g_fp_applied = 0;
}

// ── Per-frame lifecycle ──
void AP_TrapTick(struct GameTracker *gGT)
{
	struct ApTrapSched      *s = AP_TrapSched();
	struct ApTrapSchedFrame  f;
	struct ApTrapSchedEvents ev;
	struct Driver           *local;
	int                      k;
	char                     msg[112];

	// Debug keys work anywhere (title screen included) -- but only when enabled
	// via ap-config.txt dev_keys=1 (gate inside AP_TrapDebugKeys).
	AP_TrapDebugKeys();

	if (gGT == 0)
		return;

	// Maintain the countdown latch BEFORE anything reads the race predicate, the
	// same way ap_wumpa.c does: a countdown can never be observed inside the
	// race-load gap, so seeing one is proof race init has run.
	if (!AP_TrapOnTrack() || (gGT->gameMode1 & END_OF_RACE) != 0)
		g_trap_countdown_seen = 0;
	else if (gGT->trafficLightsTimer >= 1)
		g_trap_countdown_seen = 1;

	f.elapsedMs = gGT->elapsedTimeMS;
	f.onTrack = AP_TrapOnTrack();
	f.raceActive = AP_TrapRaceActive(gGT);
	f.frame = sdata != 0 ? (unsigned)sdata->frameCounter : 0u;

	// On lap 2 or later (lapIndex is 0-based: 0=lap1, 1=lap2, 2=lap3; PlayLevel.c:126),
	// and not past the finish (lapIndex < numLaps). raceActive already proves
	// drivers[0] is a born driver, so lapIndex is real and not load-gap leftovers.
	local = gGT->drivers[0];
	f.lapWindow = f.raceActive && local != 0 &&
	              local->lapIndex >= 1 && local->lapIndex < gGT->numLaps;

	ap_trap_sched_tick(s, &f, &ev);

	for (k = 0; k < ev.firedCount; k++)
	{
		snprintf(msg, sizeof msg, "[AP TRAP] FIRING: %s (%d ms, slot %d)\n",
		         AP_TrapName(s->slot[ev.fired[k]].effect),
		         s->slot[ev.fired[k]].remainingMs, ev.fired[k]);
		AP_LogLine(msg);
	}
	for (k = 0; k < ev.heldCount; k++)
	{
		// Never silent: an apply that cannot proceed safely says so and waits.
		snprintf(msg, sizeof msg,
		         "[AP TRAP] holding %s: waiting for a safe race frame (slot %d)\n",
		         AP_TrapName(s->slot[ev.held[k]].effect), ev.held[k]);
		AP_LogLine(msg);
	}
	for (k = 0; k < ev.clearedCount; k++)
	{
		snprintf(msg, sizeof msg, "[AP TRAP] cleared: %s (slot %d)\n",
		         AP_TrapName(s->slot[ev.cleared[k]].effect), ev.cleared[k]);
		AP_LogLine(msg);
	}

	// First-person camera: force/give back based on the FP flag and this frame's
	// camera safety. Deliberately the RAW firing flag, not the fire-safe one, so a
	// pause or the countdown does not flicker the camera; AP_TrapCameraSafe owns
	// which frames may be written.
	AP_TrapApplyCamera(gGT, s->firing[AP_TRAP_FIRSTPERSON], AP_TrapCameraSafe(gGT));
}

// ── Engine physics/input call-sites ──

int AP_TrapGravity(struct Driver *driver, int gravityY)
{
	if (g_sched.active[AP_TRAP_LOWGRAV] && AP_TrapIsLocal(driver))
		return (gravityY * AP_TRAP_GRAV_SCALE) / 256;
	return gravityY;
}

void AP_TrapFriction(struct Driver *driver, int *perpendicularFriction, int *forwardFriction)
{
	if (g_sched.active[AP_TRAP_ICY] && AP_TrapIsLocal(driver))
	{
		// KNOWN, ACCEPTED SIDE EFFECT: this scales the friction scalars BEFORE the
		// per-terrain groundFrictionScale multiplies in (VehPhysForce.c:306-307),
		// so while the trap fires, off-road drag is also cut to ~16 percent -- the
		// 20 s icy window doubles as an off-road cutting window for players who
		// know that. Kept as deliberate counterplay (see issue #116 discussion;
		// Ignore Off-Road is separately planned as an item, #14).
		*perpendicularFriction = (*perpendicularFriction * AP_TRAP_FRICTION_SCALE) / 256;
		*forwardFriction = (*forwardFriction * AP_TRAP_FRICTION_SCALE) / 256;
	}
}

// Raise reserves to the trap floor without ever lowering them (s16 field; the
// floor is far below the 32767 ceiling, and values already above it pass through
// untouched, so no overflow path is introduced).
static void AP_TrapFloorReserves(struct Driver *driver)
{
	if (driver->reserves < AP_TRAP_BOOST_RESERVES)
		driver->reserves = AP_TRAP_BOOST_RESERVES;
}

void AP_TrapForceBoost(struct Driver *driver)
{
	if (!AP_TrapIsLocal(driver))
		return;
	if (g_sched.active[AP_TRAP_USF_NOBRAKE])
	{
		// Genuinely USF-tier cap, derived from the real fire-level formula
		// (VehFire.c:358-359): cap = singleTurbo + (fireLevel * (sacred - singleTurbo))
		// >> 8, evaluated at a super turbo pad's fireLevel (0x800). The old pin to
		// const_SacredFireSpeed only granted red-fire speed, which is NOT USF --
		// the engine itself defines USF as fireSpeedCap above sacred (VehFire.c:378).
		int usfCap = ((int)driver->const_SingleTurboSpeed +
		              ((AP_TRAP_USF_FIRELEVEL *
		                ((int)driver->const_SacredFireSpeed - (int)driver->const_SingleTurboSpeed)) >> 8));
		if (usfCap > 32767)
			usfCap = 32767; // fireSpeedCap is s16
		AP_TrapFloorReserves(driver);
		// Floor, not pin: VehFire_Increment demotes the cap on any non-super-pad
		// boost while above sacred (VehFire.c:375-381); re-raising here each frame
		// restores the trap tier without ever downgrading a higher cap.
		if (driver->fireSpeedCap < usfCap)
			driver->fireSpeedCap = (s16)usfCap;
	}
	else if (g_sched.active[AP_TRAP_BOOST])
	{
		AP_TrapFloorReserves(driver);
		// Floor, not pin: a hard pin to const_SingleTurboSpeed would DOWNGRADE a
		// red fire the player earns mid-trap. Only raise up to the milder tier.
		if (driver->fireSpeedCap < driver->const_SingleTurboSpeed)
			driver->fireSpeedCap = driver->const_SingleTurboSpeed;
	}
}

void AP_TrapDriveInput(struct Driver *driver, struct GamepadBuffer *pad,
                       int *buttonsHeld, int *cross, int *square)
{
	if (!g_sched.active[AP_TRAP_USF_NOBRAKE] || !AP_TrapIsLocal(driver))
		return;

	// Kill the brake button and force the throttle button for this frame's physics.
	*square = 0;
	*buttonsHeld &= ~BTN_SQUARE;
	*buttonsHeld |= BTN_CROSS;
	*cross = BTN_CROSS;

	// Neutralise the analog sticks so the pull-back reverse/brake path (read later
	// in this same PhysLinear call, VehPhysProc.c:638/686) also goes dead. The pad
	// buffer is repopulated from input every frame before physics, so this only
	// affects the current frame's reads.
	if (pad != 0)
	{
		pad->stickLY = AP_STICK_NEUTRAL;
		pad->stickRY = AP_STICK_NEUTRAL;
	}
}

#endif // CTR_AP
