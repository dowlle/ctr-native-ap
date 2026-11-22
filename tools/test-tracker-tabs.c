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
#include "../ap/ap_wumpa_shop_menu.h"

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
	int retailCount = AP_PauseRow_Build(0, 0, 0, 0, retail);
	EXPECT(retailCount == 4 && retail[0] == AP_PAUSEROW_RESUME && retail[1] == AP_PAUSEROW_HINTS &&
		retail[2] == AP_PAUSEROW_QUIT && retail[3] == AP_PAUSEROW_OPTIONS, "retail order");
	for (int flags = 0; flags < 8; ++flags) {
		int character = !!(flags & 1), tracker = !!(flags & 2), travel = !!(flags & 4);
		signed char kinds[AP_PAUSEROW_KINDS];
		int count = AP_PauseRow_Build(1, character, tracker, travel, kinds);
		int at = 0;
		EXPECT(count == 3 + character + tracker + travel, "only available rows appear");
		EXPECT(kinds[at++] == AP_PAUSEROW_RESUME, "RESUME first");
		if (tracker) EXPECT(kinds[at++] == AP_PAUSEROW_TRACKER, "TRACKER follows RESUME");
		if (travel) EXPECT(kinds[at++] == AP_PAUSEROW_TRAVEL, "TRAVEL follows TRACKER");
		if (character) EXPECT(kinds[at++] == AP_PAUSEROW_CHARACTER, "SELECT CHARACTER follows TRAVEL");
		EXPECT(kinds[at++] == AP_PAUSEROW_OPTIONS && kinds[at++] == AP_PAUSEROW_QUIT, "OPTIONS then QUIT");
		for (int r = 0; r < count; ++r) {
			EXPECT(AP_PauseRow_Up(AP_PauseRow_Down(r, count), count) == r, "up undoes down");
			EXPECT(AP_PauseRow_Carry(kinds, count, r, retail, retailCount) ==
			       (kinds[r] == AP_PAUSEROW_OPTIONS ? 3 : kinds[r] == AP_PAUSEROW_QUIT ? 2 : 0),
			       "retain the same action or fall back to RESUME");
		}
	}
	EXPECT(AP_LNG_TRACKER > 0x1000 && AP_LNG_TRACKER < 0x8000, "the TRACKER label index is clear of retail strings and the lock bit");
	EXPECT(AP_LNG_PLAY_SOLO > 0x1000 && AP_LNG_PLAY_SOLO < 0x8000 && AP_LNG_PLAY_SOLO != AP_LNG_TRACKER,
	       "the PLAY SOLO label index (#89) is clear of retail strings, the lock bit and TRACKER");
	EXPECT(AP_LNG_TRAVEL > 0x1000 && AP_LNG_TRAVEL < 0x8000 &&
	       AP_LNG_TRAVEL != AP_LNG_TRACKER && AP_LNG_TRAVEL != AP_LNG_PLAY_SOLO && AP_LNG_TRAVEL != AP_LNG_WUMPA_SHOP,
	       "the TRAVEL label index (A3-16) is clear of retail strings, the lock bit and the other AP rows");
}

int main(void)
{
	tabs(); racers(); pause_rows();
	printf("tracker tabs: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
