// Alpha 4 progression diagnostic: podium-rung slot_data parser characterization.
//
// Build + run from the repository root:
//   g++ -m32 -std=c++17 -DCTR_AP -I ap/vendor/json/include
//   tools/test-podium-seedcfg.cpp ap/ap_seedcfg.cpp -o /tmp/test-podium-seedcfg
#include <cstdio>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

extern "C" void AP_LogLine(const char *) {}

static int checks;
static int failures;

static void expect(long got, long want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		std::printf("FAIL %s (got %ld, want %ld)\n", name, got, want);
	}
}

static nlohmann::json base(void)
{
	return {{"ctr_options", {{"schema_version", 7}}}};
}

static void test_roos_tubes_alpha3_array(void)
{
	nlohmann::json doc = base();
	doc["podium_checks"] = {
		{"enabled", true},
		{"locations", {{"6", {35015104, 35015105, 35015106, 35015107, 35015005}}}},
	};
	ap_seedcfg_parse_json(doc);

	expect(ctr_cfg.podium_enabled, 1, "Alpha 3 podium block enables the feature");
	expect(ctr_cfg.podium[6].held_1st, 35015104, "Roo's Tubes Held 1st");
	expect(ctr_cfg.podium[6].held_3rd, 35015105, "Roo's Tubes Held 3rd");
	expect(ctr_cfg.podium[6].held_5th, 35015106, "Roo's Tubes Held 5th");
	expect(ctr_cfg.podium[6].finish_podium, 35015107, "Roo's Tubes Finish on Podium");
	expect(ctr_cfg.podium[6].finish_any, 35015005, "Roo's Tubes Finish Any");
}

static void test_absent_and_reduced_rungs_reset(void)
{
	ap_seedcfg_parse_json(base());
	expect(ctr_cfg.podium_enabled, 0, "absent block disables podium checks");
	expect(ctr_cfg.podium[6].held_3rd, -1, "absent block resets Roo Held 3rd");
	expect(ctr_cfg.podium[6].held_5th, -1, "absent block resets Roo Held 5th");

	nlohmann::json doc = base();
	doc["podium_checks"] = {
		{"locations", {{"6", {35015104, nullptr, nullptr, 35015107, 35015005}}}},
	};
	ap_seedcfg_parse_json(doc);
	expect(ctr_cfg.podium_enabled, 1, "present schema-7 block defaults enabled");
	expect(ctr_cfg.podium[6].held_3rd, -1, "null Held 3rd remains absent");
	expect(ctr_cfg.podium[6].held_5th, -1, "null Held 5th remains absent");
}

static void test_wrong_track_key_cannot_poison_roo(void)
{
	nlohmann::json doc = base();
	doc["podium_checks"] = {
		{"locations", {{"5", {35015104, 35015105, 35015106, 35015107, 35015005}}}},
	};
	ap_seedcfg_parse_json(doc);
	expect(ctr_cfg.podium[6].held_3rd, -1, "wrong LevelID leaves Roo Held 3rd absent");
	expect(ctr_cfg.podium[5].held_3rd, 35015105, "wire remains physically keyed");
}

int main(void)
{
	test_roos_tubes_alpha3_array();
	test_absent_and_reduced_rungs_reset();
	test_wrong_track_key_cannot_poison_roo();
	std::printf("%s podium seedcfg (%d checks, %d failures)\n",
	            failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
