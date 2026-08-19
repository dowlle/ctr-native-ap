#ifndef AP_DEMOCAM_LOGIC_H
#define AP_DEMOCAM_LOGIC_H

#include <string.h>

// ============================================================================
// DEMO CAMERA PROTOTYPE -- pure state machine.
//
// Engine-free by design so tools/test-democam.c can drive the whole
// engage/restore contract on the host. ap_democam.c owns the only conversion
// between struct CameraDC / struct PushBuffer and the plain mirror below, and
// static-asserts the flag literals here against the engine enum.
//
// RETAIL CONSTRAINTS THIS MIRROR ENCODES (game/CAM.c, game/BOTS.c):
//
//  * The cinematic camera is ARMED by one flag bit and NOTHING else.
//    CAM_EndOfRace (CAM.c:544) either sets CAMERA_FLAG_ARCADE_END_OF_RACE_REQUESTED
//    on the driver's CameraDC, or -- in Battle / 3+ screens / a track with fewer
//    than 2 spawn points -- falls into CAM_EndOfRace_Battle, a different spin-360
//    behavior. Only the arcade branch is reused here.
//
//  * The per-frame driver is CAM_ThTick (CAM.c:1806), a PROC thread birthed by
//    CAM_Init. Its end-of-race block (CAM.c:1882-2068) reads only the flag, the
//    level's ST1_CAMERA_EOR table and the driver's checkpoint index. It never
//    reads boolDemoMode, ACTION_BOT or any bot field, so the camera does not
//    need its retail owner. BOT_FLAG_DEMO_CAMERA_STARTED (BOTS.c:1026) is the
//    CALLER's bookkeeping and is deliberately not touched.
//
//  * Nothing in retail ever clears ARCADE_END_OF_RACE_REQUESTED. CAM_StartOfRace
//    (CAM.c:520) clears only BATTLE_END_OF_RACE and ARCADE_END_OF_RACE_ACTIVE.
//    Disengage therefore has to clear the request bit itself or the cinematic
//    camera stays armed for the rest of the session.
//
//  * Engaging is latent, not immediate. CAM_ThTick only switches when the driver
//    reaches a checkpoint that has an authored end-of-race camera, and it refuses
//    an entry that equals cDC->currEOR. Clearing currEOR on engage is what makes
//    a second engagement on the same stretch of track possible at all.
//
//  * cameraMode != 0 is what bypasses CAM_FollowDriver_Normal, the ordinary chase
//    camera. Every authored mode writes a different subset of CameraDC, so the
//    snapshot below is the UNION of the writes in that switch plus the per-frame
//    fields the selected mode then advances.
//
//  * No field written by that path is a driver field. Steering, throttle,
//    physics and input are untouched by construction.
// ============================================================================

// Engine flag literals (include/namespace_Camera.h). ap_democam.c asserts these.
#define AP_DEMOCAM_FLAG_RESET_RAIN_POS         0x1u
#define AP_DEMOCAM_FLAG_BATTLE_EOR             0x4u
#define AP_DEMOCAM_FLAG_DIRECTION_CHANGED      0x8u
#define AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED   0x20u
#define AP_DEMOCAM_FLAG_TRACK_PATH_FACE_DRIVER 0x40u
#define AP_DEMOCAM_FLAG_ARCADE_EOR_ACTIVE      0x1000u

// Flag bits the cinematic path can own. Restore assigns exactly these back from
// the snapshot and leaves every other bit (REVERSE, FROZEN, MASK_GRAB,
// FIRE_SPEED_ZOOM, the transition trio) as the engine currently has it, so a
// concurrent engine write during the engagement is never rolled back.
#define AP_DEMOCAM_FLAG_OWNED_MASK                                        \
	(AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED | AP_DEMOCAM_FLAG_ARCADE_EOR_ACTIVE | \
	 AP_DEMOCAM_FLAG_BATTLE_EOR | AP_DEMOCAM_FLAG_TRACK_PATH_FACE_DRIVER)

// Raised (not restored) on release: the chase camera has to re-seat from the
// cinematic position instead of lerping out of it, and the rain buffer has to
// re-anchor. Both are self-clearing at the end of the next CAM_ThTick
// (CAM.c:2379-2384), which is why they are not part of the owned mask.
#define AP_DEMOCAM_FLAG_RESEAT_MASK \
	(AP_DEMOCAM_FLAG_RESET_RAIN_POS | AP_DEMOCAM_FLAG_DIRECTION_CHANGED)

// Every CameraDC / PushBuffer field the end-of-race path can write, widened to
// host types. Order and membership are the declared surface: nothing outside
// this struct is snapshotted, and nothing outside it is written on restore.
struct ApDemoCamFields
{
	unsigned flags;

	// Mode selection and the "which authored camera is in use" latch.
	int cameraMode;      // CameraDC 0x9a
	int cameraModePrev;  // 0x9c
	unsigned long currEOR; // 0xa0, an ST1_CAMERA_EOR entry pointer

	// Written by authored mode 3 (CAM.c:1979-1984).
	unsigned action;   // 0x04
	int mode;          // 0x08
	unsigned unk0xC;   // 0x0c
	int desiredRotX;   // 0x10

	// DELIBERATELY ABSENT: nearOrFar (0x0a) and zoomToggleState (0x92). The
	// cinematic path never writes them, but the player's own L2 zoom press does
	// (CAM.c:1855-1871) and does not trip a force-clear, so snapshotting them
	// would revert a live player action on release.

	// Transition ramp, shared by the start-of-race fly-in and modes 9/12/13.
	int transitionBlend;      // 0x8c
	int transitionFrame;      // 0x8e
	int transitionFrameCount; // 0x9e
	int spin360Angle;         // 0x90

	// Track-path cursor, advanced every frame by modes 9/12/13.
	int trackPathProgress;      // 0x94
	unsigned long trackPathNode; // 0x88

	// Authored target pose (modes 7, 9, 10, 11, 12, 13) and the Battle spin.
	int transitionToPos[3]; // 0xa4
	int transitionToRot[3]; // 0xaa

	// eorModeData union (0xb0). The pointPath members cover all 8 bytes, so
	// restoring them also restores the trackPathSpeed reading of the union.
	int eorEndPos[3];
	int eorSpeed;

	// Angle-axis follow offsets (modes 8 and 14).
	int driverEyeOffset[3];  // 0x74
	int angleAxisLerpRatio;  // 0x7a
	int driverLookOffset[3]; // 0x7c

	int heightSmoothingStart; // 0xc0, zeroed by authored mode 0
	int botFlagsPrevFrame;    // 0x84, written by authored modes 8/14 and never
	                          // by the chase path, so it does not self-heal

	// The whole 0xc6-0xda BlastedLerp group, mirrored together: the arcade
	// end-of-race arm and the KS_BLASTED landing path each write pending,
	// both vectors and the count as one unit, so restoring any member alone
	// would recombine a stale value with fresh ones.
	int blastedLerpPending;   // 0xc6
	int blastedUnkOffset[2];  // 0xc8
	int blastedDesiredRot[3]; // 0xcc
	int blastedDesiredPos[3]; // 0xd4
	int blastedLerpFrames;    // 0xda

	// PushBuffer pose. The chase camera rewrites these next frame, but they are
	// restored anyway so a single-frame engage/disengage leaves no visible jump.
	int pbPos[3];
	int pbRot[3];
	int pbDistanceToScreenPrev;
	int pbDistanceToScreenCurr;
};

// Which camera the snapshot belongs to. A map reset re-runs CAM_Init, which
// memsets the CameraDC and re-births the driver, so a snapshot taken before it
// must never be written back over the fresh state.
struct ApDemoCamIdentity
{
	unsigned long cameraDC;
	unsigned long pushBuffer;
	unsigned long driver;
	int levelID;
};

// Everything the tick needs to decide engage / hold / force-clear. All plain
// booleans so the harness can enumerate the matrix without an engine.
struct ApDemoCamGate
{
	int cameraPresent;   // CameraDC, PushBuffer and drivers[0] all non-null
	int raceActive;      // mid-race, lights out, no pause / menu / cutscene / EOR
	int loading;         // sdata->Loading.stage != LOAD_IDLE
	int scriptedCamera;  // mask grab, transition, frozen, Battle end-of-race
	int driverIsBot;     // ACTION_BOT, or a kart state that owns the camera
	int eorTableUsable;  // level has an ST1_CAMERA_EOR table CAM_ThTick can pick from
	int trapOwnsCamera;  // the First Person trap forces cameraMode every frame
};

enum
{
	AP_DEMOCAM_ENGAGE_OK = 0,
	AP_DEMOCAM_ENGAGE_ALREADY,  // idempotent re-arm on the same camera
	AP_DEMOCAM_ENGAGE_REFUSED   // still engaged on a DIFFERENT camera; clear first
};

enum
{
	AP_DEMOCAM_RELEASE_NOT_ENGAGED = 0, // safe no-op, no field written
	AP_DEMOCAM_RELEASE_RESTORED,
	AP_DEMOCAM_RELEASE_ABANDONED        // camera identity changed, snapshot dropped
};

enum
{
	AP_DEMOCAM_CLEAR_NONE = -1,
	AP_DEMOCAM_CLEAR_REQUEST = 0,
	AP_DEMOCAM_CLEAR_MAP_RESET,
	AP_DEMOCAM_CLEAR_MAP_CHANGE,
	AP_DEMOCAM_CLEAR_SCRIPTED_CAMERA,
	AP_DEMOCAM_CLEAR_RACE_ENDED,
	AP_DEMOCAM_CLEAR_CONNECT_RESET
};

struct ApDemoCamState
{
	int engaged;
	int lastClearReason;
	unsigned engageCount;
	struct ApDemoCamIdentity ident;
	struct ApDemoCamFields snapshot;
};

static inline void AP_DemoCam_Reset(struct ApDemoCamState *st)
{
	memset(st, 0, sizeof *st);
	st->lastClearReason = AP_DEMOCAM_CLEAR_NONE;
}

static inline int AP_DemoCam_IdentityEqual(const struct ApDemoCamIdentity *a,
                                           const struct ApDemoCamIdentity *b)
{
	return a->cameraDC == b->cameraDC && a->pushBuffer == b->pushBuffer &&
	       a->driver == b->driver && a->levelID == b->levelID;
}

static inline int AP_DemoCam_CanEngage(const struct ApDemoCamGate *g)
{
	return g->cameraPresent && g->raceActive && !g->loading && !g->scriptedCamera &&
	       !g->driverIsBot && !g->trapOwnsCamera && g->eorTableUsable;
}

// The single force-clear rule. identityChanged is computed against the snapshot
// taken at engage time, so it distinguishes a fresh map (levelID moved) from a
// same-map restart (the CameraDC / driver pointers moved under us).
static inline int AP_DemoCam_ForcedClearReason(const struct ApDemoCamGate *g,
                                               int levelChanged, int pointersChanged)
{
	if (levelChanged)
		return AP_DEMOCAM_CLEAR_MAP_CHANGE;
	if (pointersChanged || !g->cameraPresent)
		return AP_DEMOCAM_CLEAR_MAP_RESET;
	// A load in flight is the last frame on which the OLD camera is still the
	// one the snapshot describes, so the clear has to happen here rather than
	// after the destination initializes.
	if (g->loading)
		return AP_DEMOCAM_CLEAR_MAP_CHANGE;
	// The First Person trap writes cameraMode every frame while active, so an
	// engagement under it would snapshot the trap's mode and restore it after
	// the trap ended, stranding the player in first person. Treat the trap
	// like any other scripted camera owner.
	if (g->scriptedCamera || g->driverIsBot || g->trapOwnsCamera)
		return AP_DEMOCAM_CLEAR_SCRIPTED_CAMERA;
	if (!g->raceActive)
		return AP_DEMOCAM_CLEAR_RACE_ENDED;
	return AP_DEMOCAM_CLEAR_NONE;
}

// The whole engine-visible mutation on the engage EDGE: arm the request bit and
// drop the last-used end-of-race entry so CAM_ThTick is allowed to select one
// again (it refuses an entry equal to currEOR, CAM.c:1938).
static inline void AP_DemoCam_ApplyEngage(struct ApDemoCamFields *f)
{
	f->flags |= AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED;
	f->currEOR = 0;
}

// Per-frame hold. Only the request bit is re-asserted: clearing currEOR again
// would make CAM_ThTick re-select and re-initialize the same authored camera
// every frame instead of letting the selected one run.
static inline void AP_DemoCam_ApplyHold(struct ApDemoCamFields *f)
{
	f->flags |= AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED;
}

static inline int AP_DemoCam_Engage(struct ApDemoCamState *st,
                                    const struct ApDemoCamIdentity *ident,
                                    struct ApDemoCamFields *f)
{
	if (st->engaged)
	{
		if (!AP_DemoCam_IdentityEqual(&st->ident, ident))
			return AP_DEMOCAM_ENGAGE_REFUSED;
		AP_DemoCam_ApplyHold(f);
		return AP_DEMOCAM_ENGAGE_ALREADY;
	}

	st->snapshot = *f;
	st->ident = *ident;
	st->engaged = 1;
	st->engageCount++;
	AP_DemoCam_ApplyEngage(f);
	return AP_DEMOCAM_ENGAGE_OK;
}

static inline int AP_DemoCam_Release(struct ApDemoCamState *st,
                                     const struct ApDemoCamIdentity *ident,
                                     struct ApDemoCamFields *f, int reason)
{
	unsigned flags;

	if (!st->engaged)
		return AP_DEMOCAM_RELEASE_NOT_ENGAGED;

	st->engaged = 0;
	st->lastClearReason = reason;

	if (!AP_DemoCam_IdentityEqual(&st->ident, ident))
	{
		memset(&st->snapshot, 0, sizeof st->snapshot);
		memset(&st->ident, 0, sizeof st->ident);
		return AP_DEMOCAM_RELEASE_ABANDONED;
	}

	flags = f->flags;
	*f = st->snapshot;
	f->flags = (flags & ~(unsigned)AP_DEMOCAM_FLAG_OWNED_MASK) |
	           (st->snapshot.flags & (unsigned)AP_DEMOCAM_FLAG_OWNED_MASK) |
	           (unsigned)AP_DEMOCAM_FLAG_RESEAT_MASK;

	memset(&st->ident, 0, sizeof st->ident);
	return AP_DEMOCAM_RELEASE_RESTORED;
}

#endif // AP_DEMOCAM_LOGIC_H
