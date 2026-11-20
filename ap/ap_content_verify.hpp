// Content-plan completability sweep (#133 PR 8). Included by ap_seedcfg.cpp
// after ap_content_plan_impl.hpp, so it reads the admitted plan in
// content_plan::state. The plan is the apworld's logic as data, and validate()
// has already re-derived and refused every check, route, requirement and goal
// that native would not build itself, so this sweep interprets the admitted
// plan rather than a second hand-written table. It mirrors, rule for rule:
//
//   * content_plan.create_regions: pad regions (entrance requirement, first
//     stage-1 gate, racer lock), each check's routes, stage-2 gates, letter
//     items, `boost` and `stats` bound to its first route's racer lock, the
//     Itemsanity weapon alternative to a first-boost floor, Cup Leg
//     Victory routes and Complete One Lap routes (#344, content_laps);
//   * content_bosses.install: one region per encounter (garage entrance plus
//     the encounter requirement) with its check and companion event;
//   * content_global_checks.install and tiger_temple_door: Itemsanity and Hit
//     Character checks, and the Tiger Temple stone door;
//   * set_rules: the goal predicate, whose `checked` terms read the companion
//     events.
//
// Three rules are apworld code rather than plan data and are ported below:
// content_capability.weapon_arm, content_cups.route_boost / prefix_boost (and
// the finish_boost they read, with the project's per-track USF rulings), and
// the Hit target rule. Everything else comes from the plan.
#include "ap_verify_logic.h"
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace content_verify {
using J = nlohmann::json;

// worlds/ctr/data/usf_finish_rulings.json: the finish rank (2 USF, 1 the
// first boost, 0 the generic terms only) by package content id. A row with a
// revision ("" for every revision) also names the LEV pin and covers only that
// exact package; it wins over the row for every revision (A2-22).
// A new scoped row names the first catalog version that enables it.
// tools/test-content-plan-parity.py compares this table with the paired
// apworld's.
static const struct { const char *content_id, *revision, *lev_sha256; int rank; int since = 0; } kUsfRulings[] = {
    {"arabian-heights-night", "", "", 2}, {"baby-t-park", "", "", 0}, {"cortex-vortex", "", "", 2},
    {"north-pole", "", "", 0}, {"tv-world", "", "", 0},
    // Project rulings of 2026-11-05 07:19 CET (A2-27), per registry revision and LEV pin.
    {"ancient-lake", "1.0.0", "7e5b776f312f500112061b3b95a1f371571fd7dfe7fbd1dc404e7b3d3a3a1e78", 0},
    {"ancient-lake", "1.0.1", "2fc45ad5edaf735426b99229cc561293d02badc6a578ef53f328ad3fc2f85360", 0},
    {"ancient-lake", "1.0.2", "c4497a621a97a8df1c763e0b6a2dc75cd9f3a57b217b0cefa6a938fc83ee6c70", 0},
    {"bowser-castle-1", "1.0.0", "0b59b54ac3d902d43ee7b798d45dd4979aa5859520460282d22fb6bc85dcbcd9", 0},
    {"drive-thru-danger", "1.0.0", "77706a3b5d46cdd75ffa05e4da316cf5dc400d6e17aad7ccdf0c3f5cd62eee79", 0},
    {"lights-out", "1.0.0", "5ebdb9f235ba0c07ba5c72b715c949afdd6062fcf3208f45a1c13936ffb529c6", 1},
    {"neon-paradise", "1.0.0", "665d19819c437ab9ab6cbccac7c0202979405f16e1af39e06d9ea1d3286be9d0", 0},
    {"neon-paradise", "1.1.0", "639434d84fe6de4ba88a31a1dd5a11291d640617ab25af9c7bfbd5d383b53d5d", 0},
    {"sunset-vista", "1.2.0", "97fd76f06e3c57207560751728eb23dbf81980887e5f7e25f2296917578735b5", 0},
};
static const int64_t kItemBase = 35010000, kKeyItem = 35010014;
static const int64_t kItemsanityFirst = 35016000, kItemsanityLast = 35016021;
static const int64_t kHitFirst = 35025000;
static const int kFirstGuest = 8, kTigerTemple = 4, kPapusPyramid = 5;
static const int kTurbo = 0, kMask = 6; // AP_VerifyWeaponOwned indices
static const long kMaskItem = kItemBase + AP_VF_WEAPON_FIRST + kMask;
static const int kUsf = 2, kFirstBoost = 1;
static constexpr int kNoFinishRank = 4;  // the exported Boost range is 0 to 3

static bool is_sunset_finish_ruling(const char *content, const char *revision) {
    return std::strcmp(content, "sunset-vista") == 0 && std::strcmp(revision, "1.2.0") == 0;
}

// WUMPA: the goal's `wumpa_banked` arm (J11), met when Wumpa Income is.
enum Op { ALWAYS, COUNT, SUM, DISTINCT, CHECKED, ALL, ANY, WUMPA };
struct Node { Op op; int count; std::vector<int64_t> codes; std::vector<int> kids; };
struct Region { std::vector<int> gates; int lock = -1; bool income = false, mask = false; };
struct Route {
    int region = -1, stage = -1, lock = -1, need = 0;
    bool boss = false, mask = false;
    std::vector<int64_t> first;
};
// SHOP: a Wumpa Shop slot, its hub's Key floor (`target`) and income.
enum Kind { PAD, SHARED, LEG, ENCOUNTER, ITEMSANITY, HIT, SHOP, LAP, RACE };
enum Arm { ARM_NONE, ARM_FAMILIES, ARM_PAPU };
struct Loc {
    int64_t code = 0;
    Kind kind = PAD;
    int region = -1, lock = -1, boost = 0, arm = ARM_NONE, arm_families = 0, weapon = -1, target = -1;
    bool stats = false, door = false, mask = false;
    std::vector<int> stages;          // PAD: one stage-2 gate per route, all set, else empty
    std::vector<int64_t> first;       // PAD: the first race's checks, one needed, for a check of a later race
    std::vector<Route> routes;        // SHARED, LEG, ITEMSANITY, HIT
    std::vector<int64_t> letters;     // Lettersanity items the check needs
    std::vector<int64_t> triggers;    // HIT: the guest's created unlock wins
};
struct Model {
    uint64_t epoch = 0;
    unsigned int parse_revision = 0;
    std::vector<Node> nodes;
    std::vector<Region> regions;
    std::vector<Loc> locs;
    std::vector<int> encounter_loc;   // encounter index -> locs index
    std::vector<int64_t> items;       // every item code the plan pools
    // Wumpa Income (J11, apworld content plan): a Trophy Race or CTR
    // Challenge route on a normal pad racing a retail race track 0..15 or a
    // custom track whose package has measured Wumpa capability (ruling of
    // 2026-11-02), with its pad's gates and the track's finish rank bound to
    // the racer lock.
    std::vector<Route> income;
    int goal = -1;
    bool itemsanity = false;
};

static AP_VerifyOptions options() {
    AP_VerifyOptions o{};
    o.boost_mode = ctr_cfg.boost_mode;
    o.stats_mode = ctr_cfg.stats_mode;
    o.character_unlocks = ctr_cfg.character_unlocks;
    o.starting_character = ctr_cfg.starting_character;
    o.itemsanity = ctr_cfg.itemsanity;
    o.logic_difficulty = ctr_cfg.logic_difficulty;
    o.shortcut_knowledge = ctr_cfg.shortcut_knowledge;
    o.boost_blue_fire = ctr_cfg.boost_blue_fire;
    return o;
}

static int compile(Model &m, const J &n) {
    Node out{ALWAYS, 0, {}, {}};
    const std::string op = n.at("op").get<std::string>();
    if (op == "count") { out.op = COUNT; out.codes.push_back(n.at("item").get<int64_t>()); }
    else if (op == "sum" || op == "distinct") {
        out.op = op == "sum" ? SUM : DISTINCT;
        for (const auto &c : n.at("items")) out.codes.push_back(c.get<int64_t>());
    } else if (op == "checked") {
        out.op = CHECKED;
        for (const auto &c : n.at("locations")) out.codes.push_back(c.get<int64_t>());
    } else if (op == "all" || op == "any") {
        out.op = op == "all" ? ALL : ANY;
        for (const auto &t : n.at("terms")) out.kids.push_back(compile(m, t));
    } else if (op == "wumpa_banked") out.op = WUMPA;
    else if (op != "always") throw std::runtime_error("unsupported predicate " + op);
    if (n.contains("count")) out.count = n["count"].get<int>();
    m.nodes.push_back(out);
    return (int)m.nodes.size() - 1;
}

// Tracks named by the plan's `capability.finish_floors` (reviewed Royal Raceway 1.0.0):
// every finish needs the full rank whatever the descriptor states. Set by build().
static std::set<std::string> finish_floor_tracks;
// Tracks named by `capability.finish_free` (reviewed Green Hill Raceway 2.0.0): no rank for the finish itself.
static std::set<std::string> finish_free_tracks;

static void read_finish_overrides(const J &plan) {
    finish_floor_tracks.clear();
    finish_free_tracks.clear();
    if (!plan.contains("capability")) return;
    const J &caps = plan.at("capability");
    for (const auto &id : caps.value("finish_free", J::array())) finish_free_tracks.insert(id.get<std::string>());
    for (const auto &id : caps.value("finish_floors", J::array())) finish_floor_tracks.insert(id.get<std::string>());
}

// content_capability.finish_boost: the ranks crossing a track's finish line
// needs, 0 without Progressive Boost.
static int finish_boost(const J &track, const std::map<std::string, const J *> &packages, const AP_VerifyOptions &o) {
    if (!o.boost_mode) return 0;
    if (track.at("origin") == "custom" && finish_free_tracks.count(track.at("id").get<std::string>())) return 0;
    if (track.at("origin") == "custom" && finish_floor_tracks.count(track.at("id").get<std::string>())) return kUsf;
    const std::string origin = track.at("origin").get<std::string>();
    if (origin == "cortex_vortex") return kUsf;
    if (origin == "retail") {
        const int id = track.at("retail_id").get<int>();
        return id == 7 || id == 10 || (id == 13 && o.shortcut_knowledge != 2) ? kUsf : 0;
    }
    const J *pkg = packages.count(track.value("package_id", "")) ? packages.at(track.value("package_id", "")) : nullptr;
    if (pkg) {
        const std::string content = pkg->value("content_id", "");
        std::string lev;
        for (const auto &pin : pkg->at("files"))
            if (pin.value("role", "") == "lev") lev = pin.value("sha256", "");
        const int version = content_plan::state.track_ruling_version;
        const auto matches = [&](const auto &r, const std::string &revision, const std::string &pin) {
            return r.since <= version && *r.revision && content == r.content_id && revision == r.revision
                && pin == r.lev_sha256
                && !(content_plan::state.pre_sunset_finish_ruling && is_sunset_finish_ruling(r.content_id, r.revision));
        };
        const auto source = content_plan::scoped_ruling(content_plan::state, *pkg, lev,
            [&](const std::string &revision, const std::string &pin) {
                for (const auto &r : kUsfRulings) if (matches(r, revision, pin)) return true;
                return false;
            });
        int selected_rank = -1, selected_version = -1;
        for (const auto &r : kUsfRulings)
            if (matches(r, source.first, source.second) && r.since > selected_version) {
                selected_rank = r.rank;
                selected_version = r.since;
            }
        if (selected_rank >= 0) return selected_rank;
        for (const auto &r : kUsfRulings) if (!*r.revision && content == r.content_id) return r.rank;
        const std::string free_source = content_plan::ruling_pin(content_plan::state, *pkg, lev, [](const std::string &pin) {
            return pin == content_plan::kCountryPickupPin || pin == content_plan::kToastyPickupPin;
        });
        if (!free_source.empty() && content_plan::inherited_ruling_history(content_plan::state, *pkg, lev)) return 0;
        const J &caps = pkg->at("capabilities");
        // A package that does not state super_turbo_pads is treated as having one.
        if (caps.contains("super_turbo_pads") && caps["super_turbo_pads"] == false) return 0;
    }
    return kUsf;
}
static bool difficulty_track(const J &track) {
    const std::string origin = track.at("origin").get<std::string>();
    if (origin == "custom") return true;
    return origin == "retail" && AP_VerifyDifficultyTrack(track.at("retail_id").get<int>());
}
// content_capability.check_boost for a Trophy Race.
static int trophy_boost(const J &track, const std::map<std::string, const J *> &packages, const AP_VerifyOptions &o) {
    if (!o.boost_mode) return 0;
    const int floor = o.logic_difficulty < 2 && difficulty_track(track) ? kFirstBoost : 0;
    return std::max(finish_boost(track, packages, o), floor);
}
// content_cups.route_boost (own leg's Trophy rank) and prefix_boost (without it).
static int leg_boost(const J &entry, const std::string &leg_id, bool own, const std::map<std::string, const J *> &tracks,
                     const std::map<std::string, const J *> &packages, const AP_VerifyOptions &o) {
    if (!o.boost_mode) return 0;
    int need = 0;
    for (const auto &leg : entry.at("legs")) {
        const J &track = *tracks.at(leg.at("track_id").get<std::string>());
        if (leg.at("id").get<std::string>() == leg_id) return own ? std::max(need, trophy_boost(track, packages, o)) : need;
        need = std::max(need, finish_boost(track, packages, o));
    }
    throw std::runtime_error("cup leg route names no leg of its entry");
}

// Project the exported rule onto Boost with all other requirements satisfied.
// Both AP boost modes give the same threshold when every racer has the same rank.
static int exported_finish_rank(int root, std::vector<int> &ranks, const AP_WinLogicEnv *inventory = nullptr) {
    int &rank = ranks.at(root);
    if (rank >= 0) return rank;
    const auto &logic = ctr_win_logic;
    const auto &node = logic.nodes[root];
    if (node.tag == AP_WL_T_FALSE) rank = kNoFinishRank;
    else if (node.tag == AP_WL_T_CAP || node.tag == AP_WL_T_STATCAP) {
        // Probe inventories use Boost mode off to test the non-Boost part of a cap.
        rank = inventory && !AP_WinLogicEval(&logic, inventory, root, 0) ? kNoFinishRank : (int)node.a;
    }
    else if (node.tag == AP_WL_T_ALL || node.tag == AP_WL_T_ANY) {
        const bool all = node.tag == AP_WL_T_ALL;
        rank = all ? 0 : kNoFinishRank;
        for (long i = 0; i < node.b; ++i) {
            const int term = exported_finish_rank(logic.kids[node.a + i], ranks, inventory);
            rank = all ? std::max(rank, term) : std::min(rank, term);
        }
    } else rank = inventory && !AP_WinLogicEval(&logic, inventory, root, 0) ? kNoFinishRank : 0;
    return rank;
}

static bool markerless_route_ranks_match();

static bool markerless_finish_matches() {
    const auto o = options();
    if (!o.boost_mode || content_plan::state.track_ruling_version || ctr_win_logic.state != AP_WL_VALID) return true;
    const J p = J::parse(content_plan::state.canonical);
    const auto &reviews = content_plan::content_registry().at("track_ruling_revisions");
    std::map<std::string, const J *> packages;
    bool has_unreviewed_track = false;
    bool has_sunset_finish_ruling = false;
    for (const auto &pkg : p.at("packages")) {
        packages.emplace(content_plan::text(pkg.at("id")), &pkg);
        const std::string key = content_plan::text(pkg.at("content_id"));
        if (!reviews.contains(key)) continue;
        std::string lev;
        for (const auto &file : pkg.at("files"))
            if (file.at("role") == "lev") lev = content_plan::text(file.at("sha256"));
        if (!has_sunset_finish_ruling && key == "sunset-vista")
            has_sunset_finish_ruling = std::any_of(std::begin(kUsfRulings), std::end(kUsfRulings), [&](const auto &r) {
                return is_sunset_finish_ruling(r.content_id, r.revision) && pkg.at("revision") == r.revision
                    && lev == r.lev_sha256;
            });
        const auto &history = reviews.at(key);
        // Reviews added after the first catalog never affect markerless rooms.
        if (std::none_of(history.begin(), history.end(), [](const J &row) { return row.size() == 2; })) continue;
        if (std::none_of(history.begin(), history.end(), [&](const J &row) {
                return row.size() == 2 && row[0] == lev && row[1] == pkg.at("revision");
            })) has_unreviewed_track = true;
    }
    if (!has_unreviewed_track) return true;
    const auto tracks = content_plan::table(p.at("tracks"), 160);
    const auto entries = content_plan::table(p.at("entries"), 160);
    read_finish_overrides(p);
    const auto matches_export = [&] {
        std::vector<int> ranks(ctr_win_logic.node_count, -1);
        for (const auto &check : p.at("checks")) {
            const bool lap = check.at("kind") == "lap";
            if (!lap && check.at("kind") != "cup_leg") continue;
            const int index = AP_WinLogicFindCheck(&ctr_win_logic, check.at("location").get<long>());
            if (index < 0) continue;
            int expected = kNoFinishRank;
            for (const auto &route : check.at("routes")) {
                int rank = lap ? finish_boost(*tracks.at(content_plan::text(check.at("owner"))), packages, o) : 0;
                if (!route.at("leg_id").is_null())
                    rank = std::max(rank, leg_boost(*entries.at(content_plan::text(route.at("entry_id"))),
                                                   content_plan::text(route.at("leg_id")), !lap, tracks, packages, o));
                expected = std::min(expected, rank);
            }
            const int actual = exported_finish_rank(ctr_win_logic.checks[index].rule, ranks);
            if (actual < kNoFinishRank && expected != actual) return false;
        }
        return markerless_route_ranks_match();
    };
    if (matches_export()) return true;
    if (!has_sunset_finish_ruling) return false;
    // The pinned 1.2.0 package was playable before its free-finish ruling.
    // Check its previous rules across the whole room, including cup routes,
    // and keep the matching interpretation for the verifier's later sweep.
    content_plan::state.pre_sunset_finish_ruling = true;
    if (matches_export()) return true;
    content_plan::state.pre_sunset_finish_ruling = false;
    return false;
}

static int lock_of(const J &pad) { return pad.at("racer_lock").is_null() ? -1 : pad["racer_lock"].get<int>(); }

static bool exported_mask_term(int root) {
    const auto &logic = ctr_win_logic;
    if (root < 0 || root >= logic.node_count) return false;
    const auto &node = logic.nodes[root];
    if (node.tag == AP_WL_T_ITEMS_SUM) return node.a == 1 && node.c == 1 && logic.ids[node.b] == kMaskItem;
    if (node.tag == AP_WL_T_ALL || node.tag == AP_WL_T_ANY)
        for (long i = 0; i < node.b; ++i)
            if (exported_mask_term(logic.kids[node.a + i])) return true;
    return false;
}

static Model build() {
    using content_plan::state;
    const AP_VerifyOptions o = options();
    const J p = J::parse(state.canonical);
    read_finish_overrides(p);
    Model m;
    m.epoch = content_plan::epoch;
    m.parse_revision = s_seedcfg_parse_revision;
    const J *gc = p.contains("global_checks") ? &p["global_checks"] : nullptr;
    m.itemsanity = gc && gc->value("itemsanity", false);
    std::map<std::string, int> req;
    for (const auto &r : p.at("requirements")) req[r.at("id").get<std::string>()] = compile(m, r.at("predicate"));
    std::map<std::string, const J *> pads, tracks, packages, entries;
    for (const auto &x : p.at("pads")) pads[x.at("id").get<std::string>()] = &x;
    for (const auto &x : p.at("tracks")) tracks[x.at("id").get<std::string>()] = &x;
    for (const auto &x : p.at("packages")) packages[x.at("id").get<std::string>()] = &x;
    for (const auto &x : p.at("entries")) entries[x.at("id").get<std::string>()] = &x;
    const bool repeatable_pickups = content_plan::repeatable_pickups(p);
    // Match the generator's Tizi Madness gates throughout the sweep so a
    // blocked check cannot supply its own Mask.
    std::set<std::string> masked_regions, masked_cups, masked_cup_starts, masked_legs;
    if (m.itemsanity) {
        const auto tizi_tracks = content_plan::tizi_mask_tracks(p);
        if (!tizi_tracks.empty()) {
            for (const auto &[id, entry] : entries) {
                if (!entry->contains("legs")) {
                    if (tizi_tracks.count(entry->value("track_id", ""))) masked_regions.insert(id);
                    continue;
                }
                const auto &legs = entry->at("legs");
                bool mask = false;
                for (const auto &leg : legs) {
                    mask = mask || tizi_tracks.count(leg.at("track_id").get<std::string>()) != 0;
                    if (mask) masked_legs.insert(leg.at("id").get<std::string>());
                }
                if (mask) masked_cups.insert(id);
                if (!legs.empty() && masked_legs.count(legs.front().at("id").get<std::string>()))
                    masked_cup_starts.insert(id);
            }
        }
    }
    auto needs_mask = [&](const J &route) {
        if (masked_cups.empty()) return false;
        return route.at("leg_id").is_null() ? masked_cups.count(route.at("entry_id").get<std::string>()) != 0
                                            : masked_legs.count(route.at("leg_id").get<std::string>()) != 0;
    };
    // Tizi Madness was supported before its Mask ruling. Without the new
    // pickup feature, distinguish those older rooms by their carried terms.
    if (!repeatable_pickups && !content_plan::has_feature(p, content_plan::kTiziMaskFeature) &&
        (!masked_regions.empty() || !masked_cups.empty())) {
        bool carried = false;
        if (ctr_win_logic.state == AP_WL_VALID) {
            for (const auto &check : p.at("checks")) {
                if (check.at("kind") == "itemsanity" || check.at("kind") == "hit") continue;
                const auto &routes = check.at("routes");
                if (std::none_of(routes.begin(), routes.end(), [&](const J &route) {
                        return needs_mask(route) || masked_regions.count(route.at("entry_id").get<std::string>()) != 0;
                    })) continue;
                const int i = AP_WinLogicFindCheck(&ctr_win_logic, check.at("location").get<long>());
                if (i < 0) continue;
                const auto &logic = ctr_win_logic.checks[i];
                if (exported_mask_term(logic.rule) || exported_mask_term(ctr_win_logic.region_root[logic.region])) {
                    carried = true;
                    break;
                }
            }
        }
        if (!carried) {
            masked_regions.clear();
            masked_cups.clear();
            masked_cup_starts.clear();
            masked_legs.clear();
        }
    }

    // Regions by entry id: a pad's entrance requirement, its first stage-1
    // gate and its racer lock; an encounter's garage entrance and requirement.
    std::map<std::string, int> region;
    std::map<std::string, std::map<std::string, int>> stage2; // pad id -> mode -> requirement
    for (const auto &pad : p.at("pads")) {
        std::map<std::string, int> &later = stage2[pad.at("id").get<std::string>()];
        Region r;
        r.gates.push_back(req.at(pad.at("entrance_req").get<std::string>()));
        bool first = true;
        for (const auto &g : pad.at("mode_gates")) {
            if (g.at("stage") == 1 && first) { r.gates.push_back(req.at(g.at("requirement_id").get<std::string>())); first = false; }
            if (g.at("stage") == 2) later[g.at("mode").get<std::string>()] = req.at(g.at("requirement_id").get<std::string>());
        }
        r.lock = lock_of(pad);
        if (pad.at("entry_id").is_null()) continue;
        const auto entry_id = pad.at("entry_id").get<std::string>();
        r.mask = masked_regions.count(entry_id) != 0;
        region[entry_id] = (int)m.regions.size();
        m.regions.push_back(r);
    }
    std::map<int64_t, int> encounter_of_code;
    const J &goal_node = p.at("goal");
    bool wumpa_goal = goal_node.at("op") == "wumpa_banked";
    if (goal_node.at("op") == "all")
        for (const auto &term : goal_node.at("terms")) wumpa_goal = wumpa_goal || term.at("op") == "wumpa_banked";
    for (const auto &enc : p.at("encounters")) {
        Region r;
        r.gates.push_back(req.at(pads.at(enc.at("pad_id").get<std::string>())->at("entrance_req").get<std::string>()));
        r.gates.push_back(req.at(enc.at("requirement_id").get<std::string>()));
        // With the Wumpa goal, the N. Oxide race that finishes the seed also
        // needs Wumpa Income (content_bosses.install).
        r.income = wumpa_goal && p.contains("bosses") && enc.at("completion_location") ==
                   (p.at("bosses").at("oxide_goal") == 1 ? 35011104 : p.at("bosses").at("oxide_goal") == 2 ? 35011105 : 0);
        const auto entry_id = enc.at("entry_id").get<std::string>();
        r.mask = masked_regions.count(entry_id) != 0;
        region[entry_id] = (int)m.regions.size();
        m.regions.push_back(r);
        encounter_of_code[enc.at("completion_location").get<int64_t>()] = (int)m.encounter_loc.size();
        m.encounter_loc.push_back(-1);
    }
    auto region_of = [&](const std::string &entry) { return region.count(entry) ? region.at(entry) : -1; };
    // The first race won rule (`pad_requirements_v1`): a later race counts once
    // a check of the pad's first mode is reached. content_plan.create_regions.
    std::map<std::string, std::string> first_mode;
    std::map<std::string, std::vector<int64_t>> first_checks;
    // A ctr_first pad: its Trophy Race check proves the win, and the CTR Token
    // check needs that win too (content_plan.first_race_checks and merged_race).
    std::map<std::string, std::vector<int64_t>> merged;
    for (const auto &c : p.at("checks"))
        if (c.at("kind") == "trophy" && c.at("routes").at(0).at("mode") == "ctr")
            merged[c.at("routes").at(0).at("own_pad_id").get<std::string>()].push_back(c.at("location").get<int64_t>());
    if (p.contains("pad_requirements"))
        for (const auto &pad : p.at("pads"))
            if (pad.at("mode_gates").size() > 1) first_mode[pad.at("id").get<std::string>()] = pad["mode_gates"][0].at("mode").get<std::string>();
    for (const auto &c : p.at("checks")) {
        const auto &rs = c.at("routes");
        if (rs.empty()) continue;
        const auto own = first_mode.find(rs.at(0).at("own_pad_id").get<std::string>());
        if (own != first_mode.end() && c.at("kind") == (merged.count(own->first) ? "trophy" : own->second) &&
            std::all_of(rs.begin(), rs.end(), [&](const J &r) { return r.at("own_pad_id") == rs.at(0).at("own_pad_id"); }))
            first_checks[own->first].push_back(c.at("location").get<int64_t>());
    }
    auto first_of = [&](const J &route) {
        const std::string id = route.at("own_pad_id").get<std::string>();
        const auto own = first_mode.find(id);
        return own == first_mode.end() || own->second == route.at("mode") || !first_checks.count(id)
                   ? std::vector<int64_t>() : first_checks.at(id);
    };
    auto stage_of = [&](const J &route) {
        const auto &later = stage2[route.at("own_pad_id").get<std::string>()];
        const auto it = later.find(route.at("mode").get<std::string>());
        return it == later.end() ? -1 : it->second;
    };

    // Lettersanity items (content_plan.letter_requirements): a CTR Challenge
    // needs every pooled letter item of its track, a mode 2 letter check its own.
    std::map<int64_t, std::vector<int64_t>> letters;
    if (state.families.lettersanity == 2 || state.families.lettersanity == 3)
        for (const auto &pad : state.pads) {
            if (!pad.occupied || !pad.ctr_location) continue;
            for (int i = 0; i < 3; ++i) {
                if (pad.letter_items[i] <= 0) continue;
                letters[pad.ctr_location].push_back(pad.letter_items[i]);
                if (pad.letter_locations[i] > 0) letters[pad.letter_locations[i]].push_back(pad.letter_items[i]);
            }
        }
    // The Tiger Temple stone door (content_global_checks.tiger_temple_door).
    std::set<int64_t> door;
    std::set<int64_t> present;
    for (const auto &c : p.at("checks")) present.insert(c.at("location").get<int64_t>());
    if (m.itemsanity) {
        for (const auto &pad : state.pads) {
            if (!pad.occupied || pad.cortex || pad.retail_id != kTigerTemple || pad.mode_count == 0) continue;
            const int64_t letter_r = pad.letter_locations[2];
            if (letter_r > 0 && present.count(letter_r)) door.insert(letter_r);
            if (pad.ctr_location > 0 && present.count(pad.ctr_location) &&
                (state.families.lettersanity != 2 || (letter_r > 0 && present.count(letter_r))))
                door.insert(pad.ctr_location);
            for (int b = 0; b < pad.box_count; ++b)
                if ((pad.box_spots ? pad.box_number[b] == 5 : b == 4) &&
                    pad.box_locations[b] > 0 && present.count(pad.box_locations[b])) door.insert(pad.box_locations[b]);
        }
        // Selected ordinary pickups share the exact catalog door flags used
        // by the admitted plan. Their rank alone does not open the door.
        const auto sanity = state.sanity.find("retail:4");
        if (sanity != state.sanity.end()) {
            static const J needs = J::parse(kContentRegistryJSON).at("sanity").at("retail:4").at("needs");
            static const char *const kinds[] = {"weapon_crate", "wumpa_crate", "wumpa_fruit"};
            static const int budgets[] = {80, 16, 32};
            for (int family = 0; family < 3; ++family) {
                const J rules = needs.value(kinds[family], J::object());
                for (int n = 0; n < budgets[family]; ++n) {
                    const int64_t code = sanity->second.locations[family][n];
                    if (code > 0 && present.count(code) &&
                        rules.value(std::to_string(n + 1), J::object()).value("door", false)) door.insert(code);
                }
            }
        }
    }

    // Wumpa Income routes (J11): every Trophy Race and CTR Challenge of a
    // track entry on a retail race track 0..15 or a custom track with measured
    // Wumpa capability (apworld content_wumpa.income_track), in its pad's
    // region.
    for (const auto &pad : p.at("pads")) {
        if (pad.at("entry_id").is_null() || pad.at("kind") != "normal") continue;
        const J &entry = *entries.at(pad["entry_id"].get<std::string>());
        if (entry.at("kind") != "track") continue;
        const J &track = *tracks.at(entry.at("track_id").get<std::string>());
        const std::string origin = track.at("origin").get<std::string>();
        const J *pkg = packages.count(track.value("package_id", "")) ? packages.at(track.value("package_id", "")) : nullptr;
        const bool banks = origin == "retail" ? track.at("retail_id").get<int>() <= 15
                                              : origin == "custom" && pkg && pkg->at("capabilities").value("wumpa", false);
        if (!banks) continue;
        for (const auto &mode : entry.at("modes")) {
            if (mode != "trophy" && mode != "ctr") continue;
            Route R;
            R.region = region_of(entry.at("id").get<std::string>());
            const auto &later = stage2[pad.at("id").get<std::string>()];
            const auto it = later.find(mode.get<std::string>());
            R.stage = it == later.end() ? -1 : it->second;
            R.lock = lock_of(pad);
            R.need = finish_boost(track, packages, o);
            R.first = first_of(J{{"own_pad_id", pad.at("id")}, {"mode", mode}});
            m.income.push_back(R);
        }
    }
    // A plan with no such race banks in its cup legs (A3-7, apworld
    // content_wumpa.cup_income_routes): every leg on a track that banks, in
    // its cup's region, needing the finish of every leg up to its own.
    if (m.income.empty())
        for (const auto &pad : p.at("pads")) {
            if (pad.at("entry_id").is_null() || pad.at("kind") != "normal") continue;
            const J &entry = *entries.at(pad["entry_id"].get<std::string>());
            if (entry.at("kind") != "cup") continue;
            for (const auto &leg : entry.at("legs")) {
                const J &track = *tracks.at(leg.at("track_id").get<std::string>());
                const std::string origin = track.at("origin").get<std::string>();
                const J *pkg = packages.count(track.value("package_id", "")) ? packages.at(track.value("package_id", "")) : nullptr;
                const bool banks = origin == "retail" ? track.at("retail_id").get<int>() <= 15
                                                      : origin == "custom" && pkg && pkg->at("capabilities").value("wumpa", false);
                if (!banks) continue;
                Route R;
                R.region = region_of(entry.at("id").get<std::string>());
                R.lock = lock_of(pad);
                R.need = std::max(finish_boost(track, packages, o),
                                  leg_boost(entry, leg.at("id").get<std::string>(), false, tracks, packages, o));
                R.mask = masked_legs.count(leg.at("id").get<std::string>()) != 0;
                m.income.push_back(R);
            }
        }
    for (const auto &c : p.at("checks")) {
        Loc L;
        L.code = c.at("location").get<int64_t>();
        const std::string kind = c.at("kind").get<std::string>();
        const J &routes = c.at("routes");
        if (kind == "wumpa_shop") {
            // No route: its hub's Key floor (N. Sanity Beach 0, Gem Stone
            // Valley and The Lost Ruins 1, Glacier Park 2, Citadel City 3) and
            // Wumpa Income.
            static const std::map<std::string, int> floors = {{"hub:26", 0}, {"hub:25", 1}, {"hub:27", 1}, {"hub:28", 2}, {"hub:29", 3}};
            L.kind = SHOP;
            // Logical farming budget (paired with wumpa_shop.logic_key_floor).
            // Native affordability stays wallet/lifetime-based, so farming
            // ahead remains possible without making it expected early logic.
            const int amount = std::max(c.at("price").get<int>(), c.at("unlock_banked").get<int>());
            int budget_floor = 0; // Old rooms retain their original income-only logic.
            if (p.at("wumpa_shop").contains("logic_scale")) {
                const int scale = p.at("wumpa_shop").at("logic_scale").get<int>();
                budget_floor = amount * 100 <= 100 * scale ? 0 : amount * 100 <= 300 * scale ? 1
                    : amount * 100 <= 600 * scale ? 2 : amount * 100 <= 900 * scale ? 3 : 4;
            }
            L.target = std::max(floors.at(c.at("owner").get<std::string>()), budget_floor);
            m.locs.push_back(L);
            continue;
        }
        const J &first = routes.at(0);
        const auto boost = state.boosts.find(L.code);
        L.boost = boost == state.boosts.end() ? 0 : boost->second;
        L.stats = state.stat_checks.count(L.code) != 0;
        L.lock = lock_of(*pads.at(first.at("own_pad_id").get<std::string>()));
        L.door = door.count(L.code) != 0;
        if (letters.count(L.code)) L.letters = letters.at(L.code);
        // content_capability.weapon_arm: only with Itemsanity, on a track
        // check whose rank is the first boost and not a finish term.
        const J *track = tracks.count(c.at("owner").get<std::string>()) ? tracks.at(c["owner"].get<std::string>()) : nullptr;
        if (m.itemsanity && track && L.boost == kFirstBoost && o.boost_mode && !finish_boost(*track, packages, o)) {
            const std::string award = first.at("award").get<std::string>();
            const bool gated = difficulty_track(*track);
            if (kind == "trophy" && o.logic_difficulty < 2 && gated) { L.arm = ARM_FAMILIES; L.arm_families = AP_VF_DIFFICULTY_WEAPON_FAMILY_MIN; }
            else if (kind == "podium" && award == "held_1st" && o.logic_difficulty < 2) {
                L.arm = ARM_FAMILIES;
                L.arm_families = o.logic_difficulty == 0 && gated ? AP_VF_DIFFICULTY_WEAPON_FAMILY_MIN : 1;
            } else if (kind == "podium" && award == "finish_podium" && o.logic_difficulty == 0 && gated) {
                L.arm = ARM_FAMILIES; L.arm_families = AP_VF_DIFFICULTY_WEAPON_FAMILY_MIN;
            } else if (kind == "letter" && track->at("origin") == "retail" && track->at("retail_id") == kPapusPyramid &&
                       (award == "letter_c" || award == "letter_t"))
                L.arm = ARM_PAPU;
        }
        if (kind == "boss_race" || kind == "boss_cup") {
            L.kind = ENCOUNTER;
            const int e = encounter_of_code.at(L.code);
            L.region = region_of(p.at("encounters").at(e).at("entry_id").get<std::string>());
            L.mask = needs_mask(first);
            m.encounter_loc[e] = (int)m.locs.size();
            m.locs.push_back(L);
            continue;
        }
        if (kind == "cup_leg") {
            L.kind = LEG;
            for (const auto &r : routes) {
                Route R;
                R.region = region_of(r.at("entry_id").get<std::string>());
                R.lock = lock_of(*pads.at(r.at("own_pad_id").get<std::string>()));
                R.need = leg_boost(*entries.at(r["entry_id"].get<std::string>()), r.at("leg_id").get<std::string>(), true,
                                   tracks, packages, o);
                R.mask = needs_mask(r);
                L.routes.push_back(R);
            }
            m.locs.push_back(L);
            continue;
        }
        // Ordinary pickup sanity, plus Reach 10 Wumpa and rungs with a cup leg or boss race route (row 10i,
        // content_plan.create_regions): any route. A pad route keeps its
        // stage gate and first race; a cup route needs the finish of every
        // earlier leg; a boss route its encounter. The check's rank is on top,
        // bound to the route pad's racer lock.
        if (kind == "weapon_crate" || kind == "wumpa_crate" || kind == "wumpa_fruit" ||
            ((kind == "wumpa" || kind == "podium") &&
             std::any_of(routes.begin(), routes.end(), [](const J &r) { return !r.at("leg_id").is_null() || !r.at("encounter_id").is_null(); }))) {
            L.kind = RACE;
            for (const auto &r : routes) {
                if (repeatable_pickups && kind != "podium" && !content_plan::repeatable_pickup_route(r)) continue;
                Route R;
                R.region = region_of(r.at("entry_id").get<std::string>());
                R.lock = lock_of(*pads.at(r.at("own_pad_id").get<std::string>()));
                R.mask = needs_mask(r);
                R.need = L.boost;
                if (!r.at("leg_id").is_null())
                    R.need = std::max(R.need, leg_boost(*entries.at(r["entry_id"].get<std::string>()),
                                                        r.at("leg_id").get<std::string>(), false, tracks, packages, o));
                else if (r.at("encounter_id").is_null()) { R.stage = stage_of(r); R.first = first_of(r); }
                L.routes.push_back(R);
            }
            m.locs.push_back(L);
            continue;
        }
        // Complete One Lap (#344, content_plan.create_regions): any route; a
        // standalone route needs its mode's stage-2 gate and the track's
        // finish rank, a cup route the finish of every leg up to its own, a
        // boss race its encounter and the finish rank.
        if (kind == "lap") {
            L.kind = LAP;
            for (const auto &r : routes) {
                Route R;
                R.region = region_of(r.at("entry_id").get<std::string>());
                R.lock = lock_of(*pads.at(r.at("own_pad_id").get<std::string>()));
                R.need = track ? finish_boost(*track, packages, o) : 0;
                R.mask = needs_mask(r);
                if (!r.at("leg_id").is_null())
                    R.need = std::max(R.need, leg_boost(*entries.at(r["entry_id"].get<std::string>()),
                                                        r.at("leg_id").get<std::string>(), false, tracks, packages, o));
                else if (r.at("encounter_id").is_null()) R.stage = stage_of(r);
                L.routes.push_back(R);
            }
            m.locs.push_back(L);
            continue;
        }
        std::set<std::string> pads_of;
        for (const auto &r : routes) pads_of.insert(r.at("own_pad_id").get<std::string>());
        // Itemsanity and Hit checks keep their own rule also when every route
        // is on one pad (apworld content_plan.create_regions, A3-7).
        if (pads_of.size() == 1 && kind != "itemsanity" && kind != "hit") {
            L.kind = PAD;
            const J &pad = *pads.at(*pads_of.begin());
            L.region = pad.at("entry_id").is_null() ? -1 : region_of(pad["entry_id"].get<std::string>());
            L.mask = std::all_of(routes.begin(), routes.end(), needs_mask);
            std::vector<int> later;
            for (const auto &r : routes) later.push_back(stage_of(r));
            if (std::all_of(later.begin(), later.end(), [](int s) { return s >= 0; }))
                for (int s : later) if (std::find(L.stages.begin(), L.stages.end(), s) == L.stages.end()) L.stages.push_back(s);
            if (std::all_of(routes.begin(), routes.end(), [&](const J &r) { return !first_of(r).empty(); })) L.first = first_of(routes.at(0));
            m.locs.push_back(L);
            continue;
        }
        L.kind = kind == "itemsanity" ? ITEMSANITY : kind == "hit" ? HIT : SHARED;
        for (const auto &r : routes) {
            Route R;
            R.region = region_of(r.at("entry_id").get<std::string>());
            R.stage = stage_of(r);
            R.first = first_of(r);
            const auto won = merged.find(r.at("own_pad_id").get<std::string>());
            if (kind == "ctr" && won != merged.end()) R.first = won->second;
            R.lock = lock_of(*pads.at(r.at("own_pad_id").get<std::string>()));
            const std::string mode = r.at("mode").get<std::string>();
            R.boss = mode == "boss_race" || mode == "boss_cup";
            R.mask = L.kind == HIT && R.boss ? masked_cup_starts.count(r.at("entry_id").get<std::string>()) != 0
                                              : needs_mask(r);
            if ((L.kind == HIT || L.kind == ITEMSANITY) && mode == "cup")
                R.need = leg_boost(*entries.at(r["entry_id"].get<std::string>()), r.at("leg_id").get<std::string>(), false,
                                   tracks, packages, o);
            L.routes.push_back(R);
        }
        if (L.kind == ITEMSANITY) {
            content_plan::need(L.code >= kItemsanityFirst && L.code <= kItemsanityLast, "itemsanity check outside its block");
            L.weapon = (int)((L.code - kItemsanityFirst) / 2);
        }
        if (L.kind == HIT) {
            L.target = (int)(L.code - kHitFirst);
            content_plan::need(L.target >= 0 && L.target < 16, "hit check outside its block");
            if (L.target >= kFirstGuest && ctr_cfg.hit.valid) {
                const ctr_hit_trigger &t = ctr_cfg.hit.triggers[L.target - kFirstGuest];
                if (!t.fallback_keys)
                    for (int k = 0; k < t.count && k < CTR_CFG_HIT_TRIGGER_MAX; ++k)
                        if (present.count(t.any_of[k])) L.triggers.push_back(t.any_of[k]);
            }
            // The roster swap: a default racer drawn as a boss waits for that
            // boss race like a guest does.
            if (L.target < kFirstGuest && ctr_cfg.hit.valid)
                for (int k = 0; k < AP_HIT_BOSS_CODES; ++k)
                    if (ctr_cfg.hit.boss_identity[k] == L.target && present.count(AP_HIT_BOSS_WIN_CODES[k]))
                        L.triggers.push_back(AP_HIT_BOSS_WIN_CODES[k]);
        }
        m.locs.push_back(L);
    }
    m.goal = compile(m, p.at("goal"));
    std::set<int64_t> items;
    for (int i = 0; i < AP_VF_ITEM_COUNT; ++i) items.insert(kItemBase + i);
    for (const auto &row : p.at("items").at("rows")) items.insert(row.at("item").get<int64_t>());
    m.items.assign(items.begin(), items.end());
    return m;
}

struct Sweep {
    const Model &m;
    AP_VerifyOptions o;
    std::map<int64_t, int> held;
    int dense[AP_VF_ITEM_COUNT] = {};
    // Per location: `done` counted and banked (reached, or checked at the
    // start); `reached` its rule met in this sweep, which a checked location
    // alone does not prove. Per region: reachable.
    std::vector<char> done, reached, open;
    std::vector<char> event;          // per encounter: companion event held
    Sweep(const Model &model) : m(model), o(options()), done(model.locs.size()), reached(model.locs.size()),
                                open(model.regions.size()),
                                event(model.encounter_loc.size()) {}
    int count(int64_t code) const { const auto it = held.find(code); return it == held.end() ? 0 : it->second; }
    void bank(int64_t code, int n) {
        if (code <= 0 || n <= 0) return;
        held[code] += n;
        if (code >= kItemBase && code < kItemBase + AP_VF_ITEM_COUNT) dense[code - kItemBase] += n;
    }
    bool won(int64_t completion) const {
        for (size_t e = 0; e < m.encounter_loc.size(); ++e)
            if (m.encounter_loc[e] >= 0 && m.locs[m.encounter_loc[e]].code == completion) return event[e];
        return false;
    }
    bool eval(int n) const {
        const Node &x = m.nodes[n];
        int sum = 0;
        switch (x.op) {
        case ALWAYS: return true;
        case COUNT: return count(x.codes[0]) >= x.count;
        case SUM: for (auto c : x.codes) sum += count(c); return sum >= x.count;
        case DISTINCT: for (auto c : x.codes) sum += count(c) > 0; return sum >= x.count;
        case CHECKED: for (auto c : x.codes) sum += won(c); return sum >= x.count;
        case ALL: for (int k : x.kids) if (!eval(k)) return false; return true;
        case ANY: for (int k : x.kids) if (eval(k)) return true; return false;
        case WUMPA: return income();
        }
        return false;
    }
    bool region(int r) const { return r >= 0 && open[r]; }
    // A check of the pad's first mode was reached in this sweep.
    bool first_won(const std::vector<int64_t> &codes) const {
        if (codes.empty()) return true;
        for (size_t i = 0; i < m.locs.size(); ++i)
            if (reached[i] && std::find(codes.begin(), codes.end(), m.locs[i].code) != codes.end()) return true;
        return false;
    }
    bool income() const {
        for (const auto &r : m.income)
            if (route(r) && (!r.need || AP_VerifyCapabilityGate(&o, dense, r.lock, r.need, 0)))
                return true;
        return false;
    }
    bool mask_gate(bool required) const { return !required || AP_VerifyWeaponOwned(dense, kMask); }
    bool route(const Route &r) const {
        return region(r.region) && mask_gate(r.mask) && (r.stage < 0 || eval(r.stage)) && first_won(r.first);
    }
    // with_boost: the check's rank and stat term, bound to its racer lock,
    // or the Itemsanity weapon alternative.
    bool boost(const Loc &L) const {
        if (!L.boost && !L.stats) return true;
        if (AP_VerifyCapabilityGate(&o, dense, L.lock, L.boost, L.stats)) return true;
        if (L.arm == ARM_FAMILIES) return AP_VerifyUsefulWeaponFamilies(dense) >= L.arm_families;
        if (L.arm == ARM_PAPU) return AP_VerifyWeaponOwned(dense, kTurbo) || AP_VerifyWeaponOwned(dense, kMask);
        return false;
    }
    // with_boost for one route of a RACE check (row 10i): the route's rank on
    // its pad's racer lock; the weapon alternative only at the check's rank.
    bool route_boost(const Loc &L, const Route &r) const {
        if (!r.need && !L.stats) return true;
        if (AP_VerifyCapabilityGate(&o, dense, r.lock, r.need, L.stats)) return true;
        if (r.need != L.boost) return false;
        if (L.arm == ARM_FAMILIES) return AP_VerifyUsefulWeaponFamilies(dense) >= L.arm_families;
        if (L.arm == ARM_PAPU) return AP_VerifyWeaponOwned(dense, kTurbo) || AP_VerifyWeaponOwned(dense, kMask);
        return false;
    }
    bool letters(const Loc &L) const {
        for (auto c : L.letters) if (count(c) <= 0) return false;
        return true;
    }
    bool encounter_rule(int e) const {
        const int i = m.encounter_loc[e];
        return i >= 0 && region(m.locs[i].region) && mask_gate(m.locs[i].mask) && boost(m.locs[i]);
    }
    bool hit(const Loc &L) const {
        if (m.itemsanity && !AP_VerifyHitMethodOwned(dense)) return false;
        // A boss can be hit on entering the race. Winning it independently
        // requires the venue's finish rank and the entire boss cup's gates.
        for (const auto &r : L.routes) if (r.boss && region(r.region) && mask_gate(r.mask)) return true;
        if (L.target >= kFirstGuest && ctr_cfg.hit.valid && ctr_cfg.hit.triggers[L.target - kFirstGuest].fallback_keys) {
            if (count(kKeyItem) < ctr_cfg.hit.triggers[L.target - kFirstGuest].fallback_keys) return false;
        } else if (!L.triggers.empty()) {
            bool any = false;
            for (auto code : L.triggers)
                for (size_t j = 0; j < m.locs.size() && !any; ++j) any = m.locs[j].code == code && reached[j];
            if (!any) return false;
        }
        for (const auto &r : L.routes) {
            if (r.boss || !route(r)) continue;
            // content_global_checks._appearance: a pad locked to the target
            // proves nothing; a locked pad seats its own racer.
            if (r.lock == L.target) continue;
            for (int c = 0; c < 16; ++c) {
                if (c == L.target || (r.lock >= 0 && c != r.lock)) continue;
                if (!AP_VerifyCharacterUnlocked(&o, dense, c)) continue;
                if (!r.need || AP_VerifyCapabilityForCharacter(&o, dense, c, r.need, 0)) return true;
            }
        }
        return false;
    }
    bool reachable(const Loc &L) const {
        bool ok = false;
        switch (L.kind) {
        case PAD:
            ok = region(L.region) && mask_gate(L.mask);
            if (ok && !L.stages.empty()) {
                ok = false;
                for (int s : L.stages) ok = ok || eval(s);
            }
            ok = ok && first_won(L.first) && letters(L) && boost(L);
            break;
        case SHARED:
            for (const auto &r : L.routes) ok = ok || route(r);
            ok = ok && letters(L) && boost(L);
            break;
        case LEG:
            for (const auto &r : L.routes)
                ok = ok || (region(r.region) && mask_gate(r.mask) && (!r.need || AP_VerifyCapabilityGate(&o, dense, r.lock, r.need, 0)));
            break;
        case LAP:
            for (const auto &r : L.routes)
                ok = ok || (region(r.region) && mask_gate(r.mask) && (r.stage < 0 || eval(r.stage)) &&
                            (!r.need || AP_VerifyCapabilityGate(&o, dense, r.lock, r.need, 0)));
            break;
        case RACE:
            for (const auto &r : L.routes) ok = ok || (route(r) && route_boost(L, r));
            break;
        case ENCOUNTER:
            ok = region(L.region) && mask_gate(L.mask) && boost(L);
            break;
        case ITEMSANITY:
            ok = AP_VerifyWeaponOwned(dense, L.weapon) &&
                 (L.weapon != kTurbo || !o.boost_mode || AP_VerifyCapabilityGate(&o, dense, -1, 1, 0));
            if (ok) {
                bool any = false;
                // A cup leg route (A3-7) needs the finish of every earlier
                // leg, bound to the cup pad's racer lock.
                for (const auto &r : L.routes)
                    any = any || (route(r) && (!r.need || AP_VerifyCapabilityGate(&o, dense, r.lock, r.need, 0)));
                ok = any;
            }
            break;
        case HIT:
            ok = hit(L);
            break;
        case SHOP:
            ok = count(kKeyItem) >= L.target && income();
            break;
        }
        return ok && (!L.door || AP_VerifyTigerDoorWeapon(dense));
    }
};

static bool finish_route_open(const Sweep &s, const Route &route) {
    if (route.region < 0) return false;
    const Region &region = s.m.regions[route.region];
    const bool driveable = region.lock < 0 || !s.o.character_unlocks || region.lock == s.o.starting_character
        || s.count(AP_WL_UNLOCK_ITEM[region.lock]) > 0;
    return driveable && std::all_of(region.gates.begin(), region.gates.end(), [&](int gate) { return s.eval(gate); })
        && (route.stage < 0 || s.eval(route.stage))
        && (!(route.mask || region.mask) || s.count(kMaskItem) > 0);
}

static void finish_gate_items(const Model &model, int gate, std::set<int64_t> &items) {
    const Node &node = model.nodes[gate];
    if (node.op == COUNT || node.op == SUM || node.op == DISTINCT)
        items.insert(node.codes.begin(), node.codes.end());
    for (int child : node.kids) finish_gate_items(model, child, items);
}

static bool markerless_route_ranks_match() {
    // Test each alternative with the inventory reduced to its own gates.
    // A rank change on an early route can disappear when every route is open.
    const Model model = build();
    Sweep probe(model);
    constexpr int full = 65536;
    for (auto item : model.items) probe.held[item] = full;
    std::fill(probe.event.begin(), probe.event.end(), true);
    AP_WinLogicEnv inventory{};
    inventory.user = &probe;
    inventory.item_count = [](void *user, long code) {
        return std::max(0, static_cast<Sweep *>(user)->count(code) - AP_WinLogicStartCount(&ctr_win_logic, code));
    };
    inventory.bosses_won = [](void *) { return 6; };
    inventory.collected = [](void *, long) { return 1; };
    inventory.character_unlocks = probe.o.character_unlocks;
    inventory.starting_character = probe.o.starting_character;
    const auto &logic = ctr_win_logic;
    std::vector<int> ranks(logic.node_count, -1);
    for (const Loc &loc : model.locs) {
        if ((loc.kind != LAP && loc.kind != LEG) || loc.routes.size() < 2) continue;
        const int check_index = AP_WinLogicFindCheck(&logic, loc.code);
        if (check_index < 0) continue;
        const auto &check = logic.checks[check_index];
        std::set<int64_t> gate_items;
        for (const Route &route : loc.routes) {
            if (route.region < 0) continue;
            const auto &region = model.regions[route.region];
            for (int gate : region.gates) finish_gate_items(model, gate, gate_items);
            if (route.stage >= 0) finish_gate_items(model, route.stage, gate_items);
            if (route.mask || region.mask) gate_items.insert(kMaskItem);
            if (probe.o.character_unlocks && region.lock >= 0 && region.lock != probe.o.starting_character)
                gate_items.insert(AP_WL_UNLOCK_ITEM[region.lock]);
        }
        std::vector<int64_t> order(gate_items.begin(), gate_items.end());
        for (const Route &target : loc.routes) {
            for (auto item : order) probe.held[item] = full;
            if (!finish_route_open(probe, target)) continue;
            for (bool reverse : {false, true}) {
                if (reverse) std::reverse(order.begin(), order.end());
                for (auto item : order) {
                    int low = AP_WinLogicStartCount(&logic, item), high = full;
                    while (low < high) {
                        const int mid = low + (high - low) / 2;
                        probe.held[item] = mid;
                        if (finish_route_open(probe, target)) high = mid;
                        else low = mid + 1;
                    }
                    probe.held[item] = low;
                }
                int expected = kNoFinishRank;
                for (const Route &route : loc.routes)
                    if (finish_route_open(probe, route)) expected = std::min(expected, route.need);
                std::fill(ranks.begin(), ranks.end(), -1);
                const int actual = std::max(exported_finish_rank(check.rule, ranks, &inventory),
                    exported_finish_rank(logic.region_root[check.region], ranks, &inventory));
                if (actual < kNoFinishRank && expected != actual) return false;
                for (auto item : order) probe.held[item] = full;
                if (reverse) std::reverse(order.begin(), order.end());
            }
        }
    }
    return true;
}

static int sweep(const Model &model, const ap_content_verify_io *io, ap_content_verify_result *out) {
    Sweep s(model);
    for (auto code : model.items) s.bank(code, io->foreign(io->ctx, code));
    std::vector<char> present(model.locs.size());
    for (size_t i = 0; i < model.locs.size(); ++i) {
        int64_t own = 0;
        // Admission binds the room to the plan's checks, so every location
        // is present. One that were not would still take part in the logic,
        // but it would never be counted and would bank nothing.
        present[i] = (char)io->scout(io->ctx, model.locs[i].code, &own);
        if (!present[i]) continue;
        out->total++;
        if (io->checked(io->ctx, model.locs[i].code)) { s.done[i] = 1; s.bank(own, 1); }
    }
    for (bool progress = true; progress;) {
        progress = false;
        for (size_t r = 0; r < model.regions.size(); ++r) {
            bool ok = model.regions[r].lock < 0 || AP_VerifyCharacterUnlocked(&s.o, s.dense, model.regions[r].lock);
            for (int g : model.regions[r].gates) ok = ok && s.eval(g);
            ok = ok && s.mask_gate(model.regions[r].mask) && (!model.regions[r].income || s.income());
            s.open[r] = ok;
        }
        // The companion event (`checked` predicates): the race won in this
        // room, or its rule met now. A checked location alone is not a win.
        for (size_t e = 0; e < model.encounter_loc.size(); ++e) {
            if (s.event[e]) continue;
            const int i = model.encounter_loc[e];
            const bool won = i >= 0 && io->won && io->won(io->ctx, model.locs[i].code);
            if (won || s.encounter_rule((int)e)) { s.event[e] = 1; progress = true; }
        }
        for (size_t i = 0; i < model.locs.size(); ++i) {
            if (s.reached[i] || !s.reachable(model.locs[i])) continue;
            s.reached[i] = 1;
            progress = true;
            if (s.done[i]) continue; // checked at the start: already banked
            int64_t own = 0;
            if (present[i]) io->scout(io->ctx, model.locs[i].code, &own);
            s.done[i] = 1;
            s.bank(own, 1);
        }
    }
    for (size_t i = 0; i < model.locs.size(); ++i) {
        if (!present[i]) continue;
        // `done` includes locations reached by the sweep and those checked at start.
        if (s.done[i]) { out->reachable++; continue; }
        if (out->unreachable_count < AP_CONTENT_VERIFY_LISTED) out->unreachable[out->unreachable_count++] = model.locs[i].code;
        if (io->unreached) io->unreached(io->ctx, model.locs[i].code);
    }
    out->goal = s.eval(model.goal);
    out->keys = s.count(kKeyItem);
    return 1;
}
} // namespace content_verify

extern "C" int ap_content_plan_verify(const ap_content_verify_io *io, ap_content_verify_result *out) {
    using namespace content_verify;
    static Model model;
    static bool have = false;
    std::memset(out, 0, sizeof *out);
    if (!ap_content_plan_active() || !io) return 0;
    try {
        // The carried win logic can change even when the bound plan is identical.
        if (!have || model.epoch != content_plan::epoch || model.parse_revision != s_seedcfg_parse_revision) {
            have = false;
            model = build();
            have = true;
        }
        return sweep(model, io, out);
    } catch (const std::exception &e) {
        AP_LogLine((std::string("[AP VERIFY] content-plan sweep refused: ") + e.what() + "\n").c_str());
        std::memset(out, 0, sizeof *out);
        return 0;
    }
}
