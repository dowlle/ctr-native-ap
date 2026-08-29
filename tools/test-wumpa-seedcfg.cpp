// Native parser acceptance for the `wumpa_checks` wire block (2026-08-29
// specification, Lane A). Compiles the REAL parser -- ap/ap_seedcfg.cpp is
// linked in, so there is no reimplementation to drift.
//
//   c++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include
//     tools/test-wumpa-seedcfg.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-wumpa-seedcfg && /tmp/test-wumpa-seedcfg
//
// Exit 0 = every assertion held. The parser logs to stderr; the harness reports
// on stdout, so `2>/dev/null` gives a clean transcript. Run it at 32-bit AND at
// host width: the block carries `long` codes and this client ships as a 32-bit
// binary, so a value that fits one and not the other has to fail here rather
// than in a player's seed.
//
// The behaviour under test, in one sentence: THE WIRE IS THE AUTHORITY on which
// Wumpa codes this seed carries, and anything this build cannot read becomes a
// check that never fires rather than a guess.
//
// What this pins:
//   1. the three modes, and the mapping each one produces,
//   2. ABSENCE: no block is the option off, which is also every pre-2026-08-29
//      seed and every off seed,
//   3. LEGACY: the pre-widening {enabled, locations:[35016100]} shape reads as
//      global, because that is the only thing it ever meant,
//   4. per-entry refusal, not total refusal: a destination this build cannot
//      read is one check that never fires, never a seed-wide failure,
//   5. the custom destination slot -- role to cup LevelID, the identity fields
//      it carries, and every shape that must be ignored,
//   6. that the parse is idempotent: a second seed does not inherit the first's
//      mapping.

#include <cstdio>
#include <cstring>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);

// The parser logs through the client's shim. This harness has no client.
extern "C" void AP_LogLine(const char *) {}

static int checks;
static int failures;

static void expect_long(long got, long want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		std::printf("FAIL %s (got %ld, want %ld)\n", name, got, want);
	}
}

static void expect_int(int got, int want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		std::printf("FAIL %s (got %d, want %d)\n", name, got, want);
	}
}

static void expect_str(const char *got, const char *want, const char *name)
{
	checks++;
	if (got == nullptr || std::strcmp(got, want) != 0)
	{
		failures++;
		std::printf("FAIL %s (got \"%s\", want \"%s\")\n", name,
		            got ? got : "(null)", want);
	}
}

#define GLOBAL_CODE 35016100L
#define RETAIL_BASE 35016101L
#define CUSTOM_CODE 35016120L
#define PACKAGE_UUID "60d5a8a8-b69a-4f6a-a0d8-9a43d91e3f2e"

// The canonical retail track order the apworld minted the codes in, expressed
// the way the wire expresses it: engine LevelID -> code. Spelled out rather than
// generated, so a reordering on either side shows up as a diff here.
static const struct
{
	int levelID;
	long code;
} RETAIL[] = {
	{3, 35016101},  // Crash Cove
	{6, 35016102},  // Roo's Tubes
	{9, 35016103},  // Mystery Caves
	{8, 35016104},  // Sewer Speedway
	{14, 35016105}, // Coco Park
	{4, 35016106},  // Tiger Temple
	{5, 35016107},  // Papu's Pyramid
	{0, 35016108},  // Dingo Canyon
	{2, 35016109},  // Blizzard Bluff
	{1, 35016110},  // Dragon Mines
	{12, 35016111}, // Polar Pass
	{15, 35016112}, // Tiny Arena
	{7, 35016113},  // Hot Air Skyway
	{10, 35016114}, // Cortex Castle
	{11, 35016115}, // N. Gin Labs
	{13, 35016116}, // Oxide Station
	{16, 35016117}, // Slide Coliseum
	{17, 35016118}, // Turbo Track
};
#define RETAIL_COUNT ((int)(sizeof RETAIL / sizeof RETAIL[0]))

static nlohmann::json base(void)
{
	return {{"ctr_options", {{"schema_version", 8}}}};
}

static nlohmann::json retail_map(void)
{
	nlohmann::json out = nlohmann::json::object();
	for (int i = 0; i < RETAIL_COUNT; i++)
		out[std::to_string(RETAIL[i].levelID)] = RETAIL[i].code;
	return out;
}

static nlohmann::json global_block(void)
{
	return {{"mode", 1},
	        {"global", GLOBAL_CODE},
	        {"retail_tracks", nlohmann::json::object()},
	        {"custom_destinations", nlohmann::json::object()}};
}

static nlohmann::json per_track_block(void)
{
	return {{"mode", 2},
	        {"global", -1},
	        {"retail_tracks", retail_map()},
	        {"custom_destinations", nlohmann::json::object()}};
}

static nlohmann::json custom_destination(void)
{
	return {{"code", CUSTOM_CODE},
	        {"package_uuid", PACKAGE_UUID},
	        {"wumpa_collectible", true}};
}

// Every code absent and the mode off: the state an off seed, a pre-widening seed
// and a refused block must all end in.
static void expect_inert(const char *what)
{
	char label[160];
	std::snprintf(label, sizeof label, "%s: mode is off", what);
	expect_int(ctr_cfg.wumpa.mode, CTR_CFG_WUMPA_OFF, label);
	std::snprintf(label, sizeof label, "%s: no global code", what);
	expect_long(ctr_cfg.wumpa.global_code, -1, label);
	for (int t = 0; t < CTR_CFG_WUMPA_TRACK_COUNT; t++)
	{
		std::snprintf(label, sizeof label, "%s: no code for level %d", what, t);
		expect_long(ctr_cfg.wumpa.tracks[t], -1, label);
	}
	std::snprintf(label, sizeof label, "%s: no custom destination", what);
	expect_int(ctr_cfg.wumpa.custom_count, 0, label);
}

// ── absence ─────────────────────────────────────────────────────────────────

static void test_absent(void)
{
	// Every pre-2026-08-29 seed, and every off seed. Both mean the same thing to
	// every caller: nothing is ever emitted.
	ap_seedcfg_parse_json(base());
	expect_inert("a seed with no wumpa_checks block");
	expect_int(ctr_cfg.schema_version, 8, "an absent block does not disturb the parse");
}

// ── the three modes ─────────────────────────────────────────────────────────

static void test_global(void)
{
	nlohmann::json doc = base();
	doc["wumpa_checks"] = global_block();
	ap_seedcfg_parse_json(doc);

	expect_int(ctr_cfg.wumpa.mode, CTR_CFG_WUMPA_GLOBAL, "global mode is read");
	expect_long(ctr_cfg.wumpa.global_code, GLOBAL_CODE, "the global code is read");
	expect_int(ctr_cfg.wumpa.custom_count, 0, "global mode binds no destination");
	for (int t = 0; t < CTR_CFG_WUMPA_TRACK_COUNT; t++)
		expect_long(ctr_cfg.wumpa.tracks[t], -1,
		            "global mode leaves every retail destination empty");
}

static void test_per_track(void)
{
	nlohmann::json doc = base();
	doc["wumpa_checks"] = per_track_block();
	ap_seedcfg_parse_json(doc);

	expect_int(ctr_cfg.wumpa.mode, CTR_CFG_WUMPA_PER_TRACK, "per-track mode is read");
	expect_long(ctr_cfg.wumpa.global_code, -1,
	            "per-track carries no global code");
	for (int i = 0; i < RETAIL_COUNT; i++)
	{
		char label[96];
		std::snprintf(label, sizeof label, "retail LevelID %d keeps its code",
		              RETAIL[i].levelID);
		expect_long(ctr_cfg.wumpa.tracks[RETAIL[i].levelID], RETAIL[i].code, label);
	}
	expect_int(ctr_cfg.wumpa.custom_count, 0,
	            "no custom destination without one on the wire");

	// The block is keyed by engine LevelID, and the codes are contiguous behind
	// the global one in the canonical retail track order. Assert the span rather
	// than only the entries, so a code landing outside the approved block is
	// caught even if every individual lookup happens to agree.
	for (int t = 0; t < CTR_CFG_WUMPA_TRACK_COUNT; t++)
	{
		checks++;
		if (ctr_cfg.wumpa.tracks[t] < RETAIL_BASE ||
		    ctr_cfg.wumpa.tracks[t] > RETAIL_BASE + RETAIL_COUNT - 1)
		{
			failures++;
			std::printf("FAIL level %d holds %ld, outside the approved block\n",
			            t, ctr_cfg.wumpa.tracks[t]);
		}
	}
}

// ── the legacy shape ────────────────────────────────────────────────────────

static void test_legacy_shape(void)
{
	// Every seed rolled between the 0.2.0 freeze and 2026-08-29 carries this
	// shape and meant exactly one thing: the single global check. Refusing it
	// would break those seeds for no gain.
	nlohmann::json doc = base();
	doc["wumpa_checks"] = {{"enabled", true},
	                       {"locations", nlohmann::json::array({GLOBAL_CODE})}};
	ap_seedcfg_parse_json(doc);
	expect_int(ctr_cfg.wumpa.mode, CTR_CFG_WUMPA_GLOBAL,
	           "the pre-widening shape reads as global");
	expect_long(ctr_cfg.wumpa.global_code, GLOBAL_CODE,
	            "the pre-widening shape yields the global code");

	// enabled=0 in that shape was the option off, which the apworld never
	// actually emitted -- but reading it as global would invent a check.
	doc["wumpa_checks"]["enabled"] = false;
	ap_seedcfg_parse_json(doc);
	expect_inert("the pre-widening shape with enabled=false");
}

// ── per-entry refusal ───────────────────────────────────────────────────────

static void test_unknown_mode(void)
{
	// A mode from a newer apworld. Emitting nothing is the only answer that
	// cannot send a wrong code; the schema banner already handles the advice.
	nlohmann::json doc = base();
	doc["wumpa_checks"] = per_track_block();
	doc["wumpa_checks"]["mode"] = 3;
	ap_seedcfg_parse_json(doc);
	expect_inert("an unknown mode");

	doc["wumpa_checks"]["mode"] = -1;
	ap_seedcfg_parse_json(doc);
	expect_inert("a negative mode");
}

static void test_retail_entry_refusals(void)
{
	nlohmann::json doc = base();
	doc["wumpa_checks"] = per_track_block();
	// Out-of-range keys, an unparseable key and a non-integer value are each
	// skipped -- one check that never fires, not a seed-wide failure. The block
	// only says which code goes with which destination; the server's location
	// list is what says the check exists at all.
	doc["wumpa_checks"]["retail_tracks"]["18"] = 35016119;
	doc["wumpa_checks"]["retail_tracks"]["-1"] = 35016099;
	doc["wumpa_checks"]["retail_tracks"]["104"] = CUSTOM_CODE;
	doc["wumpa_checks"]["retail_tracks"]["not-a-number"] = 35016150;
	doc["wumpa_checks"]["retail_tracks"]["5"] = "35016107";
	ap_seedcfg_parse_json(doc);

	expect_int(ctr_cfg.wumpa.mode, CTR_CFG_WUMPA_PER_TRACK,
	           "one bad entry does not refuse the block");
	expect_long(ctr_cfg.wumpa.tracks[3], 35016101,
	            "a good entry beside a bad one survives");
	expect_long(ctr_cfg.wumpa.tracks[5], -1,
	            "a non-integer code is skipped rather than coerced");

	// A malformed retail_tracks value leaves every destination empty without
	// touching the mode -- which is a seed where per-track sends nothing, and is
	// still a better answer than guessing the range.
	doc = base();
	doc["wumpa_checks"] = per_track_block();
	doc["wumpa_checks"]["retail_tracks"] = "nope";
	ap_seedcfg_parse_json(doc);
	expect_int(ctr_cfg.wumpa.mode, CTR_CFG_WUMPA_PER_TRACK,
	           "a malformed map still leaves a readable mode");
	for (int t = 0; t < CTR_CFG_WUMPA_TRACK_COUNT; t++)
		expect_long(ctr_cfg.wumpa.tracks[t], -1,
		            "a malformed map mints no destination");
}

// ── the custom destination slot ─────────────────────────────────────────────

static void test_custom_destination(void)
{
	nlohmann::json doc = base();
	doc["wumpa_checks"] = per_track_block();
	doc["wumpa_checks"]["custom_destinations"]["purple_gem_cup"] =
	    custom_destination();
	ap_seedcfg_parse_json(doc);

	expect_int(ctr_cfg.wumpa.custom_count, 1, "the bound destination is read");
	expect_int(ctr_cfg.wumpa.custom[0].cup_level_id, 104,
	           "the role word resolves to its Gem Cup LevelID at parse time");
	expect_long(ctr_cfg.wumpa.custom[0].code, CUSTOM_CODE,
	            "the destination slot's code is read");
	expect_int(ctr_cfg.wumpa.custom[0].wumpa_collectible, 1,
	           "the measured capability is read");
	expect_str(ctr_cfg.wumpa.custom[0].package_uuid, PACKAGE_UUID,
	           "the package identity is read");

	// The custom code sits outside the retail block entirely, which is what
	// makes "never fall back to the host retail level" a structural property
	// rather than a runtime check that could be forgotten.
	checks++;
	if (ctr_cfg.wumpa.custom[0].code >= RETAIL_BASE &&
	    ctr_cfg.wumpa.custom[0].code < RETAIL_BASE + RETAIL_COUNT)
	{
		failures++;
		std::printf("FAIL the custom code collides with the retail block\n");
	}
}

static void test_custom_destination_refusals(void)
{
	static const char *what[] = {
	    "an unknown role", "no code", "no wumpa_collectible",
	    "a malformed package uuid", "a non-object entry",
	};
	nlohmann::json bad[5];

	// A role a newer apworld supports and this build does not. The destination
	// exists in the seed, this client cannot serve it, and no code for it may
	// ever be sent -- so the slot stays empty rather than being guessed at.
	bad[0] = per_track_block();
	bad[0]["custom_destinations"]["green_gem_cup"] = custom_destination();

	bad[1] = per_track_block();
	bad[1]["custom_destinations"]["purple_gem_cup"] = custom_destination();
	bad[1]["custom_destinations"]["purple_gem_cup"].erase("code");

	// A destination that does not state the measured capability is not
	// self-describing, and a Wumpa check must never guess it.
	bad[2] = per_track_block();
	bad[2]["custom_destinations"]["purple_gem_cup"] = custom_destination();
	bad[2]["custom_destinations"]["purple_gem_cup"].erase("wumpa_collectible");

	// Without a readable package identity the emit path has nothing to compare
	// against, so the slot cannot be honoured.
	bad[3] = per_track_block();
	bad[3]["custom_destinations"]["purple_gem_cup"] = custom_destination();
	bad[3]["custom_destinations"]["purple_gem_cup"]["package_uuid"] = "not-a-uuid";

	bad[4] = per_track_block();
	bad[4]["custom_destinations"]["purple_gem_cup"] = 35016120;

	for (int i = 0; i < 5; i++)
	{
		char label[128];
		nlohmann::json doc = base();
		doc["wumpa_checks"] = bad[i];
		ap_seedcfg_parse_json(doc);

		std::snprintf(label, sizeof label, "%s binds no destination", what[i]);
		expect_int(ctr_cfg.wumpa.custom_count, 0, label);
		// And the retail half is untouched: one unreadable destination is one
		// check that never fires, not a seed-wide refusal.
		std::snprintf(label, sizeof label, "%s leaves the retail map intact",
		              what[i]);
		expect_long(ctr_cfg.wumpa.tracks[3], 35016101, label);
	}
}

// ── idempotence ─────────────────────────────────────────────────────────────

static void test_reparse_clears(void)
{
	// A client can connect to a second seed without restarting. A mapping that
	// survived the reparse would send the previous seed's codes.
	nlohmann::json doc = base();
	doc["wumpa_checks"] = per_track_block();
	doc["wumpa_checks"]["custom_destinations"]["purple_gem_cup"] =
	    custom_destination();
	ap_seedcfg_parse_json(doc);
	expect_int(ctr_cfg.wumpa.custom_count, 1, "the first seed binds a destination");

	ap_seedcfg_parse_json(base());
	expect_inert("a second seed with no block");

	// And a per-track seed followed by a global one keeps no per-track codes.
	doc = base();
	doc["wumpa_checks"] = per_track_block();
	ap_seedcfg_parse_json(doc);
	doc = base();
	doc["wumpa_checks"] = global_block();
	ap_seedcfg_parse_json(doc);
	expect_long(ctr_cfg.wumpa.global_code, GLOBAL_CODE, "the global code is read");
	for (int t = 0; t < CTR_CFG_WUMPA_TRACK_COUNT; t++)
		expect_long(ctr_cfg.wumpa.tracks[t], -1,
		            "a global seed keeps no per-track codes from the last one");
}

// The codes are `long` on the wire and this client ships 32-bit, so pin that the
// values in play round-trip through whatever `long` is here. The harness is run
// at both widths for exactly this reason.
static void test_code_width(void)
{
	nlohmann::json doc = base();
	doc["wumpa_checks"] = per_track_block();
	doc["wumpa_checks"]["custom_destinations"]["purple_gem_cup"] =
	    custom_destination();
	ap_seedcfg_parse_json(doc);

	expect_int((int)(sizeof(long) >= 4), 1, "a location code needs 32 bits");
	expect_long(ctr_cfg.wumpa.tracks[17], 35016118,
	            "the highest retail code survives this build's long");
	expect_long(ctr_cfg.wumpa.custom[0].code, CUSTOM_CODE,
	            "the custom code survives this build's long");
}

int main(void)
{
	test_absent();
	test_global();
	test_per_track();
	test_legacy_shape();
	test_unknown_mode();
	test_retail_entry_refusals();
	test_custom_destination();
	test_custom_destination_refusals();
	test_reparse_clears();
	test_code_width();

	std::printf("%s wumpa seedcfg (%d checks, %d failures)\n",
	            failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
