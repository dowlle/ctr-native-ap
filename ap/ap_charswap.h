#ifndef AP_CHARSWAP_H
#define AP_CHARSWAP_H

// ---------------------------------------------------------------------------
// Hub character picker + hub-swap FEASIBILITY PROTOTYPE (spike, not a feature)
//
// Answers the R7 / issue #54 research ticket "can the adventure hub swap the
// player's character safely, and can a thin reusable 16-portrait picker drive
// it". Everything here is gated behind ap-config.txt dev_keys=1 and the
// adventure hub, so a normal seed never sees it.
//
// PRODUCTIONISED from the spike for #54/#209. All four original seams
// are now real: unlock state reads AP_CharacterUnlocked (the received character
// items), the stat configuration reads the seed's own resolved
// stat_source/stat_owner/stat_editing_allowed, and the seed's YAML starting
// racer is seated here, and the current racer persists to per-slot AP data
// storage (key "ctr_character_<slot>") across a reconnect or a slot switch.
// ---------------------------------------------------------------------------

struct GameTracker;
struct Driver;

// Per-frame logic + input. Called from AP_OnFrame (every frame, all modes);
// self-gates on dev keys + "adventure hub is open".
void AP_CharSwap_Tick(struct GameTracker *gGT);

// Draws the picker. Called from BOTH hub UI passes (AH_Map.c RenderAllHUD and
// UI_RenderFrame_AdvHub) next to AP_FeedDrawHub, which are mutually exclusive
// per frame, so this runs exactly once per frame while the hub is up.
void AP_CharPicker_Draw(void);

// True while the picker owns input. Used to keep the driver frozen, to refuse a
// Start press so the pause menu cannot open on top of it, and to stop AH_Door's
// #51 release from taking the picker's own kart freeze back.
int AP_CharSwap_PickerOpen(void);

// Must the hub's standing HUD stand down this frame?
//
// One question, asked by every hub HUD site, so they cannot drift apart. The
// picker is a full-screen modal drawn late in the hub UI pass, and everything
// the hub keeps permanently on screen is submitted BEFORE it and therefore
// lands on top of it (AddPrim prepends and walks,
// platform/native_libgpu.c:309). The minimap group was the first surface caught
// doing this; the relic / key / trophy counters across the top of the screen
// were the second, found in live play on 2026-08-12.
//
// Suppressing the draw is deliberate rather than re-ordering two ordering
// tables against each other: the picker holds the kart and the input while it
// is up, so a standing counter has nothing useful to say, and a skipped draw
// cannot be defeated later by a submission-order detail the way a re-order can.
int AP_CharSwap_HubHudHidden(void);

// The adventure-hub pause menu's SELECT CHARACTER row was chosen (#238).
//
// Records a request rather than opening the picker directly: the picker refuses
// to open while the game is paused, and rightly so, since the pause owns the
// vehicle-freeze bits and its RectMenu owns input. The caller resumes through
// the vanilla RESUME path and AP_CharSwap_Tick opens the picker on the first
// safe frame after that. The request has no deadline and is dropped if the hub
// goes away before it can be honoured.
void AP_CharSwap_RequestPickerFromPause(void);

// Should the adventure-hub pause menu carry a SELECT CHARACTER row at all?
// True on a seed carrying the character phase, and on a dev-keys build so the
// manual matrix can reach the row without a seed connected.
int AP_CharSwap_PauseRowLive(void);

// Does this seed carry the character phase (unlocks, a racer lock, a non-vanilla
// stat source, a chosen starting racer or a forced starting class)? A 0.2.0 seed
// with every character option off answers 0 and behaves like a pre-feature seed.
int AP_CharSwap_FeatureLive(void);

// Racer-lock enforcement (ruled 2026-08-17). ForceForWarp: called at the warp
// commit with the PHYSICAL pad id; seats the pad's demanded racer (if any)
// before the destination load, remembering the player's racer. RestoreOnHub:
// called when a level transition lands in a hub; puts the remembered racer
// back. Both are cheap no-ops when no lock / no enforcement is in flight.
void AP_RacerLock_ForceForWarp(int physPadLevelID);
void AP_RacerLock_RestoreOnHub(void);

// Seat this slot's racer, once per authoritative answer: the value persisted in
// per-slot AP data storage if there is one, otherwise the seed's YAML starting
// racer. The racer is never an item, so nothing else would ever apply it.
//
// Takes gGT only to ask ap_cs_hubReady whether this is a frame where changing an
// ALREADY-seated racer is safe. A stored racer whose unlock item has not been
// replayed yet is held pending with no deadline (ap_charseat.h explains why no
// timer can be trusted here), so its restore can arrive mid-race; that restore
// waits for the hub. The first seat of a connection is not gated, because the
// hub births the player during its own load.
void AP_CharSwap_SeatStartingCharacter(struct GameTracker *gGT);

// The racer the adventure-start Garage character select must commit, or -1 when
// this seed does not carry the character phase and the retail garage should run
// untouched. When it returns a racer, the garage's own picker is SKIPPED: the
// seed (or the racer persisted from a previous session) decides who you start
// as, and switching afterwards is the hub picker's job.
//
// The gate applies in all-unlocked mode too. The garage can only ever offer the
// eight vanilla starters (gGarage.unusedArr_garageChars, game/233/D233.c:29),
// so leaving it live there would not be "the comfortable way to choose" -- it
// would be a second, narrower picker that silently overwrites the seated racer.
int AP_CharSwap_GarageRacer(void);

// Re-arm the one-shot seat above on a fresh connect, so a reconnect or a slot
// switch re-applies the authoritative racer instead of keeping whatever the
// local save holds. Also ZEROES the live editable-stat deltas: the restore is
// asynchronous and may never run at all on a slot with no stored package, so
// without this the previous slot's tune stays in effect on the new one.
void AP_CharSwap_ConnectReset(void);

// Post-pass over VehBirth_SetConsts: applies the AP-owned stat package for the
// LOCAL player only, over the same struct Driver offsets the vanilla loop
// wrote. This is the shape recommended by the 2026-07-25 Tier-1 source read
// (no vanilla data layout is touched, so retail behaviour is unchanged when
// the package is inactive). No-op unless a test package is active.
void AP_CharSwap_ApplyStatPackage(struct Driver *driver);

#endif // AP_CHARSWAP_H
