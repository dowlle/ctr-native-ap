// g++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include tools/test-lap-checks.cpp ap/ap_seedcfg.cpp -o /tmp/test-lap-checks
// #344: the normal room's `lap_checks` block through the real parser
// (ap/ap_seedcfg.cpp is linked in), the frozen retail code table against the
// content-plan registry, and the pure crossing and lookup helpers.
// `--probe` reads one slot_data JSON per line and prints the parsed table, for
// a round trip against the paired apworld's emitted rooms.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"
#include "../ap/ap_named_blocks.h"
#include "../ap/ap_content_registry.hpp"
extern "C" void AP_LogLine(const char *) {}
using nlohmann::json;

static json all_retail() {
    json tracks = json::object();
    for (int lid = 0; lid < AP_LAP_RETAIL_TRACKS; ++lid) tracks[std::to_string(lid)] = AP_LapRetailCode(lid);
    tracks["110"] = AP_LAP_CORTEX_CODE;
    return tracks;
}
static json seed() {
    return {{"ctr_options", {{"schema_version", 18}, {"lap_completion_checks", true}}},
            {"lap_checks", {{"version", 1}, {"tracks", all_retail()}, {"custom_slots", json::object()}}}};
}
// A legacy custom track entry (tools/test-custom-tracks-seedcfg.cpp's shape).
static json custom_block(bool named) {
    json flags = {{"crates", true}, {"ctr_letters", true}, {"relic_crates", true}, {"ai_nav", true}, {"minimap", false},
                  {"ghosts", false}, {"wumpa_collectible", true}, {"spawns", 8}, {"checkpoints", 35}};
    json e = {{"id", "baby-t-park"}, {"package_uuid", "60d5a8a8-b69a-4f6a-a0d8-9a43d91e3f2e"}, {"package_version", "1.0.0"},
              {"minimum_client_version", "0.2.0-alpha6"}, {"minimum_apworld_version", "0.2.0-alpha6"},
              {"lev_sha256", "be161e0b11aa03505c501b7db012d830405e69e171f84633c2e24ffe9cc3cbf8"},
              {"vrm_sha256", "2dcaa0fe93359c7ae00fb93842a581210e0dcc2db73f4de43508375834092e83"},
              {"navigation", {{"uuid", "898a9315-693f-4ed3-b6a0-fbe50db8bc40"}, {"revision", 1}}},
              {"laps", 7}, {"slot", 1}, {"host_level_id", 6}, {"replaces_cup_level_id", 104}, {"boxes", false},
              {"flags", flags},
              {"locations", {{"trophy", 35016300}, {"podium", {35016400, 35016401, -1, 35016403, 35016404}}}}};
    if (named) {
        e["named_block"] = 8;
        e["locations"] = {{"trophy", 35201600}, {"podium", {35201601, 35201602, -1, 35201604, 35201605}}};
    }
    return {{"enabled", true}, {"version", CTR_CFG_CT_BLOCK_VERSION_KNOWN}, {"tracks", json::array({e})}};
}

static int checks = 0;
#define CHECK(x) do { assert(x); ++checks; } while (0)

static void refused(const json &j) {
    ap_seedcfg_parse_json(j);
    CHECK(!ctr_cfg_active() && !ctr_lap_checks.enabled);
}

int main(int argc, char **argv) {
    int vanilla[20] = {3, 9, 2, 5, 6, 14, 12, 10, 4, 8, 1, 11, 0, 15, 7, 13, 6, 5, 1, 7};
    ctr_cfg_set_vanilla_cup_legs(vanilla);
    if (argc > 1 && std::string(argv[1]) == "--probe") {
        std::string line;
        while (std::getline(std::cin, line)) {
            ap_seedcfg_parse_json(json::parse(line));
            json r = {{"active", ctr_cfg_active() != 0}, {"enabled", ctr_lap_checks.enabled != 0},
                      {"tracks", json::object()}, {"custom_slots", json::object()}};
            for (int lid = 0; lid < AP_LAP_RETAIL_TRACKS; ++lid)
                if (ctr_lap_checks.tracks[lid] > 0) r["tracks"][std::to_string(lid)] = ctr_lap_checks.tracks[lid];
            if (ctr_lap_checks.cortex > 0) r["tracks"]["110"] = ctr_lap_checks.cortex;
            if (ctr_lap_checks.custom > 0) r["custom_slots"][std::to_string(ctr_lap_checks.custom_slot)] = ctr_lap_checks.custom;
            std::cout << "PROBE " << r.dump() << std::endl;
        }
        return 0;
    }

    // The compiled retail table is the content-plan registry's `lap` row per
    // engine id, and the Cortex Vortex and generic codes match too.
    const json registry = json::parse(kContentRegistryJSON);
    std::set<long> codes;
    for (const auto &row : registry["retail"]) {
        CHECK(AP_LapRetailCode(row["id"].get<int>()) == row["lap"].get<long>());
        codes.insert(row["lap"].get<long>());
    }
    CHECK(codes.size() == AP_LAP_RETAIL_TRACKS && *codes.begin() == AP_LAP_RETAIL_CODE_BASE &&
          *codes.rbegin() == AP_LAP_RETAIL_CODE_BASE + 17);
    CHECK(registry["cortex_vortex"]["lap"].get<long>() == AP_LAP_CORTEX_CODE);
    for (int s = 1; s <= 32; ++s) CHECK(registry["custom_lap_locations"][s - 1].get<long>() == AP_LAP_CUSTOM_CODE_BASE + s - 1);
    CHECK(AP_LapRetailCode(-1) == -1 && AP_LapRetailCode(18) == -1 && AP_LapRetailCode(110) == -1);
    CHECK(ap_named_location_code(8, AP_NAMED_OFF_LAP) == 35201616);

    // Off, absent: nothing, and the seed is admitted.
    ap_seedcfg_parse_json({{"ctr_options", {{"schema_version", 18}}}});
    CHECK(ctr_cfg_active() && !ctr_lap_checks.enabled);
    CHECK(AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_RETAIL, 0) == -1);
    ap_seedcfg_parse_json({{"ctr_options", {{"schema_version", 18}, {"lap_completion_checks", false}}}});
    CHECK(ctr_cfg_active() && !ctr_lap_checks.enabled);

    // On: every key maps to its frozen code.
    ap_seedcfg_parse_json(seed());
    CHECK(ctr_cfg_active() && ctr_lap_checks.enabled);
    for (int lid = 0; lid < AP_LAP_RETAIL_TRACKS; ++lid)
        CHECK(AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_RETAIL, lid) == AP_LapRetailCode(lid));
    CHECK(AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_CORTEX, 110) == AP_LAP_CORTEX_CODE);
    CHECK(AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_CUSTOM, 1) == -1);
    CHECK(AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_RETAIL, -1) == -1);
    CHECK(AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_RETAIL, 18) == -1);
    CHECK(AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_NONE, 0) == -1);

    // A subset: absent tracks have no code.
    json subset = seed();
    subset["lap_checks"]["tracks"].erase("3");
    subset["lap_checks"]["tracks"].erase("110");
    ap_seedcfg_parse_json(subset);
    CHECK(ctr_lap_checks.enabled && ctr_lap_checks.tracks[3] == -1 && ctr_lap_checks.cortex == -1 &&
          ctr_lap_checks.tracks[4] == AP_LapRetailCode(4));

    // Every parse resets: a later room without the block has no table.
    ap_seedcfg_parse_json({{"ctr_options", {{"schema_version", 18}}}});
    CHECK(!ctr_lap_checks.enabled && ctr_lap_checks.tracks[4] == -1);

    // Refusals.
    for (int mutation = 0; mutation < 19; ++mutation) {
        json b = seed();
        switch (mutation) {
        case 0: b["ctr_options"]["lap_completion_checks"] = 1; break;
        case 1: b.erase("lap_checks"); break;
        case 2: b["ctr_options"]["lap_completion_checks"] = false; break;
        case 3: b["lap_checks"]["version"] = 2; break;
        case 4: b["lap_checks"]["version"] = true; break;
        case 5: b["lap_checks"]["extra"] = 0; break;
        case 6: b["lap_checks"].erase("custom_slots"); break;
        case 7: b["lap_checks"]["tracks"]["0"] = AP_LapRetailCode(1); break;
        case 8: b["lap_checks"]["tracks"]["18"] = 35028018; break;
        case 9: b["lap_checks"]["tracks"]["03"] = AP_LapRetailCode(3); break;
        case 10: b["lap_checks"]["tracks"]["110"] = 35026008; break;
        case 11: b["lap_checks"]["tracks"]["x"] = 1; break;
        case 12: b["lap_checks"]["tracks"]["5"] = true; break;
        case 13: b["lap_checks"]["custom_slots"]["1"] = 35028100; break; // no custom track admitted
        case 14: b["lap_checks"]["tracks"] = json::array(); break;
        case 15: b["ctr_options"]["schema_version"] = 17; break;
        case 16: b["lap_checks"]["tracks"]["-1"] = 1; break;
        case 17: b["lap_checks"]["custom_slots"] = json::array(); break;
        case 18: b["lap_checks"] = 0; break;
        }
        refused(b);
    }
    // The flag without a block is refused even when false-y blocks are absent.
    refused({{"ctr_options", {{"schema_version", 18}}}, {"lap_checks", seed()["lap_checks"]}});

    // The legacy custom track: its generic slot or its named block's offset 16.
    for (int named = 0; named < 2; ++named) {
        json b = seed();
        b["custom_tracks"] = custom_block(named != 0);
        const long code = named ? 35201616 : 35028100;
        b["lap_checks"]["custom_slots"]["1"] = code;
        ap_seedcfg_parse_json(b);
        CHECK(ctr_cfg_active() && ctr_cfg.custom_tracks_ok && ctr_lap_checks.enabled);
        CHECK(AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_CUSTOM, 1) == code);
        CHECK(AP_LapCodeFor(&ctr_lap_checks, AP_LAP_ID_CUSTOM, 2) == -1);
        b["lap_checks"]["custom_slots"]["1"] = named ? 35028100 : 35201616; // the other path's code
        refused(b);
        b["lap_checks"]["custom_slots"].erase("1");
        b["lap_checks"]["custom_slots"]["2"] = 35028101; // not the admitted slot
        refused(b);
    }

    // The crossing rule: the local player's kart, lap 1 and later, in an
    // Adventure race with laps.
    CHECK(AP_LapCrossingCounts(0, 0, 1, 1));
    CHECK(AP_LapCrossingCounts(0, 0, 3, 1));
    CHECK(!AP_LapCrossingCounts(1, 0, 1, 1)); // the second player
    CHECK(!AP_LapCrossingCounts(0, 1, 1, 1)); // a bot
    CHECK(!AP_LapCrossingCounts(0, 0, 0, 1)); // the start-line crossing
    CHECK(!AP_LapCrossingCounts(0, 0, 1, 0)); // boss race, Arcade, demo
    std::printf("Lap checks parser, registry table and crossing rule: %d checks passed\n", checks);
    return 0;
}
