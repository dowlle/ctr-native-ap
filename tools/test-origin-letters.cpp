// g++ -m32 -std=c++17 -O2 -DCTR_AP -fsanitize=undefined -fno-sanitize-recover=all -Iap -Iap/vendor/json/include tools/test-origin-letters.cpp ap/ap_seedcfg.cpp -o /tmp/test-origin-letters
#include "ap_content_modes.h"
#include "ap_named_blocks.h"
#include "ap_seedcfg.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using J = nlohmann::json;
extern "C" void AP_LogLine(const char *) {}

static int failures;
static void expect(bool passed, const char *message)
{
    if (!passed) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

static const ctr_content_pad *load_pad(const J &wire, int physical = 8)
{
    ap_seedcfg_parse_json(wire);
    if (ap_seedcfg_rejected() || !ap_content_plan_present()) return nullptr;
    std::vector<int64_t> checks;
    for (const auto &row : wire["content_plan"]["checks"]) checks.push_back(row["location"].get<int64_t>());
    return ap_content_plan_bind("origin-letters", 0, 1, checks.data(), checks.size())
        ? ap_content_plan_pad(physical) : nullptr;
}

static auto named_track(J &plan)
{
    auto &tracks = plan["tracks"];
    return std::find_if(tracks.begin(), tracks.end(), [](const J &row) { return row.contains("named_block"); });
}

static J new_code(J wire, const std::string &glyphs)
{
    auto &plan = wire["content_plan"];
    auto track = named_track(plan);
    (*track)["letters"] = glyphs;
    plan["required_features"].push_back("origin_letters_v1");
    // The fixture draws the second and third slots on Baby T Park (block 8).
    for (int slot : {1, 2}) {
        const char glyph = glyphs[slot];
        for (auto &row : plan["checks"]) {
            if (row["location"] == 35201611 + slot) {
                row["location"] = ap_named_letter_location(8, glyph);
                row["display_name"] = std::string("Baby T Park: Letter ") + glyph;
            }
        }
        for (auto &row : plan["items"]["rows"]) {
            if (row["item"] == 35100040 + slot) row["item"] = ap_named_letter_item(8, glyph);
        }
    }
    return wire;
}

int main()
{
    int vanilla[20]{};
    ctr_cfg_set_vanilla_cup_legs(vanilla);
    J old;
    std::ifstream("tools/fixtures/content-plan/track-checks.json") >> old;
    const auto *pad = load_pad(old);
    expect(pad && ap_content_letter_glyph(pad, 1) == 'T' && pad->letter_items[1] == 35100041,
           "legacy room keeps the T glyph and item");
    const J cnk = new_code(old, "CNK");
    pad = load_pad(cnk);
    expect(pad && ap_content_letter_glyph(pad, 1) == 'N' && ap_content_letter_glyph(pad, 2) == 'K'
           && pad->letter_locations[1] == 35201754 && pad->letter_locations[2] == 35201751
           && pad->letter_items[1] == 38500301 && pad->letter_items[2] == 38500298,
           "CNK keeps logical slots and uses glyph-keyed location and item IDs");
    pad = load_pad(new_code(old, "M6C"));
    expect(pad && ap_content_letter_glyph(pad, 1) == '6' && pad->letter_locations[1] == 35201773
           && pad->letter_items[1] == 38500320 && pad->letter_locations[2] == 35201611
           && pad->letter_items[2] == 35100040, "digits work and C reuses its ID in the third slot");
    expect(ap_named_letter_bit(38500301) == 301 && ap_named_letter_bit(38500298) == 298
           && ap_named_letter_bit(35100040) == 290 && ap_named_letter_bit(38500290) == -1
           && ap_named_letter_bit(38859999) == 359999, "old and new item addresses map to the right received bits");
    expect(load_pad(new_code(old, "CTR")) != nullptr, "an explicit CTR code is valid");
    for (const auto &bad : {"CNN", "cnk", "CN!", "CNKK"}) {
        J wire = cnk;
        (*named_track(wire["content_plan"]))["letters"] = bad;
        expect(!load_pad(wire), "reject an invalid origin code");
    }
    J missing_feature = cnk;
    missing_feature["content_plan"]["required_features"].erase(
        std::prev(missing_feature["content_plan"]["required_features"].end()));
    expect(!load_pad(missing_feature), "reject origin letters without the feature");
    J cups;
    std::ifstream("tools/fixtures/content-plan/cups.json") >> cups;
    auto &cup_plan = cups["content_plan"];
    auto shared = named_track(cup_plan);
    (*shared)["letters"] = "CNK";
    cup_plan["required_features"].push_back("origin_letters_v1");
    const std::string entry_id = "entry/" + (*shared)["id"].get<std::string>();
    const auto &pads = cup_plan["pads"];
    const auto track_pad = std::find_if(pads.begin(), pads.end(), [&](const J &row) {
        return row["entry_id"] == entry_id;
    });
    const int physical = std::stoi(track_pad->at("id").get<std::string>().substr(4));
    pad = load_pad(cups, physical);
    expect(pad && ap_content_letter_glyph(pad, 1) == 'N',
           "a named CTR Challenge still admits when the same track appears in a cup");
    return failures != 0;
}
