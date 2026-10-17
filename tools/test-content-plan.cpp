// Production content-plan admission and retained room identity.
// g++ -m32 -std=c++17 -DCTR_AP -fsanitize=undefined -fno-sanitize-recover=all -Iap -Iap/vendor/json/include tools/test-content-plan.cpp ap/ap_seedcfg.cpp -o /tmp/test-content-plan
#include "ap_seedcfg.h"
#include "ap_content_plan.h"
#include "ap_content_modes.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstring>
#include <set>
using J = nlohmann::json;
extern "C" void AP_LogLine(const char *) {}
static int checks, failures;
static void expect(bool good, const std::string &why) {
    ++checks;
    if (!good) { ++failures; std::cout << "FAIL: " << why << '\n'; }
}
static std::vector<int64_t> locations(const J &j) {
    std::vector<int64_t> ids;
    for (const auto &c : j["content_plan"]["checks"]) ids.push_back(c["location"].get<int64_t>());
    return ids;
}
static bool bind_room(const J &j, const char *seed = "seed-A", int team = 0, int slot = 1) {
    const auto ids = locations(j);
    return ap_content_plan_bind(seed, team, slot, ids.data(), ids.size());
}
static bool parse(const J &j) {
    ap_seedcfg_parse_json(j);
    return !ap_seedcfg_rejected() && ap_content_plan_present();
}
static void refuse(const J &j, const std::string &why) {
    expect(!parse(j) && !ctr_cfg_active() && !ap_content_plan_active() &&
           ap_content_plan_epoch() == 0 && !ap_content_plan_pad(3) && !ap_content_plan_package(0), why);
}
static void mutations(const J &base, const J &node, const J::json_pointer &path) {
    if (node.is_object()) {
        J changed = base;
        changed[path]["unexpected_field"] = 1;
        refuse(changed, "extra field at " + path.to_string());
        for (auto it = node.begin(); it != node.end(); ++it) {
            // Optional custom-track fields (#306) and the envelope's
            // reserve_top_up Boolean (#451): absent is a legal plan.
            if (it.key() != "room_title" && it.key() != "named_block" && it.key() != "reserve_top_up") {
                changed = base; changed[path].erase(it.key());
                refuse(changed, "missing field at " + path.to_string() + "/" + it.key());
            }
            mutations(base, it.value(), path / it.key());
        }
    } else if (node.is_array()) {
        for (size_t i = 0; i < node.size(); ++i) mutations(base, node[i], path / std::to_string(i));
    } else if (node.is_number_integer()) {
        J changed = base; changed[path] = node.get<double>();
        refuse(changed, "float token at " + path.to_string());
        changed[path] = true;
        refuse(changed, "bool token at " + path.to_string());
    }
}
int main(int argc, char **argv) {
    int vanilla[20]{}; ctr_cfg_set_vanilla_cup_legs(vanilla);
    if (argc > 1 && std::string(argv[1]) == "--probe") {
        std::string line;
        while (std::getline(std::cin, line)) {
            try { std::cout << (parse(J::parse(line)) ? "ACCEPT" : "REFUSE") << std::endl; }
            catch (const std::exception &) { std::cout << "REFUSE" << std::endl; }
        }
        return 0;
    }
    J base, extras;
    std::ifstream("tools/fixtures/content-plan/baseline.json") >> base;
    std::ifstream("tools/fixtures/content-plan/starts-extras.json") >> extras;
    expect(parse(base) && !ap_content_plan_active() && !ap_content_plan_pad(3), "parsed plan is not admitted before room binding");
    expect(bind_room(base) && ap_content_plan_active(), "actual generated room binds");
    const uint64_t original_epoch = ap_content_plan_epoch();
    expect(original_epoch != 0, "nonzero admitted epoch");
    int occupied = 0, custom = 0, empty = 0;
    for (int id = 0; id <= 110; ++id) {
        const auto *p = ap_content_plan_pad(id);
        if (!p) continue;
        if (!p->occupied) { ++empty; continue; }
        ++occupied;
        expect(p->hub >= 25 && p->hub <= 29 && p->trophy_location > 0 && p->entry_id[0], "owned pad route");
        if (p->retail_id < 0) {
            ++custom;
            const auto *pkg = ap_content_plan_package(p->package_index);
            // Baby T Park owns named block 8 (#133), so its Trophy uses the block's code.
            expect(p->laps == 7 && p->custom_slot == 1 && p->named_block == 8 && p->trophy_location == 35201600 && pkg &&
                   std::string(pkg->revision) == "1.0.2" && pkg->lev_bytes == 2558168, "exact custom revision and authored laps");
        }
    }
    expect(occupied == 19 && custom == 1 && empty == 8, "19 occupied and eight empty pads");
    expect(!ap_content_plan_package(-1) && !ap_content_plan_package(2) && !ap_content_plan_item(35010015), "out-of-registry lookup refused");
    int gems[5] = {1, 1, 1, 1, 0};
    expect(!ap_content_plan_goal(gems), "goal needs all five selected Gem colours");
    gems[4] = 1; expect(ap_content_plan_goal(gems), "selected Gem goal met");
    gems[0] = 99; gems[4] = 0; expect(!ap_content_plan_goal(gems), "extra Gems are not distinct colours");
    J two_colours = base;
    for (auto &r : two_colours["content_plan"]["items"]["rows"]) {
        const int id = r["item"].get<int>();
        if (id == 35010010 || id == 35010012 || id == 35010013) {
            r["base"] = 0; r["remaining"] = 0; r["receipt_cap"] = 0; r["requirement_ceiling"] = 0;
        }
        if (id == 35010010) { r["extra"] = 1; r["remaining"] = 1; r["receipt_cap"] = 1; }
    }
    two_colours["content_plan"]["items"]["filler_count"] = 7;
    two_colours["content_plan"]["goal"] = {{"op", "distinct"}, {"items", J::array({35010009, 35010011})}, {"count", 2}};
    two_colours["ctr_options"]["goal_gems"] = 2;
    expect(parse(two_colours) && bind_room(two_colours), "selected two-colour Gem profile binds");
    int subset[5] = {1, 99, 0, 99, 99};
    expect(!ap_content_plan_goal(subset), "unselected Gem colours cannot satisfy the goal");
    subset[2] = 1; expect(ap_content_plan_goal(subset), "selected red and blue satisfy goal");
    expect(parse(base) && bind_room(base), "return to original plan");
    const uint64_t reconnect_epoch = ap_content_plan_epoch();
    expect(parse(base) && bind_room(base) && ap_content_plan_epoch() == reconnect_epoch, "same room reconnect preserves epoch");
    expect(parse(base) && bind_room(base, "seed-B") && ap_content_plan_epoch() != original_epoch, "different seed invalidates old epoch");
    auto previous = ap_content_plan_epoch();
    expect(parse(base) && bind_room(base, "seed-B", 1) && ap_content_plan_epoch() != previous, "different team invalidates old epoch");
    previous = ap_content_plan_epoch();
    expect(parse(base) && bind_room(base, "seed-B", 1, 2) && ap_content_plan_epoch() != previous, "different slot invalidates old epoch");
    previous = ap_content_plan_epoch();
    expect(parse(extras) && bind_room(extras, "seed-B", 1, 2) && ap_content_plan_epoch() != previous, "changed plan invalidates epoch");
    const auto *key = ap_content_plan_item(35010014);
    const auto *trophy = ap_content_plan_item(35010000);
    expect(key && key->base == 4 && key->extra == 2 && key->start_from_pool == 2 && key->remaining == 4 && key->receipt_cap == 6, "exact Key accounting");
    expect(trophy && trophy->start_additional == 3 && trophy->remaining == 6 && trophy->receipt_cap == 9, "additional starts retained");
    auto ids = locations(base);
    parse(base); ids.back()++;
    expect(!ap_content_plan_bind("seed-A", 0, 1, ids.data(), ids.size()) && ap_seedcfg_rejected(), "same-sized wrong room location set refused");
    ids = locations(base); ids.back() = ids.front(); parse(base);
    expect(!ap_content_plan_bind("seed-A", 0, 1, ids.data(), ids.size()), "duplicate room location refused");
    parse(base); expect(!ap_content_plan_bind("seed-A", 0, 1, nullptr, ids.size()), "null room location array refused");
    parse(base); expect(!ap_content_plan_bind(nullptr, 0, 1, ids.data(), ids.size()), "missing seed identity refused");
    expect(parse(base) && bind_room(base), "recovery after late refusal");
    ap_seedcfg_reject_late("test withdrawal");
    expect(!ap_content_plan_active() && !ap_content_plan_pad(3), "late refusal revokes runtime tables");
    mutations(base, base, J::json_pointer(""));
    J bad = base; bad["content_plan"]["required_features"] = J::array({"future_feature"}); refuse(bad, "unknown required feature");
    bad = base; bad["schema_version"] = 16; refuse(bad, "old schema cannot carry new plan");
    bad = base; bad["content_plan"]["version"] = 2; refuse(bad, "future plan version");
    bad = base; bad["content_plan"]["checks"][0]["routes"][0]["own_pad_id"] = "pad:99"; refuse(bad, "foreign pad route");
    bad = base; bad["content_plan"]["items"]["rows"][0]["base"] = UINT64_MAX; refuse(bad, "unsigned overflow");
    bad = base; bad["content_plan"]["tracks"][0]["id"] = std::string("retail:\0x", 9); refuse(bad, "NUL in identity");
    for (const J legacy : {J::object(), J{{"ctr_options", {{"schema_version", 16}}}}}) {
        parse(base); bind_room(base); ap_seedcfg_parse_json(legacy);
        expect(!ap_seedcfg_rejected() && !ap_content_plan_present() && !ap_content_plan_active(), "legacy parse withdraws new state");
    }
    // Manual pad requirements (#463): the paired apworld's own emitted plan.
    J manual;
    std::ifstream("tools/fixtures/content-plan/manual-rules.json") >> manual;
    expect(parse(manual) && bind_room(manual, "seed-M") && ap_content_plan_active(), "manual-rule plan binds");
    {
        const auto *cove = ap_content_plan_pad(3), *skull = ap_content_plan_pad(21), *cup = ap_content_plan_pad(100);
        const auto *turbo = ap_content_plan_pad(17), *dingo = ap_content_plan_pad(0), *plain = ap_content_plan_pad(6);
        expect(cove && cove->occupied && cove->has_rule && cove->rule_count == 3 && cove->rule_mask == 1u && cove->keys == 0,
               "concrete Trophy rule resolved on a free-hub pad");
        expect(turbo && turbo->has_rule && turbo->rule_count == 2 && turbo->rule_mask == (1u << 2) && turbo->keys == 1,
               "Gold Relic rule keeps its hub Key floor");
        expect(cup && cup->has_rule && cup->rule_count == 2 && cup->rule_mask == 0x3E00u && cup->keys == 2,
               "AnyGem sum rule resolved over the five base Gems");
        expect(dingo && dingo->has_rule && dingo->rule_count == 4 && dingo->rule_mask == 0x6u,
               "AnyRelic sum rule resolved over the base relic tiers only");
        expect(skull && !skull->has_rule && skull->rule_mask == 0, "free pad carries no rule");
        expect(plain && !plain->has_rule, "unlisted pad carries no rule");
        int held[15] = {};
        expect(!ap_content_pad_open(cove, held), "Trophy rule closed with nothing");
        held[0] = 2; expect(!ap_content_pad_open(cove, held), "two of three Trophies is closed");
        held[0] = 3; expect(ap_content_pad_open(cove, held), "three Trophies open the pad");
        expect(!ap_content_pad_open(turbo, held), "Gold Relic pad closed without its hub Key");
        held[14] = 1; held[2] = 1; expect(!ap_content_pad_open(turbo, held), "one of two Gold Relics is closed");
        held[1] = 5; expect(!ap_content_pad_open(turbo, held), "Sapphire Relics do not count for Gold Relic");
        held[2] = 2; expect(ap_content_pad_open(turbo, held), "Key floor plus two Gold Relics opens it");
        int gems[15] = {}; gems[14] = 2; gems[9] = 1;
        expect(!ap_content_pad_open(cup, gems) && ap_content_rule_owned(cup, gems) == 1, "one Gem of two is closed");
        gems[13] = 1; expect(ap_content_pad_open(cup, gems) && ap_content_rule_owned(cup, gems) == 2, "any two Gems open the Cup pad");
        int relics[15] = {}; relics[1] = 2; relics[3] = 9; relics[14] = 1;
        expect(!ap_content_pad_open(dingo, relics), "Platinum Relics are not part of the sum");
        relics[2] = 2; expect(ap_content_pad_open(dingo, relics), "Sapphire plus Gold sum to four");
        expect(!ap_content_pad_open(nullptr, relics), "no pad is never open");
    }
    mutations(manual, manual, J::json_pointer(""));
    auto manual_refuses = [&](const std::string &why, const std::function<void(J &)> &change) {
        J bad = manual; change(bad["content_plan"]); refuse(bad, "manual rule: " + why);
    };
    auto requirement = [](J &plan, const std::string &id) -> J & {
        for (auto &r : plan["requirements"]) if (r["id"] == id) return r;
        throw std::runtime_error("missing requirement " + id);
    };
    manual_refuses("count above the base supply", [&](J &p) { requirement(p, "manual:3")["predicate"]["count"] = 6; });
    manual_refuses("count zero", [&](J &p) { requirement(p, "manual:3")["predicate"]["count"] = 0; });
    manual_refuses("item outside the counted families", [&](J &p) { requirement(p, "manual:3")["predicate"]["item"] = 35010015; });
    manual_refuses("item with no base supply", [&](J &p) { requirement(p, "manual:3")["predicate"]["item"] = 35010003; });
    manual_refuses("sum drops a colour", [&](J &p) { requirement(p, "manual:100")["predicate"]["items"].erase(4); });
    manual_refuses("sum reordered", [&](J &p) { std::reverse(requirement(p, "manual:100")["predicate"]["items"].begin(), requirement(p, "manual:100")["predicate"]["items"].end()); });
    manual_refuses("sum lists an item with no base supply", [&](J &p) { requirement(p, "manual:0")["predicate"]["items"].push_back(35010003); });
    manual_refuses("sum mixes families", [&](J &p) { requirement(p, "manual:0")["predicate"]["items"] = J::array({35010001, 35010009}); });
    manual_refuses("sum count above the family supply", [&](J &p) { requirement(p, "manual:0")["predicate"]["count"] = 5; });
    manual_refuses("unsupported predicate", [&](J &p) { requirement(p, "manual:3")["predicate"] = {{"op", "all"}, {"terms", J::array({J{{"op", "always"}}})}}; });
    manual_refuses("required feature missing", [&](J &p) { p["required_features"] = J::array({"content_plan_v1"}); });
    manual_refuses("required feature unknown", [&](J &p) { p["required_features"] = J::array({"content_plan_v1", "manual_pad_requirements_v2"}); });
    manual_refuses("gate points elsewhere", [&](J &p) { for (auto &pad : p["pads"]) if (pad["id"] == "pad:3") pad["mode_gates"][0]["requirement_id"] = "free"; });
    manual_refuses("gate points at another pad's rule", [&](J &p) { for (auto &pad : p["pads"]) if (pad["id"] == "pad:3") pad["mode_gates"][0]["requirement_id"] = "manual:17"; });
    manual_refuses("record without a pad", [&](J &p) { requirement(p, "manual:3")["id"] = "manual:4"; });
    manual_refuses("record for an unknown pad", [&](J &p) { requirement(p, "manual:3")["id"] = "manual:99"; });
    manual_refuses("leading zero in the pad number", [&](J &p) { requirement(p, "manual:3")["id"] = "manual:03"; });
    manual_refuses("stray requirement record", [&](J &p) { p["requirements"].push_back({{"id", "extra"}, {"predicate", {{"op", "always"}}}}); });
    manual_refuses("record keeps extra fields", [&](J &p) { requirement(p, "manual:3")["note"] = 1; });
    {
        J bad = manual;
        auto &rows = bad["content_plan"]["requirements"];
        rows.erase(std::remove_if(rows.begin(), rows.end(), [](const J &r) { return r["id"].get<std::string>().rfind("manual:", 0) == 0; }), rows.end());
        refuse(bad, "manual gates without their records");
        bad["content_plan"]["required_features"] = J::array({"content_plan_v1"});
        refuse(bad, "gates still point at removed records");
    }
    {
        // The same plan with the gates and records removed is a plain plan again.
        J plain = manual;
        auto &rows = plain["content_plan"]["requirements"];
        rows.erase(std::remove_if(rows.begin(), rows.end(), [](const J &r) { return r["id"].get<std::string>().rfind("manual:", 0) == 0; }), rows.end());
        for (auto &pad : plain["content_plan"]["pads"]) if (!pad["mode_gates"].empty()) pad["mode_gates"][0]["requirement_id"] = "free";
        plain["content_plan"]["required_features"] = J::array({"content_plan_v1"});
        expect(parse(plain) && bind_room(plain, "seed-P"), "a plan with no manual records and only the base feature admits");
        expect(!ap_content_plan_pad(3)->has_rule, "no stale rule survives a re-parse");
    }
    {
        J old = manual; old["schema_version"] = 17; old["ctr_options"]["schema_version"] = 17;
        refuse(old, "a schema 17 content plan is no longer admitted");
    }
    // Open package admission (#133 PR 2): the paired apworld's own plan with two
    // packages outside any compiled registry, one on a named block (test data on
    // the tv-world key, block 78) and one on a generic slot (path B+).
    J open;
    std::ifstream("tools/fixtures/content-plan/open-packages.json") >> open;
    expect(parse(open) && bind_room(open, "seed-O") && ap_content_plan_active(), "open-package plan binds");
    {
        int named = 0, generic = 0;
        for (int id = 0; id <= 110; ++id) {
            const auto *p = ap_content_plan_pad(id);
            if (!p || !p->occupied || p->retail_id >= 0) continue;
            const auto *pkg = ap_content_plan_package(p->package_index);
            expect(pkg && p->laps == pkg->laps, "custom pad races the package's authored laps");
            if (!pkg) continue;
            if (p->named_block == 78) {
                ++named;
                expect(p->laps == 5 && p->trophy_location == 35200000 + 200 * 78 && std::string(pkg->content_id) == "tv-world" &&
                       pkg->capabilities == (AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_ARCADE) && !pkg->has_relic_targets,
                       "named open package: block code, laps 5, claimed capabilities");
            } else {
                ++generic;
                expect(p->named_block == -1 && p->laps == 3 && p->custom_slot == 1 && p->trophy_location == 35016300 &&
                       std::string(p->room_title) == "Fixture Loop" && pkg->has_relic_targets &&
                       pkg->relic_targets[0] == 96000 && pkg->relic_targets[1] == 86400 && pkg->relic_targets[2] == 76800 &&
                       pkg->capabilities == (AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_ARCADE | AP_CONTENT_CAP_RELIC) &&
                       pkg->lev_bytes == 1234567 && pkg->vrm_bytes == 458808,
                       "B+ open package: generic slot code, laps 3, relic targets");
            }
        }
        expect(named == 1 && generic == 1, "one named and one generic open package");
        expect(ap_content_plan_package(1) && !ap_content_plan_package(2) && !ap_content_plan_package(AP_CONTENT_MAX_PACKAGES),
               "package lookup bounded by the plan");
    }
    mutations(open, open, J::json_pointer(""));
    auto package_refuses = [&](const std::string &why, const std::function<void(J &)> &change) {
        J bad = open; change(bad["content_plan"]["packages"]); refuse(bad, "open package: " + why);
    };
    package_refuses("unsorted records", [](J &p) { std::swap(p[0], p[1]); });
    package_refuses("id not derived", [](J &p) { p[0]["id"] = "package/fixture-loop/2.2"; });
    package_refuses("uppercase track key", [](J &p) { p[0]["content_id"] = "Fixture-loop"; });
    package_refuses("underscore in track key", [](J &p) { p[0]["content_id"] = "fixture_loop"; });
    package_refuses("double hyphen in track key", [](J &p) { p[0]["content_id"] = "fixture--loop"; });
    package_refuses("revision starts with a dot", [](J &p) { p[0]["revision"] = ".1"; });
    package_refuses("revision not text", [](J &p) { p[0]["revision"] = 21; });
    package_refuses("uppercase uuid", [](J &p) { p[0]["uuid"] = "0F0E0D0C-0B0A-4908-8706-050403020100"; });
    package_refuses("control character in title", [](J &p) { p[0]["display_name"] = "Fixture\tLoop"; });
    package_refuses("title over 128 bytes", [](J &p) { p[0]["display_name"] = std::string(129, 'x'); });
    package_refuses("empty author", [](J &p) { p[0]["author"] = ""; });
    package_refuses("zero laps", [](J &p) { p[0]["laps"] = 0; });
    package_refuses("eight laps", [](J &p) { p[0]["laps"] = 8; });
    package_refuses("capability not Boolean", [](J &p) { p[0]["capabilities"]["arcade"] = 1; });
    package_refuses("unknown capability", [](J &p) { p[0]["capabilities"]["boxes"] = true; });
    package_refuses("relic without targets", [](J &p) { p[0].erase("relic_targets"); });
    package_refuses("targets without relic", [](J &p) { p[1]["relic_targets"] = p[0]["relic_targets"]; });
    package_refuses("targets not faster per tier", [](J &p) { p[0]["relic_targets"]["gold"] = 96000; });
    package_refuses("target above one hour", [](J &p) { p[0]["relic_targets"]["sapphire"] = 3456001; });
    package_refuses("target zero", [](J &p) { p[0]["relic_targets"]["platinum"] = 0; });
    package_refuses("files reversed", [](J &p) { std::swap(p[0]["files"][0], p[0]["files"][1]); });
    package_refuses("third file", [](J &p) { p[0]["files"].push_back(p[0]["files"][1]); });
    package_refuses("uppercase hash", [](J &p) { p[0]["files"][0]["sha256"] = std::string(64, 'A'); });
    package_refuses("short hash", [](J &p) { p[0]["files"][1]["sha256"] = std::string(63, 'b'); });
    package_refuses("LEV over 16 MiB", [](J &p) { p[0]["files"][0]["bytes"] = 16 * 1024 * 1024 + 1; });
    package_refuses("VRM over 4 MiB", [](J &p) { p[0]["files"][1]["bytes"] = 4 * 1024 * 1024 + 1; });
    package_refuses("wrong format", [](J &p) { p[0]["files"][1]["format"] = "ctr-lev"; });
    package_refuses("format version 2", [](J &p) { p[0]["files"][0]["format_version"] = 2; });
    package_refuses("evidence names an unpinned hash", [](J &p) {
        p[0]["evidence"] = J::array({J{{"capability", "arcade"}, {"file_hashes", J::array({std::string(64, 'e')})},
                                       {"verifier", "v"}, {"revision", 1}, {"level", "structural"}}}); });
    package_refuses("seventeen evidence rows", [](J &p) {
        const J row{{"capability", "arcade"}, {"file_hashes", J::array({p[0]["files"][0]["sha256"]})},
                    {"verifier", "v"}, {"revision", 1}, {"level", "runtime"}};
        p[0]["evidence"] = J::array(); for (int i = 0; i < 17; ++i) p[0]["evidence"].push_back(row); });
    package_refuses("repeated track key", [](J &p) { p[1]["content_id"] = "fixture-loop"; p[1]["id"] = "package/fixture-loop/3"; p[1]["revision"] = "3"; });
    package_refuses("repeated uuid", [](J &p) { p[1]["uuid"] = p[0]["uuid"]; });
    package_refuses("repeated file pair", [](J &p) { p[1]["files"] = p[0]["files"]; });
    {
        J bad = open;
        for (auto &t : bad["content_plan"]["tracks"]) if (t["origin"] == "custom" && t["laps"] == 3) t["laps"] = 7;
        refuse(bad, "open package: track laps differ from the descriptor");
        bad = open; bad["content_plan"]["packages"][1]["capabilities"]["arcade"] = false;
        refuse(bad, "open package: Trophy track without the arcade capability");
    }
    // Track identity (#133 PR 2 review): native holds the apworld's named-block
    // table, so a key with a block must use exactly that block and name, a key
    // without one takes its generic slot, and a generic room title never takes
    // a retail or named track's name or repeats another check's name.
    {
        auto track_of = [](J &plan, const std::string &key) -> J & {
            for (auto &t : plan["tracks"]) if (t.value("package_id", "").rfind("package/" + key + "/", 0) == 0) return t;
            throw std::runtime_error("missing track " + key);
        };
        auto check_of = [](J &plan, const std::string &owner) -> J & {
            for (auto &c : plan["checks"]) if (c["owner"] == owner) return c;
            throw std::runtime_error("missing check " + owner);
        };
        auto identity_refuses = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = open; change(bad["content_plan"]); refuse(bad, "track identity: " + why);
        };
        identity_refuses("named key moved to a generic slot", [&](J &p) {
            J &t = track_of(p, "tv-world"); t.erase("named_block");
            J &c = check_of(p, t["id"]); c["location"] = 35016301; });
        identity_refuses("unnamed key given North Pole's block, name and check", [&](J &p) {
            J &t = track_of(p, "fixture-loop"); t["named_block"] = 53; t["room_title"] = "North Pole";
            J &c = check_of(p, t["id"]); c["location"] = 35200000 + 200 * 53; c["display_name"] = "North Pole: Trophy Race"; });
        identity_refuses("named key on another block", [&](J &p) {
            J &t = track_of(p, "tv-world"); t["named_block"] = 77;
            check_of(p, t["id"])["location"] = 35200000 + 200 * 77; });
        identity_refuses("named key under another room title", [&](J &p) {
            J &t = track_of(p, "tv-world"); t["room_title"] = "TV World 2";
            check_of(p, t["id"])["display_name"] = "TV World 2: Trophy Race"; });
        identity_refuses("named key without its room title", [&](J &p) { track_of(p, "tv-world").erase("room_title"); });
        auto retitle = [&](J &p, const std::string &title) {
            J &t = track_of(p, "fixture-loop"); t["room_title"] = title;
            check_of(p, t["id"])["display_name"] = title + ": Trophy Race"; };
        identity_refuses("generic room title equal to the named track in the seed", [&](J &p) { retitle(p, "TV World"); });
        identity_refuses("generic room title equal to a named track outside the seed", [&](J &p) { retitle(p, "north pole"); });
        identity_refuses("generic room title equal to a retail track", [&](J &p) { retitle(p, "Crash Cove"); });
        J good = open; retitle(good["content_plan"], "Crash Cove (Custom)");
        expect(parse(good), "track identity: a suffixed generic room title admits");
        // Two generic tracks: TV World's record moved to the unregistered key
        // tv-park on generic slot 2. Distinct titles admit; equal titles refuse.
        auto two_generic = [&](const std::string &title) {
            std::string text = open.dump();
            for (size_t at; (at = text.find("tv-world")) != std::string::npos;) text.replace(at, 8, "tv-park");
            J plan = J::parse(text);
            J &t = track_of(plan["content_plan"], "tv-park"); t.erase("named_block"); t["room_title"] = title;
            t["display_name"] = title;
            for (auto &pkg : plan["content_plan"]["packages"]) if (pkg["content_id"] == "tv-park") pkg["display_name"] = title;
            J &c = check_of(plan["content_plan"], t["id"]); c["location"] = 35016301; c["display_name"] = title + ": Trophy Race";
            return plan;
        };
        expect(parse(two_generic("TV Park")), "track identity: two generic tracks with distinct titles admit");
        refuse(two_generic("Fixture Loop"), "track identity: two generic tracks with one room title");
        refuse(two_generic("FIXTURE LOOP"), "track identity: room titles compare without case");
    }
    // Track modes (#133 PR 3): the paired apworld's plan with all three modes,
    // two stages and a mode order drawn per pad, on the 18 retail tracks and
    // three custom packages (tv-world on block 78 with CTR and Relic, the B+
    // fixture-loop with Trophy and Relic, the Relic-only relic-loop).
    J modes;
    std::ifstream("tools/fixtures/content-plan/track-modes.json") >> modes;
    expect(parse(modes) && bind_room(modes, "seed-M") && ap_content_plan_active(), "track-mode plan binds");
    {
        int retail = 0, custom = 0, later = 0, relicFirst = 0;
        int none[15] = {}, keys[15] = {};
        keys[14] = 4;
        for (int id = 0; id <= 110; ++id) {
            const auto *p = ap_content_plan_pad(id);
            if (!p || !p->occupied || p->crystal) continue;
            int64_t codes[3];
            expect(p->mode_count >= 1 && p->stages[0] == 1, "first mode opens with the pad");
            expect(ap_content_mode_open(p, p->modes[0], keys), "first mode open with the hub Keys");
            if (p->mode_count > 1) {
                ++later;
                expect(p->has_stage2 && p->stage2_mask && p->stage2_count >= 1 && p->stage2_count <= 4,
                       "multi-mode pad has its second-stage rule");
                for (int k = 1; k < p->mode_count; ++k)
                    expect(p->stages[k] == 2 && !ap_content_mode_open(p, p->modes[k], keys), "later mode closed without its rule");
                int rich[15] = {};
                for (int i = 0; i < 15; ++i) rich[i] = 99;
                expect(ap_content_mode_open(p, p->modes[1], rich), "later mode opens on the rule");
            }
            if (p->modes[0] == AP_CONTENT_MODE_RELIC) ++relicFirst;
            if (p->retail_id >= 0) {
                ++retail;
                expect(p->mode_count == 3 && p->trophy_location > 0 && p->ctr_location > 0 && p->relic_locations[2] > 0,
                       "retail pad owns all three modes");
                if (p->retail_id == 3)
                    expect(p->trophy_location == 35011000 && p->ctr_location == 35012300 && p->relic_locations[0] == 35012000 &&
                           p->relic_locations[1] == 35012100 && p->relic_locations[2] == 35012200, "Crash Cove keeps its frozen codes");
                continue;
            }
            ++custom;
            const auto *pkg = ap_content_plan_package(p->package_index);
            const std::string key = pkg ? pkg->content_id : "";
            if (key == "tv-world")
                expect(p->mode_count == 3 && p->ctr_location == 35200000 + 200 * 78 + 9 &&
                       p->relic_locations[0] == 35200000 + 200 * 78 + 6 && p->relic_locations[2] == 35200000 + 200 * 78 + 8,
                       "named package modes on its block offsets");
            else if (key == "fixture-loop")
                expect(p->mode_count == 2 && p->ctr_location == 0 && p->custom_slot == 1 && p->trophy_location == 35016300 &&
                       p->relic_locations[0] == 35022000 && p->relic_locations[2] == 35022002 &&
                       ap_content_mode_codes(p, AP_CONTENT_MODE_CTR, codes) == 0, "B+ package without letters: Trophy and Relic");
            else
                expect(key == "relic-loop" && p->mode_count == 1 && p->modes[0] == AP_CONTENT_MODE_RELIC && !p->has_stage2 &&
                       p->trophy_location == 0 && p->relic_locations[0] == 35022003 && p->relic_locations[2] == 35022005 &&
                       ap_content_mode_open(p, AP_CONTENT_MODE_RELIC, none) == (p->keys == 0),
                       "Relic-only package on generic slot 2");
        }
        expect(retail == 18 && custom == 3 && later == 20, "18 retail and 3 custom track pads, 20 with later modes");
        expect(relicFirst > 0, "a per-pad order puts Relic first somewhere");
    }
    mutations(modes, modes, J::json_pointer(""));
    {
        auto modes_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = modes; change(bad["content_plan"]); refuse(bad, "track modes: " + why);
        };
        auto multi = [](J &plan) -> J & {
            for (auto &p : plan["pads"]) if (p["mode_gates"].size() == 3) return p;
            throw std::runtime_error("no three-mode pad");
        };
        auto record = [](J &plan, const J &id) -> J & {
            for (auto &r : plan["requirements"]) if (r["id"] == id) return r;
            throw std::runtime_error("missing record");
        };
        auto drop = [](J &plan, const J &id) {
            auto &rows = plan["requirements"];
            for (size_t i = 0; i < rows.size(); ++i) if (rows[i]["id"] == id) { rows.erase(i); return; }
        };
        modes_refuse("later gate as stage one", [&](J &p) { multi(p)["mode_gates"][1]["stage"] = 1; });
        modes_refuse("later gate on the entrance record", [&](J &p) { multi(p)["mode_gates"][2]["requirement_id"] = "free"; });
        modes_refuse("first gate as stage two", [&](J &p) { multi(p)["mode_gates"][0]["stage"] = 2; });
        modes_refuse("gate order differs from entry order", [&](J &p) { auto &g = multi(p)["mode_gates"]; std::swap(g[0], g[2]); });
        modes_refuse("stage-two record dropped", [&](J &p) { drop(p, multi(p)["mode_gates"][1]["requirement_id"]); });
        modes_refuse("stage-two count above supply", [&](J &p) { record(p, multi(p)["mode_gates"][1]["requirement_id"])["predicate"]["count"] = 99; });
        modes_refuse("stage-two count zero", [&](J &p) { record(p, multi(p)["mode_gates"][1]["requirement_id"])["predicate"]["count"] = 0; });
        modes_refuse("stage-two predicate always", [&](J &p) {
            record(p, multi(p)["mode_gates"][1]["requirement_id"])["predicate"] = J{{"op", "always"}}; });
        modes_refuse("stage-two record for an empty pad", [&](J &p) {
            for (auto &pad : p["pads"]) if (pad["entry_id"].is_null() && pad["kind"] == "normal") {
                const std::string id = pad["id"];
                p["requirements"].push_back(J{{"id", "stage2:" + id.substr(4)}, {"predicate", J{{"op", "count"}, {"item", 35010000}, {"count", 1}}}});
                return;
            } });
        modes_refuse("stage-two record for a one-mode pad", [&](J &p) {
            for (auto &pad : p["pads"]) if (pad["mode_gates"].size() == 1 && pad["mode_gates"][0]["mode"] != "crystal") {
                const std::string id = pad["id"];
                p["requirements"].push_back(J{{"id", "stage2:" + id.substr(4)}, {"predicate", J{{"op", "count"}, {"item", 35010000}, {"count", 1}}}});
                return;
            } });
        modes_refuse("entry repeats a mode", [&](J &p) {
            for (auto &e : p["entries"]) if (e["kind"] == "track" && e["modes"].size() == 3) { e["modes"][2] = e["modes"][0]; return; } });
        modes_refuse("entry mode unknown", [&](J &p) {
            for (auto &e : p["entries"]) if (e["kind"] == "track" && e["modes"].size() == 3) { e["modes"][2] = "merged"; return; } });
        modes_refuse("track modes out of canonical order", [&](J &p) {
            for (auto &t : p["tracks"]) if (t["origin"] == "retail") { t["modes"] = J::array({"ctr", "trophy", "relic"}); return; } });
        modes_refuse("capabilities differ from modes", [&](J &p) {
            for (auto &t : p["tracks"]) if (t["origin"] == "retail") { t["capabilities"] = J::array({"trophy"}); return; } });
        modes_refuse("feature dropped", [&](J &p) { p["required_features"] = J::array({"content_plan_v1"}); });
        modes_refuse("relic award swapped", [&](J &p) {
            for (auto &c : p["checks"]) if (c["kind"] == "relic" && c["routes"][0]["award"] == "gold") { c["routes"][0]["award"] = "platinum"; return; } });
        modes_refuse("CTR award as a plain win", [&](J &p) {
            for (auto &c : p["checks"]) if (c["kind"] == "ctr") { c["routes"][0]["award"] = "win"; return; } });
        modes_refuse("relic package loses its lap graph", [&](J &p) {
            for (auto &pkg : p["packages"]) if (pkg["content_id"] == "relic-loop") pkg["capabilities"]["time_trial"] = false; });
        modes_refuse("merged entry", [&](J &p) {
            for (auto &e : p["entries"]) if (e["kind"] == "track") { e["merged"] = true; return; } });
        J single = modes;
        // Single stage: every later gate on the entrance record, no stage-2 records.
        for (auto &pad : single["content_plan"]["pads"])
            for (size_t k = 1; k < pad["mode_gates"].size(); ++k) {
                pad["mode_gates"][k]["stage"] = 1; pad["mode_gates"][k]["requirement_id"] = pad["mode_gates"][0]["requirement_id"];
            }
        J kept = J::array();
        for (const auto &r : single["content_plan"]["requirements"]) if (r["id"].get<std::string>().rfind("stage2:", 0) != 0) kept.push_back(r);
        single["content_plan"]["requirements"] = kept;
        expect(parse(single) && bind_room(single, "seed-S"), "the same plan with one stage admits");
        const auto *p = ap_content_plan_pad(3);
        int none[15] = {};
        expect(p && p->mode_count == 3 && !p->has_stage2 && ap_content_mode_open(p, p->modes[2], none),
               "one stage: every mode opens with the pad");
    }
    // Per-track check families (#133 PR 3b): the paired apworld's plans with
    // podium rungs, per-track Wumpa and Lettersanity mode 2 (two letters), and
    // with five rungs, global Wumpa and Lettersanity mode 3, on Crash Cove,
    // Roo's Tubes and Slide Coliseum, Baby T Park on block 8 (generic slot 1)
    // and the B+ fixture-loop with letters and Wumpa on generic slot 2.
    J fams, famg;
    std::ifstream("tools/fixtures/content-plan/track-checks.json") >> fams;
    std::ifstream("tools/fixtures/content-plan/track-checks-global.json") >> famg;
    expect(parse(fams) && bind_room(fams, "seed-F") && ap_content_plan_active(), "track-check plan binds");
    {
        const auto *fam = ap_content_plan_checks();
        expect(fam->podium == 27 && fam->wumpa == 2 && fam->lettersanity == 2 && fam->letters_per_track == 2,
               "track_checks settings parsed");
        int pads = 0, ctr = 0;
        for (int id = 0; id <= 110; ++id) {
            const auto *p = ap_content_plan_pad(id);
            if (!p || !p->occupied || p->crystal) continue;
            const bool trophy = ap_content_mode_slot(p, AP_CONTENT_MODE_TROPHY) >= 0;
            const bool token = ap_content_mode_slot(p, AP_CONTENT_MODE_CTR) >= 0;
            ++pads;
            expect((p->rung_locations[0] > 0) == trophy && (p->rung_locations[4] > 0) == trophy && p->rung_locations[2] == 0,
                   "four rungs exactly on a Trophy Race, no held 5th");
            int letters = 0;
            for (int l = 0; l < 3; ++l) {
                letters += p->letter_locations[l] > 0;
                expect((p->letter_locations[l] > 0) == (p->letter_items[l] > 0), "mode 2: a letter check has its item");
            }
            expect(letters == (token ? 2 : 0), "two letter checks on a CTR Challenge");
            ctr += token;
            expect((p->wumpa_location > 0) == (trophy || token) && p->wumpa_modes == ((trophy ? 1u : 0u) | (token ? 2u : 0u)),
                   "per-track Wumpa from Trophy and CTR");
            if (p->retail_id == 3)
                expect(p->rung_locations[0] == 35015100 && p->rung_locations[4] == 35015002 && p->wumpa_location == 35016101 &&
                       (p->letter_locations[1] == 0 || (p->letter_locations[1] == 35012501 && p->letter_items[1] == 35010140)),
                       "Crash Cove keeps its frozen rung and Wumpa codes");
            if (p->retail_id >= 0) continue;
            const auto *pkg = ap_content_plan_package(p->package_index);
            const std::string key = pkg ? pkg->content_id : "";
            if (key == "baby-t-park")
                expect(p->named_block == 8 && p->rung_locations[0] == 35201601 && p->rung_locations[4] == 35201605 &&
                       p->wumpa_location == 35201614 &&
                       (p->letter_locations[0] == 0 || (p->letter_locations[0] == 35201611 && p->letter_items[0] == 35100040)) &&
                       (p->letter_locations[2] == 0 || (p->letter_locations[2] == 35201613 && p->letter_items[2] == 35100042)),
                       "named block families on its offsets");
            else
                expect(key == "fixture-loop" && p->custom_slot == 2 && p->rung_locations[0] == 35016405 &&
                       p->wumpa_location == 35016601 &&
                       (p->letter_locations[1] == 0 || (p->letter_locations[1] == 35020004 && p->letter_items[1] == 35021004)),
                       "generic slot families on the Custom Track 2 codes");
        }
        expect(pads == 5 && ctr == 5, "5 track pads, all with a CTR Challenge");
    }
    expect(parse(famg) && bind_room(famg, "seed-G") && ap_content_plan_active(), "global track-check plan binds");
    {
        const auto *fam = ap_content_plan_checks();
        expect(fam->podium == 31 && fam->wumpa == 1 && fam->wumpa_global == 35016100 && fam->lettersanity == 3 &&
               fam->letters_per_track == 0, "global settings parsed");
        for (int id = 0; id <= 110; ++id) {
            const auto *p = ap_content_plan_pad(id);
            if (!p || !p->occupied || p->crystal) continue;
            const bool token = ap_content_mode_slot(p, AP_CONTENT_MODE_CTR) >= 0;
            expect(p->wumpa_location == 0 && p->wumpa_modes != 0, "global: routes but no per-track check");
            for (int l = 0; l < 3; ++l)
                expect(p->letter_locations[l] == 0 && (p->letter_items[l] > 0) == token, "mode 3: three items, no letter checks");
        }
    }
    mutations(fams, fams, J::json_pointer(""));
    {
        auto fam_refuse = [&](const std::string &why, const J &source, const std::function<void(J &)> &change) {
            J bad = source; change(bad["content_plan"]); refuse(bad, "track checks: " + why);
        };
        auto first = [](J &plan, const std::string &kind) -> J & {
            for (auto &c : plan["checks"]) if (c["kind"] == kind) return c;
            throw std::runtime_error("no check of kind " + kind);
        };
        auto erase_first = [](J &plan, const std::string &kind) {
            auto &rows = plan["checks"];
            for (size_t i = 0; i < rows.size(); ++i) if (rows[i]["kind"] == kind) { rows.erase(i); return; }
        };
        auto letter_row = [](J &plan) -> J & {
            for (auto &r : plan["items"]["rows"]) if (r["item"].get<int64_t>() > 35010014) return r;
            throw std::runtime_error("no letter row");
        };
        fam_refuse("letter check dropped", fams, [&](J &p) { erase_first(p, "letter"); });
        fam_refuse("rung dropped", fams, [&](J &p) { erase_first(p, "podium"); });
        fam_refuse("Wumpa dropped", fams, [&](J &p) { erase_first(p, "wumpa"); });
        fam_refuse("global Wumpa dropped", famg, [&](J &p) { erase_first(p, "wumpa"); });
        fam_refuse("letter item row dropped", fams, [&](J &p) {
            auto &rows = p["items"]["rows"];
            for (size_t i = 0; i < rows.size(); ++i) if (rows[i]["item"].get<int64_t>() > 35010014) { rows.erase(i); return; } });
        fam_refuse("letter item pooled twice", fams, [&](J &p) {
            J &r = letter_row(p); r["base"] = 2; r["remaining"] = 2; r["receipt_cap"] = 2; r["requirement_ceiling"] = 2; });
        fam_refuse("letter item with an extra copy", fams, [&](J &p) {
            J &r = letter_row(p); r["extra"] = 1; r["remaining"] = 2; r["receipt_cap"] = 2; });
        fam_refuse("unknown item row", fams, [&](J &p) { letter_row(p)["item"] = 35010187; });
        fam_refuse("rung set the options never create", fams, [&](J &p) { p["track_checks"]["podium_rungs"] = J::array({"held_3rd"}); });
        fam_refuse("rungs out of slot order", fams, [&](J &p) {
            p["track_checks"]["podium_rungs"] = J::array({"held_3rd", "held_1st", "finish_podium", "finish_any"}); });
        fam_refuse("unknown rung", fams, [&](J &p) { p["track_checks"]["podium_rungs"][0] = "held_2nd"; });
        fam_refuse("wumpa mode unknown", fams, [&](J &p) { p["track_checks"]["wumpa"] = "per_pad"; });
        fam_refuse("per-track checks under global", fams, [&](J &p) { p["track_checks"]["wumpa"] = "global"; });
        fam_refuse("letters_per_track in mode 3", famg, [&](J &p) { p["track_checks"]["letters_per_track"] = 3; });
        fam_refuse("letters_per_track missing in mode 2", fams, [&](J &p) { p["track_checks"].erase("letters_per_track"); });
        fam_refuse("letter count above letters_per_track", fams, [&](J &p) { p["track_checks"]["letters_per_track"] = 3; });
        fam_refuse("mode 2 plan read as mode 1", fams, [&](J &p) { p["track_checks"]["lettersanity"] = 1; });
        fam_refuse("mode 3 plan read as mode 2", famg, [&](J &p) { p["track_checks"]["lettersanity"] = 2; p["track_checks"]["letters_per_track"] = 3; });
        fam_refuse("empty record", fams, [&](J &p) { p["track_checks"] = J::object(); });
        fam_refuse("unknown setting", fams, [&](J &p) { p["track_checks"]["box_locations"] = true; });
        fam_refuse("record removed", fams, [&](J &p) { p.erase("track_checks"); });
        fam_refuse("feature dropped", fams, [&](J &p) {
            auto &f = p["required_features"];
            for (size_t i = 0; i < f.size(); ++i) if (f[i] == "wumpa_checks_v1") { f.erase(i); return; } });
        fam_refuse("Wumpa route from a Relic Race", fams, [&](J &p) { first(p, "wumpa")["routes"][0]["mode"] = "relic"; });
        fam_refuse("Wumpa routes reordered", fams, [&](J &p) {
            for (auto &c : p["checks"]) if (c["kind"] == "wumpa" && c["routes"].size() == 2) { std::swap(c["routes"][0], c["routes"][1]); return; } });
        fam_refuse("global routes reversed", famg, [&](J &p) {
            auto &r = first(p, "wumpa")["routes"]; std::reverse(r.begin(), r.end()); });
        fam_refuse("rung on a CTR route", fams, [&](J &p) { first(p, "podium")["routes"][0]["mode"] = "ctr"; });
        fam_refuse("letter award swapped", fams, [&](J &p) {
            J &c = first(p, "letter"); c["routes"][0]["award"] = c["routes"][0]["award"] == "letter_c" ? "letter_t" : "letter_c"; });
        fam_refuse("rung display name changed", fams, [&](J &p) { first(p, "podium")["display_name"] = "Crash Cove: Held 2nd"; });
        fam_refuse("package without Wumpa keeps its check", fams, [&](J &p) {
            for (auto &pkg : p["packages"]) if (pkg["content_id"] == "fixture-loop") pkg["capabilities"]["wumpa"] = false; });
        fam_refuse("letter moved to an undrawn letter", fams, [&](J &p) {
            std::set<int64_t> have;
            for (const auto &c : p["checks"])
                if (c["owner"] == "retail:3" && c["kind"] == "letter") have.insert(c["location"].get<int64_t>());
            for (auto &c : p["checks"])
                if (c["owner"] == "retail:3" && c["kind"] == "letter")
                    for (int l = 0; l < 3; ++l)
                        if (!have.count(35012500 + l)) {
                            c["location"] = 35012500 + l;
                            c["routes"][0]["award"] = std::string("letter_") + "ctr"[l];
                            c["display_name"] = std::string("Crash Cove: Letter ") + "CTR"[l];
                            return;
                        } });
    }
    // Cortex Vortex as a track and Relic Race Perfect checks (#133 PR 3c): the
    // paired apworld's plan with Crash Cove, Oxide Station, Slide Coliseum,
    // Cortex Vortex, Baby T Park on block 8 (no Relic Race), North Pole on
    // block 53 and the B+ fixture-loop (both with a Relic Race), Lettersanity
    // mode 3, per-track Wumpa and four rungs.
    J cvp;
    std::ifstream("tools/fixtures/content-plan/cortex-perfect.json") >> cvp;
    expect(parse(cvp) && bind_room(cvp, "seed-C") && ap_content_plan_active() && ap_content_plan_cortex(),
           "Cortex Vortex and perfect plan binds");
    {
        const auto *fam = ap_content_plan_checks();
        expect(fam->relic_perfect == 1 && fam->lettersanity == 3 && fam->wumpa == 2, "relic_perfect setting parsed");
        int pads = 0, perfects = 0, cortex = 0;
        for (int id = 0; id <= 110; ++id) {
            const auto *p = ap_content_plan_pad(id);
            if (!p || !p->occupied || p->crystal) continue;
            ++pads;
            const bool relic = ap_content_mode_slot(p, AP_CONTENT_MODE_RELIC) >= 0;
            expect((p->perfect_location > 0) == relic, "a perfect exactly on a Relic Race");
            perfects += p->perfect_location > 0;
            if (p->retail_id == 3) expect(p->perfect_location == 35012400 && ap_content_pad_host(p) == 3, "Crash Cove's frozen perfect");
            if (p->retail_id == 13) expect(p->perfect_location == 35012415 && !p->cortex, "Oxide Station keeps its own perfect");
            if (p->retail_id == 16) expect(p->perfect_location == 35012416, "Slide Coliseum's frozen perfect");
            if (p->cortex) {
                ++cortex;
                expect(p->retail_id == -1 && p->package_index == -1 && p->custom_slot == 0 && p->named_block == -1 &&
                       p->laps == 3 && ap_content_pad_host(p) == AP_CONTENT_CORTEX_HOST && p->mode_count == 3 &&
                       std::string(p->track_id) == "cortex_vortex" && std::string(p->room_title) == "Cortex Vortex",
                       "Cortex Vortex pad identity");
                expect(p->trophy_location == 35026000 && p->ctr_location == 35026004 && p->relic_locations[0] == 35026001 &&
                       p->relic_locations[2] == 35026003 && p->perfect_location == 35026005 && p->wumpa_location == 35016121 &&
                       p->rung_locations[0] == 35026010 && p->rung_locations[4] == 35026014 && p->rung_locations[2] == 0,
                       "Cortex Vortex keeps its frozen codes, never Oxide Station's");
                for (int l = 0; l < 3; ++l)
                    expect(p->letter_items[l] == 35010200 + l && p->letter_locations[l] == 0, "Cortex Vortex letter items");
                int64_t fam_codes[AP_CONTENT_FAMILY_SLOTS]; int slots[AP_CONTENT_FAMILY_SLOTS];
                const int n = ap_content_mode_family_codes(p, AP_CONTENT_MODE_RELIC, fam_codes, slots);
                expect(n == 1 && fam_codes[0] == 35026005 && slots[0] == AP_CONTENT_FAMILY_PERFECT, "the Relic Race reaches its perfect");
            }
            if (p->retail_id >= 0 || p->cortex) continue;
            const auto *pkg = ap_content_plan_package(p->package_index);
            const std::string key = pkg ? pkg->content_id : "";
            if (key == "north-pole") expect(p->named_block == 53 && p->perfect_location == 35210610, "named block perfect at offset 10");
            if (key == "fixture-loop") expect(p->perfect_location == 35024000 + p->custom_slot - 1, "generic slot perfect");
            if (key == "baby-t-park") expect(p->perfect_location == 0, "no Relic Race, no perfect");
        }
        expect(pads == 7 && perfects == 6 && cortex == 1, "7 track pads, 6 perfects, one Cortex Vortex");
    }
    mutations(cvp, cvp, J::json_pointer(""));
    {
        auto cv_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = cvp; change(bad["content_plan"]); refuse(bad, "cortex/perfect: " + why);
        };
        auto check_of = [](J &plan, const std::string &owner, const std::string &kind) -> J & {
            for (auto &c : plan["checks"]) if (c["owner"] == owner && c["kind"] == kind) return c;
            throw std::runtime_error("no check " + owner + " " + kind);
        };
        auto cv_track = [](J &plan) -> J & {
            for (auto &t : plan["tracks"]) if (t["origin"] == "cortex_vortex") return t;
            throw std::runtime_error("no Cortex Vortex track");
        };
        auto drop = [](J &plan, const std::string &owner, const std::string &kind) {
            auto &rows = plan["checks"];
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i]["owner"] == owner && rows[i]["kind"] == kind) {
                    rows.erase(i);
                    plan["items"]["coded_checks"] = plan["items"]["coded_checks"].get<int>() - 1;
                    plan["items"]["filler_count"] = plan["items"]["filler_count"].get<int>() - 1;
                    return;
                }
        };
        cv_refuse("perfect dropped", [&](J &p) { drop(p, "cortex_vortex", "perfect"); });
        cv_refuse("generic perfect dropped", [&](J &p) { drop(p, "custom/fixture-loop/2.1", "perfect"); });
        cv_refuse("Cortex Vortex takes Oxide Station's perfect", [&](J &p) { check_of(p, "cortex_vortex", "perfect")["location"] = 35012415; });
        cv_refuse("Cortex Vortex Trophy takes Oxide Station's", [&](J &p) { check_of(p, "cortex_vortex", "trophy")["location"] = 35011013; });
        cv_refuse("named perfect off its block", [&](J &p) { check_of(p, "custom/north-pole/1.1.0", "perfect")["location"] = 35024005; });
        cv_refuse("perfect award changed", [&](J &p) { check_of(p, "retail:3", "perfect")["routes"][0]["award"] = "sapphire"; });
        cv_refuse("perfect on the Trophy route", [&](J &p) { check_of(p, "retail:3", "perfect")["routes"][0]["mode"] = "trophy"; });
        cv_refuse("perfect setting false", [&](J &p) { p["track_checks"]["relic_perfect"] = false; });
        cv_refuse("perfect setting as integer", [&](J &p) { p["track_checks"]["relic_perfect"] = 1; });
        cv_refuse("perfect setting dropped", [&](J &p) { p["track_checks"].erase("relic_perfect"); });
        cv_refuse("perfect feature dropped", [&](J &p) {
            auto &f = p["required_features"];
            for (size_t i = 0; i < f.size(); ++i) if (f[i] == "relic_perfect_v1") { f.erase(i); return; } });
        cv_refuse("Cortex Vortex laps", [&](J &p) { cv_track(p)["laps"] = 5; });
        cv_refuse("Cortex Vortex package", [&](J &p) { cv_track(p)["package_id"] = "package/cortex-vortex/1.1.2"; });
        cv_refuse("Cortex Vortex name", [&](J &p) { cv_track(p)["display_name"] = "Oxide Station"; });
        cv_refuse("Cortex Vortex with a custom slot", [&](J &p) { cv_track(p)["custom_slot"] = 9; });
        cv_refuse("Cortex Vortex as a custom origin", [&](J &p) { cv_track(p)["origin"] = "custom"; });
        cv_refuse("Cortex Vortex letter item swapped", [&](J &p) {
            for (auto &r : p["items"]["rows"]) if (r["item"] == 35010201) { r["item"] = 35010186; break; }
            std::sort(p["items"]["rows"].begin(), p["items"]["rows"].end(),
                      [](const J &a, const J &b) { return a["item"].get<int64_t>() < b["item"].get<int64_t>(); }); });
        cv_refuse("B+ title takes Cortex Vortex", [&](J &p) {
            for (auto &t : p["tracks"]) if (t["id"] == "custom/fixture-loop/2.1") {
                const std::string old = t["room_title"];
                t["room_title"] = "Cortex Vortex";
                for (auto &c : p["checks"]) if (c["owner"] == t["id"]) {
                    std::string name = c["display_name"];
                    c["display_name"] = "Cortex Vortex" + name.substr(old.size());
                }
            } });
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
