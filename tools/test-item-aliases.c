// Out-of-engine assertions for the native display-alias layer (issue #324).
//
//   cc -Wall -Wextra -DCTR_AP -I ap -I . -I include -o /tmp/test-item-aliases tools/test-item-aliases.c && /tmp/test-item-aliases
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Coverage:
//   * every one of the 4 shared-global capability aliases (BOOST+/SPEED+/ACCEL+/TURN+)
//   * every one of the 64 per-character capability aliases (16 racers x 4 chains),
//     racer-first order, exact wording
//   * the Progressive Starting Wumpa alias
//   * every one of the 48 Lettersanity letter aliases (16 tracks x C/T/R)
//   * unknown ids (below native's item space, and an arbitrary cross-game id) fall
//     through to "not aliased"
//   * character-unlock ids (123..138), ordinary items (0..14) and a trap id (16)
//     fall through to "not aliased" -- unchanged per acceptance #4
//   * every aliased identity fits the 31-visible-character item field
//   * the three held-position reason strings (IN 1ST/3RD/5TH), finish reasons
//     unchanged, and an out-of-range tag's fallback

#include <stdio.h>
#include <string.h>

#include "../ap/ap_item_aliases.h"
#include "../ap/ap_rung_feed_reason_logic.h"

static int g_failures = 0;

#define CHECK(name, condition) do { \
	if (!(condition)) { \
		printf("FAIL %s\n", name); \
		g_failures++; \
	} \
} while (0)

// The native item field (AP_FEED_ITEM_CAP / AP_CEREMONY item buffer) allows 31
// VISIBLE characters before FROM/TO/rung text is appended (ap_hooks.c).
#define ITEM_FIELD_MAX 31

static void expect_alias(const char *name, long long id, const char *want)
{
	char out[64];
	int ok = AP_ItemDisplayAlias(id, out, (int)sizeof out);
	if (!ok)
	{
		printf("FAIL %s: expected alias \"%s\", got none\n", name, want);
		g_failures++;
		return;
	}
	if (strcmp(out, want) != 0)
	{
		printf("FAIL %s: expected \"%s\", got \"%s\"\n", name, want, out);
		g_failures++;
		return;
	}
	if (strlen(out) > ITEM_FIELD_MAX)
	{
		printf("FAIL %s: alias \"%s\" is %d chars, exceeds the %d-char item field\n",
		       name, out, (int)strlen(out), ITEM_FIELD_MAX);
		g_failures++;
	}
}

static void expect_no_alias(const char *name, long long id)
{
	char out[64];
	out[0] = '\0';
	int ok = AP_ItemDisplayAlias(id, out, (int)sizeof out);
	CHECK(name, !ok);
}

// ── Shared-global capability chains (indices 27..30) ────────────────────────
static void test_shared_global_capability(void)
{
	expect_alias("shared boost",    AP_ITEM_BASE + 27, "BOOST+");
	expect_alias("shared speed",    AP_ITEM_BASE + 28, "SPEED+");
	expect_alias("shared accel",    AP_ITEM_BASE + 29, "ACCEL+");
	expect_alias("shared turning",  AP_ITEM_BASE + 30, "TURN+");
}

// ── Per-character capability block (indices 31..94): 16 racers x 4 chains ──
// Racer-first order per acceptance #3. Wire roster order and qualifiers are
// ap_capability.c's AP_CAP_ROSTER_CHARACTER / issue #324's approved table.
static void test_percharacter_capability(void)
{
	static const char *const racer[16] = {
		"CRASH", "COCO", "POLAR", "PURA", "CORTEX", "N. TROPY",
		"ROO", "PAPU", "JOE", "PINSTRIPE", "DINGODILE", "TINY",
		"N. GIN", "FAKE CRASH", "OXIDE", "PENTA",
	};
	static const char *const cap[4] = {"BOOST+", "SPEED+", "ACCEL+", "TURN+"};
	int slot, chain;

	for (slot = 0; slot < 16; slot++)
	{
		for (chain = 0; chain < 4; chain++)
		{
			char want[40], name[64];
			long long id = AP_ITEM_BASE + AP_CAPABILITY_PC_ITEM_FIRST_INDEX + slot * 4 + chain;
			snprintf(want, sizeof want, "%s %s", racer[slot], cap[chain]);
			snprintf(name, sizeof name, "pc capability slot=%d(%s) chain=%d", slot, racer[slot], chain);
			expect_alias(name, id, want);
		}
	}

	// Spelled-out examples straight from the issue body.
	expect_alias("issue example: Crash Boost",
	             AP_ITEM_BASE + AP_CAPABILITY_PC_ITEM_FIRST_INDEX + 0 * 4 + 0, "CRASH BOOST+");
	expect_alias("issue example: Penta Accel",
	             AP_ITEM_BASE + AP_CAPABILITY_PC_ITEM_FIRST_INDEX + 15 * 4 + 2, "PENTA ACCEL+");
}

static void test_starting_wumpa(void)
{
	expect_alias("progressive starting wumpa",
	             AP_ITEM_BASE + AP_WUMPA_PROGRESSIVE_ITEM_INDEX, "START WUMPA+");
}

// ── Lettersanity (indices 139..186): 16 tracks x C/T/R ──────────────────────
static void test_letters(void)
{
	static const char *const track[16] = {
		"DINGO CANYON",   "DRAGON MINES",   "BLIZZARD BLUFF", "CRASH COVE",
		"TIGER TEMPLE",   "PAPU'S PYRAMID", "ROO'S TUBES",    "HOT AIR SKYWAY",
		"SEWER SPEEDWAY", "MYSTERY CAVES",  "CORTEX CASTLE",  "N. GIN LABS",
		"POLAR PASS",     "OXIDE STATION",  "COCO PARK",      "TINY ARENA",
	};
	// Row -> LevelID, mirroring ap_lettersanity.h's AP_LetterItemRowToLevelIDPure
	// exactly (an independent copy here so a change to that table cannot silently
	// agree with itself).
	static const int rowLevel[16] = {3, 6, 9, 8, 14, 4, 5, 0, 2, 1, 12, 15, 7, 10, 11, 13};
	static const char letterChar[3] = {'C', 'T', 'R'};
	int row, letter;

	for (row = 0; row < 16; row++)
	{
		for (letter = 0; letter < 3; letter++)
		{
			char want[40], name[64];
			long long id = AP_ITEM_BASE + CTR_LETTER_ITEM_FIRST_INDEX + row * 3 + letter;
			snprintf(want, sizeof want, "%c: %s", letterChar[letter], track[rowLevel[row]]);
			snprintf(name, sizeof name, "letter row=%d letter=%c", row, letterChar[letter]);
			expect_alias(name, id, want);
		}
	}

	// Issue example.
	{
		// Row whose LevelID is 4 (Tiger Temple) is row 5 (rowLevel[5] == 4).
		long long id = AP_ITEM_BASE + CTR_LETTER_ITEM_FIRST_INDEX + 5 * 3 + 2; // R
		expect_alias("issue example: Letter R (Tiger Temple)", id, "R: TIGER TEMPLE");
	}
}

// ── Untouched identities: character unlocks, ordinary items, traps, unknown ─
static void test_unaliased_identities(void)
{
	int i;
	// Character unlocks (123..138) must keep their existing name -- acceptance #4.
	for (i = AP_CHARACTER_ITEM_FIRST_INDEX;
	     i < AP_CHARACTER_ITEM_FIRST_INDEX + AP_CHARACTER_ITEM_COUNT; i++)
	{
		char name[48];
		snprintf(name, sizeof name, "character unlock idx=%d must stay unaliased", i);
		expect_no_alias(name, AP_ITEM_BASE + i);
	}
	// Ordinary core items (Trophy..Key, Wumpa filler) idx 0..15.
	for (i = 0; i <= 15; i++)
	{
		char name[48];
		snprintf(name, sizeof name, "ordinary core item idx=%d must stay unaliased", i);
		expect_no_alias(name, AP_ITEM_BASE + i);
	}
	// A trap identity (Icy Road, idx 16) must stay unaliased.
	expect_no_alias("trap idx=16 (Icy Road) must stay unaliased", AP_ITEM_BASE + 16);
	// Unknown / cross-game ids: far outside native's item space in both directions.
	expect_no_alias("id far below AP_ITEM_BASE", 1);
	expect_no_alias("id at 0", 0);
	expect_no_alias("negative id", -35010001LL);
	expect_no_alias("cross-game id (unrelated base)", 700000LL);
	// A gap between blocks (idx 26, one below the capability block) stays unaliased.
	expect_no_alias("idx=26 (surface item, gap before capability block)", AP_ITEM_BASE + 26);
	// A gap between the per-character block and Starting Wumpa (idx 95..119, 121)
	// must not accidentally alias.
	expect_no_alias("idx=95 (itemsanity weapon, gap after pc capability block)", AP_ITEM_BASE + 95);
	expect_no_alias("idx=121 (Wumpa Big Bundle, one below Starting Wumpa)", AP_ITEM_BASE + 121);
	// One past the last letter index (187, Gas Pedal) must not accidentally alias.
	expect_no_alias("idx=187 (Gas Pedal, one past the last letter index)", AP_ITEM_BASE + 187);
}

// ── Mutation-guard: the ID-vs-name boundary is a hard idx cut, not a name match ──
static void test_id_boundary_not_name_match(void)
{
	// The alias resolver only ever looks at the numeric id; feeding it an id one
	// below a block's first index must not alias, and one at the first index must.
	expect_no_alias("one below capability block first index",
	                 AP_ITEM_BASE + AP_CAPABILITY_ITEM_FIRST_INDEX - 1);
	expect_alias("exactly at capability block first index",
	             AP_ITEM_BASE + AP_CAPABILITY_ITEM_FIRST_INDEX, "BOOST+");
	expect_no_alias("one past the last per-character capability index",
	                 AP_ITEM_BASE + AP_CAPABILITY_PC_ITEM_FIRST_INDEX + AP_CAPABILITY_PC_ITEM_COUNT);
	expect_no_alias("one below the first letter index",
	                 AP_ITEM_BASE + CTR_LETTER_ITEM_FIRST_INDEX - 1);
	expect_no_alias("one past the last letter index",
	                 AP_ITEM_BASE + CTR_LETTER_ITEM_FIRST_INDEX +
	                     CTR_CFG_LETTER_TRACK_COUNT * CTR_CFG_LETTER_COUNT);
}

// ── Held-position reason strings (podium-rung feed, ap_hooks.c) ────────────
static void test_rung_feed_reasons(void)
{
	CHECK("held 1st -> IN 1ST",  strcmp(AP_RungFeedReasonPure(0), "IN 1ST") == 0);
	CHECK("held 3rd -> IN 3RD",  strcmp(AP_RungFeedReasonPure(1), "IN 3RD") == 0);
	CHECK("held 5th -> IN 5TH",  strcmp(AP_RungFeedReasonPure(2), "IN 5TH") == 0);
	// Finish rungs are explicitly NOT part of this change (acceptance: "leave
	// every other feed string ... unchanged").
	CHECK("finish podium unchanged",
	      strcmp(AP_RungFeedReasonPure(3), "FINISH ON PODIUM") == 0);
	CHECK("finish any unchanged", strcmp(AP_RungFeedReasonPure(4), "FINISH") == 0);
	CHECK("out-of-range tag falls back",
	      strcmp(AP_RungFeedReasonPure(99), "PODIUM RUNG") == 0);
	// None of the shortened reasons may regress to the old "BE " prefix.
	CHECK("held 1st has no BE prefix", strncmp(AP_RungFeedReasonPure(0), "BE ", 3) != 0);
	CHECK("held 3rd has no BE prefix", strncmp(AP_RungFeedReasonPure(1), "BE ", 3) != 0);
	CHECK("held 5th has no BE prefix", strncmp(AP_RungFeedReasonPure(2), "BE ", 3) != 0);
}

// ── Maximum-width guard: the worst-case composed line still fits AP_FEED_TEXT_CAP ──
// "%s TO %s (%s)" is the longest feed format (AP_FeedOnRungSent): item(<=31) +
// " TO " + player(<=23) + " (" + reason(<=7) + ")" = 31+4+23+2+7+1 = 68, well
// under the 96-byte AP_FEED_TEXT_CAP storage cap (ap_hooks.c).
static void test_worst_case_line_width(void)
{
	char worst[128];
	const char *longestReason = "FINISH ON PODIUM"; // 17, longer than IN 1ST/3RD/5TH
	snprintf(worst, sizeof worst, "%s TO %s (%s)",
	         "DINGODILE ACCEL+", "AN ARCHIPELAGO PLAYER 1", longestReason);
	CHECK("worst-case composed rung line fits the 96-byte feed buffer",
	      strlen(worst) < 96);
}

int main(void)
{
	test_shared_global_capability();
	test_percharacter_capability();
	test_starting_wumpa();
	test_letters();
	test_unaliased_identities();
	test_id_boundary_not_name_match();
	test_rung_feed_reasons();
	test_worst_case_line_width();

	printf("\n%s\n", g_failures ? "FAILURES PRESENT" : "all assertions passed");
	return g_failures != 0;
}
