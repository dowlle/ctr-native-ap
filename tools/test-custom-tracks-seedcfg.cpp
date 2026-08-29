// Native parser acceptance for the schema-8 `custom_tracks` wire block (Baby T
// Park event spike, rung 2c). Compiles the REAL parser -- ap/ap_seedcfg.cpp is
// linked in, so there is no reimplementation to drift.
//
//   c++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include \
//     tools/test-custom-tracks-seedcfg.cpp ap/ap_seedcfg.cpp \
//     -o /tmp/test-custom-tracks-seedcfg && /tmp/test-custom-tracks-seedcfg
//
// Exit 0 = every assertion held. The parser logs to stderr; the harness reports
// on stdout, so `2>/dev/null` gives a clean transcript.
//
// The binding behaviour under test, in one sentence: a seed either hands this
// client a completely readable custom-track descriptor or it hands it nothing,
// and "nothing" always means the named cup keeps its vanilla four legs.
//
// What this pins:
//   1. the happy path: every field of the block reaches ctr_cfg intact,
//   2. ABSENCE: no block means the feature is off and no cup is displaced --
//      the state every pre-custom-tracks seed is in,
//   3. TOTAL REFUSAL. There is no partial parse. Each malformed shape gets its
//      own case, and each asserts the same two things: the descriptor is not
//      usable, AND the cup it named is not displaced. Those must move together,
//      because a client that displaced a cup it cannot serve would leave the Gem
//      unreachable, and one that served a track without displacing would race
//      four retail legs the seed's logic says do not exist,
//   4. the two refusal FLAVOURS: an unknown block version also raises
//      schema_newer (so the "update the client" banner fires, which is the right
//      advice), while a malformed-but-known-version block does not (that is a
//      generation bug, and telling the player to update misdirects them),
//   5. digest shape, including the trap that json_str leaves an OVERSIZED value
//      empty -- a 65-character digest must refuse, not silently read as absent,
//   6. displacement is per-cup and exact: the named cup is displaced and the
//      other four are not, whatever their gem_cup_legs rows say.

#include <cstdio>
#include <cstring>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

// ap_cfg_log fans out to this; the C unity build is not linked here.
extern "C" void AP_LogLine(const char *) {}

static int checks;
static int failures;

static void expect_eq(int got, int want, const char *name)
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
	if (std::strcmp(got, want) != 0)
	{
		failures++;
		std::printf("FAIL %s\n  got  \"%s\"\n  want \"%s\"\n", name, got, want);
	}
}

static const char *LEV_HASH = "96ad9f74f51a02eafcc207cd02c97052d674c950e0f24b6440a227494a705fe8";
static const char *VRM_HASH = "2dcaa0fe93359c7ae00fb93842a581210e0dcc2db73f4de43508375834092e83";
static const char *PACKAGE_UUID = "60d5a8a8-b69a-4f6a-a0d8-9a43d91e3f2e";
static const char *NAVIGATION_UUID = "898a9315-693f-4ed3-b6a0-fbe50db8bc40";

static int vanilla_legs[20] = {3, 9, 2, 5, 6, 14, 12, 10, 4, 8, 1, 11, 0, 15, 7, 13, 6, 5, 1, 7};

static nlohmann::json base(void)
{
	return {{"ctr_options", {{"schema_version", 8}}}};
}

// The event seed's own block, exactly as the apworld emits it.
static nlohmann::json good_flags(void)
{
	return {{"crates", true},   {"ctr_letters", true}, {"relic_crates", true},
	        {"ai_nav", true},   {"minimap", false},    {"ghosts", false},
	        // Block version 3. Measured from the real v1.0.0 LEV: 8 fruit crates,
	        // 40 guaranteed fruit against a target of 10.
	        {"wumpa_collectible", true},
	        {"spawns", 8},      {"checkpoints", 35}};
}

static nlohmann::json good_entry(void)
{
	return {{"id", "baby-t-park"},
	        {"package_uuid", PACKAGE_UUID},
	        {"package_version", "1.0.0"},
	        {"minimum_client_version", "0.2.0-alpha6"},
	        {"minimum_apworld_version", "0.2.0-alpha6"},
	        {"lev_sha256", LEV_HASH},
	        {"vrm_sha256", VRM_HASH},
	        {"navigation", {{"uuid", NAVIGATION_UUID}, {"revision", 1}}},
	        {"laps", 7},
	        {"host_level_id", 6},
	        {"replaces_cup_level_id", 104},
	        {"boxes", false}, // the event YAML emits false; see the rung-2b analysis
	        {"flags", good_flags()}};
}

static nlohmann::json good_block(void)
{
	return {{"enabled", true},
	        {"version", CTR_CFG_CT_BLOCK_VERSION_KNOWN},
	        {"tracks", nlohmann::json::array({good_entry()})}};
}

// Assert the whole feature is off AND no cup was displaced. Every refusal case
// ends here: the two facts must move together or the seed becomes unwinnable in
// one direction or wrong-content in the other.
static void expect_refused(const char *what)
{
	char label[160];

	std::snprintf(label, sizeof label, "%s: descriptor not usable", what);
	expect_eq(ctr_cfg.custom_tracks_ok, 0, label);

	for (int cup = 0; cup < 5; cup++)
	{
		std::snprintf(label, sizeof label, "%s: cup %d not displaced", what, cup);
		expect_eq(ctr_cfg_cup_displaced(cup), 0, label);
	}
}

// ---------------------------------------------------------------------------

static void test_happy_path(void)
{
	nlohmann::json doc = base();
	doc["custom_tracks"] = good_block();
	ap_seedcfg_parse_json(doc);

	expect_eq(ctr_cfg.custom_tracks_seen, 1, "block seen");
	expect_eq(ctr_cfg.custom_tracks_ok, 1, "block usable");
	expect_eq(ctr_cfg.schema_newer, 0, "schema 8 is understood, no update banner");

	expect_eq(ctr_cfg.custom_track.laps, 7, "laps");
	expect_eq(ctr_cfg.custom_track.host_level_id, 6, "host_level_id");
	expect_eq(ctr_cfg.custom_track.replaces_cup_level_id, 104, "replaces_cup_level_id");
	expect_eq(ctr_cfg.custom_track.boxes, 0, "boxes:false is honoured");
	expect_str(ctr_cfg.custom_track.id, "baby-t-park", "track id");
	expect_str(ctr_cfg.custom_track.package_uuid, PACKAGE_UUID, "package UUID");
	expect_str(ctr_cfg.custom_track.package_version, "1.0.0", "package version");
	expect_str(ctr_cfg.custom_track.minimum_client_version, "0.2.0-alpha6", "minimum client");
	expect_str(ctr_cfg.custom_track.minimum_apworld_version, "0.2.0-alpha6", "minimum apworld");
	expect_str(ctr_cfg.custom_track.navigation_uuid, NAVIGATION_UUID, "navigation UUID");
	expect_eq((int)ctr_cfg.custom_track.navigation_revision, 1, "navigation revision");
	expect_str(ctr_cfg.custom_track.lev_sha256, LEV_HASH, "lev digest");
	expect_str(ctr_cfg.custom_track.vrm_sha256, VRM_HASH, "vrm digest");

	expect_eq(ctr_cfg.custom_track.flags.crates, 1, "flags.crates");
	expect_eq(ctr_cfg.custom_track.flags.ctr_letters, 1, "flags.ctr_letters");
	expect_eq(ctr_cfg.custom_track.flags.relic_crates, 1, "flags.relic_crates");
	expect_eq(ctr_cfg.custom_track.flags.ai_nav, 1, "flags.ai_nav");
	expect_eq(ctr_cfg.custom_track.flags.minimap, 0, "flags.minimap");
	expect_eq(ctr_cfg.custom_track.flags.ghosts, 0, "flags.ghosts");
	expect_eq(ctr_cfg.custom_track.flags.wumpa_collectible, 1,
	          "flags.wumpa_collectible");
	expect_eq(ctr_cfg.custom_track.flags.spawns, 8, "flags.spawns");
	expect_eq(ctr_cfg.custom_track.flags.checkpoints, 35, "flags.checkpoints");

	// Displacement is exact: the named cup and only the named cup.
	expect_eq(ctr_cfg_cup_displaced(4), 1, "Purple is displaced");
	expect_eq(ctr_cfg_cup_displaced(0), 0, "Red is not displaced");
	expect_eq(ctr_cfg_cup_displaced(1), 0, "Green is not displaced");
	expect_eq(ctr_cfg_cup_displaced(2), 0, "Blue is not displaced");
	expect_eq(ctr_cfg_cup_displaced(3), 0, "Yellow is not displaced");
	expect_eq(ctr_cfg_cup_displaced(-1), 0, "out-of-range cup is not displaced");
	expect_eq(ctr_cfg_cup_displaced(5), 0, "out-of-range cup is not displaced");
}

// The displaced cup's gem_cup_legs row stays complete on the wire, and native
// must read it as present-but-irrelevant. This pins BOTH halves at once: the row
// still parses into ctr_cfg (other consumers rely on the table being complete),
// and the cup still reports displaced.
static void test_displaced_cup_keeps_its_wire_row(void)
{
	nlohmann::json doc = base();
	doc["custom_tracks"] = good_block();
	doc["gem_cup_legs"] = {{"104", {2, 3, 4, 5}}};
	ap_seedcfg_parse_json(doc);

	expect_eq(ctr_cfg.custom_tracks_ok, 1, "displaced-row seed still parses");
	expect_eq(ctr_cfg_cup_displaced(4), 1, "the cup is displaced");
	expect_eq(ctr_cfg_cup_leg(4, 0), 2, "its wire row is still readable");
	expect_eq(ctr_cfg_cup_leg(4, 3), 5, "its wire row is still complete");
}

static void test_absent_block(void)
{
	ap_seedcfg_parse_json(base());

	expect_eq(ctr_cfg.custom_tracks_seen, 0, "absent block is not seen");
	expect_eq(ctr_cfg.schema_newer, 0, "absent block raises no banner");
	expect_refused("absent block");
}

static void test_absent_block_clears_a_previous_seed(void)
{
	nlohmann::json doc = base();
	doc["custom_tracks"] = good_block();
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg.custom_tracks_ok, 1, "armed by the first seed");

	// Reconnecting to a seed without the block must not inherit the old one.
	ap_seedcfg_parse_json(base());
	expect_refused("a second seed without the block");
}

static void test_unknown_block_version(void)
{
	nlohmann::json doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["version"] = CTR_CFG_CT_BLOCK_VERSION_KNOWN + 1;
	ap_seedcfg_parse_json(doc);

	expect_eq(ctr_cfg.custom_tracks_seen, 1, "unknown version is still seen");
	// The banner is the right advice here: this seed really does need a newer
	// client, and the verifier's schema_newer early-out stops it reasoning over
	// a seed shape it cannot see.
	expect_eq(ctr_cfg.schema_newer, 1, "unknown block version raises the update banner");
	expect_refused("unknown block version");

	// version 0 (i.e. the key absent entirely) is equally unknown.
	doc["custom_tracks"].erase("version");
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg.schema_newer, 1, "missing block version raises the update banner");
	expect_refused("missing block version");

	// The version this build SUPERSEDED is unknown in exactly the same way, and
	// for a reason worth spelling out: a version-2 entry carries no measured
	// wumpa_collectible, and a per-track Wumpa check must never guess one. The
	// only safe reading of a version-2 block is "not this build's".
	doc["custom_tracks"]["version"] = CTR_CFG_CT_BLOCK_VERSION_KNOWN - 1;
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg.schema_newer, 1, "a superseded block version also raises the banner");
	expect_refused("superseded block version");
}

// A malformed-but-known-version block is a generation bug, not a version gap.
// It must refuse WITHOUT telling the player to update their client.
static void test_malformed_does_not_raise_the_banner(void)
{
	nlohmann::json doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0]["laps"] = 99;
	ap_seedcfg_parse_json(doc);

	expect_eq(ctr_cfg.schema_newer, 0, "a malformed entry does not say 'update the client'");
	expect_refused("malformed entry");
}

static void test_block_shapes(void)
{
	nlohmann::json doc;

	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["enabled"] = false;
	ap_seedcfg_parse_json(doc);
	expect_refused("enabled:false");

	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"].erase("tracks");
	ap_seedcfg_parse_json(doc);
	expect_refused("no tracks array");

	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"] = nlohmann::json::array();
	ap_seedcfg_parse_json(doc);
	expect_refused("empty tracks array");

	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"] = "not an array";
	ap_seedcfg_parse_json(doc);
	expect_refused("tracks is not an array");

	// This build has exactly one loader slot. Serving one of two silently would
	// be a wrong-content outcome, so two entries refuse rather than pick.
	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"] = nlohmann::json::array({good_entry(), good_entry()});
	ap_seedcfg_parse_json(doc);
	expect_refused("two entries");

	doc = base();
	doc["custom_tracks"] = "not an object";
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg.custom_tracks_seen, 0, "a non-object block is not even seen");
	expect_refused("block is not an object");

	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0] = "not an object";
	ap_seedcfg_parse_json(doc);
	expect_refused("entry is not an object");
}

static void test_field_ranges(void)
{
	struct
	{
		const char *field;
		nlohmann::json value;
		const char *what;
	} bad[] = {
	    {"laps", 0, "laps 0"},
	    {"laps", 8, "laps 8"},
	    {"laps", -1, "laps -1"},
	    {"host_level_id", -1, "host_level_id -1"},
	    {"host_level_id", 18, "host_level_id 18 (a battle arena)"},
	    {"host_level_id", 25, "host_level_id 25 (a hub)"},
	    {"replaces_cup_level_id", 99, "replaces_cup_level_id 99"},
	    {"replaces_cup_level_id", 105, "replaces_cup_level_id 105"},
	    {"replaces_cup_level_id", 4, "replaces_cup_level_id 4 (a cup INDEX, not a LevelID)"},
	};

	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		nlohmann::json doc = base();
		doc["custom_tracks"] = good_block();
		doc["custom_tracks"]["tracks"][0][bad[i].field] = bad[i].value;
		ap_seedcfg_parse_json(doc);
		expect_refused(bad[i].what);
	}

	// Missing required scalars are refused, not defaulted.
	const char *required[] = {"laps", "host_level_id", "replaces_cup_level_id"};
	for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); i++)
	{
		char what[96];
		nlohmann::json doc = base();
		doc["custom_tracks"] = good_block();
		doc["custom_tracks"]["tracks"][0].erase(required[i]);
		ap_seedcfg_parse_json(doc);
		std::snprintf(what, sizeof what, "missing %s", required[i]);
		expect_refused(what);
	}

	// The two extremes of each accepted range still parse.
	nlohmann::json doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0]["laps"] = 1;
	doc["custom_tracks"]["tracks"][0]["host_level_id"] = 0;
	doc["custom_tracks"]["tracks"][0]["replaces_cup_level_id"] = 100;
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg.custom_tracks_ok, 1, "range extremes parse");
	expect_eq(ctr_cfg_cup_displaced(0), 1, "cup LevelID 100 displaces cup 0");
	expect_eq(ctr_cfg_cup_displaced(4), 0, "and leaves cup 4 alone");

	doc["custom_tracks"]["tracks"][0]["laps"] = 7;
	doc["custom_tracks"]["tracks"][0]["host_level_id"] = 17;
	doc["custom_tracks"]["tracks"][0]["replaces_cup_level_id"] = 104;
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg.custom_tracks_ok, 1, "opposite range extremes parse");
}

static void test_digests(void)
{
	static const char *bad[] = {
	    "", // absent / empty
	    "96ad9f74f51a02eafcc207cd02c97052d674c950e0f24b6440a227494a705fe",  // 63
	    "96ad9f74f51a02eafcc207cd02c97052d674c950e0f24b6440a227494a705fe8a", // 65
	    "96ad9f74f51a02eafcc207cd02c97052d674c950e0f24b6440a227494a705fZ8",  // non-hex
	    "not a digest at all",
	};

	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		char what[96];
		nlohmann::json doc = base();
		doc["custom_tracks"] = good_block();
		doc["custom_tracks"]["tracks"][0]["lev_sha256"] = bad[i];
		ap_seedcfg_parse_json(doc);
		std::snprintf(what, sizeof what, "bad lev digest #%u", i);
		expect_refused(what);

		doc = base();
		doc["custom_tracks"] = good_block();
		doc["custom_tracks"]["tracks"][0]["vrm_sha256"] = bad[i];
		ap_seedcfg_parse_json(doc);
		std::snprintf(what, sizeof what, "bad vrm digest #%u", i);
		expect_refused(what);
	}

	// A missing digest key is refused, never treated as "no check required".
	nlohmann::json doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0].erase("lev_sha256");
	ap_seedcfg_parse_json(doc);
	expect_refused("missing lev digest");

	// Uppercase is accepted: the apworld case-folds before emitting, but native's
	// own comparison is case-insensitive and a stricter wire check would turn a
	// harmless upstream change into a refused seed.
	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0]["lev_sha256"] =
	    "96AD9F74F51A02EAFCC207CD02C97052D674C950E0F24B6440A227494A705FE8";
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg.custom_tracks_ok, 1, "an uppercase digest is accepted");
}

static void test_package_and_navigation_identity(void)
{
	// Every v2 identity field is required. json_str deliberately yields an
	// empty destination for absent, non-string, or oversized values, and each
	// such case must refuse the whole descriptor.
	static const char *required[] = {
	    "id", "package_uuid", "package_version", "minimum_client_version",
	    "minimum_apworld_version",
	};
	for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); i++)
	{
		char what[112];
		nlohmann::json doc = base();
		doc["custom_tracks"] = good_block();
		doc["custom_tracks"]["tracks"][0].erase(required[i]);
		ap_seedcfg_parse_json(doc);
		std::snprintf(what, sizeof what, "missing %s", required[i]);
		expect_refused(what);
	}

	struct
	{
		const char *field;
		nlohmann::json value;
		const char *what;
	} bad[] = {
	    {"package_uuid", "not-a-uuid", "malformed package UUID"},
	    {"package_uuid", "60D5A8A8-B69A-4F6A-A0D8-9A43D91E3F2Eextra", "oversized package UUID"},
	    {"package_version", "", "empty package version"},
	    {"package_version", "1.0.0\nunsafe", "non-plain package version"},
	    {"minimum_client_version", 6, "non-string minimum client"},
	    {"minimum_apworld_version", "alpha6\\unsafe", "non-plain minimum apworld"},
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		nlohmann::json doc = base();
		doc["custom_tracks"] = good_block();
		doc["custom_tracks"]["tracks"][0][bad[i].field] = bad[i].value;
		ap_seedcfg_parse_json(doc);
		expect_refused(bad[i].what);
	}

	nlohmann::json doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0].erase("navigation");
	ap_seedcfg_parse_json(doc);
	expect_refused("missing navigation object");

	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0]["navigation"]["uuid"] = "bad";
	ap_seedcfg_parse_json(doc);
	expect_refused("malformed navigation UUID");

	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0]["navigation"]["revision"] = 0;
	ap_seedcfg_parse_json(doc);
	expect_refused("navigation revision zero");
}

static void test_flags(void)
{
	static const char *boolFlags[] = {"crates", "ctr_letters", "relic_crates", "ai_nav",
	                                  "minimap", "ghosts", "wumpa_collectible"};

	// Every flag is required: a descriptor that omits one is not self-describing,
	// and a silently defaulted capability is the same class of plausible-but-wrong
	// state the digests guard against.
	for (unsigned i = 0; i < sizeof(boolFlags) / sizeof(boolFlags[0]); i++)
	{
		char what[96];
		nlohmann::json doc = base();
		doc["custom_tracks"] = good_block();
		doc["custom_tracks"]["tracks"][0]["flags"].erase(boolFlags[i]);
		ap_seedcfg_parse_json(doc);
		std::snprintf(what, sizeof what, "missing flags.%s", boolFlags[i]);
		expect_refused(what);
	}

	nlohmann::json doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0].erase("flags");
	ap_seedcfg_parse_json(doc);
	expect_refused("missing flags object");

	struct
	{
		const char *field;
		int value;
		const char *what;
	} ranges[] = {
	    {"spawns", 0, "flags.spawns 0"},
	    {"spawns", 9, "flags.spawns 9"},
	    {"spawns", -1, "missing flags.spawns"},
	    {"checkpoints", 0, "flags.checkpoints 0"},
	    {"checkpoints", 256, "flags.checkpoints 256"},
	};

	for (unsigned i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++)
	{
		nlohmann::json d = base();
		d["custom_tracks"] = good_block();
		d["custom_tracks"]["tracks"][0]["flags"][ranges[i].field] = ranges[i].value;
		ap_seedcfg_parse_json(d);
		expect_refused(ranges[i].what);
	}

	// A track that measured false for a capability still parses: the wire records
	// what was measured, and deciding whether the engine can serve it is the
	// loader's job, not the parser's.
	doc = base();
	doc["custom_tracks"] = good_block();
	doc["custom_tracks"]["tracks"][0]["flags"]["ai_nav"] = false;
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg.custom_tracks_ok, 1, "ai_nav:false parses (the loader judges it)");
	expect_eq(ctr_cfg.custom_track.flags.ai_nav, 0, "and lands as measured");
}

static void test_schema_gate(void)
{
	// The block is refused outright below schema 1, along with everything else.
	nlohmann::json doc = {{"ctr_options", {{"schema_version", 0}}}};
	doc["custom_tracks"] = good_block();
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg_active(), 0, "schema 0 is inactive");
	expect_refused("schema 0");

	// Schema 8 is this build's ceiling; 9 is newer and raises the banner, but the
	// block still parses best-effort like every other field.
	doc = {{"ctr_options", {{"schema_version", 9}}}};
	doc["custom_tracks"] = good_block();
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg.schema_newer, 1, "schema 9 raises the update banner");

	expect_eq(CTR_CFG_SCHEMA_KNOWN, 8, "this build understands schema 8");
}

int main(void)
{
	ctr_cfg_set_vanilla_cup_legs(vanilla_legs);

	test_happy_path();
	test_displaced_cup_keeps_its_wire_row();
	test_absent_block();
	test_absent_block_clears_a_previous_seed();
	test_unknown_block_version();
	test_malformed_does_not_raise_the_banner();
	test_block_shapes();
	test_field_ranges();
	test_digests();
	test_package_and_navigation_identity();
	test_flags();
	test_schema_gate();

	std::printf("%s custom-tracks seedcfg (%d checks, %d failures)\n", failures ? "FAIL" : "PASS",
	            checks, failures);
	return failures ? 1 : 0;
}
