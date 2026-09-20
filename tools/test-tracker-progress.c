// Host assertions for the adventure-map tracker's Itemsanity and Hit progress
// rows (#349 sibling). The row model lives in ap/ap_tracker_progress.h, so this
// harness and the engine draw classify the same inputs the same way.
//
// The two rules this pins:
//   - Every enabled feature contributes its FULL roster. An enabled Hit block
//     is validated in ap_seedcfg.cpp to carry all sixteen canonical locations,
//     so a missing membership is inconsistent state and still renders a row,
//     classified absent rather than done. It never shrinks the roster.
//   - A location that is not in the seed is absent, never pending and never
//     done, even if a stale checked input says otherwise.
//
// cc -std=c99 -Wall -Wextra -Wno-unused-function -I . -o /tmp/test-tracker-progress tools/test-tracker-progress.c
// /tmp/test-tracker-progress

#include <stdio.h>
#include <string.h>
#include "../ap/ap_tracker_progress.h"

static int checks;
static int failures;

#define EXPECT(expr, why) \
	do { \
		checks++; \
		if (!(expr)) { \
			printf("FAIL %s\n", why); \
			failures++; \
		} \
	} while (0)

int main(void)
{
	unsigned char owned[AP_TRACKER_PROGRESS_WEAPONS] = {0};
	unsigned char weaponExists[AP_TRACKER_PROGRESS_WEAPONS][2] = {{0}};
	unsigned char weaponChecked[AP_TRACKER_PROGRESS_WEAPONS][2] = {{0}};
	unsigned char hitExists[AP_TRACKER_PROGRESS_RACERS] = {0};
	unsigned char hitChecked[AP_TRACKER_PROGRESS_RACERS] = {0};
	AP_TrackerProgressRow rows[AP_TRACKER_PROGRESS_MAX_ROWS];
	int combination, i, count;

	EXPECT(AP_TRACKER_PROGRESS_WEAPONS == 11, "eleven weapons in the frozen order");
	EXPECT(AP_TRACKER_PROGRESS_RACERS == 16, "sixteen canonical Hit engine ids");
	EXPECT(AP_TRACKER_PROGRESS_MAX_ROWS == 27, "the panel holds both full rosters");

	// Both options off: no rows, so the panel has nothing to show.
	count = AP_TrackerProgressRowsPure(0, 0, owned, weaponExists, weaponChecked,
		hitExists, hitChecked, rows);
	EXPECT(count == 0, "both options off produces no rows");

	// Itemsanity only, every plain/juiced done combination across all weapons.
	for (combination = 0; combination < 4; combination++)
	{
		memset(weaponExists, 1, sizeof weaponExists);
		memset(weaponChecked, 0, sizeof weaponChecked);
		for (i = 0; i < AP_TRACKER_PROGRESS_WEAPONS; i++)
		{
			owned[i] = (unsigned char)(i & 1);
			weaponChecked[i][0] = (unsigned char)(combination & 1);
			weaponChecked[i][1] = (unsigned char)((combination >> 1) & 1);
		}

		count = AP_TrackerProgressRowsPure(1, 0, owned, weaponExists,
			weaponChecked, hitExists, hitChecked, rows);
		EXPECT(count == AP_TRACKER_PROGRESS_WEAPONS,
			"Itemsanity emits every weapon row");

		for (i = 0; i < AP_TRACKER_PROGRESS_WEAPONS; i++)
		{
			EXPECT(rows[i].kind == AP_TRACKER_PROGRESS_WEAPON, "Itemsanity row kind");
			EXPECT(rows[i].index == i, "Itemsanity row order");
			EXPECT(rows[i].owned == (i & 1), "ownership is informational");
			EXPECT(rows[i].plain ==
				((combination & 1) ? AP_TRACKER_PROGRESS_DONE : AP_TRACKER_PROGRESS_PENDING),
				"plain completion state");
			EXPECT(rows[i].juiced ==
				((combination & 2) ? AP_TRACKER_PROGRESS_DONE : AP_TRACKER_PROGRESS_PENDING),
				"juiced completion state");
		}
	}

	// A weapon use check that is not in the seed is absent, and its weapon row
	// stays: the roster is not shortened. A stale checked input cannot make it
	// read as done.
	memset(weaponExists, 1, sizeof weaponExists);
	memset(weaponChecked, 0, sizeof weaponChecked);
	weaponExists[4][1] = 0;
	weaponChecked[4][1] = 1;
	count = AP_TrackerProgressRowsPure(1, 0, owned, weaponExists, weaponChecked,
		hitExists, hitChecked, rows);
	EXPECT(count == AP_TRACKER_PROGRESS_WEAPONS,
		"a missing juiced check does not remove its weapon");
	EXPECT(rows[4].plain == AP_TRACKER_PROGRESS_PENDING,
		"the existing plain check remains pending");
	EXPECT(rows[4].juiced == AP_TRACKER_PROGRESS_ABSENT,
		"a missing juiced check is absent even if checked input is true");

	weaponExists[2][0] = 0;
	weaponChecked[2][0] = 1;
	weaponExists[2][1] = 1;
	weaponChecked[2][1] = 0;
	count = AP_TrackerProgressRowsPure(1, 0, owned, weaponExists, weaponChecked,
		hitExists, hitChecked, rows);
	EXPECT(count == AP_TRACKER_PROGRESS_WEAPONS,
		"a missing plain check does not remove its weapon");
	EXPECT(rows[2].plain == AP_TRACKER_PROGRESS_ABSENT,
		"a missing plain check is absent even if checked input is true");
	EXPECT(rows[2].juiced == AP_TRACKER_PROGRESS_PENDING,
		"the existing juiced check remains pending");

	// Hit only: the full canonical roster, none checked.
	memset(hitExists, 1, sizeof hitExists);
	memset(hitChecked, 0, sizeof hitChecked);
	count = AP_TrackerProgressRowsPure(0, 1, owned, weaponExists, weaponChecked,
		hitExists, hitChecked, rows);
	EXPECT(count == AP_TRACKER_PROGRESS_RACERS, "Hit option emits the full roster");
	for (i = 0; i < count; i++)
	{
		EXPECT(rows[i].kind == AP_TRACKER_PROGRESS_HIT, "Hit row kind");
		EXPECT(rows[i].index == i, "Hit row order");
		EXPECT(rows[i].owned == 0, "a Hit row carries no weapon ownership");
		EXPECT(rows[i].plain == AP_TRACKER_PROGRESS_PENDING, "none hit is pending");
	}

	// Hit only: some checked.
	for (i = 0; i < AP_TRACKER_PROGRESS_RACERS; i++)
		hitChecked[i] = (unsigned char)((i % 3) == 0);
	count = AP_TrackerProgressRowsPure(0, 1, owned, weaponExists, weaponChecked,
		hitExists, hitChecked, rows);
	EXPECT(count == AP_TRACKER_PROGRESS_RACERS, "partial hits keep the full roster");
	for (i = 0; i < count; i++)
		EXPECT(rows[i].plain ==
			((i % 3) == 0 ? AP_TRACKER_PROGRESS_DONE : AP_TRACKER_PROGRESS_PENDING),
			"some hit follows checked state");

	// Hit only: all checked.
	memset(hitChecked, 1, sizeof hitChecked);
	count = AP_TrackerProgressRowsPure(0, 1, owned, weaponExists, weaponChecked,
		hitExists, hitChecked, rows);
	EXPECT(count == AP_TRACKER_PROGRESS_RACERS, "all hits keep the full roster");
	for (i = 0; i < count; i++)
		EXPECT(rows[i].plain == AP_TRACKER_PROGRESS_DONE, "all hit is done");

	// Inconsistent state: an enabled block is validated to carry all sixteen,
	// so a location missing from the server set must still render its row and
	// must read absent, not done, even with a stale checked input.
	hitExists[7] = 0;
	hitChecked[7] = 1;
	count = AP_TrackerProgressRowsPure(0, 1, owned, weaponExists, weaponChecked,
		hitExists, hitChecked, rows);
	EXPECT(count == AP_TRACKER_PROGRESS_RACERS,
		"a missing Hit membership does not shorten the roster");
	EXPECT(rows[7].kind == AP_TRACKER_PROGRESS_HIT, "the missing row is still a Hit row");
	EXPECT(rows[7].index == 7, "the missing row keeps its engine id");
	EXPECT(rows[7].plain == AP_TRACKER_PROGRESS_ABSENT,
		"a missing Hit membership is absent, not done");
	EXPECT(rows[6].plain == AP_TRACKER_PROGRESS_DONE, "a present, checked Hit stays done");

	// Both on: weapon rows first, then the full Hit roster.
	memset(weaponExists, 1, sizeof weaponExists);
	memset(weaponChecked, 0, sizeof weaponChecked);
	memset(hitExists, 1, sizeof hitExists);
	memset(hitChecked, 0, sizeof hitChecked);
	count = AP_TrackerProgressRowsPure(1, 1, owned, weaponExists, weaponChecked,
		hitExists, hitChecked, rows);
	EXPECT(count == AP_TRACKER_PROGRESS_MAX_ROWS, "both options emit both rosters");
	for (i = 0; i < AP_TRACKER_PROGRESS_WEAPONS; i++)
		EXPECT(rows[i].kind == AP_TRACKER_PROGRESS_WEAPON, "weapons precede hits");
	for (i = AP_TRACKER_PROGRESS_WEAPONS; i < count; i++)
		EXPECT(rows[i].kind == AP_TRACKER_PROGRESS_HIT, "hits follow weapons");
	EXPECT(rows[AP_TRACKER_PROGRESS_WEAPONS].index == 0, "the Hit roster starts at id 0");

	printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "ok", checks, failures);
	return failures != 0;
}
