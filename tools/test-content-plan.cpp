// Production content-plan admission and retained room identity.
// g++ -m32 -std=c++17 -DCTR_AP -fsanitize=undefined -fno-sanitize-recover=all -Iap -Iap/vendor/json/include tools/test-content-plan.cpp ap/ap_seedcfg.cpp -o /tmp/test-content-plan
#include "ap_seedcfg.h"
#include "ap_content_plan.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstring>
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
            changed = base; changed[path].erase(it.key());
            refuse(changed, "missing field at " + path.to_string() + "/" + it.key());
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
            expect(p->laps == 7 && p->custom_slot == 1 && p->trophy_location == 35016300 && pkg &&
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
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
