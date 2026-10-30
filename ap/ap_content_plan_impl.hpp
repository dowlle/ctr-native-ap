// Included by ap_seedcfg.cpp so every production-parser harness exercises the
// same admission path. Implements standalone track entries with their Trophy,
// CTR Challenge and Relic Race modes (#133 PR 3), their podium rungs, Reach 10
// Wumpa and Lettersanity checks (#133 PR 3b), Cortex Vortex as a track and the
// Relic Race Perfect checks (#133 PR 3c), pool-only and character packs (#133
// PR 4a, 4b), capability packs with per-check boost ranks (#133 PR 4c),
// Itemsanity and Hit Character as global checks (#133 PR 4d), Item Box
// Locations on retail and named-block custom tracks (#133 PR 4e), and Crystal
// Challenge entries; a
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
    // Character unlocks (#133 PR 4b): the plan's `racers` record.
    bool racers = false, remove_oxide = false;
    int starting_character = -1, requested_locks = 0;
    // Capability packs (#133 PR 4c): the `capability` record, each check's
    // boost rank, and every ledger row's starting copies.
    ctr_content_capability capability{};
    std::map<int64_t, int> boosts, starts;
    std::set<int64_t> checks;
    // Global check families (#133 PR 4d): the `global_checks` record.
    bool itemsanity = false, hit = false;
    // Item Box Locations (#133 PR 4e): the `item_boxes` record, and the box
    // checks that carry the hard-tier stat term (`stats`).
    ctr_content_item_boxes item_boxes{};
    std::set<int64_t> stat_checks;
    // Boss garages (#133 PR 6): the `bosses` record and the venues its
    // encounters race (engine LevelIDs; the Final Challenge on Cortex Vortex
    // or Oxide Station). goal_count is 0 when the goal has no Gem arm.
    ctr_content_bosses bosses{};
    // Boss cups (#133 PR 6b): each cup garage's legs as a cup pad, found by
    // ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD + g); never in `pads`.
    ctr_content_pad garage_cups[4]{};
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
    // `super_turbo_pads` (#133 PR 4c) is optional: whether the LEV has a super
    // turbo pad trigger quad. Measured and compared on load.
    J mode_caps = caps;
    target.super_turbo_pads = AP_CONTENT_SUPER_PADS_UNSTATED;
    if (mode_caps.is_object() && mode_caps.contains("super_turbo_pads")) {
        need(mode_caps["super_turbo_pads"].is_boolean(), "package capability flag is not true or false");
        target.super_turbo_pads = mode_caps["super_turbo_pads"].get<bool>() ? 1 : 0;
        mode_caps.erase("super_turbo_pads");
    }
    // `weapon_crates` (#133 PR 4e) is optional: how many weapon crates the
    // LEV holds, counted and compared on load. It seats a named track's boxes.
    target.weapon_crates = AP_CONTENT_WEAPON_CRATES_UNSTATED;
    if (mode_caps.is_object() && mode_caps.contains("weapon_crates")) {
        target.weapon_crates = number(mode_caps["weapon_crates"], 0, AP_CONTENT_WEAPON_CRATES_MAX);
        mode_caps.erase("weapon_crates");
    }
    // `checkpoints` and `ai_nav` (#133 PR 6b) are optional: the restart point
    // count and whether the LEV has AI paths, measured and compared on load.
    // Together they let the track be a boss venue.
    target.checkpoints = AP_CONTENT_UNSTATED;
    target.ai_nav = AP_CONTENT_UNSTATED;
    if (mode_caps.is_object() && mode_caps.contains("checkpoints")) {
        target.checkpoints = number(mode_caps["checkpoints"], 1, 255);
        mode_caps.erase("checkpoints");
    }
    if (mode_caps.is_object() && mode_caps.contains("ai_nav")) {
        need(mode_caps["ai_nav"].is_boolean(), "package capability flag is not true or false");
        target.ai_nav = mode_caps["ai_nav"].get<bool>() ? 1 : 0;
        mode_caps.erase("ai_nav");
    }
    fields(mode_caps, {"time_trial", "arcade", "relic", "ctr_tokens", "wumpa"});
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
// The five surface items (35010021..25) and the three relic items, Sapphire,
// Gold and Platinum (one generator row 2).
static const int64_t kSurfaceFirst = 35010021, kSurfaceLast = 35010025;
static const int kSurfaceCount = 5;
static const int64_t kRelicItems[3] = {35010001, 35010002, 35010003};
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
// Character unlocks (#133 PR 4b). Roster slot (the unlock item 35010123 +
// slot, apworld progressive_capability.ROSTER) -> engine character id, the
// mirror of characters.ROSTER_CHARACTER_ID and ap_capability.c
// AP_CAP_ROSTER_CHARACTER; the parity tool compares it with the apworld's.
static const int kRosterCharacter[16] = {0, 3, 6, 7, 1, 12, 10, 9, 11, 8, 5, 2, 4, 14, 15, 13};
static const int64_t kRacerItemBase = 35010123;
static const int kOxide = 15;          // NITROS_OXIDE
static const int kPhysicalPads = 27;   // the racer_locked_pads ceiling
static void racer_settings(const J &t, State &out) {
    fields(t, {"starting_character", "remove_playable_oxide", "requested_locks"});
    out.racers = true;
    out.starting_character = number(t["starting_character"], 0, 15);
    need(t["remove_playable_oxide"].is_boolean(), "racers.remove_playable_oxide must be true or false");
    out.remove_oxide = t["remove_playable_oxide"].get<bool>();
    need(!(out.remove_oxide && out.starting_character == kOxide), "Oxide cannot be removed and be the starting racer");
    out.requested_locks = number(t["requested_locks"], 0, kPhysicalPads);
}
// A racer the plan never pools nor locks: the starting racer, and Oxide when
// Remove Playable Oxide is on.
static bool racer_excluded(const State &out, int character) {
    return character == out.starting_character || (out.remove_oxide && character == kOxide);
}
// Capability packs (#133 PR 4c), apworld content_capability.check_settings:
// both modes 0 to 2 and not both off, Blue Fire only with boost, difficulty and
// knowledge 0 to 2.
static void capability_settings(const J &t, State &out) {
    fields(t, {"boost_mode", "boost_blue_fire", "stats_mode", "logic_difficulty", "shortcut_knowledge"});
    auto &c = out.capability;
    c.present = 1;
    c.boost_mode = number(t["boost_mode"], 0, 2);
    c.stats_mode = number(t["stats_mode"], 0, 2);
    c.logic_difficulty = number(t["logic_difficulty"], 0, 2);
    c.shortcut_knowledge = number(t["shortcut_knowledge"], 0, 2);
    need(t["boost_blue_fire"].is_boolean(), "capability.boost_blue_fire must be true or false");
    c.boost_blue_fire = t["boost_blue_fire"].get<bool>() ? 1 : 0;
    need(c.boost_mode || c.stats_mode, "capability is present only when Progressive Boost or Progressive Stats is on");
    need(!c.boost_blue_fire || c.boost_mode, "capability.boost_blue_fire needs Progressive Boost");
}
// The ledger rows a `capability` record pools (apworld
// progressive_capability.created_item_counts): Progressive Boost 35010027 with
// 2 copies, 3 with Blue Fire, or per racer 35010031 + 4 * roster slot; the
// three stat chains 35010028..30, or per racer + 1..3, four copies each.
static std::map<int64_t, int> capability_rows(const ctr_content_capability &c) {
    std::map<int64_t, int> rows;
    const int boost = c.boost_blue_fire ? 3 : 2;
    for (int slot = 0; slot < 16; ++slot)
        for (int chain = 0; chain < 4; ++chain) {
            const int mode = chain ? c.stats_mode : c.boost_mode;
            const int copies = chain ? 4 : boost;
            if (mode == 1 && slot == 0) rows[35010027 + chain] = copies;
            if (mode == 2) rows[35010031 + 4 * slot + chain] = copies;
        }
    return rows;
}
// Global check families (#133 PR 4d), apworld content_global_checks:
// `itemsanity` and `hit_character`, each present only as true.
// Item Box Locations (#133 PR 4e), apworld content_item_boxes.check_settings:
// exactly `shortcut_knowledge` 0 to 2, equal to the capability record's when
// that is present (checked by the caller once both are read).
static void item_box_settings(const J &t, State &out) {
    fields(t, {"shortcut_knowledge"});
    out.item_boxes.present = 1;
    out.item_boxes.shortcut_knowledge = number(t["shortcut_knowledge"], 0, 2);
}
// The seats a custom track's boxes take: min(crates, 15), spread over the
// crate table (ap_box_seat_logic.h computes which).
static int box_seats(int crates) {
    return crates <= 0 ? 0 : crates < AP_NAMED_ITEM_BOX_SLOTS ? crates : AP_NAMED_ITEM_BOX_SLOTS;
}
static void global_settings(const J &t, State &out) {
    need(t.is_object() && !t.empty() && t.size() <= 2, "global_checks must be a nonempty record");
    for (auto it = t.begin(); it != t.end(); ++it) {
        need(it.key() == "itemsanity" || it.key() == "hit_character", "unknown global_checks field");
        need(it.value().is_boolean() && it.value().get<bool>(), "global_checks entries must be true when present");
    }
    out.itemsanity = t.contains("itemsanity");
    out.hit = t.contains("hit_character");
}
// The 11 Itemsanity weapon items (35010095..105), one ledger row each.
static const int64_t kWeaponFirst = 35010095, kWeaponCount = 11;
static unsigned record_caps(const State &out, const ctr_content_pad &pad) {
    return pad.package_index >= 0 ? out.packages[pad.package_index].capabilities : 0u;
}
static J route_record(const std::string &entry, const char *mode, const std::string &pad, const char *award) {
    return J{{"entry_id", entry}, {"mode", mode}, {"leg_id", nullptr}, {"encounter_id", nullptr}, {"own_pad_id", pad}, {"award", award}};
}
// Boss garages (#133 PR 6): the `bosses` record (apworld content_bosses
// RECORD_KEYS). oxide_goal 0 none, 1 any_percent, 2 101_percent, 3 disabled;
// the Final Challenge relic mode 0..4 (4 total_relics) and its resolved count;
// the Oxide 1 skip only with 101_percent; repeats only with track_shuffle.
static void boss_settings(const J &b, ctr_content_bosses &out) {
    fields(b, {"oxide_goal", "bosses_required", "oxide_1_optional", "oxide_final_unlock", "oxide_final_count", "track_shuffle", "repeats", "requirement_level", "cups"});
    out.present = 1;
    // #133 PR 6b: the ordinary garages holding a boss cup, distinct and in order.
    need(b["cups"].is_array() && b["cups"].size() <= 4, "bosses.cups must list ordinary garages");
    int previous = -1;
    for (int g = 0; g < 4; ++g) out.cup[g] = 0;
    for (const auto &g : b["cups"]) {
        const int garage = number(g, 0, 3);
        need(garage > previous, "bosses.cups must be distinct and in order");
        out.cup[garage] = 1;
        previous = garage;
    }
    {
        static const char *const levels[4] = {"standard", "high", "very_high", "chaotic"};
        out.requirement_level = -1;
        if (b["requirement_level"].is_string())
            for (int i = 0; i < 4; ++i) if (b["requirement_level"] == levels[i]) out.requirement_level = i;
        need(out.requirement_level >= 0, "bosses.requirement_level must be standard, high, very_high or chaotic");
    }
    out.oxide_goal = number(b["oxide_goal"], 0, 3);
    out.bosses_required = number(b["bosses_required"], 0, 4);
    for (const char *key : {"oxide_1_optional", "track_shuffle", "repeats"})
        need(b[key].is_boolean(), std::string("bosses.") + key + " must be true or false");
    out.oxide_1_optional = b["oxide_1_optional"].get<bool>() ? 1 : 0;
    out.track_shuffle = b["track_shuffle"].get<bool>() ? 1 : 0;
    out.repeats = b["repeats"].get<bool>() ? 1 : 0;
    need(!out.oxide_1_optional || out.oxide_goal == 2, "bosses.oxide_1_optional belongs to oxide_goal 101_percent only");
    need(!out.repeats || out.track_shuffle, "bosses.repeats belongs to track_shuffle only");
    out.final_unlock = number(b["oxide_final_unlock"], 0, 4);
    out.final_count = number(b["oxide_final_count"], 1, out.final_unlock == 4 ? 54 : 18);
    need(out.oxide_goal != 3 || (out.final_unlock == 0 && out.final_count == 18),
         "bosses.oxide_final_unlock and oxide_final_count keep their defaults when Oxide is disabled");
}
// Vanilla pad requirements (one generator row 3): every pad keeps its original
// requirement on the physical pad, clamped to the seed's base items, and with
// any stage 2 in the plan every pad with several races asks Keys from the
// physical pad's hub (apworld content_requirements.vanilla_requirements).
static void vanilla_pads(const State &out)
{
    static const struct { int physical, first, last, count; } rows[] = {
        {3, 0, 0, 0}, {6, 0, 0, 0}, {9, 0, 0, 1}, {8, 0, 0, 3}, {14, 0, 0, 4}, {4, 0, 0, 4}, {5, 0, 0, 6}, {0, 0, 0, 7},
        {2, 0, 0, 8}, {1, 0, 0, 9}, {12, 0, 0, 10}, {15, 0, 0, 11}, {7, 0, 0, 14}, {10, 0, 0, 12}, {11, 0, 0, 12},
        {13, 0, 0, 15}, {21, 14, 14, 1}, {19, 14, 14, 2}, {23, 14, 14, 3}, {18, 14, 14, 4}, {16, 1, 1, 10},
        {17, 9, 13, 5}, {100, 4, 4, 4}, {101, 5, 5, 4}, {102, 6, 6, 4}, {103, 7, 7, 4}, {104, 8, 8, 4}};
    bool any_stage2 = false;
    for (const auto &pad : out.pads) any_stage2 = any_stage2 || pad.has_stage2;
    for (const auto &row : rows)
        for (const auto &pad : out.pads) {
            if (pad.physical != row.physical || !pad.occupied) continue;
            unsigned mask = 0;
            long long supply = 0;
            for (int i = row.first; i <= row.last; ++i)
                if (out.items[i].base) { mask |= 1u << i; supply += out.items[i].base; }
            const long long want = std::min<long long>(row.count, supply);
            need(want ? pad.has_rule && pad.rule_mask == mask && pad.rule_count == want : !pad.has_rule,
                 "vanilla stage 1 requirements do not match the physical pads");
            const int keys = pad.physical >= 100 ? 3 : (pad.physical == 16 || pad.physical == 17) ? 2 : pad.keys + 1;
            if (pad.mode_count > 1 && any_stage2)
                need(pad.has_stage2 && pad.stage2_mask == (1u << 14) && pad.stage2_count == std::min<long long>(keys, out.items[14].base),
                     "vanilla stage 2 requirements do not match the physical pads");
        }
}

// The four ordinary garages' frozen Boss Race checks and opponents, then N.
// Oxide's two challenges (apworld content_bosses.GARAGES and EVENTS).
static const int64_t kBossCodes[6] = {35011100, 35011101, 35011102, 35011103, 35011104, 35011105};
static const char *const kBossNames[6] = {"Ripper Roo Garage: Boss Race", "Papu Papu Garage: Boss Race",
                                          "Komodo Joe Garage: Boss Race", "Pinstripe Garage: Boss Race",
                                          "N. Oxide Garage: N. Oxide's Challenge",
                                          "N. Oxide Garage: N. Oxide's Final Challenge"};
static const int kBossOpponents[6] = {10, 9, 11, 8, 15, 15};
static const int kOxideStationLevel = 13; // N. Oxide's Challenge, and the Final Challenge's other venue
static const int kCortexVenue = -2;       // the Final Challenge on the bundled Cortex Vortex pair
static const int kCortexRestartPoints = 255; // the bundled Cortex Vortex LEV (#133 PR 6b), re-measured on load
static J all_of(const J &terms) { return terms.size() == 1 ? terms[0] : J{{"op", "all"}, {"terms", terms}}; }
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
    const bool has_racers = p.is_object() && p.contains("racers");
    const bool has_capability = p.is_object() && p.contains("capability");
    const bool has_global = p.is_object() && p.contains("global_checks");
    const bool has_boxes = p.is_object() && p.contains("item_boxes");
    const bool has_bosses = p.is_object() && p.contains("bosses"); // #133 PR 6
    // One generator row 3: the pad requirement mode. Declares
    // `pad_requirements_v1`: drawn and vanilla stage 1 records and, on every
    // pad with several races, the first race won rule.
    const bool has_pad_requirements = p.is_object() && p.contains("pad_requirements");
    // One generator row 4: items fixed to their own check. Declares
    // `fixed_items_v1`. The ledger's locked fields are recomputed from it.
    const bool has_fixed = p.is_object() && p.contains("fixed_items");
    need(p.is_object() && p.size() == 11u + (has_families ? 1u : 0u) + (has_packs ? 1u : 0u) + (has_racers ? 1u : 0u) +
                                         (has_capability ? 1u : 0u) + (has_global ? 1u : 0u) + (has_boxes ? 1u : 0u) +
                                         (has_bosses ? 1u : 0u) + (has_pad_requirements ? 1u : 0u) + (has_fixed ? 1u : 0u),
         "unsupported or incomplete record fields");
    for (const char *name : {"version", "required_features", "packages", "tracks", "entries", "pads", "encounters", "requirements", "checks", "items", "goal"})
        need(p.contains(name), std::string("missing field: ") + name);
    need(p.at("version") == 1, "unsupported required content-plan version");
    ctr_content_checks &fam = out.families;
    fam = ctr_content_checks{};
    if (has_families) family_settings(p["track_checks"], fam);
    const std::map<int64_t, int> packs = has_packs ? item_packs(p["item_packs"]) : std::map<int64_t, int>{};
    // Character unlocks (#133 PR 4b): one ledger row per unlock racer.
    std::map<int64_t, int> racer_rows;
    if (has_racers) {
        racer_settings(p["racers"], out);
        for (int slot = 0; slot < 16; ++slot)
            if (!racer_excluded(out, kRosterCharacter[slot])) racer_rows[kRacerItemBase + slot] = 1;
    }
    // Capability packs (#133 PR 4c): the boost and stat chain rows.
    if (has_capability) capability_settings(p["capability"], out);
    const std::map<int64_t, int> cap_rows = has_capability ? capability_rows(out.capability) : std::map<int64_t, int>{};
    // Itemsanity and Hit Character (#133 PR 4d): one row per weapon item.
    if (has_global) global_settings(p["global_checks"], out);
    // Item Box Locations (#133 PR 4e): one Shortcut Knowledge tier per plan.
    if (has_boxes) {
        item_box_settings(p["item_boxes"], out);
        need(!out.capability.present || out.capability.shortcut_knowledge == out.item_boxes.shortcut_knowledge,
             "item_boxes.shortcut_knowledge disagrees with capability.shortcut_knowledge");
    }
    std::map<int64_t, int> weapon_rows;
    for (int64_t w = 0; out.itemsanity && w < kWeaponCount; ++w) weapon_rows[kWeaponFirst + w] = 1;
    // Boss garages (#133 PR 6): encounters exist exactly with the `bosses`
    // record; they, their entries, requirements and checks are recomputed below.
    out.bosses = ctr_content_bosses{};
    out.bosses.oxide_goal = 3; out.bosses.final_track = -1;
    for (int g = 0; g < 4; ++g) {
        out.bosses.venues[g] = -1;
        out.bosses.venue_package[g] = -1; out.bosses.venue_cortex[g] = 0; out.bosses.venue_points[g] = 0;
    }
    if (has_bosses) boss_settings(p["bosses"], out.bosses);
    need(p.at("encounters").is_array() && p.at("encounters").empty() == !has_bosses,
         "encounters without the bosses record, or the reverse");
    static const J registry = J::parse(kContentRegistryJSON);
    const auto &named_table = named_block_table(registry);
    // Names a generic-slot (B+) room title must not take: every retail track
    // and every named block, whether or not it is in this seed.
    std::set<std::string> reserved_titles;
    for (const auto &row : registry["retail"]) reserved_titles.insert(fold(row["name"].get<std::string>()));
    for (const auto &row : named_table) reserved_titles.insert(fold(row.second.name));
    reserved_titles.insert(fold(registry["cortex_vortex"]["name"].get<std::string>()));
    const auto packages = table(p.at("packages"), AP_CONTENT_MAX_PACKAGES);
    // Cups (#133 PR 5) add tracks raced only as legs: at most every retail
    // race track, every package and Cortex Vortex.
    const auto tracks = table(p.at("tracks"), 64);
    const auto entries = table(p.at("entries"), 33); // 27 pads and six boss encounters
    const auto pads = table(p.at("pads"), 32);
    const auto requirements = table(p.at("requirements"), 70); // 5 hub, 27 manual or stage-1, 27 stage-2, 6 boss
    enum { PAD_REQ_VANILLA, PAD_REQ_RANDOM, PAD_REQ_NO_FOUR_KEYS };
    int pad_mode = PAD_REQ_RANDOM;
    if (has_pad_requirements) {
        const J &record = p["pad_requirements"];
        need(record.is_object(), "pad_requirements must be a mapping");
        fields(record, {"mode"});
        pad_mode = record["mode"] == "vanilla" ? PAD_REQ_VANILLA : record["mode"] == "randomized" ? PAD_REQ_RANDOM :
                   record["mode"] == "random_without_4_keys" ? PAD_REQ_NO_FOUR_KEYS : -1;
        need(pad_mode >= 0, "pad_requirements.mode is unknown");
    }
    bool fixed_keys = false, fixed_gems = false, fixed_filler = false;
    if (has_fixed) {
        const J &record = p["fixed_items"];
        need(record.is_object() && !record.empty(), "fixed_items must be a nonempty mapping");
        for (const auto &item : record.items())
            need((item.key() == "keys" || item.key() == "gems" || item.key() == "oxide_1_filler") && item.value() == true,
                 "fixed_items holds keys, gems and oxide_1_filler, true when present");
        fixed_keys = record.contains("keys"); fixed_gems = record.contains("gems"); fixed_filler = record.contains("oxide_1_filler");
    }
    need(pads.size() == 32 && requirements.size() >= 5, "physical pad or requirement registry mismatch");
    // Manual pad requirements (#463): every record beyond the five physical hub
    // ones must be `manual:<physical>`, is checked against the item ledger
    // below, and is the only gate its own pad may reference. The feature name is
    // required exactly when such a record exists, so a client without this
    // evaluator never reads a Key-only plan by mistake.
    // Second-stage records (#133 PR 3) are `stage2:<physical>`: the shared
    // gate of a pad's later modes, parsed and checked exactly like a manual rule.
    // Drawn and vanilla stage 1 records (one generator row 3) are
    // `stage1:<physical>`, the same shape as a manual rule; a pad holds one or
    // the other, so gate_ids is the pad's one stage 1 record.
    std::map<int, std::string> manual_ids, stage1_ids, stage2_ids;
    size_t boss_requirements = 0; // `boss:<g>`, `oxide:first`, `oxide:final` (#133 PR 6), checked below
    for (const auto &pair : requirements) {
        if (pair.first == "free" || pair.first.rfind("keys:", 0) == 0) continue;
        if (pair.first == "boss:0" || pair.first == "boss:1" || pair.first == "boss:2" || pair.first == "boss:3" ||
            pair.first == "oxide:first" || pair.first == "oxide:final") { ++boss_requirements; continue; }
        const bool later = pair.first.rfind("stage2:", 0) == 0;
        const bool drawn = pair.first.rfind("stage1:", 0) == 0;
        const size_t prefix = 7; // "manual:", "stage1:" and "stage2:" have the same length
        need((later || drawn || pair.first.rfind("manual:", 0) == 0) && pair.first.size() > prefix && pair.first.size() <= prefix + 3, "unknown requirement record: " + pair.first);
        const std::string digits = pair.first.substr(prefix);
        need(std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) && (digits.size() == 1 || digits[0] != '0'), "malformed pad requirement id");
        need((later ? stage2_ids : drawn ? stage1_ids : manual_ids).emplace(std::stoi(digits), pair.first).second, "duplicate pad requirement");
    }
    need(requirements.size() == 5 + manual_ids.size() + stage1_ids.size() + stage2_ids.size() + boss_requirements, "requirement registry mismatch");
    need(has_pad_requirements || stage1_ids.empty(), "a stage 1 record needs the pad_requirements record");
    std::map<int, std::string> gate_ids = stage1_ids;
    for (const auto &pair : manual_ids) need(gate_ids.insert(pair).second, "a manual rule and a drawn requirement on one pad");
    bool arenas=false, arcade=false, pickups=false, modes=false, cups=false, gem_cups=false;
    int relic_races = 0;
    for (const auto &pair : entries) if(pair.second->at("kind")=="crystal") {
        arenas=true; arcade |= AP_ArcadeArenaIndex(number(pair.second->at("arena_id"),18,24))>=0;
    } else if (pair.second->at("kind") == "cup") { cups = true; gem_cups |= pair.first.rfind("entry/gem_cup:", 0) == 0; }
    else if (pair.second->at("kind") != "boss_race" && pair.second->at("kind") != "boss_cup") {
        modes |= pair.second->at("modes") != J::array({"trophy"});
        const J &entry_modes = pair.second->at("modes");
        relic_races += entry_modes.is_array() && std::find(entry_modes.begin(), entry_modes.end(), J("relic")) != entry_modes.end();
    }
    // The relic guard (one generator row 4): with vanilla requirements and Gems
    // fixed, a Relic Race on the physical five-Gem pad holds no tier check. The
    // item counts stay exact, so a tier's checks follow the eligible races.
    std::string guarded_entry;
    if (pad_mode == PAD_REQ_VANILLA && fixed_gems && pads.count("pad:17")) {
        const J &entry_id = pads.at("pad:17")->at("entry_id");
        if (entry_id.is_string() && entries.count(entry_id.get<std::string>())) {
            const J &entry = *entries.at(entry_id.get<std::string>());
            if (entry.at("kind") == "track" && entry.at("modes").is_array() &&
                std::find(entry["modes"].begin(), entry["modes"].end(), J("relic")) != entry["modes"].end())
                guarded_entry = entry_id.get<std::string>();
        }
    }
    const int eligible_races = relic_races - (guarded_entry.empty() ? 0 : 1);
    // Item ledger facts the feature list needs before the ledger is read: the
    // five surface items (one generator row 2) and each relic tier's copies.
    bool surface_rows = false, relic_cut = false;
    int64_t relic_supply[3] = {0, 0, 0};
    {
        const J &rows = p.at("items").at("rows");
        need(rows.is_array(), "item ledger rows");
        for (const auto &row : rows) {
            const int64_t code = number(row.at("item"), 1);
            surface_rows |= code >= kSurfaceFirst && code <= kSurfaceLast;
            for (int t = 0; t < 3; ++t)
                if (code == kRelicItems[t]) {
                    relic_supply[t] = (int64_t)number(row.at("base")) + number(row.at("extra"));
                    relic_cut |= std::min<int64_t>(relic_supply[t], eligible_races) < relic_races;
                }
        }
    }
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
    if(has_racers)features.push_back("character_unlocks_v1");
    if(has_capability)features.push_back("capability_packs_v1");
    if(has_global)features.push_back("global_checks_v1");
    if(has_boxes)features.push_back("item_boxes_v1"); // #133 PR 4e
    if(cups)features.push_back("cup_entries_v1");      // #133 PR 5
    if(has_bosses)features.push_back("boss_encounters_v1"); // #133 PR 6
    if(gem_cups)features.push_back("gem_cups_v1");          // one generator row 2
    if(surface_rows)features.push_back("surface_items_v1");
    if(relic_cut)features.push_back("relic_tier_checks_v1");
    if(has_pad_requirements)features.push_back("pad_requirements_v1"); // one generator row 3
    if(has_fixed)features.push_back("fixed_items_v1");                 // one generator row 4
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
    // Every Trophy Race and CTR Challenge (#133 PR 4d), an Itemsanity and Hit
    // route each. Since #133 PR 5b native seats the Hit Character draw on
    // every such race, custom-served included, so a guest routes from all.
    struct RaceRoute { int physical; std::string entry, pad; unsigned modes; };
    std::vector<RaceRoute> race_routes;
    // Every leg of every cup (#133 PR 5b): a Hit route each, the cup's frozen
    // field seated in every leg. A leg on a crateless custom track is none.
    struct CupHitRoute { int physical; std::string entry, pad, leg; };
    std::vector<CupHitRoute> cup_hit_routes;
    std::map<int, ctr_content_pad *> manual_pad; // occupied pad per manual or stage 1 record
    std::map<int, ctr_content_pad *> stage2_pad; // pad whose later modes use each stage-2 record
    bool starting_entry = false;
    int relic_checks[3] = {0, 0, 0}; // tier checks in the plan, per tier
    // Cups (#133 PR 5). A leg names a plan track; a track raced only in cups
    // is checked here like a standalone one (registry metadata, package,
    // named block, room title) and must list the trophy mode only. A leg track
    // without a standalone Trophy Race owns a `Cup Leg Victory` check with a
    // route per occurrence. Sweeps exist for every cup or none.
    bool sweeps = false;
    for (const auto &check : p["checks"])
        sweeps |= check.is_object() && check.contains("kind") && check["kind"] == "cup_sweep";
    std::set<std::string> trophy_tracks;
    for (const auto &pair : entries)
        if (pair.second->at("kind") == "track" && pair.second->at("modes").is_array())
            for (const auto &m : pair.second->at("modes"))
                if (m == "trophy") trophy_tracks.insert(text(pair.second->at("track_id")));
    struct LegTrack { ctr_content_leg leg; std::string title; int full_laps; };
    std::map<std::string, LegTrack> leg_tracks;
    auto leg_track = [&](const std::string &track_id) -> const LegTrack & {
        const auto known = leg_tracks.find(track_id);
        if (known != leg_tracks.end()) return known->second;
        need(tracks.count(track_id), "a cup leg names a track the plan does not list");
        const J &track = *tracks.at(track_id);
        LegTrack lt{};
        ctr_content_leg &leg = lt.leg;
        leg.retail_id = -1; leg.package_index = -1; leg.named_block = -1;
        const auto title = text(track.at("display_name"), false);
        lt.title = title;
        if (track.at("origin") == "retail") {
            fields(track, {"id", "origin", "retail_id", "display_name", "laps", "modes", "capabilities", "field_size"});
            const int rid = number(track["retail_id"], 0, 17);
            need(rid < 16 && track_id == "retail:" + std::to_string(rid) && title == registry["retail"][rid]["name"] &&
                 track["laps"] == AP_CONTENT_RETAIL_LAPS, "a retail cup leg is not a race track or its metadata differs");
            leg.retail_id = rid;
            leg.victory = registry["retail"][rid]["cup_leg"].get<int64_t>();
        } else if (track.at("origin") == "cortex_vortex") {
            fields(track, {"id", "origin", "package_id", "display_name", "laps", "modes", "capabilities", "field_size"});
            const J &row = registry["cortex_vortex"];
            need(track_id == row["id"].get<std::string>() && track["package_id"] == row["package_id"] &&
                 title == row["name"].get<std::string>() && track["laps"] == row["laps"], "Cortex Vortex metadata mismatch");
            leg.cortex = 1;
            leg.victory = row["cup_leg"].get<int64_t>();
        } else {
            J base = track;
            const bool named = base.is_object() && base.contains("named_block");
            const bool titled = base.is_object() && base.contains("room_title");
            if (named) base.erase("named_block");
            if (titled) base.erase("room_title");
            fields(base, {"id", "origin", "package_id", "custom_slot", "display_name", "laps", "modes", "capabilities", "field_size"});
            const auto package_id = text(track["package_id"]);
            need(track["origin"] == "custom" && packages.count(package_id), "unsupported custom origin or package");
            const J &pkg = *packages.at(package_id);
            const ctr_content_package &record = out.packages[package_index.at(package_id)];
            need(track_id == "custom/" + pkg["content_id"].get<std::string>() + "/" + pkg["revision"].get<std::string>() &&
                 title == pkg["display_name"] && number(track["laps"], 1, 7) == record.laps, "custom metadata mismatch");
            // A custom leg races a Trophy-style race: eight karts with AI paths.
            need(record.capabilities & AP_CONTENT_CAP_ARCADE, "a custom cup leg needs the arcade capability");
            leg.package_index = package_index.at(package_id);
            leg.custom_slot = number(track["custom_slot"], 1, AP_CONTENT_MAX_PACKAGES);
            if (titled) {
                lt.title = text(track["room_title"], false);
                need(ap_room_title_ok(lt.title.c_str(), lt.title.size()), "invalid room title");
            }
            const auto owned = named_table.find(record.content_id);
            if (named) {
                leg.named_block = number(track["named_block"], 0, AP_NAMED_BLOCK_LIMIT - 1);
                need(owned != named_table.end() && leg.named_block == owned->second.block &&
                     titled && lt.title == owned->second.name, "named block does not match this build's registry");
                leg.victory = ap_named_location_code(leg.named_block, AP_NAMED_OFF_CUP_LEG_VICTORY);
            } else {
                need(owned == named_table.end(), "a track with a named block must use it");
                need(!reserved_titles.count(fold(lt.title)), "generic-slot room title takes a retail or named track's name");
                leg.victory = registry["custom_cup_leg_locations"][leg.custom_slot - 1].get<int64_t>();
            }
        }
        lt.full_laps = number(track["laps"], 1, 7);
        if (trophy_tracks.count(track_id)) leg.victory = 0;
        copy(leg.track_id, J(track_id));
        return leg_tracks.emplace(track_id, lt).first->second;
    };
    int shortened = -1; // the plan-wide shortened-cup switch, -1 before a leg tells
    std::set<int> cup_names, gem_colours;
    struct LegUse { std::string track, entry, pad, leg; };
    std::vector<LegUse> leg_uses; // in pad order; re-sorted to entry order below
    for (int pi = 0; pi < 32; ++pi) {
        const bool garage = pi >= 27;
        const int physical = garage ? pi - 27 : registry["pads"][pi]["physical"].get<int>();
        const int hub = garage ? (physical < 4 ? 26 + physical : 25) : registry["pads"][pi]["hub"].get<int>();
        // A garage holding an encounter takes its hub's Key gate (#133 PR 6):
        // keys:<g> for the four boss garages, keys:4 for the Oxide garage.
        const bool garage_used = garage && out.bosses.present && (physical < 4 || out.bosses.oxide_goal != 3);
        const int keys = garage ? (garage_used ? (physical < 4 ? physical : 4) : 0) : registry["pads"][pi]["keys"].get<int>();
        const auto id = std::string(garage ? "garage:" : "pad:") + std::to_string(physical);
        need(pads.count(id), "missing canonical physical pad: " + id);
        const J &pad = *pads.at(id);
        fields(pad, {"id", "hub_id", "kind", "entry_id", "entrance_req", "mode_gates", "racer_lock"});
        need(pad["hub_id"] == "hub:" + std::to_string(hub) && pad["kind"] == (garage ? "garage" : "normal") &&
             pad["entrance_req"] == (keys ? "keys:" + std::to_string(keys) : "free"), "pad hub, gate or kind mismatch");
        ctr_content_pad scratch{};
        auto &dest = garage ? scratch : out.pads[pi];
        dest.physical = physical; dest.hub = hub; dest.keys = keys;
        dest.retail_id = -1; dest.package_index = -1; dest.named_block = -1;
        // Racer lock (#133 PR 4b): an engine character id, never the starting
        // racer or a removed Oxide, and only with the `racers` record. Whether
        // the pad may carry one is decided once the item ledger is read.
        dest.racer_lock = -1;
        if (!pad["racer_lock"].is_null()) {
            need(has_racers && !garage, "a racer lock needs Character Unlocks (the racers record)");
            dest.racer_lock = number(pad["racer_lock"], 0, 15);
            need(!racer_excluded(out, dest.racer_lock), "a racer lock names the starting racer, a removed Oxide or no racer");
        }
        if (pad["entry_id"].is_null()) {
            need(pad["mode_gates"] == J::array(), "empty pad has mode gates");
            need(dest.racer_lock < 0, "a racer lock on a pad that opens at the start or holds no entry");
            continue;
        }
        const auto entry_id = text(pad["entry_id"]);
        need(!garage && entries.count(entry_id) && used_entries.insert(entry_id).second, "invalid or repeated pad entry");
        const J &entry = *entries.at(entry_id);
        const bool crystal=entry.at("kind")=="crystal";
        if (entry.at("kind") == "cup") {
            // Cups (#133 PR 5): `entry/cup:<name index>`, the pool name, four
            // legs `<entry>/leg:<n>`, the player-only roster, eight karts. The
            // first five cups are the Gem Cups, `entry/gem_cup:<colour>`, with
            // their frozen Gem and sweep checks (one generator row 2).
            fields(entry, {"id", "kind", "display_name", "legs", "roster", "field_size"});
            const std::string first = gate_ids.count(physical) ? gate_ids.at(physical) : std::string("free");
            need(pad["mode_gates"] == J::array({J{{"mode", "cup"}, {"stage", 1}, {"requirement_id", first}}}), "unsupported mode gates");
            if (gate_ids.count(physical)) manual_pad[physical] = &dest;
            const bool gem = entry_id.rfind("entry/gem_cup:", 0) == 0;
            const std::string prefix = gem ? "entry/gem_cup:" : "entry/cup:";
            need(entry_id.rfind(prefix, 0) == 0 && entry_id.size() > prefix.size() && entry_id.size() <= prefix.size() + 2, "cup entry id is not entry/cup:<name index> or entry/gem_cup:<colour>");
            const std::string digits = entry_id.substr(prefix.size());
            need(std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) &&
                 (digits.size() == 1 || digits[0] != '0'), "cup entry id is not entry/cup:<name index> or entry/gem_cup:<colour>");
            const int index = std::stoi(digits);
            const J &names = registry[gem ? "gem_cups" : "cups"];
            need(index < (int)names.size() && (gem ? gem_colours : cup_names).insert(index).second, "unknown or repeated cup name");
            const std::string name = names[index]["name"];
            need(entry["display_name"] == name, "cup display name is not its Gem Cup or pool name");
            need(entry["roster"] == J::array({J{{"role", "player"}, {"slot", 0}}}) && entry["field_size"] == 8,
                 "cup roster or field size is not the Adventure field");
            const J &legs = entry["legs"];
            need(legs.is_array() && legs.size() == AP_CONTENT_CUP_LEGS, "a cup has exactly four legs");
            for (int n = 0; n < AP_CONTENT_CUP_LEGS; ++n) {
                const J &leg = legs[n];
                fields(leg, {"id", "track_id", "laps"});
                const std::string leg_id = entry_id + "/leg:" + std::to_string(n);
                need(leg["id"] == leg_id, "cup leg id is not <entry>/leg:<n>");
                const LegTrack &info = leg_track(text(leg["track_id"]));
                dest.legs[n] = info.leg;
                copy(dest.legs[n].leg_id, J(leg_id));
                // Laps: the track's own, or max(1, floor(laps / 2)) with
                // one_lap_cups, the same switch in every leg.
                const int full = info.full_laps, short_laps = full / 2 > 1 ? full / 2 : 1;
                const int laps = number(leg["laps"], 1, 7);
                need(laps == full || laps == short_laps, "a cup leg's laps are not its track's laps or the shortened-cup laps");
                if (full != short_laps) {
                    const int now = laps == short_laps ? 1 : 0;
                    need(shortened < 0 || shortened == now, "cup legs shorten their laps in some legs and not in others");
                    shortened = now;
                }
                dest.legs[n].laps = laps;
                if (dest.legs[n].victory) leg_uses.push_back(LegUse{dest.legs[n].track_id, entry_id, id, leg_id});
                if (dest.legs[n].package_index < 0 || out.packages[dest.legs[n].package_index].weapon_crates != 0)
                    cup_hit_routes.push_back(CupHitRoute{physical, entry_id, id, leg_id});
            }
            dest.occupied = dest.cup = 1;
            dest.cup_index = gem ? -1 : index;
            dest.gem_cup = gem ? 100 + index : 0;
            dest.laps = dest.legs[0].laps;
            dest.trophy_location = names[index]["win"].get<int64_t>();
            dest.sweep_location = sweeps ? names[index]["sweep"].get<int64_t>() : 0;
            const std::string owner = (gem ? "gem_cup:" : "cup:") + std::to_string(index);
            copy(dest.entry_id, entry["id"]); copy(dest.track_id, J(owner));
            copy(dest.display_name, J(name)); copy(dest.room_title, J(name));
            need(expected_checks.emplace(dest.trophy_location, J{{"location", dest.trophy_location}, {"owner", owner}, {"kind", "cup"},
                                                                 {"display_name", name + (gem ? ": Gem" : ": Win")},
                                                                 {"routes", J::array({route_record(entry_id, "cup", id, "win")})}}).second,
                 "duplicate check identity");
            if (sweeps)
                need(expected_checks.emplace(dest.sweep_location, J{{"location", dest.sweep_location}, {"owner", owner}, {"kind", "cup_sweep"},
                                                                    {"display_name", name + ": Win All Legs"},
                                                                    {"routes", J::array({route_record(entry_id, "cup", id, "win_all_legs")})}}).second,
                     "duplicate check identity");
            if (!keys) starting_entry = true;
            continue;
        }
        const std::string first_gate = gate_ids.count(physical) ? gate_ids.at(physical) : std::string("free");
        if (crystal)
            need(pad["mode_gates"] == J::array({J{{"mode", "crystal"}, {"stage", 1}, {"requirement_id", first_gate}}}), "unsupported mode gates");
        if (gate_ids.count(physical)) manual_pad[physical] = &dest;
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
        dest.first_race_rule = has_pad_requirements;
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
        // A tier's check exists where the plan lists it (one generator row 2):
        // the list is the seed's draw, its size is checked against the ledger.
        const bool relic_race = (mode_bits & (1u << AP_CONTENT_MODE_RELIC)) != 0;
        dest.relic_route = relic_race ? codes[AP_CONTENT_MODE_RELIC][0] : 0;
        for (int t = 0; t < 3; ++t) {
            dest.relic_locations[t] = relic_race && plan_codes.count(codes[AP_CONTENT_MODE_RELIC][t]) ? codes[AP_CONTENT_MODE_RELIC][t] : 0;
            relic_checks[t] += dest.relic_locations[t] != 0;
            need(!dest.relic_locations[t] || entry["id"] != guarded_entry, "the Relic Race on the guarded five-Gem pad holds a tier check");
        }
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
                if (mode == AP_CONTENT_MODE_RELIC && !dest.relic_locations[t]) continue;
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
        // A custom track whose package states no weapon crates rolls no weapon,
        // so it is no Itemsanity or Hit route (#133 PR 4e).
        const bool crateless = dest.package_index >= 0 && out.packages[dest.package_index].weapon_crates == 0;
        if (reach && !crateless) race_routes.push_back(RaceRoute{physical, entry_id, id, reach});
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
        // Item Box Locations (#133 PR 4e), after the track's other checks: a
        // retail track's frozen slots its tier creates (registry `boxes`, one
        // [code, tier] per placed slot), a named-block custom track's Item
        // Box 1 to min(weapon_crates, 15); a route per mode in the pad's order.
        if (out.item_boxes.present) {
            int slots = 0;
            if (dest.retail_id >= 0) {
                const J &rows = registry["retail"][dest.retail_id]["boxes"];
                need(rows.is_array() && rows.size() <= AP_NAMED_ITEM_BOX_SLOTS, "registry box rows exceed the slot ceiling");
                for (size_t b = 0; b < rows.size(); ++b)
                    if (rows[b][1].get<int>() <= out.item_boxes.shortcut_knowledge) {
                        dest.box_locations[b] = rows[b][0].get<int64_t>();
                        slots = (int)b + 1;
                    }
            } else if (dest.named_block >= 0 && dest.package_index >= 0) {
                slots = box_seats(out.packages[dest.package_index].weapon_crates);
                dest.box_crates = slots ? out.packages[dest.package_index].weapon_crates : 0;
                for (int b = 0; b < slots; ++b)
                    dest.box_locations[b] = ap_named_location_code(dest.named_block, AP_NAMED_OFF_ITEM_BOX_1 + b);
            }
            dest.box_count = slots;
            for (int b = 0; b < slots; ++b) {
                if (!dest.box_locations[b]) continue;
                J routes = J::array();
                for (int k = 0; k < dest.mode_count; ++k) routes.push_back(route_record(entry_id, mode_names[dest.modes[k]], id, "box"));
                family_check(dest.box_locations[b], "box", "Item Box " + std::to_string(b + 1), routes);
            }
        }
        if (!keys) starting_entry = true;
    }
    // Cups (#133 PR 5): tracks raced only as legs take their identities here,
    // once, and list the trophy mode only.
    for (const auto &pair : leg_tracks) {
        if (used_tracks.count(pair.first)) continue;
        const J &track = *tracks.at(pair.first);
        const ctr_content_leg &leg = pair.second.leg;
        need(track.at("modes") == J::array({"trophy"}) && track.at("capabilities") == J::array({"trophy"}) &&
             track.at("field_size") == 8, "a track raced only in cups lists the trophy mode only");
        if (leg.retail_id >= 0) need(retail_ids.insert(leg.retail_id).second, "unknown or duplicate retail track");
        // A Cortex Vortex raced only in cups is still scanned and armed.
        else if (leg.cortex) { need(!cortex_seen, "Cortex Vortex metadata mismatch"); cortex_seen = true; out.cortex = true; }
        else {
            need(custom_slots.insert(leg.custom_slot).second, "custom metadata mismatch");
            if (leg.named_block >= 0) need(named_blocks.insert(leg.named_block).second, "duplicate named block");
            used_packages.insert(out.packages[leg.package_index].id);
        }
        used_tracks.insert(pair.first);
    }
    for (const auto &pair : leg_tracks)
        if (pair.second.leg.package_index >= 0) used_packages.insert(out.packages[pair.second.leg.package_index].id);
    // The first five cups are the Gem Cups, Red first (one generator row 2).
    need((gem_colours.empty() || *gem_colours.rbegin() == (int)gem_colours.size() - 1) &&
         (cup_names.empty() || gem_colours.size() == 5), "a plan's first five cups are the Gem Cups, Red first");
    // `Cup Leg Victory`: a route per occurrence, in the plan's entry order,
    // then leg order (apworld content_cups.leg_victories).
    {
        std::map<std::string, size_t> entry_order;
        for (size_t i = 0; i < p["entries"].size(); ++i) entry_order[p["entries"][i].at("id").get<std::string>()] = i;
        std::stable_sort(leg_uses.begin(), leg_uses.end(), [&](const LegUse &a, const LegUse &b) {
            return entry_order.at(a.entry) < entry_order.at(b.entry); });
        std::vector<std::string> order;
        std::map<std::string, J> victories;
        for (const auto &use : leg_uses) {
            const LegTrack &info = leg_tracks.at(use.track);
            if (!victories.count(use.track)) {
                order.push_back(use.track);
                victories[use.track] = J{{"location", info.leg.victory}, {"owner", use.track}, {"kind", "cup_leg"},
                                         {"display_name", info.title + ": Cup Leg Victory"}, {"routes", J::array()}};
            }
            victories[use.track]["routes"].push_back(J{{"entry_id", use.entry}, {"mode", "cup"}, {"leg_id", use.leg},
                                                       {"encounter_id", nullptr}, {"own_pad_id", use.pad}, {"award", "leg_win"}});
        }
        for (const auto &track : order)
            need(expected_checks.emplace(victories[track]["location"].get<int64_t>(), victories[track]).second, "duplicate check identity");
    }
    // Boss garages (#133 PR 6), apworld content_bosses.build: one encounter
    // per ordinary garage, then N. Oxide's two unless Oxide is disabled, each
    // naming a `boss_race` entry (venue, three laps, the garage's own boss with
    // the retail driving profile and the inherited kit), a requirement and its
    // frozen completion check. A venue raced nowhere else is a trophy-only
    // track record. The requirement predicates need the item ledger and are
    // checked after it.
    std::map<int, std::vector<J>> boss_hits; // opponent engine id -> Hit routes
    J boss_requirement_ids = J::array();     // per encounter, checked after the ledger
    if (out.bosses.present) {
        const J &encs = p["encounters"];
        const size_t count = out.bosses.oxide_goal == 3 ? 4u : 6u;
        need(encs.is_array() && encs.size() == count, "encounter count does not match the bosses record");
        std::set<int> drawn;
        for (size_t k = 0; k < count; ++k) {
            const int g = k < 4 ? (int)k : 4, index = k < 5 ? 0 : 1;
            const J &enc = encs[k];
            fields(enc, {"id", "pad_id", "index", "entry_id", "requirement_id", "completion_location"});
            const std::string eid = "encounter/garage:" + std::to_string(g) + "/" + std::to_string(index);
            const std::string nid = "entry/boss:" + std::to_string(g) + "/" + std::to_string(index);
            const std::string pad = "garage:" + std::to_string(g);
            need(enc["id"] == eid && enc["pad_id"] == pad && enc["index"] == index && enc["entry_id"] == nid &&
                 enc["completion_location"] == kBossCodes[k], "encounter record does not match its garage");
            need(entries.count(nid) && used_entries.insert(nid).second, "an encounter names no boss race entry");
            const J &entry = *entries.at(nid);
            // Boss cups (#133 PR 6b): four legs against the garage's own boss
            // alone, boss AI on every leg; a points win completes the garage.
            // Each leg is a certified venue for this garage (as a Boss Track
            // Shuffle venue), the four are different, three laps each. The
            // garage's cup pad (ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD
            // + g)) carries the legs into the content-cup machinery.
            const bool cup_garage = g < 4 && out.bosses.cup[g];
            need(g < 4 || entry["kind"] != "boss_cup", "N. Oxide's encounters hold no boss cup");
            need(cup_garage == (entry["kind"] == "boss_cup"), "boss cup garages do not match the bosses record");
            if (cup_garage) {
                fields(entry, {"id", "kind", "legs", "opponent"});
                need(entry["opponent"] == J{{"role", "opponent"}, {"slot", 1}, {"character_id", kBossOpponents[k]},
                                           {"driving_profile", "retail"}, {"attack_profile", "inherited"}},
                     "boss cup entry does not match its garage's retail opponent");
                const J &legs = entry["legs"];
                need(legs.is_array() && legs.size() == AP_CONTENT_CUP_LEGS, "a boss cup races four legs");
                ctr_content_pad &cp = out.garage_cups[g];
                cp = ctr_content_pad{};
                cp.physical = AP_CONTENT_GARAGE_CUP_PAD + g; cp.hub = 26 + g; cp.occupied = 1; cp.cup = 1;
                cp.cup_index = -1; cp.retail_id = -1; cp.package_index = -1; cp.named_block = -1; cp.racer_lock = -1;
                cp.trophy_location = kBossCodes[k];
                copy(cp.entry_id, J(nid)); copy(cp.track_id, J(eid));
                copy(cp.display_name, J(std::string(kBossNames[k]).substr(0, std::string(kBossNames[k]).find(':'))));
                std::set<std::string> seen_legs;
                for (size_t n = 0; n < AP_CONTENT_CUP_LEGS; ++n) {
                    const J &leg = legs[n];
                    fields(leg, {"id", "track_id", "laps"});
                    const std::string lid = nid + "/leg:" + std::to_string(n);
                    const std::string ltrack = text(leg["track_id"]);
                    need(leg["id"] == lid && leg["laps"] == AP_CONTENT_RETAIL_LAPS && seen_legs.insert(ltrack).second,
                         "a boss cup leg is not its garage's leg, three laps, or repeats a track");
                    int points = -1;
                    if (ltrack == "cortex_vortex") points = kCortexRestartPoints;
                    else if (ltrack.rfind("custom/", 0) == 0) {
                        need(tracks.count(ltrack) && used_tracks.count(ltrack), "a custom boss cup leg the plan does not race");
                        const ctr_content_package &pkg = out.packages[package_index.at(text(tracks.at(ltrack)->at("package_id")))];
                        need(pkg.ai_nav == 1, "a custom boss cup leg without AI navigation");
                        points = pkg.checkpoints;
                    } else if (ltrack.rfind("retail:", 0) == 0 && ltrack.size() <= 9) {
                        const std::string digits = ltrack.substr(7);
                        need(!digits.empty() && std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) &&
                             (digits.size() == 1 || digits[0] != '0'), "unknown boss venue");
                        const int lv = std::stoi(digits);
                        need(lv < 18 && AP_BossVenueTrackKnown(lv), "unknown boss venue");
                        points = AP_BOSS_VENUE_RESTART_POINTS[lv];
                    } else need(false, "unknown boss venue");
                    need(points >= 1 && AP_BossVenueScheduleFits(g, points), "a boss cup leg that cannot carry the boss's attack");
                    need(tracks.count(ltrack), "a boss venue the plan does not list");
                    if (!used_tracks.count(ltrack)) {
                        const J &track = *tracks.at(ltrack);
                        if (ltrack == "cortex_vortex") {
                            const J &row = registry["cortex_vortex"];
                            need(!cortex_seen && track == J{{"id", row["id"]}, {"origin", "cortex_vortex"}, {"package_id", row["package_id"]},
                                                            {"display_name", row["name"]}, {"laps", row["laps"]}, {"modes", J::array({"trophy"})},
                                                            {"capabilities", J::array({"trophy"})}, {"field_size", 8}},
                                 "a boss venue raced nowhere else is not its trophy-only record");
                            cortex_seen = true; out.cortex = true;
                        } else {
                            const int lv = std::stoi(ltrack.substr(7));
                            need(retail_ids.insert(lv).second &&
                                 track == J{{"id", ltrack}, {"origin", "retail"}, {"retail_id", lv}, {"display_name", registry["retail"][lv]["name"]},
                                            {"laps", AP_CONTENT_RETAIL_LAPS}, {"modes", J::array({"trophy"})},
                                            {"capabilities", J::array({"trophy"})}, {"field_size", 8}},
                                 "a boss venue raced nowhere else is not its trophy-only record");
                        }
                        used_tracks.insert(ltrack);
                    }
                    if (ltrack == "cortex_vortex") out.cortex = true;
                    ctr_content_leg l = leg_track(ltrack).leg;
                    l.victory = 0; l.laps = AP_CONTENT_RETAIL_LAPS;
                    copy(l.leg_id, J(lid));
                    cp.legs[n] = l;
                    out.bosses.cup_points[g][n] = points;
                }
                out.bosses.venues[g] = AP_BOSS_VENUE_GARAGE[g].homeLevel;
                const J route{{"entry_id", nid}, {"mode", "boss_cup"}, {"leg_id", nullptr}, {"encounter_id", eid}, {"own_pad_id", pad}, {"award", "win"}};
                need(expected_checks.emplace(kBossCodes[k], J{{"location", kBossCodes[k]}, {"owner", eid}, {"kind", "boss_cup"},
                                                              {"display_name", kBossNames[k]}, {"routes", J::array({route})}}).second,
                     "duplicate check identity");
                J hit = route; hit["award"] = "hit";
                boss_hits[kBossOpponents[k]].push_back(hit);
                boss_requirement_ids.push_back(enc["requirement_id"]);
                continue;
            }
            fields(entry, {"id", "kind", "track_id", "laps", "opponent"});
            need(entry["kind"] == "boss_race" && entry["laps"] == AP_CONTENT_RETAIL_LAPS &&
                 entry["opponent"] == J{{"role", "opponent"}, {"slot", 1}, {"character_id", kBossOpponents[k]},
                                       {"driving_profile", "retail"}, {"attack_profile", "inherited"}},
                 "boss race entry does not match its garage's retail opponent");
            const std::string track_id = text(entry["track_id"]);
            int level = -1;
            // #133 PR 6b: an ordinary garage may race a custom track the plan
            // also races on a pad or in a cup (its record is checked there),
            // served on the garage's home LevelID, by its stated restart point
            // count; Cortex Vortex by its bundled count.
            int custom_package = -1;
            if (g < 4 && track_id.rfind("custom/", 0) == 0) {
                need(out.bosses.track_shuffle, "a custom boss venue needs track_shuffle");
                need(tracks.count(track_id) && used_tracks.count(track_id), "a custom boss venue the plan does not race");
                const std::string package_id = text(tracks.at(track_id)->at("package_id"));
                need(package_index.count(package_id), "a custom boss venue without its package");
                custom_package = package_index.at(package_id);
                const ctr_content_package &pkg = out.packages[custom_package];
                need(pkg.ai_nav == 1 && pkg.checkpoints >= 1 && AP_BossVenueScheduleFits(g, pkg.checkpoints),
                     "a custom boss venue that cannot carry the boss's attack");
                need(out.bosses.repeats || drawn.insert(1000 + custom_package).second, "boss venues repeat without repeats");
                out.bosses.venue_package[g] = custom_package;
                out.bosses.venue_points[g] = pkg.checkpoints;
                out.bosses.venues[g] = AP_BOSS_VENUE_GARAGE[g].homeLevel;
            }
            else if (track_id == "cortex_vortex") level = kCortexVenue;
            else if (track_id.rfind("retail:", 0) == 0 && track_id.size() <= 9) {
                const std::string digits = track_id.substr(7);
                need(!digits.empty() && std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) &&
                     (digits.size() == 1 || digits[0] != '0'), "unknown boss venue");
                level = std::stoi(digits);
                need(level < 18 && AP_BossVenueTrackKnown(level), "unknown boss venue");
            } else need(false, "unknown boss venue");
            if (g < 4 && custom_package >= 0) {
                // checked above
            } else if (g < 4 && level == kCortexVenue) {
                need(out.bosses.track_shuffle && AP_BossVenueScheduleFits(g, kCortexRestartPoints),
                     "Cortex Vortex as a garage venue needs track_shuffle");
                need(out.bosses.repeats || drawn.insert(level).second, "boss venues repeat without repeats");
                out.bosses.venue_cortex[g] = 1;
                out.cortex = true; // the bundled pair is scanned and measured at connect
                out.bosses.venue_points[g] = kCortexRestartPoints;
                out.bosses.venues[g] = AP_BOSS_VENUE_GARAGE[g].homeLevel;
            } else if (g < 4) {
                need(level >= 0, "an ordinary garage races a retail venue");
                if (out.bosses.track_shuffle) need(AP_BossVenueCertified(g, level), "a boss venue that cannot carry the boss's attack");
                else need(level == AP_BOSS_VENUE_GARAGE[g].homeLevel, "without track_shuffle a boss races its home track");
                need(out.bosses.repeats || drawn.insert(level).second, "boss venues repeat without repeats");
                out.bosses.venues[g] = level;
            } else if (index == 0) {
                need(level == kOxideStationLevel, "N. Oxide's Challenge races Oxide Station");
            } else {
                need(level == kOxideStationLevel || level == kCortexVenue,
                     "N. Oxide's Final Challenge races Cortex Vortex or Oxide Station");
                out.bosses.final_track = level == kOxideStationLevel ? CTR_CFG_OXIDE_FINAL_OXIDE_STATION
                                                                          : CTR_CFG_OXIDE_FINAL_CORTEX_VORTEX;
            }
            // The venue's track record: a pad track's or a cup leg's, else a
            // trophy-only record of its own.
            need(tracks.count(track_id), "a boss venue the plan does not list");
            if (!used_tracks.count(track_id)) {
                const J &track = *tracks.at(track_id);
                if (level == kCortexVenue) {
                    const J &row = registry["cortex_vortex"];
                    need(!cortex_seen && track == J{{"id", row["id"]}, {"origin", "cortex_vortex"}, {"package_id", row["package_id"]},
                                                    {"display_name", row["name"]}, {"laps", row["laps"]}, {"modes", J::array({"trophy"})},
                                                    {"capabilities", J::array({"trophy"})}, {"field_size", 8}},
                         "a boss venue raced nowhere else is not its trophy-only record");
                    cortex_seen = true;
                } else {
                    need(retail_ids.insert(level).second &&
                         track == J{{"id", track_id}, {"origin", "retail"}, {"retail_id", level}, {"display_name", registry["retail"][level]["name"]},
                                    {"laps", AP_CONTENT_RETAIL_LAPS}, {"modes", J::array({"trophy"})},
                                    {"capabilities", J::array({"trophy"})}, {"field_size", 8}},
                         "a boss venue raced nowhere else is not its trophy-only record");
                }
                used_tracks.insert(track_id);
            }
            const J route{{"entry_id", nid}, {"mode", "boss_race"}, {"leg_id", nullptr}, {"encounter_id", eid}, {"own_pad_id", pad}, {"award", "win"}};
            need(expected_checks.emplace(kBossCodes[k], J{{"location", kBossCodes[k]}, {"owner", eid}, {"kind", "boss_race"},
                                                          {"display_name", kBossNames[k]}, {"routes", J::array({route})}}).second,
                 "duplicate check identity");
            J hit = route; hit["award"] = "hit";
            boss_hits[kBossOpponents[k]].push_back(hit);
            boss_requirement_ids.push_back(enc["requirement_id"]);
        }
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
    // Itemsanity and Hit Character (#133 PR 4d): global checks with a route
    // per Trophy Race and CTR Challenge, in physical pad order, then mode
    // order. A Hit check also routes from every cup leg (#133 PR 5b), in leg
    // order on its pad, and every Hit target has the same routes. The codes
    // and names are the registry's (the apworld's frozen class order).
    std::sort(race_routes.begin(), race_routes.end(),
              [](const RaceRoute &a, const RaceRoute &b) { return a.physical < b.physical; });
    std::vector<std::pair<int, J>> hit_route_list;
    for (const auto &r : race_routes)
        for (int m = 0; m < 2; ++m)
            if (r.modes & (1u << m)) hit_route_list.emplace_back(r.physical, route_record(r.entry, m ? "ctr" : "trophy", r.pad, "hit"));
    for (const auto &r : cup_hit_routes)
        hit_route_list.emplace_back(r.physical, J{{"entry_id", r.entry}, {"mode", "cup"}, {"leg_id", r.leg},
                                                  {"encounter_id", nullptr}, {"own_pad_id", r.pad}, {"award", "hit"}});
    std::stable_sort(hit_route_list.begin(), hit_route_list.end(),
                     [](const std::pair<int, J> &a, const std::pair<int, J> &b) { return a.first < b.first; });
    auto global_check = [&](const J &row, const char *kind, const char *award, bool hit) {
        J routes = J::array();
        if (hit)
            for (const auto &r : hit_route_list) routes.push_back(r.second);
        // A boss can be hit in its own boss race (#133 PR 6): its encounters'
        // routes come after the others, and only on its own Hit check.
        if (hit && !routes.empty()) {
            const auto boss = boss_hits.find((int)(row["location"].get<int64_t>() - 35025000));
            if (boss != boss_hits.end()) for (const auto &r : boss->second) routes.push_back(r);
        }
        else
            for (const auto &r : race_routes)
                for (int m = 0; m < 2; ++m)
                    if (r.modes & (1u << m)) routes.push_back(route_record(r.entry, m ? "ctr" : "trophy", r.pad, award));
        need(!routes.empty(), std::string("global check without a race to earn it: ") + row["name"].get<std::string>());
        const int64_t code = row["location"].get<int64_t>();
        need(expected_checks.emplace(code, J{{"location", code}, {"owner", "global"}, {"kind", kind},
                                             {"display_name", row["name"]}, {"routes", routes}}).second,
             "duplicate check identity");
    };
    if (out.itemsanity)
        for (size_t i = 0; i < registry["itemsanity"].size(); ++i)
            global_check(registry["itemsanity"][i], "itemsanity", i % 2 ? "use_juiced" : "use", false);
    if (out.hit)
        for (size_t i = 0; i < registry["hit_character"].size(); ++i)
            global_check(registry["hit_character"][i], "hit", "hit", true);
    // Two checks never share a name, so a room cannot show two identical rows.
    std::set<std::string> check_names;
    for (const auto &pair : expected_checks)
        need(check_names.insert(fold(pair.second["display_name"].get<std::string>())).second, "two checks share a display name");
    need(entries.empty() || starting_entry, "no playable starting pad");
    need(p["checks"].is_array() && p["checks"].size() == expected_checks.size(), "check registry size mismatch");
    // A check's `boost` (#133 PR 4c) is the apworld's logic as data: native
    // checks its range and that the plan has Progressive Boost on, and reads
    // it; it does not re-derive which ranks a race needs (the win_logic rule).
    const int boost_ceiling = out.capability.boost_mode ? (out.capability.boost_blue_fire ? 3 : 2) : 0;
    for (const auto &check : p["checks"]) {
        const int64_t id = number(check.at("location"), 1);
        J bare = check;
        if (bare.is_object() && bare.contains("boost")) {
            need(boost_ceiling > 0, "a check boost rank needs Progressive Boost (the capability record)");
            out.boosts[id] = number(bare["boost"], 1, boost_ceiling);
            bare.erase("boost");
        }
        // An Item Box check's stat term (#133 PR 4e): true only, only on a box
        // check, only with Progressive Stats on. Read as given, like `boost`.
        if (bare.is_object() && bare.contains("stats")) {
            need(bare["stats"] == true && out.capability.stats_mode && bare.contains("kind") && bare["kind"] == "box",
                 "a check stat term must be true, on an Item Box check, with Progressive Stats on");
            out.stat_checks.insert(id);
            bare.erase("stats");
        }
        need(out.checks.insert(id).second && expected_checks.count(id) && bare == expected_checks.at(id), "check identity or route ownership mismatch");
    }
    for (auto &pad : out.pads) {
        if (!pad.occupied) continue;
        const auto trophy = out.boosts.find(pad.trophy_location), ctr = out.boosts.find(pad.ctr_location);
        const auto sweep = out.boosts.find(pad.sweep_location);
        pad.sweep_boost = pad.sweep_location > 0 && sweep != out.boosts.end() ? sweep->second : 0;
        pad.trophy_boost = pad.trophy_location > 0 && trophy != out.boosts.end() ? trophy->second : 0;
        pad.ctr_boost = pad.ctr_location > 0 && ctr != out.boosts.end() ? ctr->second : 0;
    }
    const J &items = p["items"];
    fields(items, {"rows", "coded_checks", "locked_checks", "filler_count", "trap_count"});
    // The 15 counted families, plus one row per pooled letter item (#133 PR 3b),
    // one per pool-only pack item (#133 PR 4a) and one per unlock racer (#133 PR 4b).
    need(items["rows"].is_array() && items["rows"].size() == 15 + letter_items.size() + packs.size() + racer_rows.size() + cap_rows.size() +
                                                           weapon_rows.size() + (surface_rows ? kSurfaceCount : 0),
         "item family registry mismatch");
    std::set<int> item_ids;
    std::set<int64_t> letter_rows, pack_rows, unlock_rows, chain_rows, weapon_seen, surface_seen;
    int64_t remaining = 0, locked_checks = fixed_filler ? 1 : 0;
    J gems = J::array();
    for (const auto &row : items["rows"]) {
        fields(row, {"item", "base", "extra", "start_from_pool", "start_additional", "locked_selected", "remaining", "receipt_cap", "requirement_ceiling"});
        const int64_t code = number(row["item"], 1);
        ctr_content_item letter_row{};
        if (letter_items.count(code)) {
            need(letter_rows.insert(code).second && row["base"] == 1 && row["extra"] == 0, "a letter item is pooled once");
        }
        const bool racer = racer_rows.count(code) != 0;
        const bool chain = cap_rows.count(code) != 0;
        const bool weapon = weapon_rows.count(code) != 0;
        // The five surface items are pooled together or not at all.
        const bool surface = code >= kSurfaceFirst && code <= kSurfaceLast;
        const bool pack = packs.count(code) != 0 || racer || chain || weapon || surface;
        if (pack)
            need((racer ? unlock_rows : chain ? chain_rows : weapon ? weapon_seen : surface ? surface_seen : pack_rows).insert(code).second &&
                 row["base"] == (racer || weapon || surface ? 1 : chain ? cap_rows.at(code) : packs.at(code)) &&
                 row["extra"] == 0, "a pack, racer, capability, weapon or surface item row disagrees with its record");
        const int i = letter_items.count(code) || pack ? -1 : number(row["item"], 35010000, 35010014) - 35010000;
        need(i < 0 || item_ids.insert(i).second, "duplicate item family");
        auto &r = i < 0 ? letter_row : out.items[i];
        r.base = number(row["base"]); r.extra = number(row["extra"]);
        r.start_from_pool = number(row["start_from_pool"], 0, 10000); r.start_additional = number(row["start_additional"], 0, 10000);
        r.remaining = number(row["remaining"]); r.receipt_cap = number(row["receipt_cap"]);
        // A fixed item takes a base copy that a from-pool start left (content_fixed.placements).
        const int64_t copies = (int64_t)r.base + r.extra - r.start_from_pool;
        const int64_t locked = i == 14 && fixed_keys ? std::max<int64_t>(0, std::min<int64_t>(r.base, copies)) :
                               i >= 9 && i <= 13 && fixed_gems && r.base && copies > 0 ? 1 : 0;
        locked_checks += locked;
        need(row["locked_selected"] == locked && r.remaining == copies - locked && r.receipt_cap == (int64_t)r.base + r.extra + r.start_additional && number(row["requirement_ceiling"]) == r.base, "selected item ledger mismatch");
        need(i != 14 || r.base == 4, "four base Keys are mandatory");
        need(i >= 0 || pack || r.base == 1, "a letter item is pooled once");
        need(i < 9 || i > 13 || r.base <= 1, "base Gems must be distinct colours");
        out.starts[code] = r.start_from_pool + r.start_additional;
        remaining += r.remaining;
    }
    // Manual requirement records: item identities and counts come from the
    // ledger, so a forged record cannot name an item the seed lacks or ask for
    // more than the base supply (`requirement_ceiling`).
    need(manual_pad.size() == gate_ids.size(), "manual requirement without an occupied pad");
    need(stage2_pad.size() == stage2_ids.size(), "second-stage requirement without a pad that uses it");
    for (int pass = 0; pass < 2; ++pass)
    for (const auto &pair : pass ? stage2_ids : gate_ids) {
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
        // A drawn requirement of this mode never asks 4 Keys; a manual rule is
        // refused at generation, and Key rules on the wire fall under the same bar.
        need(pad_mode != PAD_REQ_NO_FOUR_KEYS || mask != (1u << 14) || count < 4, "random_without_4_keys asks for 4 Keys");
    }
    need(pad_mode != PAD_REQ_VANILLA || manual_ids.empty(), "manual rules need a random pad requirement mode");
    if (pad_mode == PAD_REQ_VANILLA) vanilla_pads(out);
    else if (has_pad_requirements) {
        // A drawn stage keeps its count ceiling (Keys excepted), and a stage 2
        // asks more than its pad's stage 1, the apworld's check_record. The
        // draw itself is not redone.
        for (const auto &pair : stage1_ids) {
            const ctr_content_pad &pad = *manual_pad.at(pair.first);
            need(pad.rule_mask == (1u << 14) || pad.rule_count <= 8, "a drawn stage 1 asks above its count ceiling");
        }
        for (const auto &pair : stage2_ids) {
            const ctr_content_pad &pad = *stage2_pad.at(pair.first);
            need(pad.stage2_mask == (1u << 14) || pad.stage2_count <= 4, "a drawn stage 2 asks above its count ceiling");
            need(!pad.has_rule || pad.stage2_count > pad.rule_count || (pad.rule_mask & ~pad.stage2_mask),
                 "a stage 2 asks no more than its stage 1");
        }
    }
    need(item_ids.size() == 15 && letter_rows.size() == letter_items.size() && pack_rows.size() == packs.size() &&
         unlock_rows.size() == racer_rows.size() && chain_rows.size() == cap_rows.size() &&
         weapon_seen.size() == weapon_rows.size() && surface_seen.size() == (surface_rows ? (size_t)kSurfaceCount : 0u),
         "item family registry mismatch");
    // A tier's checks follow its copies, capped by the Relic Races in the seed.
    for (int t = 0; t < 3; ++t)
        need(relic_checks[t] == std::min<int64_t>(relic_supply[t], eligible_races), "relic tier checks do not follow the item counts");
    // Racer locks (#133 PR 4b), apworld content_plan.lock_eligible: only on an
    // occupied pad that does not open at the start, because its hub gate needs
    // Keys or its stage 1 rule is not met by the starting items (from-pool and
    // additional starts), and exactly min(requested_locks, eligible) of them.
    // With the pad_requirements record the four N. Sanity Beach race pads never
    // take a lock, and vanilla requirements lock no pad.
    {
        int eligible = 0, locks = 0;
        for (const auto &pad : out.pads) {
            if (!pad.occupied) { need(pad.racer_lock < 0, "a racer lock on a pad that opens at the start or holds no entry"); continue; }
            long long held = 0;
            for (int i = 0; i < 15; ++i)
                if (pad.rule_mask & (1u << i)) held += (long long)out.items[i].start_from_pool + out.items[i].start_additional;
            const bool beach = pad.physical == 3 || pad.physical == 6 || pad.physical == 9 || pad.physical == 8;
            const bool opens = (has_pad_requirements && (beach || pad_mode == PAD_REQ_VANILLA)) ||
                               (pad.keys == 0 && (!pad.has_rule || held >= pad.rule_count));
            if (!opens) ++eligible;
            if (pad.racer_lock >= 0) {
                need(!opens, "a racer lock on a pad that opens at the start or holds no entry");
                ++locks;
            }
        }
        if (out.racers) need(locks == std::min(out.requested_locks, eligible), "racer lock count is not min(requested_locks, eligible pads)");
    }
    for (int i = 9; i <= 13; ++i) if (out.items[i].base) { gems.push_back(35010000 + i); out.goal_mask |= 1 << (i - 9); }
    const int filler = number(items["filler_count"], 0, 4096);
    // Boss garages (#133 PR 6): the Oxide goal check holds filler only (issue
    // #27), so a plan whose goal is an Oxide challenge keeps one filler slot.
    need(!out.bosses.present || (out.bosses.oxide_goal != 1 && out.bosses.oxide_goal != 2) || filler >= 1,
         "the Oxide goal check needs one filler slot");
    need(number(items["coded_checks"], 0, 4096) == (int)out.checks.size() && items["locked_checks"] == locked_checks && remaining + filler + locked_checks == (int64_t)out.checks.size() && number(items["trap_count"], 0, 4096) <= filler, "item/check capacity mismatch");
    // What the record fixes must exist: garages, all five Gem Cups, and the Final
    // Challenge goal with the optional first race.
    need(!fixed_keys || out.bosses.present, "fixed_items.keys needs boss garages");
    if (fixed_gems)
        for (int colour = 0; colour < 5; ++colour)
            need(entries.count("entry/gem_cup:" + std::to_string(colour)) != 0, "fixed_items.gems needs all five Gem Cups");
    need(!fixed_filler || (out.bosses.present && out.bosses.oxide_goal == 2 && out.bosses.oxide_1_optional),
         "fixed_items.oxide_1_filler belongs to oxide_goal 101_percent with oxide_1_optional");
    // The goal (#133 PR 6): the Gem arm counts the selected distinct base Gem
    // colours, alone without the `bosses` record; with it, the AND of the
    // Oxide, Boss and Gem arms the record and the Gem arm give (apworld
    // content_bosses.goal), and the Gem arm may be absent.
    const J &goal = p["goal"];
    need(goal.is_object() && goal.contains("op"), "unsupported goal predicate");
    const J *gem = nullptr;
    if (goal["op"] == "distinct") gem = &goal;
    else if (goal["op"] == "all" && goal.contains("terms") && goal["terms"].is_array())
        for (const auto &term : goal["terms"])
            if (term.is_object() && term.contains("op") && term["op"] == "distinct") { need(!gem, "two Gem arms in the goal"); gem = &term; }
    out.goal_count = 0;
    if (gem) {
        fields(*gem, {"op", "items", "count"});
        out.goal_count = number((*gem)["count"], 1, 5);
        need((*gem)["items"] == gems && out.goal_count <= (int)gems.size(), "goal must count selected distinct base Gems");
    }
    if (!out.bosses.present) need(gem == &goal, "goal must count selected distinct base Gems");
    else {
        const ctr_content_bosses &b = out.bosses;
        J companions = J::array();
        if (b.bosses_required)
            companions.push_back(J{{"op", "checked"}, {"locations", J::array({kBossCodes[0], kBossCodes[1], kBossCodes[2], kBossCodes[3]})},
                                   {"count", b.bosses_required}});
        if (gem) companions.push_back(*gem);
        J terms = J::array();
        if (b.oxide_goal == 1 || b.oxide_goal == 2)
            terms.push_back(J{{"op", "checked"}, {"locations", J::array({kBossCodes[b.oxide_goal == 1 ? 4 : 5]})}, {"count", 1}});
        for (const auto &t : companions) terms.push_back(t);
        need(!terms.empty() && goal == all_of(terms), "goal does not match the bosses record");
        // Requirements: the retail Trophy counts on the four boss garages
        // (never above the base Trophy supply); N. Oxide's Challenge `free`, or
        // the companion arms with any_percent; the Final Challenge the
        // companion arms when Oxide is the goal and its relic term on the base
        // supply (one tier, any tier with enough, or the sum of the tiers).
        // The ordinary garages (#133 PR 6b) carry the mixed-item roll: one
        // family over its base items, Keys above the garage's hub gate (0, 1,
        // 2, 3 Keys) and at most three, any other count 1 to the family's base
        // supply; with vanilla pad requirements the Trophy table (apworld
        // content_bosses.check_garage_requirement). The draw is not redone.
        std::map<std::string, J> want;
        for (int g = 0; g < 4; ++g) {
            const std::string rid = "boss:" + std::to_string(g);
            need(requirements.count(rid) != 0, "a boss garage has no requirement record");
            fields(*requirements.at(rid), {"id", "predicate"});
            const J &pred = requirements.at(rid)->at("predicate");
            need(pred.is_object() && pred.contains("op") && (pred["op"] == "count" || pred["op"] == "sum"),
                 "a boss garage requirement is one item count or one family sum");
            int type = 0, colour = -1;
            long long supply = 0;
            if (pred["op"] == "count") {
                fields(pred, {"op", "item", "count"});
                const int item = number(pred["item"], 35010000, 35010014) - 35010000;
                supply = out.items[item].base;
                if (item == 0) type = 1;
                else if (item <= 3) { type = 4; colour = item - 1; }
                else if (item <= 8) { type = 3; colour = item - 4; }
                else if (item <= 13) { type = 5; colour = item - 9; }
                else type = 2;
            } else {
                fields(pred, {"op", "items", "count"});
                static const int families[3][3] = {{1, 3, 7}, {4, 8, 6}, {9, 13, 8}}; // first, last, ctr_req Any type
                for (const auto &family : families) {
                    J eligible = J::array();
                    long long sum = 0;
                    for (int i = family[0]; i <= family[1]; ++i)
                        if (out.items[i].base) { eligible.push_back(35010000 + i); sum += out.items[i].base; }
                    if (eligible.size() >= 2 && pred["items"] == eligible) { type = family[2]; supply = sum; }
                }
                need(type != 0, "garage sum requirement items are not exactly one family's base items");
            }
            const int count = number(pred["count"], 1);
            need(supply >= 1 && count <= supply, "a boss garage asks for more than the base supply");
            if (type == 1 && !has_pad_requirements) need(count == 4, "a Trophy garage asks exactly four Trophies");
            if (pad_mode == PAD_REQ_VANILLA)
                need(type == 1 && count == std::min<long long>(4 * (g + 1), supply), "vanilla garages ask the Trophy table");
            if (type == 2) need(count > g && count <= 3, "a Key garage asks more than its hub gate and at most three");
            out.bosses.garage_req_type[g] = type;
            out.bosses.garage_req_count[g] = count;
            out.bosses.garage_req_colour[g] = colour;
            want[rid] = pred;
        }
        J ids = J::array({"boss:0", "boss:1", "boss:2", "boss:3"});
        if (b.oxide_goal != 3) {
            const bool first = b.oxide_goal == 1 && !companions.empty();
            if (first) want["oxide:first"] = all_of(companions);
            ids.push_back(first ? "oxide:first" : "free");
            J final_terms = b.oxide_goal == 1 || b.oxide_goal == 2 ? companions : J::array();
            const int n = b.final_count;
            const int tier_base[3] = {out.items[1].base, out.items[2].base, out.items[3].base};
            int supply = 0;
            J relic;
            if (b.final_unlock == 4) {
                J items = J::array();
                for (int t = 0; t < 3; ++t) if (tier_base[t] > 0) { items.push_back(35010001 + t); supply += tier_base[t]; }
                relic = J{{"op", "sum"}, {"items", items}, {"count", n}};
            } else if (b.final_unlock == 3) {
                J any = J::array();
                for (int t = 0; t < 3; ++t) {
                    supply = std::max(supply, tier_base[t]);
                    if (tier_base[t] >= n) any.push_back(J{{"op", "count"}, {"item", 35010001 + t}, {"count", n}});
                }
                relic = J{{"op", "any"}, {"terms", any}};
            } else {
                supply = tier_base[b.final_unlock];
                relic = J{{"op", "count"}, {"item", 35010001 + b.final_unlock}, {"count", n}};
            }
            need(supply >= 1 && n <= supply, "the Final Challenge relic count is above the base supply its mode can count");
            final_terms.push_back(relic);
            want["oxide:final"] = all_of(final_terms);
            ids.push_back("oxide:final");
        }
        need(boss_requirement_ids == ids, "encounter requirement ids do not match the bosses record");
        need(want.size() == boss_requirements, "boss requirement records do not match the encounters");
        for (const auto &pair : want)
            need(requirements.count(pair.first) && *requirements.at(pair.first) == J{{"id", pair.first}, {"predicate", pair.second}},
                 "boss requirement records do not match the encounters");
    }
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
        // `hit_character_encounters` (#133 PR 4d): the normal room's block,
        // present exactly when the plan has Hit Character on (checked below).
        const bool hit_block = j.is_object() && j.contains("hit_character_encounters");
        need(j.is_object() && j.size() == 3u + (traps ? 1u : 0u) + (hit_block ? 1u : 0u) && j.contains("schema_version") &&
             j.contains("content_plan") && j.contains("ctr_options"), "unsupported or incomplete record fields");
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
        // Stat settings (#133 PR 4b), present only when set: the starting
        // racer's class, Penta's MAX table, and the resolved editable-stats
        // trio (apworld content_plan.STAT_ENVELOPE): editable stats (source 2,
        // owner 1 or 2, editing on) or Progressive Stats (source 1, owner the
        // stats mode, editing off, #133 PR 4c), checked against the plan below.
        const bool stat_class = co.contains("starting_stat_class"), penta = co.contains("penta_stats");
        const bool trio = co.contains("stat_source") || co.contains("stat_owner") || co.contains("stat_editing_allowed");
        const int start_class = stat_class ? number(co["starting_stat_class"], 1, 4) : 0;
        need(!penta || number(co["penta_stats"], 1, 1) == 1, "penta_stats must be 1 when present");
        int stat_owner = 0, stat_source = 0;
        if (trio) {
            need(co.contains("stat_source") && co.contains("stat_owner") && co.contains("stat_editing_allowed"),
                 "stat_source, stat_owner and stat_editing_allowed must be sent together");
            stat_source = number(co["stat_source"], 1, 2);
            need(co["stat_editing_allowed"] == (stat_source == 2),
                 "stat_source, stat_owner and stat_editing_allowed must be editable stats or Progressive Stats together");
            stat_owner = number(co["stat_owner"], 1, 2);
        }
        for(const char *key:{"starting_stat_class","penta_stats","stat_source","stat_owner","stat_editing_allowed"})base.erase(key);
        // Item Box Locations (#133 PR 4e): `box_locations` true and the seed's
        // `color_boxes_by_item`, present exactly with the plan's record.
        const bool box_flag = co.contains("box_locations"), box_colours = co.contains("color_boxes_by_item");
        need(!box_flag || co["box_locations"] == true, "box_locations must be true when present");
        need(!box_colours || co["color_boxes_by_item"].is_boolean(), "color_boxes_by_item must be true or false");
        for(const char *key:{"box_locations","color_boxes_by_item"})base.erase(key);
        const bool relic_progression = co.contains("relic_progression");
        if(relic_progression) {
            const J &map = co["relic_progression"];
            need(map.is_object() && map.size() == 3 && map.contains("Sapphire Relic") &&
                 map.contains("Gold Relic") && map.contains("Platinum Relic") &&
                 map["Sapphire Relic"].is_boolean() && map["Gold Relic"].is_boolean() &&
                 map["Platinum Relic"].is_boolean(), "relic_progression must name the three relic tiers with boolean values");
            base.erase("relic_progression");
        }
        fields(base, {"schema_version", "world_version", "content_plan", "goal", "goal_oxide", "goal_bosses", "goal_gems", "starting_character", "character_unlocks", "progressive_boost", "progressive_stats", "hit_character", "death_link", "deathlink_amnesty"});
        need(number(co["schema_version"]) == 18 && co["content_plan"] == true, "inconsistent content-plan envelope");
        need(co["goal"] == -1 && co["character_unlocks"].is_boolean() && co["hit_character"].is_boolean(),
             "unsupported content-plan options");
        const int boost_mode = number(co["progressive_boost"], 0, 2), stats_mode = number(co["progressive_stats"], 0, 2);
        size_t nodes = 0, bytes = 0; budget(co, 0, nodes, bytes);
        State candidate;
        validate(j["content_plan"], candidate);
        bool arenas=false,arcade=false,pickups=false;
        for(const auto &pad:candidate.pads)if(pad.occupied && pad.crystal) {
            arenas=true;arcade |= AP_ArcadeArenaIndex(pad.retail_id)>=0;pickups |= pad.crystal_count>0;
        }
        // Character Unlocks (#133 PR 4b) is on exactly when the plan has its
        // `racers` record, and the envelope's starting racer is the record's.
        need(co["character_unlocks"].get<bool>() == candidate.racers &&
             (!candidate.racers || number(co["starting_character"], 0, 15) == candidate.starting_character),
             "character envelope disagrees with the plan's racers record");
        // Capability packs (#133 PR 4c): the envelope's two modes are the
        // record's (0 without it), and Progressive Stats sends source 1 with
        // its mode as owner.
        need(boost_mode == candidate.capability.boost_mode && stats_mode == candidate.capability.stats_mode,
             "capability envelope disagrees with the plan's capability record");
        // Hit Character (#133 PR 4d): the envelope flag and the room's block
        // follow the plan's record.
        need(co["hit_character"].get<bool>() == candidate.hit && hit_block == candidate.hit,
             "Hit Character envelope or block disagrees with the plan's global_checks record");
        need((stat_source == 1) == (stats_mode != 0) && (stat_source != 1 || stat_owner == stats_mode),
             "a Progressive Stats plan must send stat_source 1 with its mode as stat_owner");
        need(box_flag == (candidate.item_boxes.present != 0) && box_colours == box_flag,
             "box envelope disagrees with the plan's item_boxes record");
        const size_t optional = (send ? 1u : 0u) + (display ? 1u : 0u) + (colors ? 1u : 0u) +
                                (stat_class ? 1u : 0u) + (penta ? 1u : 0u) + (trio ? 3u : 0u) +
                                (box_flag ? 2u : 0u) + (relic_progression ? 1u : 0u);
        if(arenas) {
            need(co.size()==base.size()+3+optional && co.at("include_battle_arenas")==true &&
                 co.at("include_arcade_arenas")==arcade && co.at("crystalsanity")==pickups,"arena options disagree with plan");
        } else need(co.size()==base.size()+optional && !co.contains("include_battle_arenas") &&
                    !co.contains("include_arcade_arenas") && !co.contains("crystalsanity"),"arena options without arena entries");
        // The plan owns content availability; legacy arena-pad filtering must
        // not close a physical arena seat holding a Trophy entry.
        ctr_cfg.include_battle_arenas=1;
        // The goal keys follow the plan (#133 PR 6): goal_oxide and
        // goal_bosses its `bosses` record (3 and 0 without one), goal_gems its
        // Gem arm (0 when the goal has none).
        need(number(co["goal_gems"], 0, 5) == candidate.goal_count &&
             number(co["goal_oxide"], 0, 3) == candidate.bosses.oxide_goal &&
             number(co["goal_bosses"], 0, 4) == candidate.bosses.bosses_required, "goal envelope disagrees with plan");
        ctr_cfg.starting_character = number(co["starting_character"], 0, 15);
        // 0 off, 1 mask_reset, 2 any_hit, 3 race_loss (#286), 4 weapon_hit
        // (#483). Before #133 PR 4a this refused race_loss, which the apworld
        // emits for content plans too.
        ctr_cfg.death_link = number(co["death_link"], 0, 4);
        ctr_cfg.deathlink_amnesty = number(co["deathlink_amnesty"], 1, 30);
        ctr_cfg.death_link_send = send_mask; // -1 absent: the legacy coupling
        ctr_cfg.warp_pad_item_display = display_mode;
        ctr_cfg.ap_item_type_colors = colors ? (co["ap_item_type_colors"].get<bool>() ? 1 : 0) : 1;
        text(co["world_version"]); copy(ctr_cfg.world_version, co["world_version"]);
        ctr_cfg.goal = -1; ctr_cfg.goal_oxide = 3; ctr_cfg.goal_bosses = 0; ctr_cfg.goal_gems = candidate.goal_count;
        // Boss garages (#133 PR 6): the normal room's garage, Oxide and goal
        // code runs on these, as from a normal room's slot_data: the garage
        // roll as boss_req (boss_garage_req mode 2), the Oxide garage's four Keys,
        // the composed goal, the Final Challenge relic gate and venue, and the
        // drawn venues as a boss_encounters record (ctr_boss_encounters).
        std::memset(&ctr_boss_encounters, 0, sizeof ctr_boss_encounters);
        ctr_boss_encounters.state = AP_BOSS_ENC_RETAIL;
        if (candidate.bosses.present) {
            const ctr_content_bosses &b = candidate.bosses;
            ctr_cfg.goal_oxide = b.oxide_goal;
            ctr_cfg.goal_bosses = b.bosses_required;
            ctr_cfg.oxide_1_optional = b.oxide_1_optional;
            ctr_cfg.oxide_final_unlock = b.final_unlock;
            ctr_cfg.oxide_final_count = b.final_count;
            for (int g = 0; g < 4; ++g)
                ctr_cfg.boss_req[g] = ctr_req{b.garage_req_type[g], b.garage_req_count[g], b.garage_req_colour[g]};
            ctr_cfg.boss_req[4] = ctr_req{2, 4, -1};
            for (int g = 0; g < 5; ++g) ctr_cfg.boss_n_tracks[g] = 0;
            if (b.track_shuffle) {
                ctr_boss_encounters.state = AP_BOSS_ENC_VALID;
                for (int g = 0; g < 4; ++g) {
                    ctr_boss_encounters.track[g] = (signed char)b.venues[g];
                    // #133 PR 6b: custom bytes on the home LevelID.
                    ctr_boss_encounters.custom[g] = (unsigned char)(b.venue_package[g] >= 0 || b.venue_cortex[g]);
                    ctr_boss_encounters.points[g] = (short)b.venue_points[g];
                }
            }
            std::memset(&ctr_cfg.oxide_final_venue, 0, sizeof ctr_cfg.oxide_final_venue);
            ctr_cfg.oxide_final_venue.host_level_id = -1;
            ctr_cfg.oxide_final_venue.location = -1;
            ctr_cfg.oxide_final_venue.wumpa_location = -1; // a boss race routes no Reach 10 Wumpa
            if (b.oxide_goal != 3) {
                ctr_oxide_final_venue &v = ctr_cfg.oxide_final_venue;
                v.seen = v.valid = 1;
                v.track = b.final_track;
                v.host_level_id = CTR_CFG_CORTEX_HOST_LEVEL;
                v.location = 35011105;
                std::snprintf(v.lev_sha256, sizeof v.lev_sha256, "%s", CTR_CFG_CORTEX_LEV_SHA256);
                std::snprintf(v.vrm_sha256, sizeof v.vrm_sha256, "%s", CTR_CFG_CORTEX_VRM_SHA256);
            }
        }
        ctr_cfg.character_unlocks = candidate.racers ? 1 : 0;
        ctr_cfg.racer_locked_pads = candidate.racers ? candidate.requested_locks : 0;
        ctr_cfg.character_phase_present = 1;
        // Racer locks into the normal room's arrays (dense pads and the cup
        // range 100..104), so the pad's racer line, the warp-commit seating and
        // the tracker read them as on a normal room (#133 PR 4b).
        for (const auto &pad : candidate.pads)
            if (pad.occupied && pad.racer_lock >= 0) {
                if (pad.physical >= 100 && pad.physical <= 104) ctr_cfg.gem_cup_racer_lock[pad.physical - 100] = pad.racer_lock;
                else if (pad.physical >= 0 && pad.physical < CTR_CFG_PAD_COUNT) ctr_cfg.racer_lock[pad.physical] = pad.racer_lock;
            }
        ctr_cfg.starting_stat_class = start_class;
        ctr_cfg.penta_stats = penta ? 1 : 0;
        ctr_cfg.stat_source = stat_source;
        ctr_cfg.stat_owner = stat_owner;
        ctr_cfg.stat_editing_allowed = stat_source == 2 ? 1 : 0;
        ctr_cfg.editable_stats = stat_source == 2 ? stat_owner : 0; // the raw option, for logs only
        // Capability packs (#133 PR 4c): the kart reads these like a normal room's.
        ctr_cfg.boost_mode = candidate.capability.boost_mode;
        ctr_cfg.boost_blue_fire = candidate.capability.boost_blue_fire;
        ctr_cfg.stats_mode = candidate.capability.stats_mode;
        if (candidate.capability.present) {
            ctr_cfg.logic_difficulty = candidate.capability.logic_difficulty;
            ctr_cfg.shortcut_knowledge = candidate.capability.shortcut_knowledge;
        }
        // Item Box Locations (#133 PR 4e): the boxes stand from the room's own
        // codes (server liveness, AP_BoxInSeed); the colours are the seed's,
        // ANDed with the player's own setting as on a normal room.
        if (candidate.item_boxes.present) {
            ctr_cfg.shortcut_knowledge = candidate.item_boxes.shortcut_knowledge;
            ctr_cfg.color_boxes_by_item = co["color_boxes_by_item"].get<bool>() ? 1 : 0;
        }
        ctr_cfg.shuffle_gems = ctr_cfg.shuffle_keys = 1;
        ctr_cfg.shuffle_warp_pads = 0; // Hub objects keep their physical IDs.
        ctr_cfg.warppad_unlock_mode = 0;
        // The trophy-count doors (#133 PR 6); empty garages stay shut anyway.
        ctr_cfg.bossgarage_mode = candidate.bosses.present ? 2 : 0;
        ctr_cfg.one_lap_cups = 0;
        if (!candidate.bosses.present) ctr_cfg.oxide_1_optional = 0;
        ctr_cfg.relic_min_time = ctr_cfg.relics_require_perfect = 0;
        // Itemsanity (#133 PR 4d): the runtime keys on the room holding the use
        // checks (AP_ItemsanityActive); the flag feeds the tracker panel.
        ctr_cfg.itemsanity = candidate.itemsanity ? 1 : 0;
        // Hit Character: the normal room's parser with the content-plan Key
        // fallback, then the fallback must be exactly the guests whose unlock
        // wins are not in the plan (the apworld derives it the same way).
        if (candidate.hit) {
            if (!parse_hit_character(j, true)) { forget(); return 0; }
            for (int g = 0; g < CTR_CFG_HIT_TRIGGER_COUNT; ++g) {
                const ctr_hit_trigger &t = ctr_cfg.hit.triggers[g];
                bool present = false;
                for (int k = 0; k < t.count; ++k) present |= candidate.checks.count(t.any_of[k]) != 0;
                need((t.fallback_keys > 0) == !present, "hit_character_encounters fallback disagrees with the plan's unlock wins");
            }
            // Content-plan cups (#133 PR 5b): one draw order per cup of the
            // plan, by name index, and none for any other index.
            for (int c = 0; c < CTR_CFG_HIT_CONTENT_CUP_COUNT; ++c) {
                bool cup = false;
                for (const auto &pad : candidate.pads) cup |= pad.occupied && pad.cup && pad.cup_index == c;
                need((ctr_cfg.hit.content_cup_present[c] != 0) == cup,
                     "hit_character_encounters content_cups disagrees with the plan's cups");
            }
        }
        state = std::move(candidate);
        if (traps) ap_seedcfg_parse_trap_identities(j, 18);
        ctr_cfg.schema_version = 18; // commit after complete validation
        return 2;
    } catch (const std::exception &e) {
        ctr_cfg.hit.valid = 0; // a Hit block parsed before the refusal never stays active
        forget(); hit_reject("content_plan: %s", e.what()); return 0;
    }
}

extern "C" int ap_content_plan_present(void) { return content_plan::state.parsed && ctr_cfg.schema_version == 18 && !ctr_cfg.seed_rejected; }
extern "C" int ap_content_plan_active(void) { return ap_content_plan_present() && content_plan::state.bound; }
extern "C" uint64_t ap_content_plan_epoch(void) { return ap_content_plan_active() ? content_plan::epoch : 0; }
extern "C" const ctr_content_pad *ap_content_plan_pad(int physical) {
    if (ap_content_plan_active() && physical >= AP_CONTENT_GARAGE_CUP_PAD && physical < AP_CONTENT_GARAGE_CUP_PAD + 4) {
        const int g = physical - AP_CONTENT_GARAGE_CUP_PAD;
        return content_plan::state.bosses.cup[g] ? &content_plan::state.garage_cups[g] : nullptr;
    }
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
extern "C" const ctr_content_capability *ap_content_plan_capability(void) {
    static const ctr_content_capability none{};
    return ap_content_plan_active() ? &content_plan::state.capability : &none;
}
extern "C" int ap_content_plan_start_count(int64_t item) {
    if (!ap_content_plan_active()) return 0;
    const auto it = content_plan::state.starts.find(item);
    return it == content_plan::state.starts.end() ? 0 : it->second;
}
extern "C" int ap_content_plan_check_boost(int64_t location) {
    if (!ap_content_plan_active()) return 0;
    const auto it = content_plan::state.boosts.find(location);
    return it == content_plan::state.boosts.end() ? 0 : it->second;
}
extern "C" const ctr_content_item_boxes *ap_content_plan_item_boxes(void) {
    static const ctr_content_item_boxes none{};
    return ap_content_plan_active() ? &content_plan::state.item_boxes : &none;
}
extern "C" const ctr_content_item *ap_content_plan_item(int64_t item) {
    return ap_content_plan_active() && item >= 35010000 && item <= 35010014 ? &content_plan::state.items[item - 35010000] : nullptr;
}
extern "C" int ap_content_plan_gems_held(const int received_gems[5]) {
    if (!ap_content_plan_active() || !received_gems) return 0;
    int count = 0;
    for (int i = 0; i < 5; ++i) if ((content_plan::state.goal_mask & (1 << i)) && received_gems[i] > 0) ++count;
    return count;
}
extern "C" int ap_content_plan_goal(const int received_gems[5]) {
    if (!ap_content_plan_active() || !received_gems) return 0;
    return ap_content_plan_gems_held(received_gems) >= content_plan::state.goal_count;
}
extern "C" const ctr_content_bosses *ap_content_plan_bosses(void) {
    static const ctr_content_bosses none{};
    return ap_content_plan_active() ? &content_plan::state.bosses : &none;
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
