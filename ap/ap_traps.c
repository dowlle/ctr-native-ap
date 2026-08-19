#ifdef CTR_AP

#include <common.h> // structs Driver/GameTracker/GamepadBuffer/CameraDC + sdata + enums
#include <stdio.h>

#include "ap_traps.h"
#include "ap_hooks.h" // AP_LogLine, AP_FeedTrapLine, AP_DevKeysEnabled

// ============================================================================
// AP TRAP FRAMEWORK, engine half. See ap_traps.h for the contract and
// ap_trap_sched_logic.h for the rules. This file observes, presents and applies;
// it decides nothing about when a trap runs.
// ============================================================================

// Raw keyboard probe for the debug keybinds (platform/native_input.c, CTR_AP
// only). Same declaration ap_hooks.c uses, which keeps this module SDL-header-free.
int Platform_InputRawKeyDown(int scancode);

// Debug test-fire keys (SDL scancodes, externals/SDL/include/SDL3/SDL_scancode.h:
// KP_1=89..KP_6=94). Numpad, not F-keys: the engine's CTR_INTERNAL dev handlers
// own F1-F12 and fire regardless of AP keys. Not in the gameplay input map, so
// they never disturb driving.
//
// Every key now goes through the ORDINARY receive path. The old instant-fire
// shortcut bypassed the schedule it was supposed to be testing, and with per-effect
// scheduling there is no longer one shared lifecycle for it to skip.
#define AP_TRAP_KEY_ICY   89 // Numpad 1
#define AP_TRAP_KEY_GRAV  90 // Numpad 2
#define AP_TRAP_KEY_USF   91 // Numpad 3
#define AP_TRAP_KEY_BOOST 92 // Numpad 4
#define AP_TRAP_KEY_FP    93 // Numpad 5
#define AP_TRAP_KEY_ALL   94 // Numpad 6 -> arm all five

// First-person camera mode selector. cameraMode 0x10 = "first person" with yaw
// taken from the kart heading (d->angle), the stable hood-cam forward view
// (game/zGlobal_DATA.c:1037; implemented at game/CAM.c:2194-2210). 0 = normal
// chase. Restoring to chase also raises CAMERA_FLAG_DIRECTION_CHANGED so the chase
// cam re-seats instead of lerping from the head position (game/CAM.c convention).
#define AP_CAM_FIRSTPERSON 0x10
#define AP_CAM_CHASE       0

// Physics tuning. Gravity/friction are scaled by /256 fixed point.
#define AP_TRAP_GRAV_SCALE     74   // 74/256 ~= 0.29. Deliberately floatier than
                                    //  the engine's per-quad low-gravity factor,
                                    //  which computes 41/100 (VehPhysForce.c:104)
#define AP_TRAP_FRICTION_SCALE 40   // 40/256 ~= 0.16 grip -> icy
// Reserves FLOOR while a boost-control trap is active: reserves are only RAISED to
// this value when below it, never overwritten downward, so the per-frame decrement
// in VehPhysProc (VehPhysProc.c:111/628/2159) cannot zero the boost while banked
// reserves above the floor survive the trap.
#define AP_TRAP_BOOST_RESERVES 1200
// fireLevel of a super turbo pad (VehPhysForce.c:551), the input that makes
// VehFire_Increment (VehFire.c:358) compute a genuinely USF-tier fireSpeedCap.
#define AP_TRAP_USF_FIRELEVEL  0x800
#define AP_STICK_NEUTRAL       0x80 // analog centre (native_input.c:125-128)

// Reverse-recovery guard, shared by Forced Boost and Forced USF. Suppressing brake
// and analog reverse can wedge a kart against geometry with no way out, so both
// traps hand reverse back once the kart has been nearly stationary for about a
// second. High-speed braking is NOT handed back: the trap is still running.
//
// PROVISIONAL for Forced USF. The notebook rules Forced Boost's recovery
// explicitly and leaves Forced USF's open; applying the same guard is the only
// reading under which forced throttle cannot strand the player. Forced USF
// releases its forced throttle at the same moment, because reverse the player
// cannot reach is not a recovery path.
#define AP_TRAP_RECOVER_MS       1000
#define AP_TRAP_STATIONARY_SHIFT 4 // "nearly stationary" is under 1/16 top speed

static AP_TrapSched g_sched;
static int g_sched_ready = 0;

// Per-effect "is active this frame" cache, refreshed once per tick. The physics
// call-sites read this instead of scanning the registry, which keeps them O(1) and
// keeps their behaviour identical between two calls in one frame.
static unsigned char g_active[AP_TRAP_EFFECT_COUNT];

// Camera latch: 1 while we own cameraDC[0], so chase is restored exactly once.
static int g_fp_applied = 0;

// Map-boundary tracking. levelID alone is not enough: a restart and a Cup leg onto
// the same track keep the id, so the load itself has to count as a boundary.
static int g_epoch = 0;
static int g_prev_level = -1;
static int g_prev_load = LOAD_IDLE;

// Reverse-recovery accumulator, in milliseconds of near-stationary time.
static int g_recover_ms = 0;

static void AP_TrapEnsureReady(void)
{
	if (g_sched_ready)
		return;
	AP_TrapSchedReset(&g_sched);
	g_sched_ready = 1;
}

// The local human player. Single-player Adventure: drivers[0]. Guarded so the
// physics hooks stay safe outside a race, where drivers[] is not populated.
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
	if (effect < 0 || effect >= AP_TRAP_EFFECT_COUNT)
		return "?";
	return AP_TRAP_DESC[effect].name;
}

// ── World observation ──
//
// Which column of the settled activation-context matrix this map is. Order
// matters: a Crystal Challenge and a Cup leg both set flags an ordinary race also
// sets, so the more specific test has to come first. A map that is none of these
// (menus, loading, credits) reports 0, which no descriptor matches, so every armed
// copy simply stays armed.
static int AP_TrapContextOf(struct GameTracker *gGT, int loadStage)
{
	int gm1, gm2;

	if (gGT == 0 || loadStage != LOAD_IDLE)
		return 0;

	gm1 = gGT->gameMode1;
	gm2 = gGT->gameMode2;

	if ((gm1 & (MAIN_MENU | LOADING)) != 0)
		return 0;
	if ((gm1 & ADVENTURE_ARENA) != 0)
		return AP_TRAP_CTX_HUB;
	if ((gm1 & CRYSTAL_CHALLENGE) != 0)
		return AP_TRAP_CTX_CRYSTAL;
	if ((gm1 & BATTLE_MODE) != 0)
		return AP_TRAP_CTX_BATTLE;
	if ((gm1 & (RELIC_RACE | TIME_TRIAL)) != 0)
		return AP_TRAP_CTX_TIME_TRIAL;
	// A CTR Challenge is an ordinary Adventure race carrying the token objective,
	// so it is only distinguishable by TOKEN_RACE.
	if ((gm2 & TOKEN_RACE) != 0)
		return AP_TRAP_CTX_CTR_CHALLENGE;
	if ((gm2 & CUP_ANY_KIND) != 0)
		return AP_TRAP_CTX_CUP_LEG;
	// Standard and boss races share one column in the matrix.
	return AP_TRAP_CTX_RACE;
}

// Does the player genuinely have the kart? The starting countdown may show a
// warning but must not start an effect, which is the same test PlayLevel.c:338
// uses for lights-out.
static int AP_TrapControlUnlocked(struct GameTracker *gGT, struct Driver *local)
{
	if (gGT == 0)
		return 0;
	if ((gGT->gameMode1 & (START_OF_RACE | END_OF_RACE | MAIN_MENU | LOADING)) != 0)
		return 0;
	if (gGT->trafficLightsTimer >= 1)
		return 0;
	if (local == 0)
		return 0;
	return (local->actionsFlagSet & ACTION_RACE_FINISHED) == 0;
}

// Scripted camera or scripted control: anything that has taken the kart away from
// the player without ending the map. Ordinary respawns are deliberately absent,
// because the lifecycle continues through them.
static int AP_TrapScripted(struct GameTracker *gGT, struct Driver *local)
{
	if (gGT == 0)
		return 0;
	if ((gGT->gameMode1 & (GAME_CUTSCENE | AKU_SONG | UKA_SONG)) != 0)
		return 1;
	if ((gGT->gameMode2 & (VEH_FREEZE_PODIUM | VEH_FREEZE_DOOR)) != 0)
		return 1;
	if (local == 0)
		return 0;
	return local->kartState == KS_MASK_GRABBED || local->kartState == KS_WARP_PAD ||
	       local->kartState == KS_FREEZE;
}

static int AP_TrapFinishOrPodium(struct GameTracker *gGT, struct Driver *local)
{
	if (gGT == 0)
		return 0;
	if ((gGT->gameMode1 & END_OF_RACE) != 0)
		return 1;
	if ((gGT->gameMode2 & VEH_FREEZE_PODIUM) != 0)
		return 1;
	return local != 0 && (local->actionsFlagSet & ACTION_RACE_FINISHED) != 0;
}

// Satisfied conditional predicates. Only the one Forced USF needs is observable
// today; the rest arrive with the wave 2 effects that read them.
static unsigned AP_TrapConditions(struct Driver *local)
{
	unsigned bits = 0;
	if (local != 0 && (local->actionsFlagSet & ACTION_NEW_BOOST) != 0)
		bits |= AP_TRAP_COND_EARNED_BOOST;
	return bits;
}

// ── Presentation ──
// One place decides what the player is told, so the log and the on-screen feed can
// never disagree about a trap's state.
static void AP_TrapDrainEvents(void)
{
	AP_TrapEvent ev;
	char msg[128];

	while (AP_TrapSchedPopEvent(&g_sched, &ev))
	{
		const char *name = AP_TrapName(ev.effect);
		switch (ev.kind)
		{
		case AP_TRAP_EV_ARMED:
			snprintf(msg, sizeof msg, "%s ARMED", name);
			AP_FeedTrapLine(msg);
			snprintf(msg, sizeof msg, "[AP TRAP] armed: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_INACTIVE:
			// A trap this build cannot perform yet. One line, retained armed,
			// never consumed, so a newer apworld cannot lose an item here.
			snprintf(msg, sizeof msg,
			         "[AP TRAP] %s is not implemented in this build, held armed (slot %d)\n",
			         name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_WARN:
			snprintf(msg, sizeof msg, "%s INCOMING", name);
			AP_FeedTrapLine(msg);
			snprintf(msg, sizeof msg, "[AP TRAP] warning: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_FIRE:
			snprintf(msg, sizeof msg, "%s", name);
			AP_FeedTrapLine(msg);
			snprintf(msg, sizeof msg, "[AP TRAP] active: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_REFRESH:
			snprintf(msg, sizeof msg, "[AP TRAP] refreshed: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_CLEAR:
			snprintf(msg, sizeof msg, "[AP TRAP] cleared: %s (slot %d)\n", name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_REARM:
			snprintf(msg, sizeof msg, "[AP TRAP] re-armed before it fired: %s (slot %d)\n",
			         name, ev.slot);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_SUSPEND:
		case AP_TRAP_EV_RESUME:
			// Frequent and self-evident on screen. Log only, and only at the
			// transition, which is what the scheduler emits.
			snprintf(msg, sizeof msg, "[AP TRAP] %s: %s\n",
			         ev.kind == AP_TRAP_EV_SUSPEND ? "suspended" : "resumed", name);
			AP_LogLine(msg);
			break;
		case AP_TRAP_EV_DROPPED:
			snprintf(msg, sizeof msg, "[AP TRAP] registry full, %s dropped\n", name);
			AP_LogLine(msg);
			break;
		default:
			break;
		}
	}
}

// ── AP item pipeline seam ──
void AP_TrapReceive(int effect)
{
	AP_TrapEnsureReady();
	if (effect < 0 || effect >= AP_TRAP_EFFECT_COUNT)
	{
		char msg[96];
		snprintf(msg, sizeof msg, "[AP TRAP] unknown trap effect %d ignored\n", effect);
		AP_LogLine(msg);
		return;
	}
	// Presentation for the receipt itself is left to the scheduler: whether this
	// copy announces itself as armed depends on the world, which the next tick
	// supplies. A copy that lands in an eligible window goes straight to its
	// warning instead of flashing an armed state the player never had time to read.
	AP_TrapSchedReceive(&g_sched, effect);
}

void AP_Trap_ConnectReset(void)
{
	int e;

	AP_TrapEnsureReady();
	AP_TrapSchedReset(&g_sched);

	// Clear the cache in the same breath. The physics call-sites read it directly
	// and run before the next AP_TrapTick refreshes it, so a stale 1 here would
	// keep an effect applied for a frame past the reset.
	for (e = 0; e < AP_TRAP_EFFECT_COUNT; e++)
		g_active[e] = 0;
	g_recover_ms = 0;

	AP_LogLine("[AP TRAP] fresh connect: dropped every armed and active trap\n");
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
	static int prev[6] = {0};
	struct
	{
		int scancode;
		int effect; // -1 = arm every implemented effect
	} keys[6] = {
	    {AP_TRAP_KEY_ICY, AP_TRAP_ICY},
	    {AP_TRAP_KEY_GRAV, AP_TRAP_LOWGRAV},
	    {AP_TRAP_KEY_USF, AP_TRAP_USF_NOBRAKE},
	    {AP_TRAP_KEY_BOOST, AP_TRAP_BOOST},
	    {AP_TRAP_KEY_FP, AP_TRAP_FIRSTPERSON},
	    {AP_TRAP_KEY_ALL, -1},
	};
	int k;

	// DEAD unless ap-config.txt dev_keys=1 (shared gate with the Shortcutless dev
	// keys). Unguarded these fire traps from any numpad press in any mode, the same
	// leak class as issue #16.
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
				AP_TrapReceive(keys[k].effect);
		}
		prev[k] = down;
	}
}

// ── Camera ownership (First Person) ──
// Forced every frame while the effect is active, because the user L2 zoom and the
// start-of-race paths also write cameraMode and would otherwise clobber it
// (game/CAM.c). Runs from AP_TrapTick, which is called before the camera PROC
// ticks, so the camera update sees the forced mode.
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

// Near-stationary accumulator for the reverse-recovery guard. Kept on the frame
// tick rather than inside the input hook, which the engine may call more than once
// per frame.
static void AP_TrapTrackRecovery(struct Driver *local, int elapsedMs)
{
	int speed, limit;

	if (local == 0 || !(g_active[AP_TRAP_USF_NOBRAKE] || g_active[AP_TRAP_BOOST]))
	{
		g_recover_ms = 0;
		return;
	}

	speed = local->speedApprox;
	if (speed < 0)
		speed = -speed;
	limit = local->const_Speed_ClassStat >> AP_TRAP_STATIONARY_SHIFT;
	if (limit < 1)
		limit = 1;

	if (speed <= limit)
		g_recover_ms += elapsedMs;
	else
		g_recover_ms = 0;
}

static int AP_TrapReverseRestored(void)
{
	return g_recover_ms >= AP_TRAP_RECOVER_MS;
}

// ── Per-frame lifecycle ──
void AP_TrapTick(struct GameTracker *gGT)
{
	AP_TrapWorld w;
	struct Driver *local;
	int loadStage;
	int levelNow;
	int boundary;
	int e;

	AP_TrapEnsureReady();

	// Debug keys work anywhere, title screen included, but only when enabled via
	// ap-config.txt dev_keys=1 (gate inside AP_TrapDebugKeys).
	AP_TrapDebugKeys();

	if (gGT == 0)
		return;

	loadStage = (sdata != 0) ? sdata->Loading.stage : LOAD_IDLE;
	levelNow = gGT->levelID;

	// Map boundary. Clearing here, on the frame the load BEGINS and while the
	// source map is still standing, is what fixes the reported First Person
	// regression: a forced camera restored on the destination's first frame has
	// already leaked into that map's intro camera state and swallowed the input
	// meant to skip it. The scheduler sees the same boundary through the epoch, so
	// the ordering is enforced here and the bookkeeping is enforced there.
	boundary = (g_prev_level != levelNow) ||
	           (loadStage != LOAD_IDLE && g_prev_load == LOAD_IDLE);
	g_prev_level = levelNow;
	g_prev_load = loadStage;
	if (boundary)
	{
		g_epoch++;
		AP_TrapSchedMapChange(&g_sched);
		for (e = 0; e < AP_TRAP_EFFECT_COUNT; e++)
			g_active[e] = 0;
		AP_TrapApplyCamera(gGT, 0);
		g_recover_ms = 0;
	}

	local = gGT->drivers[0];

	w.context = AP_TrapContextOf(gGT, loadStage);
	w.mapEpoch = g_epoch;
	w.controlUnlocked = AP_TrapControlUnlocked(gGT, local);
	w.paused = (gGT->gameMode1 & PAUSE_ALL) != 0;
	w.scripted = AP_TrapScripted(gGT, local);
	w.finishOrPodium = AP_TrapFinishOrPodium(gGT, local);
	w.conditions = AP_TrapConditions(local);
	w.elapsedMs = gGT->elapsedTimeMS > 0 ? gGT->elapsedTimeMS : 32;

	AP_TrapSchedStep(&g_sched, &w);
	AP_TrapDrainEvents();

	for (e = 0; e < AP_TRAP_EFFECT_COUNT; e++)
		g_active[e] = (unsigned char)AP_TrapSchedActive(&g_sched, e);

	AP_TrapTrackRecovery(local, w.elapsedMs);
	AP_TrapApplyCamera(gGT, g_active[AP_TRAP_FIRSTPERSON]);
}

// ── Engine physics/input call-sites ──

// The Demo Camera prototype must not engage while the First Person trap owns
// cameraMode (it forces the mode every frame while active), and must force-
// clear if the trap arrives mid-engagement. Ownership means applied OR active:
// the applied latch lags one frame behind the scheduler on the arming edge.
int AP_TrapOwnsCamera(void)
{
	return g_fp_applied || AP_TrapSchedActive(&g_sched, AP_TRAP_FIRSTPERSON);
}

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
		// KNOWN, ACCEPTED SIDE EFFECT: this scales the friction scalars BEFORE the
		// per-terrain groundFrictionScale multiplies in (VehPhysForce.c:306-307),
		// so off-road drag is cut to about 16 percent as well. Kept as deliberate
		// counterplay (issue #116; Ignore Off-Road is a separate item, #14). With
		// map-lifetime duration this now lasts the whole map rather than 20 seconds.
		*perpendicularFriction = (*perpendicularFriction * AP_TRAP_FRICTION_SCALE) / 256;
		*forwardFriction = (*forwardFriction * AP_TRAP_FRICTION_SCALE) / 256;
	}
}

// Raise reserves to the trap floor without ever lowering them (s16 field; the
// floor is far below the 32767 ceiling and values already above it pass through
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
	if (g_active[AP_TRAP_USF_NOBRAKE])
	{
		// Genuinely USF-tier cap, derived from the real fire-level formula
		// (VehFire.c:358-359): cap = singleTurbo + (fireLevel * (sacred - singleTurbo))
		// >> 8, evaluated at a super turbo pad's fireLevel (0x800). A pin to
		// const_SacredFireSpeed would only grant red fire, which is not USF: the
		// engine defines USF as fireSpeedCap above sacred (VehFire.c:378).
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
	else if (g_active[AP_TRAP_BOOST])
	{
		AP_TrapFloorReserves(driver);
		// Floor, not pin: a hard pin to const_SingleTurboSpeed would downgrade a
		// red fire the player earns mid-trap. Only raise up to the milder tier.
		if (driver->fireSpeedCap < driver->const_SingleTurboSpeed)
			driver->fireSpeedCap = driver->const_SingleTurboSpeed;
	}
}

void AP_TrapDriveInput(struct Driver *driver, struct GamepadBuffer *pad,
                       u32 *buttonsHeld, u32 *cross, u32 *square)
{
	int usf = g_active[AP_TRAP_USF_NOBRAKE];
	int boost = g_active[AP_TRAP_BOOST];

	if ((!usf && !boost) || !AP_TrapIsLocal(driver))
		return;

	// Both traps disable braking. Forced Boost stops there: the player may release
	// acceleration and coast, which is what keeps it the milder of the two.
	*square = 0;
	*buttonsHeld &= ~BTN_SQUARE;

	if (usf)
	{
		*buttonsHeld |= BTN_CROSS;
		*cross = BTN_CROSS;
	}

	// Neutralise the analog sticks so the pull-back reverse/brake path (read later
	// in this same PhysLinear call, VehPhysProc.c:638/686) goes dead too. The pad
	// buffer is repopulated from input every frame before physics, so this only
	// affects the current frame's reads.
	//
	// The recovery guard releases both the analog path and, for Forced USF, the
	// forced throttle, once the kart has sat nearly still for about a second. A
	// wedged kart can then reverse out; a moving one still cannot brake.
	if (AP_TrapReverseRestored())
	{
		if (usf)
		{
			*buttonsHeld &= ~BTN_CROSS;
			*cross = 0;
		}
		return;
	}

	if (pad != 0)
	{
		pad->stickLY = AP_STICK_NEUTRAL;
		pad->stickRY = AP_STICK_NEUTRAL;
	}
}

#endif // CTR_AP
