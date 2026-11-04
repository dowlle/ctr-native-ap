// #306 named custom-track blocks and room titles, through the production parser.
// g++ -m32 -std=c++17 -DCTR_AP -fsanitize=undefined -fno-sanitize-recover=all -Iap -Iap/vendor/json/include tools/test-named-blocks.cpp ap/ap_seedcfg.cpp -o /tmp/test-named-blocks
//
// Fixtures under tools/fixtures/named-blocks are slot_data the paired apworld
// emitted (see the README there), edited where the README says: Baby T Park
// on its shipped named block 8, and the generic fixture on the unregistered
// `fixture-park` key, because native now holds the named-block table.
#include "ap_seedcfg.h"
#include "ap_content_plan.h"
#include "ap_named_blocks.h"
#include "ap_lettersanity.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using J = nlohmann::json;
extern "C" void AP_LogLine(const char *) {}
static int checks, failures;
static void expect(bool good, const std::string &why) {
    ++checks;
    if (!good) { ++failures; std::cout << "FAIL: " << why << '\n'; }
}
static J load(const char *name) {
    J j;
    std::ifstream(std::string("tools/fixtures/named-blocks/") + name) >> j;
    return j;
}
static std::vector<int64_t> locations(const J &j) {
    std::vector<int64_t> ids;
    for (const auto &c : j["content_plan"]["checks"]) ids.push_back(c["location"].get<int64_t>());
    return ids;
}
static bool admit(const J &j) {
    ap_seedcfg_parse_json(j);
    if (ap_seedcfg_rejected() || !ap_content_plan_present()) return false;
    const auto ids = locations(j);
    return ap_content_plan_bind("seed-A", 0, 1, ids.data(), ids.size()) != 0;
}
static const ctr_content_pad *custom_pad() {
    for (int id = 0; id <= 110; ++id) {
        const auto *p = ap_content_plan_pad(id);
        if (p && p->occupied && p->retail_id < 0) return p;
    }
    return nullptr;
}
static J &custom_track(J &j) {
    for (auto &t : j["content_plan"]["tracks"]) if (t["origin"] == "custom") return t;
    throw std::runtime_error("fixture has no custom track");
}
static J &custom_check(J &j) {
    const auto owner = custom_track(j)["id"];
    for (auto &c : j["content_plan"]["checks"]) if (c["owner"] == owner) return c;
    throw std::runtime_error("fixture has no custom check");
}

int main() {
    int vanilla[20]{}; ctr_cfg_set_vanilla_cup_legs(vanilla);

    // Id math, the same numbers the apworld's test_named_tracks pins.
    expect(ap_named_location_code(0, AP_NAMED_OFF_TROPHY) == 35200000, "block 0 trophy");
    expect(ap_named_location_code(1, AP_NAMED_OFF_TROPHY) == 35200200, "block 1 trophy");
    expect(ap_named_location_code(2, AP_NAMED_OFF_LETTER_R) == 35200413, "block 2 Letter R");
    expect(ap_named_location_code(9999, AP_NAMED_OFF_TROPHY) == 37199800, "last block trophy");
    expect(ap_named_item_code(9999, 2) == 35149997, "last block Letter R item");
    expect(ap_named_location_code(-1, 0) == -1 && ap_named_location_code(10000, 0) == -1 &&
           ap_named_location_code(0, 200) == -1 && ap_named_item_code(0, 5) == -1, "out-of-range blocks refused");
    expect(ap_room_title_ok("Crash Cove (Custom)", 19) && !ap_room_title_ok("Bad [x]", 7) &&
           !ap_room_title_ok("Tab\tName", 8) && !ap_room_title_ok(" Lead", 5) &&
           !ap_room_title_ok("", 0) && !ap_room_title_ok(std::string(106, 'A').c_str(), 106) &&
           ap_room_title_ok(std::string(105, 'A').c_str(), 105), "room title rules");

    // Path C: a named block replaces the generic slot code.
    const J named = load("content-plan-named.json");
    expect(admit(named), "named-block content plan admits and binds");
    const auto *p = custom_pad();
    expect(p && p->named_block == 8 && p->trophy_location == 35201600 && p->custom_slot == 1 &&
           std::string(p->room_title) == "Baby T Park" && std::string(p->display_name) == "Baby T Park",
           "named block pad: code, slot, room title and package title");

    // Path B+: generic slot code, room title carried.
    const J generic = load("content-plan-generic.json");
    expect(admit(generic), "generic content plan admits and binds");
    p = custom_pad();
    expect(p && p->named_block == -1 && p->trophy_location == 35016300 &&
           std::string(p->room_title) == "Fixture Park", "generic pad keeps slot code and title");

    // A plan without room_title (the 2026-09-22 fixtures) still admits; the
    // room title is the package title.
    J old = generic; custom_track(old).erase("room_title");
    expect(admit(old) && (p = custom_pad()) && std::string(p->room_title) == "Fixture Park", "room_title is optional");

    auto refuse = [](const J &j, const std::string &why) {
        ap_seedcfg_parse_json(j);
        expect(!ap_content_plan_present(), why);
    };
    J bad = named; custom_check(bad)["location"] = 35016300; refuse(bad, "named block with the slot code");
    bad = named; custom_track(bad)["named_block"] = 1; refuse(bad, "named block disagrees with the check code");
    bad = named; custom_track(bad)["named_block"] = 10000; refuse(bad, "named block out of range");
    bad = named; custom_track(bad)["named_block"] = -1; refuse(bad, "negative named block");
    bad = named; custom_track(bad)["named_block"] = true; refuse(bad, "boolean named block");
    bad = named; custom_track(bad)["named_block"] = 0.0; refuse(bad, "float named block");
    bad = generic; custom_check(bad)["location"] = 35200000; refuse(bad, "named code without a named block");
    bad = generic; custom_track(bad)["named_block"] = 0; custom_track(bad)["room_title"] = "Acid Lake";
    custom_check(bad)["location"] = 35200000; custom_check(bad)["display_name"] = "Acid Lake: Trophy Race";
    refuse(bad, "unregistered key claims Acid Lake's block");
    bad = named; custom_track(bad).erase("named_block"); custom_check(bad)["location"] = 35016300;
    refuse(bad, "Baby T Park moved off its named block");
    bad = generic; custom_track(bad)["room_title"] = "Other"; refuse(bad, "check label disagrees with room title");
    bad = generic; custom_track(bad)["room_title"] = "Bad [x]";
    custom_check(bad)["display_name"] = "Bad [x]: Trophy Race"; refuse(bad, "markup bracket in room title");
    bad = generic; custom_track(bad)["room_title"] = std::string(106, 'A');
    custom_check(bad)["display_name"] = std::string(106, 'A') + ": Trophy Race"; refuse(bad, "overlong room title");
    bad = generic; custom_track(bad)["room_title"] = 7; refuse(bad, "room title is not text");
    bad = generic; custom_track(bad)["unexpected_field"] = 1; refuse(bad, "other extra fields still refused");

    // Shared title folding remains independent of room admission.
    {
        char out[32];
        ap_room_title_fold_ascii("Caf\xC3\xA9 \xC3\x9C" "ber \xE2\x80\x98" "x\xE2\x80\x99 \xE6\x97\xA5", out, sizeof out);
        expect(std::string(out) == "Cafe Uber 'x' ?", "non-ASCII folds to ASCII in game");
        ap_room_title_fold_ascii("Lodz \xC5\x81\xC3\x93", out, 6);
        expect(std::string(out) == "Lodz ", "fold truncates to the buffer");
        ap_room_title_fold_ascii("A\xC3", out, sizeof out);
        expect(std::string(out) == "A?", "truncated sequence folds to ?");
    }
    int slot = -1, letter = -1;
    expect(AP_CustomLetterItemToIdentityForTrackPure(35100040, 8, 1, &slot, &letter) &&
           slot == 1 && letter == 0, "received named Letter C maps onto the track's slot");
    ap_seedcfg_parse_json(J::object());

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
