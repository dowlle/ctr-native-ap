#ifndef AP_TRACKER_PROGRESS_H
#define AP_TRACKER_PROGRESS_H

// Pure row model for the adventure-map tracker's Itemsanity / Hit progress
// panel. Kept in a header, like ap_lettersanity.h and ap_itemsanity_logic.h, so
// the engine draw and tools/test-tracker-progress.c exercise the exact same
// row-selection and state classification. No engine or network types here.

#include "ap_itemsanity_logic.h" // AP_ITEMSANITY_WEAPON_COUNT

// One weapon row per held-id entry in the frozen itemsanity order (0..4, 6..11).
#define AP_TRACKER_PROGRESS_WEAPONS AP_ITEMSANITY_WEAPON_COUNT
// Engine character ids 0..15. An enabled Hit block is validated in
// ap_seedcfg.cpp to carry all sixteen canonical codes, so this is always the
// whole roster, never a per-seed shorter selected target roster.
#define AP_TRACKER_PROGRESS_RACERS 16
#define AP_TRACKER_PROGRESS_MAX_ROWS \
	(AP_TRACKER_PROGRESS_WEAPONS + AP_TRACKER_PROGRESS_RACERS)

enum
{
	AP_TRACKER_PROGRESS_WEAPON = 0,
	AP_TRACKER_PROGRESS_HIT = 1
};

// The same three states AP_TrackerCodeState produces from server truth.
enum
{
	AP_TRACKER_PROGRESS_ABSENT = 0,  // no such location in this seed
	AP_TRACKER_PROGRESS_PENDING = 1, // in the seed, not checked
	AP_TRACKER_PROGRESS_DONE = 2     // checked on the server
};

typedef struct
{
	int kind;   // AP_TRACKER_PROGRESS_WEAPON or _HIT
	int index;  // weapon held-id row, or engine character id
	int owned;  // Itemsanity: the weapon item has been received
	int plain;  // AP_TRACKER_PROGRESS_* state for the plain use check
	int juiced; // AP_TRACKER_PROGRESS_* state for the juiced use check
} AP_TrackerProgressRow;

// A location that is not in the seed is absent, never done. `checked` is only
// consulted when the location exists, so a stale checked input for a missing
// location cannot render as complete.
static inline int AP_TrackerProgressStatePure(int exists, int checked)
{
	if (!exists)
		return AP_TRACKER_PROGRESS_ABSENT;
	return checked ? AP_TRACKER_PROGRESS_DONE : AP_TRACKER_PROGRESS_PENDING;
}

// Build the panel rows from option state and per-location server truth.
//
// Every enabled feature contributes its full roster:
// - Itemsanity on: one weapon row for each of the eleven frozen weapons, with
//   ownership informational and separate from the two use-check states.
// - Hit on: one row for each of the sixteen canonical engine ids, whether or
//   not its location is present on the server. The enabled block is validated
//   to carry all sixteen, so a missing membership is inconsistent state, and it
//   renders as absent rather than shrinking the roster or reading as done.
// With both options off the result is zero rows and the panel does not appear.
static inline int AP_TrackerProgressRowsPure(
	int itemsanityOn,
	int hitOn,
	const unsigned char weaponOwned[AP_TRACKER_PROGRESS_WEAPONS],
	const unsigned char weaponExists[AP_TRACKER_PROGRESS_WEAPONS][2],
	const unsigned char weaponChecked[AP_TRACKER_PROGRESS_WEAPONS][2],
	const unsigned char hitExists[AP_TRACKER_PROGRESS_RACERS],
	const unsigned char hitChecked[AP_TRACKER_PROGRESS_RACERS],
	AP_TrackerProgressRow out[AP_TRACKER_PROGRESS_MAX_ROWS])
{
	int count = 0;
	int i;

	if (itemsanityOn)
	{
		for (i = 0; i < AP_TRACKER_PROGRESS_WEAPONS; i++)
		{
			AP_TrackerProgressRow *row = &out[count++];
			row->kind = AP_TRACKER_PROGRESS_WEAPON;
			row->index = i;
			row->owned = weaponOwned[i] ? 1 : 0;
			row->plain = AP_TrackerProgressStatePure(
				weaponExists[i][0], weaponChecked[i][0]);
			row->juiced = AP_TrackerProgressStatePure(
				weaponExists[i][1], weaponChecked[i][1]);
		}
	}

	if (hitOn)
	{
		for (i = 0; i < AP_TRACKER_PROGRESS_RACERS; i++)
		{
			AP_TrackerProgressRow *row = &out[count++];
			row->kind = AP_TRACKER_PROGRESS_HIT;
			row->index = i;
			row->owned = 0;
			row->plain = AP_TrackerProgressStatePure(
				hitExists[i], hitChecked[i]);
			row->juiced = AP_TRACKER_PROGRESS_ABSENT;
		}
	}

	return count;
}

#endif
