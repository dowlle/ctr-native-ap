#ifndef AP_SHORTCUT_H
#define AP_SHORTCUT_H

// Archipelago SHORTCUTLESS mechanism for CTR Native.
//
// Compiled ONLY when CTR_AP is defined. Prerequisite of the planned "Shortcut
// Unlock" PROGRESSION item: the seed
// can start Shortcutless and the AP item flips it off. This module owns the
// runtime-toggleable enforcement so that future item just calls AP_ShortcutlessSet(0).
//
// ── Mechanism (ported from a real published GPL Shortcutless) ──
// Source: kkv0n/Penguin-MODSK @ game_modes_mods, mods/game_modes/ShortcutLess/
// (a fork of GPL-3.0 CTR-tools/CTR-ModSDK; same retail engine + level data as
// this decomp). An earlier draft of this module
// wrongly concluded shortcuts need full per-track hand annotation and shipped an
// F8 capture harness; that was overscoped. Enforcement is three layers, and ~99%
// of it comes from data retail ALREADY has:
//
//   1. OFF-ROAD TERRAIN (the bulk, zero authoring): a quad whose terrain_type is
//      grass/dirt/snow (+water on Dragon Mines) is off the intended racing line,
//      so touching one is a shortcut -> mask grab. Read live from quad->terrain_type.
//   2. CHECKPOINT-% SKIP (zero coordinate authoring, per-track % constants only):
//      if the driver's lastValid checkpointIndex jumps more than a per-track % of
//      the track's checkpoint count between frames, it's a gap-skip -> rewind
//      lastValid + mask grab. AP_ShortcutSkipTick() (per frame from AP_OnFrame).
//   3. A SMALL per-track blockID table (the only hand data): ~30 on-road-shortcut
//      quads the source hands us, ported verbatim. VERIFY-FIRST CAVEAT: these
//      blockIDs are the mod's retail-disc values; confirm per track against our data
//      in-game before trusting them (shortcut_capture=1 / Numpad 8 log the touched IDs).
//
// Enforcement reuses the engine's own respawn: layers 1 & 3 go through the existing
// COLL_FIXED_PlayerSearch hook (AP_ShortcutCheck -> OR DRIVER_COLL_FLAG_MASK_GRAB_
// REQUEST, COLL.c); layer 2 sets that flag directly after rewinding lastValid, so
// the Aku-Aku mask grab replants the racer at the previous valid checkpoint
// (VehStuckProc.c). Nothing mutates persistent level data, so with Shortcutless OFF
// (allowed mode, or after the Shortcut Unlock item) there is ZERO vanilla impact.

#ifdef CTR_AP

struct Driver;
struct QuadBlock;
struct GameTracker;

// Optional baked annotation: an unused QuadBlockFlags bit (0x0008; the engine
// leaves 0x0008/0x0020/0x0100 free). A future asset/track-data pass can set it to
// mark shortcut geometry and AP_QuadIsShortcut() will honour it. Retail never sets
// it, so it is inert until used.
#define AP_QUADBLOCK_FLAG_SHORTCUT 0x0008

// Shortcuts option model. YAML `shortcuts:` maps here; native
// reads it from ap-config now (slot_data later).
enum AP_ShortcutMode
{
	AP_SC_ALLOWED = 0,    // vanilla; enforcement never arms; no unlock item
	AP_SC_FORBIDDEN = 1,  // Shortcutless enforced all seed; no item
	AP_SC_UNLOCKABLE = 2  // starts enforced; Shortcut Unlock item flips it off
};

// Runtime Shortcutless toggle. The "Shortcut Unlock" AP item flips it via
// AP_ShortcutlessSet(0); the seed enables it at start per the mode above.
int  AP_ShortcutlessActive(void);
void AP_ShortcutlessSet(int on);
int  AP_ShortcutMode(void);             // current AP_ShortcutMode
int  AP_ShortcutSwitchingAllowed(void); // allow_shortcut_switching (in-game switch)

// Engine hook -- called from COLL_FIXED_PlayerSearch after the vanilla mask-grab
// test. Returns 1 if the racer should be reset (grabbed): Shortcutless active,
// `driver` is the local player, and `quad` is shortcut geometry (layers 1 & 3).
// Caller ORs the result into DRIVER_COLL_FLAG_MASK_GRAB_REQUEST.
int AP_ShortcutCheck(struct Driver *driver, struct QuadBlock *quad);

// 1 if `quad` is shortcut geometry: off-road terrain, a per-track table blockID,
// the live-captured set, or the baked flag.
int AP_QuadIsShortcut(struct QuadBlock *quad);

// Per-frame checkpoint-% gap-skip detector (layer 2). Call from AP_OnFrame.
void AP_ShortcutSkipTick(struct GameTracker *gGT);

// Debug keybinds (Numpad 7 dev toggle Shortcutless, Numpad 8 log/mark the local player's
// current quad blockID). DEAD unless ap-config.txt dev_keys=1 (AP_DevKeysEnabled,
// shared with the trap test keys) -- unguarded they leaked into release play,
// issue #16. Dev tools for verifying/extending the table; the real player
// switch (gated on allow_shortcut_switching) is the future in-game menu.
void AP_ShortcutKeys(void);

// Parse one ap-config.txt line. Recognised: "shortcuts=allowed|forbidden|
// unlockable", "allow_shortcut_switching=0|1", "shortcut_capture=0|1", and the
// legacy "shortcutless=0|1". Returns 1 if consumed, 0 otherwise.
int AP_ShortcutConfigLine(const char *line);

#endif // CTR_AP

#endif // AP_SHORTCUT_H
