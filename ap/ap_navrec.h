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
//   nav_use_recorded  reads one back and points the AI at it. Reading only, so
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
// this reloads the newest recording for the level and republishes it, and falls
// back to the level's own nav data when it cannot.
void AP_NavRec_AfterCheckpointRestore(void);

#endif // AP_NAVREC_H
