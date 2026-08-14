// Parser harness for the character-phase wire shape (issues #54 / #209).
//
// Drives ap_seedcfg_parse_json against hand-written slot_data and checks what
// landed in ctr_cfg. Standalone, like tools/test-feed-class-cpp.cpp: it compiles
// the isolated ap_seedcfg translation unit and nothing else, so it runs without
// a disc, a display or a server.
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP -I ap/vendor/json/include \
//       tools/test-character-phase-seedcfg.cpp ap/ap_seedcfg.cpp \
//       -o /tmp/test-charphase && /tmp/test-charphase
//
// What it is here to catch, in order of how much it would hurt:
//
//   1. A racer lock landing on the wrong pad, or on no pad. A lock the client
//      drops is a pad it opens too early -- the golden rule broken in the
//      permissive direction, which playtests as "the seed was easy" rather
//      than as a crash.
//   2. The resolved stat trio being re-derived instead of read. The apworld
//      owns the progressive-vs-editable precedence; if this side ever starts
//      inferring it, the two disagree and the panel lies about who owns your
//      kart.
//   3. An older seed changing behaviour. Every key here is additive, so a
//      slot_data with none of them must leave the client exactly where a
//      pre-character-phase build was.
#include <cstdio>
#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

// ap_seedcfg.cpp logs through the game's AP_LogLine, which lives in the C unity
// build. This harness links only the parser, so it supplies the one symbol
// rather than dragging the engine in -- the same trade tools/test-feed-class*.c
// make. Output goes to stdout so a parse decision stays visible when a check
// fails.
extern "C" void AP_LogLine(const char *line)
{
	std::fputs(line, stdout);
}

static int g_failures = 0;

static void check(bool ok, const char *what)
{
	if (!ok)
	{
		std::printf("FAIL: %s\n", what);
		g_failures++;
	}
}

static void check_eq(int got, int want, const char *what)
{
	if (got != want)
	{
		std::printf("FAIL: %s (got %d, want %d)\n", what, got, want);
		g_failures++;
	}
}

// A minimal but REAL slot_data: schema 7 plus whatever the case under test
// needs. Kept as a string rather than built through nlohmann so the test reads
// like the wire it is checking.
static void parse(const std::string &ctr_options, const std::string &extra = "")
{
	std::string doc = "{\"ctr_options\":{\"schema_version\":7";
	if (!ctr_options.empty())
		doc += "," + ctr_options;
	doc += "}";
	if (!extra.empty())
		doc += "," + extra;
	doc += "}";
	ap_seedcfg_parse_json(nlohmann::json::parse(doc));
}

// ---------------------------------------------------------------------------

static void test_absent_keys_are_pre_feature_behaviour()
{
	parse("");
	check_eq(ctr_cfg.starting_character, 0, "absent starting_character -> Crash");
	check_eq(ctr_cfg.starting_stat_class, 0, "absent starting_stat_class -> vanilla");
	check_eq(ctr_cfg.character_unlocks, 0, "absent character_unlocks -> off");
	check_eq(ctr_cfg.racer_locked_pads, 0, "absent racer_locked_pads -> off");
	check_eq(ctr_cfg.penta_stats, 0, "absent penta_stats -> pal");
	check_eq(ctr_cfg.stat_source, 0, "absent stat_source -> vanilla");
	check_eq(ctr_cfg.stat_owner, 0, "absent stat_owner -> none");
	check_eq(ctr_cfg.stat_editing_allowed, 0, "absent stat_editing_allowed -> no");
	check_eq(ctr_cfg.character_phase_present, 0, "absent keys -> phase not present");
	for (int pad = 0; pad < CTR_CFG_PAD_COUNT; pad++)
		check(ctr_cfg.racer_lock[pad] == -1, "absent racer_locks -> no pad locked");
	for (int cup = 0; cup < 5; cup++)
		check(ctr_cfg.gem_cup_racer_lock[cup] == -1, "absent racer_locks -> no cup locked");
}

// The defect the 23:48 pre-commit audit caught: an explicitly all-default
// character-phase seed must NOT look like a pre-feature seed. Both sides are
// asserted, because getting either wrong is a real regression -- hiding the
// picker on an all-unlocked seed deletes the feature, and showing it on a
// genuinely old seed widens a roster nobody asked to widen.
static void test_phase_presence_is_key_presence_not_value()
{
	// Old seed: no character keys at all.
	parse("");
	check_eq(ctr_cfg.character_phase_present, 0,
	         "absent keys -> no character phase (old seed stays old)");

	// New seed, all-unlocked comfort mode, every other value at its default.
	// Every scalar below reads exactly like the absent-key case above.
	parse("\"starting_character\":0,\"starting_stat_class\":0,"
	      "\"character_unlocks\":false,\"racer_locked_pads\":false,"
	      "\"penta_stats\":0,\"editable_stats\":0,"
	      "\"stat_source\":0,\"stat_owner\":0,\"stat_editing_allowed\":false");
	check_eq(ctr_cfg.character_phase_present, 1,
	         "explicit all-default character keys -> phase IS live");
	check_eq(ctr_cfg.character_unlocks, 0, "and all-unlocked mode is what it says");

	// A single key is enough; the apworld emits them together, but a partial
	// wire must not silently fall back to "no feature".
	parse("\"character_unlocks\":true");
	check_eq(ctr_cfg.character_phase_present, 1, "one key is enough");
}

static void test_scalars_round_trip()
{
	parse("\"starting_character\":10,\"starting_stat_class\":3,"
	      "\"character_unlocks\":true,\"racer_locked_pads\":true,"
	      "\"penta_stats\":1,\"editable_stats\":2,"
	      "\"stat_source\":2,\"stat_owner\":2,\"stat_editing_allowed\":true");
	check_eq(ctr_cfg.starting_character, 10, "starting_character (Ripper Roo)");
	check_eq(ctr_cfg.starting_stat_class, 3, "starting_stat_class (speed)");
	check_eq(ctr_cfg.character_unlocks, 1, "character_unlocks");
	check_eq(ctr_cfg.racer_locked_pads, 1, "racer_locked_pads");
	check_eq(ctr_cfg.penta_stats, 1, "penta_stats (ntsc)");
	check_eq(ctr_cfg.editable_stats, 2, "editable_stats raw (per_character)");
	check_eq(ctr_cfg.stat_source, 2, "stat_source (editable)");
	check_eq(ctr_cfg.stat_owner, 2, "stat_owner (per_character)");
	check_eq(ctr_cfg.stat_editing_allowed, 1, "stat_editing_allowed");
}

static void test_out_of_range_values_fail_closed()
{
	// A racer this build cannot name must not become an array read into
	// MetaDataCharacters. 16 is one past Nitros Oxide.
	parse("\"starting_character\":16");
	check_eq(ctr_cfg.starting_character, 0, "starting_character 16 -> Crash");
	parse("\"starting_character\":-1");
	check_eq(ctr_cfg.starting_character, 0, "starting_character -1 -> Crash");
	parse("\"starting_stat_class\":9");
	check_eq(ctr_cfg.starting_stat_class, 0, "starting_stat_class 9 -> vanilla");
	parse("\"stat_source\":7,\"stat_owner\":7");
	check_eq(ctr_cfg.stat_source, 0, "unknown stat_source -> vanilla, not a guess");
	check_eq(ctr_cfg.stat_owner, 0, "unknown stat_owner -> none");
}

static void test_progressive_never_exposes_an_edit_control()
{
	// The apworld guarantees this combination never ships. If one ever did,
	// silently offering an edit control over a received-item package is exactly
	// the read-only violation the 2026-08-08 ruling forbids.
	parse("\"stat_source\":1,\"stat_owner\":1,\"stat_editing_allowed\":true");
	check_eq(ctr_cfg.stat_source, 1, "stat_source stays progressive");
	check_eq(ctr_cfg.stat_editing_allowed, 0,
	         "progressive + editing_allowed is forced read-only");
}

static void test_racer_locks_block()
{
	parse("\"racer_locked_pads\":true",
	      "\"racer_locks\":{\"enabled\":true,"
	      "\"pads\":{\"3\":11,\"14\":2,\"102\":15}}");
	check_eq(ctr_cfg.racer_lock[3], 11, "pad 3 requires Komodo Joe");
	check_eq(ctr_cfg.racer_lock[14], 2, "pad 14 requires Tiny Tiger");
	check_eq(ctr_cfg.gem_cup_racer_lock[2], 15, "cup pad 102 requires Oxide");
	check_eq(ctr_cfg.racer_lock[0], -1, "an unlisted pad stays unlocked");
	check_eq(ctr_cfg.gem_cup_racer_lock[0], -1, "an unlisted cup stays unlocked");
}

static void test_racer_locks_off_leaves_every_pad_open()
{
	parse("\"racer_locked_pads\":false",
	      "\"racer_locks\":{\"enabled\":false,\"pads\":{}}");
	check_eq(ctr_cfg.racer_locked_pads, 0, "racer_locked_pads false");
	for (int pad = 0; pad < CTR_CFG_PAD_COUNT; pad++)
		check(ctr_cfg.racer_lock[pad] == -1, "locks off -> no pad locked");
}

static void test_a_lock_naming_an_unknown_racer_is_dropped_not_guessed()
{
	parse("\"racer_locked_pads\":true",
	      "\"racer_locks\":{\"enabled\":true,\"pads\":{\"5\":99,\"6\":4}}");
	check_eq(ctr_cfg.racer_lock[5], -1, "out-of-range racer -> lock dropped");
	check_eq(ctr_cfg.racer_lock[6], 4, "the neighbouring valid lock still lands");
}

static void test_malformed_block_does_not_corrupt_state()
{
	// Every one of these is a shape the apworld never emits. None may leave a
	// half-applied lock behind.
	parse("\"racer_locked_pads\":true", "\"racer_locks\":[]");
	check_eq(ctr_cfg.racer_lock[0], -1, "array instead of object -> ignored");
	parse("\"racer_locked_pads\":true", "\"racer_locks\":{\"pads\":\"nope\"}");
	check_eq(ctr_cfg.racer_lock[0], -1, "pads as a string -> ignored");
	parse("\"racer_locked_pads\":true",
	      "\"racer_locks\":{\"pads\":{\"notanint\":3,\"7\":1}}");
	check_eq(ctr_cfg.racer_lock[7], 1, "a bad key does not stop the good ones");
	parse("\"racer_locked_pads\":true",
	      "\"racer_locks\":{\"pads\":{\"7\":\"Coco\"}}");
	check_eq(ctr_cfg.racer_lock[7], -1, "a non-int value -> lock dropped");
}

static void test_a_reparse_clears_the_previous_seed()
{
	// A reconnect to a different room re-parses. A lock from the previous seed
	// surviving into this one would gate a pad nobody asked to gate.
	parse("\"racer_locked_pads\":true",
	      "\"racer_locks\":{\"pads\":{\"9\":6}}");
	check_eq(ctr_cfg.racer_lock[9], 6, "first seed's lock landed");
	parse("");
	check_eq(ctr_cfg.racer_lock[9], -1, "second seed cleared it");
	check_eq(ctr_cfg.starting_character, 0, "and reset the starting racer");
}

int main()
{
	test_absent_keys_are_pre_feature_behaviour();
	test_phase_presence_is_key_presence_not_value();
	test_scalars_round_trip();
	test_out_of_range_values_fail_closed();
	test_progressive_never_exposes_an_edit_control();
	test_racer_locks_block();
	test_racer_locks_off_leaves_every_pad_open();
	test_a_lock_naming_an_unknown_racer_is_dropped_not_guessed();
	test_malformed_block_does_not_corrupt_state();
	test_a_reparse_clears_the_previous_seed();

	if (g_failures == 0)
		std::printf("character-phase seedcfg parser: all checks passed\n");
	else
		std::printf("character-phase seedcfg parser: %d FAILURE(S)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
