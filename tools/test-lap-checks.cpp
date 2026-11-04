// g++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include tools/test-lap-checks.cpp -o /tmp/test-lap-checks
// #344: the pure crossing rule, and the frozen lap codes in the content-plan
// registry. The plan's own lap records are tools/test-content-plan-parity.py.
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <set>
#include <string>
#include <nlohmann/json.hpp>
#include "../ap/ap_lap_checks.h"
#include "../ap/ap_content_registry.hpp"
using nlohmann::json;

static int checks = 0;
#define CHECK(x) do { assert(x); ++checks; } while (0)

int main()
{
    // The local kart's first accepted lap, and any later one.
    CHECK(AP_LapCrossingCounts(0, 0, 1, 1));
    CHECK(AP_LapCrossingCounts(0, 0, 3, 1));
    CHECK(!AP_LapCrossingCounts(1, 0, 1, 1)); // another driver
    CHECK(!AP_LapCrossingCounts(0, 1, 1, 1)); // a bot in the local slot
    CHECK(!AP_LapCrossingCounts(0, 0, 0, 1)); // no lap yet
    CHECK(!AP_LapCrossingCounts(0, 0, 1, 0)); // not a race with laps
    // Every registry track has one frozen code, in the three frozen blocks.
    const json registry = json::parse(kContentRegistryJSON);
    std::set<int64_t> codes;
    for (const auto &row : registry["retail"]) {
        const int64_t code = row["lap"].get<int64_t>();
        CHECK(code >= 35028000 && code < 35028018 && codes.insert(code).second);
    }
    CHECK(registry["cortex_vortex"]["lap"] == 35026009);
    for (size_t i = 0; i < registry["custom_lap_locations"].size(); ++i)
        CHECK(registry["custom_lap_locations"][i] == (int64_t)(35028100 + i));
    std::printf("Lap crossing rule and registry codes: %d checks passed\n", checks);
    return 0;
}
