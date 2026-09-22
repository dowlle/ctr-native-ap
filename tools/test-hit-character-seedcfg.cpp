// Native parser acceptance for the schema-16 `hit_character_encounters` block
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
//   6. boss identity substitution round-trips (the identity table is data),
//   7. block schema 3's optional per-guest `fallback_keys` (accepted shapes, the
//      fixed count table, boss guests, and block schema 2 staying admissible).

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

// One destination's order, compared element-for-element and in order against
// the emitted fixture.
static void compare_order(const nlohmann::json &want, const ctr_hit_order &got,
                          const char *key)
{
	const nlohmann::json &w = want["order"];
	expect_eq((long long)w.size(), CTR_CFG_HIT_CHARACTER_COUNT, "order has sixteen ids");
	for (int i = 0; i < (int)w.size() && i < CTR_CFG_HIT_CHARACTER_COUNT; i++)
		expect_eq_at(got.ids[i], (long long)w[i].get<long long>(), "order", key, i);
}

static void test_valid_roundtrip(void)
{
	expect_accept(g_fixture, "fixture");

	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	expect(h != NULL, "fixture exposes encounters");
	if (!h)
		return;

	const nlohmann::json &b = g_fixture["hit_character_encounters"];

	expect_eq(h->schema, b["schema"].get<int>(), "block schema verbatim");
	expect_eq(CTR_CFG_HIT_BLOCK_SCHEMA_MIN, 2, "this build still admits block schema 2");
	expect_eq(CTR_CFG_HIT_BLOCK_SCHEMA_KNOWN, 3, "this build knows block schema 3");
	expect_eq(h->enabled, 1, "scalar enabled");
	expect_eq(h->seen, 1, "block seen");
	expect_eq(h->valid, 1, "block valid");

	// policy, every field against the fixture.
	expect_eq((long long)h->seed, (long long)b["policy"]["seed"].get<unsigned long long>(),
	          "policy.seed verbatim");
	expect_eq(h->max_guests, b["policy"]["max_guests"].get<int>(), "policy.max_guests");
	expect(b["policy"]["draw"].get<std::string>() == "unhit_first_rotation",
	       "fixture policy.draw");

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
		compare_order(b["tracks"][key], h->tracks[lid], key.c_str());
	}

	// cups: every cup id, every list element, in emitted order.
	for (int c = 0; c < CTR_CFG_HIT_CUP_COUNT; c++)
	{
		const std::string key = std::to_string(100 + c);
		compare_order(b["cups"][key], h->cups[c], key.c_str());
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
	auto &t0 = d["hit_character_encounters"]["tracks"]["0"]["order"];
	t0 = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
	expect_accept(d, "reordered order");

	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	expect(h != NULL, "reordered exposes encounters");
	if (!h)
		return;
	for (int i = 0; i < 16; i++)
		expect_eq(h->tracks[0].ids[i], 15 - i, "reordered order verbatim");
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
		d["hit_character_encounters"]["schema"] = 4;
		expect_reject(d, "unknown block schema 4");
		expect(std::string(ap_seedcfg_reject_reason()).find("known block schema (2 or 3)") !=
		           std::string::npos,
		       "the refusal names both accepted block schemas");
	}
	{
		// A superseded schema-1 (pinned) block is refused visibly, with the
		// reason naming the known block schema.
		nlohmann::json d = fx();
		auto &blk = d["hit_character_encounters"];
		blk["schema"] = 1;
		blk["policy"] = {{"seed", 1}, {"self_character", "never_seat_player"},
		                 {"boss_eligible_after_clear", true}, {"guest_slots", 1}};
		for (auto grp : {"tracks", "cups"})
			for (auto it = blk[grp].begin(); it != blk[grp].end(); ++it)
				it.value() = {{"base", {0, 1, 2, 3, 4, 5, 6, 7}},
				              {"pinned", nlohmann::json::array()},
				              {"reserve", {8, 9, 10, 11, 12, 13, 14, 15}}};
		expect_reject(d, "superseded block schema 1");
		expect(std::string(ap_seedcfg_reject_reason()).find("known block schema (2 or 3)") !=
		           std::string::npos,
		       "schema-1 refusal names the accepted block schemas");
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
		d["hit_character_encounters"]["policy"]["draw"] = "pins";
		expect_reject(d, "wrong draw");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"].erase("draw");
		expect_reject(d, "missing draw");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["max_guests"] = 2;
		expect_reject(d, "max_guests 2");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["max_guests"] = true;
		expect_reject(d, "bool max_guests");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["policy"]["guest_slots"] = 1;
		expect_reject(d, "schema-1 policy key on a schema-2 block");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"].erase("policy");
		expect_reject(d, "missing policy");
	}
}

static void test_orders(void)
{
	auto bad = [](const char *grp, const char *key, nlohmann::json v, const char *what) {
		nlohmann::json d = fx();
		d["hit_character_encounters"][grp][key] = v;
		expect_reject(d, what);
	};
	nlohmann::json ok = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	bad("tracks", "0", {{"order", {0, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}}},
	    "duplicate id in order");
	bad("tracks", "0", {{"order", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}}},
	    "short order");
	bad("tracks", "0", {{"order", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 1}}},
	    "long order");
	bad("tracks", "0", {{"order", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16}}},
	    "id out of range in order");
	bad("tracks", "0", {{"order", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, true}}},
	    "bool in order");
	bad("tracks", "0", {{"order", ok}, {"pinned", nlohmann::json::array()}},
	    "extra key beside order");
	bad("tracks", "0", ok, "bare list instead of an order object");
	bad("tracks", "0", {{"base", {0, 1, 2, 3, 4, 5, 6, 7}},
	                    {"pinned", nlohmann::json::array()},
	                    {"reserve", {8, 9, 10, 11, 12, 13, 14, 15}}},
	    "schema-1 candidate shape");
	bad("cups", "100", {{"order", {0, 1, 2, 3}}}, "short cup order");
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"].erase("17");
		expect_reject(d, "missing track key");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["tracks"]["18"] = {{"order", ok}};
		expect_reject(d, "extra track key");
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["cups"].erase("104");
		expect_reject(d, "missing cup key");
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

// The committed fixture is a block schema 2 seed (alpha2). This is the same
// seed re-labelled as block schema 3: the shape is identical until a trigger
// entry carries `fallback_keys`.
static nlohmann::json fx3(void)
{
	nlohmann::json d = fx();
	d["hit_character_encounters"]["schema"] = 3;
	return d;
}

// Block schema 3: the optional per-guest Key fallback. The counts are a frozen
// table (14 -> 1, 13 -> 2, 12 -> 3, 15 -> 4), never a per-seed roll, so a value
// that disagrees means a mismatched apworld and refuses the seed.
static void test_fallback_keys(void)
{
	// A block 3 seed with no fallback anywhere is the ordinary case.
	{
		nlohmann::json d = fx3();
		expect_accept(d, "block schema 3 without any fallback");
		const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
		expect(h != NULL, "block 3 exposes encounters");
		if (h)
		{
			expect_eq(h->schema, 3, "block schema 3 stored");
			for (int g = 8; g <= 15; g++)
				expect_eq(h->triggers[g - 8].fallback_keys, 0, "no fallback parsed");
		}
	}

	// Each of the four fallback guests, at its own fixed count.
	{
		const int guests[4] = {14, 13, 12, 15};
		const int keys[4] = {1, 2, 3, 4};
		for (int i = 0; i < 4; i++)
		{
			nlohmann::json d = fx3();
			const std::string key = std::to_string(guests[i]);
			d["hit_character_encounters"]["unlock_triggers"][key]["fallback_keys"] = keys[i];
			expect_accept(d, "fallback guest accepted");
			const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
			expect(h != NULL, "fallback seed exposes encounters");
			if (!h)
				continue;
			expect_eq(h->triggers[guests[i] - 8].fallback_keys, keys[i],
			          "fallback count parsed");
			// kind and any_of keep today's strict validation, verbatim.
			expect_eq(h->triggers[guests[i] - 8].count,
			          (long long)d["hit_character_encounters"]["unlock_triggers"][key]
			              ["any_of"].size(),
			          "fallback entry keeps its win set");
			for (int g = 8; g <= 15; g++)
				if (g != guests[i])
					expect_eq(h->triggers[g - 8].fallback_keys, 0,
					          "other guests keep no fallback");
		}
	}

	// All four at once, which is what a maximally displaced seed emits.
	{
		nlohmann::json d = fx3();
		auto &ug = d["hit_character_encounters"]["unlock_triggers"];
		ug["12"]["fallback_keys"] = 3;
		ug["13"]["fallback_keys"] = 2;
		ug["14"]["fallback_keys"] = 1;
		ug["15"]["fallback_keys"] = 4;
		expect_accept(d, "all four fallback guests");
		const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
		if (h)
		{
			expect_eq(h->triggers[12 - 8].fallback_keys, 3, "N. Tropy at 3");
			expect_eq(h->triggers[13 - 8].fallback_keys, 2, "Penta Penguin at 2");
			expect_eq(h->triggers[14 - 8].fallback_keys, 1, "Fake Crash at 1");
			expect_eq(h->triggers[15 - 8].fallback_keys, 4, "Nitros Oxide at 4");
		}
	}

	// The four boss guests never carry a fallback: they always have a boss race.
	for (int g = 8; g <= 11; g++)
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"][std::to_string(g)]
		 ["fallback_keys"] = 1;
		expect_reject(d, "fallback on a boss guest");
	}

	// Type-exact integer, range 1..4, and the fixed table value.
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = 0;
		expect_reject(d, "fallback_keys 0");
	}
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = 5;
		expect_reject(d, "fallback_keys 5");
	}
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = -1;
		expect_reject(d, "fallback_keys -1");
	}
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = 3.0;
		expect_reject(d, "fallback_keys 3.0 (float)");
	}
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = "3";
		expect_reject(d, "fallback_keys \"3\" (string)");
	}
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = true;
		expect_reject(d, "fallback_keys true (bool)");
	}
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = nullptr;
		expect_reject(d, "fallback_keys null");
	}
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = 2;
		expect_reject(d, "N. Tropy at the wrong fixed count");
	}
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["15"]["fallback_keys"] = 1;
		expect_reject(d, "Nitros Oxide at the wrong fixed count");
	}

	// Any OTHER extra key in a trigger entry is still unknown, in both schemas.
	{
		nlohmann::json d = fx3();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback"] = 3;
		expect_reject(d, "unknown extra trigger key in block 3");
	}
	{
		nlohmann::json d = fx3();
		auto &t = d["hit_character_encounters"]["unlock_triggers"]["12"];
		t["fallback_keys"] = 3;
		t["extra"] = 1;
		expect_reject(d, "fallback plus another unknown key");
	}

	// Block schema 2 keeps today's shape exactly: the fixture is still accepted,
	// and fallback_keys in a block 2 entry is an unknown key.
	{
		nlohmann::json d = fx();
		expect_accept(d, "block schema 2 seed still accepted");
		const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
		expect(h != NULL, "block 2 exposes encounters");
		if (h)
		{
			expect_eq(h->schema, 2, "block schema 2 stored");
			for (int g = 8; g <= 15; g++)
				expect_eq(h->triggers[g - 8].fallback_keys, 0,
				          "block 2 can never carry a fallback");
		}
	}
	{
		nlohmann::json d = fx();
		d["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = 3;
		expect_reject(d, "fallback_keys in a block schema 2 seed");
		expect(std::string(ap_seedcfg_reject_reason())
		           .find("must have exactly kind and any_of") != std::string::npos,
		       "block 2 refuses fallback_keys as an unknown key");
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
	bad["hit_character_encounters"]["schema"] = 1;
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
// feature requires an actual integer global schema >= 16; a present block must
// be validated even with no ctr_options / schema 0. A future global schema >= 16
// is admitted when the block schema is known.
static void test_global_schema_boundary(void)
{
	for (int s : {0, 13, 14, 15})
	{
		nlohmann::json d = fx();
		d["ctr_options"]["schema_version"] = s;
		expect_reject(d, "enabled feature under pre-16 global schema");
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
		d["ctr_options"]["schema_version"] = CTR_CFG_SCHEMA_KNOWN + 1; // future global, known block 1
		expect_accept(d, "future global schema >=16 with known block admitted");
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
	test_orders();
	test_triggers();
	test_fallback_keys();
	test_bosses();
	test_seed_transitions();
	test_global_schema_boundary();
	test_cache_clearing_sequence();

	std::printf("%s: %d checks, %d failures\n",
	            g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
