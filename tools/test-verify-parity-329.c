// Seed-verifier parity with the apworld capability rules after the 2026-09-27
// rulings (issue #329): the difficulty term with Itemsanity off, the gated
// track set, Hot Air Skyway Held 1st, the Oxide win terms, the CTR Token
// Challenge terms, per-letter terms, boss races and the Platinum floor.
// Table-driven; every row names the apworld rule it mirrors.
#include <stdio.h>
#include <string.h>

#include "../ap/ap_verify_logic.h"

static int failures;
#define OK(label, expr) do { int got = !!(expr); \
	printf("%s  %s\n", got ? "ok  " : "FAIL", label); failures += !got; } while (0)

static AP_VerifyOptions base_options(void)
{
	AP_VerifyOptions o;
	memset(&o, 0, sizeof o);
	o.character_unlocks = 1;
	o.starting_character = 0;
	o.boost_mode = 1;
	o.logic_difficulty = 1;
	o.shortcut_knowledge = 0;
	return o;
}

static void give_families(int items[AP_VF_ITEM_COUNT], int families)
{
	static const int weapon[5] = {6, 2, 1, 8, 7}; // Mask, Missile, Bomb, Warpball, Clock
	int i;
	for (i = 0; i < families && i < 5; i++)
		items[AP_VF_WEAPON_FIRST + weapon[i]] = 1;
}

// 1. add_capability_difficulty_rules on a difficulty-gated track (Crash Cove).
typedef struct
{
	const char *label;
	int boost_mode, itemsanity, difficulty, boosts, families, rung, want;
} DifficultyRow;

static const DifficultyRow difficulty_rows[] = {
	{"itemsanity off, medium, Trophy, no boost: blocked (#329)",         1, 0, 1, 0, 0, -1, 0},
	{"itemsanity off, medium, Trophy, one boost: open",                  1, 0, 1, 1, 0, -1, 1},
	{"itemsanity off, medium, Trophy, three families but no items: blocked", 1, 0, 1, 0, 3, -1, 0},
	{"itemsanity off, easy, Trophy, no boost: blocked",                  1, 0, 0, 0, 0, -1, 0},
	{"itemsanity off, hard, Trophy, no boost: open",                     1, 0, 2, 0, 0, -1, 1},
	{"boost pack off, easy, Trophy: vacuous",                            0, 0, 0, 0, 0, -1, 1},
	{"itemsanity on, medium, Trophy, two families: blocked",             1, 1, 1, 0, 2, -1, 0},
	{"itemsanity on, medium, Trophy, three families: open",              1, 1, 1, 0, 3, -1, 1},
	{"itemsanity on, medium, Trophy, one boost: open",                   1, 1, 1, 1, 0, -1, 1},
	{"itemsanity off, easy, Finish on Podium, no boost: blocked",        1, 0, 0, 0, 0,  3, 0},
	{"itemsanity off, easy, Held 1st, no boost: blocked",                1, 0, 0, 0, 0,  0, 0},
	{"itemsanity off, easy, Held 1st, one boost: open",                  1, 0, 0, 1, 0,  0, 1},
	{"itemsanity off, easy, Held 3rd, no boost: free",                   1, 0, 0, 0, 0,  1, 1},
	{"itemsanity off, easy, Finish Any, no boost: free",                 1, 0, 0, 0, 0,  4, 1},
	{"itemsanity off, medium, Finish on Podium, no boost: free",         1, 0, 1, 0, 0,  3, 1},
	{"itemsanity off, medium, Held 1st, no boost: free",                 1, 0, 1, 0, 0,  0, 1},
	{"itemsanity on, easy, Held 1st, three families: open",              1, 1, 0, 0, 3,  0, 1},
};

static void test_difficulty(void)
{
	unsigned i;
	for (i = 0; i < sizeof difficulty_rows / sizeof difficulty_rows[0]; i++)
	{
		const DifficultyRow *r = &difficulty_rows[i];
		AP_VerifyOptions o = base_options();
		int items[AP_VF_ITEM_COUNT];
		int res;
		memset(items, 0, sizeof items);
		o.boost_mode = r->boost_mode;
		o.itemsanity = r->itemsanity;
		o.logic_difficulty = r->difficulty;
		items[AP_VF_BOOST_SHARED] = r->boosts;
		give_families(items, r->families);
		res = r->rung < 0
			? AP_VerifyTrophyCapabilityGate(&o, items, 3, -1)
			: AP_VerifyPodiumRung(&o, items, 3, r->rung, -1, 1, 0);
		OK(r->label, res == r->want);
	}
}

// 2. difficulty_gated_tracks(): easy group + ruled group, trial tracks included.
static void test_gated_set(void)
{
	// Level ids 0..17. 7 (Hot Air Skyway), 10 (Cortex Castle) and 13 (Oxide
	// Station) are finish-gated instead and are not in the difficulty set.
	static const int gated[18] = {
		1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1 };
	AP_VerifyOptions o = base_options();
	int items[AP_VF_ITEM_COUNT];
	int level, all = 1;
	char label[96];
	memset(items, 0, sizeof items);
	for (level = 0; level < 18; level++)
		if (AP_VerifyDifficultyTrack(level) != gated[level])
		{
			snprintf(label, sizeof label, "gated set disagrees at level %d", level);
			OK(label, 0);
			all = 0;
		}
	OK("gated set is the 7 measured + 8 ruled tracks (Slide Coliseum, Turbo Track included)", all);
	OK("Slide Coliseum Trophy blocked with no boost on medium",
		!AP_VerifyTrophyCapabilityGate(&o, items, 16, -1));
	OK("Turbo Track Trophy blocked with no boost on medium",
		!AP_VerifyTrophyCapabilityGate(&o, items, 17, -1));
	OK("custom Trophy Race blocked with no boost on medium",
		!AP_VerifyDifficultyRungTerm(&o, items, -1, -1));
	OK("custom Held 3rd stays free on easy", (o.logic_difficulty = 0,
		AP_VerifyDifficultyRungTerm(&o, items, 1, -1)));
	OK("custom Finish on Podium blocked on easy",
		!AP_VerifyDifficultyRungTerm(&o, items, 3, -1));
	items[AP_VF_BOOST_SHARED] = 1;
	OK("custom Trophy Race opens at one boost",
		AP_VerifyDifficultyRungTerm(&o, items, -1, -1));
}

// 3. Hot Air Skyway (7) and Oxide Station (13) podium rungs.
typedef struct
{
	const char *label;
	int level, difficulty, sk, boosts, rung, own, cup, want;
} RungRow;

static const RungRow rung_rows[] = {
	{"HAS Held 1st bare on hard: blocked (#329)",               7, 2, 0, 0, 0, 1, 0, 0},
	{"HAS Held 1st bare on hard knowledge: blocked, no escape", 7, 2, 2, 0, 0, 1, 0, 0},
	{"HAS Held 1st at one boost: blocked",                      7, 1, 0, 1, 0, 1, 0, 0},
	{"HAS Held 1st at USF: open",                               7, 1, 0, 2, 0, 1, 0, 1},
	{"HAS Held 1st via a legging cup, bare: blocked",           7, 2, 0, 0, 0, 0, 1, 0},
	{"HAS Held 1st via a legging cup at USF: open",             7, 2, 0, 2, 0, 0, 1, 1},
	{"HAS Held 3rd bare: free",                                 7, 0, 0, 0, 1, 1, 0, 1},
	{"HAS Held 5th bare: free",                                 7, 0, 0, 0, 2, 1, 0, 1},
	{"HAS Finish Any bare on its own pad: blocked (finish)",    7, 2, 0, 0, 4, 1, 0, 0},
	{"HAS Finish Any at USF: open",                             7, 2, 0, 2, 4, 1, 0, 1},
	{"Oxide Held 1st bare: blocked",                           13, 2, 0, 0, 0, 1, 0, 0},
	{"Oxide Held 1st bare on hard knowledge: open",            13, 2, 2, 0, 0, 1, 0, 1},
	{"Oxide Held 3rd bare: free",                              13, 2, 0, 0, 1, 1, 0, 1},
	{"no route at all: blocked",                                7, 2, 0, 2, 1, 0, 0, 0},
	{"Crash Cove Finish on Podium via cup, easy, bare: blocked", 3, 0, 0, 0, 3, 0, 1, 0},
	{"Crash Cove Finish Any via cup, easy, bare: open",          3, 0, 0, 0, 4, 0, 1, 1},
};

static void test_rungs(void)
{
	unsigned i;
	for (i = 0; i < sizeof rung_rows / sizeof rung_rows[0]; i++)
	{
		const RungRow *r = &rung_rows[i];
		AP_VerifyOptions o = base_options();
		int items[AP_VF_ITEM_COUNT];
		memset(items, 0, sizeof items);
		o.logic_difficulty = r->difficulty;
		o.shortcut_knowledge = r->sk;
		items[AP_VF_BOOST_SHARED] = r->boosts;
		OK(r->label, AP_VerifyPodiumRung(&o, items, r->level, r->rung, -1,
			r->own, r->cup) == r->want);
	}
	{
		AP_VerifyOptions o = base_options();
		int items[AP_VF_ITEM_COUNT];
		memset(items, 0, sizeof items);
		o.boost_mode = 0;
		OK("HAS Held 1st vacuous with the boost pack off",
			AP_VerifyPodiumRung(&o, items, 7, 0, 1, 1, 0));
		o.boost_mode = 2;
		items[AP_VF_PC_FIRST + 0 * 4] = 2;    // Crash USF
		items[AP_VF_CHARACTER_FIRST + 4] = 1; // Cortex unlocked, empty chain
		OK("HAS Held 1st on a Cortex-locked pad does not borrow Crash USF",
			!AP_VerifyPodiumRung(&o, items, 7, 0, 1, 1, 0));
	}
}

// 4. add_oxide_access_contract: first boost rank plus the venue finish term.
typedef struct
{
	const char *label;
	int final_challenge, cortex_venue, sk, boosts, want;
} OxideRow;

static const OxideRow oxide_rows[] = {
	{"first challenge, one boost: blocked (Oxide Station USF)", 0, 0, 0, 1, 0},
	{"first challenge, hard knowledge, no boost: blocked (floor)", 0, 0, 2, 0, 0},
	{"first challenge, hard knowledge, one boost: open",        0, 0, 2, 1, 1},
	{"final on Oxide Station, hard knowledge, one boost: open", 1, 0, 2, 1, 1},
	{"final on Cortex Vortex, hard knowledge, one boost: blocked (venue USF)", 1, 1, 2, 1, 0},
	{"final on Cortex Vortex at USF: open",                     1, 1, 2, 2, 1},
	{"final on Cortex Vortex, medium knowledge, USF: open",     1, 1, 1, 2, 1},
};

static void test_oxide(void)
{
	unsigned i;
	for (i = 0; i < sizeof oxide_rows / sizeof oxide_rows[0]; i++)
	{
		const OxideRow *r = &oxide_rows[i];
		AP_VerifyOptions o = base_options();
		int items[AP_VF_ITEM_COUNT];
		memset(items, 0, sizeof items);
		o.oxide_final_cortex = r->cortex_venue;
		o.shortcut_knowledge = r->sk;
		items[AP_VF_BOOST_SHARED] = r->boosts;
		OK(r->label, AP_VerifyOxideWinTerm(&o, items, r->final_challenge) == r->want);
	}
}

// 5. Token, letter, boss and relic terms that mirror the capability contract.
static void test_other_terms(void)
{
	AP_VerifyOptions o = base_options();
	AP_VerifyLetterSet ls;
	int items[AP_VF_ITEM_COUNT];

	memset(items, 0, sizeof items);
	memset(&ls, 0, sizeof ls);
	ls.item[0] = ls.item[1] = ls.item[2] = -1;
	o.logic_difficulty = 2;
	OK("token challenge takes the first boost rank at hard",
		!AP_VerifyTokenTerm(&o, items, 3, -1, &ls));
	items[AP_VF_BOOST_SHARED] = 1;
	OK("token challenge opens at one boost", AP_VerifyTokenTerm(&o, items, 3, -1, &ls));
	OK("Oxide token needs USF while T and R are physical",
		!AP_VerifyTokenTerm(&o, items, 13, -1, &ls));
	ls.mode = 2;
	ls.present[0] = 1;
	ls.item[0] = 150;
	OK("mode 2 token needs its selected letter item",
		!AP_VerifyTokenTerm(&o, items, 3, -1, &ls));
	items[150] = 1;
	OK("mode 2 token opens with its selected letter item",
		AP_VerifyTokenTerm(&o, items, 3, -1, &ls));
	OK("Oxide token with only C selected: first rank is enough",
		AP_VerifyTokenTerm(&o, items, 13, -1, &ls));
	ls.mode = 3;
	ls.item[1] = 151;
	ls.item[2] = 152;
	OK("mode 3 token needs all three letter items",
		!AP_VerifyTokenTerm(&o, items, 3, -1, &ls));
	items[151] = items[152] = 1;
	OK("mode 3 token opens with all three letter items",
		AP_VerifyTokenTerm(&o, items, 3, -1, &ls));
	ls.mode = 0;
	o.itemsanity = 1;
	OK("Tiger token needs a door opener with Itemsanity on",
		!AP_VerifyTokenTerm(&o, items, 4, -1, &ls));
	items[AP_VF_WEAPON_FIRST + 5] = 1; // Shield Bubble
	OK("Tiger token opens with a door opener", AP_VerifyTokenTerm(&o, items, 4, -1, &ls));
	o.boost_mode = 0;
	items[AP_VF_BOOST_SHARED] = 0;
	OK("token floor vacuous with the boost pack off",
		AP_VerifyTokenTerm(&o, items, 3, -1, &ls));

	o = base_options();
	memset(items, 0, sizeof items);
	items[AP_VF_BOOST_SHARED] = 1;
	o.shortcut_knowledge = 2;
	OK("Oxide letter T needs USF, no hard-knowledge escape",
		!AP_VerifyLetterTerm(&o, items, 13, 1, -1));
	OK("Oxide letter C keeps the shared rule", AP_VerifyLetterTerm(&o, items, 13, 0, -1));
	o.itemsanity = 1;
	OK("Tiger letter R needs a door opener", !AP_VerifyLetterTerm(&o, items, 4, 2, -1));
	OK("Tiger letter C is free", AP_VerifyLetterTerm(&o, items, 4, 0, -1));
	items[AP_VF_BOOST_SHARED] = 0;
	OK("Papu letter C bare: blocked", !AP_VerifyLetterTerm(&o, items, 5, 0, -1));
	items[AP_VF_WEAPON_FIRST + AP_VF_WEAPON_MASK] = 1;
	OK("Papu letter C with Mask and Itemsanity: open", AP_VerifyLetterTerm(&o, items, 5, 0, -1));
	o.itemsanity = 0;
	OK("Papu letter C with Mask but Itemsanity off: blocked",
		!AP_VerifyLetterTerm(&o, items, 5, 0, -1));
	OK("Papu letter R is free", AP_VerifyLetterTerm(&o, items, 5, 2, -1));

	o = base_options();
	memset(items, 0, sizeof items);
	OK("boss race takes the first boost rank", !AP_VerifyBossWinTerm(&o, items, 0));
	items[AP_VF_BOOST_SHARED] = 1;
	OK("boss race opens at one boost", AP_VerifyBossWinTerm(&o, items, 0));
	OK("Pinstripe (Hot Air Skyway) needs USF", !AP_VerifyBossWinTerm(&o, items, 3));
	items[AP_VF_BOOST_SHARED] = 2;
	OK("Pinstripe opens at USF", AP_VerifyBossWinTerm(&o, items, 3));

	o = base_options();
	memset(items, 0, sizeof items);
	items[AP_VF_BOOST_SHARED] = 1;
	OK("medium Platinum floor is rank 2", !AP_VerifyLocationCapabilityGate(&o, items, 35012200L, -1));
	OK("medium Gold stays rank 1", AP_VerifyLocationCapabilityGate(&o, items, 35012100L, -1));
	items[AP_VF_BOOST_SHARED] = 2;
	OK("medium Platinum opens at rank 2", AP_VerifyLocationCapabilityGate(&o, items, 35012200L, -1));
	o.logic_difficulty = 0;
	o.boost_blue_fire = 1;
	OK("easy Platinum with Blue Fire needs rank 3",
		!AP_VerifyLocationCapabilityGate(&o, items, 35012200L, -1));
	items[AP_VF_BOOST_SHARED] = 3;
	OK("easy Platinum with Blue Fire opens at rank 3",
		AP_VerifyLocationCapabilityGate(&o, items, 35012200L, -1));
	o.logic_difficulty = 2;
	items[AP_VF_BOOST_SHARED] = 1;
	OK("hard Platinum keeps the per-track rank 1",
		AP_VerifyLocationCapabilityGate(&o, items, 35012200L, -1));
}

int main(void)
{
	test_difficulty();
	test_gated_set();
	test_rungs();
	test_oxide();
	test_other_terms();
	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
