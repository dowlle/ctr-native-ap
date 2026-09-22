// Included by ap_seedcfg.cpp so every production-parser harness exercises the
// same admission path. Deliberately implements only the standalone Trophy
// profile; a broader schema shape is not a promise of runtime support.
#include "ap_content_plan.h"
#include "ap_content_registry.hpp"
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <algorithm>

namespace content_plan {
using J = nlohmann::json;
struct State {
    bool parsed = false, bound = false;
    std::array<ctr_content_pad, 27> pads{};
    std::array<ctr_content_package, 2> packages{};
    std::array<ctr_content_item, 15> items{};
    int package_count = 0, goal_count = 0, goal_mask = 0;
    std::set<int64_t> checks;
    std::string canonical;
};
static State state;
static std::string previous_identity;
static uint64_t epoch = 0;
static void need(bool ok, const std::string &why) {
    if (!ok) throw std::runtime_error(why);
}
static void forget() {
    state = State{};
    if (!previous_identity.empty()) { ++epoch; previous_identity.clear(); }
}
static void fields(const J &j, std::initializer_list<const char *> names) {
    need(j.is_object() && j.size() == names.size(), "unsupported or incomplete record fields");
    for (const char *name : names) need(j.contains(name), std::string("missing field: ") + name);
}
static int number(const J &j, int low = 0, int high = INT32_MAX) {
    need(j.is_number_integer(), "expected exact integer");
    if (j.is_number_unsigned()) need(j.get<uint64_t>() <= (uint64_t)high, "integer outside supported bounds");
    const int64_t n = j.get<int64_t>();
    need(n >= low && n <= high, "integer outside supported bounds");
    return (int)n;
}
static std::string text(const J &j, bool key = true) {
    need(j.is_string(), "expected string");
    const std::string s = j.get<std::string>();
    need(!s.empty() && s.size() <= (key ? 64u : 128u), "string outside supported byte bounds");
    for (unsigned char c : s) {
        if (key) need((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || (c && std::strchr("_.:/+-", c)), "invalid identity string");
        else need(c >= 32 && c != 127, "control character in display label");
    }
    return s;
}
template<size_t N> static void copy(char (&dest)[N], const J &source) {
    const auto s = source.get<std::string>();
    need(s.size() < N, "runtime text buffer exceeded");
    std::memcpy(dest, s.c_str(), s.size() + 1);
}
static void budget(const J &j, unsigned depth, size_t &nodes, size_t &bytes) {
    need(depth <= 32 && ++nodes <= 500000, "nesting/node budget exceeded");
    need(!j.is_number_float() && !j.is_discarded() && !j.is_binary(), "non-exact JSON value");
    bytes += 2;
    if (j.is_string()) {
        const auto &s = j.get_ref<const std::string &>();
        need(s.size() <= 128, "UTF-8 string budget exceeded");
        bytes += s.size();
    } else if (j.is_object() || j.is_array()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (j.is_object()) { need(it.key().size() <= 64, "field name too long"); bytes += it.key().size(); }
            budget(it.value(), depth + 1, nodes, bytes);
        }
    }
    need(bytes <= 16 * 1024 * 1024, "content-plan byte budget exceeded");
}
using Table = std::map<std::string, const J *>;
static Table table(const J &rows, size_t maximum) {
    need(rows.is_array() && rows.size() <= maximum, "record array exceeds profile bounds");
    Table result;
    for (const auto &row : rows) {
        const auto id = text(row.at("id"));
        need(result.emplace(id, &row).second, "duplicate record identity: " + id);
    }
    return result;
}
static void validate(const J &p, State &out) {
    size_t nodes = 0, bytes = 0;
    budget(p, 0, nodes, bytes);
    out.canonical = p.dump(); // also validates UTF-8
    need(out.canonical.size() <= 16 * 1024 * 1024, "serialized byte budget exceeded");
    fields(p, {"version", "required_features", "packages", "tracks", "entries", "pads", "encounters", "requirements", "checks", "items", "goal"});
    need(p.at("version") == 1 && p.at("required_features") == J::array({"content_plan_v1"}), "unsupported required content-plan version or feature");
    need(p.at("encounters") == J::array(), "encounter routes are not implemented");
    static const J registry = J::parse(kContentRegistryJSON);
    const auto packages = table(p.at("packages"), 2);
    const auto tracks = table(p.at("tracks"), 27);
    const auto entries = table(p.at("entries"), 27);
    const auto pads = table(p.at("pads"), 32);
    const auto requirements = table(p.at("requirements"), 5);
    need(pads.size() == 32 && requirements.size() == 5, "physical pad or requirement registry mismatch");
    for (int i = 0; i <= 4; ++i) {
        const auto id = i ? "keys:" + std::to_string(i) : "free";
        const J pred = i ? J{{"op", "count"}, {"item", 35010014}, {"count", i}} : J{{"op", "always"}};
        need(requirements.count(id) && *requirements.at(id) == J{{"id", id}, {"predicate", pred}}, "physical hub requirement mismatch");
    }
    std::map<std::string, int> package_index;
    for (const auto &pair : packages) {
        const J &pkg = *pair.second;
        need(std::find(registry["packages"].begin(), registry["packages"].end(), pkg) != registry["packages"].end(), "unsupported exact package revision or evidence");
        const int idx = out.package_count++;
        package_index.emplace(pair.first, idx);
        auto &target = out.packages[idx];
        copy(target.id, pkg["id"]); copy(target.content_id, pkg["content_id"]);
        copy(target.uuid, pkg["uuid"]); copy(target.revision, pkg["revision"]);
        copy(target.display_name, pkg["display_name"]); copy(target.author, pkg["author"]);
        copy(target.lev_sha256, pkg["files"][0]["sha256"]); copy(target.vrm_sha256, pkg["files"][1]["sha256"]);
        target.lev_bytes = number(pkg["files"][0]["bytes"], 1);
        target.vrm_bytes = number(pkg["files"][1]["bytes"], 1);
    }
    std::set<std::string> used_entries, used_tracks, used_packages;
    std::set<int> retail_ids, custom_slots;
    std::map<int64_t, J> expected_checks;
    bool starting_entry = false;
    for (int pi = 0; pi < 32; ++pi) {
        const bool garage = pi >= 27;
        const int physical = garage ? pi - 27 : registry["pads"][pi]["physical"].get<int>();
        const int hub = garage ? (physical < 4 ? 26 + physical : 25) : registry["pads"][pi]["hub"].get<int>();
        const int keys = garage ? 0 : registry["pads"][pi]["keys"].get<int>();
        const auto id = std::string(garage ? "garage:" : "pad:") + std::to_string(physical);
        need(pads.count(id), "missing canonical physical pad: " + id);
        const J &pad = *pads.at(id);
        fields(pad, {"id", "hub_id", "kind", "entry_id", "entrance_req", "mode_gates", "racer_lock"});
        need(pad["hub_id"] == "hub:" + std::to_string(hub) && pad["kind"] == (garage ? "garage" : "normal") &&
             pad["entrance_req"] == (keys ? "keys:" + std::to_string(keys) : "free") && pad["racer_lock"].is_null(), "pad hub, gate or kind mismatch");
        ctr_content_pad scratch{};
        auto &dest = garage ? scratch : out.pads[pi];
        dest.physical = physical; dest.hub = hub; dest.keys = keys;
        dest.retail_id = -1; dest.package_index = -1;
        if (pad["entry_id"].is_null()) {
            need(pad["mode_gates"] == J::array(), "empty pad has mode gates");
            continue;
        }
        const auto entry_id = text(pad["entry_id"]);
        need(!garage && entries.count(entry_id) && used_entries.insert(entry_id).second, "invalid or repeated pad entry");
        need(pad["mode_gates"] == J::array({J{{"mode", "trophy"}, {"stage", 1}, {"requirement_id", "free"}}}), "unsupported mode gates");
        const J &entry = *entries.at(entry_id);
        fields(entry, {"id", "kind", "track_id", "modes", "merged"});
        need(entry["kind"] == "track" && entry["modes"] == J::array({"trophy"}) && entry["merged"] == false, "unsupported entry kind or modes");
        const auto track_id = text(entry["track_id"]);
        need(tracks.count(track_id) && used_tracks.insert(track_id).second, "missing or repeated standalone track");
        const J &track = *tracks.at(track_id);
        need(track.at("modes") == J::array({"trophy"}) && track.at("capabilities") == J::array({"trophy"}) && track.at("field_size") == 8, "unsupported track capabilities");
        const auto title = text(track.at("display_name"), false);
        if (track.at("origin") == "retail") {
            fields(track, {"id", "origin", "retail_id", "display_name", "laps", "modes", "capabilities", "field_size"});
            const int rid = number(track["retail_id"], 0, 17);
            need(retail_ids.insert(rid).second && track_id == "retail:" + std::to_string(rid) && title == registry["retail"][rid]["name"] && track["laps"] == 3, "retail metadata mismatch");
            dest.retail_id = rid;
            dest.trophy_location = registry["retail"][rid]["location"].get<int64_t>();
        } else {
            fields(track, {"id", "origin", "package_id", "custom_slot", "display_name", "laps", "modes", "capabilities", "field_size"});
            const auto package_id = text(track["package_id"]);
            need(track["origin"] == "custom" && packages.count(package_id), "unsupported custom origin or package");
            const J &pkg = *packages.at(package_id);
            const int slot = number(track["custom_slot"], 1, 32);
            need(custom_slots.insert(slot).second && track_id == "custom/" + pkg["content_id"].get<std::string>() + "/" + pkg["revision"].get<std::string>() && title == pkg["display_name"] && track["laps"] == 7, "custom metadata mismatch");
            used_packages.insert(package_id);
            dest.package_index = package_index.at(package_id); dest.custom_slot = slot;
            dest.trophy_location = registry["custom_locations"][slot - 1].get<int64_t>();
        }
        dest.occupied = 1; dest.laps = number(track["laps"], 1, 7);
        copy(dest.entry_id, entry["id"]); copy(dest.track_id, track["id"]); copy(dest.display_name, track["display_name"]);
        const J route{{"entry_id", entry_id}, {"mode", "trophy"}, {"leg_id", nullptr}, {"encounter_id", nullptr}, {"own_pad_id", id}, {"award", "win"}};
        const J check{{"location", dest.trophy_location}, {"owner", track_id}, {"kind", "trophy"}, {"display_name", title + ": Trophy Race"}, {"routes", J::array({route})}};
        need(expected_checks.emplace(dest.trophy_location, check).second, "duplicate check identity");
        if (!keys) starting_entry = true;
    }
    need(used_entries.size() == entries.size() && used_tracks.size() == tracks.size() && used_packages.size() == packages.size(), "unused entry, track or package");
    need(tracks.empty() || starting_entry, "no playable starting pad");
    need(p["checks"].is_array() && p["checks"].size() == expected_checks.size(), "check registry size mismatch");
    for (const auto &check : p["checks"]) {
        const int64_t id = number(check.at("location"), 1);
        need(out.checks.insert(id).second && expected_checks.count(id) && check == expected_checks.at(id), "check identity or route ownership mismatch");
    }
    const J &items = p["items"];
    fields(items, {"rows", "coded_checks", "locked_checks", "filler_count", "trap_count"});
    need(items["rows"].is_array() && items["rows"].size() == 15, "item family registry mismatch");
    std::set<int> item_ids;
    int64_t remaining = 0;
    J gems = J::array();
    for (const auto &row : items["rows"]) {
        fields(row, {"item", "base", "extra", "start_from_pool", "start_additional", "locked_selected", "remaining", "receipt_cap", "requirement_ceiling"});
        const int i = number(row["item"], 35010000, 35010014) - 35010000;
        need(item_ids.insert(i).second, "duplicate item family");
        auto &r = out.items[i];
        r.base = number(row["base"]); r.extra = number(row["extra"]);
        r.start_from_pool = number(row["start_from_pool"], 0, 10000); r.start_additional = number(row["start_additional"], 0, 10000);
        r.remaining = number(row["remaining"]); r.receipt_cap = number(row["receipt_cap"]);
        need(row["locked_selected"] == 0 && r.remaining == (int64_t)r.base + r.extra - r.start_from_pool && r.receipt_cap == (int64_t)r.base + r.extra + r.start_additional && number(row["requirement_ceiling"]) == r.base, "selected item ledger mismatch");
        need(i != 14 || r.base == 4, "four base Keys are mandatory");
        need(i < 9 || i > 13 || r.base <= 1, "base Gems must be distinct colours");
        remaining += r.remaining;
    }
    for (int i = 9; i <= 13; ++i) if (out.items[i].base) { gems.push_back(35010000 + i); out.goal_mask |= 1 << (i - 9); }
    const int filler = number(items["filler_count"], 0, 4096);
    need(number(items["coded_checks"], 0, 4096) == (int)out.checks.size() && items["locked_checks"] == 0 && remaining + filler == (int64_t)out.checks.size() && number(items["trap_count"], 0, 4096) <= filler, "item/check capacity mismatch");
    fields(p["goal"], {"op", "items", "count"});
    out.goal_count = number(p["goal"]["count"], 1, 5);
    need(p["goal"]["op"] == "distinct" && p["goal"]["items"] == gems && out.goal_count <= (int)gems.size(), "goal must count selected distinct base Gems");
    out.parsed = true;
}
} // namespace content_plan

// Returns 0 refused, 1 legacy, 2 completely parsed new profile. This runs before
// any legacy parser early return, so a missing envelope cannot hide a block.
static int parse_content_plan(const nlohmann::json &j) {
    using namespace content_plan;
    state = State{};
    const bool block = j.is_object() && j.contains("content_plan");
    const bool flag = j.is_object() && j.contains("ctr_options") && j["ctr_options"].is_object() && j["ctr_options"].contains("content_plan");
    if (!block && !flag) { forget(); return 1; }
    try {
        fields(j, {"schema_version", "content_plan", "ctr_options"});
        need(number(j["schema_version"]) == 17, "content-plan global schema must be 17");
        const J &co = j["ctr_options"];
        fields(co, {"schema_version", "world_version", "content_plan", "goal", "goal_oxide", "goal_bosses", "goal_gems", "starting_character", "character_unlocks", "progressive_boost", "progressive_stats", "hit_character", "death_link", "deathlink_amnesty"});
        need(number(co["schema_version"]) == 17 && co["content_plan"] == true, "inconsistent content-plan envelope");
        need(co["goal"] == -1 && co["goal_oxide"] == 3 && co["goal_bosses"] == 0 && co["character_unlocks"] == false && co["progressive_boost"] == 0 && co["progressive_stats"] == 0 && co["hit_character"] == false, "unsupported content-plan options");
        size_t nodes = 0, bytes = 0; budget(co, 0, nodes, bytes);
        State candidate;
        validate(j["content_plan"], candidate);
        need(number(co["goal_gems"], 1, 5) == candidate.goal_count, "goal envelope disagrees with plan");
        ctr_cfg.starting_character = number(co["starting_character"], 0, 15);
        ctr_cfg.death_link = number(co["death_link"], 0, 2);
        ctr_cfg.deathlink_amnesty = number(co["deathlink_amnesty"], 1, 30);
        text(co["world_version"]); copy(ctr_cfg.world_version, co["world_version"]);
        ctr_cfg.goal = -1; ctr_cfg.goal_oxide = 3; ctr_cfg.goal_bosses = 0; ctr_cfg.goal_gems = candidate.goal_count;
        ctr_cfg.character_unlocks = 0;
        ctr_cfg.character_phase_present = 1;
        ctr_cfg.shuffle_gems = ctr_cfg.shuffle_keys = 1;
        ctr_cfg.shuffle_warp_pads = 0; // Hub objects keep their physical IDs.
        ctr_cfg.warppad_unlock_mode = ctr_cfg.bossgarage_mode = 0;
        ctr_cfg.one_lap_cups = ctr_cfg.oxide_1_optional = 0;
        ctr_cfg.relic_min_time = ctr_cfg.relics_require_perfect = 0;
        state = std::move(candidate);
        ctr_cfg.schema_version = 17; // commit after complete validation
        return 2;
    } catch (const std::exception &e) {
        forget(); hit_reject("content_plan: %s", e.what()); return 0;
    }
}

extern "C" int ap_content_plan_present(void) { return content_plan::state.parsed && ctr_cfg.schema_version == 17 && !ctr_cfg.seed_rejected; }
extern "C" int ap_content_plan_active(void) { return ap_content_plan_present() && content_plan::state.bound; }
extern "C" uint64_t ap_content_plan_epoch(void) { return ap_content_plan_active() ? content_plan::epoch : 0; }
extern "C" const ctr_content_pad *ap_content_plan_pad(int physical) {
    if (ap_content_plan_active()) for (const auto &p : content_plan::state.pads) if (p.physical == physical) return &p;
    return nullptr;
}
extern "C" const ctr_content_package *ap_content_plan_package(int index) {
    return ap_content_plan_active() && index >= 0 && index < content_plan::state.package_count ? &content_plan::state.packages[index] : nullptr;
}
extern "C" const ctr_content_item *ap_content_plan_item(int64_t item) {
    return ap_content_plan_active() && item >= 35010000 && item <= 35010014 ? &content_plan::state.items[item - 35010000] : nullptr;
}
extern "C" int ap_content_plan_goal(const int received_gems[5]) {
    if (!ap_content_plan_active() || !received_gems) return 0;
    int count = 0;
    for (int i = 0; i < 5; ++i) if ((content_plan::state.goal_mask & (1 << i)) && received_gems[i] > 0) ++count;
    return count >= content_plan::state.goal_count;
}
extern "C" int ap_content_plan_bind(const char *seed, int team, int slot, const int64_t *locations, size_t count) {
    using namespace content_plan;
    state.bound = false;
    try {
        need(ap_content_plan_present(), "no parsed content plan to bind");
        need(seed && seed[0] && std::strlen(seed) <= 1024 && team >= 0 && slot > 0, "missing room/slot identity");
        need(count == state.checks.size() && (!count || locations), "room location count differs from content plan");
        std::set<int64_t> actual;
        for (size_t i = 0; i < count; ++i) need(actual.insert(locations[i]).second, "duplicate room location");
        need(actual == state.checks, "room locations differ from content plan");
        const std::string identity = J::array({seed, team, slot, state.canonical}).dump();
        if (identity != previous_identity) { ++epoch; previous_identity = identity; }
        state.bound = true;
        return 1;
    } catch (const std::exception &e) {
        forget(); ap_seedcfg_reject_late(e.what()); return 0;
    }
}
