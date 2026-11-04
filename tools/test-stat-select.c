// cc -std=c11 -Wall -Wextra -Werror -o /tmp/test-stat-select tools/test-stat-select.c
//
// Progressive Stats / Progressive Boost rank selector (ruling 2026-09-11,
// extended to Boost 2026-10-26), driven through ap/ap_stat_select_logic.h, the
// header ap_capability.c and the hub picker run.
//
// The cases follow the ruling clause by clause: the range is the bottom of the
// chain up to the received ceiling and never above it; a selection at the old
// ceiling follows an upgrade; a lowered selection stays; per stat and, in
// per-character mode, per racer; Boost is a selectable chain like the others.
// The reconnect replay case is the reason the representation is
// follow-or-explicit with a clamp on read rather than a receipt-time rule.

#include <stdio.h>

#include "../ap/ap_stat_select_logic.h"

// Chain and tier numbering as in ap/ap_capability.h, named here so the harness
// does not drag the game headers in (ap_capability.c pins the shared values).
#define CHAIN_BOOST 0
#define CHAIN_SPEED 1
#define CHAIN_ACCEL 2
#define CHAIN_TURN 3
#define TIER_NONE 0
#define TIER_BOOST 1
#define TIER_USF 2
#define TIER_BLUE 3
#define VERY_LOW 0
#define LOW 1
#define MEDIUM 2
#define HIGH 3
#define VERY_HIGH 4

static int failures;

static void expect(int actual, int wanted, const char *name)
{
	printf("%s  %-58s %d\n", actual == wanted ? "ok  " : "FAIL", name, actual);
	if (actual != wanted)
	{
		printf("      wanted %d\n", wanted);
		failures++;
	}
}

// Press LEFT/RIGHT `n` times.
static int press(int sel, int ceiling, int dir, int n)
{
	while (n-- > 0)
		sel = AP_StatSel_Step(sel, ceiling, dir);
	return sel;
}

int main(void)
{
	int sel;
	int c;

	// ── Default: follow the ceiling ──
	expect(AP_StatSel_Applied(AP_SS_FOLLOW, MEDIUM), MEDIUM, "default selection drives the ceiling");
	expect(AP_StatSel_Applied(AP_SS_FOLLOW, VERY_LOW), VERY_LOW, "default at zero items drives VERY LOW");
	expect(AP_StatSel_IsLowered(AP_SS_FOLLOW, HIGH), 0, "default is not lowered");

	// ── Range: bottom of the chain up to the ceiling, never above ──
	sel = press(AP_SS_FOLLOW, HIGH, -1, 10);
	expect(sel, VERY_LOW, "LEFT stops at VERY LOW");
	expect(AP_StatSel_Applied(sel, HIGH), VERY_LOW, "and the kart drives VERY LOW");
	sel = press(sel, HIGH, +1, 10);
	expect(sel, AP_SS_FOLLOW, "RIGHT back to the ceiling stores follow");
	expect(AP_StatSel_Applied(sel, HIGH), HIGH, "RIGHT never passes the ceiling");
	expect(AP_StatSel_Step(AP_SS_FOLLOW, HIGH, +1), AP_SS_FOLLOW, "RIGHT at the ceiling is a no-op");
	expect(AP_StatSel_Step(AP_SS_FOLLOW, VERY_LOW, -1), AP_SS_FOLLOW, "zero items: nothing to lower");
	{
		int outside = 0;
		for (c = VERY_LOW; c <= VERY_HIGH; c++)
		{
			int v;
			for (v = AP_SS_FOLLOW; v <= AP_SS_VALUE_MAX; v++)
			{
				int a = AP_StatSel_Applied(v, c);
				if (a < 0 || a > c)
				{
					printf("      applied %d outside 0..%d for stored %d\n", a, c, v);
					outside++;
				}
			}
		}
		expect(outside, 0, "every stored value x ceiling lands inside 0..ceiling");
	}

	// ── Ceiling clamp: a stored value above the ceiling drives the ceiling ──
	expect(AP_StatSel_Applied(VERY_HIGH, LOW), LOW, "explicit above the ceiling clamps to it");
	expect(AP_StatSel_IsLowered(VERY_HIGH, LOW), 0, "and is not reported as lowered");

	// ── Upgrade with the selection at the old ceiling: it follows ──
	sel = AP_SS_FOLLOW;
	expect(AP_StatSel_Applied(sel, LOW), LOW, "at the ceiling with LOW unlocked");
	expect(AP_StatSel_Applied(sel, MEDIUM), MEDIUM, "an upgrade carries it to MEDIUM");
	sel = press(press(AP_SS_FOLLOW, MEDIUM, -1, 1), MEDIUM, +1, 1);
	expect(AP_StatSel_Applied(sel, HIGH), HIGH, "lowered then raised back to the ceiling follows too");

	// ── Upgrade with a deliberately lowered selection: it stays ──
	sel = press(AP_SS_FOLLOW, HIGH, -1, 2); // HIGH -> LOW
	expect(AP_StatSel_Applied(sel, HIGH), LOW, "lowered from HIGH to LOW");
	expect(AP_StatSel_IsLowered(sel, HIGH), 1, "reported as lowered");
	expect(AP_StatSel_Applied(sel, VERY_HIGH), LOW, "an upgrade leaves the lowered rank alone");
	expect(AP_StatSel_IsLowered(sel, VERY_HIGH), 1, "still lowered, under the new ceiling");
	sel = press(sel, VERY_HIGH, +1, 2);
	expect(AP_StatSel_Applied(sel, VERY_HIGH), HIGH, "RIGHT twice from LOW is HIGH, still below");
	expect(sel, HIGH, "stored explicitly");
	sel = press(sel, VERY_HIGH, +1, 1);
	expect(sel, AP_SS_FOLLOW, "one more reaches the new ceiling and follows again");

	// ── Reconnect replay: the ceiling is rebuilt from zero ──
	// A rule applied at receipt time would see the lowered LOW equal the
	// half-replayed ceiling and drag it up. Clamp on read cannot.
	sel = press(AP_SS_FOLLOW, VERY_HIGH, -1, 3); // VERY HIGH -> LOW
	for (c = VERY_LOW; c <= VERY_HIGH; c++)
		(void)AP_StatSel_Applied(sel, c); // the replay climbs 0..4
	expect(sel, LOW, "replay does not rewrite the stored selection");
	expect(AP_StatSel_Applied(sel, VERY_LOW), VERY_LOW, "mid-replay it drives the lower ceiling");
	expect(AP_StatSel_Applied(sel, VERY_HIGH), LOW, "after the replay it drives LOW again");

	// ── Boost is a chain like the stats ──
	sel = press(AP_SS_FOLLOW, TIER_BLUE, -1, 1);
	expect(AP_StatSel_Applied(sel, TIER_BLUE), TIER_USF, "Blue Fire lowered to USF");
	sel = press(sel, TIER_BLUE, -1, 1);
	expect(AP_StatSel_Applied(sel, TIER_BLUE), TIER_BOOST, "then to BOOST");
	sel = press(sel, TIER_BLUE, -1, 5);
	expect(AP_StatSel_Applied(sel, TIER_BLUE), TIER_NONE, "floor is tier NONE (pads only)");
	sel = press(AP_SS_FOLLOW, TIER_USF, -1, 1);
	expect(AP_StatSel_Applied(sel, TIER_BLUE), TIER_BOOST, "lowered BOOST stays when Blue Fire arrives");
	expect(AP_StatSel_Applied(AP_SS_FOLLOW, TIER_USF), TIER_USF, "unlowered boost follows to USF");

	// ── Pack off: everything passes the fail-open -1 through ──
	expect(AP_StatSel_Applied(LOW, -1), -1, "pack off: applied is -1");
	expect(AP_StatSel_Step(LOW, -1, -1), LOW, "pack off: a press changes nothing");
	expect(AP_StatSel_IsLowered(LOW, -1), 0, "pack off: never lowered");

	// ── Rows: shared row vs one per racer ──
	expect(AP_StatSel_Row(AP_SS_MODE_SHARED, 7), 0, "shared_global: every racer uses row 0");
	expect(AP_StatSel_Row(AP_SS_MODE_SHARED, -1), 0, "shared_global: even an unknown racer");
	expect(AP_StatSel_Row(AP_SS_MODE_PER_CHARACTER, 0), 1, "per_character: roster slot 0 is row 1");
	expect(AP_StatSel_Row(AP_SS_MODE_PER_CHARACTER, 15), 16, "per_character: roster slot 15 is row 16");
	expect(AP_StatSel_Row(AP_SS_MODE_PER_CHARACTER, -1), -1, "per_character: unknown racer has no row");
	expect(AP_StatSel_Row(AP_SS_MODE_PER_CHARACTER, 16), -1, "per_character: slot 16 is out of range");
	expect(AP_StatSel_Row(0, 3), -1, "pack off: no row");
	expect(AP_StatSel_Row(3, 3), -1, "unknown mode: no row");

	// Per stat and per racer: a table indexed the way ap_capability.c does it.
	{
		int table[AP_SS_WIRE_COUNT];
		int i;
		int a, b;
		for (i = 0; i < AP_SS_WIRE_COUNT; i++)
			table[i] = AP_SS_FOLLOW;
		a = AP_StatSel_Index(AP_StatSel_Row(AP_SS_MODE_PER_CHARACTER, 2), CHAIN_SPEED);
		b = AP_StatSel_Index(AP_StatSel_Row(AP_SS_MODE_PER_CHARACTER, 3), CHAIN_SPEED);
		table[a] = AP_StatSel_Step(table[a], HIGH, -1);
		expect(AP_StatSel_Applied(table[a], HIGH), MEDIUM, "racer A speed lowered");
		expect(AP_StatSel_Applied(table[b], HIGH), HIGH, "racer B speed untouched");
		expect(AP_StatSel_Applied(table[AP_StatSel_Index(3, CHAIN_ACCEL)], HIGH), HIGH,
		       "racer A acceleration untouched");
		expect(AP_StatSel_Applied(table[AP_StatSel_Index(3, CHAIN_BOOST)], TIER_BLUE), TIER_BLUE,
		       "racer A boost untouched");
		expect(AP_StatSel_Applied(table[AP_StatSel_Index(0, CHAIN_SPEED)], HIGH), HIGH,
		       "shared row untouched by a per-racer change");

		// Every (row, chain) has its own distinct slot inside the wire array.
		{
			int seen[AP_SS_WIRE_COUNT] = {0};
			int r, ch, bad = 0;
			for (r = 0; r < AP_SS_ROWS; r++)
				for (ch = 0; ch < AP_SS_CHAINS; ch++)
				{
					int idx = AP_StatSel_Index(r, ch);
					if (idx < 0 || idx >= AP_SS_WIRE_COUNT || seen[idx]++)
						bad++;
				}
			expect(bad, 0, "row x chain maps one-to-one onto the 68-value wire");
		}
		expect(AP_StatSel_Index(AP_SS_ROWS, 0), -1, "row out of range has no slot");
		expect(AP_StatSel_Index(0, AP_SS_CHAINS), -1, "chain out of range has no slot");
		expect(AP_StatSel_Index(-1, 0), -1, "no row, no slot");
	}

	// ── Stored package bounds (untrusted data storage) ──
	expect(AP_SS_WIRE_COUNT, 68, "wire length: 17 rows x 4 chains");
	expect(AP_StatSel_ValueOk(-1), 1, "-1 (follow) is a legal value");
	expect(AP_StatSel_ValueOk(4), 1, "4 (VERY HIGH) is a legal value");
	expect(AP_StatSel_ValueOk(-2), 0, "-2 is rejected");
	expect(AP_StatSel_ValueOk(5), 0, "5 is rejected");
	expect(AP_StatSel_ValueOk(4294967300LL), 0, "a value that would narrow to 4 is rejected");

	// ── Picker panel rows: 0 BOOST, 1 SPEED, 2 ACCEL, 3 TURN ──
	expect(AP_StatSel_FirstRow(0xF), 0, "all rows live: start on BOOST");
	expect(AP_StatSel_FirstRow(0xE), 1, "no boost pack: start on SPEED");
	expect(AP_StatSel_FirstRow(0x1), 0, "boost only: start on BOOST");
	expect(AP_StatSel_FirstRow(0x0), -1, "nothing editable: no edit focus");
	expect(AP_StatSel_NextRow(3, +1, 0xF), 0, "DOWN from TURN wraps to BOOST");
	expect(AP_StatSel_NextRow(0, -1, 0xF), 3, "UP from BOOST wraps to TURN");
	expect(AP_StatSel_NextRow(3, +1, 0xE), 1, "DOWN from TURN skips a missing BOOST row");
	expect(AP_StatSel_NextRow(0, +1, 0x1), 0, "boost only: DOWN stays on BOOST");
	expect(AP_StatSel_NextRow(1, +1, 0x0), -1, "nothing editable: no row");
	expect(AP_StatSel_ValidRow(0, 0xE), 1, "a BOOST focus with no boost pack moves to SPEED");
	expect(AP_StatSel_ValidRow(2, 0xF), 2, "a live focus stays put");
	{
		int r, ok = 1;
		for (r = 0; r < AP_SS_PANEL_ROWS; r++)
			if (AP_StatSel_NextRow(AP_StatSel_NextRow(r, +1, 0xF), -1, 0xF) != r)
				ok = 0;
		expect(ok, 1, "UP undoes DOWN on every row");
	}

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
