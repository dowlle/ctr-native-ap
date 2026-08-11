// Out-of-engine parser assertions for the Lettersanity slot_data block.
//
//   c++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include \
//     tools/test-lettersanity-seedcfg.cpp ap/ap_seedcfg.cpp \
//     -o /tmp/test-lettersanity-seedcfg && /tmp/test-lettersanity-seedcfg

#include <cstdio>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"

extern "C" void AP_LogLine(const char *) {}

static int failures;
#define EXPECT(expr, why) do { int ok = !!(expr); std::printf("%-4s %s\n", ok ? "ok" : "FAIL", why); if (!ok) failures++; } while (0)

int main()
{
	int vanilla_legs[20] = {0};
	ctr_cfg_set_vanilla_cup_legs(vanilla_legs);

	nlohmann::json both = {
		{"ctr_options", {{"schema_version", 7}}},
		{"lettersanity_checks", {
			{"mode", 2},
			{"letters_per_track", 2},
			{"locations", {{"0", {1001, -1, 1003}}, {"15", {-1, 1152, 1153}}}}
		}}
	};
	ap_seedcfg_parse_json(both);
	EXPECT(ctr_cfg.schema_version == 7, "schema remains active");
	EXPECT(ctr_cfg.lettersanity_mode == 2, "both mode parsed");
	EXPECT(ctr_cfg.lettersanity_locations[0][0] == 1001, "first code parsed");
	EXPECT(ctr_cfg.lettersanity_locations[0][1] == -1, "inactive sentinel preserved");
	EXPECT(ctr_cfg.lettersanity_locations[15][2] == 1153, "last track code parsed");

	nlohmann::json absent = {{"ctr_options", {{"schema_version", 7}}}};
	ap_seedcfg_parse_json(absent);
	EXPECT(ctr_cfg.lettersanity_mode == 0, "absent block resets mode off");
	EXPECT(ctr_cfg.lettersanity_locations[0][0] == -1, "absent block resets codes");

	nlohmann::json malformed = {
		{"ctr_options", {{"schema_version", 7}}},
		{"lettersanity_checks", {
			{"mode", 3},
			{"locations", {{"bad", {1, 2, 3}}, {"16", {1, 2, 3}}, {"1", "wrong"}}}
		}}
	};
	ap_seedcfg_parse_json(malformed);
	EXPECT(ctr_cfg.lettersanity_mode == 3, "items-only mode parsed");
	EXPECT(ctr_cfg.lettersanity_locations[1][0] == -1, "malformed entries stay absent");

	std::printf("\n%s\n", failures ? "FAILURES PRESENT" : "all assertions passed");
	return failures != 0;
}
