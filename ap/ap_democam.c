#ifdef CTR_AP

#include <common.h> // structs CameraDC/PushBuffer/Driver/GameTracker + sdata + enums
#include <stdio.h>

#include "ap_democam.h"
#include "ap_traps.h"
#include "ap_democam_logic.h"
#include "ap_hooks.h" // AP_LogLine, AP_DevKeysEnabled

// Raw keyboard probe for the debug toggle (platform/native_input.c, CTR_AP
// only). Same declaration ap_traps.c uses -- keeps this module SDL-header-free.
int Platform_InputRawKeyDown(int scancode);

// Numpad +. 89-99 are taken (traps, Shortcutless, character picker); 84-88 are
// unclaimed and are not in the gameplay input map, so this never disturbs
// driving.
#define AP_DEMOCAM_KEY_TOGGLE 87

// The flag literals in ap_democam_logic.h have to stay the engine's values: the
// harness compares against them without ever seeing namespace_Camera.h.
CTR_STATIC_ASSERT(AP_DEMOCAM_FLAG_RESET_RAIN_POS == CAMERA_FLAG_RESET_RAIN_POS);
CTR_STATIC_ASSERT(AP_DEMOCAM_FLAG_BATTLE_EOR == CAMERA_FLAG_BATTLE_END_OF_RACE);
CTR_STATIC_ASSERT(AP_DEMOCAM_FLAG_DIRECTION_CHANGED == CAMERA_FLAG_DIRECTION_CHANGED);
CTR_STATIC_ASSERT(AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED == CAMERA_FLAG_ARCADE_END_OF_RACE_REQUESTED);
CTR_STATIC_ASSERT(AP_DEMOCAM_FLAG_TRACK_PATH_FACE_DRIVER == CAMERA_FLAG_TRACK_PATH_FACE_DRIVER);
CTR_STATIC_ASSERT(AP_DEMOCAM_FLAG_ARCADE_EOR_ACTIVE == CAMERA_FLAG_ARCADE_END_OF_RACE_ACTIVE);

// Camera flags that mean somebody else owns the shot right now: a mask rescue,
// either half of a scripted transition, a frozen camera, or the Battle
// end-of-race spin. Engaging under any of these would fight the owner, and an
// engagement already running is force-cleared.
#define AP_DEMOCAM_SCRIPTED_FLAGS                                         \
	(CAMERA_FLAG_MASK_GRAB | CAMERA_FLAG_TRANSITION_AWAY |                \
	 CAMERA_FLAG_TRANSITION_BACK | CAMERA_FLAG_TRANSITION_HOLD |          \
	 CAMERA_FLAG_FROZEN | CAMERA_FLAG_BATTLE_END_OF_RACE)

static struct ApDemoCamState ap_democam;
static int ap_democam_enabled;    // ap-config.txt debug_democam=1
static int ap_democam_want;       // toggle state the live tester drives
static int ap_democam_forceClear; // a connect reset asked for a release next tick

static const char *AP_DemoCamReasonName(int reason)
{
	switch (reason)
	{
	case AP_DEMOCAM_CLEAR_REQUEST:          return "toggled off";
	case AP_DEMOCAM_CLEAR_MAP_RESET:        return "map reset";
	case AP_DEMOCAM_CLEAR_MAP_CHANGE:       return "map change";
	case AP_DEMOCAM_CLEAR_SCRIPTED_CAMERA:  return "scripted camera handover";
	case AP_DEMOCAM_CLEAR_RACE_ENDED:       return "race window closed";
	case AP_DEMOCAM_CLEAR_CONNECT_RESET:    return "connection reset";
	default:                                return "?";
	}
}

// ── CameraDC / PushBuffer <-> plain mirror ──
// The two functions below are the ONLY place the prototype reads or writes
// engine camera state. Keep them exact inverses: the harness proves the round
// trip on the mirror, and that proof only carries over if nothing is dropped
// here.

static void AP_DemoCamRead(const struct CameraDC *cDC, const struct PushBuffer *pb,
                           struct ApDemoCamFields *f)
{
	f->flags = (unsigned)cDC->flags;

	f->cameraMode = cDC->cameraMode;
	f->cameraModePrev = cDC->cameraModePrev;
	f->currEOR = (unsigned long)cDC->currEOR;

	f->action = (unsigned)cDC->action;
	f->mode = cDC->mode;
	f->unk0xC = (unsigned)cDC->unk0xC;
	f->desiredRotX = cDC->desiredRot.x;

	f->transitionBlend = cDC->transitionBlend;
	f->transitionFrame = cDC->transitionFrame;
	f->transitionFrameCount = cDC->transitionFrameCount;
	f->spin360Angle = cDC->spin360Angle;

	f->trackPathProgress = cDC->trackPathProgress;
	f->trackPathNode = (unsigned long)cDC->trackPathNode;

	f->transitionToPos[0] = cDC->transitionTo.pos.x;
	f->transitionToPos[1] = cDC->transitionTo.pos.y;
	f->transitionToPos[2] = cDC->transitionTo.pos.z;
	f->transitionToRot[0] = cDC->transitionTo.rot.x;
	f->transitionToRot[1] = cDC->transitionTo.rot.y;
	f->transitionToRot[2] = cDC->transitionTo.rot.z;

	f->eorEndPos[0] = cDC->eorModeData.pointPath.endPos.x;
	f->eorEndPos[1] = cDC->eorModeData.pointPath.endPos.y;
	f->eorEndPos[2] = cDC->eorModeData.pointPath.endPos.z;
	f->eorSpeed = cDC->eorModeData.pointPath.speed;

	f->driverEyeOffset[0] = cDC->driverOffset_CamEyePos.x;
	f->driverEyeOffset[1] = cDC->driverOffset_CamEyePos.y;
	f->driverEyeOffset[2] = cDC->driverOffset_CamEyePos.z;
	f->angleAxisLerpRatio = cDC->angleAxisLerpRatio;
	f->driverLookOffset[0] = cDC->driverOffset_CamLookAtPos.x;
	f->driverLookOffset[1] = cDC->driverOffset_CamLookAtPos.y;
	f->driverLookOffset[2] = cDC->driverOffset_CamLookAtPos.z;

	f->heightSmoothingStart = cDC->heightSmoothing.startOffset;
	f->botFlagsPrevFrame = (int)cDC->botFlagsPrevFrame;

#if BUILD >= UsaRetail
	f->blastedLerpPending = cDC->BlastedLerp.boolLerpPending;
	f->blastedUnkOffset[0] = cDC->BlastedLerp.unkOffset[0];
	f->blastedUnkOffset[1] = cDC->BlastedLerp.unkOffset[1];
	f->blastedDesiredRot[0] = cDC->BlastedLerp.desiredRot.x;
	f->blastedDesiredRot[1] = cDC->BlastedLerp.desiredRot.y;
	f->blastedDesiredRot[2] = cDC->BlastedLerp.desiredRot.z;
	f->blastedDesiredPos[0] = cDC->BlastedLerp.desiredPos.x;
	f->blastedDesiredPos[1] = cDC->BlastedLerp.desiredPos.y;
	f->blastedDesiredPos[2] = cDC->BlastedLerp.desiredPos.z;
	f->blastedLerpFrames = cDC->BlastedLerp.framesRemaining;
#else
	f->blastedLerpPending = 0;
	f->blastedUnkOffset[0] = 0;
	f->blastedUnkOffset[1] = 0;
	f->blastedDesiredRot[0] = 0;
	f->blastedDesiredRot[1] = 0;
	f->blastedDesiredRot[2] = 0;
	f->blastedDesiredPos[0] = 0;
	f->blastedDesiredPos[1] = 0;
	f->blastedDesiredPos[2] = 0;
	f->blastedLerpFrames = 0;
#endif

	f->pbPos[0] = pb->pos.x;
	f->pbPos[1] = pb->pos.y;
	f->pbPos[2] = pb->pos.z;
	f->pbRot[0] = pb->rot.x;
	f->pbRot[1] = pb->rot.y;
	f->pbRot[2] = pb->rot.z;
	f->pbDistanceToScreenPrev = pb->distanceToScreen_PREV;
	f->pbDistanceToScreenCurr = pb->distanceToScreen_CURR;
}

static void AP_DemoCamWrite(struct CameraDC *cDC, struct PushBuffer *pb,
                            const struct ApDemoCamFields *f)
{
	cDC->flags = (CameraFlags)f->flags;

	cDC->cameraMode = (s16)f->cameraMode;
	cDC->cameraModePrev = (s16)f->cameraModePrev;
	cDC->currEOR = (void *)(unsigned long)f->currEOR;

	cDC->action = (u32)f->action;
	cDC->mode = (u16)f->mode;
	cDC->unk0xC = (u32)f->unk0xC;
	cDC->desiredRot.x = (s16)f->desiredRotX;

	cDC->transitionBlend = (s16)f->transitionBlend;
	cDC->transitionFrame = (s16)f->transitionFrame;
	cDC->transitionFrameCount = (s16)f->transitionFrameCount;
	cDC->spin360Angle = (s16)f->spin360Angle;

	cDC->trackPathProgress = f->trackPathProgress;
	cDC->trackPathNode = (struct CheckpointNode *)(unsigned long)f->trackPathNode;

	cDC->transitionTo.pos.x = (s16)f->transitionToPos[0];
	cDC->transitionTo.pos.y = (s16)f->transitionToPos[1];
	cDC->transitionTo.pos.z = (s16)f->transitionToPos[2];
	cDC->transitionTo.rot.x = (s16)f->transitionToRot[0];
	cDC->transitionTo.rot.y = (s16)f->transitionToRot[1];
	cDC->transitionTo.rot.z = (s16)f->transitionToRot[2];

	cDC->eorModeData.pointPath.endPos.x = (s16)f->eorEndPos[0];
	cDC->eorModeData.pointPath.endPos.y = (s16)f->eorEndPos[1];
	cDC->eorModeData.pointPath.endPos.z = (s16)f->eorEndPos[2];
	cDC->eorModeData.pointPath.speed = (s16)f->eorSpeed;

	cDC->driverOffset_CamEyePos.x = (s16)f->driverEyeOffset[0];
	cDC->driverOffset_CamEyePos.y = (s16)f->driverEyeOffset[1];
	cDC->driverOffset_CamEyePos.z = (s16)f->driverEyeOffset[2];
	cDC->angleAxisLerpRatio = (s16)f->angleAxisLerpRatio;
	cDC->driverOffset_CamLookAtPos.x = (s16)f->driverLookOffset[0];
	cDC->driverOffset_CamLookAtPos.y = (s16)f->driverLookOffset[1];
	cDC->driverOffset_CamLookAtPos.z = (s16)f->driverLookOffset[2];

	cDC->heightSmoothing.startOffset = (s16)f->heightSmoothingStart;
	cDC->botFlagsPrevFrame = (u32)f->botFlagsPrevFrame;

#if BUILD >= UsaRetail
	cDC->BlastedLerp.boolLerpPending = (s16)f->blastedLerpPending;
	cDC->BlastedLerp.unkOffset[0] = (s16)f->blastedUnkOffset[0];
	cDC->BlastedLerp.unkOffset[1] = (s16)f->blastedUnkOffset[1];
	cDC->BlastedLerp.desiredRot.x = (s16)f->blastedDesiredRot[0];
	cDC->BlastedLerp.desiredRot.y = (s16)f->blastedDesiredRot[1];
	cDC->BlastedLerp.desiredRot.z = (s16)f->blastedDesiredRot[2];
	cDC->BlastedLerp.desiredPos.x = (s16)f->blastedDesiredPos[0];
	cDC->BlastedLerp.desiredPos.y = (s16)f->blastedDesiredPos[1];
	cDC->BlastedLerp.desiredPos.z = (s16)f->blastedDesiredPos[2];
	cDC->BlastedLerp.framesRemaining = (s16)f->blastedLerpFrames;
#endif

	pb->pos.x = (s16)f->pbPos[0];
	pb->pos.y = (s16)f->pbPos[1];
	pb->pos.z = (s16)f->pbPos[2];
	pb->rot.x = (s16)f->pbRot[0];
	pb->rot.y = (s16)f->pbRot[1];
	pb->rot.z = (s16)f->pbRot[2];
	pb->distanceToScreen_PREV = f->pbDistanceToScreenPrev;
	pb->distanceToScreen_CURR = f->pbDistanceToScreenCurr;
}

// ── Gate + identity ──

// Race window: the same test AP_TrapTick uses (mid-race, lights out, no pause,
// menu, cutscene or end-of-race block).
static int AP_DemoCamRaceActive(struct GameTracker *gGT)
{
	if (gGT == 0)
		return 0;
	return (gGT->gameMode1 &
	        (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) == 0 &&
	       gGT->trafficLightsTimer < 1;
}

// CAM_ThTick can only select an authored end-of-race camera when the level
// carries an ST1_CAMERA_EOR table with entries (CAM.c:1887-1893), and
// CAM_EndOfRace only takes the arcade branch outside Battle with fewer than
// three screens (CAM.c:551). Anywhere else the request bit does nothing at all,
// which is exactly the "no safe racing camera target" case the design keeps the
// effect armed for.
static int AP_DemoCamEorTableUsable(struct GameTracker *gGT)
{
	if (gGT == 0 || gGT->level1 == 0 || gGT->level1->ptrSpawnType1 == 0)
		return 0;
	if ((gGT->gameMode1 & BATTLE_MODE) != 0)
		return 0;
	if (gGT->numPlyrCurrGame >= 3)
		return 0;
	return gGT->level1->ptrSpawnType1->count >= 3;
}

static void AP_DemoCamCollect(struct GameTracker *gGT, struct ApDemoCamIdentity *ident,
                              struct ApDemoCamGate *gate, struct CameraDC **outCDC,
                              struct PushBuffer **outPB)
{
	struct CameraDC *cDC = 0;
	struct PushBuffer *pb = 0;
	struct Driver *d = 0;

	memset(ident, 0, sizeof *ident);
	memset(gate, 0, sizeof *gate);
	*outCDC = 0;
	*outPB = 0;

	if (gGT == 0)
		return;

	ident->levelID = (int)gGT->levelID;

	cDC = &gGT->cameraDC[0];
	pb = cDC->pushBuffer;
	d = cDC->driverToFollow;

	gate->cameraPresent = (pb != 0 && d != 0 && d == gGT->drivers[0]);
	gate->raceActive = AP_DemoCamRaceActive(gGT);
	gate->loading = (sdata != 0 && sdata->Loading.stage != LOAD_IDLE);
	gate->scriptedCamera = (cDC->flags & AP_DEMOCAM_SCRIPTED_FLAGS) != 0;
	gate->driverIsBot = d != 0 && (((d->actionsFlagSet & ACTION_BOT) != 0) ||
	                               d->kartState == KS_WARP_PAD || d->kartState == KS_FREEZE);
	gate->eorTableUsable = AP_DemoCamEorTableUsable(gGT);
	gate->trapOwnsCamera = AP_TrapOwnsCamera();

	if (!gate->cameraPresent)
		return;

	ident->cameraDC = (unsigned long)cDC;
	ident->pushBuffer = (unsigned long)pb;
	ident->driver = (unsigned long)d;

	*outCDC = cDC;
	*outPB = pb;
}

// ── Release helper ──
// Reads the CURRENT camera, hands it to the state machine, and writes back only
// when the snapshot actually belonged to it. An abandoned release writes
// nothing: after a map change the CameraDC has been memset and re-seeded by
// CAM_Init (CAM.c:382), and pushing a stale snapshot over that is the one way
// this prototype could corrupt a camera it does not own.
static void AP_DemoCamRelease(struct CameraDC *cDC, struct PushBuffer *pb,
                              const struct ApDemoCamIdentity *ident, int reason)
{
	struct ApDemoCamFields fields;
	char msg[128];
	int result;

	if (!ap_democam.engaged)
		return;

	memset(&fields, 0, sizeof fields);
	if (cDC != 0 && pb != 0)
		AP_DemoCamRead(cDC, pb, &fields);

	result = AP_DemoCam_Release(&ap_democam, ident, &fields, reason);

	if (result == AP_DEMOCAM_RELEASE_RESTORED && cDC != 0 && pb != 0)
		AP_DemoCamWrite(cDC, pb, &fields);
	else if (result == AP_DEMOCAM_RELEASE_ABANDONED && cDC != 0)
		// The snapshot no longer describes this camera, so restoring it would
		// corrupt state the module does not own. The one bit the ENGAGE wrote
		// is still ours, and retail never clears it (CAM_StartOfRace clears
		// only the battle and active flags), so drop exactly that bit.
		cDC->flags &= ~AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED;

	snprintf(msg, sizeof msg, "[AP DEMOCAM] disengaged (%s): %s\n",
	         AP_DemoCamReasonName(reason),
	         result == AP_DEMOCAM_RELEASE_RESTORED ? "camera state restored"
	                                               : "snapshot abandoned, camera not written");
	AP_LogLine(msg);
}

// ── Debug toggle ──
static void AP_DemoCamDebugKey(void)
{
	static int prev = 0;
	int down;

	if (!ap_democam_enabled || !AP_DevKeysEnabled())
	{
		prev = 0;
		return;
	}

	down = Platform_InputRawKeyDown(AP_DEMOCAM_KEY_TOGGLE);
	if (down && !prev)
		ap_democam_want = !ap_democam_want;
	prev = down;
}

int AP_DemoCamConfigLine(const char *line)
{
	if (strncmp(line, "debug_democam=", 14) != 0)
		return 0;
	ap_democam_enabled = (line[14] != '0');
	return 1;
}

void AP_DemoCam_ConnectReset(void)
{
	// Requests the release rather than wiping the state here. Wiping would drop
	// the snapshot while ARCADE_END_OF_RACE_REQUESTED stayed set on the live
	// camera, and retail never clears that bit on its own (CAM.c:520 clears only
	// the ACTIVE and Battle bits), so the cinematic camera would stay armed for
	// the rest of the session. The next tick runs the ordinary release, which
	// restores when the camera is still the snapshotted one and abandons when it
	// is not.
	ap_democam_want = 0;
	if (ap_democam.engaged)
	{
		ap_democam_forceClear = 1;
		AP_LogLine("[AP DEMOCAM] fresh connect: releasing the active engagement\n");
	}
}

// ── Per-frame lifecycle ──
void AP_DemoCamTick(struct GameTracker *gGT)
{
	struct ApDemoCamIdentity ident;
	struct ApDemoCamGate gate;
	struct ApDemoCamFields fields;
	struct CameraDC *cDC;
	struct PushBuffer *pb;
	char msg[128];
	int reason;

	AP_DemoCamDebugKey();

	if (!ap_democam_enabled && !ap_democam.engaged)
		return;

	AP_DemoCamCollect(gGT, &ident, &gate, &cDC, &pb);

	if (ap_democam.engaged)
	{
		int levelChanged = ident.levelID != ap_democam.ident.levelID;
		int pointersChanged = ident.cameraDC != ap_democam.ident.cameraDC ||
		                      ident.pushBuffer != ap_democam.ident.pushBuffer ||
		                      ident.driver != ap_democam.ident.driver;

		reason = AP_DemoCam_ForcedClearReason(&gate, levelChanged, pointersChanged);
		if (reason == AP_DEMOCAM_CLEAR_NONE && ap_democam_forceClear)
			reason = AP_DEMOCAM_CLEAR_CONNECT_RESET;
		if (reason == AP_DEMOCAM_CLEAR_NONE && !ap_democam_want)
			reason = AP_DEMOCAM_CLEAR_REQUEST;

		if (reason != AP_DEMOCAM_CLEAR_NONE)
		{
			AP_DemoCamRelease(cDC, pb, &ident, reason);
			ap_democam_want = 0;
			ap_democam_forceClear = 0;
			return;
		}

		// Hold. This single bit is the whole of AP_DemoCam_ApplyHold, written
		// directly rather than through a read/modify/write of the full mirror:
		// re-asserting is free when the bit is already set, and rewriting the
		// other snapshotted fields every frame would fight whichever authored
		// mode CAM_ThTick is currently advancing.
		cDC->flags |= (CameraFlags)AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED;
		return;
	}

	ap_democam_forceClear = 0;

	if (!ap_democam_want || !AP_DemoCam_CanEngage(&gate))
		return;

	AP_DemoCamRead(cDC, pb, &fields);
	if (AP_DemoCam_Engage(&ap_democam, &ident, &fields) != AP_DEMOCAM_ENGAGE_OK)
		return;
	AP_DemoCamWrite(cDC, pb, &fields);

	snprintf(msg, sizeof msg,
	         "[AP DEMOCAM] engaged on level %d (engagement %u); driver untouched\n",
	         ident.levelID, ap_democam.engageCount);
	AP_LogLine(msg);
}

#endif // CTR_AP
