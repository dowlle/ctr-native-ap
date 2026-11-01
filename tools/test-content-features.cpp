// Content plans with the Wumpa Shop, the Wumpa goal, ctr_first and the boss
// opponent and kit shuffles (J11), on the production parser and verifier.
// g++ -m32 -std=c++17 -DCTR_AP -fsanitize=undefined -fno-sanitize-recover=all -Iap -Iap/vendor/json/include tools/test-content-features.cpp ap/ap_seedcfg.cpp -o /tmp/test-content-features
//
// The plans are derived here from the checked-in apworld fixtures by the
// rules of the J11 wire spec (WIRE-SPEC.md, sections A to D), so every case
// states the spec's shape in code: a ctr_first rewrite of track-modes,
// track-checks and global-checks, a shop and goal arm added to track-checks
// and bosses, a Wumpa-only goal, and opponent and kit draws on bosses.json
// with the Hit Character block derived from them. When the apworld's own J11
// fixtures are checked in (ctr-first, wumpa-shop, wumpa-goal-only,
// boss-shuffle, all-030) they are admitted, verified and walked too.
#include "ap_seedcfg.h"
#include "ap_content_plan.h"
#include "ap_boss_venue.h"
#include "ap_wumpa_shop.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <vector>
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
static bool parse(const J &j) {
    ap_seedcfg_parse_json(j);
    return !ap_seedcfg_rejected() && ap_content_plan_present();
}
static bool admit(const J &j) {
    if (!parse(j)) return false;
    const auto ids = locations(j);
    return ap_content_plan_bind("seed-J11", 0, 1, ids.data(), ids.size()) == 1;
}
static void refuse(const J &j, const std::string &why) {
    expect(!parse(j) && !ctr_cfg_active() && !ap_content_plan_active(), "refuses: " + why);
}
static J load(const std::string &name) {
    std::ifstream in("tools/fixtures/content-plan/" + name + ".json");
    if (!in) return J();
    return J::parse(in);
}
static J &find_id(J &rows, const std::string &id) {
    for (auto &r : rows) if (r["id"] == id) return r;
    throw std::runtime_error("no record " + id);
}
static J &check_of(J &plan, int64_t code) {
    for (auto &c : plan["checks"]) if (c["location"] == code) return c;
    throw std::runtime_error("no check " + std::to_string(code));
}
static bool has_mode(const J &modes, const char *m) {
    for (const auto &x : modes) if (x == m) return true;
    return false;
}
static J all_of(const J &terms) { return terms.size() == 1 ? terms[0] : J{{"op", "all"}, {"terms", terms}}; }
// required_features: the base list, then the J11 names in their fixed order.
static void features(J &room) {
    J &p = room["content_plan"];
    static const char *const j11[4] = {"ctr_first_v1", "boss_opponents_v1", "wumpa_shop_v1", "wumpa_goal_v1"};
    J out = J::array();
    for (const auto &f : p["required_features"])
        if (std::find(std::begin(j11), std::end(j11), f.get<std::string>()) == std::end(j11)) out.push_back(f);
    bool goal = p["goal"]["op"] == "wumpa_banked";
    if (p["goal"]["op"] == "all")
        for (const auto &t : p["goal"]["terms"]) goal |= t["op"] == "wumpa_banked";
    if (p.contains("adventure_structure")) out.push_back(j11[0]);
    if (p.contains("bosses") && p["bosses"].contains("opponent_shuffle")) out.push_back(j11[1]);
    if (p.contains("wumpa_shop")) out.push_back(j11[2]);
    if (goal) out.push_back(j11[3]);
    p["required_features"] = out;
}

// ── B. ctr_first ─────────────────────────────────────────────────────────
// Every entry whose track has a Trophy Race and a CTR Challenge races
// ["ctr"] plus "relic" last; the Trophy check and the podium rungs route from
// the CTR Challenge, every other family keeps only its non-Trophy routes, and a
// box check's routes follow the new mode order. A single-mode entry has no
// stage-2 gate, so its record goes.
static J ctr_first(J room) {
    J &p = room["content_plan"];
    p["adventure_structure"] = "ctr_first";
    std::map<std::string, J> modes;
    for (auto &e : p["entries"]) {
        if (e["kind"] != "track") continue;
        const J &track = find_id(p["tracks"], e["track_id"]);
        if (!has_mode(track["modes"], "trophy") || !has_mode(track["modes"], "ctr")) continue;
        J m = J::array({"ctr"});
        if (has_mode(track["modes"], "relic")) m.push_back("relic");
        e["modes"] = m;
        modes[e["id"]] = m;
    }
    for (auto &pad : p["pads"]) {
        if (pad["entry_id"].is_null() || !modes.count(pad["entry_id"])) continue;
        const J &m = modes[pad["entry_id"]];
        const std::string first = pad["mode_gates"][0]["requirement_id"];
        std::string later;
        for (const auto &g : pad["mode_gates"]) if (g["stage"] == 2) later = g["requirement_id"];
        J gates = J::array();
        for (size_t k = 0; k < m.size(); ++k)
            gates.push_back({{"mode", m[k]}, {"stage", k && !later.empty() ? 2 : 1}, {"requirement_id", k && !later.empty() ? later : first}});
        pad["mode_gates"] = gates;
        if (m.size() == 1 && !later.empty()) {
            auto &reqs = p["requirements"];
            reqs.erase(std::remove_if(reqs.begin(), reqs.end(), [&](const J &r) { return r["id"] == later; }), reqs.end());
        }
    }
    for (auto &c : p["checks"]) {
        J routes = J::array();
        for (auto r : c["routes"]) {
            if (r["entry_id"].is_string() && modes.count(r["entry_id"]) && r["mode"] == "trophy") {
                if (c["kind"] == "trophy") { r["mode"] = "ctr"; r["award"] = "win"; }
                else if (c["kind"] == "podium") r["mode"] = "ctr";
                else continue;
            }
            routes.push_back(r);
        }
        if (c["kind"] == "box" && routes.size() > 1 && modes.count(routes[0]["entry_id"])) {
            const J &m = modes[routes[0]["entry_id"]];
            std::stable_sort(routes.begin(), routes.end(), [&](const J &a, const J &b) {
                return std::find(m.begin(), m.end(), a["mode"]) < std::find(m.begin(), m.end(), b["mode"]); });
        }
        c["routes"] = routes;
    }
    features(room);
    return room;
}

// ── C. Wumpa Shop and goal ───────────────────────────────────────────────
static const char *const kHubs[5] = {"N. Sanity Beach", "Gem Stone Valley", "Lost Ruins", "Glacier Park", "Citadel City"};
static const int kHubLevel[5] = {26, 25, 27, 28, 29};
static const int kBandLow[5] = {10, 20, 20, 30, 40};
static int scale(int v, int percent) { return std::max(1, (v * percent + 50) / 100); }
static int unlock_of(int slot, int slots, int percent) {
    const int later = slot - std::min(slots, std::max(2, (slots + 1) / 2));
    return later <= 0 ? 0 : scale(50 * later, percent);
}
static void add_shop(J &room, int slots, int prices, const char *progression, bool hints) {
    J &p = room["content_plan"];
    p["wumpa_shop"] = {{"slots", slots}, {"prices", prices}, {"progression", progression}, {"hints", hints}};
    for (int h = 0; h < 5; ++h)
        for (int n = 1; n <= slots; ++n)
            p["checks"].push_back({{"location", 35028600 + 40 * h + n - 1}, {"owner", "hub:" + std::to_string(kHubLevel[h])},
                                   {"kind", "wumpa_shop"}, {"display_name", std::string(kHubs[h]) + " Shop: Slot " + std::to_string(n)},
                                   {"routes", J::array()}, {"price", scale(kBandLow[h] + n % 7, prices)},
                                   {"unlock_banked", unlock_of(n, slots, prices)}});
    auto &items = p["items"];
    items["filler_count"] = items["filler_count"].get<int>() + 5 * slots;
    items["coded_checks"] = items["coded_checks"].get<int>() + 5 * slots;
    features(room);
}
static void add_goal(J &room, int count) {
    J &p = room["content_plan"];
    const J arm = {{"op", "wumpa_banked"}, {"count", count}};
    if (p["goal"]["op"] == "all") p["goal"]["terms"].push_back(arm);
    else p["goal"] = {{"op", "all"}, {"terms", J::array({p["goal"], arm})}};
    features(room);
}

// ── D. Boss opponent and kit shuffle ─────────────────────────────────────
// racers[g] for the garages, final_racer for both N. Oxide encounters; kits
// from `rows` (weapon, throw, path, base cooldown, juice per segment), the
// checkpoints the garage's retail starts mapped onto its venue.
struct Row { int weapon, throw_flag, path, cooldown, juice; };
static int scaled(int base, int intensity) {
    static const int num[4] = {3, 1, 3, 1}, den[4] = {2, 1, 4, 2};
    return std::max(2, std::min(255, base * num[intensity] / den[intensity]));
}
static int venue_points(const J &plan, const std::string &track) {
    if (track == "cortex_vortex") return 255;
    if (track.rfind("retail:", 0) == 0) return AP_BOSS_VENUE_RESTART_POINTS[std::stoi(track.substr(7))];
    for (const auto &t : plan["tracks"])
        if (t["id"] == track)
            for (const auto &pk : plan["packages"]) if (pk["id"] == t["package_id"]) return pk["capabilities"]["checkpoints"].get<int>();
    throw std::runtime_error("no venue " + track);
}
static J kit_schedule(int g, int points, const std::vector<Row> &rows, int intensity) {
    J out = J::array();
    const int home_last = g < 4 ? AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_GARAGE[g].homeLevel] - 1 : 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const int cp = g < 4 ? AP_BossVenueCheckpoint(AP_BOSS_VENUE_GARAGE[g].attack[i], home_last, points - 1) : AP_BOSS_OXIDE_CHECKPOINTS[i];
        out.push_back(J::array({cp, rows[i].throw_flag, rows[i].weapon, rows[i].path, scaled(rows[i].cooldown, intensity), rows[i].juice}));
    }
    return out;
}
static const Row kBomb{0x01, 3, 1, 0x0F, 0}, kBombB{0x01, 3, 1, 0x0C, 0}, kMissile{0x02, 3, 1, 0x0F, 0}, kTnt{0x03, 3, 0, 0x0F, 0},
    kPotion{0x66, 2, 1, 0x0C, 2}, kShield{0x06, 3, 1, 0x0C, 0}, kTurbo{0x00, 3, 1, 0x0C, 0}, kSuper{0x0D, 3, 1, 0x0C, 0},
    kNone{0x0F, 3, 1, 0x0F, 0};
static const std::vector<Row> kKits[4] = {{kBomb, kMissile, kBomb, kMissile, kBomb}, {kTnt, kPotion, kTnt, kPotion, kTnt},
                                          {kShield, kMissile, kShield, kMissile, kShield}, {kTurbo, kBombB, kTurbo, kBombB, kTurbo}};
static std::vector<Row> oxide_rows() {
    std::vector<Row> r;
    for (int i = 0; i < AP_BOSS_FINAL_SEGMENTS; ++i) r.push_back(i % 2 ? kSuper : kBombB);
    return r;
}
static const int kContentFallback[8] = {4, 2, 1, 3, 3, 2, 1, 4};
// The Hit Character block on a content plan (decision 9): `bosses` names the
// racer each boss race seats; a guest's any_of is its retail track wins then
// every boss code seating it; kind boss with a boss code, else track, else the
// retail kind; the content-plan Keys when none of its wins is a plan check.
static void derive_hit(J &room, const int ident[6]) {
    static const int64_t codes[6] = {35011100, 35011101, 35011102, 35011103, 35011104, 35011105};
    static const int retail[6] = {10, 9, 11, 8, 15, 15};
    static const std::vector<int64_t> track_wins[8] = {{}, {}, {}, {}, {35016200, 35016201}, {35011008, 35011010}, {35011000, 35011003}, {}};
    J &h = room["hit_character_encounters"];
    bool shuffled = false;
    for (int i = 0; i < 6; ++i) { h["bosses"][std::to_string(codes[i])] = ident[i]; shuffled |= ident[i] != retail[i]; }
    if (!shuffled) return;
    std::set<int64_t> plan;
    for (const auto &c : room["content_plan"]["checks"]) plan.insert(c["location"].get<int64_t>());
    for (int guest = 8; guest < 16; ++guest) {
        J any = J::array();
        bool boss = false, present = false;
        for (auto w : track_wins[guest - 8]) any.push_back(w);
        for (int i = 0; i < 6; ++i) if (ident[i] == guest) { any.push_back(codes[i]); boss = true; }
        for (const auto &w : any) present |= plan.count(w.get<int64_t>()) != 0;
        J t = {{"kind", boss ? "boss" : !track_wins[guest - 8].empty() ? "track" : "boss"}, {"any_of", any}};
        if (!present) t["fallback_keys"] = kContentFallback[guest - 8];
        h["unlock_triggers"][std::to_string(guest)] = t;
    }
}
struct Shuffle {
    bool opponent = true, oxide_pool = true, kit = true, all_compatible = true, home = false;
    int racers[4] = {0, 12, 13, 15}, final_racer = 3, intensity = 2;
    const char *assignment = "mixed_loadouts";
};
static int home_points(int g) { return AP_BOSS_VENUE_RESTART_POINTS[AP_BOSS_VENUE_GARAGE[g].homeLevel]; }
static J boss_shuffle(J room, const Shuffle &s) {
    static const char *const intensities[4] = {"relaxed", "standard", "intense", "mayhem"};
    static const int retail[6] = {10, 9, 11, 8, 15, 15};
    J &p = room["content_plan"];
    J &b = p["bosses"];
    b["opponent_shuffle"] = s.opponent;
    b["oxide_in_pool"] = s.oxide_pool;
    b["kit_shuffle"] = s.kit;
    b["gauntlet"] = false;
    b["kit"] = s.kit ? J{{"assignment", s.assignment}, {"intensity", intensities[s.intensity]}, {"loss_easing", false}} : J();
    b["oxide_attack_profile"] = s.all_compatible ? "all_compatible" : "retail";
    b["track_assignment"] = s.home ? "character_home_track" : "independent_track";
    int ident[6];
    for (int k = 0; k < 6; ++k) {
        const int g = k < 4 ? k : 4;
        const bool oxide = g == 4;
        const int racer = oxide ? (s.oxide_pool ? s.final_racer : 15) : (s.opponent ? s.racers[g] : retail[k]);
        ident[k] = racer;
        J &e = find_id(p["entries"], "entry/boss:" + std::to_string(g) + "/" + std::to_string(k == 5 ? 1 : 0));
        if (s.home && !oxide) {
            static const int home[16] = {3, 10, 15, 14, 11, 0, 12, 4, 7, 5, 6, 1, 17, 2, 8, 13};
            e["track_id"] = "retail:" + std::to_string(home[racer]);
        }
        J opp = {{"role", "opponent"}, {"slot", 1}, {"character_id", racer}, {"driving_profile", "retail"}, {"attack_profile", "inherited"}};
        const bool composed = oxide ? s.all_compatible : s.kit;
        if (composed) {
            opp["attack_profile"] = "composed";
            const int intensity = s.kit ? s.intensity : 1;
            if (oxide) opp["schedule"] = kit_schedule(4, 0, oxide_rows(), intensity);
            else if (std::string(s.assignment) == "signature_profiles") {
                static const std::vector<Row> sig[4] = {{kTnt, kTnt, kTnt, kTnt, kTnt},
                                                        {kPotion, {0x0F, 2, 1, 0x0C, 2}, kPotion, {0x0F, 2, 1, 0x0C, 2}, kPotion},
                                                        {{0x64, 2, 0, 0x11, 2}, {0x64, 2, 0, 0x11, 2}, {0x0F, 2, 0, 0x11, 2}, {0x64, 2, 0, 0x11, 2}, {0x64, 2, 0, 0x11, 2}},
                                                        {kBomb, kBomb, kNone, kBomb, kBomb}};
                opp["schedule"] = kit_schedule(g, e["kind"] == "boss_cup" ? home_points(g) : venue_points(p, e["track_id"]), sig[(g + 1) % 4], intensity);
            } else
                opp["schedule"] = kit_schedule(g, e["kind"] == "boss_cup" ? home_points(g) : venue_points(p, e["track_id"]), kKits[g], intensity);
        }
        e["opponent"] = opp;
    }
    // Hit routes follow the racer each encounter seats.
    std::vector<J> boss_routes(6);
    for (auto &c : p["checks"]) {
        if (c["kind"] != "hit") continue;
        J kept = J::array();
        for (const auto &r : c["routes"]) {
            if (r["mode"] == "boss_race") {
                const std::string enc = r["encounter_id"];
                const int g = enc[17] - '0', idx = enc[19] - '0';
                boss_routes[g < 4 ? g : 4 + idx] = r;
            } else kept.push_back(r);
        }
        c["routes"] = kept;
    }
    for (int k = 0; k < 6; ++k)
        if (!boss_routes[k].is_null()) check_of(p, 35025000 + ident[k])["routes"].push_back(boss_routes[k]);
    if (room.contains("hit_character_encounters")) derive_hit(room, ident);
    features(room);
    return room;
}

// ── verifier ─────────────────────────────────────────────────────────────
struct Room { std::map<int64_t, int> foreign; std::set<int64_t> present, checked; };
static int foreign_of(void *ctx, int64_t item) {
    const auto &f = static_cast<Room *>(ctx)->foreign;
    const auto it = f.find(item);
    return it == f.end() ? 0 : it->second;
}
static int scout_of(void *ctx, int64_t location, int64_t *own) { *own = 0; return static_cast<Room *>(ctx)->present.count(location) != 0; }
static int checked_of(void *ctx, int64_t location) { return static_cast<Room *>(ctx)->checked.count(location) != 0; }
static std::set<int64_t> *missed;
static void unreached_of(void *, int64_t location) { if (missed) missed->insert(location); }
static ap_content_verify_result sweep(Room &room, std::set<int64_t> *miss = nullptr) {
    ap_content_verify_io io{&room, foreign_of, scout_of, checked_of, nullptr, unreached_of};
    ap_content_verify_result out{};
    missed = miss;
    expect(ap_content_plan_verify(&io, &out) == 1, "the sweep gives a verdict (never INDETERMINATE) on an admitted J11 plan");
    missed = nullptr;
    return out;
}
// Everything held reaches every check and the goal; nothing held reaches
// neither; Keys and every item but the race entrances' gates keep the shop
// behind its hub floor.
static void verify(const J &room, const std::string &name) {
    expect(admit(room), name + ": admitted for the sweep");
    if (!ap_content_plan_active()) return;
    Room all;
    for (auto id : locations(room)) all.present.insert(id);
    for (int64_t code = 35010000; code < 35010210; ++code) all.foreign[code] = 99;
    for (const auto &row : room["content_plan"]["items"]["rows"]) all.foreign[row["item"].get<int64_t>()] = 99;
    auto r = sweep(all);
    expect(r.goal && r.reachable == r.total && r.total == (int)all.present.size(),
           name + ": everything held reaches every check and the goal (" + std::to_string(r.reachable) + "/" + std::to_string(r.total) + ")");
    Room none;
    none.present = all.present;
    r = sweep(none);
    // A Wumpa-only goal is met from the start when a free pad races a
    // retail track (income needs no item); every other goal needs items.
    const J &goal = room["content_plan"]["goal"];
    expect((goal["op"] == "wumpa_banked" || !r.goal) && r.reachable < r.total, name + ": nothing held does not reach the goal");
}

// A walk over the J11 fields only: every one tampered (dropped, extra key,
// float or bool for an integer, another string) refuses.
static void walk(const J &base, const J &node, const J::json_pointer &path, const std::string &name) {
    if (node.is_object()) {
        J changed = base;
        changed[path]["unexpected_field"] = 1;
        refuse(changed, name + ": extra field at " + path.to_string());
        for (auto it = node.begin(); it != node.end(); ++it) {
            changed = base; changed[path].erase(it.key());
            refuse(changed, name + ": missing field at " + path.to_string() + "/" + it.key());
            walk(base, it.value(), path / it.key(), name);
        }
    } else if (node.is_array()) {
        for (size_t i = 0; i < node.size(); ++i) walk(base, node[i], path / std::to_string(i), name);
    } else if (node.is_number_integer()) {
        J changed = base; changed[path] = node.get<double>();
        refuse(changed, name + ": float token at " + path.to_string());
        changed[path] = true;
        refuse(changed, name + ": bool token at " + path.to_string());
    } else if (node.is_string()) {
        J changed = base; changed[path] = node.get<std::string>() + "x";
        refuse(changed, name + ": other string at " + path.to_string());
    }
}
static void tamper(const J &base, const std::string &name, const std::function<void(J &)> &change) {
    J bad = base;
    change(bad);
    refuse(bad, name);
}

int main() {
    int vanilla[20]{}; ctr_cfg_set_vanilla_cup_legs(vanilla);
    const J modes = load("track-modes"), families = load("track-checks"), globals = load("global-checks"),
            fam_global = load("track-checks-global"), capability = load("capability"), bosses = load("bosses"),
            bfinal = load("bosses-final"), bcups = load("bosses-cups"), bcustom = load("bosses-custom");
    expect(!modes.is_null() && !families.is_null() && !globals.is_null() && !bosses.is_null(), "base fixtures present");

    // Options off: every base fixture still admits with no J11 state.
    for (const J *room : {&modes, &families, &bosses, &bfinal, &bcups, &bcustom}) {
        expect(admit(*room) && !ap_content_plan_ctr_first() && ap_content_plan_wumpa()->shop_slots == 0 &&
               ap_content_plan_wumpa()->goal == 0 && !ap_content_plan_bosses()->extended && ctr_wumpa_shop.state == AP_WUMPA_SHOP_ABSENT &&
               ctr_wumpa_shop.goal == 0, "options off: a base fixture admits with no J11 state");
    }

    // ── ctr_first ──
    {
        const J cf = ctr_first(families);
        expect(admit(cf) && ap_content_plan_ctr_first(), "ctr_first: track-checks rewritten admits");
        int first = 0;
        for (int id = 0; id <= 110; ++id) {
            const auto *pad = ap_content_plan_pad(id);
            if (!pad || !pad->occupied || !pad->ctr_first) continue;
            ++first;
            expect(pad->mode_count >= 1 && pad->modes[0] == AP_CONTENT_MODE_CTR && ap_content_mode_slot(pad, AP_CONTENT_MODE_TROPHY) < 0 &&
                   pad->trophy_location > 0 && pad->ctr_location > 0 && pad->stages[0] == 1,
                   std::string("ctr_first: pad ") + pad->entry_id + " races CTR first and keeps its Trophy code");
            bool rungs = false;
            for (int r = 0; r < 5; ++r) rungs |= pad->rung_locations[r] > 0;
            expect(rungs, std::string("ctr_first: pad ") + pad->entry_id + " keeps its podium rungs");
            if (pad->mode_count == 2) expect(pad->modes[1] == AP_CONTENT_MODE_RELIC, "ctr_first: relic second");
        }
        expect(first == 5, "ctr_first: all five track-checks entries run ctr_first");
        verify(cf, "ctr_first track-checks");
        auto entry = [](J &j, const std::string &id) -> J & { return find_id(j["content_plan"]["entries"], id); };
        tamper(cf, "ctr_first: entry keeps the Trophy mode", [&](J &j) { entry(j, "entry/retail:3")["modes"] = J::array({"ctr", "trophy", "relic"}); });
        tamper(cf, "ctr_first: relic first", [&](J &j) { entry(j, "entry/retail:3")["modes"] = J::array({"relic", "ctr"}); });
        tamper(cf, "ctr_first: relic dropped", [&](J &j) { entry(j, "entry/retail:3")["modes"] = J::array({"ctr"}); });
        tamper(cf, "ctr_first: track record drops trophy", [&](J &j) {
            auto &t = find_id(j["content_plan"]["tracks"], "retail:3");
            t["modes"] = J::array({"ctr", "relic"}); t["capabilities"] = t["modes"]; });
        tamper(cf, "ctr_first: Trophy check routes from trophy", [&](J &j) {
            check_of(j["content_plan"], 35011000)["routes"][0]["mode"] = "trophy"; });
        tamper(cf, "ctr_first: Trophy check needs the letters", [&](J &j) {
            check_of(j["content_plan"], 35011000)["routes"][0]["award"] = "win_letters"; });
        tamper(cf, "ctr_first: Trophy check dropped", [&](J &j) {
            auto &c = j["content_plan"]["checks"];
            c.erase(std::remove_if(c.begin(), c.end(), [](const J &x) { return x["location"] == 35011000; }), c.end());
            j["content_plan"]["items"]["filler_count"] = j["content_plan"]["items"]["filler_count"].get<int>() - 1;
            j["content_plan"]["items"]["coded_checks"] = j["content_plan"]["items"]["coded_checks"].get<int>() - 1; });
        tamper(cf, "ctr_first: a rung routes from trophy", [&](J &j) {
            for (auto &c : j["content_plan"]["checks"])
                if (c["kind"] == "podium" && c["owner"] == "retail:3") { c["routes"][0]["mode"] = "trophy"; break; } });
        tamper(cf, "ctr_first: structure key dropped", [](J &j) { j["content_plan"].erase("adventure_structure"); features(j); });
        tamper(cf, "ctr_first: structure classic", [](J &j) { j["content_plan"]["adventure_structure"] = "classic"; });
        tamper(cf, "ctr_first: feature dropped", [](J &j) {
            auto &f = j["content_plan"]["required_features"]; f.erase(std::find(f.begin(), f.end(), J("ctr_first_v1"))); });
        tamper(cf, "ctr_first: feature on a classic plan", [&](J &j) { j = families; j["content_plan"]["required_features"].push_back("ctr_first_v1"); });
        tamper(cf, "ctr_first: key on a classic plan", [&](J &j) { j = families; j["content_plan"]["adventure_structure"] = "ctr_first"; features(j); });
        tamper(cf, "ctr_first: CTR mode at stage 2", [&](J &j) {
            find_id(j["content_plan"]["pads"], "pad:104")["mode_gates"][0]["stage"] = 2; });
        walk(cf, cf["content_plan"]["adventure_structure"], J::json_pointer("/content_plan/adventure_structure"), "ctr_first");

        // Decision 4: a track without a CTR Challenge keeps the classic order.
        const J cm = ctr_first(modes);
        expect(admit(cm), "ctr_first: track-modes rewritten admits");
        const ctr_content_pad *loop = nullptr, *relic = nullptr;
        for (int id = 0; id <= 110; ++id) {
            const auto *pad = ap_content_plan_pad(id);
            if (!pad || !pad->occupied) continue;
            if (std::string(pad->entry_id) == "entry/custom/fixture-loop/2.1") loop = pad;
            if (std::string(pad->entry_id) == "entry/custom/relic-loop/1") relic = pad;
        }
        expect(loop && !loop->ctr_first && loop->modes[0] == AP_CONTENT_MODE_TROPHY && relic && !relic->ctr_first,
               "ctr_first: decision 4, a track without CTR keeps the classic Trophy-first order");
        verify(cm, "ctr_first track-modes");
        tamper(cm, "ctr_first: a fallback track reordered", [&](J &j) {
            entry(j, "entry/custom/fixture-loop/2.1")["modes"] = J::array({"relic", "trophy"}); });
        // Hit Character, Itemsanity and global Wumpa route from the CTR Challenge.
        const J cg = ctr_first(globals);
        expect(admit(cg), "ctr_first: global-checks rewritten admits");
        verify(cg, "ctr_first global-checks");
        tamper(cg, "ctr_first: a Hit route from the Trophy mode", [&](J &j) {
            auto &c = check_of(j["content_plan"], 35025001);
            J r = c["routes"][0]; r["mode"] = "trophy"; r["entry_id"] = "entry/retail:3"; r["own_pad_id"] = "pad:6";
            c["routes"].push_back(r); });
        const J cw = ctr_first(fam_global);
        expect(admit(cw), "ctr_first: track-checks-global rewritten admits");
        verify(cw, "ctr_first track-checks-global");
        const J cc = ctr_first(capability);
        expect(admit(cc), "ctr_first: capability rewritten admits");
        verify(cc, "ctr_first capability");
    }

    // ── Wumpa Shop ──
    {
        J shop = families;
        add_shop(shop, 4, 150, "excluded", false);
        add_goal(shop, 300);
        expect(admit(shop), "shop: track-checks with a four-slot shop and a Wumpa arm admits");
        const auto *w = ap_content_plan_wumpa();
        expect(w->shop_slots == 4 && w->prices == 150 && w->excluded && !w->hints && w->goal == 300, "shop: the record is read");
        expect(ctr_wumpa_shop.state == AP_WUMPA_SHOP_VALID && ctr_wumpa_shop.slots == 4 && ctr_wumpa_shop.goal == 300 &&
               ctr_wumpa_shop.hints == 0 && AP_WumpaBankingActive(&ctr_wumpa_shop) && AP_WumpaShopUsable(&ctr_wumpa_shop),
               "shop: the ordinary shop configuration is filled");
        bool slots = true;
        for (int h = 0; h < 5; ++h)
            for (int n = 1; n <= 4; ++n) {
                const AP_WumpaShopSlot &s = ctr_wumpa_shop.slot[h][n - 1];
                slots = slots && s.code == AP_WumpaShopCode(h, n) && s.price == scale(kBandLow[h] + n % 7, 150) && s.unlock == unlock_of(n, 4, 150);
            }
        expect(slots && ctr_wumpa_shop.slot[0][2].unlock == 75 && ctr_wumpa_shop.slot[0][3].unlock == 150,
               "shop: codes, prices and shelf unlocks are the plan's");
        verify(shop, "shop track-checks");
        {
            // The shop needs its hub's Key floor and income: Keys alone (no
            // race open) reach no slot; with every item but Keys, N. Sanity
            // Beach's slots are reachable and Citadel City's are not.
            Room room;
            for (auto id : locations(shop)) room.present.insert(id);
            for (int64_t code = 35010000; code < 35010210; ++code) room.foreign[code] = 99;
            for (const auto &row : shop["content_plan"]["items"]["rows"]) room.foreign[row["item"].get<int64_t>()] = 99;
            room.foreign[35010014] = 0;
            std::set<int64_t> miss;
            sweep(room, &miss);
            expect(miss.count(35028600) && miss.count(35028640), "shop: no Key opens no retail race here, so no income and no slot");
            room.foreign[35010014] = 2; // Crash Cove's pad (keys:2) banks fruit
            miss.clear();
            sweep(room, &miss);
            expect(!miss.count(35028600) && !miss.count(35028640 + 3) && !miss.count(35028720) && miss.count(35028760) &&
                   miss.count(35028763), "shop: two Keys and income reach every hub but Citadel City (three Keys)");
        }
        auto ck = [](J &j, int64_t code) -> J & { return check_of(j["content_plan"], code); };
        tamper(shop, "shop: unlock forged", [&](J &j) { ck(j, 35028603)["unlock_banked"] = 151; });
        tamper(shop, "shop: unlock on a starting shelf", [&](J &j) { ck(j, 35028600)["unlock_banked"] = 1; });
        tamper(shop, "shop: price above the ceiling", [&](J &j) { ck(j, 35028600)["price"] = 100001; });
        tamper(shop, "shop: price negative", [&](J &j) { ck(j, 35028600)["price"] = -1; });
        tamper(shop, "shop: price dropped", [&](J &j) { ck(j, 35028600).erase("price"); });
        tamper(shop, "shop: a route", [&](J &j) { ck(j, 35028600)["routes"] = check_of(j["content_plan"], 35011000)["routes"]; });
        tamper(shop, "shop: owner", [&](J &j) { ck(j, 35028640)["owner"] = "hub:26"; });
        tamper(shop, "shop: name", [&](J &j) { ck(j, 35028680)["display_name"] = "The Lost Ruins Shop: Slot 1"; });
        tamper(shop, "shop: kind", [&](J &j) { ck(j, 35028600)["kind"] = "wumpa"; });
        tamper(shop, "shop: a boost rank", [&](J &j) { ck(j, 35028600)["boost"] = 1; });
        tamper(shop, "shop: order", [&](J &j) { auto &c = j["content_plan"]["checks"]; std::swap(c[c.size() - 1], c[c.size() - 2]); });
        tamper(shop, "shop: not last", [&](J &j) {
            auto &c = j["content_plan"]["checks"]; J last = c[c.size() - 1]; c.erase(c.size() - 1); c.insert(c.begin(), last); });
        tamper(shop, "shop: a slot dropped", [&](J &j) {
            auto &c = j["content_plan"]["checks"]; c.erase(c.size() - 1);
            j["content_plan"]["items"]["filler_count"] = j["content_plan"]["items"]["filler_count"].get<int>() - 1;
            j["content_plan"]["items"]["coded_checks"] = j["content_plan"]["items"]["coded_checks"].get<int>() - 1; });
        tamper(shop, "shop: a sixth hub slot code", [&](J &j) { ck(j, 35028603)["location"] = 35028604; });
        tamper(shop, "shop: record slots", [&](J &j) { j["content_plan"]["wumpa_shop"]["slots"] = 5; });
        tamper(shop, "shop: record slots zero", [&](J &j) { j["content_plan"]["wumpa_shop"]["slots"] = 0; });
        tamper(shop, "shop: record slots 26", [&](J &j) { j["content_plan"]["wumpa_shop"]["slots"] = 26; });
        tamper(shop, "shop: record prices", [&](J &j) { j["content_plan"]["wumpa_shop"]["prices"] = 100; });
        tamper(shop, "shop: record prices 24", [&](J &j) { j["content_plan"]["wumpa_shop"]["prices"] = 24; });
        tamper(shop, "shop: record prices 401", [&](J &j) { j["content_plan"]["wumpa_shop"]["prices"] = 401; });
        tamper(shop, "shop: record progression", [&](J &j) { j["content_plan"]["wumpa_shop"]["progression"] = "progression"; });
        tamper(shop, "shop: record hints", [&](J &j) { j["content_plan"]["wumpa_shop"]["hints"] = 1; });
        tamper(shop, "shop: record dropped", [&](J &j) { j["content_plan"].erase("wumpa_shop"); features(j); });
        tamper(shop, "shop: feature dropped", [&](J &j) {
            auto &f = j["content_plan"]["required_features"]; f.erase(std::find(f.begin(), f.end(), J("wumpa_shop_v1"))); });
        tamper(shop, "shop: features reordered", [&](J &j) {
            auto &f = j["content_plan"]["required_features"]; std::swap(f[f.size() - 1], f[f.size() - 2]); });
        tamper(shop, "shop: an envelope key", [&](J &j) { j["ctr_options"]["wumpa_shop_slots"] = 4; });
        tamper(shop, "shop: a top-level block", [&](J &j) { j["wumpa_shop"] = {{"version", 1}, {"hubs", J::array()}}; });
        walk(shop, shop["content_plan"]["wumpa_shop"], J::json_pointer("/content_plan/wumpa_shop"), "shop record");
        {
            const size_t last = shop["content_plan"]["checks"].size() - 1;
            walk(shop, shop["content_plan"]["checks"][last], J::json_pointer("/content_plan/checks/" + std::to_string(last)), "shop check");
        }
        // allowed and hints on: still the ordinary configuration.
        J allowed = families;
        add_shop(allowed, 25, 400, "allowed", true);
        expect(admit(allowed) && ctr_wumpa_shop.slots == 25 && ctr_wumpa_shop.hints == 1 && ctr_wumpa_shop.goal == 0 &&
               !ap_content_plan_wumpa()->excluded, "shop: 25 slots at 400 percent, allowed, hints on, no goal");
        verify(allowed, "shop 25 slots");
    }

    // ── Wumpa goal ──
    {
        J only = families;
        only["content_plan"]["goal"] = {{"op", "wumpa_banked"}, {"count", 5000}};
        only["ctr_options"]["goal_gems"] = 0;
        features(only);
        expect(admit(only) && ap_content_plan_wumpa()->goal == 5000 && ctr_wumpa_shop.goal == 5000 &&
               ctr_wumpa_shop.state == AP_WUMPA_SHOP_ABSENT && AP_WumpaBankingActive(&ctr_wumpa_shop) && ctr_cfg.goal_gems == 0,
               "goal: a Wumpa-only goal admits and turns banking on without a shop");
        int gems[5] = {};
        expect(ap_content_plan_goal(gems), "goal: no Gem arm, so the plan's Gem term is met");
        verify(only, "Wumpa-only goal");
        tamper(only, "goal: count 0", [](J &j) { j["content_plan"]["goal"]["count"] = 0; });
        tamper(only, "goal: count 5001", [](J &j) { j["content_plan"]["goal"]["count"] = 5001; });
        tamper(only, "goal: extra key", [](J &j) { j["content_plan"]["goal"]["items"] = J::array(); });
        tamper(only, "goal: wrapped alone", [](J &j) { j["content_plan"]["goal"] = {{"op", "all"}, {"terms", J::array({j["content_plan"]["goal"]})}}; });
        tamper(only, "goal: feature dropped", [](J &j) { j["content_plan"]["required_features"].erase(j["content_plan"]["required_features"].size() - 1); });
        tamper(only, "goal: envelope goal_gems", [](J &j) { j["ctr_options"]["goal_gems"] = 5; });
        tamper(only, "goal: no arm at all", [](J &j) { j["content_plan"]["goal"] = {{"op", "all"}, {"terms", J::array()}}; features(j); });
        walk(only, only["content_plan"]["goal"], J::json_pointer("/content_plan/goal"), "Wumpa-only goal");
        J gem_and = families;
        add_goal(gem_and, 1);
        expect(admit(gem_and) && ap_content_plan_wumpa()->goal == 1 && ctr_cfg.goal_gems == 5, "goal: Gem arm AND Wumpa arm admits");
        tamper(gem_and, "goal: Wumpa arm before the Gem arm", [](J &j) {
            auto &t = j["content_plan"]["goal"]["terms"]; std::swap(t[0], t[1]); });
        tamper(gem_and, "goal: two Wumpa arms", [](J &j) { j["content_plan"]["goal"]["terms"].push_back(j["content_plan"]["goal"]["terms"][1]); });
        tamper(gem_and, "goal: Wumpa arm in a requirement", [](J &j) {
            for (auto &r : j["content_plan"]["requirements"])
                if (r["id"].get<std::string>().rfind("stage2:", 0) == 0) { r["predicate"] = {{"op", "wumpa_banked"}, {"count", 1}}; break; } });
        // Bosses: Oxide, Boss and Gem arms, then Wumpa.
        J boss_goal = bosses;
        add_goal(boss_goal, 777);
        add_shop(boss_goal, 2, 25, "allowed", true);
        expect(admit(boss_goal) && ctr_wumpa_shop.goal == 777 && ctr_wumpa_shop.slots == 2 && ctr_cfg.goal_oxide == 1,
               "goal: bosses plan with a Wumpa arm and a two-slot shop admits");
        verify(boss_goal, "bosses with shop and goal");
        tamper(boss_goal, "goal: Wumpa arm inside the Oxide companions", [](J &j) {
            for (auto &r : j["content_plan"]["requirements"])
                if (r["id"] == "oxide:first") r["predicate"]["terms"].push_back({{"op", "wumpa_banked"}, {"count", 777}}); });
        tamper(boss_goal, "goal: Wumpa arm not last", [](J &j) {
            auto &t = j["content_plan"]["goal"]["terms"]; std::rotate(t.begin(), t.end() - 1, t.end()); });
    }

    // ── Boss opponent and kit shuffle ──
    {
        const Shuffle all;
        const J bs = boss_shuffle(bosses, all);
        if (std::getenv("J11_DUMP")) std::ofstream(std::getenv("J11_DUMP")) << bs.dump();
        expect(admit(bs), "bosses: opponent, kit, Oxide in the pool and all_compatible admits");
        const ctr_content_bosses *b = ap_content_plan_bosses();
        expect(b->extended && b->opponent_shuffle && b->oxide_in_pool && b->kit_shuffle && b->oxide_profile_all && !b->home_venues &&
               b->kit_intensity == 2 && b->kit_assignment == 0 && b->loss_easing == 0 && b->opponent[0] == 0 && b->opponent[1] == 12 &&
               b->opponent[2] == 13 && b->opponent[3] == 15 && b->final_opponent == 3, "bosses: the record and draws are read");
        const AP_BossEncounters &e = ctr_boss_encounters;
        bool kits = true;
        for (int g = 0; g < 4; ++g) {
            kits = kits && e.composed[g] && AP_BossEncounterOpponent(&e, g) == all.racers[g] &&
                   AP_BossEncounterKitGarage(&e, 1, 0, g, e.track[g]) == g &&
                   !AP_BossKitScheduleProblem(e.schedule[g], AP_BOSS_VENUE_SEGMENTS, AP_BOSS_VENUE_RESTART_POINTS[e.track[g]]);
        }
        expect(e.state == AP_BOSS_ENC_VALID && e.version == AP_BOSS_ENCOUNTERS_VERSION_2 && e.kit && !e.lossEasing && e.trackOn &&
               e.opponentOn && e.oxideInPool && e.oxideProfileAll && e.finalPresent && e.finalOpponent == 3 && e.finalComposed &&
               !e.gauntletOn && kits && AP_BossEncounterFinalOpponent(&e) == 3 &&
               !AP_BossFinalScheduleProblem(e.finalSchedule, AP_BOSS_FINAL_SEGMENTS),
               "bosses: the ordinary version 2 boss record is filled from the plan");
        const ctr_hit_encounters &h = ctr_cfg.hit;
        expect(h.valid && h.boss_identity[0] == 0 && h.boss_identity[1] == 12 && h.boss_identity[2] == 13 && h.boss_identity[3] == 15 &&
               h.boss_identity[4] == 3 && h.boss_identity[5] == 3 && h.triggers[0].fallback_keys == 4 && h.triggers[0].count == 0 &&
               h.triggers[4].kind == CTR_CFG_HIT_KIND_BOSS && h.triggers[4].count == 3 && h.triggers[4].fallback_keys == 0 &&
               h.triggers[7].count == 1 && h.triggers[7].any_of[0] == 35011103,
               "bosses: the Hit block follows the drawn racers (decision 9)");
        verify(bs, "boss shuffle");
        auto ent = [](J &j, int g, int i = 0) -> J & { return find_id(j["content_plan"]["entries"], "entry/boss:" + std::to_string(g) + "/" + std::to_string(i)); };
        auto rec = [](J &j) -> J & { return j["content_plan"]["bosses"]; };
        tamper(bs, "bosses: two garages seat one racer", [&](J &j) { ent(j, 1)["opponent"]["character_id"] = 0; });
        tamper(bs, "bosses: a garage seats the final racer", [&](J &j) { ent(j, 1)["opponent"]["character_id"] = 3; });
        tamper(bs, "bosses: final racer is N. Oxide with oxide_in_pool", [&](J &j) {
            ent(j, 4)["opponent"]["character_id"] = 15; ent(j, 4, 1)["opponent"]["character_id"] = 15; ent(j, 3)["opponent"]["character_id"] = 5; });
        tamper(bs, "bosses: Oxide encounters seat different racers", [&](J &j) { ent(j, 4, 1)["opponent"]["character_id"] = 6; });
        tamper(bs, "bosses: Oxide encounters carry different profiles", [&](J &j) { ent(j, 4, 1)["opponent"]["schedule"][1][4] = 2; });
        tamper(bs, "bosses: racer id 16", [&](J &j) { ent(j, 0)["opponent"]["character_id"] = 16; });
        tamper(bs, "bosses: N. Oxide in a garage without oxide_in_pool", [&](J &j) {
            rec(j)["oxide_in_pool"] = false; ent(j, 4)["opponent"]["character_id"] = 15; ent(j, 4, 1)["opponent"]["character_id"] = 15; });
        tamper(bs, "bosses: oxide_in_pool without opponent_shuffle", [&](J &j) { rec(j)["opponent_shuffle"] = false; });
        tamper(bs, "bosses: kit row uncertified", [&](J &j) { ent(j, 0)["opponent"]["schedule"][1][3] = 0; });
        tamper(bs, "bosses: kit cooldown unscaled", [&](J &j) { ent(j, 0)["opponent"]["schedule"][0][4] = 15; });
        tamper(bs, "bosses: kit cooldown below the floor", [&](J &j) { ent(j, 0)["opponent"]["schedule"][0][4] = 1; });
        tamper(bs, "bosses: kit checkpoint moved", [&](J &j) { ent(j, 0)["opponent"]["schedule"][2][0] = ent(j, 0)["opponent"]["schedule"][2][0].get<int>() + 1; });
        tamper(bs, "bosses: kit never attacks", [&](J &j) {
            for (auto &seg : ent(j, 0)["opponent"]["schedule"]) { seg[1] = 3; seg[2] = 0x0f; seg[3] = 1; seg[4] = 11; seg[5] = 0; } });
        tamper(bs, "bosses: super engine in a garage", [&](J &j) {
            auto &seg = ent(j, 0)["opponent"]["schedule"][1]; seg[1] = 3; seg[2] = 0x0d; seg[3] = 1; seg[4] = 9; seg[5] = 0; });
        tamper(bs, "bosses: one family in a mixed kit", [&](J &j) {
            for (auto &seg : ent(j, 0)["opponent"]["schedule"]) { seg[1] = 3; seg[2] = 1; seg[3] = 1; seg[4] = 11; seg[5] = 0; } });
        tamper(bs, "bosses: two garages carry the same kit", [&](J &j) {
            auto &a = ent(j, 0)["opponent"]["schedule"], &c = ent(j, 2)["opponent"]["schedule"];
            for (size_t i = 0; i < 5; ++i) for (int f = 1; f < 6; ++f) c[i][f] = a[i][f]; });
        tamper(bs, "bosses: four segments", [&](J &j) { ent(j, 1)["opponent"]["schedule"].erase(4); });
        tamper(bs, "bosses: a seven-field segment", [&](J &j) { ent(j, 1)["opponent"]["schedule"][0].push_back(0); });
        tamper(bs, "bosses: kit inherited with the kit switch", [&](J &j) { ent(j, 2)["opponent"]["attack_profile"] = "inherited"; ent(j, 2)["opponent"].erase("schedule"); });
        tamper(bs, "bosses: Oxide profile twelve segments", [&](J &j) { ent(j, 4)["opponent"]["schedule"].erase(12); ent(j, 4, 1)["opponent"]["schedule"].erase(12); });
        tamper(bs, "bosses: Oxide profile off its checkpoints", [&](J &j) {
            ent(j, 4)["opponent"]["schedule"][3][0] = 48; ent(j, 4, 1)["opponent"]["schedule"][3][0] = 48; });
        tamper(bs, "bosses: Oxide profile inherited with all_compatible", [&](J &j) {
            for (int i = 0; i < 2; ++i) { ent(j, 4, i)["opponent"]["attack_profile"] = "inherited"; ent(j, 4, i)["opponent"].erase("schedule"); } });
        tamper(bs, "bosses: driving profile", [&](J &j) { ent(j, 0)["opponent"]["driving_profile"] = "boss"; });
        tamper(bs, "bosses: slot 2", [&](J &j) { ent(j, 0)["opponent"]["slot"] = 2; });
        tamper(bs, "bosses: kit record on kit off", [&](J &j) { rec(j)["kit_shuffle"] = false; });
        tamper(bs, "bosses: kit intensity", [&](J &j) { rec(j)["kit"]["intensity"] = "standard"; });
        tamper(bs, "bosses: kit intensity unknown", [&](J &j) { rec(j)["kit"]["intensity"] = "brutal"; });
        tamper(bs, "bosses: kit assignment signature", [&](J &j) { rec(j)["kit"]["assignment"] = "signature_profiles"; });
        tamper(bs, "bosses: kit loss_easing not Boolean", [&](J &j) { rec(j)["kit"]["loss_easing"] = 0; });
        tamper(bs, "bosses: track_assignment home with independent venues", [&](J &j) { rec(j)["track_assignment"] = "character_home_track"; });
        tamper(bs, "bosses: Oxide profile retail with a composed record", [&](J &j) { rec(j)["oxide_attack_profile"] = "retail"; });
        tamper(bs, "bosses: repeats", [&](J &j) { rec(j)["repeats"] = true; });
        tamper(bs, "bosses: feature dropped", [&](J &j) {
            auto &f = j["content_plan"]["required_features"]; f.erase(std::find(f.begin(), f.end(), J("boss_opponents_v1"))); });
        tamper(bs, "bosses: Hit route stays with the retail boss", [&](J &j) {
            auto &roo = check_of(j["content_plan"], 35025010);
            roo["routes"].push_back(check_of(j["content_plan"], 35025000)["routes"].back()); });
        tamper(bs, "bosses: Hit route dropped from the drawn racer", [&](J &j) { check_of(j["content_plan"], 35025000)["routes"].erase(
                                                                                   check_of(j["content_plan"], 35025000)["routes"].size() - 1); });
        tamper(bs, "bosses: Hit table keeps the retail bosses", [&](J &j) { j["hit_character_encounters"]["bosses"]["35011100"] = 10; });
        tamper(bs, "bosses: Hit table final racer", [&](J &j) { j["hit_character_encounters"]["bosses"]["35011105"] = 15; });
        tamper(bs, "bosses: Hit trigger keeps the retail win", [&](J &j) {
            j["hit_character_encounters"]["unlock_triggers"]["10"] = {{"kind", "boss"}, {"any_of", J::array({35011100})}}; });
        tamper(bs, "bosses: Hit fallback missing for a boss raced nowhere", [&](J &j) {
            j["hit_character_encounters"]["unlock_triggers"]["8"].erase("fallback_keys"); });
        tamper(bs, "bosses: Hit fallback on a guest raced in an encounter", [&](J &j) {
            j["hit_character_encounters"]["unlock_triggers"]["12"]["fallback_keys"] = 3; });
        tamper(bs, "bosses: Hit fallback count", [&](J &j) { j["hit_character_encounters"]["unlock_triggers"]["9"]["fallback_keys"] = 1; });
        {
            // Every J11 field of the boss record and the boss entries' opponents.
            walk(bs, bs["content_plan"]["bosses"], J::json_pointer("/content_plan/bosses"), "boss record");
            for (const auto &id : {"entry/boss:0/0", "entry/boss:4/1"})
                for (size_t i = 0; i < bs["content_plan"]["entries"].size(); ++i)
                    if (bs["content_plan"]["entries"][i]["id"] == id)
                        walk(bs, bs["content_plan"]["entries"][i]["opponent"],
                             J::json_pointer("/content_plan/entries/" + std::to_string(i) + "/opponent"), std::string("opponent ") + id);
        }
        // Kit only: retail opponents, composed kits, no final record.
        Shuffle kit_only;
        kit_only.opponent = kit_only.oxide_pool = kit_only.all_compatible = false;
        kit_only.intensity = 3;
        const J ko = boss_shuffle(bosses, kit_only);
        expect(admit(ko) && ctr_boss_encounters.version == 2 && ctr_boss_encounters.kit && !ctr_boss_encounters.opponentOn &&
               !ctr_boss_encounters.finalPresent && ctr_boss_encounters.finalOpponent == 15 && ctr_cfg.hit.boss_identity[0] == 10 &&
               ctr_cfg.hit.triggers[0].count == 1 && ctr_cfg.hit.triggers[0].fallback_keys == 0,
               "bosses: kit shuffle alone keeps the retail opponents and Hit triggers");
        verify(ko, "kit shuffle alone");
        tamper(ko, "bosses: a garage swaps its racer without opponent_shuffle", [&](J &j) { ent(j, 0)["opponent"]["character_id"] = 0; });
        tamper(ko, "bosses: N. Oxide composed without all_compatible", [&](J &j) {
            for (int i = 0; i < 2; ++i) { ent(j, 4, i)["opponent"]["attack_profile"] = "composed";
                                          ent(j, 4, i)["opponent"]["schedule"] = kit_schedule(4, 0, oxide_rows(), 3); } });
        // Signature profiles at relaxed intensity.
        Shuffle sig;
        sig.assignment = "signature_profiles";
        sig.intensity = 0;
        sig.oxide_pool = false;
        sig.final_racer = 15;
        sig.racers[3] = 5;
        const J sp = boss_shuffle(bosses, sig);
        expect(admit(sp) && ap_content_plan_bosses()->kit_assignment == 1 && ctr_boss_encounters.finalOpponent == 15 &&
               ctr_boss_encounters.finalComposed, "bosses: signature profiles, Oxide composed and not drawn");
        tamper(sp, "bosses: a signature profile edited", [&](J &j) {
            auto &seg = ent(j, 0)["opponent"]["schedule"][0]; seg[1] = 3; seg[2] = 1; seg[3] = 1; seg[4] = 22; seg[5] = 0; });
        // all_compatible alone: retail garages, N. Oxide composed.
        Shuffle prof;
        prof.opponent = prof.oxide_pool = prof.kit = false;
        const J pa = boss_shuffle(bosses, prof);
        expect(admit(pa) && ctr_boss_encounters.version == 2 && !ctr_boss_encounters.kit && ctr_boss_encounters.finalComposed &&
               ctr_boss_encounters.finalOpponent == 15 && !ctr_boss_encounters.composed[0], "bosses: all_compatible alone");
        tamper(pa, "bosses: no switch on with the keys", [&](J &j) {
            rec(j)["oxide_attack_profile"] = "retail";
            for (int i = 0; i < 2; ++i) { ent(j, 4, i)["opponent"]["attack_profile"] = "inherited"; ent(j, 4, i)["opponent"].erase("schedule"); } });
        // character_home_track: each garage races on its racer's home track.
        Shuffle home;
        home.home = true;
        const J hm = boss_shuffle(bosses, home);
        expect(admit(hm) && ap_content_plan_bosses()->home_venues && ap_content_plan_bosses()->venues[0] == 3 &&
               ap_content_plan_bosses()->venues[1] == 17 && ap_content_plan_bosses()->venues[3] == 13,
               "bosses: character_home_track races each racer at home");
        verify(hm, "character_home_track");
        tamper(hm, "bosses: a home venue moved", [&](J &j) { ent(j, 0)["track_id"] = "retail:14"; });
        tamper(hm, "bosses: home without track_shuffle", [&](J &j) { rec(j)["track_shuffle"] = false; });
        // Without track_shuffle: home venues, opponents drawn.
        J noshuffle = bfinal;
        {
            Shuffle s;
            s.oxide_pool = false; s.all_compatible = false; s.final_racer = 15;
            s.racers[3] = 5;
            const J nf = boss_shuffle(noshuffle, s);
            expect(admit(nf) && ctr_boss_encounters.state == AP_BOSS_ENC_VALID && !ctr_boss_encounters.trackOn &&
                   ctr_boss_encounters.track[0] == 6 && ctr_boss_encounters.opponent[0] == 0,
                   "bosses: opponent and kit without track_shuffle keep the home venues");
            verify(nf, "bosses-final shuffled");
        }
        // A boss cup takes a drawn racer and kit; the kit keeps the garage's
        // retail segment starts and each leg maps them at race start.
        {
            Shuffle p2;
            p2.oxide_pool = false; p2.all_compatible = false; p2.racers[3] = 1;
            const J cu = boss_shuffle(bcups, p2);
            expect(admit(cu) && ctr_boss_encounters.composed[0], "bosses: a boss cup takes a drawn racer and kit");
            tamper(cu, "bosses: a boss cup kit off the retail segments", [&](J &j) {
                for (auto &e : j["content_plan"]["entries"])
                    if (e["kind"] == "boss_cup") { e["opponent"]["schedule"][1][0] = 200; break; } });
            Shuffle p3;
            p3.opponent = p3.oxide_pool = p3.kit = false;
            const J cp = boss_shuffle(bcups, p3);
            expect(admit(cp) && ctr_boss_encounters.finalComposed, "bosses: boss cups with all_compatible alone admit");
        }
        // Custom and Cortex Vortex venues carry kits mapped onto their counts.
        {
            J cu = bcustom;
            cu["content_plan"]["bosses"]["repeats"] = false;
            // bosses-custom repeats a venue; move the repeat to a free retail track.
            std::set<std::string> seen;
            for (int g = 0; g < 4; ++g) {
                J &e = ent(cu, g);
                if (!seen.insert(e["track_id"].get<std::string>()).second) e["track_id"] = "retail:" + std::to_string(AP_BOSS_VENUE_GARAGE[g].homeLevel);
            }
            Shuffle s;
            s.oxide_pool = false; s.all_compatible = false; s.final_racer = 15; s.racers[3] = 5;
            const J cs = boss_shuffle(cu, s);
            const bool ok = admit(cs);
            expect(ok && ctr_boss_encounters.custom[1] && ctr_boss_encounters.composed[1] && ctr_boss_encounters.points[1] == 255,
                   "bosses: a kit on Cortex Vortex maps onto 255 restart points");
            if (ok) verify(cs, "bosses-custom shuffled");
        }
    }

    // ── all four together ──
    {
        J all4 = boss_shuffle(ctr_first(bosses), Shuffle{});
        add_shop(all4, 10, 100, "allowed", true);
        add_goal(all4, 2000);
        expect(admit(all4) && ap_content_plan_ctr_first() && ap_content_plan_bosses()->extended && ctr_wumpa_shop.slots == 10 &&
               ctr_wumpa_shop.goal == 2000, "all four: ctr_first, boss shuffle, shop and goal admit together");
        const auto &f = all4["content_plan"]["required_features"];
        expect(f[f.size() - 4] == "ctr_first_v1" && f[f.size() - 3] == "boss_opponents_v1" && f[f.size() - 2] == "wumpa_shop_v1" &&
               f[f.size() - 1] == "wumpa_goal_v1" && f[f.size() - 5] == "boss_encounters_v1", "all four: required_features order");
        verify(all4, "all four");
        tamper(all4, "all four: feature order", [](J &j) {
            auto &x = j["content_plan"]["required_features"]; std::swap(x[x.size() - 1], x[x.size() - 4]); });
        // A refusal leaves no shop configuration behind.
        J bad = all4;
        check_of(bad["content_plan"], 35028600)["price"] = -5;
        refuse(bad, "all four: a forged price");
        expect(ctr_wumpa_shop.state == AP_WUMPA_SHOP_ABSENT && ctr_wumpa_shop.goal == 0, "a refused plan leaves no shop configuration");
    }

    // ── the apworld's own J11 fixtures, when checked in ──
    for (const char *name : {"ctr-first", "wumpa-shop", "wumpa-goal-only", "boss-shuffle", "all-030", "boss-cups-gauntlet"}) {
        const J room = load(name);
        if (room.is_null()) { std::cout << "note: apworld fixture " << name << ".json not checked in yet\n"; continue; }
        expect(admit(room), std::string(name) + ": the apworld's fixture admits");
        const std::string n = name;
        if (n == "ctr-first") {
            const auto *cove = ap_content_plan_pad(100), *roo = ap_content_plan_pad(3), *btp = ap_content_plan_pad(6), *loop = ap_content_plan_pad(14);
            expect(cove && cove->ctr_first && cove->mode_count == 2 && cove->modes[0] == AP_CONTENT_MODE_CTR && cove->stages[1] == 2 &&
                   cove->trophy_location == 35011000 && cove->trophy_boost > 0 && cove->rung_locations[0] > 0 &&
                   btp && btp->ctr_first && btp->mode_count == 1 && roo && !roo->ctr_first && roo->modes[0] == AP_CONTENT_MODE_TROPHY &&
                   loop && !loop->ctr_first, "ctr-first: ctr_first pads and the decision 4 fallbacks");
        } else if (n == "wumpa-shop") {
            expect(ctr_wumpa_shop.state == AP_WUMPA_SHOP_VALID && ctr_wumpa_shop.slots == 3 && ctr_wumpa_shop.goal == 750 &&
                   ap_content_plan_wumpa()->excluded && ctr_cfg.goal_gems == 3, "wumpa-shop: shop, goal and Gem arm");
        } else if (n == "wumpa-goal-only") {
            expect(ctr_wumpa_shop.state == AP_WUMPA_SHOP_ABSENT && ctr_wumpa_shop.goal == 1200 && ctr_cfg.goal_gems == 0 &&
                   AP_WumpaBankingActive(&ctr_wumpa_shop), "wumpa-goal-only: banking on for the goal alone");
        } else if (n == "boss-shuffle") {
            const AP_BossEncounters &e = ctr_boss_encounters;
            expect(e.version == 2 && e.custom[0] && e.points[0] == 255 && e.custom[3] && e.points[3] == 35 && e.opponent[0] == 13 &&
                   e.opponent[3] == 4 && e.finalOpponent == 7 && e.finalComposed && e.oxideInPool &&
                   ctr_cfg.hit.triggers[0].fallback_keys > 0 && ctr_cfg.hit.boss_identity[5] == 7,
                   "boss-shuffle: custom venue kits, Oxide in a garage, the Hit block follows the draw");
            auto ent = [](J &j, int g) -> J & { return find_id(j["content_plan"]["entries"], "entry/boss:" + std::to_string(g) + "/0"); };
            tamper(room, "boss-shuffle: a Baby T Park kit checkpoint moved", [&](J &j) { ent(j, 3)["opponent"]["schedule"][1][0] = 6; });
            tamper(room, "boss-shuffle: Baby T Park restart points restated", [&](J &j) {
                for (auto &pk : j["content_plan"]["packages"]) if (pk["content_id"] == "baby-t-park") pk["capabilities"]["checkpoints"] = 40; });
            tamper(room, "boss-shuffle: a Cortex Vortex kit on the retail scale", [&](J &j) {
                auto &sc = ent(j, 0)["opponent"]["schedule"]; sc[1][0] = 16; sc[2][0] = 33; sc[3][0] = 47; sc[4][0] = 70; });
            tamper(room, "boss-shuffle: N. Oxide without oxide_in_pool", [&](J &j) { j["content_plan"]["bosses"]["oxide_in_pool"] = false; });
        } else if (n == "boss-cups-gauntlet") {
            const AP_BossEncounters &e = ctr_boss_encounters;
            expect(e.gauntletOn && e.version == 2 && e.composed[0] && e.composed[3] && e.gauntletAttacker[1] == e.opponent[1],
                   "boss-cups-gauntlet: the kits arm the Purple Gem Cup's legs");
            tamper(room, "gauntlet: a number", [&](J &j) { j["content_plan"]["bosses"]["gauntlet"] = 1; });
            tamper(room, "gauntlet: without the kit", [&](J &j) { j["content_plan"]["bosses"]["kit_shuffle"] = false; });
        } else if (n == "all-030") {
            expect(ap_content_plan_ctr_first() && ap_content_plan_bosses()->home_venues && ctr_wumpa_shop.slots == 4 &&
                   ctr_wumpa_shop.goal == 500 && ctr_wumpa_shop.slot[0][0].price == 10, "all-030: all four features filled");
        }
        verify(room, name);
        const J &p = room["content_plan"];
        for (const char *key : {"adventure_structure", "wumpa_shop", "bosses", "goal"})
            if (p.contains(key)) walk(room, p[key], J::json_pointer(std::string("/content_plan/") + key), name);
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
