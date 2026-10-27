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
#include <string>
#include <vector>

namespace content_verify {
using J = nlohmann::json;

// worlds/ctr/data/usf_finish_rulings.json, by package content id: 1 needs USF
// to finish, 0 takes the generic terms only. tools/test-content-verify-parity.py
// compares this table with the paired apworld's.
static const struct { const char *content_id; int usf; } kUsfRulings[] = {
    {"arabian-heights-night", 1}, {"baby-t-park", 0}, {"cortex-vortex", 1}, {"north-pole", 0}, {"tv-world", 0},
};
static const int64_t kItemBase = 35010000, kKeyItem = 35010014;
static const int64_t kItemsanityFirst = 35016000, kItemsanityLast = 35016021;
static const int64_t kHitFirst = 35025000;
static const int kFirstGuest = 8, kTigerTemple = 4, kPapusPyramid = 5;
static const int kTurbo = 0, kMask = 6; // AP_VerifyWeaponOwned indices
static const int kUsf = 2, kFirstBoost = 1;

enum Op { ALWAYS, COUNT, SUM, DISTINCT, CHECKED, ALL, ANY };
struct Node { Op op; int count; std::vector<int64_t> codes; std::vector<int> kids; };
struct Region { std::vector<int> gates; int lock = -1; };
struct Route { int region = -1, stage = -1, lock = -1, need = 0, encounter = -1; bool boss = false; };
enum Kind { PAD, SHARED, LEG, ENCOUNTER, ITEMSANITY, HIT, LAP };
enum Arm { ARM_NONE, ARM_FAMILIES, ARM_PAPU };
struct Loc {
    int64_t code = 0;
    Kind kind = PAD;
    int region = -1, lock = -1, boost = 0, arm = ARM_NONE, arm_families = 0, weapon = -1, target = -1;
    bool stats = false, door = false;
    std::vector<int> stages;          // PAD: one stage-2 gate per route, all set, else empty
    std::vector<Route> routes;        // SHARED, LEG, ITEMSANITY, HIT
    std::vector<int64_t> letters;     // Lettersanity items the check needs
    std::vector<int64_t> triggers;    // HIT: the guest's created unlock wins
};
struct Model {
    uint64_t epoch = 0;
    std::vector<Node> nodes;
    std::vector<Region> regions;
    std::vector<Loc> locs;
    std::vector<int> encounter_loc;   // encounter index -> locs index
    std::vector<int64_t> items;       // every item code the plan pools
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
    } else if (op != "always") throw std::runtime_error("unsupported predicate " + op);
    if (n.contains("count")) out.count = n["count"].get<int>();
    m.nodes.push_back(out);
    return (int)m.nodes.size() - 1;
}

// content_capability.finish_boost: the ranks crossing a track's finish line
// needs, 0 without Progressive Boost.
static int finish_boost(const J &track, const std::map<std::string, const J *> &packages, const AP_VerifyOptions &o) {
    if (!o.boost_mode) return 0;
    const std::string origin = track.at("origin").get<std::string>();
    if (origin == "cortex_vortex") return kUsf;
    if (origin == "retail") {
        const int id = track.at("retail_id").get<int>();
        return id == 7 || id == 10 || (id == 13 && o.shortcut_knowledge != 2) ? kUsf : 0;
    }
    const J *pkg = packages.count(track.value("package_id", "")) ? packages.at(track.value("package_id", "")) : nullptr;
    if (pkg) {
        const std::string content = pkg->value("content_id", "");
        for (const auto &r : kUsfRulings) if (content == r.content_id) return r.usf ? kUsf : 0;
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

static int lock_of(const J &pad) { return pad.at("racer_lock").is_null() ? -1 : pad["racer_lock"].get<int>(); }

static Model build() {
    using content_plan::state;
    const AP_VerifyOptions o = options();
    const J p = J::parse(state.canonical);
    Model m;
    m.epoch = content_plan::epoch;
    const J *gc = p.contains("global_checks") ? &p["global_checks"] : nullptr;
    m.itemsanity = gc && gc->value("itemsanity", false);
    std::map<std::string, int> req;
    for (const auto &r : p.at("requirements")) req[r.at("id").get<std::string>()] = compile(m, r.at("predicate"));
    std::map<std::string, const J *> pads, tracks, packages, entries;
    for (const auto &x : p.at("pads")) pads[x.at("id").get<std::string>()] = &x;
    for (const auto &x : p.at("tracks")) tracks[x.at("id").get<std::string>()] = &x;
    for (const auto &x : p.at("packages")) packages[x.at("id").get<std::string>()] = &x;
    for (const auto &x : p.at("entries")) entries[x.at("id").get<std::string>()] = &x;

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
        region[pad["entry_id"].get<std::string>()] = (int)m.regions.size();
        m.regions.push_back(r);
    }
    std::map<std::string, int> encounter;
    std::map<int64_t, int> encounter_of_code;
    for (const auto &enc : p.at("encounters")) {
        Region r;
        r.gates.push_back(req.at(pads.at(enc.at("pad_id").get<std::string>())->at("entrance_req").get<std::string>()));
        r.gates.push_back(req.at(enc.at("requirement_id").get<std::string>()));
        region[enc.at("entry_id").get<std::string>()] = (int)m.regions.size();
        m.regions.push_back(r);
        encounter_of_code[enc.at("completion_location").get<int64_t>()] = (int)encounter.size();
        encounter[enc.at("id").get<std::string>()] = (int)encounter.size();
    }
    m.encounter_loc.assign(encounter.size(), -1);
    auto region_of = [&](const std::string &entry) { return region.count(entry) ? region.at(entry) : -1; };
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
    if (m.itemsanity)
        for (const auto &pad : state.pads) {
            if (!pad.occupied || pad.cortex || pad.retail_id != kTigerTemple || pad.mode_count == 0) continue;
            const int64_t letter_r = pad.letter_locations[2];
            if (letter_r > 0 && present.count(letter_r)) door.insert(letter_r);
            if (pad.ctr_location > 0 && present.count(pad.ctr_location) &&
                (state.families.lettersanity != 2 || (letter_r > 0 && present.count(letter_r))))
                door.insert(pad.ctr_location);
            if (pad.box_locations[4] > 0 && present.count(pad.box_locations[4])) door.insert(pad.box_locations[4]);
        }

    for (const auto &c : p.at("checks")) {
        Loc L;
        L.code = c.at("location").get<int64_t>();
        const std::string kind = c.at("kind").get<std::string>();
        const J &routes = c.at("routes");
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
                L.routes.push_back(R);
            }
            m.locs.push_back(L);
            continue;
        }
        // Complete One Lap (#344, content_plan.create_regions): any route; a
        // standalone route needs its mode's stage-2 gate and the track's
        // finish rank, a cup route the finish of every leg up to its own.
        if (kind == "lap") {
            L.kind = LAP;
            for (const auto &r : routes) {
                Route R;
                R.region = region_of(r.at("entry_id").get<std::string>());
                R.lock = lock_of(*pads.at(r.at("own_pad_id").get<std::string>()));
                R.need = track ? finish_boost(*track, packages, o) : 0;
                if (r.at("leg_id").is_null()) R.stage = stage_of(r);
                else R.need = std::max(R.need, leg_boost(*entries.at(r["entry_id"].get<std::string>()),
                                                         r.at("leg_id").get<std::string>(), false, tracks, packages, o));
                L.routes.push_back(R);
            }
            m.locs.push_back(L);
            continue;
        }
        std::set<std::string> pads_of;
        for (const auto &r : routes) pads_of.insert(r.at("own_pad_id").get<std::string>());
        if (pads_of.size() == 1) {
            L.kind = PAD;
            const J &pad = *pads.at(*pads_of.begin());
            L.region = pad.at("entry_id").is_null() ? -1 : region_of(pad["entry_id"].get<std::string>());
            std::vector<int> later;
            for (const auto &r : routes) later.push_back(stage_of(r));
            if (std::all_of(later.begin(), later.end(), [](int s) { return s >= 0; }))
                for (int s : later) if (std::find(L.stages.begin(), L.stages.end(), s) == L.stages.end()) L.stages.push_back(s);
            m.locs.push_back(L);
            continue;
        }
        L.kind = kind == "itemsanity" ? ITEMSANITY : kind == "hit" ? HIT : SHARED;
        for (const auto &r : routes) {
            Route R;
            R.region = region_of(r.at("entry_id").get<std::string>());
            R.stage = stage_of(r);
            R.lock = lock_of(*pads.at(r.at("own_pad_id").get<std::string>()));
            const std::string mode = r.at("mode").get<std::string>();
            R.boss = mode == "boss_race" || mode == "boss_cup";
            if (R.boss) R.encounter = encounter.at(r.at("encounter_id").get<std::string>());
            if (L.kind == HIT && mode == "cup")
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
        }
        return false;
    }
    bool region(int r) const { return r >= 0 && open[r]; }
    bool route(const Route &r) const { return region(r.region) && (r.stage < 0 || eval(r.stage)); }
    // with_boost: the check's rank and stat term, bound to its racer lock,
    // or the Itemsanity weapon alternative.
    bool boost(const Loc &L) const {
        if (!L.boost && !L.stats) return true;
        if (AP_VerifyCapabilityGate(&o, dense, L.lock, L.boost, L.stats)) return true;
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
        return i >= 0 && region(m.locs[i].region) && boost(m.locs[i]);
    }
    bool hit(const Loc &L) const {
        if (m.itemsanity && !AP_VerifyHitMethodOwned(dense)) return false;
        // A boss route and an unlock win read the location's rule
        // (location.can_reach), never its checked state.
        for (const auto &r : L.routes) if (r.boss && encounter_rule(r.encounter)) return true;
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
            ok = region(L.region);
            if (ok && !L.stages.empty()) {
                ok = false;
                for (int s : L.stages) ok = ok || eval(s);
            }
            ok = ok && letters(L) && boost(L);
            break;
        case SHARED:
            for (const auto &r : L.routes) ok = ok || route(r);
            ok = ok && letters(L) && boost(L);
            break;
        case LEG:
            for (const auto &r : L.routes)
                ok = ok || (region(r.region) && (!r.need || AP_VerifyCapabilityGate(&o, dense, r.lock, r.need, 0)));
            break;
        case LAP:
            for (const auto &r : L.routes)
                ok = ok || (region(r.region) && (r.stage < 0 || eval(r.stage)) &&
                            (!r.need || AP_VerifyCapabilityGate(&o, dense, r.lock, r.need, 0)));
            break;
        case ENCOUNTER:
            ok = region(L.region) && boost(L);
            break;
        case ITEMSANITY:
            ok = AP_VerifyWeaponOwned(dense, L.weapon) &&
                 (L.weapon != kTurbo || !o.boost_mode || AP_VerifyCapabilityGate(&o, dense, -1, 1, 0));
            if (ok) {
                bool any = false;
                for (const auto &r : L.routes) any = any || route(r);
                ok = any;
            }
            break;
        case HIT:
            ok = hit(L);
            break;
        }
        return ok && (!L.door || AP_VerifyTigerDoorWeapon(dense));
    }
};

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
        if (!have || model.epoch != content_plan::epoch) { have = false; model = build(); have = true; }
        return sweep(model, io, out);
    } catch (const std::exception &e) {
        AP_LogLine((std::string("[AP VERIFY] content-plan sweep refused: ") + e.what() + "\n").c_str());
        std::memset(out, 0, sizeof *out);
        return 0;
    }
}

