// Hand-placed AP box spots (alpha3 row A3-14), the coordinate harness.
//
//   1. The shared fixture tools/fixtures/box_spots_builtin_rows.json (a
//      byte-identical copy of the apworld's
//      worlds/ctr/test/fixtures/box_spots_builtin_rows.json, which the apworld
//      checks against its 18 retail spot files) matches AP_EMBEDDED_PLACEMENTS
//      spot for spot: Item Box n of a level is that level's n-th table row,
//      with the same position and angle. So a retail box stands where it stood
//      in alpha2 whether its spot comes from the seed or from the table.
//   2. Codes: retail Item Box 1 to 15 from the spot formula equal the frozen
//      block (AP_BoxMap_Code), 16 to 50 are the alpha3 block 35014300 + 35 x
//      track, every code distinct; named Item Box n is offset 19 + n.
//   3. The pick shape the parser accepts.
//
//   c++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include
//     tools/test-box-spots.cpp -o /tmp/test-box-spots && /tmp/test-box-spots
//
// Run from the repository root (the fixture path is relative to it).

#include <cstdio>
#include <fstream>
#include <set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ap_box_map.h"
#include "ap_box_spot_logic.h"
#include "ap_named_blocks.h"
#include "ap_placements_data.h"

static int failures, checks;

static void expect(bool ok, const char *what)
{
	++checks;
	if (!ok)
	{
		std::printf("FAIL %s\n", what);
		++failures;
	}
}

int main()
{
	nlohmann::json fx;
	std::ifstream("tools/fixtures/box_spots_builtin_rows.json") >> fx;
	expect(fx.is_object() && fx["rows"].is_array() && fx["rows"].size() == (size_t)AP_EMBEDDED_PLACEMENT_COUNT,
	       "fixture holds one row per embedded placement");

	// 1. n-th row of a level, in table order, is spot n.
	std::vector<std::vector<const AP_EmbeddedPlacement *>> by_level(AP_BOX_TRACK_COUNT);
	for (int i = 0; i < AP_EMBEDDED_PLACEMENT_COUNT; ++i)
		by_level[AP_EMBEDDED_PLACEMENTS[i].level].push_back(&AP_EMBEDDED_PLACEMENTS[i]);
	size_t matched = 0;
	for (const auto &row : fx["rows"])
	{
		const int level = row[0], number = row[1];
		if (level < 0 || level >= AP_BOX_TRACK_COUNT || number < 1 || number > (int)by_level[level].size())
		{
			expect(false, "fixture row names a level and number the table has");
			continue;
		}
		const AP_EmbeddedPlacement *p = by_level[level][number - 1];
		bool same = p->x == row[2].get<int>() && p->y == row[3].get<int>() && p->z == row[4].get<int>() &&
		            p->rotY == row[5].get<int>();
		if (!same)
			std::printf("  level %d Item Box %d differs from the embedded table\n", level, number);
		expect(same, "built-in spot equals its embedded placement row");
		matched += same;
	}
	size_t embedded = 0;
	for (const auto &rows : by_level)
		embedded += rows.size();
	expect(matched == embedded && matched == 241, "all 241 built-in spots match");

	// 2. Codes.
	std::set<long> codes;
	bool frozen = true, distinct = true, ext = true;
	for (int level = 0; level < AP_BOX_TRACK_COUNT; ++level)
	{
		const int track = AP_BoxMap_ApTrack(level);
		for (int n = 1; n <= AP_BOX_SPOT_MAX_NUMBER; ++n)
		{
			const long code = AP_BoxSpot_RetailCode(track, n);
			if (n <= 15)
				frozen &= code == AP_BoxMap_Code(level, n - 1);
			else
				ext &= code == 35014300L + 35L * track + (n - 16);
			distinct &= codes.insert(code).second && code >= 35014000L && code < 35015000L;
		}
	}
	expect(frozen, "retail Item Box 1 to 15 keep the frozen codes");
	expect(ext, "retail Item Box 16 to 50 use the alpha3 block");
	expect(distinct && codes.size() == 18u * 50u && *codes.rbegin() == 35014929L, "900 distinct retail box codes up to 35014929");
	expect(AP_BoxSpot_RetailCode(-1, 1) < 0 && AP_BoxSpot_RetailCode(18, 1) < 0 && AP_BoxSpot_RetailCode(0, 0) < 0 &&
	       AP_BoxSpot_RetailCode(0, 51) < 0, "retail codes refuse bad tracks and numbers");
	expect(AP_BoxSpot_NamedOffset(1) == AP_NAMED_OFF_ITEM_BOX_1 && AP_BoxSpot_NamedOffset(15) == 34 &&
	       AP_BoxSpot_NamedOffset(16) == 35 && AP_BoxSpot_NamedOffset(50) == 69 && AP_BoxSpot_NamedOffset(0) < 0 &&
	       AP_BoxSpot_NamedOffset(51) < 0, "named Item Box n is offset 19 + n, 1 to 50");
	expect(ap_named_location_code(8, AP_BoxSpot_NamedOffset(16)) == 35200000 + 200 * 8 + 35, "named block 8 Item Box 16");

	// 3. Pick shape.
	const int ten[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	const int fifteen[15] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16, 18, 20, 22, 23};
	const int down[3] = {3, 2, 1}, dup[3] = {1, 1, 2}, high[1] = {51};
	expect(AP_BoxSpot_PickShape(10, ten, 10), "a pool of 10 picks all 10");
	expect(!AP_BoxSpot_PickShape(10, ten, 9), "a pool of 10 must pick all 10");
	expect(AP_BoxSpot_PickShape(18, fifteen, 15), "a pool of 18 picks 15");
	expect(!AP_BoxSpot_PickShape(18, ten, 10), "a pool of 18 must pick 15");
	expect(!AP_BoxSpot_PickShape(3, down, 3) && !AP_BoxSpot_PickShape(3, dup, 3), "picks are strictly ascending");
	expect(!AP_BoxSpot_PickShape(1, high, 1) && !AP_BoxSpot_PickShape(51, fifteen, 15), "numbers and pool stay within 50");
	expect(AP_BoxSpot_PickShape(0, nullptr, 0), "an empty pool picks nothing");

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
