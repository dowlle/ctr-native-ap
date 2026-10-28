// Host acceptance for the real slot-data parser and the authored retail box
// table. The count check is shared by the box spawn and pad count/glow paths.
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

	auto legacy = seed;
	legacy["item_box_checks"].erase("placement_counts");
	parse(legacy);
	CHECK(ctr_cfg.box_placement_state == 0);
	for (int level = 0; level < CTR_CFG_BOX_TRACK_COUNT; ++level)
		CHECK(ctr_cfg_box_placement_valid(level, native_count(level) + 1));
	legacy.erase("item_box_checks");
	parse(legacy);
	CHECK(ctr_cfg_box_placement_valid(3, native_count(3) + 1));

	parse(seed);
	CHECK(ctr_cfg.box_placement_state == 1);
	auto revision = ctr_cfg_box_placement_revision();
	for (int level = 0; level < CTR_CFG_BOX_TRACK_COUNT; ++level)
		CHECK(ctr_cfg_box_placement_valid(level, native_count(level)));

	auto drift = seed;
	drift["item_box_checks"]["placement_counts"]["3"] = native_count(3) + 1;
	parse(drift);
	CHECK(ctr_cfg_box_placement_revision() != revision);
	CHECK(!ctr_cfg_box_placement_valid(3, native_count(3)));
	CHECK(ctr_cfg_box_placement_valid(4, native_count(4)));
	CHECK(log_lines.find("CRASH_COVE (LevelID 3) placement count mismatch") != std::string::npos);
	CHECK(log_lines.find("seed 11, native 10") != std::string::npos);
	auto once = log_lines.size();
	CHECK(!ctr_cfg_box_placement_valid(3, native_count(3)));
	CHECK(log_lines.size() == once);
	// Reparse must clear the refusal and its diagnostic latch.
	parse(seed);
	CHECK(ctr_cfg_box_placement_valid(3, native_count(3)));
	auto zero = seed;
	zero["item_box_checks"]["placement_counts"]["3"] = 0;
	parse(zero);
	CHECK(ctr_cfg.box_placement_state == 1);
	CHECK(ctr_cfg_box_placement_valid(3, 0));
	CHECK(!ctr_cfg_box_placement_valid(3, native_count(3)));

	const nlohmann::json malformed[] = {
		-1, "10", true, 1.5, nlohmann::json::array(),
		nlohmann::json::object(), 2147483648ULL
	};
	for (const auto &value : malformed)
	{
		auto bad = seed;
		bad["item_box_checks"]["placement_counts"]["3"] = value;
		parse(bad);
		CHECK(ctr_cfg.box_placement_state == -1);
		CHECK(!ctr_cfg_box_placement_valid(3, native_count(3)));
		CHECK(!ctr_cfg_box_placement_valid(4, native_count(4)));
		CHECK(log_lines.find("placement_counts refused") != std::string::npos);
	}
	auto bad = seed;
	bad["item_box_checks"]["placement_counts"] = nlohmann::json::array();
	parse(bad);
	CHECK(ctr_cfg.box_placement_state == -1);
	bad = seed;
	bad["item_box_checks"]["placement_counts"]["3x"] = 10;
	parse(bad);
	CHECK(ctr_cfg.box_placement_state == -1);
	CHECK(!ctr_cfg_box_placement_valid(4, native_count(4)));
	bad = seed;
	bad["item_box_checks"]["placement_counts"]["03"] = 10;
	parse(bad);
	CHECK(ctr_cfg.box_placement_state == -1);
	parse(seed);
	CHECK(ctr_cfg_box_placement_valid(3, native_count(3)));

	std::printf("%s box placement guard (%d checks, %d failures)\n",
	            failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
