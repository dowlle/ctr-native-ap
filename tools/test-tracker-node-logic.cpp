// g++ -m32 -std=c++17 -O1 -DCTR_AP -Iap -Iap/vendor/json/include tools/test-tracker-node-logic.cpp ap/ap_seedcfg.cpp -o /tmp/test-tracker-node-logic
#include "ap_content_plan.h"
#include "ap_seedcfg.h"
#include "ap_pad_logic_look.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using Json = nlohmann::json;
using Codes = std::set<int64_t>;
extern "C" void AP_LogLine(const char *) {}

static Json read(const char *path)
{
    std::ifstream stream(path);
    assert(stream);
    return Json::parse(stream);
}

struct State {
    std::map<long, int> items;
    Codes checked;
};

static int item_count(void *user, long id)
{
    const auto &items = static_cast<State *>(user)->items;
    const auto it = items.find(id);
    return it == items.end() ? 0 : it->second;
}

static int collected(void *user, long id)
{
    return static_cast<State *>(user)->checked.count(id);
}

static int run(const Json &slot, const Json &states)
{
    ap_seedcfg_parse_json(slot);
    assert(ap_content_plan_present());
    std::vector<int64_t> locations;
    std::map<std::string, Codes> by_node, boxes_by_track;
    std::map<std::string, std::string> node_of_entry;
    const auto &plan = slot.at("content_plan");
    for (const auto &node : plan.at("pads")) {
        const auto id = node.at("id").get<std::string>();
        by_node[id];
        if (node.at("entry_id").is_string())
            node_of_entry[node.at("entry_id").get<std::string>()] = id;
    }
    if (plan.contains("encounters")) for (const auto &encounter : plan.at("encounters"))
        node_of_entry[encounter.at("entry_id").get<std::string>()] = encounter.at("pad_id");
    for (const auto &check : plan.at("checks")) {
        const int64_t code = check.at("location");
        locations.push_back(code);
        for (const auto &route : check.at("routes"))
            by_node[route.at("own_pad_id").get<std::string>()].insert(code);
        if (check.at("kind") == "box")
            boxes_by_track[check.at("owner").get<std::string>()].insert(code);
    }
    for (const auto &entry : plan.at("entries")) {
        if (entry.at("kind") != "cup" && entry.at("kind") != "boss_cup") continue;
        auto &node = by_node.at(node_of_entry.at(entry.at("id").get<std::string>()));
        for (const auto &leg : entry.at("legs")) {
            const auto &boxes = boxes_by_track[leg.at("track_id").get<std::string>()];
            node.insert(boxes.begin(), boxes.end());
        }
    }
    assert(ap_content_plan_bind("tracker-room", 0, 1, locations.data(), locations.size()));
    int errors = 0;
    for (const auto &entry : by_node) {
        const auto &name = entry.first;
        const bool garage = name.rfind("garage:", 0) == 0;
        const int node = std::stoi(name.substr(name.find(':') + 1)) +
                         (garage ? AP_CONTENT_GARAGE_CUP_PAD : 0);
        int count = 0;
        const int64_t *codes = ap_content_plan_tracker_locations(node, &count);
        Codes actual;
        for (int i = 0; i < count; ++i) actual.insert(codes[i]);
        assert(actual == entry.second && int(actual.size()) == count);
        for (const auto &sample : states) {
            State state;
            for (const auto &row : sample.at("received").items())
                state.items[std::stol(row.key())] = row.value();
            state.checked = sample.at("checked").get<Codes>();
            const Codes expected = sample.at("in_logic").get<Codes>();
            AP_WinLogicEnv env{};
            env.user = &state;
            env.item_count = item_count;
            env.item_count_includes_start = 1;
            env.collected = collected;
            env.character_unlocks = ctr_cfg.character_unlocks;
            env.starting_character = ctr_cfg.starting_character;
            env.boost_mode = ctr_cfg.boost_mode;
            Codes got, want;
            for (int64_t code : actual) {
                if (expected.count(code)) want.insert(code);
                if (!state.checked.count(code) && AP_PadLogicStateOf(&ctr_win_logic, &env, code) == 1)
                    got.insert(code);
            }
            std::cout << sample.at("label") << ' ' << name << " (" << count << " checks): "
                      << want.size() << " expected, " << got.size() << " in logic";
            if (got != want) {
                ++errors;
                for (int64_t code : want) if (!got.count(code)) std::cout << " missing " << code;
                for (int64_t code : got) if (!want.count(code)) std::cout << " extra " << code;
            }
            std::cout << '\n';
        }
    }
    return errors;
}

int main(int argc, char **argv)
{
    int vanilla[20]{};
    ctr_cfg_set_vanilla_cup_legs(vanilla);
    if (argc == 2) {
        int errors = 0;
        for (const auto &room : read(argv[1]))
            errors += run(room.at("slot_data"), room.at("states"));
        return errors != 0;
    }
    assert(run(read("tools/fixtures/content-plan/boss-cups-gauntlet.json"), Json::array()) == 0);
    assert(run(read("tools/fixtures/content-plan/global-checks.json"), Json::array()) == 0);
    std::cout << "Tracker nodes contain each unique accepted route, including shared and garage routes.\n";
}
