#ifndef AP_NAVREC_H
#define AP_NAVREC_H

// ============================================================================
// AI lap recorder, 0.2.0 slice.
//
// Drive a race, get a container of your own laps out; hand that container back
// to the bots on a later run. Two independent options gate the two halves, both
// off by default:
//
//   nav_record        writes files under ap-navpaths/. Nothing else in this
//                     build creates files on a player's disk unasked, and this
//                     option is the whole consent surface for that.
//   nav_use_recorded  reads them back and points the AI at them. Up to three
//                     containers fill the three engine lanes, preferring a
//                     different contributor per lane so a race is not one
//                     person's line under one name seven times. Reading only, so
//                     someone who wants recorded lines never has to switch on
//                     the half that writes.
//   nav_driver_name   the name stamped into the file. Empty falls back to the
//                     configured Archipelago slot name.
//
// With both options off this module samples nothing, allocates nothing, opens
// nothing and replaces no nav pointer.
//
// The file format is specified in tools/navrec/FORMAT.md and implemented in the
// freestanding ap/ap_navrec_format.h, which tools/test-navrec.c compiles
// directly.
//
// Playback of named bots, the widened lane table, the pace controller and the
// 1 to 7 field are a later release and are deliberately absent here.
// ============================================================================

// Per-frame sampling, the option state machine and the end-of-race write. Called
// from the AP frame hook.
void AP_NavRec_Tick(struct GameTracker *gGT);

// Called immediately after BOTS_InitNavPath has populated sdata->NavPath_ptr*
// and before BOTS_GotoStartingLine takes frame 0. Replaces those pointers with a
// recorded container when nav_use_recorded is on and a file exists for the level.
// BOTS_InitNavPath is the only place the engine reads level1->LevNavTable, so
// this is the one point at which retargeting the AI is both complete and safe.
void AP_NavRec_AfterBotsInit(void);

// Called from VehPickupItem_ShootNow when a held item fires. Counts the two
// items the container records, and only for the local player. Firing the Mask
// does not make a lap dirty; it is counted so a later consumer can judge it.
void AP_NavRec_NoteItemFire(struct Driver *d, int weaponID);

// Called at the end of NativeCheckpoint_RelocateSDataPointers. Relocation cannot
// repair a state restored into a fresh process, where these lanes are zeroed, so
// this reassembles the same lane set for the level and republishes it, and falls
// back to the level's own nav data when it cannot. Selection is deterministic,
// so an unchanged folder restores the field the race started with.
void AP_NavRec_AfterCheckpointRestore(void);

// Draw the sanitized contributor name above every bot currently driving a
// recorded lane. The loader fills the three engine lanes from up to three
// different people's containers, so the name is per lane: a bot carries the name
// on the file feeding the lane it is following, read from botData.botPath each
// frame. A lane whose file carries no name draws no label and leaves the other
// lanes drawing theirs.
void AP_NavRec_DrawBotNames(void);

// Custom-track loader seam. A package owns a permanent 16-byte UUID and a
// navigation compatibility revision. Set this before BOTS_InitNavPath for a
// custom load and clear it before an ordinary retail load. The physical engine
// level ID is deliberately not part of the identity.
//
// Block is the third arm: a load that SERVES custom-track bytes but has no
// usable navigation identity. Falling back to Clear there would let the
// borrowed host LevelID match retail recordings onto custom geometry and stamp
// laps recorded here as retail lines; blocked, this module neither loads nor
// writes a recording for the load and the level's own lanes run.
void AP_NavRec_SetActiveCustomTrack(const unsigned char uuid[16], unsigned int navRevision);
void AP_NavRec_ClearActiveCustomTrack(void);
void AP_NavRec_BlockRecordedLanes(void);

#endif // AP_NAVREC_H
