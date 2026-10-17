// Included by ap_seedcfg.cpp so every production-parser harness exercises the
// same admission path. Implements standalone track entries with their Trophy,
// CTR Challenge and Relic Race modes (#133 PR 3), their podium rungs, Reach 10
// Wumpa and Lettersanity checks (#133 PR 3b), Cortex Vortex as a track and the
// Relic Race Perfect checks (#133 PR 3c), and Crystal Challenge entries; a
// broader schema shape is not a promise of runtime support.
// Custom packages are admitted openly (#133 PR 2): any complete descriptor.
#include "ap_content_plan.h"
#include "ap_content_registry.hpp"
#include "ap_named_blocks.h"
#include "ap_arena_content.h"
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
    std::array<ctr_content_package, AP_CONTENT_MAX_PACKAGES> packages{};
    std::array<ctr_content_item, 15> items{};
    ctr_content_checks families{};
    int package_count = 0, goal_count = 0, goal_mask = 0;
    bool cortex = false; // a Cortex Vortex track entry (#133 PR 3c)
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
// Open package admission (#133 PR 2). The apworld applies the same rules in
// worlds/ctr/custom_packages.py; tools/test-content-plan-parity.py compares
// the two on forged descriptors. A descriptor is untrusted input: every field
// is closed and bounded, and nothing here grants a capability. The bytes are
// located, hashed and measured later, on load (ap_hooks_custom_content.c).
static bool track_key(const std::string &s) {
    if (s.empty() || s.size() > 64 || s.front() == '-' || s.back() == '-') return false;
    char previous = 0;
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
        if (c == '-' && previous == '-') return false;
        previous = c;
    }
    return true;
}
static bool revision_text(const std::string &s) {
    if (s.empty() || s.size() > 64) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && (i == 0 || !std::strchr("._+-", c))) return false;
    }
    return true;
}
static bool lower_hex(const std::string &s, size_t n) {
    return s.size() == n && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
static bool lower_uuid(const std::string &s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
static void package_record(const J &pkg, ctr_content_package &target) {
    need(pkg.is_object(), "package record is not an object");
    J base = pkg;
    const bool targets = base.contains("relic_targets");
    if (targets) base.erase("relic_targets");
    fields(base, {"id", "content_id", "uuid", "revision", "display_name", "author", "laps", "capabilities", "files", "evidence"});
    need(pkg["content_id"].is_string() && track_key(pkg["content_id"].get<std::string>()), "package track key is not lowercase hyphenated text");
    need(pkg["revision"].is_string() && revision_text(pkg["revision"].get<std::string>()), "package revision has unsupported characters");
    const std::string key = pkg["content_id"], revision = pkg["revision"];
    const std::string id = "package/" + key + "/" + revision;
    need(id.size() <= 64 && ("custom/" + key + "/" + revision).size() <= 64, "package id exceeds 64 bytes");
    need(pkg["id"] == id, "package id is not package/<content_id>/<revision>");
    need(pkg["uuid"].is_string() && lower_uuid(pkg["uuid"].get<std::string>()), "package uuid is not a lowercase UUID");
    text(pkg["display_name"], false); text(pkg["author"], false);
    target.laps = number(pkg["laps"], 1, 7);
    const J &caps = pkg["capabilities"];
    fields(caps, {"time_trial", "arcade", "relic", "ctr_tokens", "wumpa"});
    static const std::pair<const char *, unsigned> cap_bits[] = {
        {"time_trial", AP_CONTENT_CAP_TIME_TRIAL}, {"arcade", AP_CONTENT_CAP_ARCADE}, {"relic", AP_CONTENT_CAP_RELIC},
        {"ctr_tokens", AP_CONTENT_CAP_CTR_TOKENS}, {"wumpa", AP_CONTENT_CAP_WUMPA}};
    target.capabilities = 0;
    for (const auto &cap : cap_bits) {
        need(caps[cap.first].is_boolean(), "package capability flag is not true or false");
        if (caps[cap.first].get<bool>()) target.capabilities |= cap.second;
    }
    const bool relic = (target.capabilities & AP_CONTENT_CAP_RELIC) != 0;
    need(relic == targets, "relic_targets must be present exactly when relic is true");
    target.has_relic_targets = targets;
    if (targets) {
        const J &t = pkg["relic_targets"];
        fields(t, {"sapphire", "gold", "platinum"});
        const char *tiers[3] = {"sapphire", "gold", "platinum"};
        for (int i = 0; i < 3; ++i) target.relic_targets[i] = number(t[tiers[i]], 1, 960 * 60 * 60);
        need(target.relic_targets[0] > target.relic_targets[1] && target.relic_targets[1] > target.relic_targets[2],
             "relic targets must get strictly faster from sapphire to platinum");
    }
    const J &files = pkg["files"];
    need(files.is_array() && files.size() == 2, "package must pin exactly the LEV then the VRM");
    static const char *roles[2] = {"lev", "vrm"};
    static const int limits[2] = {16 * 1024 * 1024, 4 * 1024 * 1024};
    std::set<std::string> hashes;
    for (int i = 0; i < 2; ++i) {
        const J &file = files[i];
        fields(file, {"role", "sha256", "bytes", "format", "format_version"});
        need(file["role"] == roles[i] && file["format"] == std::string("ctr-") + roles[i] &&
             file["format_version"].is_number_integer() && file["format_version"] == 1, "package file pin role, format or version mismatch");
        need(file["sha256"].is_string() && lower_hex(file["sha256"].get<std::string>(), 64), "package file hash is not 64 lowercase hex characters");
        hashes.insert(file["sha256"].get<std::string>());
        const int size = number(file["bytes"], 1, limits[i]);
        (i ? target.vrm_bytes : target.lev_bytes) = (uint32_t)size;
    }
    const J &evidence = pkg["evidence"];
    need(evidence.is_array() && evidence.size() <= 16, "package evidence array bounds");
    for (const auto &row : evidence) {
        fields(row, {"capability", "file_hashes", "verifier", "revision", "level"});
        text(row["capability"]); text(row["verifier"]); number(row["revision"], 1);
        need(row["level"] == "structural" || row["level"] == "runtime", "package evidence level");
        const J &refs = row["file_hashes"];
        need(refs.is_array() && !refs.empty() && refs.size() <= 2, "package evidence hash list bounds");
        std::set<std::string> seen;
        for (const auto &h : refs)
            need(h.is_string() && hashes.count(h.get<std::string>()) && seen.insert(h.get<std::string>()).second,
                 "package evidence names a hash that is not one of its pins");
    }
    copy(target.id, pkg["id"]); copy(target.content_id, pkg["content_id"]);
    copy(target.uuid, pkg["uuid"]); copy(target.revision, pkg["revision"]);
    copy(target.display_name, pkg["display_name"]); copy(target.author, pkg["author"]);
    copy(target.lev_sha256, files[0]["sha256"]); copy(target.vrm_sha256, files[1]["sha256"]);
}
// Room titles compare without ASCII case, as the apworld's clash rules do.
static std::string fold(std::string s) {
    for (char &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}
struct NamedBlock { int block; std::string name; };
// The apworld's named-track table, from ap_content_registry.hpp: track key to
// block and frozen room name. A malformed table refuses every plan.
static const std::map<std::string, NamedBlock> &named_block_table(const J &registry) {
    static const std::map<std::string, NamedBlock> table = [&registry] {
        std::map<std::string, NamedBlock> out;
        std::set<int> blocks;
        std::set<std::string> names;
        for (const auto &row : registry.at("named_blocks")) {
            const std::string key = row.at("key"), name = row.at("name");
            const int block = row.at("block");
            if (!track_key(key) || block < 0 || block >= AP_NAMED_BLOCK_LIMIT || !ap_room_title_ok(name.c_str(), name.size()) ||
                !blocks.insert(block).second || !names.insert(fold(name)).second || !out.emplace(key, NamedBlock{block, name}).second)
                return std::map<std::string, NamedBlock>{};
        }
        return out;
    }();
    need(!table.empty(), "compiled named-block table is malformed");
    return table;
}
// Per-track check families (#133 PR 3b). Rung keys in SLOT_ORDER (apworld
// podium.SLOT_ORDER) with their location suffixes; bit k is rung k.
static const char *const rung_keys[5] = {"held_1st", "held_3rd", "held_5th", "finish_podium", "finish_any"};
static const char *const rung_suffixes[5] = {"Held 1st", "Held 3rd", "Held 5th", "Finish on Podium", "Finish (Any Position)"};
static const char *const letter_keys[3] = {"letter_c", "letter_t", "letter_r"};
static const char *const letter_suffixes[3] = {"Letter C", "Letter T", "Letter R"};
// The rung sets the podium options can create (podium.created_rung_keys): held
// 1st and 3rd, optionally 5th; finish on podium, optionally any position.
static bool rung_set_ok(int mask) {
    const int held = mask & 7, finish = mask & 24;
    return mask && (held == 0 || held == 3 || held == 7) && (finish == 0 || finish == 8 || finish == 24);
}
static void family_settings(const J &t, ctr_content_checks &fam) {
    need(t.is_object() && !t.empty(), "track_checks must be a nonempty record");
    for (auto it = t.begin(); it != t.end(); ++it)
        need(it.key() == "podium_rungs" || it.key() == "wumpa" || it.key() == "lettersanity" || it.key() == "letters_per_track" ||
             it.key() == "relic_perfect", "unknown track_checks field");
    if (t.contains("podium_rungs")) {
        const J &rungs = t["podium_rungs"];
        need(rungs.is_array() && !rungs.empty() && rungs.size() <= 5, "podium_rungs list bounds");
        int mask = 0, last = -1;
        for (const auto &key : rungs) {
            int k = -1;
            for (int i = 0; i < 5; ++i) if (key == rung_keys[i]) k = i;
            need(k > last, "podium_rungs must list known rungs in slot order");
            last = k; mask |= 1 << k;
        }
        need(rung_set_ok(mask), "podium_rungs is not a rung set the podium options create");
        fam.podium = mask;
    }
    if (t.contains("wumpa")) {
        need(t["wumpa"] == "global" || t["wumpa"] == "per_track", "track_checks.wumpa must be global or per_track");
        fam.wumpa = t["wumpa"] == "global" ? 1 : 2;
    }
    if (t.contains("lettersanity")) fam.lettersanity = number(t["lettersanity"], 1, 3);
    need(t.contains("letters_per_track") == (fam.lettersanity == 1 || fam.lettersanity == 2),
         "letters_per_track belongs to Lettersanity modes 1 and 2 only");
    if (t.contains("letters_per_track")) fam.letters_per_track = number(t["letters_per_track"], 1, 3);
    // Relic Race Perfect checks (#133 PR 3c): present only as true.
    if (t.contains("relic_perfect")) {
        need(t["relic_perfect"].is_boolean() && t["relic_perfect"].get<bool>(), "track_checks.relic_perfect must be true when present");
        fam.relic_perfect = 1;
    }
}
// Pool-only packs (#133 PR 4a): items that add no location and no rule. The
// plan's optional `item_packs` record sizes their ledger rows (apworld
// content_plan.PACK_ITEMS). Returns item code -> copies.
static std::map<int64_t, int> item_packs(const J &t) {
    static const char *const keys[3] = {"progressive_starting_wumpa", "turbo_grant", "tizi_helper"};
    static const int64_t codes[3] = {35010122, 35010189, 35010188};
    need(t.is_object() && !t.empty() && t.size() <= 3, "item_packs must be a nonempty record");
    std::map<int64_t, int> out;
    size_t seen = 0;
    for (int k = 0; k < 3; ++k) {
        if (!t.contains(keys[k])) continue;
        ++seen;
        const J &v = t[keys[k]];
        if (k == 0) out[codes[k]] = number(v, 1, 10);
        else { need(v.is_boolean() && v.get<bool>(), "item_packs grants must be true when present"); out[codes[k]] = 1; }
    }
    need(seen == t.size(), "unknown item_packs field");
    return out;
}
static unsigned record_caps(const State &out, const ctr_content_pad &pad) {
    return pad.package_index >= 0 ? out.packages[pad.package_index].capabilities : 0u;
}
static J route_record(const std::string &entry, const char *mode, const std::string &pad, const char *award) {
    return J{{"entry_id", entry}, {"mode", mode}, {"leg_id", nullptr}, {"encounter_id", nullptr}, {"own_pad_id", pad}, {"award", award}};
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
    // `track_checks` (#133 PR 3b) is optional: absent when the seed has no
    // podium rungs, Wumpa checks or Lettersanity.
    const bool has_families = p.is_object() && p.contains("track_checks");
    const bool has_packs = p.is_object() && p.contains("item_packs");
    need(p.is_object() && p.size() == 11u + (has_families ? 1u : 0u) + (has_packs ? 1u : 0u), "unsupported or incomplete record fields");
    for (const char *name : {"version", "required_features", "packages", "tracks", "entries", "pads", "encounters", "requirements", "checks", "items", "goal"})
        need(p.contains(name), std::string("missing field: ") + name);
    need(p.at("version") == 1, "unsupported required content-plan version");
    ctr_content_checks &fam = out.families;
    fam = ctr_content_checks{};
    if (has_families) family_settings(p["track_checks"], fam);
    const std::map<int64_t, int> packs = has_packs ? item_packs(p["item_packs"]) : std::map<int64_t, int>{};
    need(p.at("encounters") == J::array(), "encounter routes are not implemented");
    static const J registry = J::parse(kContentRegistryJSON);
    const auto &named_table = named_block_table(registry);
    // Names a generic-slot (B+) room title must not take: every retail track
    // and every named block, whether or not it is in this seed.
    std::set<std::string> reserved_titles;
    for (const auto &row : registry["retail"]) reserved_titles.insert(fold(row["name"].get<std::string>()));
    for (const auto &row : named_table) reserved_titles.insert(fold(row.second.name));
    reserved_titles.insert(fold(registry["cortex_vortex"]["name"].get<std::string>()));
    const auto packages = table(p.at("packages"), AP_CONTENT_MAX_PACKAGES);
    const auto tracks = table(p.at("tracks"), 27);
    const auto entries = table(p.at("entries"), 27);
    const auto pads = table(p.at("pads"), 32);
    const auto requirements = table(p.at("requirements"), 64); // 5 hub, up to 27 manual and 27 stage-2
    need(pads.size() == 32 && requirements.size() >= 5, "physical pad or requirement registry mismatch");
    // Manual pad requirements (#463): every record beyond the five physical hub
    // ones must be `manual:<physical>`, is checked against the item ledger
    // below, and is the only gate its own pad may reference. The feature name is
    // required exactly when such a record exists, so a client without this
    // evaluator never reads a Key-only plan by mistake.
    // Second-stage records (#133 PR 3) are `stage2:<physical>`: the shared
    // gate of a pad's later modes, parsed and checked exactly like a manual rule.
    std::map<int, std::string> manual_ids, stage2_ids;
    for (const auto &pair : requirements) {
        if (pair.first == "free" || pair.first.rfind("keys:", 0) == 0) continue;
        const bool later = pair.first.rfind("stage2:", 0) == 0;
        const size_t prefix = 7; // "manual:" and "stage2:" have the same length
        need((later || pair.first.rfind("manual:", 0) == 0) && pair.first.size() > prefix && pair.first.size() <= prefix + 3, "unknown requirement record: " + pair.first);
        const std::string digits = pair.first.substr(prefix);
        need(std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) && (digits.size() == 1 || digits[0] != '0'), "malformed pad requirement id");
        need((later ? stage2_ids : manual_ids).emplace(std::stoi(digits), pair.first).second, "duplicate pad requirement");
    }
    need(requirements.size() == 5 + manual_ids.size() + stage2_ids.size(), "requirement registry mismatch");
    bool arenas=false, arcade=false, pickups=false, modes=false;
    for (const auto &pair : entries) if(pair.second->at("kind")=="crystal") {
        arenas=true; arcade |= AP_ArcadeArenaIndex(number(pair.second->at("arena_id"),18,24))>=0;
    } else modes |= pair.second->at("modes") != J::array({"trophy"});
    need(p["checks"].is_array() && p["checks"].size()<=4096,"check array bounds");
    for(const auto &check:p["checks"]) pickups |= check.at("kind")=="crystal_pickup";
    J features=J::array({"content_plan_v1"});
    if(!manual_ids.empty())features.push_back("manual_pad_requirements_v1");
    if(arenas)features.push_back("crystal_entries_v1");
    if(pickups)features.push_back("crystal_pickups_v1");
    if(arcade)features.push_back("arcade_crystals_v1");
    if(modes)features.push_back("track_modes_v1");
    if(fam.podium)features.push_back("podium_rungs_v1");
    if(fam.wumpa)features.push_back("wumpa_checks_v1");
    if(fam.lettersanity)features.push_back("lettersanity_v1");
    if(fam.relic_perfect)features.push_back("relic_perfect_v1");
    if(!packs.empty())features.push_back("item_packs_v1");
    need(p.at("required_features") == features,
         "unsupported or inconsistent required content-plan features");
    for (int i = 0; i <= 4; ++i) {
        const auto id = i ? "keys:" + std::to_string(i) : "free";
        const J pred = i ? J{{"op", "count"}, {"item", 35010014}, {"count", i}} : J{{"op", "always"}};
        need(requirements.count(id) && *requirements.at(id) == J{{"id", id}, {"predicate", pred}}, "physical hub requirement mismatch");
    }
    // Records in id order (the apworld emits them sorted), with no repeated
    // track key, package uuid or LEV/VRM pair: one identity per track.
    std::map<std::string, int> package_index;
    std::set<std::string> keys, uuids, pairs;
    std::string previous_id;
    for (const auto &pkg : p["packages"]) {
        const std::string id = text(pkg.at("id"));
        need(package_index.empty() || id > previous_id, "package records are not sorted by id");
        previous_id = id;
        const int idx = out.package_count++;
        package_index.emplace(id, idx);
        auto &target = out.packages[idx];
        package_record(pkg, target);
        need(keys.insert(target.content_id).second && uuids.insert(target.uuid).second &&
             pairs.insert(std::string(target.lev_sha256) + target.vrm_sha256).second,
             "a track appears twice in the package list");
    }
    std::set<std::string> used_entries, used_tracks, used_packages;
    std::set<int> retail_ids, custom_slots, named_blocks, arena_ids;
    bool cortex_seen = false;
    std::map<int64_t, J> expected_checks;
    // Lettersanity modes 1 and 2 draw which letters have checks; the plan's own
    // check list says which, and each CTR Challenge must have letters_per_track.
    std::set<int64_t> plan_codes;
    for (const auto &check : p["checks"])
        if (check.is_object() && check.contains("location") && check["location"].is_number_integer())
            plan_codes.insert(check["location"].get<int64_t>());
    std::set<int64_t> letter_items;                    // pooled letter items (modes 2 and 3)
    struct GlobalRoute { int physical; std::string entry, pad; unsigned modes; };
    std::vector<GlobalRoute> global_routes;           // `wumpa: global`
    std::map<int, ctr_content_pad *> manual_pad; // occupied pad per manual record
    std::map<int, ctr_content_pad *> stage2_pad; // pad whose later modes use each stage-2 record
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
        dest.retail_id = -1; dest.package_index = -1; dest.named_block = -1;
        if (pad["entry_id"].is_null()) {
            need(pad["mode_gates"] == J::array(), "empty pad has mode gates");
            continue;
        }
        const auto entry_id = text(pad["entry_id"]);
        need(!garage && entries.count(entry_id) && used_entries.insert(entry_id).second, "invalid or repeated pad entry");
        const J &entry = *entries.at(entry_id);
        const bool crystal=entry.at("kind")=="crystal";
        const std::string first_gate = manual_ids.count(physical) ? manual_ids.at(physical) : std::string("free");
        if (crystal)
            need(pad["mode_gates"] == J::array({J{{"mode", "crystal"}, {"stage", 1}, {"requirement_id", first_gate}}}), "unsupported mode gates");
        if (manual_ids.count(physical)) manual_pad[physical] = &dest;
        if(crystal) {
            fields(entry,{"id","kind","arena_id","target","time_limit_ticks"});
            const int level=number(entry["arena_id"],18,24);
            const std::string owner="arena:"+std::to_string(level);
            need(arena_ids.insert(level).second && entry_id=="entry/"+owner && entry["target"]==20 &&
                 number(entry["time_limit_ticks"])==AP_ArenaTimeLimit(level),"arena identity, target or timer mismatch");
            dest.occupied=dest.crystal=1; dest.retail_id=level; dest.laps=1;
            dest.target=20; dest.time_limit_ticks=AP_ArenaTimeLimit(level);
            dest.trophy_location=AP_ArenaCompletionCode(level);
            copy(dest.entry_id,entry["id"]); copy(dest.track_id,J(owner));
            copy(dest.display_name,J(AP_ArenaName(level))); copy(dest.room_title,J(AP_ArenaName(level)));
            J route{{"entry_id",entry_id},{"mode","crystal"},{"leg_id",nullptr},{"encounter_id",nullptr},{"own_pad_id",id},{"award","complete"}};
            J check{{"location",dest.trophy_location},{"owner",owner},{"kind","crystal"},
                    {"display_name",std::string(AP_ArenaName(level))+": Crystal Challenge"},{"routes",J::array({route})}};
            need(expected_checks.emplace(dest.trophy_location,check).second,"duplicate completion");
            if(pickups) {
                dest.crystal_count=20;
                for(int n=0;n<20;++n) {
                    dest.crystal_locations[n]=AP_ArenaPickupCode(level,n);
                    const std::string ordinal=(n<9?"0":"")+std::to_string(n+1);
                    route["award"]="crystal:"+ordinal;
                    check["location"]=dest.crystal_locations[n]; check["kind"]="crystal_pickup";
                    check["display_name"]=std::string(AP_ArenaName(level))+": Crystal "+ordinal;
                    check["routes"]=J::array({route});
                    need(expected_checks.emplace(dest.crystal_locations[n],check).second,"duplicate pickup");
                }
            }
            if(!keys)starting_entry=true;
            continue;
        }
        fields(entry, {"id", "kind", "track_id", "modes", "merged"});
        need(entry["kind"] == "track" && entry["merged"] == false, "unsupported entry kind or merged mode");
        // Modes (#133 PR 3): the entry lists its modes in the pad's resolved
        // order; the track lists the same modes in canonical order.
        static const char *mode_names[AP_CONTENT_MODE_COUNT] = {"trophy", "ctr", "relic"};
        const J &entry_modes = entry["modes"];
        need(entry_modes.is_array() && !entry_modes.empty() && entry_modes.size() <= AP_CONTENT_MODE_COUNT, "entry mode list bounds");
        unsigned mode_bits = 0;
        dest.mode_count = 0;
        for (const auto &m : entry_modes) {
            int mode = -1;
            for (int k = 0; k < AP_CONTENT_MODE_COUNT; ++k) if (m == mode_names[k]) mode = k;
            need(mode >= 0 && !(mode_bits & (1u << mode)), "unknown or repeated entry mode");
            mode_bits |= 1u << mode;
            dest.modes[dest.mode_count++] = mode;
        }
        J canonical_modes = J::array();
        for (int k = 0; k < AP_CONTENT_MODE_COUNT; ++k) if (mode_bits & (1u << k)) canonical_modes.push_back(mode_names[k]);
        const auto track_id = text(entry["track_id"]);
        need(tracks.count(track_id) && used_tracks.insert(track_id).second, "missing or repeated standalone track");
        const J &track = *tracks.at(track_id);
        need(track.at("modes") == canonical_modes && track.at("capabilities") == canonical_modes && track.at("field_size") == 8, "unsupported track modes or capabilities");
        // With two stages the first mode opens with the pad and the others share
        // `stage2:<physical>`; with one stage every mode uses the entrance gate.
        const bool later_stage = stage2_ids.count(physical) != 0;
        J gates = J::array();
        for (int k = 0; k < dest.mode_count; ++k) {
            dest.stages[k] = k && later_stage ? 2 : 1;
            gates.push_back(J{{"mode", mode_names[dest.modes[k]]}, {"stage", dest.stages[k]},
                              {"requirement_id", k && later_stage ? stage2_ids.at(physical) : first_gate}});
        }
        need(pad["mode_gates"] == gates, "unsupported mode gates");
        if (later_stage && dest.mode_count > 1) { stage2_pad[physical] = &dest; dest.has_stage2 = 1; }
        const auto title = text(track.at("display_name"), false);
        std::string check_title = title;
        int64_t codes[AP_CONTENT_MODE_COUNT][3] = {};
        unsigned supported = 0;
        if (track.at("origin") == "retail") {
            fields(track, {"id", "origin", "retail_id", "display_name", "laps", "modes", "capabilities", "field_size"});
            const int rid = number(track["retail_id"], 0, 17);
            need(retail_ids.insert(rid).second && track_id == "retail:" + std::to_string(rid) && title == registry["retail"][rid]["name"] && track["laps"] == AP_CONTENT_RETAIL_LAPS, "retail metadata mismatch");
            dest.retail_id = rid;
            const J &row = registry["retail"][rid];
            codes[AP_CONTENT_MODE_TROPHY][0] = row["location"].get<int64_t>();
            codes[AP_CONTENT_MODE_CTR][0] = row["ctr"].get<int64_t>();
            for (int t = 0; t < 3; ++t) codes[AP_CONTENT_MODE_RELIC][t] = row["relic"][t].get<int64_t>();
            supported = 7u; // retail tracks run all three modes
        } else if (track.at("origin") == "cortex_vortex") {
            // Cortex Vortex (#133 PR 3c): the bundled Lockheart pair under its
            // own frozen identity, with no package row and no custom slot. Its
            // bytes are located and hashed on load (ap_hooks_custom_content.c).
            fields(track, {"id", "origin", "package_id", "display_name", "laps", "modes", "capabilities", "field_size"});
            const J &row = registry["cortex_vortex"];
            need(!cortex_seen && track_id == row["id"].get<std::string>() && track["package_id"] == row["package_id"] &&
                 title == row["name"].get<std::string>() && track["laps"] == row["laps"] && row["laps"] == AP_CONTENT_CORTEX_LAPS,
                 "Cortex Vortex metadata mismatch");
            cortex_seen = true;
            out.cortex = true;
            dest.cortex = 1;
            codes[AP_CONTENT_MODE_TROPHY][0] = row["location"].get<int64_t>();
            codes[AP_CONTENT_MODE_CTR][0] = row["ctr"].get<int64_t>();
            for (int t = 0; t < 3; ++t) codes[AP_CONTENT_MODE_RELIC][t] = row["relic"][t].get<int64_t>();
            supported = 7u; // AI paths, the three letters and time crates: all three modes
        } else {
            // #306: optional `named_block` (the track's own id block, path C)
            // and `room_title` (the sanitized title the room shows, path B+).
            J base = track;
            const bool named = base.is_object() && base.contains("named_block");
            const bool titled = base.is_object() && base.contains("room_title");
            if (named) base.erase("named_block");
            if (titled) base.erase("room_title");
            fields(base, {"id", "origin", "package_id", "custom_slot", "display_name", "laps", "modes", "capabilities", "field_size"});
            const auto package_id = text(track["package_id"]);
            need(track["origin"] == "custom" && packages.count(package_id), "unsupported custom origin or package");
            const J &pkg = *packages.at(package_id);
            const int slot = number(track["custom_slot"], 1, AP_CONTENT_MAX_PACKAGES);
            const ctr_content_package &record = out.packages[package_index.at(package_id)];
            need(custom_slots.insert(slot).second && track_id == "custom/" + pkg["content_id"].get<std::string>() + "/" + pkg["revision"].get<std::string>() && title == pkg["display_name"] && number(track["laps"], 1, 7) == record.laps, "custom metadata mismatch");
            // Measured capabilities decide the modes (apworld supported_modes):
            // Trophy needs arcade, CTR arcade and the letters, Relic time
            // crates with targets on a timed lap graph.
            const unsigned caps = record.capabilities;
            if (caps & AP_CONTENT_CAP_ARCADE) supported |= 1u << AP_CONTENT_MODE_TROPHY;
            if ((caps & AP_CONTENT_CAP_ARCADE) && (caps & AP_CONTENT_CAP_CTR_TOKENS)) supported |= 1u << AP_CONTENT_MODE_CTR;
            if ((caps & AP_CONTENT_CAP_RELIC) && (caps & AP_CONTENT_CAP_TIME_TRIAL) && record.has_relic_targets) supported |= 1u << AP_CONTENT_MODE_RELIC;
            used_packages.insert(package_id);
            dest.package_index = package_index.at(package_id); dest.custom_slot = slot;
            dest.named_block = -1;
            if (titled) {
                const auto room = text(track["room_title"], false);
                need(ap_room_title_ok(room.c_str(), room.size()), "invalid room title");
                check_title = room;
            }
            // A key with a named block must use that block under its frozen
            // name; any other key takes its generic slot (apworld content_plan.validate).
            const auto owned = named_table.find(record.content_id);
            if (named) {
                dest.named_block = number(track["named_block"], 0, AP_NAMED_BLOCK_LIMIT - 1);
                need(owned != named_table.end() && dest.named_block == owned->second.block &&
                     titled && check_title == owned->second.name, "named block does not match this build's registry");
                need(named_blocks.insert(dest.named_block).second, "duplicate named block");
                codes[AP_CONTENT_MODE_TROPHY][0] = ap_named_location_code(dest.named_block, AP_NAMED_OFF_TROPHY);
                codes[AP_CONTENT_MODE_CTR][0] = ap_named_location_code(dest.named_block, AP_NAMED_OFF_CTR_TOKEN);
                for (int t = 0; t < 3; ++t) codes[AP_CONTENT_MODE_RELIC][t] = ap_named_location_code(dest.named_block, AP_NAMED_OFF_RELIC_SAPPHIRE + t);
            } else {
                need(owned == named_table.end(), "a track with a named block must use it");
                need(!reserved_titles.count(fold(check_title)), "generic-slot room title takes a retail or named track's name");
                codes[AP_CONTENT_MODE_TROPHY][0] = registry["custom_locations"][slot - 1].get<int64_t>();
                codes[AP_CONTENT_MODE_CTR][0] = registry["custom_ctr_locations"][slot - 1].get<int64_t>();
                for (int t = 0; t < 3; ++t) codes[AP_CONTENT_MODE_RELIC][t] = registry["custom_relic_locations"][slot - 1][t].get<int64_t>();
            }
        }
        need((mode_bits & ~supported) == 0, "a track mode its capabilities do not allow");
        dest.occupied = 1; dest.laps = number(track["laps"], 1, 7);
        copy(dest.entry_id, entry["id"]); copy(dest.track_id, track["id"]); copy(dest.display_name, track["display_name"]);
        copy(dest.room_title, J(check_title));
        dest.trophy_location = (mode_bits & (1u << AP_CONTENT_MODE_TROPHY)) ? codes[AP_CONTENT_MODE_TROPHY][0] : 0;
        dest.ctr_location = (mode_bits & (1u << AP_CONTENT_MODE_CTR)) ? codes[AP_CONTENT_MODE_CTR][0] : 0;
        for (int t = 0; t < 3; ++t)
            dest.relic_locations[t] = (mode_bits & (1u << AP_CONTENT_MODE_RELIC)) ? codes[AP_CONTENT_MODE_RELIC][t] : 0;
        static const char *awards[AP_CONTENT_MODE_COUNT][3] = {{"win"}, {"win_letters"}, {"sapphire", "gold", "platinum"}};
        static const char *suffixes[AP_CONTENT_MODE_COUNT][3] = {{"Trophy Race"}, {"CTR Token Challenge"},
                                                                 {"Sapphire Time Trial", "Gold Time Trial", "Platinum Time Trial"}};
        // Per-track check families (#133 PR 3b): retail rows, the named
        // block's offsets, or the generic slot's families.
        int64_t rung[5] = {}, wumpa = 0, letter[3] = {}, letter_item[3] = {}, perfect = 0;
        if (dest.retail_id >= 0 || dest.cortex) {
            const J &row = dest.cortex ? registry["cortex_vortex"] : registry["retail"][dest.retail_id];
            for (int k = 0; k < 5; ++k) rung[k] = row["podium"][k].get<int64_t>();
            wumpa = row["wumpa"].get<int64_t>();
            perfect = row["perfect"].get<int64_t>();
            for (int l = 0; l < 3; ++l) { letter[l] = row["letters"][l].get<int64_t>(); letter_item[l] = row["letter_items"][l].get<int64_t>(); }
        } else if (dest.named_block >= 0) {
            for (int k = 0; k < 5; ++k) rung[k] = ap_named_location_code(dest.named_block, AP_NAMED_OFF_HELD_1ST + k);
            wumpa = ap_named_location_code(dest.named_block, AP_NAMED_OFF_WUMPA);
            perfect = ap_named_location_code(dest.named_block, AP_NAMED_OFF_RELIC_PERFECT);
            for (int l = 0; l < 3; ++l) {
                letter[l] = ap_named_location_code(dest.named_block, AP_NAMED_OFF_LETTER_C + l);
                letter_item[l] = ap_named_item_code(dest.named_block, l);
            }
        } else {
            const int s = dest.custom_slot - 1;
            for (int k = 0; k < 5; ++k) rung[k] = registry["custom_podium_locations"][s][k].get<int64_t>();
            wumpa = registry["custom_wumpa_locations"][s].get<int64_t>();
            perfect = registry["custom_perfect_locations"][s].get<int64_t>();
            for (int l = 0; l < 3; ++l) {
                letter[l] = registry["custom_letter_locations"][s][l].get<int64_t>();
                letter_item[l] = registry["custom_letter_items"][s][l].get<int64_t>();
            }
        }
        auto family_check = [&](int64_t code, const char *kind, const std::string &suffix, const J &routes) {
            const J check{{"location", code}, {"owner", track_id}, {"kind", kind},
                          {"display_name", check_title + ": " + suffix}, {"routes", routes}};
            need(expected_checks.emplace(code, check).second, "duplicate check identity");
        };
        for (int k = 0; k < dest.mode_count; ++k) {
            const int mode = dest.modes[k];
            for (int t = 0; t < (mode == AP_CONTENT_MODE_RELIC ? 3 : 1); ++t) {
                const J route{{"entry_id", entry_id}, {"mode", mode_names[mode]}, {"leg_id", nullptr}, {"encounter_id", nullptr}, {"own_pad_id", id}, {"award", awards[mode][t]}};
                const J check{{"location", codes[mode][t]}, {"owner", track_id}, {"kind", mode_names[mode]},
                              {"display_name", check_title + ": " + suffixes[mode][t]}, {"routes", J::array({route})}};
                need(expected_checks.emplace(codes[mode][t], check).second, "duplicate check identity");
            }
            // A Relic Race carries its Relic Race Perfect check (#133 PR 3c),
            // the track's own code, never the engine host's.
            if (mode == AP_CONTENT_MODE_RELIC && fam.relic_perfect) {
                dest.perfect_location = perfect;
                family_check(perfect, "perfect", "Relic Race Perfect", J::array({route_record(entry_id, "relic", id, "perfect")}));
            }
            // A Trophy Race carries the seed's podium rungs.
            for (int r = 0; mode == AP_CONTENT_MODE_TROPHY && r < 5; ++r)
                if (fam.podium & (1 << r)) {
                    dest.rung_locations[r] = rung[r];
                    family_check(rung[r], "podium", rung_suffixes[r], J::array({route_record(entry_id, "trophy", id, rung_keys[r])}));
                }
            // A CTR Challenge carries letters_per_track letter checks
            // (Lettersanity modes 1 and 2); which ones is the seed's draw.
            if (mode == AP_CONTENT_MODE_CTR && (fam.lettersanity == 1 || fam.lettersanity == 2)) {
                int drawn = 0;
                for (int l = 0; l < 3; ++l)
                    if (plan_codes.count(letter[l])) {
                        ++drawn;
                        dest.letter_locations[l] = letter[l];
                        family_check(letter[l], "letter", letter_suffixes[l], J::array({route_record(entry_id, "ctr", id, letter_keys[l])}));
                    }
                need(drawn == fam.letters_per_track, "letter checks do not match letters_per_track");
            }
        }
        // Lettersanity items: mode 2 the drawn letters, mode 3 all three.
        if ((mode_bits & (1u << AP_CONTENT_MODE_CTR)) && fam.lettersanity >= 2)
            for (int l = 0; l < 3; ++l)
                if (fam.lettersanity == 3 || dest.letter_locations[l]) {
                    dest.letter_items[l] = letter_item[l];
                    need(letter_items.insert(letter_item[l]).second, "duplicate letter item");
                }
        // Reach 10 Wumpa: a Trophy Race or CTR Challenge on a track that can
        // reach ten fruit (a custom package needs its measured `wumpa` flag).
        const bool fruit = dest.retail_id >= 0 || dest.cortex || (record_caps(out, dest) & AP_CONTENT_CAP_WUMPA);
        const unsigned reach = mode_bits & ((1u << AP_CONTENT_MODE_TROPHY) | (1u << AP_CONTENT_MODE_CTR));
        if (fam.wumpa && fruit && reach) {
            dest.wumpa_modes = reach;
            if (fam.wumpa == 2) {
                dest.wumpa_location = wumpa;
                J routes = J::array();
                for (int m = 0; m < 2; ++m)
                    if (reach & (1u << m)) routes.push_back(route_record(entry_id, mode_names[m], id, "wumpa"));
                family_check(wumpa, "wumpa", "Reach 10 Wumpa", routes);
            } else global_routes.push_back(GlobalRoute{physical, entry_id, id, reach});
        }
        if (!keys) starting_entry = true;
    }
    need(used_entries.size() == entries.size() && used_tracks.size() == tracks.size() && used_packages.size() == packages.size(), "unused entry, track or package");
    // `wumpa: global`: one check, a route per Trophy Race and CTR Challenge
    // that can reach ten fruit, in physical pad order, then mode order.
    if (fam.wumpa == 1) {
        fam.wumpa_global = registry["wumpa_global"].get<int64_t>();
        std::sort(global_routes.begin(), global_routes.end(),
                  [](const GlobalRoute &a, const GlobalRoute &b) { return a.physical < b.physical; });
        J routes = J::array();
        for (const auto &r : global_routes)
            for (int m = 0; m < 2; ++m)
                if (r.modes & (1u << m)) routes.push_back(route_record(r.entry, m ? "ctr" : "trophy", r.pad, "wumpa"));
        if (!routes.empty())
            need(expected_checks.emplace(fam.wumpa_global, J{{"location", fam.wumpa_global}, {"owner", "global"}, {"kind", "wumpa"},
                                                             {"display_name", "Wumpa: Reach 10 Wumpa"}, {"routes", routes}}).second,
                 "duplicate check identity");
    }
    // Two checks never share a name, so a room cannot show two identical rows.
    std::set<std::string> check_names;
    for (const auto &pair : expected_checks)
        need(check_names.insert(fold(pair.second["display_name"].get<std::string>())).second, "two checks share a display name");
    need(entries.empty() || starting_entry, "no playable starting pad");
    need(p["checks"].is_array() && p["checks"].size() == expected_checks.size(), "check registry size mismatch");
    for (const auto &check : p["checks"]) {
        const int64_t id = number(check.at("location"), 1);
        need(out.checks.insert(id).second && expected_checks.count(id) && check == expected_checks.at(id), "check identity or route ownership mismatch");
    }
    const J &items = p["items"];
    fields(items, {"rows", "coded_checks", "locked_checks", "filler_count", "trap_count"});
    // The 15 counted families, plus one row per pooled letter item (#133 PR 3b)
    // and one per pool-only pack item (#133 PR 4a).
    need(items["rows"].is_array() && items["rows"].size() == 15 + letter_items.size() + packs.size(), "item family registry mismatch");
    std::set<int> item_ids;
    std::set<int64_t> letter_rows, pack_rows;
    int64_t remaining = 0;
    J gems = J::array();
    for (const auto &row : items["rows"]) {
        fields(row, {"item", "base", "extra", "start_from_pool", "start_additional", "locked_selected", "remaining", "receipt_cap", "requirement_ceiling"});
        const int64_t code = number(row["item"], 1);
        ctr_content_item letter_row{};
        if (letter_items.count(code)) {
            need(letter_rows.insert(code).second && row["base"] == 1 && row["extra"] == 0, "a letter item is pooled once");
        }
        const bool pack = packs.count(code) != 0;
        if (pack)
            need(pack_rows.insert(code).second && row["base"] == packs.at(code) && row["extra"] == 0, "a pack item row disagrees with item_packs");
        const int i = letter_items.count(code) || pack ? -1 : number(row["item"], 35010000, 35010014) - 35010000;
        need(i < 0 || item_ids.insert(i).second, "duplicate item family");
        auto &r = i < 0 ? letter_row : out.items[i];
        r.base = number(row["base"]); r.extra = number(row["extra"]);
        r.start_from_pool = number(row["start_from_pool"], 0, 10000); r.start_additional = number(row["start_additional"], 0, 10000);
        r.remaining = number(row["remaining"]); r.receipt_cap = number(row["receipt_cap"]);
        need(row["locked_selected"] == 0 && r.remaining == (int64_t)r.base + r.extra - r.start_from_pool && r.receipt_cap == (int64_t)r.base + r.extra + r.start_additional && number(row["requirement_ceiling"]) == r.base, "selected item ledger mismatch");
        need(i != 14 || r.base == 4, "four base Keys are mandatory");
        need(i >= 0 || pack || r.base == 1, "a letter item is pooled once");
        need(i < 9 || i > 13 || r.base <= 1, "base Gems must be distinct colours");
        remaining += r.remaining;
    }
    // Manual requirement records: item identities and counts come from the
    // ledger, so a forged record cannot name an item the seed lacks or ask for
    // more than the base supply (`requirement_ceiling`).
    need(manual_pad.size() == manual_ids.size(), "manual requirement without an occupied pad");
    need(stage2_pad.size() == stage2_ids.size(), "second-stage requirement without a pad that uses it");
    for (int pass = 0; pass < 2; ++pass)
    for (const auto &pair : pass ? stage2_ids : manual_ids) {
        fields(*requirements.at(pair.second), {"id", "predicate"});
        const J &pred = requirements.at(pair.second)->at("predicate");
        ctr_content_pad &target = *(pass ? stage2_pad : manual_pad).at(pair.first);
        long long supply = 0;
        unsigned mask = 0;
        if (pred.is_object() && pred.contains("op") && pred["op"] == "count") {
            fields(pred, {"op", "item", "count"});
            const int item = number(pred["item"], 35010000, 35010014) - 35010000;
            mask = 1u << item; supply = out.items[item].base;
        } else {
            need(pred.is_object() && pred.contains("op") && pred["op"] == "sum", "unsupported manual predicate");
            fields(pred, {"op", "items", "count"});
            need(pred["items"].is_array() && !pred["items"].empty() && pred["items"].size() <= 5, "sum rule item list");
            static const int families[3][2] = {{1, 3}, {4, 8}, {9, 13}}; // relic tiers, CTR Tokens, Gems
            bool matched = false;
            for (const auto &family : families) {
                J eligible = J::array();
                for (int i = family[0]; i <= family[1]; ++i) if (out.items[i].base) eligible.push_back(35010000 + i);
                if (pred["items"] == eligible) {
                    matched = true;
                    for (int i = family[0]; i <= family[1]; ++i) if (out.items[i].base) { mask |= 1u << i; supply += out.items[i].base; }
                }
            }
            need(matched, "sum rule items are not exactly one family's base items");
        }
        const int count = number(pred["count"], 1);
        need(mask && count <= supply, "pad rule count outside the base supply");
        if (pass) { target.stage2_count = count; target.stage2_mask = mask; }
        else { target.has_rule = 1; target.rule_count = count; target.rule_mask = mask; }
    }
    need(item_ids.size() == 15 && letter_rows.size() == letter_items.size() && pack_rows.size() == packs.size(), "item family registry mismatch");
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
        // `trap_identities` (#133 PR 4a): the block every schema-18 room
        // carries. Optional here, so an earlier content-plan room keeps the
        // frozen trap table; when present it is read like a normal room's.
        const bool traps = j.is_object() && j.contains("trap_identities");
        need(j.is_object() && j.size() == 3u + (traps ? 1u : 0u) && j.contains("schema_version") && j.contains("content_plan") &&
             j.contains("ctr_options"), "unsupported or incomplete record fields");
        need(number(j["schema_version"]) == 18, "content-plan global schema must be 18");
        const J &co = j["ctr_options"];
        J base=co;
        for(const char *key:{"include_battle_arenas","include_arcade_arenas","crystalsanity"})base.erase(key);
        // Optional envelope keys (#133 PR 4a), present when the option is set:
        // an explicit DeathLink send mask and the two warp-pad display settings
        // (apworld content_plan.ENVELOPE_OPTIONAL). Absent keeps the defaults.
        need(co.is_object(), "content-plan envelope must be an object");
        const bool send = co.contains("death_link_send"), display = co.contains("warp_pad_item_display"),
                   colors = co.contains("ap_item_type_colors");
        const int send_mask = send ? number(co["death_link_send"], 0, 7) : -1;
        const int display_mode = display ? number(co["warp_pad_item_display"], 0, 1) : 0;
        need(!colors || co["ap_item_type_colors"].is_boolean(), "ap_item_type_colors must be true or false");
        for(const char *key:{"death_link_send","warp_pad_item_display","ap_item_type_colors"})base.erase(key);
        fields(base, {"schema_version", "world_version", "content_plan", "goal", "goal_oxide", "goal_bosses", "goal_gems", "starting_character", "character_unlocks", "progressive_boost", "progressive_stats", "hit_character", "death_link", "deathlink_amnesty"});
        need(number(co["schema_version"]) == 18 && co["content_plan"] == true, "inconsistent content-plan envelope");
        need(co["goal"] == -1 && co["goal_oxide"] == 3 && co["goal_bosses"] == 0 && co["character_unlocks"] == false && co["progressive_boost"] == 0 && co["progressive_stats"] == 0 && co["hit_character"] == false, "unsupported content-plan options");
        size_t nodes = 0, bytes = 0; budget(co, 0, nodes, bytes);
        State candidate;
        validate(j["content_plan"], candidate);
        bool arenas=false,arcade=false,pickups=false;
        for(const auto &pad:candidate.pads)if(pad.occupied && pad.crystal) {
            arenas=true;arcade |= AP_ArcadeArenaIndex(pad.retail_id)>=0;pickups |= pad.crystal_count>0;
        }
        const size_t optional = (send ? 1u : 0u) + (display ? 1u : 0u) + (colors ? 1u : 0u);
        if(arenas) {
            need(co.size()==base.size()+3+optional && co.at("include_battle_arenas")==true &&
                 co.at("include_arcade_arenas")==arcade && co.at("crystalsanity")==pickups,"arena options disagree with plan");
        } else need(co.size()==base.size()+optional && !co.contains("include_battle_arenas") &&
                    !co.contains("include_arcade_arenas") && !co.contains("crystalsanity"),"arena options without arena entries");
        // The plan owns content availability; legacy arena-pad filtering must
        // not close a physical arena seat holding a Trophy entry.
        ctr_cfg.include_battle_arenas=1;
        need(number(co["goal_gems"], 1, 5) == candidate.goal_count, "goal envelope disagrees with plan");
        ctr_cfg.starting_character = number(co["starting_character"], 0, 15);
        // 0 off, 1 mask_reset, 2 any_hit, 3 race_loss (#286). Before #133 PR 4a
        // this refused race_loss, which the apworld emits for content plans too.
        ctr_cfg.death_link = number(co["death_link"], 0, 3);
        ctr_cfg.deathlink_amnesty = number(co["deathlink_amnesty"], 1, 30);
        ctr_cfg.death_link_send = send_mask; // -1 absent: the legacy coupling
        ctr_cfg.warp_pad_item_display = display_mode;
        ctr_cfg.ap_item_type_colors = colors ? (co["ap_item_type_colors"].get<bool>() ? 1 : 0) : 1;
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
        if (traps) ap_seedcfg_parse_trap_identities(j, 18);
        ctr_cfg.schema_version = 18; // commit after complete validation
        return 2;
    } catch (const std::exception &e) {
        forget(); hit_reject("content_plan: %s", e.what()); return 0;
    }
}

extern "C" int ap_content_plan_present(void) { return content_plan::state.parsed && ctr_cfg.schema_version == 18 && !ctr_cfg.seed_rejected; }
extern "C" int ap_content_plan_active(void) { return ap_content_plan_present() && content_plan::state.bound; }
extern "C" uint64_t ap_content_plan_epoch(void) { return ap_content_plan_active() ? content_plan::epoch : 0; }
extern "C" const ctr_content_pad *ap_content_plan_pad(int physical) {
    if (ap_content_plan_active()) for (const auto &p : content_plan::state.pads) if (p.physical == physical) return &p;
    return nullptr;
}
extern "C" int ap_content_plan_cortex(void) { return ap_content_plan_active() && content_plan::state.cortex; }
extern "C" const ctr_content_package *ap_content_plan_package(int index) {
    return ap_content_plan_active() && index >= 0 && index < content_plan::state.package_count ? &content_plan::state.packages[index] : nullptr;
}
extern "C" const ctr_content_checks *ap_content_plan_checks(void) {
    static const ctr_content_checks none{};
    return ap_content_plan_active() ? &content_plan::state.families : &none;
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
