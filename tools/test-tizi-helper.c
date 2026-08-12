// Host harness for the #223 Tizi Helper's two pure rules (ap/ap_tizi_logic.h).
//
// Both rules are unreachable from a build machine: one needs a live server and
// a slot's item history, the other needs Papu's Pyramid's level data off the
// disc. So they are lifted into a header with no engine, no network and no
// config dependency, and driven here directly.
//
// WHAT IS COVERED
//
//   1. THE RULED ACTIVATION TABLE, exhaustively. Both itemsanity states crossed
//      with all four combinations of "helper received" and "Mask received" --
//      the exact matrix the #223 body and the 2026-08-10 ruling specify.
//
//   2. THE ROW SELECTION, including every refusal. A clean four-box row, a row
//      that straddles two checkpoint nodes, a fifth box far enough back to keep
//      the row unambiguous and one too close to it, a track whose crates all sit
//      mid-lap, too few crates, and a level that reported no track length.
//
// WHAT IS NOT COVERED, and cannot be here: whether the four crates this rule
// selects on the REAL Papu's Pyramid LEV are the four the player means. That is
// an in-game observation and it is queued as one. The rule fails closed and logs
// its census, so the in-game pass is a log read, not a re-derivation.

#include <stdio.h>

#include "../ap/ap_tizi_logic.h"

// Prototypes and item indices only, no engine dependency. Pulled in so the
// composition invariant in section 3 can be asserted against the real
// constants rather than against numbers copied into this file.
#define CTR_AP 1
#include "../ap/ap_tizi.h"
#include "../ap/ap_itemsanity_logic.h"

static int g_failures;

static void check(int condition, const char *what)
{
	if (!condition)
	{
		printf("FAIL: %s\n", what);
		g_failures++;
	}
}

// ---------------------------------------------------------------------------
// 1. The ruled activation table
// ---------------------------------------------------------------------------

static void test_no_helper_is_never_active(void)
{
	check(!AP_TiziHelperActive(0, 0, 0), "no helper, itemsanity off");
	check(!AP_TiziHelperActive(0, 0, 1), "no helper, itemsanity off, mask held");
	check(!AP_TiziHelperActive(0, 1, 0), "no helper, itemsanity on");
	check(!AP_TiziHelperActive(0, 1, 1), "no helper, itemsanity on, mask held");
}

static void test_itemsanity_off_needs_only_the_helper(void)
{
	check(AP_TiziHelperActive(1, 0, 0), "helper alone activates with itemsanity off");
	check(AP_TiziHelperActive(1, 0, 1), "helper plus mask activates with itemsanity off");
}

static void test_itemsanity_on_needs_both(void)
{
	check(!AP_TiziHelperActive(1, 1, 0),
	      "helper without the Mask item stays inert with itemsanity on");
	check(AP_TiziHelperActive(1, 1, 1),
	      "helper plus the Mask item activates with itemsanity on");
}

// ---------------------------------------------------------------------------
// 2. Row selection
// ---------------------------------------------------------------------------

#define TRACK_LEN 4096L
// The opening stretch is the top eighth of the lap: >= 3584 here.
// One row span is a 64th: 64 here.

static void test_a_clean_row_of_four_is_selected(void)
{
	// Four crates on one checkpoint node just past the line, then the rest of
	// the track's boxes much further along.
	long progress[] = {4000, 4000, 4000, 4000, 3000, 2500, 1200};
	int index[AP_TIZI_ROW_SIZE];
	int rc = AP_TiziSelectRow(progress, 7, TRACK_LEN, index);
	check(rc == AP_TIZI_ROW_OK, "a clean row resolves");
	check(index[0] >= 0 && index[0] <= 3, "row member 0 is one of the four");
	check(index[3] >= 0 && index[3] <= 3, "row member 3 is one of the four");
	check(index[0] != index[1] && index[1] != index[2] && index[2] != index[3],
	      "row members are distinct");
}

static void test_a_row_straddling_two_nodes_still_resolves(void)
{
	// Real geometry: two boxes resolve to one node, two to the next.
	long progress[] = {4000, 4000, 3970, 3970, 3000};
	int index[AP_TIZI_ROW_SIZE];
	check(AP_TiziSelectRow(progress, 5, TRACK_LEN, index) == AP_TIZI_ROW_OK,
	      "a row spread across two adjacent nodes still resolves");
}

static void test_a_scattered_leading_four_is_refused(void)
{
	// Four crates in the opening stretch but strung out along it: that is not a
	// row, and picking the leading four anyway would be a guess.
	long progress[] = {4090, 3900, 3750, 3600, 3000};
	int index[AP_TIZI_ROW_SIZE];
	check(AP_TiziSelectRow(progress, 5, TRACK_LEN, index) == AP_TIZI_ROW_NOT_A_ROW,
	      "a scattered leading four is refused");
}

static void test_a_fifth_box_in_the_same_row_is_ambiguous(void)
{
	long progress[] = {4000, 4000, 4000, 4000, 3990};
	int index[AP_TIZI_ROW_SIZE];
	check(AP_TiziSelectRow(progress, 5, TRACK_LEN, index) == AP_TIZI_ROW_AMBIGUOUS,
	      "a fifth crate inside the row span makes the row ambiguous");
}

static void test_a_fifth_box_clearly_behind_is_fine(void)
{
	long progress[] = {4000, 4000, 4000, 4000, 3800};
	int index[AP_TIZI_ROW_SIZE];
	check(AP_TiziSelectRow(progress, 5, TRACK_LEN, index) == AP_TIZI_ROW_OK,
	      "a fifth crate clearly behind the row leaves the row unambiguous");
}

static void test_crates_outside_the_opening_stretch_do_not_count(void)
{
	// Every crate sits mid-lap: there is no first row after the start line.
	long progress[] = {3000, 3000, 3000, 3000, 2000};
	int index[AP_TIZI_ROW_SIZE];
	check(AP_TiziSelectRow(progress, 5, TRACK_LEN, index) == AP_TIZI_ROW_TOO_FEW,
	      "crates outside the opening stretch are not a start-line row");
}

static void test_too_few_crates_is_refused(void)
{
	long progress[] = {4000, 4000, 4000};
	int index[AP_TIZI_ROW_SIZE];
	check(AP_TiziSelectRow(progress, 3, TRACK_LEN, index) == AP_TIZI_ROW_TOO_FEW,
	      "three crates cannot form a row of four");
	check(AP_TiziSelectRow(progress, 0, TRACK_LEN, index) == AP_TIZI_ROW_TOO_FEW,
	      "an empty census is refused");
}

static void test_a_missing_track_length_is_refused(void)
{
	long progress[] = {4000, 4000, 4000, 4000};
	int index[AP_TIZI_ROW_SIZE];
	check(AP_TiziSelectRow(progress, 4, 0, index) == AP_TIZI_ROW_NO_TRACK_LENGTH,
	      "a level with no track length is refused rather than guessed at");
}

static void test_the_row_is_reported_in_track_order(void)
{
	// Ordering matters for the log line an in-game pass reads: the highest
	// distToFinish (closest to the line) comes first.
	long progress[] = {3990, 4020, 4000, 4010, 3000};
	int index[AP_TIZI_ROW_SIZE];
	check(AP_TiziSelectRow(progress, 5, TRACK_LEN, index) == AP_TIZI_ROW_OK,
	      "the near-line four resolve");
	check(progress[index[0]] >= progress[index[1]] &&
	      progress[index[1]] >= progress[index[2]] &&
	      progress[index[2]] >= progress[index[3]],
	      "the row is reported nearest-the-line first");
}

static void test_the_selection_never_reads_past_the_census(void)
{
	// A one-element census must not walk into the rest of the array.
	long progress[] = {4000, 4000, 4000, 4000};
	int index[AP_TIZI_ROW_SIZE];
	check(AP_TiziSelectRow(progress, 1, TRACK_LEN, index) == AP_TIZI_ROW_TOO_FEW,
	      "count, not array size, bounds the census");
}

// ---------------------------------------------------------------------------
// 3. Composition invariant: the Mask receipt stays reachable with itemsanity on
//
// #223's Mask index and #145's weapon-item range are not independent. If the
// Mask sits inside the range, the ReceivedItems drain in ap_hooks.c is an
// else-if chain in which the itemsanity arm matches FIRST, so a separate
// `idx == AP_TIZI_MASK_ITEM_INDEX` arm is dead code and AP_TiziReceiveMask() is
// never called -- which, per the activation table above, leaves the helper
// permanently inert on exactly the seeds it exists for. The v2 composition
// resolved that by having the itemsanity arm forward the Mask explicitly.
//
// These assertions pin the two facts that make the forward mandatory. If a
// future renumber moves the Mask out of the range, the second one fails and
// whoever renumbers is told to revisit the drain rather than discovering it in
// game.
// ---------------------------------------------------------------------------

static void test_the_mask_index_agrees_with_the_itemsanity_numbering(void)
{
	// The Mask is held item 0x7, which the itemsanity numbering places at
	// weapon slot 6, i.e. apworld item index 95 + 6 = 101.
	check(AP_ItemsanityWeaponIndex(AP_TIZI_HELD_ITEM_MASK) == 6,
	      "Mask is itemsanity weapon slot 6");
	check(AP_TIZI_MASK_ITEM_INDEX ==
	          AP_ITEMSANITY_ITEM_FIRST_INDEX +
	              AP_ItemsanityWeaponIndex(AP_TIZI_HELD_ITEM_MASK),
	      "#223's Mask item index is derived from the itemsanity numbering");
}

static void test_the_mask_receipt_needs_the_itemsanity_drain_to_forward_it(void)
{
	// The overlap itself. While this holds, the itemsanity arm owns index 101
	// and MUST call AP_TiziReceiveMask(); nothing else in the chain can.
	check(AP_TIZI_MASK_ITEM_INDEX >= AP_ITEMSANITY_ITEM_FIRST_INDEX &&
	          AP_TIZI_MASK_ITEM_INDEX <
	              AP_ITEMSANITY_ITEM_FIRST_INDEX + AP_ITEMSANITY_WEAPON_COUNT,
	      "Mask index is inside the itemsanity range, so the drain must fan it out");

	// And the consequence that makes it matter: with itemsanity on, a helper
	// with no Mask receipt is inert.
	check(!AP_TiziHelperActive(1, 1, 0),
	      "helper without the Mask receipt is inert while itemsanity is on");
	check(AP_TiziHelperActive(1, 1, 1),
	      "helper with the Mask receipt is active while itemsanity is on");

	// The Tizi Helper item itself is outside the range, so its own arm is fine.
	check(AP_TIZI_ITEM_INDEX < AP_ITEMSANITY_ITEM_FIRST_INDEX ||
	          AP_TIZI_ITEM_INDEX >=
	              AP_ITEMSANITY_ITEM_FIRST_INDEX + AP_ITEMSANITY_WEAPON_COUNT,
	      "the Tizi Helper item index does not collide with the itemsanity range");
}

int main(void)
{
	test_no_helper_is_never_active();
	test_itemsanity_off_needs_only_the_helper();
	test_itemsanity_on_needs_both();

	test_a_clean_row_of_four_is_selected();
	test_a_row_straddling_two_nodes_still_resolves();
	test_a_scattered_leading_four_is_refused();
	test_a_fifth_box_in_the_same_row_is_ambiguous();
	test_a_fifth_box_clearly_behind_is_fine();
	test_crates_outside_the_opening_stretch_do_not_count();
	test_too_few_crates_is_refused();
	test_a_missing_track_length_is_refused();
	test_the_row_is_reported_in_track_order();
	test_the_selection_never_reads_past_the_census();

	test_the_mask_index_agrees_with_the_itemsanity_numbering();
	test_the_mask_receipt_needs_the_itemsanity_drain_to_forward_it();

	if (g_failures == 0)
		printf("tizi helper (activation table + row selection): all checks passed\n");
	else
		printf("tizi helper: %d FAILURE(S)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
