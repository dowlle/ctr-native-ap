// Out-of-engine assertions for the AP item-box location bookkeeping (issue
// #109). Compiles the REAL logic: ap/ap_box_map.h is freestanding by design and
// pulls in only ap/ap_locations.h, so this harness links nothing from the game
// and can run on any host, with no disc, no display and no seed.
//
//   cc -Wall -Wextra -DCTR_AP -o /tmp/test-box-map tools/test-box-map.c && /tmp/test-box-map
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// What it pins:
//   1. the engine-LevelID -> apworld-box-track derivation, including that the
//      18 values form a permutation of 0..17 (so no two tracks can share a
//      code block and no block can be skipped),
//   2. the frozen code block: 270 codes, 35014000-35014269, 15-stride, and
//      every code distinct,
//   3. slot assignment: positional, unconditional, and NOT shifted by boxes
//      that are absent from the seed or already checked,
//   4. the despawn bookkeeping: a checked box is not in the set, which is what
//      "gone for the rest of the seed, across reconnects" means in code,
//   5. the ceiling: placements past 15 on a track are dropped and COUNTED, so
//      the caller can log them instead of losing them silently.

#include <stdio.h>
#include <string.h> // memset

#include "../ap/ap_box_map.h"

static int g_failures = 0;

static void expect_int(int got, int want, const char *what)
{
	if (got != want)
	{
		printf("FAIL %-58s got %d want %d\n", what, got, want);
		g_failures++;
	}
	else
	{
		printf("ok   %-58s %d\n", what, got);
	}
}

static void expect_long(long got, long want, const char *what)
{
	if (got != want)
	{
		printf("FAIL %-58s got %ld want %ld\n", what, got, want);
		g_failures++;
	}
	else
	{
		printf("ok   %-58s %ld\n", what, got);
	}
}

// ── 1. the derivation ───────────────────────────────────────────────────────

static void test_track_derivation(void)
{
	int seen[AP_BOX_TRACK_COUNT];
	int level;

	printf("\n-- LevelID -> apworld box track (derived from the Sapphire block) --\n");
	memset(seen, 0, sizeof seen);

	for (level = 0; level < AP_BOX_TRACK_COUNT; level++)
	{
		int t = AP_BoxMap_ApTrack(level);

		if (t < 0 || t >= AP_BOX_TRACK_COUNT)
		{
			printf("FAIL level %d derived track %d, out of range\n", level, t);
			g_failures++;
			continue;
		}
		if (seen[t])
		{
			printf("FAIL level %d derived track %d, already claimed\n", level, t);
			g_failures++;
			continue;
		}
		seen[t] = 1;
	}

	for (level = 0; level < AP_BOX_TRACK_COUNT; level++)
	{
		if (!seen[level])
		{
			printf("FAIL apworld track %d was never derived from any LevelID\n", level);
			g_failures++;
		}
	}
	printf("ok   the 18 LevelIDs map onto the 18 apworld tracks, one to one\n");

	// Three spot values from the shipped table, so a silent re-index of the
	// Sapphire block cannot pass the permutation check alone. Engine LevelID 3
	// is CRASH_COVE and its Sapphire location is 35012000, the first of the
	// block; Slide Coliseum and Turbo Track are the two tracks the 08-10
	// addendum ruling added, and they are the block's last two.
	expect_int(AP_BoxMap_ApTrack(3), 0, "CRASH_COVE (LevelID 3) is apworld track 0");
	expect_int(AP_BoxMap_ApTrack(16), 16, "SLIDE_COLISEUM (LevelID 16) is apworld track 16");
	expect_int(AP_BoxMap_ApTrack(17), 17, "TURBO_TRACK (LevelID 17) is apworld track 17");

	// Everything that is not one of the 18 box tracks has no box block at all:
	// the arenas (18..24), the hubs and menus above them, and nonsense input.
	expect_int(AP_BoxMap_ApTrack(18), -1, "NITRO_COURT (arena) has no box track");
	expect_int(AP_BoxMap_ApTrack(24), -1, "LAB_BASEMENT (arena) has no box track");
	expect_int(AP_BoxMap_ApTrack(40), -1, "a hub/menu level has no box track");
	expect_int(AP_BoxMap_ApTrack(-1), -1, "a negative level has no box track");
}

// ── 2. the frozen code block ────────────────────────────────────────────────

static void test_code_block(void)
{
	int  level, slot;
	int  count = 0;
	long lo = 0, hi = 0;
	static char used[AP_BOX_LOCATION_COUNT];

	printf("\n-- the frozen item_boxes code block --\n");
	memset(used, 0, sizeof used);

	for (level = 0; level < AP_BOX_TRACK_COUNT; level++)
	{
		for (slot = 0; slot < AP_BOX_SLOTS_PER_TRACK; slot++)
		{
			long code = AP_BoxMap_Code(level, slot);
			long off;

			if (code < 0)
			{
				printf("FAIL level %d slot %d has no code\n", level, slot);
				g_failures++;
				continue;
			}

			off = code - AP_BOX_CODE_BASE;
			if (off < 0 || off >= AP_BOX_LOCATION_COUNT)
			{
				printf("FAIL level %d slot %d code %ld is outside the block\n", level, slot, code);
				g_failures++;
				continue;
			}
			if (used[off])
			{
				printf("FAIL code %ld minted twice\n", code);
				g_failures++;
				continue;
			}
			used[off] = 1;

			if (count == 0 || code < lo)
				lo = code;
			if (count == 0 || code > hi)
				hi = code;
			count++;
		}
	}

	expect_int(count, AP_BOX_LOCATION_COUNT, "distinct codes minted (18 x 15)");
	expect_long(lo, 35014000L, "lowest box code");
	expect_long(hi, 35014269L, "highest box code");

	// The stride the freeze note states, checked rather than assumed.
	expect_long(AP_BoxMap_Code(3, 0), 35014000L, "Crash Cove slot 0");
	expect_long(AP_BoxMap_Code(3, 14), 35014014L, "Crash Cove slot 14");
	expect_long(AP_BoxMap_Code(6, 0), 35014015L, "Roo's Tubes slot 0 is one stride on");
	expect_long(AP_BoxMap_Code(17, 14), 35014269L, "Turbo Track slot 14 is the last code");

	// Off the end of a track: no name exists, so no code may be invented.
	expect_long(AP_BoxMap_Code(3, 15), -1, "slot 15 is past the frozen ceiling");
	expect_long(AP_BoxMap_Code(3, -1), -1, "a negative slot has no code");
	expect_long(AP_BoxMap_Code(18, 0), -1, "an arena has no box codes");
}

// ── 3-5. the spawn set ──────────────────────────────────────────────────────

// A tiny stand-in for the seed and the server's checked set: the harness owns
// two code lists and the builder queries them exactly as the client queries
// ctr_cfg and ap_net.
struct FakeState
{
	long absent[8];
	int  numAbsent;
	long checked[8];
	int  numChecked;
};

static int fake_in_seed(long code, void *ctx)
{
	struct FakeState *s = (struct FakeState *)ctx;
	int               i;

	for (i = 0; i < s->numAbsent; i++)
	{
		if (s->absent[i] == code)
			return 0;
	}
	return 1;
}

static int fake_checked(long code, void *ctx)
{
	struct FakeState *s = (struct FakeState *)ctx;
	int               i;

	for (i = 0; i < s->numChecked; i++)
	{
		if (s->checked[i] == code)
			return 1;
	}
	return 0;
}

// Six placements on Crash Cove (LevelID 3) and two on Roo's Tubes, so every
// build also proves the level filter.
static const AP_BoxPlacement k_placements[] = {
	{3, 100, 0, 100, 0},   // Crash Cove slot 0
	{3, 200, 0, 200, 512}, // slot 1
	{6, 900, 0, 900, 0},   // Roo's Tubes slot 0
	{3, 300, 0, 300, 0},   // Crash Cove slot 2
	{3, 400, 0, 400, 0},   // slot 3
	{6, 950, 0, 950, 0},   // Roo's Tubes slot 1
	{3, 500, 0, 500, 0},   // Crash Cove slot 4
	{3, 600, 0, 600, 0},   // slot 5
};
#define K_PLACEMENT_COUNT ((int)(sizeof(k_placements) / sizeof(k_placements[0])))

static void test_set_plain(void)
{
	AP_BoxSlot       out[AP_BOX_SLOTS_PER_TRACK];
	struct FakeState st;
	int              n, over = -1;

	printf("\n-- the spawn set: nothing absent, nothing checked --\n");
	memset(&st, 0, sizeof st);

	n = AP_BoxMap_BuildSet(k_placements, K_PLACEMENT_COUNT, 3,
	                       fake_in_seed, fake_checked, &st,
	                       out, AP_BOX_SLOTS_PER_TRACK, &over);

	expect_int(n, 6, "Crash Cove has 6 boxes standing");
	expect_int(over, 0, "nothing overflowed");
	expect_int(out[0].slot, 0, "first entry is slot 0");
	expect_long(out[0].code, 35014000L, "first entry's code");
	expect_int(out[0].x, 100, "first entry kept its x");
	expect_int(out[1].rotY, 512, "second entry kept its rot_y");
	expect_int(out[5].slot, 5, "last entry is slot 5");
	expect_long(out[5].code, 35014005L, "last entry's code");

	// The level filter: Roo's Tubes placements are interleaved above and must
	// neither appear here nor consume a Crash Cove slot.
	n = AP_BoxMap_BuildSet(k_placements, K_PLACEMENT_COUNT, 6,
	                       fake_in_seed, fake_checked, &st,
	                       out, AP_BOX_SLOTS_PER_TRACK, &over);
	expect_int(n, 2, "Roo's Tubes has 2 boxes standing");
	expect_long(out[0].code, 35014015L, "Roo's Tubes slot 0 code");
	expect_long(out[1].code, 35014016L, "Roo's Tubes slot 1 code");
}

static void test_set_absent_does_not_shift(void)
{
	AP_BoxSlot       out[AP_BOX_SLOTS_PER_TRACK];
	struct FakeState st;
	int              n, over = -1;

	printf("\n-- a box missing from the seed must NOT re-point the ones after it --\n");
	memset(&st, 0, sizeof st);
	st.absent[st.numAbsent++] = 35014001L; // Crash Cove slot 1

	n = AP_BoxMap_BuildSet(k_placements, K_PLACEMENT_COUNT, 3,
	                       fake_in_seed, fake_checked, &st,
	                       out, AP_BOX_SLOTS_PER_TRACK, &over);

	expect_int(n, 5, "five boxes stand, not six");
	expect_int(out[0].slot, 0, "slot 0 unchanged");
	expect_int(out[1].slot, 2, "the entry after the hole is still slot 2");
	expect_long(out[1].code, 35014002L, "and still carries slot 2's code");
	expect_int(out[1].x, 300, "and still carries slot 2's coordinates");
	expect_int(out[4].slot, 5, "the last one is still slot 5");
}

static void test_set_checked_is_gone(void)
{
	AP_BoxSlot       out[AP_BOX_SLOTS_PER_TRACK];
	struct FakeState st;
	int              n, over = -1;
	int              i;

	printf("\n-- a checked box is gone, and stays gone on a rebuild --\n");
	memset(&st, 0, sizeof st);
	st.checked[st.numChecked++] = 35014000L; // slot 0, broken earlier this seed
	st.checked[st.numChecked++] = 35014005L; // slot 5, broken by an earlier session

	n = AP_BoxMap_BuildSet(k_placements, K_PLACEMENT_COUNT, 3,
	                       fake_in_seed, fake_checked, &st,
	                       out, AP_BOX_SLOTS_PER_TRACK, &over);

	expect_int(n, 4, "four boxes stand after two were checked");
	for (i = 0; i < n; i++)
	{
		if (out[i].code == 35014000L || out[i].code == 35014005L)
		{
			printf("FAIL a checked box came back at slot %d\n", out[i].slot);
			g_failures++;
		}
	}
	expect_int(out[0].slot, 1, "the set now starts at slot 1");
	expect_long(out[3].code, 35014004L, "and ends at slot 4's code");

	// The rebuild-on-reconnect path is the same call with the same checked set,
	// which is the whole point of keying it off server truth: it is idempotent.
	{
		AP_BoxSlot again[AP_BOX_SLOTS_PER_TRACK];
		int        n2 = AP_BoxMap_BuildSet(k_placements, K_PLACEMENT_COUNT, 3,
		                                   fake_in_seed, fake_checked, &st,
		                                   again, AP_BOX_SLOTS_PER_TRACK, &over);
		int same = (n2 == n);

		// Field by field, NOT memcmp: AP_BoxSlot carries alignment padding whose
		// bytes are whatever was on the stack, so a memcmp here compares garbage
		// and fails on an identical set. (It did, the first time this ran.)
		for (i = 0; same && i < n; i++)
		{
			same = again[i].slot == out[i].slot && again[i].code == out[i].code &&
			       again[i].x == out[i].x && again[i].y == out[i].y &&
			       again[i].z == out[i].z && again[i].rotY == out[i].rotY;
		}

		expect_int(n2, n, "a rebuild yields the same count");
		if (!same)
		{
			printf("FAIL a rebuild yielded a different set\n");
			g_failures++;
		}
		else
		{
			printf("ok   a rebuild yields the identical set\n");
		}
	}
}

static void test_set_ceiling(void)
{
	AP_BoxPlacement many[20];
	AP_BoxSlot      out[AP_BOX_SLOTS_PER_TRACK];
	int             i, n, over = -1;

	printf("\n-- placements past the frozen 15-slot ceiling are dropped and counted --\n");
	for (i = 0; i < 20; i++)
	{
		many[i].level = 3;
		many[i].x = (short)i;
		many[i].y = 0;
		many[i].z = 0;
		many[i].rotY = 0;
	}

	n = AP_BoxMap_BuildSet(many, 20, 3, 0, 0, 0, out, AP_BOX_SLOTS_PER_TRACK, &over);

	expect_int(n, AP_BOX_SLOTS_PER_TRACK, "exactly the ceiling spawns");
	expect_int(over, 5, "the five extras are reported, not swallowed");
	expect_int(out[14].slot, 14, "the last spawned box is slot 14");
	expect_long(out[14].code, 35014014L, "and carries the block's last Crash Cove code");

	// NULL predicates read as "present, not checked", which is what the plain
	// call above relies on.
	expect_int(out[0].slot, 0, "NULL predicates still build a full set");
}

int main(void)
{
	printf("AP item box map + spawn-set bookkeeping (#109)\n");

	test_track_derivation();
	test_code_block();
	test_set_plain();
	test_set_absent_does_not_shift();
	test_set_checked_is_gone();
	test_set_ceiling();

	printf("\n%s (%d failure%s)\n",
	       g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
	return g_failures == 0 ? 0 : 1;
}
