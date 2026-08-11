#ifndef AP_AUTHOR_H
#define AP_AUTHOR_H

// In-game placement author mode for AP box locations (#182, tooling for #109).
//
// Drive to a spot, press a key, and the client records that spot as a candidate
// AP-box placement AND spawns a marker there, so authoring is place-look-adjust
// rather than place-quit-rebuild-look. The marker comes from the additive model
// loader (ap_spawn.h), which is why this and the #124 item display share
// groundwork.
//
// The mode is OFF by default and is turned on from the in-game options menu:
// OPTIONS -> Authoring -> "Box Author Mode". That toggle is the gate; the keys
// below are inert without it, which is the same leak class the dev-key gate
// closes for the trap and Shortcutless keys (issue #16).
//
// SCOPE. Placements are recorded and rendered. They are NOT pickups: nothing
// here gives a placed marker collision or fires a location check. That is the
// unsolved half of #109 and is deliberately untouched.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

struct GameTracker;

// Where placements are written, next to the executable / working directory,
// alongside ap-state.json and ctr-ap.log. Read once at first use, rewritten on
// every change.
#define AP_AUTHOR_FILE "ap-box-placements.json"

// Total placements held across ALL tracks. The file is the durable store; this
// is the in-memory mirror. 512 is roughly 20 per authorable level with room to
// spare, and overflowing it refuses the drop with a log line rather than
// dropping earlier work.
#define AP_AUTHOR_MAX_PLACEMENTS 512

// Author keys (SDL scancodes, externals/SDL/include/SDL3/SDL_scancode.h).
// Numpad, continuing where the trap keys (KP_1..KP_6 = 89..94) and the
// Shortcutless keys (KP_7/KP_8 = 95/96) stop, so nothing here collides with an
// existing binding, with the engine's own F1-F12 dev handlers
// (native_platform.c), or with the gameplay input map.
#define AP_AUTHOR_KEY_DROP 97 // Numpad 9: drop a placement at the kart position
#define AP_AUTHOR_KEY_UNDO 98 // Numpad 0: delete the last placement on this track
#define AP_AUTHOR_KEY_LIST 99 // Numpad . : list this track's placements + save

// Per-frame: reads the keys, keeps the marker set in sync with the current
// level. Called from AP_OnFrame (ap_hooks.c) before AP_Spawn_OnFrame, so a
// placement dropped this frame gets its marker in the same frame.
void AP_Author_OnFrame(struct GameTracker *gGT);

// One HUD line while the mode is on, drawn from UI_RenderFrame_Racing. Shows
// the current level, how many placements it holds, and the last coordinate
// written -- the value as STORED, not the wider live one (#182 open question 3).
// Self-gates on the mode being on; a no-op otherwise.
void AP_Author_DrawHud(void);

// 1 while the mode is on. For call sites that want to skip work entirely.
int AP_Author_Enabled(void);

// ── the placement table, shared with the runtime box module ─────────────────
//
// ap_boxes.c (#109) spawns real, breakable boxes from the SAME table this mode
// edits: one file, one reader, one ordering. That is what makes "slot N of a
// track is the Nth placement listed for it" true on both sides by construction
// rather than by two parsers agreeing, and it means an authoring edit is
// visible to the runtime half without a restart.

// ── the two sources, and which one wins ─────────────────────────────────────
//
// The shipped client carries the FINAL authored set COMPILED IN
// (ap_placements_data.h), so a player installs one executable and the boxes are
// simply there. An external AP_AUTHOR_FILE next to the executable OVERRIDES it
// wholesale, which is what keeps authoring, private sets and a hotfix possible
// without a rebuild.
//
// PRECEDENCE IS BY EXISTENCE, NOT BY CONTENT: if the file opens, the file is the
// table, even when it parses to zero placements. An operator who deliberately
// empties the file means zero boxes, and silently resurrecting 241 compiled-in
// ones behind their back would be the worse failure. The zero case is logged
// loudly instead (AP_AuthorLoad).
//
// AUTHOR MODE IS FILE-ONLY. Dropping, deleting, listing and saving all act on
// the external file's table and never on the embedded default -- the compiled-in
// set is not editable by construction. Switching the mode on with no file present
// EXPORTS the built-in set into the file once and then edits that, so the mode
// still starts from the shipped placements without ever making the embedded
// table itself writable.
//
// The rule itself and the read-only view over the winner live in
// ap_placement_table.h (freestanding, and tested out of engine by
// tools/test-box-map.c); AP_PLACEMENT_SRC_EMBEDDED / _FILE come from there.
#include "ap_placement_table.h"

// Which of the two is live right now. Meaningful after AP_Author_EnsureLoaded().
int AP_Author_PlacementSource(void);

// Read the file if it has not been read yet. Author mode does this lazily when
// it is switched on; the runtime half needs the table with the mode OFF.
void AP_Author_EnsureLoaded(void);

// Entries currently held, across all levels, in the LIVE table (file when one is
// present, else the embedded default).
int AP_Author_PlacementCount(void);

// Copy placement `index` out. Any out pointer may be NULL. Returns 0 when the
// index is out of range, in which case nothing is written.
int AP_Author_PlacementGet(int index, int *level, short *x, short *y, short *z, short *rotY);

// Bumped on every change to the table (load, drop, delete). A consumer compares
// it to the value it built its own state from instead of polling every entry.
int AP_Author_PlacementGeneration(void);

#endif // CTR_AP
#endif // AP_AUTHOR_H
