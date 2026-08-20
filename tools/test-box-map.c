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
//      the caller can log them instead of losing them silently,
//   6. the COMPILED-IN placement table (#109 packaging): its count, its per-track
//      counts, and that every row lands on a real box track inside the ceiling,
//   7. PRECEDENCE: an external placement file overrides the compiled-in default
//      by EXISTENCE, not by content, and the override is wholesale,
//   8. the §7 spawn rule: a slot stands exactly the box locations its own seed
//      created, so a reduced seating tier stands fewer boxes and no more,
//   9. the §6 pad predicate: how many boxes are still standing behind a track,
//  10. the scout-list filter: only codes this world created may go on the wire,
//      which is the precondition a peer-bound box's feed line depends on,
//  11. the whole Warp-Pad State Model v2 table (ap/ap_pad_state.h), including
//      the cell issue #232 turned on: a trophy-checked, stage-2-locked pad with
//      boxes still standing stays Raceable, so the entry gate must let the
//      player back in for them.

#include <stdio.h>
#include <string.h> // memset

#include "../ap/ap_box_map.h"
#include "../ap/ap_placement_table.h"
#include "../ap/ap_pad_state.h"

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

// ── 6. the compiled-in table ────────────────────────────────────────────────

// The per-track counts the provenance block in ap_placements_data.h states.
// Asserted rather than trusted: a regenerated table that quietly shifts a count
// re-points every later "Item Box N" name on that track, and the header's own
// documentation is the only thing that says what the FINAL set was.
static const int k_perTrack[AP_BOX_TRACK_COUNT] = {
	15, 13, 14, 10, 10, 12, 11, 15, 15, 13, 14, 15, 15, 14, 10, 15, 15, 15,
};

static void test_embedded_table(void)
{
	int counts[AP_BOX_TRACK_COUNT];
	int i, total = 0;

	printf("\n-- the COMPILED-IN default placement table (packaging) --\n");
	memset(counts, 0, sizeof counts);

	expect_int(AP_EMBEDDED_PLACEMENT_COUNT, 241, "the embedded table holds 241 placements");
	expect_int(AP_EMBEDDED_PLACEMENT_COUNT, AP_EMBEDDED_PLACEMENT_EXPECTED,
	           "count matches the header's own assertion");

	for (i = 0; i < AP_EMBEDDED_PLACEMENT_COUNT; i++)
	{
		int level = (int)AP_EMBEDDED_PLACEMENTS[i].level;

		if (AP_BoxMap_ApTrack(level) < 0)
		{
			printf("FAIL row %d sits on level %d, which is not a box track\n", i, level);
			g_failures++;
			continue;
		}
		counts[level]++;
	}

	for (i = 0; i < AP_BOX_TRACK_COUNT; i++)
	{
		total += counts[i];

		if (counts[i] > AP_BOX_SLOTS_PER_TRACK)
		{
			// A row past the ceiling has no frozen name, so it can never spawn and
			// can never send. Shipping one is a packaging bug, not a runtime one.
			printf("FAIL level %d holds %d placements, past the frozen ceiling of %d\n",
			       i, counts[i], AP_BOX_SLOTS_PER_TRACK);
			g_failures++;
		}
		if (counts[i] != k_perTrack[i])
		{
			printf("FAIL level %d holds %d placements, the provenance block says %d\n",
			       i, counts[i], k_perTrack[i]);
			g_failures++;
		}
	}
	expect_int(total, AP_EMBEDDED_PLACEMENT_COUNT, "every row landed on a box track");
	printf("ok   per-track counts match the provenance block, none past the ceiling\n");

	// Row order is load-bearing (slot assignment is positional), so the first and
	// last rows are pinned: a re-sorted regeneration would still pass every count
	// above while re-pointing names wholesale.
	expect_int((int)AP_EMBEDDED_PLACEMENTS[0].level, 3, "the table opens on CRASH_COVE");
	expect_int((int)AP_EMBEDDED_PLACEMENTS[0].x, -9378, "and on that row's x");
	// The FINAL file appends four late placements AFTER Turbo Track, so the table
	// does not end on level 17 and must not be "tidied" into level order: row 240
	// is a Mystery Caves box and sorting it home would move it to a different slot.
	expect_int((int)AP_EMBEDDED_PLACEMENTS[AP_EMBEDDED_PLACEMENT_COUNT - 1].level, 9,
	           "the table ends on a late-appended MYSTERY_CAVES row, not on level 17");
}

// ── 7. precedence: which table is live ──────────────────────────────────────

static void test_precedence(void)
{
	// A stand-in for the external file's rows, deliberately unlike the shipped
	// set so "which table answered" is never ambiguous.
	static const AP_PlacementRow k_file[] = {
		{3, 11, 12, 13, 14},
		{6, 21, 22, 23, 24},
		{17, 31, 32, 33, 34},
	};
	AP_PlacementTable t;
	int               level = -1;
	short             x = 0, y = 0, z = 0, rot = 0;

	printf("\n-- precedence: external file vs the compiled-in default --\n");

	// No file: the embedded default is the table.
	t.source = AP_PLACEMENT_SRC_EMBEDDED;
	t.file = k_file; // present but must be ignored entirely
	t.fileCount = 3;
	expect_int(AP_PlacementTable_Count(&t), AP_EMBEDDED_PLACEMENT_COUNT,
	           "no file -> the built-in default is live");
	expect_int(AP_PlacementTable_Get(&t, 0, &level, &x, 0, 0, 0), 1, "row 0 reads back");
	expect_int(level, 3, "and it is the embedded row, not the file's");
	expect_int((int)x, -9378, "with the embedded coordinates");

	// A file: it wins, wholesale.
	t.source = AP_PLACEMENT_SRC_FILE;
	expect_int(AP_PlacementTable_Count(&t), 3, "a file present -> the file is live");
	expect_int(AP_PlacementTable_Get(&t, 0, &level, &x, &y, &z, &rot), 1, "row 0 reads back");
	expect_int(level, 3, "the file's row 0 level");
	expect_int((int)x, 11, "the file's row 0 x");
	expect_int((int)rot, 14, "the file's row 0 rot_y");
	expect_int(AP_PlacementTable_Get(&t, 2, &level, 0, 0, 0, 0), 1, "the file's last row reads back");
	expect_int(level, 17, "and it is TURBO_TRACK");

	// THE RULE: existence, not content. An empty file is a legitimate "no boxes"
	// instruction and must NOT resurrect the 241 compiled-in rows behind the
	// operator's back.
	t.fileCount = 0;
	expect_int(AP_PlacementTable_Count(&t), 0,
	           "an EMPTY file still wins -- precedence is by existence");
	expect_int(AP_PlacementTable_Get(&t, 0, 0, 0, 0, 0, 0), 0, "and it hands out nothing");

	// Out of range on either source writes nothing and says so.
	t.fileCount = 3;
	level = 999;
	expect_int(AP_PlacementTable_Get(&t, 3, &level, 0, 0, 0, 0), 0, "one past the file's end");
	expect_int(level, 999, "and the out pointer was left untouched");
	expect_int(AP_PlacementTable_Get(&t, -1, 0, 0, 0, 0, 0), 0, "a negative index");
	t.source = AP_PLACEMENT_SRC_EMBEDDED;
	expect_int(AP_PlacementTable_Get(&t, AP_EMBEDDED_PLACEMENT_COUNT, 0, 0, 0, 0, 0), 0,
	           "one past the embedded table's end");
}

// ── 8. §7: a slot stands exactly its own seed's box locations ───────────────

// The §7 predicate in the harness's terms: a code is live only if this world
// created it. FakeState above models absence; this models the opposite framing
// the ruling uses, so the test reads the way the rule does.
struct FakeWorld
{
	long created[32];
	int  numCreated;
	long checked[8];
	int  numChecked;
};

static int fake_created(long code, void *ctx)
{
	struct FakeWorld *w = (struct FakeWorld *)ctx;
	int               i;

	for (i = 0; i < w->numCreated; i++)
	{
		if (w->created[i] == code)
			return 1;
	}
	return 0;
}

static int fake_world_checked(long code, void *ctx)
{
	struct FakeWorld *w = (struct FakeWorld *)ctx;
	int               i;

	for (i = 0; i < w->numChecked; i++)
	{
		if (w->checked[i] == code)
			return 1;
	}
	return 0;
}

static void test_reduced_seating_tier(void)
{
	AP_BoxSlot       out[AP_BOX_SLOTS_PER_TRACK];
	struct FakeWorld w;
	int              n, over = -1;

	printf("\n-- §7: the seed decides liveness, the placement set is geometry only --\n");
	memset(&w, 0, sizeof w);

	// A reduced seating tier: this slot's world created only three of Crash Cove's
	// six placed boxes. The geometry still holds six.
	w.created[w.numCreated++] = 35014000L; // slot 0
	w.created[w.numCreated++] = 35014002L; // slot 2
	w.created[w.numCreated++] = 35014005L; // slot 5

	n = AP_BoxMap_BuildSet(k_placements, K_PLACEMENT_COUNT, 3,
	                       fake_created, fake_world_checked, &w,
	                       out, AP_BOX_SLOTS_PER_TRACK, &over);

	expect_int(n, 3, "exactly the created locations stand, not the six placed");
	expect_int(out[0].slot, 0, "slot 0 stands");
	expect_int(out[1].slot, 2, "slot 2 stands, and kept its own slot");
	expect_int(out[2].slot, 5, "slot 5 stands");
	expect_long(out[1].code, 35014002L, "the middle one still carries slot 2's code");
	expect_int(out[1].x, 300, "and slot 2's coordinates");

	// The dev fallback is retired: a slot whose seed created NO box locations
	// stands nothing at all, rather than showing the authored set and sending
	// location ids that do not exist in that world.
	w.numCreated = 0;
	n = AP_BoxMap_BuildSet(k_placements, K_PLACEMENT_COUNT, 3,
	                       fake_created, fake_world_checked, &w,
	                       out, AP_BOX_SLOTS_PER_TRACK, &over);
	expect_int(n, 0, "a seed with NO box locations stands ZERO boxes");
}

// ── 9. §6: what is still standing behind a pad ──────────────────────────────

static void test_pad_boxes_left(void)
{
	struct FakeWorld w;

	printf("\n-- §6: the pad's uncollected-box count --\n");
	memset(&w, 0, sizeof w);

	// Crash Cove: four box locations created, geometry for six.
	w.created[w.numCreated++] = 35014000L;
	w.created[w.numCreated++] = 35014001L;
	w.created[w.numCreated++] = 35014002L;
	w.created[w.numCreated++] = 35014003L;

	expect_int(AP_BoxMap_CountStanding(3, 6, fake_created, fake_world_checked, &w), 4,
	           "four live boxes are still standing");

	// THE GATE CASE (ruled §6): the trophy is checked and boxes remain. The count
	// must stay above zero, which is what stops AP_PadState returning Done (5) or
	// Re-locked (3) and stranding them -- the trophy's own state never enters here.
	w.checked[w.numChecked++] = 35014000L;
	w.checked[w.numChecked++] = 35014001L;
	expect_int(AP_BoxMap_CountStanding(3, 6, fake_created, fake_world_checked, &w), 2,
	           "two broken, two still standing -> the pad may NOT lock");

	w.checked[w.numChecked++] = 35014002L;
	w.checked[w.numChecked++] = 35014003L;
	expect_int(AP_BoxMap_CountStanding(3, 6, fake_created, fake_world_checked, &w), 0,
	           "all four broken -> nothing is stranded, the pad may lock");

	// Geometry bounds the count, so a seed that created more box locations than
	// the placement set covers cannot keep a pad green with nothing to break.
	w.numChecked = 0;
	expect_int(AP_BoxMap_CountStanding(3, 2, fake_created, fake_world_checked, &w), 2,
	           "only two placements here -> only two can stand");
	expect_int(AP_BoxMap_CountStanding(3, 0, fake_created, fake_world_checked, &w), 0,
	           "no placements here -> nothing stands");
	expect_int(AP_BoxMap_CountStanding(3, 99, fake_created, fake_world_checked, &w), 4,
	           "a slot count past the ceiling is clamped, not trusted");

	// Not a box track at all: an arena pad has no boxes behind it by construction.
	expect_int(AP_BoxMap_CountStanding(18, 15, fake_created, fake_world_checked, &w), 0,
	           "an arena has no boxes behind its pad");
}

// ── 10. the scout list: what may go on the wire ─────────────────────────────

static void test_scout_codes(void)
{
	struct FakeWorld w;
	long             out[AP_BOX_LOCATION_COUNT];
	int              n;

	printf("\n-- the connect scout list (the peer-bound feed line's precondition) --\n");
	memset(&w, 0, sizeof w);

	// A seed WITHOUT the box class contributes nothing. This is the safety half:
	// MultiServer 0.6.7 hard-drops the connection on an invalid id in the scout
	// path, so a blind push of the 270-code block would be a disconnect.
	n = AP_BoxMap_ScoutCodes(fake_created, &w, out, AP_BOX_LOCATION_COUNT);
	expect_int(n, 0, "a seed with no box locations scouts no box codes");

	// A seed WITH box locations lists exactly those, and in code order. Without
	// them in the scout cache AP_FeedOnLocationSent cannot resolve a peer-bound
	// box's "ITEM TO PLAYER" line and stays silent, which is the bug this closes.
	w.created[w.numCreated++] = 35014269L; // deliberately out of order
	w.created[w.numCreated++] = 35014000L;
	w.created[w.numCreated++] = 35014015L;

	n = AP_BoxMap_ScoutCodes(fake_created, &w, out, AP_BOX_LOCATION_COUNT);
	expect_int(n, 3, "exactly the created box locations are scouted");
	expect_long(out[0], 35014000L, "listed in code order, lowest first");
	expect_long(out[1], 35014015L, "the second");
	expect_long(out[2], 35014269L, "the block's last code");

	// The bound is respected rather than overrun.
	n = AP_BoxMap_ScoutCodes(fake_created, &w, out, 2);
	expect_int(n, 2, "writing stops at the caller's cap");
	expect_int(AP_BoxMap_ScoutCodes(0, &w, out, 4), 0, "no predicate -> nothing scouted");
}

// ── 11. the pad state table, incl. the #232 box re-entry cell ────────────────

// Named arguments for the calls below: the table takes six ints and reads badly
// positionally, and getting one of them backwards is exactly the class of bug
// this section exists to catch.
#define RACE     1
#define NONRACE  0
#define S1_MET   1
#define S1_UNMET 0
#define RACER_MET   1
#define RACER_UNMET 0
#define TROPHY   1
#define NO_TROPHY 0
#define S2_MET   1
#define S2_UNMET 0

static void test_pad_state_table(void)
{
	printf("\n-- the pad state table (Warp-Pad State Model v2) --\n");

	// Done (5) is terminal and beats every other fact, INCLUDING an unmet
	// stage 1: nothing is left, so there is nothing to gate.
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY, S2_MET, 0, 0), 5,
	           "nothing left at all -> Done");
	expect_int(AP_PadStateDecide(RACE, S1_UNMET, RACER_UNMET, NO_TROPHY, S2_UNMET, 0, 0), 5,
	           "nothing left, stage 1 unmet -> still Done");

	// ... but a standing box IS something left, so it holds Done off. This is
	// the half #214 landed; without it a box behind a finished pad is stranded
	// by the hard lock.
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY, S2_MET, 0, 1), 4,
	           "every reward checked but a box stands -> NOT Done");

	// Locked (1) outranks everything below it.
	expect_int(AP_PadStateDecide(RACE, S1_UNMET, RACER_MET, NO_TROPHY, S2_UNMET, 3, 0), 1,
	           "stage 1 unmet -> Locked");
	expect_int(AP_PadStateDecide(RACE, S1_UNMET, RACER_MET, TROPHY, S2_UNMET, 1, 4), 1,
	           "stage 1 unmet, boxes standing -> STILL Locked (entry is the gate)");
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_UNMET, NO_TROPHY, S2_UNMET, 3, 0), 1,
	           "race pad item met, racer unmet -> Locked");
	expect_int(AP_PadStateDecide(NONRACE, S1_MET, RACER_UNMET, NO_TROPHY, S2_MET, 1, 0), 1,
	           "trial/arena/cup item met, racer unmet -> Locked");

	// Reduced lifecycle: a trial / arena / cup destination has no trophy race
	// and no second stage, so stage 1 plus anything left means enterable.
	expect_int(AP_PadStateDecide(NONRACE, S1_MET, RACER_MET, NO_TROPHY, S2_MET, 1, 0), 2,
	           "trial/arena/cup dest with a check left -> Raceable");
	expect_int(AP_PadStateDecide(NONRACE, S1_MET, RACER_MET, NO_TROPHY, S2_UNMET, 0, 2), 2,
	           "non-race dest, only boxes left -> Raceable, stage 2 is not its gate");

	// Race destination, first pass.
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, NO_TROPHY, S2_UNMET, 5, 0), 2,
	           "trophy unwon -> Raceable whatever stage 2 says");

	// THE #232 CELL. Trophy checked and stage 2 unmet is Re-locked ONLY once the
	// boxes are gone; while any stands the pad stays 2 Raceable, which is what
	// AH_WarpPad.c's entry gate and born look now both follow (AP_PadBoxReRaceable
	// is defined as "state 2 with the trophy checked", so these two rows are
	// exactly the two answers it can give).
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY, S2_UNMET, 3, 1), 2,
	           "trophy checked, stage 2 unmet, ONE box standing -> Raceable (#232)");
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY, S2_UNMET, 3, 0), 3,
	           "trophy checked, stage 2 unmet, no boxes left -> Re-locked");
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY, S2_UNMET, 0, 7), 2,
	           "boxes are the ONLY thing left -> still Raceable, never Re-locked");

	// Stage 2 met is the tier-2 tier, boxes or not -- the relic / token menu
	// owns entry there, so #232 must not divert it.
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY, S2_MET, 2, 0), 4,
	           "trophy checked, stage 2 met -> Tier-2 open");
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY, S2_MET, 2, 3), 4,
	           "stage 2 met with boxes standing -> still Tier-2 open");
}

static void test_tier2_route_table(void)
{
	printf("\n-- tier-2 token/relic/box entry routing (#265) --\n");
	expect_int(AP_PadTier2RouteDecide(1, 1, 0), AP_PAD_TIER2_MENU,
	           "token and relic left -> show chooser");
	expect_int(AP_PadTier2RouteDecide(1, 0, 0), AP_PAD_TIER2_TOKEN,
	           "only token left -> direct token race");
	expect_int(AP_PadTier2RouteDecide(0, 1, 0), AP_PAD_TIER2_RELIC,
	           "only relic left -> direct relic race");
	expect_int(AP_PadTier2RouteDecide(0, 0, 1), AP_PAD_TIER2_BOX_RERACE,
	           "both tiers checked, one box left -> plain box re-race");
	expect_int(AP_PadTier2RouteDecide(0, 0, 7), AP_PAD_TIER2_BOX_RERACE,
	           "both tiers checked, many boxes left -> plain box re-race");
	expect_int(AP_PadTier2RouteDecide(0, 0, 0), AP_PAD_TIER2_DONE,
	           "nothing left -> defensive Done route, never a dead menu");
}

// The diagnostic's route codes are what a live #232/#265 log is read through, so
// a collision between the two halves would silently mislabel a branch. Pin the
// shift here, out of engine, the same way the decisions above are pinned.
static void test_pad_route_codes(void)
{
	printf("\n-- pad entry-route diagnostic codes (#232/#265) --\n");
	expect_int(AP_PAD_ROUTE_S2LOCKED_BOX_RERACE != AP_PAD_ROUTE_S2LOCKED_INERT, 1,
	           "the two stage-2-locked routes are distinct codes");
	expect_int((int)AP_PAD_ROUTE_TIER2_BASE > (int)AP_PAD_TIER2_DONE, 1,
	           "the tier-2 base clears every tier-2 enumerator");
	expect_int(AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_MENU > AP_PAD_ROUTE_S2LOCKED_INERT, 1,
	           "no shifted tier-2 code collides with a stage-2-locked code");
	expect_int(AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_BOX_RERACE, 19,
	           "the tier-2 box re-race logs as its own code, not as the #232 one");
	expect_int(AP_PAD_ROUTE_TIER2_BASE + AP_PadTier2RouteDecide(0, 0, 3),
	           AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_BOX_RERACE,
	           "a boxes-only decision logs the tier-2 box re-race code");
	expect_int(AP_PAD_ROUTE_TIER2_BASE + AP_PadTier2RouteDecide(1, 1, 0),
	           AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_MENU,
	           "a chooser decision logs the tier-2 menu code");
}

static void test_combined_232_265_sequence(void)
{
	printf("\n-- combined trophy, gate, tier-2 and boxes lifecycle (#232/#265) --\n");
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, NO_TROPHY,
	                            S2_UNMET, 3, 4), 2,
	           "first trophy race is enterable with boxes standing");
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY,
	                            S2_UNMET, 2, 4), 2,
	           "trophy return stays open before stage 2 while boxes stand");
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY,
	                            S2_MET, 2, 4), 4,
	           "meeting stage 2 advances the same pad to tier 2");
	expect_int(AP_PadTier2RouteDecide(1, 1, 4), AP_PAD_TIER2_MENU,
	           "both ordinary tier-2 checks still use the chooser");
	expect_int(AP_PadTier2RouteDecide(0, 1, 4), AP_PAD_TIER2_RELIC,
	           "after the token check only relic remains");
	expect_int(AP_PadTier2RouteDecide(0, 0, 4), AP_PAD_TIER2_BOX_RERACE,
	           "after token and relic checks boxes get a plain re-race");
	expect_int(AP_PadTier2RouteDecide(0, 0, 1), AP_PAD_TIER2_BOX_RERACE,
	           "the last box still gets the plain re-race");
	expect_int(AP_PadStateDecide(RACE, S1_MET, RACER_MET, TROPHY,
	                            S2_MET, 0, 0), 5,
	           "checking the last box makes the pad Done");
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
	test_embedded_table();
	test_precedence();
	test_reduced_seating_tier();
	test_pad_boxes_left();
	test_scout_codes();
	test_pad_state_table();
	test_tier2_route_table();
	test_pad_route_codes();
	test_combined_232_265_sequence();

	printf("\n%s (%d failure%s)\n",
	       g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
	return g_failures == 0 ? 0 : 1;
}
