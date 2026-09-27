// Seed-verifier coverage and rules for the three families the [AP VERIFY]
// model did not enumerate: Relic Race Perfect (#49), the trial-track Trophy
// Race and CTR Token Challenge (#203) and Hit <Character>. Before this, a seed
// with all three covered 366/404 locations and the verdict was INDETERMINATE.
//
//   c++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include
//     tools/test-verify-families.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-verify-families && /tmp/test-verify-families
//
// Run from the repository root (fixture paths are relative to it).
//
// 1. Coverage: the REAL parser reads an apworld-generated slot_data with all
//    three families on (tools/fixtures/verify-families/, seed 20260927), and
//    AP_VerifyFamilyWorklist must name exactly the family codes in that seed's
//    location list. Plus the refusal and truncation paths.
// 2. Rules: table-driven rows for each family; every row names the apworld
//    rule it mirrors (Rules.add_time_trial_and_ctr_requirements,
//    usf_finish.relic_perfect_boost_min, capability_contract RULED_TROPHY_GROUP,
//    hit_character._install_target).

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"
#include "../ap/ap_relic_perfect.h"
#include "../ap/ap_verify_logic.h"
#include "../ap/ap_verify_families.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);
extern "C" void AP_LogLine(const char *) {}

static int checks;
static int failures;

static void ok(bool cond, const std::string &label)
{
	checks++;
	if (!cond)
	{
		failures++;
		std::printf("FAIL  %s\n", label.c_str());
	}
}

static nlohmann::json load(const char *path)
{
	std::ifstream in(path);
	if (!in)
	{
		std::printf("FAIL  cannot open %s\n", path);
		failures++;
		return nlohmann::json::object();
	}
	return nlohmann::json::parse(in);
}

static bool is_family_code(long c)
{
	return (c >= 35012400L && c <= 35012417L) || (c >= 35016200L && c <= 35016201L) ||
		(c >= 35016210L && c <= 35016211L) || (c >= 35025000L && c <= 35025015L);
}

static_assert(AP_VF_FAMILY_MAX == 38, "18 perfect + 4 trial + 16 Hit rows");

// ---------------------------------------------------------------------------
// 1. Coverage
// ---------------------------------------------------------------------------
static void test_coverage(void)
{
	const char *base = "tools/fixtures/verify-families/all_seed20260927";
	nlohmann::json slot = load((std::string(base) + ".slot_data.json").c_str());
	nlohmann::json server = load((std::string(base) + ".locations.json").c_str());
	AP_VerifyFamilyLocation rows[AP_VF_FAMILY_MAX];
	int truncated = 0, n, i, per_family[4] = {0, 0, 0, 0};
	std::set<long> want, got;

	ap_seedcfg_parse_json(slot);
	ok(ctr_cfg.relic_perfect_enabled == 1, "fixture: relic_perfect_checks parsed");
	ok(ctr_cfg.hit.enabled && ctr_cfg.hit.valid, "fixture: hit_character_encounters parsed");
	ok(ctr_cfg.trial_track_valid[0] && ctr_cfg.trial_track_valid[1],
	   "fixture: trial_track_checks parsed");

	for (auto &c : server)
		if (is_family_code(c.get<long>()))
			want.insert(c.get<long>());
	ok(want.size() == 38, "fixture: the apworld created 38 family locations");

	n = AP_VerifyFamilyWorklist(&ctr_cfg, rows, AP_VF_FAMILY_MAX, &truncated);
	ok(n == 38 && !truncated, "all three families enumerate 38 rows, no truncation");
	for (i = 0; i < n; i++)
	{
		got.insert(rows[i].code);
		if (rows[i].family >= 0 && rows[i].family < 4)
			per_family[rows[i].family]++;
		switch (rows[i].family)
		{
		case AP_VF_FAMILY_RELIC_PERFECT:
			ok(rows[i].code == AP_RelicPerfectExpectedCode(rows[i].track),
			   "perfect row carries its LevelID " + std::to_string(rows[i].track));
			break;
		case AP_VF_FAMILY_TRIAL_TROPHY:
			ok(rows[i].code == 35016200L + (rows[i].track - 16),
			   "trial Trophy row carries LevelID " + std::to_string(rows[i].track));
			break;
		case AP_VF_FAMILY_TRIAL_CTR:
			ok(rows[i].code == 35016210L + (rows[i].track - 16),
			   "trial CTR row carries LevelID " + std::to_string(rows[i].track));
			break;
		case AP_VF_FAMILY_HIT:
			ok(rows[i].code == 35025000L + rows[i].detail,
			   "Hit row carries engine id " + std::to_string(rows[i].detail));
			break;
		default:
			ok(false, "unknown family");
		}
	}
	ok(got == want, "worklist codes == the seed's family location codes");
	ok(per_family[AP_VF_FAMILY_RELIC_PERFECT] == 18 &&
	   per_family[AP_VF_FAMILY_TRIAL_TROPHY] == 2 &&
	   per_family[AP_VF_FAMILY_TRIAL_CTR] == 2 && per_family[AP_VF_FAMILY_HIT] == 16,
	   "18 perfect, 2 trial Trophy, 2 trial CTR, 16 Hit");

	// The boss identity table the Hit rule reads is keyed in
	// AP_VF_HIT_BOSS_CODES order; check it against the wire mapping.
	{
		const auto &bosses = slot["hit_character_encounters"]["bosses"];
		for (i = 0; i < CTR_CFG_HIT_BOSS_COUNT; i++)
		{
			std::string key = std::to_string(AP_VF_HIT_BOSS_CODES[i]);
			ok(bosses.contains(key) &&
			   bosses[key].get<int>() == ctr_cfg.hit.boss_identity[i],
			   "boss identity row " + key);
		}
	}

	// Truncation: a short buffer raises the flag and never overruns.
	truncated = 0;
	n = AP_VerifyFamilyWorklist(&ctr_cfg, rows, 10, &truncated);
	ok(n == 10 && truncated, "short buffer: 10 rows and the truncation flag");

	// A refused trial row is not payable, so it is not modelled either.
	{
		nlohmann::json bad = slot;
		bad["trial_track_checks"]["locations"]["17"] = {35016201, -1};
		ap_seedcfg_parse_json(bad);
		truncated = 0;
		n = AP_VerifyFamilyWorklist(&ctr_cfg, rows, AP_VF_FAMILY_MAX, &truncated);
		ok(n == 36 && !ctr_cfg.trial_track_valid[1],
		   "Turbo Track row refused (mode 2 without a CTR code): its 2 rows leave");
	}
	// Families off: nothing enumerates.
	{
		nlohmann::json off = slot;
		off.erase("relic_perfect_checks");
		off.erase("trial_track_checks");
		off.erase("hit_character_encounters");
		off["ctr_options"]["hit_character"] = false;
		off["ctr_options"]["relic_perfect_checks"] = false;
		ap_seedcfg_parse_json(off);
		truncated = 0;
		n = AP_VerifyFamilyWorklist(&ctr_cfg, rows, AP_VF_FAMILY_MAX, &truncated);
		ok(n == 0, "all three families off: no rows");
	}
	ap_seedcfg_parse_json(nlohmann::json::object());
}

// ---------------------------------------------------------------------------
// 2. Rules
// ---------------------------------------------------------------------------
static AP_VerifyOptions opts(int boost_mode, int difficulty, int itemsanity)
{
	AP_VerifyOptions o;
	std::memset(&o, 0, sizeof o);
	o.character_unlocks = 1;
	o.starting_character = 0;
	o.boost_mode = boost_mode;
	o.logic_difficulty = difficulty;
	o.itemsanity = itemsanity;
	return o;
}

// Relic Race Perfect: the track's Sapphire rule (Trophy Race incl. finish and
// difficulty terms, stage 2), no tier term, N. Gin Labs USF.
struct PerfectRow
{
	const char *label;
	int level, boost_mode, difficulty, sk, boosts, pad, trophy, stage2, want;
};
static const PerfectRow perfect_rows[] = {
	{"Crash Cove, medium, 1 boost: open",                         3, 1, 1, 0, 1, 1, 1, 1, 1},
	{"Crash Cove, medium, 0 boosts: inherits the Trophy difficulty term", 3, 1, 1, 0, 0, 1, 1, 1, 0},
	{"Crash Cove, hard, 0 boosts: no Gold/Platinum tier term",    3, 1, 2, 0, 0, 1, 1, 1, 1},
	{"Crash Cove, stage 2 not met: blocked",                      3, 1, 2, 0, 3, 1, 1, 0, 0},
	{"Crash Cove, pad closed: blocked",                           3, 1, 2, 0, 3, 0, 1, 1, 0},
	{"N. Gin Labs, hard, 1 boost: crate term needs USF",         11, 1, 2, 0, 1, 1, 1, 1, 0},
	{"N. Gin Labs, hard, 2 boosts: open",                        11, 1, 2, 0, 2, 1, 1, 1, 1},
	{"N. Gin Labs, boost pack off: vacuous",                     11, 0, 2, 0, 0, 1, 1, 1, 1},
	{"Hot Air Skyway, hard, 1 boost: Trophy finish term (USF)",   7, 1, 2, 0, 1, 1, 1, 1, 0},
	{"Oxide Station, hard knowledge, 0 boosts: finish escape",   13, 1, 2, 2, 0, 1, 1, 1, 1},
	{"Slide Coliseum, no trial Trophy Race: pad access only",    16, 1, 1, 0, 0, 1, 0, 0, 1},
	{"Slide Coliseum, trial Trophy Race, medium, 0 boosts: blocked", 16, 1, 1, 0, 0, 1, 1, 1, 0},
	{"Turbo Track, trial Trophy Race, hard, 0 boosts: open",     17, 1, 2, 0, 0, 1, 1, 1, 1},
	{"Turbo Track, no trial Trophy Race, pad closed: blocked",   17, 1, 2, 0, 0, 0, 0, 1, 0},
};

static void test_relic_perfect(void)
{
	for (const PerfectRow &r : perfect_rows)
	{
		AP_VerifyOptions o = opts(r.boost_mode, r.difficulty, 0);
		int items[AP_VF_ITEM_COUNT];
		std::memset(items, 0, sizeof items);
		o.shortcut_knowledge = r.sk;
		items[AP_VF_BOOST_SHARED] = r.boosts;
		ok(AP_VerifyRelicPerfect(&o, items, r.level, -1, r.pad, r.trophy, r.stage2)
		   == r.want, std::string("perfect: ") + r.label);
	}
	ok(AP_VerifyRelicPerfectBoostMin(11) == 2 && AP_VerifyRelicPerfectBoostMin(3) == 0 &&
	   AP_VerifyRelicPerfectBoostMin(13) == 0 && AP_VerifyRelicPerfectBoostMin(16) == 0,
	   "perfect: crate term is N. Gin Labs only");
	// Racer-aware: a racer-locked Labs pad reads that racer's boost chain.
	{
		AP_VerifyOptions o = opts(2, 2, 0);
		int items[AP_VF_ITEM_COUNT];
		std::memset(items, 0, sizeof items);
		items[AP_VF_CHARACTER_FIRST + AP_VerifyRosterSlot(3)] = 1; // unlock Coco
		items[AP_VF_PC_FIRST + AP_VerifyRosterSlot(0) * 4] = 2;   // Crash has USF
		ok(!AP_VerifyRelicPerfect(&o, items, 11, 3, 1, 1, 1),
		   "perfect: Labs locked to Coco, only Crash has USF: blocked");
		items[AP_VF_PC_FIRST + AP_VerifyRosterSlot(3) * 4] = 2;
		ok(AP_VerifyRelicPerfect(&o, items, 11, 3, 1, 1, 1),
		   "perfect: Labs locked to Coco with Coco's USF: open");
	}
}

// Trial Trophy Race: a ruled-group Trophy Race (difficulty term, no finish).
struct TrialTrophyRow
{
	const char *label;
	int level, boost_mode, difficulty, itemsanity, boosts, families, want;
};
static const TrialTrophyRow trial_trophy_rows[] = {
	{"Slide Coliseum, medium, 0 boosts: blocked",           16, 1, 1, 0, 0, 0, 0},
	{"Slide Coliseum, medium, 1 boost: open",               16, 1, 1, 0, 1, 0, 1},
	{"Turbo Track, easy, 0 boosts: blocked",                17, 1, 0, 0, 0, 0, 0},
	{"Turbo Track, hard, 0 boosts: open",                   17, 1, 2, 0, 0, 0, 1},
	{"Turbo Track, easy, itemsanity, three families: open", 17, 1, 0, 1, 0, 3, 1},
	{"Slide Coliseum, boost pack off: vacuous",             16, 0, 0, 0, 0, 0, 1},
};

static void test_trial_trophy(void)
{
	static const int family_weapon[5] = {6, 2, 1, 8, 7};
	for (const TrialTrophyRow &r : trial_trophy_rows)
	{
		AP_VerifyOptions o = opts(r.boost_mode, r.difficulty, r.itemsanity);
		int items[AP_VF_ITEM_COUNT];
		std::memset(items, 0, sizeof items);
		items[AP_VF_BOOST_SHARED] = r.boosts;
		for (int f = 0; f < r.families; f++)
			items[AP_VF_WEAPON_FIRST + family_weapon[f]] = 1;
		ok(AP_VerifyTrophyCapabilityGate(&o, items, r.level, -1) == r.want,
		   std::string("trial Trophy: ") + r.label);
	}
}

// Trial CTR Token Challenge: Trophy Race + stage 2 + first-boost floor +
// lettersanity modes 2/3 letter items.
struct TrialCtrRow
{
	const char *label;
	int boost_mode, difficulty, boosts, pad, stage2, letter_mode, present_c, have_c, want;
};
static const TrialCtrRow trial_ctr_rows[] = {
	{"hard, 0 boosts: first-boost floor blocks",         1, 2, 0, 1, 1, 0, 0, 0, 0},
	{"hard, 1 boost: open",                              1, 2, 1, 1, 1, 0, 0, 0, 1},
	{"medium, 1 boost: open",                            1, 1, 1, 1, 1, 0, 0, 0, 1},
	{"boost pack off: vacuous",                          0, 0, 0, 1, 1, 0, 0, 0, 1},
	{"stage 2 not met: blocked",                         1, 2, 1, 1, 0, 0, 0, 0, 0},
	{"pad closed: blocked",                              1, 2, 1, 0, 1, 0, 0, 0, 0},
	{"lettersanity 2, C selected, C item missing",       1, 2, 1, 1, 1, 2, 1, 0, 0},
	{"lettersanity 2, C selected, C item held",          1, 2, 1, 1, 1, 2, 1, 1, 1},
	{"lettersanity 2, C not selected, no items",         1, 2, 1, 1, 1, 2, 0, 0, 1},
	{"lettersanity 3, only C held: blocked",             1, 2, 1, 1, 1, 3, 0, 1, 0},
	{"lettersanity 1: no item term",                     1, 2, 1, 1, 1, 1, 1, 0, 1},
};

static void test_trial_ctr(void)
{
	for (const TrialCtrRow &r : trial_ctr_rows)
	{
		AP_VerifyOptions o = opts(r.boost_mode, r.difficulty, 0);
		AP_VerifyLetterSet ls;
		int items[AP_VF_ITEM_COUNT];
		std::memset(items, 0, sizeof items);
		std::memset(&ls, 0, sizeof ls);
		items[AP_VF_BOOST_SHARED] = r.boosts;
		ls.mode = r.letter_mode;
		ls.present[0] = r.present_c;
		ls.item[0] = 190; ls.item[1] = 191; ls.item[2] = 192;
		if (r.have_c)
			items[190] = 1;
		for (int level = 16; level <= 17; level++)
			ok(AP_VerifyTrialTokenChallenge(&o, items, level, -1, r.pad, r.stage2, &ls)
			   == r.want, std::string("trial CTR ") + std::to_string(level) + ": " + r.label);
	}
	// The trial CTR inherits the medium difficulty term from its Trophy Race,
	// and with Itemsanity three weapon families stand in for it there, but not
	// for the challenge's own first-boost floor.
	{
		AP_VerifyOptions o = opts(1, 1, 1);
		AP_VerifyLetterSet ls;
		int items[AP_VF_ITEM_COUNT];
		std::memset(items, 0, sizeof items);
		std::memset(&ls, 0, sizeof ls);
		items[AP_VF_WEAPON_FIRST + 6] = items[AP_VF_WEAPON_FIRST + 2] =
			items[AP_VF_WEAPON_FIRST + 1] = 1;
		ok(AP_VerifyTrophyCapabilityGate(&o, items, 16, -1) &&
		   !AP_VerifyTrialTokenChallenge(&o, items, 16, -1, 1, 1, &ls),
		   "trial CTR: weapon families open the Trophy Race, not the boost floor");
	}
}

// Hit <Character>: hit_character._install_target.
struct HitRow
{
	const char *label;
	int target, itemsanity, weapon, unlocked_extra, route_level, lock;
	int boss, trigger, fallback, keys, want;
};
static const HitRow hit_rows[] = {
	// default racers (engine ids 0..7): ordinary routes only
	{"default Cortex, open unlocked route, Crash selectable",   1, 0, -1, -1,  3, -1, 0, 0, 0, 0, 1},
	{"default Cortex, no open route",                           1, 0, -1, -1, -1, -1, 0, 0, 0, 0, 0},
	{"default Crash = starter, nobody else unlocked",         0, 0, -1, -1,  3, -1, 0, 0, 0, 0, 0},
	{"default Crash, Coco unlocked",                          0, 0, -1,  3,  3, -1, 0, 0, 0, 0, 1},
	{"default Cortex, pad locked to Cortex: proves nothing",      1, 0, -1, -1,  3,  1, 0, 0, 0, 0, 0},
	{"default Cortex, pad locked to Crash",                     1, 0, -1, -1,  3,  0, 0, 0, 0, 0, 1},
	{"default Cortex, pad locked to locked Polar",              1, 0, -1, -1,  3,  6, 0, 0, 0, 0, 0},
	{"default Cortex, a boss route is not consulted",           1, 0, -1, -1, -1, -1, 1, 1, 0, 0, 0},
	{"default Cortex, trial route (Slide Coliseum race on)",    1, 0, -1, -1, 16, -1, 0, 0, 0, 0, 1},
	// Itemsanity: a hit method item (Bomb, Missile, Bomb x3, Missile x3)
	{"itemsanity, no weapon",                                 1, 1, -1, -1,  3, -1, 0, 0, 0, 0, 0},
	{"itemsanity, Mask is not a hit method",                  1, 1,  6, -1,  3, -1, 0, 0, 0, 0, 0},
	{"itemsanity, Bomb x3",                                   1, 1,  9, -1,  3, -1, 0, 0, 0, 0, 1},
	{"itemsanity, Missile",                                   1, 1,  2, -1,  3, -1, 0, 0, 0, 0, 1},
	{"itemsanity, guest boss reached, no weapon",             8, 1, -1, -1, -1, -1, 1, 0, 0, 0, 0},
	// guests (8..15)
	{"guest Pinstripe, own boss race reached, no route",      8, 0, -1, -1, -1, -1, 1, 0, 0, 0, 1},
	{"guest Pinstripe, boss not reached, trigger + route",    8, 0, -1, -1,  3, -1, 0, 1, 0, 0, 1},
	{"guest N. Tropy, trigger reached, route",               12, 0, -1, -1,  3, -1, 0, 1, 0, 0, 1},
	{"guest N. Tropy, trigger not reached",                  12, 0, -1, -1,  3, -1, 0, 0, 0, 0, 0},
	{"guest N. Tropy, trigger reached, no route",            12, 0, -1, -1, -1, -1, 0, 1, 0, 0, 0},
	{"guest N. Tropy fallback 3 Keys, 2 held",               12, 0, -1, -1,  3, -1, 0, 0, 3, 2, 0},
	{"guest N. Tropy fallback 3 Keys, 3 held",               12, 0, -1, -1,  3, -1, 0, 0, 3, 3, 1},
	{"guest fallback replaces the trigger term entirely",    12, 0, -1, -1,  3, -1, 0, 1, 3, 2, 0},
	{"guest Fake Crash, pad locked to Fake Crash",           14, 0, -1, -1,  3, 14, 0, 1, 0, 0, 0},
};

static void test_hit(void)
{
	for (const HitRow &r : hit_rows)
	{
		AP_VerifyOptions o = opts(1, 1, r.itemsanity);
		AP_VerifyHitInputs in;
		int items[AP_VF_ITEM_COUNT];
		std::memset(items, 0, sizeof items);
		std::memset(&in, 0, sizeof in);
		for (int l = 0; l < AP_VF_HIT_LEVEL_COUNT; l++)
			in.pad_lock[l] = -1;
		if (r.weapon >= 0)
			items[AP_VF_WEAPON_FIRST + r.weapon] = 1;
		if (r.unlocked_extra >= 0)
			items[AP_VF_CHARACTER_FIRST + AP_VerifyRosterSlot(r.unlocked_extra)] = 1;
		if (r.route_level >= 0)
		{
			in.route_open[r.route_level] = 1;
			in.pad_lock[r.route_level] = (signed char)r.lock;
		}
		in.boss_reached = r.boss;
		in.trigger_reached = r.trigger;
		in.fallback_keys = r.fallback;
		in.keys_held = r.keys;
		ok(AP_VerifyHitTarget(&o, items, r.target, &in) == r.want,
		   std::string("Hit: ") + r.label);
	}
	// Character unlocks off: every racer is selectable, including for the
	// starter as a target.
	{
		AP_VerifyOptions o = opts(1, 1, 0);
		AP_VerifyHitInputs in;
		int items[AP_VF_ITEM_COUNT];
		o.character_unlocks = 0;
		std::memset(items, 0, sizeof items);
		std::memset(&in, 0, sizeof in);
		for (int l = 0; l < AP_VF_HIT_LEVEL_COUNT; l++)
			in.pad_lock[l] = -1;
		in.route_open[5] = 1;
		ok(AP_VerifyHitTarget(&o, items, 0, &in), "Hit: unlocks off, starter target, open");
	}
}

int main(void)
{
	test_coverage();
	test_relic_perfect();
	test_trial_trophy();
	test_trial_ctr();
	test_hit();
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
