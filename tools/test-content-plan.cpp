// Production content-plan admission and retained room identity.
// g++ -m32 -std=c++17 -DCTR_AP -fsanitize=undefined -fno-sanitize-recover=all -Iap -Iap/vendor/json/include tools/test-content-plan.cpp ap/ap_seedcfg.cpp -o /tmp/test-content-plan
#include "ap_seedcfg.h"
#include "ap_content_plan.h"
#include "ap_content_modes.h"
#include "ap_trap_items.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>
#include <functional>
#include <algorithm>
#include <map>
#include <cstring>
#include <set>
using J = nlohmann::json;
extern "C" void AP_LogLine(const char *) {}
static int checks, failures;
static int nothing_checked(int64_t) { return 0; }
static int64_t won_code;
static int won_check(int64_t code) { return code == won_code; }
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
// Paths a fixture's mutation walk skips (the global-checks room, #133 PR 4d,
// keeps the walk inside the CI time budget; see its call below).
static std::function<bool(const std::string &)> skip_path;
static void mutations(const J &base, const J &node, const J::json_pointer &path) {
    if (skip_path && skip_path(path.to_string())) return;
    // The trap block is read like a normal room's (#133 PR 4a): a malformed
    // entry or block silences traps, it never refuses the room. Its own cases
    // are below.
    if (path.to_string() == "/trap_identities") return;
    if (node.is_object()) {
        J changed = base;
        changed[path]["unexpected_field"] = 1;
        refuse(changed, "extra field at " + path.to_string());
        for (auto it = node.begin(); it != node.end(); ++it) {
            // Optional custom-track fields (#306), the envelope's
            // reserve_top_up Boolean (#451) and the #133 PR 4a envelope keys
            // and trap block: absent is a legal plan.
            // #133 PR 4b: the starting stat class and Penta keys are optional
            // one by one; the editable-stats trio only together.
            static const std::set<std::string> optional = {"room_title", "named_block", "reserve_top_up", "death_link_send",
                                                           "warp_pad_item_display", "ap_item_type_colors", "trap_identities",
                                                           "starting_stat_class", "penta_stats",
                                                           // #133 PR 4c: a check's boost rank is the
                                                           // apworld's logic as data (absent is rank 0),
                                                           // and super_turbo_pads may be unstated.
                                                           "boost", "super_turbo_pads",
                                                           // #133 PR 4e: a box check's stat term is read
                                                           // as given like `boost`.
                                                           "stats",
                                                           // and an unstated weapon crate count is legal
                                                           // (a named track then has no boxes).
                                                           "weapon_crates"};
            if (!optional.count(it.key())) {
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
            expect(ap_content_mode_open(p, p->modes[0], keys, nothing_checked), "first mode open with the hub Keys");
            if (p->mode_count > 1) {
                ++later;
                expect(p->has_stage2 && p->stage2_mask && p->stage2_count >= 1 && p->stage2_count <= 4,
                       "multi-mode pad has its second-stage rule");
                for (int k = 1; k < p->mode_count; ++k)
                    expect(p->stages[k] == 2 && !ap_content_mode_open(p, p->modes[k], keys, nothing_checked), "later mode closed without its rule");
                int rich[15] = {};
                for (int i = 0; i < 15; ++i) rich[i] = 99;
                expect(ap_content_mode_open(p, p->modes[1], rich, nothing_checked), "later mode opens on the rule");
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
                       ap_content_mode_open(p, AP_CONTENT_MODE_RELIC, none, nothing_checked) == (p->keys == 0),
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
        expect(p && p->mode_count == 3 && !p->has_stage2 && ap_content_mode_open(p, p->modes[2], none, nothing_checked),
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
    // Pool-only packs and the content-plan envelope (#133 PR 4a): an apworld-
    // emitted room with Progressive Starting Wumpa 3, Turbo Grant, Tizi
    // Helper, an explicit send mask 6, by-reward-type glow, AP item type
    // colours off and the trap_identities block.
    J packs;
    std::ifstream("tools/fixtures/content-plan/item-packs.json") >> packs;
    expect(parse(packs) && bind_room(packs) && ap_content_plan_active(), "item packs room binds");
    expect(ctr_cfg.death_link == 3 && ctr_cfg.death_link_send == 6 && ctr_cfg.warp_pad_item_display == 1 &&
           ctr_cfg.ap_item_type_colors == 0, "envelope keys applied");
    expect(ctr_trap_identities.required == 1 && ctr_trap_identities.state == AP_TRAP_IDS_VALID &&
           AP_TrapResolveEffect(&ctr_trap_identities, AP_TRAP_ITEM_MAP[0].index) >= 0, "trap block read like a normal room's");
    expect(!ap_content_plan_item(35010122) && !ap_content_plan_item(35010188) && !ap_content_plan_item(35010189),
           "pack items are not counted families");
    {
        J plain = packs;
        for (const char *key : {"death_link_send", "warp_pad_item_display", "ap_item_type_colors"}) plain["ctr_options"].erase(key);
        plain.erase("trap_identities");
        expect(parse(plain) && ctr_cfg.death_link_send == -1 && ctr_cfg.warp_pad_item_display == 0 &&
               ctr_cfg.ap_item_type_colors == 1 && ctr_trap_identities.required == 0,
               "absent keys keep the legacy coupling, one pile, colours on and the frozen trap table");
        J bad_block = packs;
        bad_block["trap_identities"]["version"] = 2;
        expect(parse(bad_block) && ctr_trap_identities.state == AP_TRAP_IDS_INVALID,
               "a malformed trap block silences traps and keeps the room, as on a normal room");
        bad_block = packs;
        bad_block["trap_identities"]["items"][std::to_string(AP_TRAP_ITEM_ID_BASE + AP_TRAP_ITEM_MAP[0].index)] = "no_such_effect";
        expect(parse(bad_block) && AP_TrapResolveEffect(&ctr_trap_identities, AP_TRAP_ITEM_MAP[0].index) < 0,
               "a refused trap entry silences only that trap");
    }
    mutations(packs, packs, J::json_pointer(""));
    {
        auto env_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = packs; change(bad); refuse(bad, "envelope: " + why);
        };
        env_refuse("send mask 8", [](J &j) { j["ctr_options"]["death_link_send"] = 8; });
        env_refuse("send mask -1", [](J &j) { j["ctr_options"]["death_link_send"] = -1; });
        env_refuse("send mask as Boolean", [](J &j) { j["ctr_options"]["death_link_send"] = true; });
        env_refuse("display 2", [](J &j) { j["ctr_options"]["warp_pad_item_display"] = 2; });
        env_refuse("display as Boolean", [](J &j) { j["ctr_options"]["warp_pad_item_display"] = true; });
        env_refuse("colours as integer", [](J &j) { j["ctr_options"]["ap_item_type_colors"] = 1; });
        J relic = packs;
        relic["ctr_options"]["relic_progression"] = {{"Sapphire Relic", true}, {"Gold Relic", false},
                                                     {"Platinum Relic", false}};
        expect(parse(relic) && bind_room(relic, "seed-relic"), "relic classification hint admits and binds");
        relic["ctr_options"].erase("relic_progression");
        expect(parse(relic), "absent relic classification hint admits");
        env_refuse("relic map missing a tier", [](J &j) { j["ctr_options"]["relic_progression"] = {{"Sapphire Relic", true}}; });
        env_refuse("relic map has an extra tier", [](J &j) { j["ctr_options"]["relic_progression"] =
            {{"Sapphire Relic", true}, {"Gold Relic", false}, {"Platinum Relic", false}, {"Ruby Relic", true}}; });
        env_refuse("relic map value is not Boolean", [](J &j) { j["ctr_options"]["relic_progression"] =
            {{"Sapphire Relic", true}, {"Gold Relic", 0}, {"Platinum Relic", false}}; });
        env_refuse("relic map is not an object", [](J &j) { j["ctr_options"]["relic_progression"] = J::array(); });
        env_refuse("an unknown envelope key", [](J &j) { j["ctr_options"]["turbo_grant"] = true; });
        env_refuse("an unknown top-level block", [](J &j) { j["itemsanity_checks"] = J::object(); });
        auto pack_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = packs; change(bad["content_plan"]); refuse(bad, "item packs: " + why);
        };
        auto row = [](J &plan, int64_t code) -> J & {
            for (auto &r : plan["items"]["rows"]) if (r["item"] == code) return r;
            throw std::runtime_error("no row");
        };
        auto drop_feature = [](J &plan) {
            auto &f = plan["required_features"];
            for (size_t i = 0; i < f.size(); ++i) if (f[i] == "item_packs_v1") { f.erase(i); return; }
        };
        pack_refuse("count disagrees with row", [](J &p) { p["item_packs"]["progressive_starting_wumpa"] = 2; });
        pack_refuse("row disagrees with count", [&](J &p) {
            row(p, 35010122).update(J{{"base", 4}, {"remaining", 4}, {"receipt_cap", 4}, {"requirement_ceiling", 4}});
            p["items"]["filler_count"] = p["items"]["filler_count"].get<int>() - 1; });
        pack_refuse("count above ten", [](J &p) { p["item_packs"]["progressive_starting_wumpa"] = 11; });
        pack_refuse("count zero", [](J &p) { p["item_packs"]["progressive_starting_wumpa"] = 0; });
        pack_refuse("grant false", [](J &p) { p["item_packs"]["turbo_grant"] = false; });
        pack_refuse("grant as integer", [](J &p) { p["item_packs"]["tizi_helper"] = 1; });
        pack_refuse("unknown pack", [](J &p) { p["item_packs"]["itemsanity"] = true; });
        pack_refuse("empty record", [](J &p) { p["item_packs"] = J::object(); });
        pack_refuse("rows without record", [&](J &p) { p.erase("item_packs"); drop_feature(p); });
        pack_refuse("record without its row", [&](J &p) {
            auto &rows = p["items"]["rows"];
            for (size_t i = 0; i < rows.size(); ++i) if (rows[i]["item"] == 35010189) { rows.erase(i); break; }
            p["items"]["filler_count"] = p["items"]["filler_count"].get<int>() + 1; });
        pack_refuse("feature dropped", drop_feature);
        pack_refuse("extra copy", [&](J &p) { row(p, 35010188).update(J{{"extra", 1}, {"remaining", 2}, {"receipt_cap", 2}}); });
        pack_refuse("pack item swapped for an unpooled item", [&](J &p) { row(p, 35010189)["item"] = 35010187; });
    }
    // Character packs (#133 PR 4b): an apworld-emitted room with Character
    // Unlocks, Remove Playable Oxide, four racer locks (Baby T Park on pad 19,
    // the B+ fixture-loop on pad 15, Papu's Pyramid on pad 7, and the Gem Cup
    // seat 104), Starting Stat Class speed, Penta's MAX table and per-racer
    // editable stats.
    J racers;
    std::ifstream("tools/fixtures/content-plan/racers.json") >> racers;
    expect(parse(racers) && bind_room(racers) && ap_content_plan_active(), "racers room binds");
    expect(ctr_cfg.character_unlocks == 1 && ctr_cfg.character_phase_present == 1 && ctr_cfg.starting_character == 4 &&
           ctr_cfg.racer_locked_pads == 27, "character unlocks applied");
    expect(ctr_cfg.starting_stat_class == 3 && ctr_cfg.penta_stats == 1 && ctr_cfg.stat_source == 2 &&
           ctr_cfg.stat_owner == 2 && ctr_cfg.stat_editing_allowed == 1, "stat settings applied");
    {
        int locked = 0;
        for (int id = 0; id <= 110; ++id) {
            const auto *p = ap_content_plan_pad(id);
            if (!p) continue;
            if (p->racer_lock >= 0) ++locked;
            const int mirror = id >= 100 ? (id <= 104 ? ctr_cfg.gem_cup_racer_lock[id - 100] : -1)
                                         : (id < CTR_CFG_PAD_COUNT ? ctr_cfg.racer_lock[id] : -1);
            expect(mirror == p->racer_lock, "racer lock mirrored into ctr_cfg for pad " + std::to_string(id));
        }
        expect(locked == 4 && ap_content_plan_pad(19)->racer_lock == 14 && ap_content_plan_pad(15)->racer_lock == 10 &&
               ap_content_plan_pad(7)->racer_lock == 1 && ap_content_plan_pad(104)->racer_lock == 13 &&
               ctr_cfg.gem_cup_racer_lock[4] == 13 && ctr_cfg.racer_lock[19] == 14, "four racer locks on their pads");
        expect(!ap_content_plan_item(35010123), "racer items are not counted families");
        // An earlier room has no lock anywhere.
        expect(parse(packs) && bind_room(packs), "item packs room binds again");
        bool none = ctr_cfg.character_unlocks == 0 && ctr_cfg.starting_stat_class == 0 && ctr_cfg.stat_source == 0;
        for (int id = 0; id <= 110; ++id) {
            const auto *p = ap_content_plan_pad(id);
            if (p && p->racer_lock >= 0) none = false;
        }
        for (int i = 0; i < CTR_CFG_PAD_COUNT; ++i) if (ctr_cfg.racer_lock[i] != -1) none = false;
        for (int i = 0; i < 5; ++i) if (ctr_cfg.gem_cup_racer_lock[i] != -1) none = false;
        expect(none, "a room without the racers record keeps every racer free and vanilla stats");
    }
    mutations(racers, racers, J::json_pointer(""));
    {
        auto racer_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = racers; change(bad); refuse(bad, "racers: " + why);
        };
        auto pad = [](J &j, const std::string &id) -> J & {
            for (auto &p : j["content_plan"]["pads"]) if (p["id"] == id) return p;
            throw std::runtime_error("no pad");
        };
        racer_refuse("lock on the starting racer", [&](J &j) { pad(j, "pad:19")["racer_lock"] = 4; });
        racer_refuse("lock on removed Oxide", [&](J &j) { pad(j, "pad:19")["racer_lock"] = 15; });
        racer_refuse("lock on a garage", [&](J &j) { pad(j, "garage:1")["racer_lock"] = 2; });
        racer_refuse("lock 16", [&](J &j) { pad(j, "pad:19")["racer_lock"] = 16; });
        racer_refuse("lock dropped", [&](J &j) { pad(j, "pad:19")["racer_lock"] = nullptr; });
        racer_refuse("requested count 3", [](J &j) { j["content_plan"]["racers"]["requested_locks"] = 3; });
        racer_refuse("Oxide flag flipped", [](J &j) { j["content_plan"]["racers"]["remove_playable_oxide"] = false; });
        racer_refuse("start changed", [](J &j) { j["content_plan"]["racers"]["starting_character"] = 2; });
        racer_refuse("envelope unlocks false", [](J &j) { j["ctr_options"]["character_unlocks"] = false; });
        racer_refuse("envelope start differs", [](J &j) { j["ctr_options"]["starting_character"] = 2; });
        racer_refuse("starting class 5", [](J &j) { j["ctr_options"]["starting_stat_class"] = 5; });
        racer_refuse("penta 2", [](J &j) { j["ctr_options"]["penta_stats"] = 2; });
        racer_refuse("progressive stat source", [](J &j) { j["ctr_options"]["stat_source"] = 1; });
        racer_refuse("stat owner 3", [](J &j) { j["ctr_options"]["stat_owner"] = 3; });
        racer_refuse("editing off with editable source", [](J &j) { j["ctr_options"]["stat_editing_allowed"] = false; });
        racer_refuse("trio without its owner", [](J &j) { j["ctr_options"].erase("stat_owner"); });
        J plain = packs;
        plain["ctr_options"]["character_unlocks"] = true;
        refuse(plain, "racers: unlocks on without the racers record");
        J trio_gone = racers;
        for (const char *key : {"stat_source", "stat_owner", "stat_editing_allowed"}) trio_gone["ctr_options"].erase(key);
        expect(parse(trio_gone) && ctr_cfg.stat_source == 0 && ctr_cfg.stat_editing_allowed == 0,
               "the stat trio absent keeps vanilla stats");
    }
    // Capability packs (#133 PR 4c): an apworld-emitted room with shared
    // Progressive Boost and Progressive Stats at medium on Crash Cove (pad 6),
    // Hot Air Skyway (pad 7), Oxide Station (Gem Cup seat 104) and Slide
    // Coliseum (pad 21), Baby T Park (pad 19, super turbo pads stated true but
    // ruled generic terms only, 2026-10-18) and the B+ fixture-loop (pad 15,
    // stated true, no ruling, so USF), with Relic Race Perfect and
    // Lettersanity mode 2.
    J caps;
    std::ifstream("tools/fixtures/content-plan/capability.json") >> caps;
    expect(parse(caps) && bind_room(caps) && ap_content_plan_active(), "capability room binds");
    expect(ctr_cfg.boost_mode == 1 && ctr_cfg.boost_blue_fire == 0 && ctr_cfg.stats_mode == 1 &&
           ctr_cfg.logic_difficulty == 1 && ctr_cfg.shortcut_knowledge == 0, "capability modes applied");
    expect(ctr_cfg.stat_source == 1 && ctr_cfg.stat_owner == 1 && ctr_cfg.stat_editing_allowed == 0 &&
           ctr_cfg.editable_stats == 0, "Progressive Stats owns the stat panel");
    {
        const auto *c = ap_content_plan_capability();
        expect(c->present == 1 && c->boost_mode == 1 && c->stats_mode == 1 && c->logic_difficulty == 1, "capability record read");
        auto ranks = [](int physical, int trophy, int ctr) {
            const auto *p = ap_content_plan_pad(physical);
            return p && p->trophy_boost == trophy && p->ctr_boost == ctr;
        };
        expect(ranks(7, 2, 2) && ranks(104, 2, 2) && ranks(15, 2, 0), "USF finish tracks: Hot Air Skyway, Oxide Station, an unruled custom track with super pads");
        expect(ranks(6, 1, 1) && ranks(21, 1, 1), "retail tracks: the medium win floor and the CTR Challenge's first boost");
        expect(ranks(19, 1, 1), "a custom track ruled generic terms only (Baby T Park)");
        expect(ap_content_plan_check_boost(ap_content_plan_pad(6)->perfect_location) == 2 &&
               ap_content_plan_check_boost(ap_content_plan_pad(6)->relic_locations[0]) == 0 &&
               ap_content_plan_check_boost(ap_content_plan_pad(6)->relic_locations[2]) == 2 &&
               ap_content_plan_check_boost(ap_content_plan_pad(7)->relic_locations[0]) == 2 &&
               ap_content_plan_check_boost(ap_content_plan_pad(6)->wumpa_location) == 0, "per-check ranks");
        expect(ap_content_plan_start_count(35010027) == 0 && !ap_content_plan_item(35010027), "boost row is not a counted family");
        int stated[2] = {-2, -2};
        for (int i = 0; ap_content_plan_package(i); ++i) {
            const auto *pkg = ap_content_plan_package(i);
            if (!std::strcmp(pkg->content_id, "baby-t-park")) stated[0] = pkg->super_turbo_pads;
            if (!std::strcmp(pkg->content_id, "fixture-loop")) stated[1] = pkg->super_turbo_pads;
        }
        expect(stated[0] == 1 && stated[1] == 1, "super_turbo_pads read from the descriptors");
        // An earlier room has no record, no rank and the packs off.
        expect(parse(racers) && bind_room(racers) && ctr_cfg.boost_mode == 0 && ctr_cfg.stats_mode == 0 &&
               ap_content_plan_capability()->present == 0 && ap_content_plan_pad(19)->trophy_boost == 0 &&
               ap_content_plan_package(0)->super_turbo_pads == AP_CONTENT_SUPER_PADS_UNSTATED, "a room without the record keeps the packs off");
    }
    mutations(caps, caps, J::json_pointer(""));
    {
        auto check = [](J &j, const std::string &kind, const char *award, int boost) -> J & {
            for (auto &c : j["content_plan"]["checks"])
                if (c["kind"] == kind && c["routes"][0]["award"] == award && c.value("boost", 0) == boost) return c;
            throw std::runtime_error("no check");
        };
        auto row = [](J &j, int64_t item) -> J & {
            for (auto &r : j["content_plan"]["items"]["rows"]) if (r["item"] == item) return r;
            throw std::runtime_error("no row");
        };
        auto drop_feature = [](J &j) {
            auto &f = j["content_plan"]["required_features"];
            for (size_t i = 0; i < f.size(); ++i) if (f[i] == "capability_packs_v1") { f.erase(i); break; }
        };
        auto cap_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = caps; change(bad); refuse(bad, "capability: " + why);
        };
        cap_refuse("rank above the chain", [&](J &j) { check(j, "trophy", "win", 2)["boost"] = 3; });
        cap_refuse("rank 0", [&](J &j) { check(j, "trophy", "win", 2)["boost"] = 0; });
        cap_refuse("rank as text", [&](J &j) { check(j, "trophy", "win", 2)["boost"] = "2"; });
        cap_refuse("record dropped, ranks kept", [&](J &j) { j["content_plan"].erase("capability"); drop_feature(j); });
        cap_refuse("feature dropped", drop_feature);
        cap_refuse("record field missing", [](J &j) { j["content_plan"]["capability"].erase("logic_difficulty"); });
        cap_refuse("unknown record field", [](J &j) { j["content_plan"]["capability"]["itemsanity"] = 0; });
        cap_refuse("boost mode 3", [](J &j) { j["content_plan"]["capability"]["boost_mode"] = 3; });
        cap_refuse("Blue Fire on, two boost copies", [](J &j) { j["content_plan"]["capability"]["boost_blue_fire"] = true; });
        cap_refuse("per-racer mode, shared rows", [](J &j) { j["content_plan"]["capability"]["boost_mode"] = 2; });
        cap_refuse("both modes off", [](J &j) {
            j["content_plan"]["capability"]["boost_mode"] = 0; j["content_plan"]["capability"]["stats_mode"] = 0; });
        cap_refuse("boost row with an extra copy", [&](J &j) {
            row(j, 35010027).update(J{{"base", 3}, {"remaining", 3}, {"receipt_cap", 3}, {"requirement_ceiling", 3}}); });
        cap_refuse("super turbo pads as integer", [](J &j) {
            for (auto &p : j["content_plan"]["packages"]) p["capabilities"]["super_turbo_pads"] = 1; });
        cap_refuse("envelope boost off", [](J &j) { j["ctr_options"]["progressive_boost"] = 0; });
        cap_refuse("envelope stats per racer", [](J &j) { j["ctr_options"]["progressive_stats"] = 2; });
        cap_refuse("editable trio on a Progressive Stats room", [](J &j) {
            j["ctr_options"]["stat_source"] = 2; j["ctr_options"]["stat_editing_allowed"] = true; });
        cap_refuse("Progressive Stats owner differs", [](J &j) { j["ctr_options"]["stat_owner"] = 2; });
        cap_refuse("Progressive Stats with editing on", [](J &j) { j["ctr_options"]["stat_editing_allowed"] = true; });
        J plain = packs;
        plain["ctr_options"]["progressive_boost"] = 1;
        refuse(plain, "capability: envelope boost on without the record");
        J trio = packs;
        trio["ctr_options"].update(J{{"stat_source", 1}, {"stat_owner", 1}, {"stat_editing_allowed", false}});
        refuse(trio, "capability: Progressive Stats trio without the record");
        // A rank inside the chain is the apworld's logic: native reads it.
        J lowered = caps;
        check(lowered, "trophy", "win", 2)["boost"] = 1;
        expect(parse(lowered), "capability: a lowered rank inside the chain is read as given");
    }
    // Global checks (#133 PR 4d): an apworld-emitted room with Itemsanity and
    // Hit Character on Cortex Vortex (pad 21), Baby T Park (pad 19), Slide
    // Coliseum (Gem Cup seat 104), Crash Cove (pad 6), the B+ fixture-loop
    // (pad 15) and Tiger Temple (pad 7), all three modes. Penta Penguin's
    // unlock wins are not in the plan (Key fallback 2), Fake Crash's and N.
    // Tropy's are; the boss guests and Oxide take the content-plan fallback.
    J glob;
    std::ifstream("tools/fixtures/content-plan/global-checks.json") >> glob;
    expect(parse(glob) && bind_room(glob) && ap_content_plan_active(), "global-checks room binds");
    expect(ctr_cfg.itemsanity == 1, "global checks: itemsanity flag for the tracker panel");
    {
        const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
        expect(h && h->enabled, "global checks: Hit Character block admitted");
        static const int want[8] = {4, 2, 1, 3, 0, 2, 0, 4};
        bool same = h != nullptr;
        for (int g = 0; same && g < 8; ++g) same = h->triggers[g].fallback_keys == want[g];
        expect(same, "global checks: content-plan Key fallback per guest");
        bool codes = h != nullptr;
        for (int i = 0; codes && i < 16; ++i) codes = h->locations[i] == 35025000 + i;
        expect(codes, "global checks: Hit codes");
        expect(parse(caps) && ctr_cfg.itemsanity == 0 && !ap_seedcfg_hit_encounters(),
               "global checks: a room without the record has both families off");
    }
    // The walk covers the record, the ledger rows, the envelope, the first
    // Itemsanity and the first Hit check and the Hit block's top level. The
    // track checks, the pads and the block's inner fields are already walked
    // through the earlier fixtures and tools/test-hit-character-seedcfg.cpp.
    {
        std::set<std::string> walked;
        const auto &cs = glob["content_plan"]["checks"];
        int first_use = -1, first_hit = -1;
        for (size_t i = 0; i < cs.size(); ++i) {
            if (cs[i]["kind"] == "itemsanity" && first_use < 0) first_use = (int)i;
            if (cs[i]["kind"] == "hit" && first_hit < 0) first_hit = (int)i;
        }
        walked.insert("/content_plan/checks/" + std::to_string(first_use));
        walked.insert("/content_plan/checks/" + std::to_string(first_hit));
        skip_path = [walked](const std::string &p) {
            if (p.rfind("/hit_character_encounters/", 0) == 0) return true;
            if (p.rfind("/content_plan/pads/", 0) == 0 || p.rfind("/content_plan/tracks/", 0) == 0 ||
                p.rfind("/content_plan/packages/", 0) == 0 || p.rfind("/content_plan/entries/", 0) == 0) return true;
            if (p.rfind("/content_plan/checks/", 0) == 0) {
                const size_t end = p.find('/', 21);
                return !walked.count(p.substr(0, end));
            }
            return false;
        };
        mutations(glob, glob, J::json_pointer(""));
        skip_path = nullptr;
    }
    {
        auto global = [](J &j, int64_t code) -> J & {
            for (auto &c : j["content_plan"]["checks"]) if (c["location"] == code) return c;
            throw std::runtime_error("no check");
        };
        auto drop_feature = [](J &j) {
            auto &f = j["content_plan"]["required_features"];
            for (size_t i = 0; i < f.size(); ++i) if (f[i] == "global_checks_v1") { f.erase(i); break; }
        };
        auto trigger = [](J &j, int guest) -> J & { return j["hit_character_encounters"]["unlock_triggers"][std::to_string(guest)]; };
        auto g_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = glob; change(bad); refuse(bad, "global checks: " + why);
        };
        g_refuse("feature dropped", drop_feature);
        g_refuse("record dropped", [&](J &j) { j["content_plan"].erase("global_checks"); drop_feature(j); });
        g_refuse("unknown record field", [](J &j) { j["content_plan"]["global_checks"]["box_locations"] = true; });
        g_refuse("itemsanity false", [](J &j) { j["content_plan"]["global_checks"]["itemsanity"] = false; });
        g_refuse("use check route dropped", [&](J &j) { global(j, 35016000)["routes"].erase(0); });
        g_refuse("use check award", [&](J &j) { global(j, 35016001)["routes"][0]["award"] = "use"; });
        g_refuse("use check owner", [&](J &j) { global(j, 35016002)["owner"] = "retail:3"; });
        // #133 PR 5b: a guest routes from custom tracks like a default racer.
        g_refuse("guest not routed from a custom track", [&](J &j) {
            auto &routes = global(j, 35025008)["routes"];
            for (size_t i = 0; i < routes.size(); ++i) if (routes[i]["entry_id"] == "entry/custom/baby-t-park/1.0.2") { routes.erase(i); break; } });
        g_refuse("default racer not routed from a custom track", [&](J &j) {
            auto &routes = global(j, 35025003)["routes"];
            for (size_t i = 0; i < routes.size(); ++i) if (routes[i]["entry_id"] == "entry/custom/fixture-loop/2.1") { routes.erase(i); break; } });
        g_refuse("weapon row dropped", [](J &j) {
            auto &rows = j["content_plan"]["items"]["rows"];
            for (size_t i = 0; i < rows.size(); ++i) if (rows[i]["item"] == 35010101) { rows.erase(i); break; }
            j["content_plan"]["items"]["filler_count"] = j["content_plan"]["items"]["filler_count"].get<int>() + 1; });
        g_refuse("Hit envelope off", [](J &j) { j["ctr_options"]["hit_character"] = false; });
        g_refuse("Hit block dropped", [](J &j) { j.erase("hit_character_encounters"); });
        g_refuse("Hit block without the record entry", [&](J &j) {
            j["content_plan"]["global_checks"].erase("hit_character");
            auto &checks = j["content_plan"]["checks"];
            for (size_t i = checks.size(); i-- > 0;) if (checks[i]["kind"] == "hit") checks.erase(i);
            auto &items = j["content_plan"]["items"];
            items["coded_checks"] = items["coded_checks"].get<int>() - 16;
            items["filler_count"] = items["filler_count"].get<int>() - 16; });
        g_refuse("fallback on a guest whose win is in the plan", [&](J &j) { trigger(j, 14)["fallback_keys"] = 1; });
        g_refuse("fallback missing on a guest without a win", [&](J &j) { trigger(j, 13).erase("fallback_keys"); });
        g_refuse("boss guest fallback missing", [&](J &j) { trigger(j, 8).erase("fallback_keys"); });
        g_refuse("boss guest fallback count", [&](J &j) { trigger(j, 8)["fallback_keys"] = 3; });
        // A normal room still refuses a fallback on a boss guest.
        J legacy = glob;
        legacy.erase("content_plan"); legacy["ctr_options"].erase("content_plan");
        ap_seedcfg_parse_json(legacy);
        expect(ap_seedcfg_rejected(), "global checks: a normal room refuses the content-plan boss fallback");
    }
    // Item Box Locations (#133 PR 4e): an apworld-emitted room at Shortcut
    // Knowledge hard with shared boost and stats, on N. Gin Labs (pad 15),
    // Tiger Temple (pad 18), Papu's Pyramid (pad 11), Oxide Station (Gem Cup
    // seat 100), Cortex Vortex (pad 8, no boxes), Baby T Park (pad 21, 8
    // stated weapon crates), Acid Lake (pad 10, named, 20 crates: 15 boxes)
    // and the B+ fixture-loop (pad 23, 12 crates, generic slot: no boxes).
    // Item Box Colours off in the seed.
    J boxes;
    std::ifstream("tools/fixtures/content-plan/item-boxes.json") >> boxes;
    expect(parse(boxes) && bind_room(boxes) && ap_content_plan_active(), "item-boxes room binds");
    {
        const ctr_content_item_boxes *b = ap_content_plan_item_boxes();
        expect(b->present == 1 && b->shortcut_knowledge == 2 && ctr_cfg.shortcut_knowledge == 2, "boxes: record and tier");
        expect(ctr_cfg.color_boxes_by_item == 0, "boxes: the seed's colour setting (off) is applied");
        const ctr_content_pad *ngin = ap_content_plan_pad(15), *btp = ap_content_plan_pad(21), *acid = ap_content_plan_pad(10);
        const ctr_content_pad *loop = ap_content_plan_pad(23), *cv = ap_content_plan_pad(8), *oxide = ap_content_plan_pad(100);
        // Frozen codes: the box block's track order (item_boxes.BOX_TRACKS),
        // not the LevelID, read back from the room's own check names.
        auto code_of = [&](const std::string &name) -> int64_t {
            for (const auto &c : boxes["content_plan"]["checks"]) if (c["display_name"] == name) return c["location"].get<int64_t>();
            return -1;
        };
        bool retail = ngin && ngin->box_count == 15;
        for (int i = 0; retail && i < 15; ++i)
            retail = ngin->box_locations[i] == code_of("N. Gin Labs: Item Box " + std::to_string(i + 1)) &&
                     ngin->box_locations[i] == ngin->box_locations[0] + i && ngin->box_locations[0] >= 35014000 &&
                     ngin->box_locations[0] < 35014270;
        expect(retail, "boxes: N. Gin Labs keeps its frozen codes, all 15 slots at hard");
        expect(oxide && oxide->box_count == 14 && oxide->box_locations[5] == code_of("Oxide Station: Item Box 6") &&
               oxide->box_locations[5] > 0, "boxes: Oxide Station on a Gem Cup seat, slot 6 created at hard");
        bool named = btp && btp->box_count == 8 && btp->box_crates == 8;
        for (int i = 0; named && i < 15; ++i) named = btp->box_locations[i] == (i < 8 ? 35200000 + 200 * 8 + 20 + i : 0);
        expect(named, "boxes: Baby T Park takes Item Box 1 to 8 on its named block");
        expect(acid && acid->box_count == 15 && acid->box_crates == 20 && acid->box_locations[14] == 35200000 + 20 + 14,
               "boxes: twenty crates seat fifteen boxes");
        expect(loop && loop->box_count == 0 && loop->box_locations[0] == 0, "boxes: a generic slot has none");
        expect(cv && cv->box_count == 0, "boxes: Cortex Vortex has none");
        expect(ap_content_plan_package(0) && ap_content_plan_package(0)->weapon_crates == 20, "boxes: the stated crate count is kept");
        expect(parse(caps) && ap_content_plan_item_boxes()->present == 0 && ctr_cfg.color_boxes_by_item == 0,
               "boxes: a room without the record has none and pink boxes");
    }
    // The walk covers the record, the envelope, the ledger, the packages
    // (their new crate count), and the first box check of N. Gin Labs (a boost
    // rank), of Oxide Station (the stat term) and of Acid Lake (named). The
    // other checks, pads, tracks and entries are walked by earlier fixtures.
    {
        std::set<std::string> walked;
        const auto &cs = boxes["content_plan"]["checks"];
        std::set<std::string> owners_seen;
        for (size_t i = 0; i < cs.size(); ++i) {
            const std::string owner = cs[i]["owner"];
            if (cs[i]["kind"] == "box" && (owner == "retail:11" || owner == "custom/acid-lake/1.0" ||
                                           (owner == "retail:13" && cs[i].contains("stats"))) &&
                owners_seen.insert(owner).second)
                walked.insert("/content_plan/checks/" + std::to_string(i));
        }
        expect(walked.size() == 3, "boxes: the walk has its three box checks");
        skip_path = [walked](const std::string &p) {
            if (p.rfind("/content_plan/pads/", 0) == 0 || p.rfind("/content_plan/tracks/", 0) == 0 ||
                p.rfind("/content_plan/entries/", 0) == 0) return true;
            if (p.rfind("/content_plan/packages/", 0) == 0)
                return p.find("/capabilities") == std::string::npos && p.size() > std::string("/content_plan/packages/0").size();
            if (p.rfind("/content_plan/checks/", 0) == 0) {
                const size_t end = p.find('/', 21);
                return !walked.count(p.substr(0, end));
            }
            return false;
        };
        mutations(boxes, boxes, J::json_pointer(""));
        skip_path = nullptr;
    }
    {
        auto box = [](J &j, const std::string &name) -> J & {
            for (auto &c : j["content_plan"]["checks"]) if (c["display_name"] == name) return c;
            throw std::runtime_error("no check " + name);
        };
        auto package = [](J &j, const std::string &key) -> J & {
            for (auto &p : j["content_plan"]["packages"]) if (p["content_id"] == key) return p;
            throw std::runtime_error("no package " + key);
        };
        auto drop_feature = [](J &j) {
            auto &f = j["content_plan"]["required_features"];
            for (size_t i = 0; i < f.size(); ++i) if (f[i] == "item_boxes_v1") { f.erase(i); break; }
        };
        auto shrink = [](J &j, int by) {
            auto &items = j["content_plan"]["items"];
            items["coded_checks"] = items["coded_checks"].get<int>() - by;
            items["filler_count"] = items["filler_count"].get<int>() - by;
        };
        auto b_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = boxes; change(bad); refuse(bad, "boxes: " + why);
        };
        auto b_accept = [&](const std::string &why, const std::function<void(J &)> &change) {
            J good = boxes; change(good); expect(parse(good), "boxes: " + why);
        };
        b_refuse("feature dropped", drop_feature);
        b_refuse("record dropped", [&](J &j) { j["content_plan"].erase("item_boxes"); drop_feature(j); });
        b_refuse("tier lowered", [](J &j) { j["content_plan"]["item_boxes"]["shortcut_knowledge"] = 1; });
        b_refuse("tier 3", [](J &j) { j["content_plan"]["item_boxes"]["shortcut_knowledge"] = 3; });
        b_refuse("tier disagrees with the capability record", [](J &j) { j["content_plan"]["capability"]["shortcut_knowledge"] = 1; });
        b_refuse("record not an object", [](J &j) { j["content_plan"]["item_boxes"] = J::array(); });
        b_refuse("box route dropped", [&](J &j) { box(j, "N. Gin Labs: Item Box 1")["routes"].erase(0); });
        b_refuse("box award", [&](J &j) { box(j, "N. Gin Labs: Item Box 1")["routes"][0]["award"] = "win"; });
        b_refuse("box code moved to the next slot", [&](J &j) { box(j, "N. Gin Labs: Item Box 1")["location"] = 35014166; });
        b_refuse("box dropped", [&](J &j) {
            auto &c = j["content_plan"]["checks"];
            for (size_t i = 0; i < c.size(); ++i) if (c[i]["display_name"] == "Baby T Park: Item Box 8") { c.erase(i); break; }
            shrink(j, 1); });
        b_refuse("box on the generic slot", [&](J &j) {
            J extra = box(j, "Baby T Park: Item Box 1");
            extra["owner"] = "custom/fixture-loop/2.1"; extra["location"] = 35017000;
            extra["display_name"] = "Fixture Loop: Item Box 1";
            j["content_plan"]["checks"].push_back(extra); shrink(j, -1); });
        b_refuse("weapon crates raised", [&](J &j) { package(j, "baby-t-park")["capabilities"]["weapon_crates"] = 9; });
        b_refuse("weapon crates lowered", [&](J &j) { package(j, "baby-t-park")["capabilities"]["weapon_crates"] = 7; });
        b_refuse("weapon crates unstated", [&](J &j) { package(j, "baby-t-park")["capabilities"].erase("weapon_crates"); });
        b_refuse("weapon crates Boolean", [&](J &j) { package(j, "acid-lake")["capabilities"]["weapon_crates"] = true; });
        b_refuse("weapon crates past the instance ceiling", [&](J &j) { package(j, "acid-lake")["capabilities"]["weapon_crates"] = 65537; });
        b_refuse("stats false", [&](J &j) { box(j, "Oxide Station: Item Box 6")["stats"] = false; });
        b_refuse("stats on a race check", [&](J &j) { box(j, "Oxide Station: Trophy Race")["stats"] = true; });
        b_refuse("stats without Progressive Stats", [](J &j) {
            j["content_plan"]["capability"]["stats_mode"] = 0;
            auto &rows = j["content_plan"]["items"]["rows"];
            for (size_t i = rows.size(); i-- > 0;) {
                const int64_t item = rows[i]["item"];
                if (item >= 35010028 && item <= 35010030) {
                    j["content_plan"]["items"]["filler_count"] = j["content_plan"]["items"]["filler_count"].get<int>() + 4;
                    rows.erase(i);
                }
            }
            j["ctr_options"]["progressive_stats"] = 0;
            for (const char *k : {"stat_source", "stat_owner", "stat_editing_allowed"}) j["ctr_options"].erase(k); });
        b_refuse("envelope box_locations false", [](J &j) { j["ctr_options"]["box_locations"] = false; });
        b_refuse("envelope box_locations dropped", [](J &j) { j["ctr_options"].erase("box_locations"); });
        b_refuse("envelope colours dropped", [](J &j) { j["ctr_options"].erase("color_boxes_by_item"); });
        b_refuse("envelope colours integer", [](J &j) { j["ctr_options"]["color_boxes_by_item"] = 1; });
        {
            J bad = caps;
            bad["ctr_options"]["box_locations"] = true; bad["ctr_options"]["color_boxes_by_item"] = true;
            refuse(bad, "boxes: envelope keys on a plan without the record");
        }
        // Read as given, like the boost rank: the apworld alone refuses these.
        b_accept("a stat term dropped is read as given", [&](J &j) { box(j, "Oxide Station: Item Box 6").erase("stats"); });
        b_accept("a box rank inside the chain is read as given", [&](J &j) { box(j, "N. Gin Labs: Item Box 4")["boost"] = 1; });
        b_accept("colours on", [](J &j) { j["ctr_options"]["color_boxes_by_item"] = true; });
        expect(parse(boxes) && ctr_cfg.color_boxes_by_item == 0, "boxes: the room parses again after the forgeries");
    }
    // A custom track that states no weapon crates is no Itemsanity or Hit
    // route (#133 PR 4e): the global-checks room with Baby T Park at 0 crates
    // keeps its routes and is refused; with its routes removed it is admitted.
    {
        J crateless = glob;
        for (auto &p : crateless["content_plan"]["packages"])
            if (p["content_id"] == "baby-t-park") p["capabilities"]["weapon_crates"] = 0;
        refuse(crateless, "crates: a crateless track still routed for Itemsanity");
        for (auto &c : crateless["content_plan"]["checks"])
            if (c["kind"] == "itemsanity" || c["kind"] == "hit") {
                J kept = J::array();
                for (const auto &r : c["routes"]) if (r["entry_id"] != "entry/custom/baby-t-park/1.0.2") kept.push_back(r);
                c["routes"] = kept;
            }
        expect(parse(crateless), "crates: without its routes the crateless track is admitted");
    }
    // Cups (#133 PR 5): nine cups over retail, custom (named and generic) and
    // Cortex Vortex legs, shortened laps, sweeps and Cup Leg Victory checks.
    // Every cup pad is checked against the room's own records.
    J cups;
    std::ifstream("tools/fixtures/content-plan/cups.json") >> cups;
    expect(parse(cups) && bind_room(cups) && ap_content_plan_active(), "cups room binds");
    {
        const J &plan = cups["content_plan"];
        std::map<std::string, J> tracks, checks_by_owner_leg;
        for (const auto &t : plan["tracks"]) tracks[t["id"]] = t;
        std::map<std::string, int64_t> victory;
        for (const auto &c : plan["checks"]) if (c["kind"] == "cup_leg") victory[c["owner"]] = c["location"].get<int64_t>();
        int seen = 0;
        for (const auto &pad_record : plan["pads"]) {
            if (pad_record["entry_id"].is_null()) continue;
            const J *entry = nullptr;
            for (const auto &e : plan["entries"]) if (e["id"] == pad_record["entry_id"]) entry = &e;
            if (!entry || (*entry)["kind"] != "cup") continue;
            ++seen;
            const std::string id = pad_record["id"];
            const ctr_content_pad *pad = ap_content_plan_pad(std::stoi(id.substr(4)));
            const std::string cup_id = (*entry)["id"];
            const bool gem = cup_id.rfind("entry/gem_cup:", 0) == 0;
            const int index = std::stoi(cup_id.substr(gem ? 14 : 10));
            expect(pad && pad->occupied && pad->cup && !pad->crystal && pad->mode_count == 0 &&
                   pad->cup_index == (gem ? -1 : index) && pad->gem_cup == (gem ? 100 + index : 0) &&
                   pad->trophy_location == (gem ? 35011200 : 35027000) + index &&
                   pad->sweep_location == (gem ? 35028300 : 35027200) + index &&
                   (*entry)["display_name"] == pad->display_name, "cups: pad, name, Win and sweep codes");
            for (int n = 0; pad && n < 4; ++n) {
                const J &leg = (*entry)["legs"][n];
                const J &track = tracks[leg["track_id"]];
                const ctr_content_leg &l = pad->legs[n];
                const int host = track["origin"] == "retail" ? track["retail_id"].get<int>() : track["origin"] == "cortex_vortex" ? 13 : 6;
                expect(ap_content_leg_host(&l) == host && l.laps == leg["laps"].get<int>() && leg["track_id"] == l.track_id &&
                       leg["id"] == l.leg_id && l.cortex == (track["origin"] == "cortex_vortex") &&
                       (l.package_index >= 0) == (track["origin"] == "custom"),
                       "cups: leg host, laps and identity");
                expect(l.victory == (victory.count(leg["track_id"]) ? victory[leg["track_id"]] : 0), "cups: leg Cup Leg Victory code");
                const int full = track["laps"].get<int>();
                expect(l.laps == (full / 2 > 1 ? full / 2 : 1), "cups: one_lap_cups shortens every leg");
            }
            int64_t codes[6]; int slots[6];
            const int n = ap_content_cup_codes(pad, codes, slots);
            expect(n >= 2 && codes[0] == pad->trophy_location && slots[0] == -1 && codes[1] == pad->sweep_location,
                   "cups: Win and sweep lead the cup's codes");
            int phys = 0, slot = 0;
            expect(ap_content_cup_bit_decode(AP_CONTENT_BIT_CUP + pad->physical * AP_CONTENT_CUP_STRIDE, &phys, &slot) &&
                   phys == pad->physical && slot == 0 && ap_content_cup_bit_code(pad, 0) == pad->sweep_location,
                   "cups: the sweep display bit resolves");
        }
        expect(seen == 9, "cups: nine cup pads");
        expect(!victory.empty(), "cups: the fixture has Cup Leg Victory checks");
        // A forged leg refuses the room.
        J forged = cups;
        for (auto &e : forged["content_plan"]["entries"])
            if (e["kind"] == "cup") { e["legs"][0]["laps"] = 6; break; }
        expect(!parse(forged), "cups: a leg with neither its laps nor the shortened laps (6) refuses");
    }
    // One generator row 2: forged Gem Cups, surface rows and relic checks.
    {
        const auto rows = [](J &j) -> J & { return j["content_plan"]["items"]["rows"]; };
        refuse([&] { J j = cups; for (auto &e : j["content_plan"]["entries"]) if (e["id"] == "entry/gem_cup:0") e["display_name"] = "Green Gem Cup"; return j; }(),
               "gem cups: a Gem Cup under another name");
        refuse([&] { J j = cups; j["content_plan"]["required_features"].erase(j["content_plan"]["required_features"].size() - 1); return j; }(),
               "gem cups: the feature is required");
        refuse([&] { J j = cups; for (auto &c : j["content_plan"]["checks"]) if (c["location"] == 35011200) c["display_name"] = "Red Gem Cup: Win"; return j; }(),
               "gem cups: the Gem check keeps its frozen name");
        refuse([&] { J j = cups; for (auto &row : rows(j)) if (row["item"] == 35010001) { row["base"] = 1; row["remaining"] = 1; row["receipt_cap"] = 1; row["requirement_ceiling"] = 1; } return j; }(),
               "relic checks: a tier with one copy keeps its checks on every race");
        refuse([&] { J j = cups; rows(j).push_back(rows(j)[0]); rows(j).back()["item"] = 35010021; return j; }(),
               "surface items: one row alone");
    }
    // Hit Character with cups (#133 PR 5b): seven cups over retail, custom
    // (named and generic) and Cortex Vortex legs. Every Hit check routes from
    // every Trophy Race, CTR Challenge and cup leg, guests included, and the
    // block carries one draw order per pool cup's name index; a Gem Cup draws
    // from the block's own `cups` order.
    J cuphits;
    std::ifstream("tools/fixtures/content-plan/cup-hits.json") >> cuphits;
    expect(parse(cuphits) && bind_room(cuphits) && ap_content_plan_active(), "cup hits: room binds");
    {
        const J &plan = cuphits["content_plan"];
        const J &block = cuphits["hit_character_encounters"];
        const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
        expect(h && h->enabled, "cup hits: Hit Character block admitted");
        std::set<int> indices;
        int gem_cups = 0;
        for (const auto &e : plan["entries"])
            if (e["kind"] == "cup") {
                const std::string cup_id = e["id"];
                if (cup_id.rfind("entry/gem_cup:", 0) == 0) ++gem_cups;
                else indices.insert(std::stoi(cup_id.substr(10)));
            }
        expect(gem_cups == 5 && indices.size() == 2, "cup hits: the five Gem Cups and two pool cups");
        bool orders = h != nullptr;
        for (int c = 0; orders && c < CTR_CFG_HIT_CONTENT_CUP_COUNT; ++c) {
            orders = (h->content_cup_present[c] != 0) == (indices.count(c) != 0);
            for (int k = 0; orders && h->content_cup_present[c] && k < 16; ++k)
                orders = h->content_cups[c].ids[k] == block["content_cups"][std::to_string(c)]["order"][k].get<int>();
        }
        expect(orders, "cup hits: one parsed order per cup name index, in wire order");
        const J *first = nullptr;
        int hits = 0, legs = 0;
        for (const auto &c : plan["checks"]) {
            if (c["kind"] != "hit") continue;
            ++hits;
            if (!first) first = &c;
            expect(c["routes"] == (*first)["routes"], "cup hits: every target has the same routes");
        }
        for (const auto &r : (*first)["routes"]) legs += r["mode"] == "cup";
        expect(hits == 16 && legs == 28, "cup hits: sixteen Hit checks, a route per cup leg");
        auto c_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = cuphits; change(bad); refuse(bad, "cup hits: " + why);
        };
        auto hit = [](J &j, int64_t code) -> J & {
            for (auto &c : j["content_plan"]["checks"]) if (c["location"] == code) return c;
            throw std::runtime_error("no check");
        };
        const std::string one = std::to_string(*indices.begin());
        c_refuse("content_cups dropped", [](J &j) { j["hit_character_encounters"].erase("content_cups"); });
        c_refuse("a cup order dropped", [&](J &j) { j["hit_character_encounters"]["content_cups"].erase(one); });
        c_refuse("an order for a cup the plan lacks", [&](J &j) {
            int other = 0; while (indices.count(other)) ++other;
            j["hit_character_encounters"]["content_cups"][std::to_string(other)] = j["hit_character_encounters"]["content_cups"][one]; });
        c_refuse("an order with a repeated id", [&](J &j) {
            auto &o = j["hit_character_encounters"]["content_cups"][one]["order"]; o[1] = o[0]; });
        c_refuse("an order with fifteen ids", [&](J &j) { j["hit_character_encounters"]["content_cups"][one]["order"].erase(15); });
        c_refuse("content_cups empty", [](J &j) { j["hit_character_encounters"]["content_cups"] = J::object(); });
        c_refuse("content_cups key with a leading zero", [&](J &j) {
            auto &cc = j["hit_character_encounters"]["content_cups"]; cc["0" + one] = cc[one]; cc.erase(one); });
        c_refuse("a guest without its cup leg routes", [&](J &j) {
            auto &routes = hit(j, 35025012)["routes"];
            for (size_t i = routes.size(); i-- > 0;) if (routes[i]["mode"] == "cup") routes.erase(i); });
        c_refuse("a cup leg route without its leg", [&](J &j) {
            for (auto &r : hit(j, 35025001)["routes"]) if (r["mode"] == "cup") { r["leg_id"] = nullptr; break; } });
        c_refuse("cup leg routes out of leg order", [&](J &j) {
            auto &routes = hit(j, 35025003)["routes"];
            for (size_t i = 0; i + 1 < routes.size(); ++i)
                if (routes[i]["mode"] == "cup" && routes[i + 1]["mode"] == "cup") { std::swap(routes[i], routes[i + 1]); break; } });
        // A normal room never carries content_cups.
        J legacy = cuphits;
        legacy.erase("content_plan"); legacy["ctr_options"].erase("content_plan");
        for (auto &t : legacy["hit_character_encounters"]["unlock_triggers"]) t.erase("fallback_keys");
        ap_seedcfg_parse_json(legacy);
        expect(ap_seedcfg_rejected() && std::string(ctr_cfg.seed_reject_reason).find("expected 7") != std::string::npos,
               "cup hits: a normal room refuses content_cups");
        expect(parse(cuphits), "cup hits: the room parses again after the forgeries");
        // Without content_cups and Hit Character the cups room keeps its PR 5 shape.
        expect(parse(cups) && !ap_seedcfg_hit_encounters(), "cup hits: the cups room has no encounter block");
    }
    // Boss garages (#133 PR 6): bosses.json (Boss Track Shuffle, any_percent
    // with the Boss and Gem companions, the Final Challenge on Cortex Vortex,
    // Progressive Boost and Hit Character on) and bosses-final.json (retail
    // pairs, 101_percent with Oxide 1 optional, the Final Challenge on Oxide
    // Station with total_relics, two cups). The parser drives the normal
    // room's garage, Oxide and goal configuration from the plan.
    J bosses, finalroom, customroom;
    std::ifstream("tools/fixtures/content-plan/bosses.json") >> bosses;
    std::ifstream("tools/fixtures/content-plan/bosses-final.json") >> finalroom;
    std::ifstream("tools/fixtures/content-plan/bosses-custom.json") >> customroom;
    // #133 PR 6b: bosses-cups.json holds boss cups in Ripper Roo's and Papu
    // Papu's garages, one with a Cortex Vortex leg.
    {
        J cuproom;
        std::ifstream("tools/fixtures/content-plan/bosses-cups.json") >> cuproom;
        expect(parse(cuproom) && bind_room(cuproom) && ap_content_plan_active(), "bosses-cups: room binds");
        const ctr_content_bosses *b = ap_content_plan_bosses();
        const ctr_content_pad *roo = ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD + 0);
        const ctr_content_pad *papu = ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD + 1);
        bool legs = roo && papu && roo->cup && roo->trophy_location == 35011100 && papu->trophy_location == 35011101 &&
                    roo->racer_lock == -1 && roo->hub == 26 && papu->hub == 27;
        bool cortex = false;
        for (int n = 0; legs && n < AP_CONTENT_CUP_LEGS; ++n) {
            legs = legs && roo->legs[n].laps == 3 && papu->legs[n].laps == 3 && roo->legs[n].victory == 0 &&
                   b->cup_points[0][n] > 0 && AP_BossVenueScheduleFits(0, b->cup_points[0][n]) &&
                   AP_BossVenueScheduleFits(1, b->cup_points[1][n]);
            cortex = cortex || roo->legs[n].cortex || papu->legs[n].cortex;
        }
        expect(b->present && b->cup[0] && b->cup[1] && !b->cup[2] && !b->cup[3] && legs && cortex &&
               !ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD + 2) && ap_content_plan_cortex(),
               "bosses-cups: two garage cups with four certified legs");
        auto k_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = cuproom; change(bad); refuse(bad, "bosses-cups: " + why);
        };
        auto cup = [](J &j, int g) -> J & {
            for (auto &e : j["content_plan"]["entries"]) if (e["id"] == "entry/boss:" + std::to_string(g) + "/0") return e;
            throw std::runtime_error("no boss entry");
        };
        k_refuse("three legs", [&](J &j) { cup(j, 0)["legs"].erase(3); });
        k_refuse("a repeated leg", [&](J &j) { cup(j, 0)["legs"][1]["track_id"] = cup(j, 0)["legs"][0]["track_id"]; });
        k_refuse("a leg on a trial track", [&](J &j) { cup(j, 1)["legs"][2]["track_id"] = "retail:16"; });
        k_refuse("a leg with other laps", [&](J &j) { cup(j, 0)["legs"][0]["laps"] = 1; });
        k_refuse("a cup garage missing from the record", [](J &j) { j["content_plan"]["bosses"]["cups"] = J::array({0}); });
        k_refuse("a race garage listed as a cup", [](J &j) { j["content_plan"]["bosses"]["cups"] = J::array({0, 1, 2}); });
        k_refuse("the cup check as a boss race", [&](J &j) {
            for (auto &c : j["content_plan"]["checks"]) if (c["location"] == 35011100) c["routes"][0]["mode"] = "boss_race"; });
    }
    // #133 PR 6b: bosses-custom.json races Cortex Vortex in Papu Papu's garage
    // and Baby T Park in Pinstripe's, served on their home LevelIDs by their
    // certified restart point counts (255 and 35).
    expect(parse(customroom) && bind_room(customroom) && ap_content_plan_active(), "bosses-custom: room binds");
    {
        const ctr_content_bosses *b = ap_content_plan_bosses();
        expect(b->present && b->track_shuffle && b->repeats &&
               b->venue_cortex[1] == 1 && b->venue_points[1] == 255 && b->venues[1] == AP_BOSS_VENUE_GARAGE[1].homeLevel &&
               b->venue_package[3] >= 0 && b->venue_points[3] == 35 && b->venues[3] == AP_BOSS_VENUE_GARAGE[3].homeLevel &&
               b->venue_package[0] < 0 && !b->venue_cortex[0] && b->venue_package[2] < 0 && !b->venue_cortex[2],
               "bosses-custom: custom and Cortex Vortex garage venues");
        expect(ctr_boss_encounters.state == AP_BOSS_ENC_VALID && ctr_boss_encounters.custom[1] && ctr_boss_encounters.custom[3] &&
               !ctr_boss_encounters.custom[0] && ctr_boss_encounters.points[3] == 35 &&
               AP_BossEncounterShuffledGarage(&ctr_boss_encounters, 1, 0, 3, AP_BOSS_VENUE_GARAGE[3].homeLevel) == 3 &&
               AP_BossEncounterShuffledGarage(&ctr_boss_encounters, 1, 0, 1, AP_BOSS_VENUE_GARAGE[1].homeLevel) == 1 &&
               AP_BossEncounterCertifiedPoints(&ctr_boss_encounters, 3, AP_BOSS_VENUE_GARAGE[3].homeLevel) == 35,
               "bosses-custom: the encounter record carries the custom venues");
        auto c_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = customroom; change(bad); refuse(bad, "bosses-custom: " + why);
        };
        auto pkg = [](J &j) -> J & {
            for (auto &p : j["content_plan"]["packages"]) if (p["content_id"] == "baby-t-park") return p;
            throw std::runtime_error("no package");
        };
        c_refuse("a count that cannot carry the attack", [&](J &j) { pkg(j)["capabilities"]["checkpoints"] = 3; });
        c_refuse("no AI navigation", [&](J &j) { pkg(j)["capabilities"]["ai_nav"] = false; });
        c_refuse("no stated count", [&](J &j) { pkg(j)["capabilities"].erase("checkpoints"); });
        c_refuse("a custom venue without track_shuffle", [](J &j) { j["content_plan"]["bosses"]["track_shuffle"] = false; });
        c_refuse("a repeated custom venue without repeats", [](J &j) {
            j["content_plan"]["bosses"]["repeats"] = false;
            for (auto &e : j["content_plan"]["entries"]) if (e["id"] == "entry/boss:0/0") e["track_id"] = "cortex_vortex"; });
    }
    expect(parse(bosses) && bind_room(bosses) && ap_content_plan_active(), "bosses: room binds");
    {
        const J &plan = bosses["content_plan"];
        const ctr_content_bosses *b = ap_content_plan_bosses();
        std::map<std::string, J> entries;
        for (const auto &e : plan["entries"]) entries[e["id"]] = e;
        expect(b->present && b->oxide_goal == 1 && b->bosses_required == 2 && b->track_shuffle && !b->repeats &&
               b->final_track == CTR_CFG_OXIDE_FINAL_CORTEX_VORTEX && b->final_unlock == 0 &&
               b->final_count == plan["bosses"]["oxide_final_count"].get<int>(), "bosses: record read");
        bool venues = true;
        std::set<int> distinct;
        for (int g = 0; g < 4; ++g) {
            const std::string track = entries["entry/boss:" + std::to_string(g) + "/0"]["track_id"];
            venues = venues && track == "retail:" + std::to_string(b->venues[g]) &&
                     ctr_boss_encounters.track[g] == b->venues[g] && AP_BossVenueCertified(g, b->venues[g]);
            distinct.insert(b->venues[g]);
        }
        expect(venues && distinct.size() == 4 && ctr_boss_encounters.state == AP_BOSS_ENC_VALID,
               "bosses: the drawn venues become the boss_encounters record");
        expect(ctr_cfg.goal_oxide == 1 && ctr_cfg.goal_bosses == 2 && ctr_cfg.goal_gems == 2 && ctr_cfg.bossgarage_mode == 2 &&
               ctr_cfg.oxide_final_unlock == 0 && ctr_cfg.oxide_final_count == b->final_count && !ctr_cfg.oxide_1_optional,
               "bosses: goal and Oxide configuration");
        // The garage roll (#133 PR 6b): each garage's ctr_req is its drawn
        // predicate (count: the item's type and colour; sum: the Any type).
        bool reqs = ctr_cfg.boss_req[4].type == 2 && ctr_cfg.boss_req[4].count == 4;
        for (const auto &r : bosses["content_plan"]["requirements"]) {
            const std::string id = r["id"];
            if (id.rfind("boss:", 0) != 0) continue;
            const int g = id[5] - '0';
            const J &pred = r["predicate"];
            int type, colour = -1;
            if (pred["op"] == "count") {
                const int item = pred["item"].get<int>() - 35010000;
                type = item == 0 ? 1 : item <= 3 ? 4 : item <= 8 ? 3 : item <= 13 ? 5 : 2;
                colour = item == 0 || item == 14 ? -1 : item <= 3 ? item - 1 : item <= 8 ? item - 4 : item - 9;
            } else {
                const int first = pred["items"][0].get<int>() - 35010000;
                type = first <= 3 ? 7 : first <= 8 ? 6 : 8;
            }
            reqs = reqs && ctr_cfg.boss_req[g].type == type && ctr_cfg.boss_req[g].count == pred["count"].get<int>() &&
                   ctr_cfg.boss_req[g].colour == colour;
        }
        expect(reqs, "bosses: each garage's drawn requirement and the Oxide garage's four Keys");
        expect(ctr_cfg.oxide_final_venue.valid && ctr_cfg.oxide_final_venue.track == CTR_CFG_OXIDE_FINAL_CORTEX_VORTEX &&
               ctr_cfg.oxide_final_venue.location == 35011105 && ctr_cfg.oxide_final_venue.host_level_id == 13 &&
               ctr_cfg.oxide_final_venue.wumpa_location == -1, "bosses: the Final Challenge venue");
        int ranked = 0;
        bool ranks = true;
        for (const auto &c : plan["checks"])
            if (c["kind"] == "boss_race") {
                ranks = ranks && c.contains("boost") && ap_content_plan_check_boost(c["location"].get<int64_t>()) == c["boost"].get<int>();
                ++ranked;
            }
        expect(ranked == 6 && ranks && ap_content_plan_check_boost(35011105) == 2,
               "bosses: boss checks carry their boost ranks (USF on Cortex Vortex)");
        int gems[5] = {1, 1, 0, 0, 0};
        expect(ap_content_plan_gems_held(gems) == 2 && ap_content_plan_goal(gems), "bosses: the Gem arm counts colours");
        // Hit Character: each boss's own race is one more route on its check.
        const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
        expect(h && h->enabled, "bosses: Hit Character block admitted");
        int roo = 0, oxide = 0, other = 0;
        for (const auto &c : plan["checks"]) {
            if (c["kind"] != "hit") continue;
            for (const auto &r : c["routes"]) {
                if (r["mode"] != "boss_race") continue;
                if (c["location"] == 35025010) ++roo; else if (c["location"] == 35025015) ++oxide; else ++other;
            }
        }
        expect(roo == 1 && oxide == 2 && other == 3, "bosses: one boss race route per encounter, on its boss's check");
        auto b_refuse = [&](const std::string &why, const std::function<void(J &)> &change) {
            J bad = bosses; change(bad); refuse(bad, "bosses: " + why);
        };
        auto entry = [](J &j, int g, int index) -> J & {
            for (auto &e : j["content_plan"]["entries"])
                if (e["id"] == "entry/boss:" + std::to_string(g) + "/" + std::to_string(index)) return e;
            throw std::runtime_error("no boss entry");
        };
        auto check = [](J &j, int64_t code) -> J & {
            for (auto &c : j["content_plan"]["checks"]) if (c["location"] == code) return c;
            throw std::runtime_error("no check");
        };
        auto requirement = [](J &j, const std::string &id) -> J & {
            for (auto &r : j["content_plan"]["requirements"]) if (r["id"] == id) return r["predicate"];
            throw std::runtime_error("no requirement");
        };
        b_refuse("a repeated venue without repeats", [&](J &j) { entry(j, 0, 0)["track_id"] = entry(j, 1, 0)["track_id"]; });
        b_refuse("another opponent", [&](J &j) { entry(j, 0, 0)["opponent"]["character_id"] = 3; });
        b_refuse("another kit", [&](J &j) { entry(j, 2, 0)["opponent"]["attack_profile"] = "mixed"; });
        b_refuse("another driving profile", [&](J &j) { entry(j, 3, 0)["opponent"]["driving_profile"] = "boss"; });
        b_refuse("Oxide 1 off Oxide Station", [&](J &j) { entry(j, 4, 0)["track_id"] = "cortex_vortex"; });
        b_refuse("an uncertified venue", [&](J &j) { entry(j, 1, 0)["track_id"] = "retail:18"; });
        // The garage roll (#133 PR 6b): native checks the drawn family and count.
        b_refuse("a garage count above the base supply", [&](J &j) { requirement(j, "boss:3")["count"] = 99; });
        b_refuse("a Trophy garage not at four", [&](J &j) {
            requirement(j, "boss:0") = J{{"op", "count"}, {"item", 35010000}, {"count", 8}}; });
        b_refuse("a Key garage at its hub gate", [&](J &j) {
            requirement(j, "boss:2") = J{{"op", "count"}, {"item", 35010014}, {"count", 2}}; });
        b_refuse("a four-Key garage", [&](J &j) {
            requirement(j, "boss:0") = J{{"op", "count"}, {"item", 35010014}, {"count", 4}}; });
        b_refuse("a free garage", [&](J &j) { requirement(j, "boss:1") = J{{"op", "always"}}; });
        b_refuse("a sum over part of a family", [&](J &j) {
            requirement(j, "boss:1") = J{{"op", "sum"}, {"items", J::array({35010001, 35010002})}, {"count", 1}}; });
        b_refuse("an unknown requirement level", [](J &j) { j["content_plan"]["bosses"]["requirement_level"] = "extreme"; });
        b_refuse("the Oxide 1 companions dropped", [&](J &j) { const J t = requirement(j, "oxide:first")["terms"][0]; requirement(j, "oxide:first") = t; });
        b_refuse("a garage gate opened", [](J &j) {
            for (auto &p : j["content_plan"]["pads"]) if (p["id"] == "garage:2") p["entrance_req"] = "free"; });
        b_refuse("the goal's boss count", [](J &j) { j["content_plan"]["goal"]["terms"][1]["count"] = 1; });
        b_refuse("the goal's Oxide arm dropped", [](J &j) {
            j["content_plan"]["goal"]["terms"].erase(0); });
        b_refuse("the record's shuffle switch", [](J &j) { j["content_plan"]["bosses"]["track_shuffle"] = false; });
        b_refuse("the record's relic count", [](J &j) { j["content_plan"]["bosses"]["oxide_final_count"] = 18; });
        b_refuse("an encounter dropped", [](J &j) { j["content_plan"]["encounters"].erase(5); });
        b_refuse("encounters out of order", [](J &j) {
            std::swap(j["content_plan"]["encounters"][0], j["content_plan"]["encounters"][1]); });
        b_refuse("a boss check award", [&](J &j) { check(j, 35011100)["routes"][0]["award"] = "hit"; });
        b_refuse("a boss check owner", [&](J &j) { check(j, 35011101)["owner"] = "encounter/garage:0/0"; });
        b_refuse("a boss race Hit route dropped", [&](J &j) {
            auto &routes = check(j, 35025010)["routes"]; routes.erase(routes.size() - 1); });
        b_refuse("a boss race Hit route on another check", [&](J &j) {
            auto &routes = check(j, 35025000)["routes"]; routes.push_back(check(j, 35025010)["routes"].back()); });
        b_refuse("the envelope's goal_bosses", [](J &j) { j["ctr_options"]["goal_bosses"] = 1; });
        b_refuse("the envelope's goal_oxide", [](J &j) { j["ctr_options"]["goal_oxide"] = 3; });
        b_refuse("the feature dropped", [](J &j) {
            auto &f = j["content_plan"]["required_features"]; f.erase(f.size() - 1); });
        b_refuse("the record dropped", [](J &j) { j["content_plan"].erase("bosses"); });
        b_refuse("encounters without bosses", [](J &j) { j["content_plan"]["bosses"]["oxide_goal"] = 3; });
        b_refuse("no filler slot for the Oxide goal check", [](J &j) {
            auto &items = j["content_plan"]["items"];
            const int filler = items["filler_count"];
            for (auto &r : items["rows"])
                if (r["item"] == 35010000) { r["extra"] = filler; r["remaining"] = 16 + filler; r["receipt_cap"] = 16 + filler; }
            items["filler_count"] = 0; items["trap_count"] = 0; });
        expect(parse(bosses), "bosses: the room parses again after the forgeries");
    }
    expect(parse(finalroom) && bind_room(finalroom) && ap_content_plan_active(), "bosses-final: room binds");
    {
        const ctr_content_bosses *b = ap_content_plan_bosses();
        expect(b->present && b->oxide_goal == 2 && b->oxide_1_optional && !b->track_shuffle && b->final_unlock == 4 &&
               b->final_track == CTR_CFG_OXIDE_FINAL_OXIDE_STATION, "bosses-final: record read");
        bool home = ctr_boss_encounters.state == AP_BOSS_ENC_RETAIL;
        for (int g = 0; g < 4; ++g) home = home && b->venues[g] == AP_BOSS_VENUE_GARAGE[g].homeLevel;
        expect(home, "bosses-final: retail pairs keep the retail venues");
        expect(ctr_cfg.goal_oxide == 2 && ctr_cfg.goal_bosses == 0 && ctr_cfg.goal_gems == 0 && ctr_cfg.oxide_1_optional == 1 &&
               ctr_cfg.oxide_final_unlock == 4 && ctr_cfg.oxide_final_count == b->final_count &&
               ctr_cfg.oxide_final_venue.valid && ctr_cfg.oxide_final_venue.track == CTR_CFG_OXIDE_FINAL_OXIDE_STATION,
               "bosses-final: goal and Final Challenge configuration");
        int none[5] = {0, 0, 0, 0, 0};
        expect(ap_content_plan_goal(none), "bosses-final: no Gem arm");
        // A mutation walk over the boss records only (the rest of the room is
        // walked by the earlier fixtures).
        skip_path = [](const std::string &path) {
            static const char *keep[] = {"/content_plan/bosses", "/content_plan/encounters", "/content_plan/goal",
                                         "/ctr_options/goal"};
            if (path.empty() || path == "/content_plan" || path == "/ctr_options") return false;
            for (const char *k : keep) if (path.rfind(k, 0) == 0 || std::string(k).rfind(path, 0) == 0) return false;
            return true;
        };
        mutations(finalroom, finalroom, J::json_pointer());
        skip_path = nullptr;
        expect(parse(finalroom), "bosses-final: the room parses again after the walk");
    }
    // Bosses off: the garages stay empty and the goal is the Gem arm alone.
    expect(parse(base) && bind_room(base) && !ap_content_plan_bosses()->present && ctr_cfg.goal_oxide == 3 &&
           ctr_cfg.goal_bosses == 0 && ctr_boss_encounters.state == AP_BOSS_ENC_RETAIL, "bosses off: empty garages");
    // One generator row 3: a later race needs the pad's first race won, on a
    // pad with a second stage and on one without.
    {
        ctr_content_pad pad{};
        int counts[15] = {};
        counts[0] = 9;
        pad.occupied = 1; pad.mode_count = 3; pad.racer_lock = -1; pad.first_race_rule = 1;
        pad.modes[0] = AP_CONTENT_MODE_TROPHY; pad.modes[1] = AP_CONTENT_MODE_CTR; pad.modes[2] = AP_CONTENT_MODE_RELIC;
        pad.stages[0] = 1; pad.stages[1] = pad.stages[2] = 2;
        pad.has_stage2 = 1; pad.stage2_mask = 1u; pad.stage2_count = 2;
        pad.trophy_location = 5001; pad.ctr_location = 5002;
        won_code = 0;
        expect(ap_content_mode_open(&pad, AP_CONTENT_MODE_TROPHY, counts, won_check), "the first race opens with the pad");
        expect(!ap_content_mode_open(&pad, AP_CONTENT_MODE_CTR, counts, won_check), "items alone do not open a later race");
        won_code = 5001;
        expect(ap_content_mode_open(&pad, AP_CONTENT_MODE_RELIC, counts, won_check), "the win and the items open it");
        counts[0] = 1;
        expect(!ap_content_mode_open(&pad, AP_CONTENT_MODE_CTR, counts, won_check), "the win alone does not open stage 2");
        pad.has_stage2 = 0; pad.stages[1] = pad.stages[2] = 1;
        won_code = 0;
        expect(!ap_content_mode_open(&pad, AP_CONTENT_MODE_CTR, counts, won_check), "no second stage still waits for the win");
        won_code = 5001;
        expect(ap_content_mode_open(&pad, AP_CONTENT_MODE_CTR, counts, won_check), "no second stage opens with the win");
        pad.modes[0] = AP_CONTENT_MODE_RELIC; pad.modes[2] = AP_CONTENT_MODE_TROPHY; pad.trophy_location = 0;
        won_code = 0;
        expect(ap_content_mode_open(&pad, AP_CONTENT_MODE_CTR, counts, won_check), "a first race with no check proves nothing");
        pad.first_race_rule = 0; pad.modes[0] = AP_CONTENT_MODE_TROPHY; pad.trophy_location = 5001;
        expect(ap_content_mode_open(&pad, AP_CONTENT_MODE_CTR, counts, won_check), "a plan without the feature keeps its gates");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
