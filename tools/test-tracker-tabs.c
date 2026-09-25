// Host checks for the enlarged Adventure tracker's tab cycle, its RACERS rows
// and the hub pause menu rows that open it.
//
//   - Tabs: the five hub maps, then RACERS and ITEMS, on one L1/R1 cycle. A tab
//     with nothing to show for the seed is skipped, for every option mix.
//   - Racer rows: one per engine character id; each pad's racer lock lands on
//     exactly the racer it names, cup pads included, malformed locks nowhere.
//   - Pause rows: RESUME, [SELECT CHARACTER], [TRACKER], hints, QUIT, OPTIONS,
//     every row reachable, and the highlight carried by row kind.
//
// cc -std=c99 -Wall -Wextra -Wno-unused-function -I . tools/test-tracker-tabs.c -o /tmp/test-tracker-tabs
// /tmp/test-tracker-tabs

#include <stdio.h>
#include <string.h>
#include "../ap/ap_tracker_tabs.h"
#include "../ap/ap_pauserow.h"

static int checks, failures;
#define EXPECT(expr, why) do { checks++; if (!(expr)) { printf("FAIL %s\n", why); failures++; } } while (0)

static void tabs(void)
{
	int phase, unlocks, locks, hit;
	/* RACERS appears exactly when racers are items, pads are racer-locked, or
	 * Hit Character is on; never on a seed without the character phase alone. */
	for (phase = 0; phase < 2; phase++) for (unlocks = 0; unlocks < 2; unlocks++)
	for (locks = 0; locks < 2; locks++) for (hit = 0; hit < 2; hit++) {
		int want = (phase && (unlocks || locks)) || hit;
		EXPECT(AP_TrackerRacersTabPure(phase, unlocks, locks, hit) == want, "RACERS shows only with something to show");
	}

	for (int racers = 0; racers < 2; racers++) for (int items = 0; items < 2; items++) {
		int shown = AP_TRACKER_TAB_HUBS + racers + items, tab = 0, seen[AP_TRACKER_TAB_COUNT] = {0}, i;
		/* Right from the first hub visits every shown tab once, then wraps. */
		for (i = 0; i < shown; i++) {
			EXPECT(AP_TrackerTabShownPure(tab, racers, items), "the cycle only lands on shown tabs");
			EXPECT(!seen[tab], "no tab is visited twice in one lap");
			seen[tab] = 1;
			tab = AP_TrackerTabStepPure(tab, 1, racers, items);
		}
		EXPECT(tab == 0, "one lap to the right comes back to the first hub");
		EXPECT(seen[AP_TRACKER_TAB_RACERS] == racers && seen[AP_TRACKER_TAB_ITEMS] == items,
			"RACERS and ITEMS are in the cycle exactly when shown");
		/* Left is the exact reverse of right. */
		for (i = 0; i < AP_TRACKER_TAB_COUNT; i++) if (AP_TrackerTabShownPure(i, racers, items)) {
			EXPECT(AP_TrackerTabStepPure(AP_TrackerTabStepPure(i, 1, racers, items), -1, racers, items) == i, "left undoes right");
		}
		/* Hub order is untouched: from a hub, right is the next hub. */
		for (i = 0; i < AP_TRACKER_TAB_HUBS - 1; i++)
			EXPECT(AP_TrackerTabStepPure(i, 1, racers, items) == i + 1, "the hubs keep their order");
		EXPECT(AP_TrackerTabStepPure(AP_TRACKER_TAB_HUBS - 1, 1, racers, items) ==
			(racers ? AP_TRACKER_TAB_RACERS : items ? AP_TRACKER_TAB_ITEMS : 0), "the extra tabs follow the last hub");
		EXPECT(AP_TrackerTabStepPure(0, -1, racers, items) ==
			(items ? AP_TRACKER_TAB_ITEMS : racers ? AP_TRACKER_TAB_RACERS : AP_TRACKER_TAB_HUBS - 1), "left from the first hub wraps to the last tab");
	}
	/* A tab that disappears while open lands on the last hub map. */
	EXPECT(AP_TrackerTabSettlePure(AP_TRACKER_TAB_RACERS, 3, 0, 1) == 3, "a vanished RACERS tab falls back to the hub");
	EXPECT(AP_TrackerTabSettlePure(AP_TRACKER_TAB_RACERS, 3, 1, 1) == AP_TRACKER_TAB_RACERS, "a shown tab stays");
	EXPECT(AP_TrackerTabSettlePure(AP_TRACKER_TAB_ITEMS, 9, 1, 0) == 0, "a bad hub falls back to the first hub");
	EXPECT(AP_TrackerTabStepPure(42, 1, 1, 1) == 1, "an out-of-range tab restarts from the first hub");
}

static void racers(void)
{
	int dense[AP_TRACKER_TAB_DENSE_PADS], cups[AP_TRACKER_TAB_CUP_PADS], i;
	unsigned char unlocked[AP_TRACKER_TAB_RACERS_COUNT] = {0}, hit[AP_TRACKER_TAB_RACERS_COUNT] = {0};
	AP_TrackerRacerRow rows[AP_TRACKER_TAB_RACERS_COUNT];
	for (i = 0; i < AP_TRACKER_TAB_DENSE_PADS; i++) dense[i] = -1;
	for (i = 0; i < AP_TRACKER_TAB_CUP_PADS; i++) cups[i] = -1;
	dense[3] = 1; dense[9] = 1; dense[27] = 15; dense[5] = 16; dense[6] = -7;
	cups[0] = 1; cups[4] = 0;
	unlocked[0] = 1; unlocked[15] = 7; hit[2] = 2; hit[3] = 1; hit[4] = 9;
	AP_TrackerRacerRowsPure(dense, cups, unlocked, hit, rows);
	EXPECT(rows[1].densePads == ((1u << 3) | (1u << 9)) && rows[1].cupPads == 1u, "a racer collects every pad locked to it");
	EXPECT(rows[15].densePads == (1u << 27) && !rows[15].cupPads, "the last dense pad maps to its racer");
	EXPECT(rows[0].cupPads == (1u << 4) && !rows[0].densePads, "a cup pad lock names its racer");
	for (i = 2; i < 15; i++) EXPECT(!rows[i].densePads && !rows[i].cupPads, "malformed or absent locks name no racer");
	EXPECT(rows[0].unlocked == 1 && rows[15].unlocked == 1 && rows[1].unlocked == 0, "unlocked is a clean boolean");
	EXPECT(rows[2].hit == 2 && rows[3].hit == 1 && rows[4].hit == 0 && rows[5].hit == 0, "hit keeps absent/pending/done and drops junk");
}

static void pause_rows(void)
{
	signed char retail[AP_PAUSEROW_KINDS];
	int retailCount = AP_PauseRow_Build(0, 0, retail), character, tracker;
	EXPECT(retailCount == 4 && retail[0] == AP_PAUSEROW_RESUME && retail[1] == AP_PAUSEROW_HINTS &&
		retail[2] == AP_PAUSEROW_QUIT && retail[3] == AP_PAUSEROW_OPTIONS, "no AP rows is the retail menu");
	for (character = 0; character < 2; character++) for (tracker = 0; tracker < 2; tracker++) {
		signed char kinds[AP_PAUSEROW_KINDS];
		int count = AP_PauseRow_Build(character, tracker, kinds), r, at = 0, trackerRow = -1;
		EXPECT(count == 4 + character + tracker, "each AP row adds one row");
		for (r = 0; r < count; r++) {
			EXPECT(AP_PauseRow_Up(AP_PauseRow_Down(r, count), count) == r, "up undoes down");
			EXPECT(AP_PauseRow_Down(r, count) != r, "no row traps the cursor");
			if (kinds[r] == AP_PAUSEROW_TRACKER) trackerRow = r;
		}
		for (r = 0; r < count; r++) at = AP_PauseRow_Down(at, count);
		EXPECT(at == 0, "walking down visits every row and wraps to RESUME");
		EXPECT(tracker ? trackerRow == 1 + character : trackerRow < 0,
			"TRACKER sits below SELECT CHARACTER, or below RESUME without it");
		for (r = 0; r < retailCount; r++) {
			int there = AP_PauseRow_Carry(retail, retailCount, r, kinds, count);
			EXPECT(kinds[there] == retail[r], "a retail highlight keeps its row in the AP set");
			EXPECT(AP_PauseRow_Carry(kinds, count, there, retail, retailCount) == r, "and comes back unchanged");
		}
		if (tracker) EXPECT(AP_PauseRow_Carry(kinds, count, trackerRow, retail, retailCount) == 0,
			"losing the TRACKER row lands on RESUME");
		EXPECT(AP_PauseRow_Carry(kinds, count, -1, retail, retailCount) == 0 &&
			AP_PauseRow_Carry(kinds, count, count, retail, retailCount) == 0, "out-of-range rows land on RESUME");
	}
	/* A mid-session change of AP rows (tracker appears, character row goes)
	 * keeps the player on the same kind of row. */
	{
		signed char five[AP_PAUSEROW_KINDS], six[AP_PAUSEROW_KINDS];
		int n5 = AP_PauseRow_Build(1, 0, five), n6 = AP_PauseRow_Build(1, 1, six);
		EXPECT(six[AP_PauseRow_Carry(five, n5, 3, six, n6)] == AP_PAUSEROW_QUIT, "QUIT stays QUIT when TRACKER appears");
		EXPECT(AP_PauseRow_Carry(six, n6, 1, five, n5) == 1, "SELECT CHARACTER stays selected");
	}
	EXPECT(AP_LNG_TRACKER > 0x1000 && AP_LNG_TRACKER < 0x8000, "the TRACKER label index is clear of retail strings and the lock bit");
}

int main(void)
{
	tabs(); racers(); pause_rows();
	printf("tracker tabs: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
