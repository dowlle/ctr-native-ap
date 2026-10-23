// Content-plan completability sweep (#133 PR 8), on the production parser.
// g++ -m32 -std=c++17 -DCTR_AP -fsanitize=undefined -fno-sanitize-recover=all -Iap -Iap/vendor/json/include tools/test-content-verify.cpp ap/ap_seedcfg.cpp -o /tmp/test-content-verify
//
// Without arguments it runs the checked-in content-plan fixtures. With
// `--probe <rows.jsonl>` it replays rows written by
// tools/content-verify-probe.py inside generation: each row is one generated
// room with the apworld's own verdicts (its reachable locations and goal) for
// the full placement and for random inventories, and the sweep must agree
// exactly.
#include "ap_seedcfg.h"
#include "ap_content_plan.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
using J = nlohmann::json;
extern "C" void AP_LogLine(const char *) {}
static int checks, failures;
static void expect(bool good, const std::string &why) {
    ++checks;
    if (!good) { ++failures; std::cout << "FAIL: " << why << '\n'; }
}

struct Room {
    std::map<int64_t, int> foreign;
    std::map<int64_t, int64_t> own; // location -> own item
    std::set<int64_t> present, checked;
};
static int foreign_of(void *ctx, int64_t item) {
    const Room &r = *static_cast<Room *>(ctx);
    const auto it = r.foreign.find(item);
    return it == r.foreign.end() ? 0 : it->second;
}
static int scout_of(void *ctx, int64_t location, int64_t *own) {
    const Room &r = *static_cast<Room *>(ctx);
    *own = 0;
    if (!r.present.count(location)) return 0;
    const auto it = r.own.find(location);
    if (it != r.own.end()) *own = it->second;
    return 1;
}
static int checked_of(void *ctx, int64_t location) { return static_cast<Room *>(ctx)->checked.count(location) != 0; }
static std::set<int64_t> *missed;
static void unreached_of(void *, int64_t location) { if (missed) missed->insert(location); }
static ap_content_verify_result run(Room &room, std::set<int64_t> *miss = nullptr) {
    ap_content_verify_io io{&room, foreign_of, scout_of, checked_of, unreached_of};
    ap_content_verify_result out;
    missed = miss;
    expect(ap_content_plan_verify(&io, &out) == 1, "the sweep gives a verdict on an admitted plan");
    missed = nullptr;
    return out;
}
static std::set<int64_t> reachable_set(Room &room, ap_content_verify_result &r) {
    std::set<int64_t> miss, out;
    r = run(room, &miss);
    for (auto code : room.present) if (!miss.count(code)) out.insert(code);
    return out;
}
static bool admit(const J &slot, const std::vector<int64_t> &locations) {
    ap_seedcfg_parse_json(slot);
    if (ap_seedcfg_rejected() || !ap_content_plan_present()) return false;
    return ap_content_plan_bind("verify-seed", 0, 1, locations.data(), locations.size()) == 1;
}
static std::vector<int64_t> plan_locations(const J &slot) {
    std::vector<int64_t> ids;
    for (const auto &c : slot["content_plan"]["checks"]) ids.push_back(c["location"].get<int64_t>());
    return ids;
}
static std::string diff(const std::set<int64_t> &want, const std::set<int64_t> &got) {
    std::string s;
    int n = 0;
    for (auto c : want) if (!got.count(c) && n++ < 8) s += " -" + std::to_string(c);
    for (auto c : got) if (!want.count(c) && n++ < 16) s += " +" + std::to_string(c);
    return s;
}

// Checked-in fixtures: with every item the plan pools held (and every racer,
// weapon and capability item), every check and the goal are reachable; with
// nothing held the goal is not; removing the Gems blocks a Gem goal.
static void fixtures() {
    static const char *names[] = {"baseline", "starts-extras", "manual-rules", "open-packages", "track-modes", "track-checks",
                                  "track-checks-global", "cortex-perfect", "item-packs", "racers", "capability",
                                  "global-checks", "item-boxes", "cups", "cup-hits", "bosses", "bosses-custom",
                                  "bosses-final", "bosses-cups"};
    for (const char *name : names) {
        std::ifstream in(std::string("tools/fixtures/content-plan/") + name + ".json");
        if (!in) { expect(false, std::string("fixture missing: ") + name); continue; }
        const J slot = J::parse(in);
        const auto ids = plan_locations(slot);
        expect(admit(slot, ids), std::string(name) + ": admitted");
        if (!ap_content_plan_active()) continue;
        Room all;
        for (auto id : ids) all.present.insert(id);
        for (int64_t code = 35010000; code < 35010210; ++code) all.foreign[code] = 99;
        for (const auto &row : slot["content_plan"]["items"]["rows"]) all.foreign[row["item"].get<int64_t>()] = 99;
        auto r = run(all);
        expect(r.goal && r.total == (int)ids.size() && r.reachable == r.total,
               std::string(name) + ": everything held reaches every check and the goal (" + std::to_string(r.reachable) + "/" +
                   std::to_string(r.total) + ")");
        Room none;
        none.present = all.present;
        r = run(none);
        expect(!r.goal && r.reachable < r.total, std::string(name) + ": nothing held does not reach the goal");
        // Checked locations count as reached and bank their own item.
        Room done = none;
        for (auto id : ids) { done.checked.insert(id); }
        r = run(done);
        expect(r.reachable == r.total, std::string(name) + ": checked locations count as reached");
        const J &goal = slot["content_plan"]["goal"];
        const bool gem = goal["op"] == "distinct" ||
                         (goal["op"] == "all" && std::any_of(goal["terms"].begin(), goal["terms"].end(),
                                                             [](const J &t) { return t["op"] == "distinct"; }));
        if (gem) {
            Room no_gems = all;
            for (int64_t code = 35010009; code <= 35010013; ++code) no_gems.foreign.erase(code);
            r = run(no_gems);
            expect(!r.goal, std::string(name) + ": without Gems the Gem goal is blocked");
        }
    }
}

// A chain on the bosses fixture, by own placement: the Keys sit on checks,
// the sweep must bank them to open keyed pads, and a Key placed behind its
// own gate leaves the room short.
static void chain() {
    std::ifstream in("tools/fixtures/content-plan/baseline.json");
    const J slot = J::parse(in);
    const auto ids = plan_locations(slot);
    expect(admit(slot, ids), "baseline: admitted for the chain case");
    const J &plan = slot["content_plan"];
    std::map<std::string, std::string> entrance;
    for (const auto &pad : plan["pads"]) if (!pad["entry_id"].is_null()) entrance[pad["entry_id"]] = pad["entrance_req"];
    std::map<int, std::vector<int64_t>> by_keys; // Keys a check's pad needs -> its checks
    for (const auto &c : plan["checks"]) {
        if (c["routes"].size() != 1) continue;
        const std::string req = entrance[c["routes"][0]["entry_id"].get<std::string>()];
        by_keys[req == "free" ? 0 : std::stoi(req.substr(5))].push_back(c["location"].get<int64_t>());
    }
    expect(by_keys[0].size() >= 1 && by_keys[1].size() >= 1 && by_keys[2].size() >= 2 && by_keys[3].size() >= 1,
           "baseline has free checks and checks behind one, two and three Keys");
    Room room;
    for (auto id : ids) room.present.insert(id);
    for (int64_t code = 35010000; code < 35010014; ++code) room.foreign[code] = 99;
    // Key n sits on a check that n Keys open, so each one opens the next.
    room.own[by_keys[0][0]] = room.own[by_keys[1][0]] = room.own[by_keys[2][0]] = room.own[by_keys[3][0]] = 35010014;
    auto r = run(room);
    expect(r.reachable == r.total && r.keys == 4, "a Key chain is banked check by check and opens every keyed pad");
    // The first Key behind a one-Key pad: nothing keyed opens.
    Room locked = room;
    locked.own.erase(by_keys[0][0]);
    locked.own[by_keys[1][1 % by_keys[1].size()]] = 35010014;
    r = run(locked);
    expect(r.keys == 0 && r.reachable == (int)by_keys[0].size(), "a Key behind its own gate leaves every keyed pad shut");
}

static void probe(const char *path) {
    std::ifstream in(path);
    std::string line;
    int rows = 0, samples = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const J row = J::parse(line);
        ++rows;
        const std::string tag = row.value("tag", "row " + std::to_string(rows));
        std::vector<int64_t> ids;
        for (const auto &l : row["locations"]) ids.push_back(l.get<int64_t>());
        if (!admit(row["slot_data"], ids)) { expect(false, tag + ": room refused"); continue; }
        Room base;
        for (auto id : ids) base.present.insert(id);
        for (const auto &c : row["precollected"]) base.foreign[c.get<int64_t>()]++;
        for (const auto &c : row["foreign"]) base.foreign[c.get<int64_t>()]++;
        // The full placement: own items banked as the sweep reaches them.
        {
            Room room = base;
            for (auto it = row["own"].begin(); it != row["own"].end(); ++it) room.own[std::stoll(it.key())] = it.value().get<int64_t>();
            ap_content_verify_result r;
            std::set<int64_t> want;
            for (const auto &c : row["full"]["reachable"]) want.insert(c.get<int64_t>());
            const auto got = reachable_set(room, r);
            expect(got == want && r.goal == row["full"]["goal"].get<bool>(),
                   tag + " full placement: goal " + std::to_string(r.goal) + diff(want, got));
        }
        for (const auto &s : row["samples"]) {
            ++samples;
            Room room = base;
            for (const auto &c : s["items"]) room.foreign[c.get<int64_t>()]++;
            ap_content_verify_result r;
            std::set<int64_t> want;
            for (const auto &c : s["reachable"]) want.insert(c.get<int64_t>());
            const auto got = reachable_set(room, r);
            expect(got == want && r.goal == s["goal"].get<bool>(),
                   tag + " sample " + std::to_string(samples) + ": goal " + std::to_string(r.goal) + " want " +
                       std::to_string(s["goal"].get<bool>()) + diff(want, got));
        }
    }
    std::cout << "probe rows " << rows << ", inventory samples " << samples << '\n';
}

int main(int argc, char **argv) {
    if (argc > 2 && std::string(argv[1]) == "--probe") probe(argv[2]);
    else { fixtures(); chain(); }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
