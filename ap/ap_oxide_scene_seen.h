#ifndef AP_OXIDE_SCENE_SEEN_H
#define AP_OXIDE_SCENE_SEEN_H

// Issue #377: "the Oxide Final Challenge scene has played for this slot", and
// "the Oxide Final Challenge is open message has been shown for this slot".
//
// The scene plays once per seed, so whether it already played is per-slot
// server state, not local save state: it must survive a client restart and a
// change of machine, and a fresh room must start clean. Server data storage is
// kept per room, so a new room built from the same seed starts without the key.
// The key also names the seed, team and slot (the same shape as the hub-door
// history in ap_door_history.h), so two slots in one room never share it.
//
// FAIL SAFE. Until the Get reply for this key has arrived (`known()`), the
// caller must not auto-play the scene: an unknown flag could be a "seen" that
// has not been read yet, and must not show the open message either. A value
// outside 0..3 is some other tool's key and leaves the flag unknown for the
// whole session, which also means no auto-play and no message.
//
// Value: an integer bitmask, 0..3. Bit 0 (AP_OXIDE_FLAG_SCENE): the scene
// has played, or was skipped by the local Skip Cutscenes option. Bit 1
// (AP_OXIDE_FLAG_OPEN_MSG): the "Oxide Final Challenge is open" message has
// been shown. Written with the data storage "or" operation and a default of 0,
// so concurrent writers can only ever set bits.
#define AP_OXIDE_FLAG_SCENE    1u
#define AP_OXIDE_FLAG_OPEN_MSG 2u
#define AP_OXIDE_FLAG_ALL      (AP_OXIDE_FLAG_SCENE | AP_OXIDE_FLAG_OPEN_MSG)
#ifdef __cplusplus
#include "ap_scene_seen_flags.h"

// The storage, barrier and merge rules are shared with the boss-door scene
// flag (ap_scene_seen_flags.h).
struct APOxideSceneSeen : APSceneSeenFlags {
    APOxideSceneSeen() : APSceneSeenFlags("ctr_oxide_final_scene_v1", AP_OXIDE_FLAG_ALL) {}
    using APSceneSeenFlags::record;
    bool seen() const { return has(AP_OXIDE_FLAG_SCENE); }
    bool record() { return record(AP_OXIDE_FLAG_SCENE); }
};
#endif
#endif
