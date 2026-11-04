// Check the authored retail placement table against its frozen fixture.
// Plan box-slot admission is covered in test-content-plan.cpp. The normal
// placement_counts parser was removed by one generator row 12.
// g++ -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include -o /tmp/test-box-placement-guard tools/test-box-placement-guard.cpp ap/ap_seedcfg.cpp
#include <cstdio>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"
#include "../ap/ap_placement_table.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);
static std::string log_lines;
extern "C" void AP_LogLine(const char *line) { log_lines += line; }

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

static int native_count(int level)
{
	AP_PlacementTable table = {AP_PLACEMENT_SRC_EMBEDDED, nullptr, 0};
	int count = 0;
	for (int row = 0; row < AP_PlacementTable_Count(&table); ++row)
	{
		int placed_level;
		if (AP_PlacementTable_Get(&table, row, &placed_level, nullptr, nullptr, nullptr, nullptr) &&
		    placed_level == level)
			++count;
	}
	return count;
}

static nlohmann::json fixture()
{
	std::ifstream in("tools/fixtures/box-colours/on_boxes_seed2026092319.slot_data.json");
	CHECK(in.good());
	if (!in.good())
		return nlohmann::json::object();
	return nlohmann::json::parse(in);
}

static void parse(const nlohmann::json &j)
{
	log_lines.clear();
	ap_seedcfg_parse_json(j);
}

int main()
{
	auto seed = fixture();
	if (!seed.contains("item_box_checks"))
		return 1;
	for (int level = 0; level < CTR_CFG_BOX_TRACK_COUNT; ++level)
		CHECK(seed["item_box_checks"]["placement_counts"][std::to_string(level)] == native_count(level));

	std::printf("%s box placement guard (%d checks, %d failures)\n",
	            failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
