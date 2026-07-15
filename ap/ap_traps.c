#ifdef CTR_AP

#include <common.h> // structs Driver/GameTracker/GamepadBuffer/CameraDC + sdata + enums
#include <stdio.h>

#include "ap_traps.h"
#include "ap_hooks.h" // AP_LogLine (non-static log shim)

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
// so they never disturb driving. Numpad 1..5 INSTANT-FIRE one effect (bypassing
// the prime/lap gate, for quick visual checks); Numpad 6 PRIMES all five (to
// exercise the real lap-2/3 priming path in the next race).
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

// Effect durations, in milliseconds of firing time.
static const int AP_TRAP_DURATION_MS[AP_TRAP_COUNT] = {
    6000, // icy
    6000, // low gravity
    4000, // USF no-brake
    4000, // boost
    5000, // first person
};

// Random fire-delay window (ms) after the trap first enters the lap-2/3 window.
#define AP_TRAP_FIRE_DELAY_MIN_MS 500
#define AP_TRAP_FIRE_DELAY_MAX_MS 8000

// Physics tuning. Gravity/friction are scaled by /256 fixed point.
#define AP_TRAP_GRAV_SCALE     74   // 74/256 ~= 0.29, matches the engine's own
                                    //  per-quad low-gravity factor (~29/100,
                                    //  VehPhysForce.c:104-105)
#define AP_TRAP_FRICTION_SCALE 40   // 40/256 ~= 0.16 grip -> icy
// Reserves value re-pinned each frame so VehPhysProc's per-frame decrement /
// resets (VehPhysProc.c:111/623/2154) can never zero it while the trap fires.
#define AP_TRAP_BOOST_RESERVES 1200
#define AP_STICK_NEUTRAL       0x80 // analog centre (native_input.c:125-128)

// ── Registry ──
typedef enum
{
	TRAP_EMPTY = 0,
	TRAP_PRIMED,  // armed, hidden, waiting for the lap-2/3 fire window
	TRAP_FIRING   // effect active, counting down remainingMs
} TrapState;

typedef struct
{
	int effect;       // AP_TrapEffect
	TrapState state;
	int rolled;       // 1 once fireDelayMs has been drawn for the current window
	int fireDelayMs;  // PRIMED: ms left before firing (rolled once in-window)
	int remainingMs;  // FIRING: ms of effect left
} TrapInstance;

#define AP_TRAP_REGISTRY_CAP 16
static TrapInstance g_traps[AP_TRAP_REGISTRY_CAP];

// Per-effect "is at least one instance FIRING" flags, recomputed each tick. The
// physics call-sites read these (plus the local-driver check) -- O(1), no scan.
static int g_active[AP_TRAP_COUNT];

// First-person camera latch: 1 while we are forcing the FP camera, so we know to
// restore chase exactly once when the effect ends.
static int g_fp_applied = 0;

// ── tiny self-contained RNG (xorshift32) -- no dependency on the engine RNG ──
static unsigned g_rng = 0;
static unsigned AP_TrapRand(void)
{
	if (g_rng == 0)
		g_rng = (unsigned)sdata->frameCounter | 1u; // lazy seed, never 0
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return g_rng;
}
static int AP_TrapRandRange(int lo, int hi)
{
	if (hi <= lo)
		return lo;
	return lo + (int)(AP_TrapRand() % (unsigned)(hi - lo));
}

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

// ── AP item pipeline seam ──
void AP_TrapReceive(int effect)
{
	int i;
	char msg[96];

	if (effect < 0 || effect >= AP_TRAP_COUNT)
		return;

	for (i = 0; i < AP_TRAP_REGISTRY_CAP; i++)
	{
		if (g_traps[i].state == TRAP_EMPTY)
		{
			g_traps[i].effect = effect;
			g_traps[i].state = TRAP_PRIMED;
			g_traps[i].rolled = 0;
			g_traps[i].fireDelayMs = 0;
			g_traps[i].remainingMs = 0;
			snprintf(msg, sizeof msg, "[AP TRAP] primed: %s (slot %d)\n",
			         AP_TrapName(effect), i);
			AP_LogLine(msg);
			return;
		}
	}
	AP_LogLine("[AP TRAP] registry full -- trap dropped\n");
}

// Fire an already-chosen slot NOW (used both by the lap-gate path and the debug
// instant-fire keys). Transitions PRIMED/EMPTY -> FIRING.
static void AP_TrapFireSlot(int i)
{
	char msg[96];
	g_traps[i].state = TRAP_FIRING;
	g_traps[i].remainingMs = AP_TRAP_DURATION_MS[g_traps[i].effect];
	snprintf(msg, sizeof msg, "[AP TRAP] FIRING: %s (%d ms, slot %d)\n",
	         AP_TrapName(g_traps[i].effect), g_traps[i].remainingMs, i);
	AP_LogLine(msg);
}

// Instant-fire one effect (debug). Reuses a spare slot.
static void AP_TrapFireNow(int effect)
{
	int i;
	if (effect < 0 || effect >= AP_TRAP_COUNT)
		return;
	for (i = 0; i < AP_TRAP_REGISTRY_CAP; i++)
	{
		if (g_traps[i].state == TRAP_EMPTY)
		{
			g_traps[i].effect = effect;
			AP_TrapFireSlot(i);
			return;
		}
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
				AP_TrapFireNow(keys[k].effect); // instant, bypasses lap gate
			}
		}
		prev[k] = down;
	}
}

// ── Camera application (first-person trap) ──
// Forced every frame while an FP instance is FIRING, because user L2 zoom / start-
// of-race paths also write cameraMode and would otherwise clobber it (game/CAM.c).
// Runs from AP_TrapTick, which fires before the camera PROC ticks this frame, so
// the camera update sees the forced mode.
static void AP_TrapApplyCamera(struct GameTracker *gGT, int wantFP)
{
	if (gGT == 0)
		return;
	if (wantFP)
	{
		gGT->cameraDC[0].cameraMode = AP_CAM_FIRSTPERSON;
		g_fp_applied = 1;
	}
	else if (g_fp_applied)
	{
		gGT->cameraDC[0].cameraMode = AP_CAM_CHASE;
		gGT->cameraDC[0].flags |= CAMERA_FLAG_DIRECTION_CHANGED;
		g_fp_applied = 0;
	}
}

// ── Per-frame lifecycle ──
void AP_TrapTick(struct GameTracker *gGT)
{
	int i, e;
	int elapsedMs;
	int raceActive, lapWindow;
	struct Driver *local;

	// Debug keys work anywhere (title screen included) -- but only when enabled
	// via ap-config.txt dev_keys=1 (gate inside AP_TrapDebugKeys).
	AP_TrapDebugKeys();

	if (gGT == 0)
		return;

	elapsedMs = gGT->elapsedTimeMS;
	if (elapsedMs <= 0)
		elapsedMs = 32; // defensive: a paused/odd frame shouldn't stall the timers

	// Race window: mid-race, countdown finished, not paused/menu/cutscene/EOR.
	// (Anchors: gameMode flags namespace_Main.h; trafficLightsTimer<1 = lights out,
	//  PlayLevel.c:338 uses the same test.)
	raceActive = (gGT->gameMode1 &
	              (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	             gGT->trafficLightsTimer < 1;

	// On lap 2 or later (lapIndex is 0-based: 0=lap1, 1=lap2, 2=lap3; PlayLevel.c:126),
	// and not past the finish (lapIndex < numLaps).
	local = gGT->drivers[0];
	lapWindow = raceActive && local != 0 &&
	            local->lapIndex >= 1 && local->lapIndex < gGT->numLaps;

	for (i = 0; i < AP_TRAP_REGISTRY_CAP; i++)
	{
		TrapInstance *t = &g_traps[i];
		switch (t->state)
		{
		case TRAP_PRIMED:
			if (lapWindow)
			{
				if (!t->rolled)
				{
					t->fireDelayMs = AP_TrapRandRange(AP_TRAP_FIRE_DELAY_MIN_MS,
					                                  AP_TRAP_FIRE_DELAY_MAX_MS);
					t->rolled = 1;
				}
				else
				{
					t->fireDelayMs -= elapsedMs;
					if (t->fireDelayMs <= 0)
						AP_TrapFireSlot(i);
				}
			}
			else
			{
				// Window closed (race ended before firing) -> re-roll next race so a
				// primed trap always eventually fires on some lap 2/3.
				t->rolled = 0;
			}
			break;
		case TRAP_FIRING:
			t->remainingMs -= elapsedMs;
			if (t->remainingMs <= 0)
			{
				char msg[96];
				snprintf(msg, sizeof msg, "[AP TRAP] cleared: %s (slot %d)\n",
				         AP_TrapName(t->effect), i);
				AP_LogLine(msg);
				t->state = TRAP_EMPTY;
			}
			break;
		case TRAP_EMPTY:
		default:
			break;
		}
	}

	// Recompute per-effect firing flags for the physics hooks.
	for (e = 0; e < AP_TRAP_COUNT; e++)
		g_active[e] = 0;
	for (i = 0; i < AP_TRAP_REGISTRY_CAP; i++)
		if (g_traps[i].state == TRAP_FIRING)
			g_active[g_traps[i].effect] = 1;

	// First-person camera: force/restore based on the FP flag.
	AP_TrapApplyCamera(gGT, g_active[AP_TRAP_FIRSTPERSON]);
}

// ── Engine physics/input call-sites ──

int AP_TrapGravity(struct Driver *driver, int gravityY)
{
	if (g_active[AP_TRAP_LOWGRAV] && AP_TrapIsLocal(driver))
		return (gravityY * AP_TRAP_GRAV_SCALE) / 256;
	return gravityY;
}

void AP_TrapFriction(struct Driver *driver, int *perpendicularFriction, int *forwardFriction)
{
	if (g_active[AP_TRAP_ICY] && AP_TrapIsLocal(driver))
	{
		*perpendicularFriction = (*perpendicularFriction * AP_TRAP_FRICTION_SCALE) / 256;
		*forwardFriction = (*forwardFriction * AP_TRAP_FRICTION_SCALE) / 256;
	}
}

void AP_TrapForceBoost(struct Driver *driver)
{
	if (!AP_TrapIsLocal(driver))
		return;
	if (g_active[AP_TRAP_USF_NOBRAKE])
	{
		driver->reserves = AP_TRAP_BOOST_RESERVES;
		driver->fireSpeedCap = driver->const_SacredFireSpeed; // max tier = USF
	}
	else if (g_active[AP_TRAP_BOOST])
	{
		driver->reserves = AP_TRAP_BOOST_RESERVES;
		driver->fireSpeedCap = driver->const_SingleTurboSpeed; // milder boost
	}
}

void AP_TrapDriveInput(struct Driver *driver, struct GamepadBuffer *pad,
                       int *buttonsHeld, int *cross, int *square)
{
	if (!g_active[AP_TRAP_USF_NOBRAKE] || !AP_TrapIsLocal(driver))
		return;

	// Kill the brake button and force the throttle button for this frame's physics.
	*square = 0;
	*buttonsHeld &= ~BTN_SQUARE;
	*buttonsHeld |= BTN_CROSS;
	*cross = BTN_CROSS;

	// Neutralise the analog sticks so the pull-back reverse/brake path (read later
	// in this same PhysLinear call, VehPhysProc.c:633/681) also goes dead. The pad
	// buffer is repopulated from input every frame before physics, so this only
	// affects the current frame's reads.
	if (pad != 0)
	{
		pad->stickLY = AP_STICK_NEUTRAL;
		pad->stickRY = AP_STICK_NEUTRAL;
	}
}

#endif // CTR_AP
