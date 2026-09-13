// Native parser acceptance for the schema-14 `hit_character_encounters` block
// (ticket 05). Compiles the REAL parser -- ap/ap_seedcfg.cpp is linked in, so
// there is no reimplementation to drift -- and drives it against the committed
// fixture copied verbatim from the apworld's actual fill_slot_data output for
// seed 2101 (tools/fixtures/ctr_hit_character_seed2101.json).
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP -I ap/vendor/json/include \
//       tools/test-hit-character-seedcfg.cpp ap/ap_seedcfg.cpp \
//       -o /tmp/test-hit-character-seedcfg && /tmp/test-hit-character-seedcfg
//
// What it pins, in one sentence: an enabled seed either hands this client a
// completely readable encounter table (every list in wire order) or it is
// refused whole -- the parser never half-activates, and a disabled/legacy seed
// stays inert rather than being called a rejection.
//
// Coverage:
//   1. valid round-trip of every list/mapping in the real fixture,
//   2. reordered-but-valid candidate lists survive verbatim,
//   3. malformed shapes / wrong types / out-of-range bounds, per field,
//   4. absent / contradictory / unknown-schema / present-null handling,
//   5. seed transitions (valid -> invalid -> valid -> absent clear old state),
//   6. boss identity substitution round-trips (the identity table is data).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

// ap_cfg_log fans out to this; the C unity build is not linked here.
extern "C" void AP_LogLine(const char *) {}

static nlohmann::json g_fixture;
static int g_checks = 0;
static int g_failures = 0;

static void expect(bool ok, const char *what)
{
	g_checks++;
	if (!ok)
	{
		std::printf("FAIL: %s\n", what);
		g_failures++;
	}
}

static void expect_eq(long long got, long long want, const char *what)
{
	g_checks++;
	if (got != want)
	{
		std::printf("FAIL: %s (got %lld, want %lld)\n", what, got, want);
		g_failures++;
	}
}

// Element-wise comparison with the owning key/index in the failure message.
static void expect_eq_at(long long got, long long want, const char *what,
                         const char *key, int idx)
{
	g_checks++;
	if (got != want)
	{
		std::printf("FAIL: %s[%s][%d] (got %lld, want %lld)\n",
		            what, key, idx, got, want);
		g_failures++;
	}
}

static nlohmann::json fx(void)
{
	return g_fixture; // deep copy (nlohmann values own their storage)
}

// Parse `d` and require the seed to be REFUSED: rejection flag set, a
// non-empty reason, the whole config inactive, and no encounter data reachable.
static void expect_reject(const nlohmann::json &d, const char *what)
{
	ap_seedcfg_parse_json(d);
	g_checks++;
	if (!ap_seedcfg_rejected())
	{
		std::printf("FAIL: expected rejection: %s\n", what);
		g_failures++;
	}
	g_checks++;
	if (ctr_cfg.schema_version != 0)
	{
		std::printf("FAIL: rejected seed left schema_version=%d: %s\n",
		            ctr_cfg.schema_version, what);
		g_failures++;
	}
	g_checks++;
	if (ap_seedcfg_hit_encounters() != NULL)
	{
		std::printf("FAIL: rejected seed exposed encounter data: %s\n", what);
		g_failures++;
	}
	g_checks++;
	if (ap_seedcfg_reject_reason()[0] == '\0')
	{
		std::printf("FAIL: rejection has no reason: %s\n", what);
		g_failures++;
	}
}

// Parse `d` and require it to be ACCEPTED and active.
static void expect_accept(const nlohmann::json &d, const char *what)
{
	ap_seedcfg_parse_json(d);
	g_checks++;
	if (ap_seedcfg_rejected())
	{
		std::printf("FAIL: unexpected rejection (%s): %s\n",
		            ap_seedcfg_reject_reason(), what);
		g_failures++;
	}
	g_checks++;
	if (ctr_cfg.schema_version < 1)
	{
		std::printf("FAIL: accepted seed left schema_version=%d: %s\n",
		            ctr_cfg.schema_version, what);
		g_failures++;
	}
}

static void expect_inert(const nlohmann::json &d, const char *what)
{
	expect_accept(d, what);
	g_checks++;
	if (ap_seedcfg_hit_encounters() != NULL)
	{
		std::printf("FAIL: disabled/legacy seed exposed encounter data: %s\n", what);
		g_failures++;
	}
}

// ---------------------------------------------------------------------------

// The three candidate lists of one destination, compared element-for-element
// and in order against the emitted fixture.
static void compare_candidates(const nlohmann::json &want, const ctr_hit_candidates &got,
                               const char *key)
{
	static const char *kLists[3] = {"base", "pinned", "reserve"};
	const ctr_hit_list *gotLists[3] = {&got.base, &got.pinned, &got.reserve};
	for (int l = 0; l < 3; l++)
	{
		const nlohmann::json &w = want[kLists[l]];
		expect_eq(gotLists[l]->count, (long long)w.size(), "candidate list count");
		for (int i = 0; i < (int)w.size(); i++)
			expect_eq_at(gotLists[l]->ids[i], (long long)w[i].get<long long>(),
			             kLists[l], key, i);
	}
}

static void test_valid_roundtrip(void)
{
	expect_accept(g_fixture, "fixture");

	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	expect(h != NULL, "fixture exposes encounters");
	if (!h)
		return;

	const nlohmann::json &b = g_fixture["hit_character_encounters"];

	expect_eq(h->schema, CTR_CFG_HIT_BLOCK_SCHEMA_KNOWN, "block schema 1");
	expect_eq(h->enabled, 1, "scalar enabled");
	expect_eq(h->seen, 1, "block seen");
	expect_eq(h->valid, 1, "block valid");

	// policy, every field against the fixture.
	expect_eq((long long)h->seed, (long long)b["policy"]["seed"].get<unsigned long long>(),
	          "policy.seed verbatim");
	expect_eq(h->guest_slots, b["policy"]["guest_slots"].get<int>(), "policy.guest_slots");
	expect_eq(h->boss_eligible_after_clear,
	          b["policy"]["boss_eligible_after_clear"].get<bool>() ? 1 : 0,
	          "policy.boss_eligible_after_clear");

	// locations: every engine id -> the emitted code, in canonical order.
	for (int id = 0; id < CTR_CFG_HIT_CHARACTER_COUNT; id++)
	{
		const std::string key = std::to_string(id);
		expect_eq(h->locations[id], (long long)b["locations"][key].get<long long>(),
		          "location code");
	}

	// tracks: every destination, every list element, in emitted order.
	for (int lid = 0; lid < CTR_CFG_HIT_TRACK_COUNT; lid++)
	{
		const std::string key = std::to_string(lid);
		compare_candidates(b["tracks"][key], h->tracks[lid], key.c_str());
	}

	// cups: every cup id, every list element, in emitted order.
	for (int c = 0; c < CTR_CFG_HIT_CUP_COUNT; c++)
	{
		const std::string key = std::to_string(100 + c);
		compare_candidates(b["cups"][key], h->cups[c], key.c_str());
	}

	// unlock_triggers: every guest, kind + every win code in emitted order.
	for (int g = 8; g <= 15; g++)
	{
		const std::string key = std::to_string(g);
		const nlohmann::json &w = b["unlock_triggers"][key];
		const ctr_hit_trigger &got = h->triggers[g - 8];
		const int wantKind = w["kind"].get<std::string>() == "boss"
		                         ? CTR_CFG_HIT_KIND_BOSS
		                         : CTR_CFG_HIT_KIND_TRACK;
		expect_eq(got.kind, wantKind, "trigger kind");
		expect_eq(got.count, (long long)w["any_of"].size(), "trigger count");
		for (int i = 0; i < (int)w["any_of"].size(); i++)
			expect_eq_at(got.any_of[i], (long long)w["any_of"][i].get<long long>(),
			             "any_of", key.c_str(), i);
	}

	// bosses: every canonical key -> the emitted engine identity.
	for (int i = 0; i < CTR_CFG_HIT_BOSS_COUNT; i++)
	{
		const std::string key = std::to_string(35011100 + i);
		expect_eq(h->boss_identity[i], b["bosses"][key].get<int>(), "boss identity");
	}
}

static void test_reordered_lists_survive(void)
{
	nlohmann::json d = fx();
	auto &t0 = d["hit_character_encounters"]["tracks"]["0"];
	std::reverse(t0["base"].begin(), t0["base"].end());
	std::reverse(t0["reserve"].begin(), t0["reserve"].end());
	expect_accept(d, "reordered lists");

	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	expect(h != NULL, "reordered exposes encounters");
	if (!h)
		return;
	static const int wantBase[8] = {3, 2, 1, 0, 7, 6, 5, 4};
	static const int wantRes[8] = {11, 10, 9, 8, 15, 14, 13, 12};
	for (int i = 0; i < 8; i++)
	{
		expect_eq(h->tracks[0].base.ids[i], wantBase[i], "reordered base verbatim");
		expect_eq(h->tracks[0].reserve.ids[i], wantRes[i], "reordered reserve verbatim");
	}
}

static void test_scalar_and_block_presence(void)
{
	{
		nlohmann::json d = fx();
		d["ctr_options"]["hit_character"] = false;
		d.erase("hit_character_encounters");
		expect_inert(d, "disabled scalar + absent block");
		expect_eq(ctr_cfg.hit.locations[0], -1, "disabled clears location state");
	}
	{
		nlohmann::json d = fx();
		d.erase("hit_character_encounters"); // scalar stays true
		expect_reject(d, "enabled absence");
	}
	{
		nlohmann::json d = fx();
		d["ctr_options"]["hit_character"] = false; // block stays present
		expect_reject(d, "contradictory false scalar + present block");
	}
	{
		nlohmann::json d = fx();
		d["ctr_options"].erase("hit_character"); // block stays present
		expect_reject(d, "absent scalar + present block");
	}
	{
		nlohmann::json d = fx();
		d["ctr_options"]["hit_character"] = 1;
		expect_reject(d, "non-bool scalar (int)");
	}
	{
		nlohmann::json d = fx();
		d["ctr_options"]["hit_character"] = "true";
		expect_reject(d, "non-bool scalar (string)");
	}
	{
		nlohmann::json d = fx();
		d["ctr_options"]["hit_character"] = nullptr;
		expect_reject(d, "null scalar");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"] = nullptr;
		expect_reject(d, "present null block is malformed");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"] = nlohmann::json::array();
		expect_reject(d, "array block");
	}
	{
		// Legacy: no scalar, no block, older schema -> inert, never rejected.
		nlohmann::json d;
		d["ctr_options"]["schema_version"] = 7;
		d["ctr_options"]["goal"] = 0;
		expect_inert(d, "legacy absence");
		expect_eq(ctr_cfg.schema_version, 7, "legacy schema stays active");
	}
}

static void test_block_schema_and_shape(void)
{
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["schema"] = 2;
		expect_reject(d, "unknown block schema");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["schema"] = true;
		expect_reject(d, "bool block schema");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"].erase("schema");
		expect_reject(d, "missing schema");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["extra"] = 1;
		expect_reject(d, "extra top-level block key");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"].erase("bosses");
		expect_reject(d, "missing bosses");
	}
}

static void test_locations(void)
{
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["locations"]["0"] = 35025001;
		expect_reject(d, "wrong canonical location code");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["locations"].erase("15");
		expect_reject(d, "missing location id");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["locations"]["16"] = 35025016;
		expect_reject(d, "extra location id");
	}
	{
		nlohmann::json d = fx();
		nlohmann::json &locs = d["hit_character_encounters"]["locations"];
		locs["00"] = locs["0"];
		locs.erase("0");
		expect_reject(d, "non-canonical location key");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["locations"]["0"] = true;
		expect_reject(d, "bool location code");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["locations"]["0"] = "35025000";
		expect_reject(d, "string location code");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["locations"]["0"] = -35025000;
		expect_reject(d, "negative location code");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["locations"] = nullptr;
		expect_reject(d, "null locations");
	}
}

static void test_policy(void)
{
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["seed"] = true;
		expect_reject(d, "bool seed");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["seed"] = -1;
		expect_reject(d, "negative seed");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["seed"] = 4294967296LL;
		expect_reject(d, "seed above uint32");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["seed"] = 0;
		expect_accept(d, "seed 0");
		expect_eq((long long)ap_seedcfg_hit_encounters()->seed, 0, "seed 0 stored");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["seed"] = 4294967295LL;
		expect_accept(d, "seed 4294967295");
		expect_eq((long long)ap_seedcfg_hit_encounters()->seed, 4294967295LL, "max seed stored");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["self_character"] = "seat_player";
		expect_reject(d, "wrong self_character");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["boss_eligible_after_clear"] = false;
		expect_reject(d, "false boss_eligible_after_clear");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["boss_eligible_after_clear"] = 1;
		expect_reject(d, "non-bool boss_eligible_after_clear");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["guest_slots"] = 2;
		expect_reject(d, "guest_slots 2");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["guest_slots"] = true;
		expect_reject(d, "bool guest_slots");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"].erase("policy");
		expect_reject(d, "missing policy");
	}
}

static void test_candidates(void)
{
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["0"]["base"] = {4, 4, 6, 7, 0, 1, 2, 3};
		expect_reject(d, "duplicate in base");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["0"]["base"] = {4, 5, 6, 7, 0, 1, 2};
		expect_reject(d, "short base");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["0"]["base"] = {4, 5, 6, 7, 0, 1, 2, 8};
		expect_reject(d, "base carries a non-default id");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["0"]["reserve"] = {12, 13, 14, 15, 8, 9, 10, 7};
		expect_reject(d, "reserve carries a default id");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["0"]["pinned"] = {14};
		expect_reject(d, "unapproved pin on track 0");
	}
	{
		// The approved pin is REQUIRED where the contract pins one.
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["3"]["pinned"] = nlohmann::json::array();
		expect_reject(d, "required Fake Crash pin on track 3 cannot be removed");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["8"]["pinned"] = nlohmann::json::array();
		expect_reject(d, "required Fake Crash pin on track 8 cannot be removed");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["1"]["pinned"] = {12};
		expect_reject(d, "wrong approved pin on track 1");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["cups"]["100"]["pinned"] = {14};
		expect_reject(d, "cup pin forbidden");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"].erase("0");
		expect_reject(d, "missing track key");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["18"] = d["hit_character_encounters"]["tracks"]["0"];
		expect_reject(d, "extra track key");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["0"].erase("reserve");
		expect_reject(d, "candidate missing reserve");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["0"]["extra"] = 1;
		expect_reject(d, "candidate extra key");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["0"]["base"] = {4, 5, 6, 7, 0, 1, 2, true};
		expect_reject(d, "bool in base list");
	}
}

static void test_triggers(void)
{
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["unlock_triggers"]["8"]["kind"] = "track";
		expect_reject(d, "wrong trigger kind");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["unlock_triggers"]["8"]["any_of"] = {35011104};
		expect_reject(d, "unapproved win code");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["any_of"] = {35016200};
		expect_reject(d, "short approved win set");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["any_of"] = {35016200, 35016200};
		expect_reject(d, "duplicate win code");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["unlock_triggers"]["8"]["any_of"] = nlohmann::json::array();
		expect_reject(d, "empty trigger list");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["unlock_triggers"]["16"] =
		    d["hit_character_encounters"]["unlock_triggers"]["15"];
		expect_reject(d, "non-guest trigger key");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["unlock_triggers"].erase("15");
		expect_reject(d, "missing guest trigger");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["unlock_triggers"]["8"].erase("kind");
		expect_reject(d, "trigger missing kind");
	}
}

static void test_bosses(void)
{
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["bosses"]["35011100"] = 16;
		expect_reject(d, "boss identity out of range");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["bosses"]["35011100"] = -1;
		expect_reject(d, "negative boss identity");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["bosses"]["35011100"] = true;
		expect_reject(d, "bool boss identity");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["bosses"].erase("35011105");
		expect_reject(d, "missing boss key");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["bosses"]["35011106"] = 15;
		expect_reject(d, "extra boss key");
	}
	{
		// Fix B: the retail identity table is the contract. A mismatched entry
		// is refused (visible incompatibility), not run with apworld logic and
		// native dispatch disagreeing about who appears.
		nlohmann::json d = fx();
		d["hit_character_encounters"]["bosses"]["35011100"] = 7;
		expect_reject(d, "altered boss identity refused");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["bosses"]["35011105"] = 3;
		expect_reject(d, "altered Oxide identity refused");
	}
	{
		// The retail table is accepted and stored verbatim.
		nlohmann::json d = fx();
		expect_accept(d, "retail boss table accepted");
		const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
		expect(h != NULL, "retail table exposes encounters");
		if (h)
		{
			expect_eq(h->boss_identity[0], 10, "retail Ripper Roo identity");
			expect_eq(h->boss_identity[1], 9, "retail Papu Papu identity");
			expect_eq(h->boss_identity[2], 11, "retail Komodo Joe identity");
			expect_eq(h->boss_identity[3], 8, "retail Pinstripe identity");
			expect_eq(h->boss_identity[4], 15, "retail Oxide identity");
			expect_eq(h->boss_identity[5], 15, "retail final Oxide identity");
		}
	}
}

static void test_seed_transitions(void)
{
	// valid -> invalid -> valid -> disabled: each step clears the previous state.
	expect_accept(g_fixture, "transition valid");
	expect(ap_seedcfg_hit_encounters() != NULL, "transition valid accessible");

	nlohmann::json bad = fx();
	bad["hit_character_encounters"]["schema"] = 2;
	expect_reject(bad, "transition invalid");
	expect_eq(ctr_cfg.hit.locations[0], -1, "invalid parse cleared old location");
	expect_eq(ctr_cfg.hit.valid, 0, "invalid parse cleared valid flag");

	expect_accept(g_fixture, "transition valid again");
	expect(ap_seedcfg_hit_encounters() != NULL, "transition re-accessible");

	nlohmann::json off = fx();
	off["ctr_options"]["hit_character"] = false;
	off.erase("hit_character_encounters");
	expect_inert(off, "transition disabled");
	expect_eq(ctr_cfg.hit.locations[0], -1, "disabled parse cleared old location");
	expect_eq(ctr_cfg.hit.valid, 0, "disabled parse cleared valid flag");
}

// The global-schema boundary (frozen 2026-09-12 23:18 ruling): an ENABLED
// feature requires an actual integer global schema >= 14; a present block must
// be validated even with no ctr_options / schema 0. A future global schema >= 14
// is admitted when the block schema is known.
static void test_global_schema_boundary(void)
{
	for (int s : {0, 13})
	{
		nlohmann::json d = fx();
		d["ctr_options"]["schema_version"] = s;
		expect_reject(d, "enabled feature under pre-14 global schema");
	}
	{
		nlohmann::json d = fx();
		d["ctr_options"]["schema_version"] = true;
		expect_reject(d, "enabled feature under bool global schema");
	}
	{
		nlohmann::json d = fx();
		d["ctr_options"].erase("schema_version");
		expect_reject(d, "enabled feature under absent global schema");
	}
	{
		nlohmann::json d = fx();
		d.erase("ctr_options"); // present block, no enabling options at all
		expect_reject(d, "present block without ctr_options cannot bypass");
	}
	{
		nlohmann::json d = fx();
		d.erase("ctr_options");
		d["hit_character_encounters"] = nullptr;
		expect_reject(d, "present null block without ctr_options cannot bypass");
	}
	{
		nlohmann::json d = fx();
		d["ctr_options"]["schema_version"] = 15; // future global, known block 1
		expect_accept(d, "future global schema >=14 with known block admitted");
		expect_eq(ctr_cfg.schema_newer, 1, "future global schema raises the banner");
		expect(ap_seedcfg_hit_encounters() != NULL, "future global block stays active");
	}
}

// valid -> invalid -> legacy -> valid: every transition clears the previous
// encounter cache and rejection state (no merging, no stale block).
static void test_cache_clearing_sequence(void)
{
	expect_accept(g_fixture, "seq: valid");
	expect(ap_seedcfg_hit_encounters() != NULL, "seq: valid active");
	expect_eq(ctr_cfg.hit.locations[0], 35025000, "seq: valid location present");

	nlohmann::json bad = fx();
	bad["hit_character_encounters"]["locations"]["0"] = 1;
	expect_reject(bad, "seq: invalid");
	expect_eq(ctr_cfg.hit.valid, 0, "seq: invalid cleared valid flag");
	expect_eq(ctr_cfg.hit.locations[0], -1, "seq: invalid cleared location");
	expect(ap_seedcfg_hit_encounters() == NULL, "seq: invalid exposes nothing");

	nlohmann::json legacy;
	legacy["ctr_options"]["schema_version"] = 7;
	legacy["ctr_options"]["goal"] = 0;
	expect_inert(legacy, "seq: legacy");
	expect_eq(ctr_cfg.hit.valid, 0, "seq: legacy keeps the feature off");
	expect_eq(ctr_cfg.seed_rejected, 0, "seq: legacy is not a rejection");
	expect_eq(ctr_cfg.hit.locations[0], -1, "seq: legacy keeps no stale location");

	expect_accept(g_fixture, "seq: valid again");
	expect(ap_seedcfg_hit_encounters() != NULL, "seq: valid re-active");
	expect_eq(ctr_cfg.hit.locations[0], 35025000, "seq: valid location restored");
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "tools/fixtures/ctr_hit_character_seed2101.json";
	std::ifstream in(path);
	if (!in)
	{
		std::fprintf(stderr, "cannot open fixture: %s\n", path);
		return 2;
	}
	try
	{
		in >> g_fixture;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "fixture is not valid JSON: %s\n", e.what());
		return 2;
	}

	test_valid_roundtrip();
	test_reordered_lists_survive();
	test_scalar_and_block_presence();
	test_block_schema_and_shape();
	test_locations();
	test_policy();
	test_candidates();
	test_triggers();
	test_bosses();
	test_seed_transitions();
	test_global_schema_boundary();
	test_cache_clearing_sequence();

	std::printf("%s: %d checks, %d failures\n",
	            g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
