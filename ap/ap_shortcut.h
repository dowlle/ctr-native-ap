#ifndef AP_SHORTCUT_H
#define AP_SHORTCUT_H

// Archipelago SHORTCUTLESS mechanism for CTR Native.
//
// Compiled ONLY when CTR_AP is defined. Prerequisite of the adopted "Shortcut
// Unlock" PROGRESSION item (Feature Triage Register B, Stef 2026-07-04): the seed
// starts Shortcutless and the AP item flips it off. This module owns the runtime-
// toggleable enforcement so that future item just calls AP_ShortcutlessSet(0).
//
// ── Honest scoping (verified against this engine's own track data) ──
// CTR's retail track data has NO notion of a shortcut. The quadblock flag set
// (namespace_Level.h:163-179) has no shortcut bit; TerrainType (namespace_Level.h:
// 73-96) has no shortcut surface; there is one checkpoint graph per track whose
// branches (nextIndex_left/right) are treated as VALID alternate routes, not
// illegal ones. The engine's existing forward-skip detector
// (COLL_FIXED_PlayerSearch_CheckMaskGrabProgress, COLL.c:1346-1350) only fires on
// gaining >1/4 of the track length between two checkpoints -- real shortcuts are
// authored to stay checkpoint-legal, so it will NOT catch them. Therefore "which
// quads are a shortcut" is data that does not exist in retail and must be ADDED
// per track. What DOES already exist is the enforcement/respawn machinery: setting
// DRIVER_COLL_FLAG_MASK_GRAB_REQUEST (COLL.c:1481) makes the Aku-Aku mask grab the
// racer and replant them at their last valid checkpoint (VehStuckProc.c:43).
//
// This module therefore ships (1) the reusable enforcement hook wired into that
// existing machinery, runtime-toggleable, and (2) a live CAPTURE harness to BUILD
// the missing per-track annotation from real play (verify-first, no inference):
// drive onto a shortcut, mark its quads, then a per-track blockID table can be
// baked from the marks. See the handoff note for the proposed data model.

#ifdef CTR_AP

struct Driver;
struct QuadBlock;

// New per-quad flag proposed for baked annotation (an unused QuadBlockFlags bit,
// namespace_Level.h leaves 0x0008/0x0020/0x0100 free). Retail quads never set it;
// it is here so a future track-data annotation pass (or an asset patch) can mark
// shortcut geometry and AP_QuadIsShortcut() will honour it with zero code change.
#define AP_QUADBLOCK_FLAG_SHORTCUT 0x0008

// Runtime Shortcutless toggle. Default OFF. The future "Shortcut Unlock" AP item
// flips it via AP_ShortcutlessSet(0); the seed enables it at start (config
// shortcutless=1, or the apworld writing that config, or a start-state hook).
int  AP_ShortcutlessActive(void);
void AP_ShortcutlessSet(int on);

// Engine hook -- called from COLL_FIXED_PlayerSearch after the vanilla mask-grab
// test (COLL.c, right after the CheckMaskGrabProgress block). Returns 1 if the
// racer should be reset (grabbed) for taking a shortcut: i.e. Shortcutless is
// active, `driver` is the local player, and `quad` is flagged/known as a shortcut.
// Also drives the passive capture log when enabled. The caller ORs the result into
// DRIVER_COLL_FLAG_MASK_GRAB_REQUEST, reusing the existing respawn pipeline.
int AP_ShortcutCheck(struct Driver *driver, struct QuadBlock *quad);

// 1 if `quad` is known to be shortcut geometry: either the baked flag bit is set,
// or its blockID is in the runtime captured set (built live via the mark key).
int AP_QuadIsShortcut(struct QuadBlock *quad);

// Debug keybinds (F7 toggle Shortcutless, F8 mark the local player's current quad
// as a shortcut + log its blockID). Called from AP_TrapTick's key poll path.
void AP_ShortcutKeys(void);

// Parse one ap-config.txt line. Recognised: "shortcutless=1" (start Shortcutless),
// "shortcut_capture=1" (passively log every quad the local player touches, to map
// a shortcut's blockIDs). Returns 1 if consumed, 0 otherwise.
int AP_ShortcutConfigLine(const char *line);

#endif // CTR_AP

#endif // AP_SHORTCUT_H
