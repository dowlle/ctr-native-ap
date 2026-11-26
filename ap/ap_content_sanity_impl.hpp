// Included inside the content-plan parser namespace. No runtime dependencies.
static void sanity_checks(const J &p, State &out, const J &registry, std::map<int64_t, J> &expected) {
    const J &s = p.at("sanity");
    const int exclusion_version = s.contains("exclusion_version") ? number(s["exclusion_version"], 2, 2) : 1;
    const bool context = s.contains("logic_difficulty");
    const int rules_version = s.contains("rules_version") ? number(s["rules_version"], 1, 6) : 0;
    J shape = s;
    shape.erase("rules_version");
    shape.erase("exclusion_version");
    if (context) fields(shape, {"itemboxsanity", "wumpasanity", "shortcut_knowledge", "tracks", "logic_difficulty", "boost_blue_fire"});
    else fields(shape, {"itemboxsanity", "wumpasanity", "shortcut_knowledge", "tracks"});
    const int difficulty = context ? number(s["logic_difficulty"], 0, 2) : out.capability.present ? out.capability.logic_difficulty : 2;
    if (context) need(s["boost_blue_fire"].is_boolean(), "sanity Blue Fire setting");
    const bool blue = context ? s["boost_blue_fire"].get<bool>() : out.capability.boost_blue_fire != 0;
    need(!context || !out.capability.present || (difficulty == out.capability.logic_difficulty && blue == !!out.capability.boost_blue_fire), "sanity object settings mismatch");
    need(!blue || out.capability.boost_mode, "sanity Blue Fire needs boost");
    const int count = number(s["itemboxsanity"], 0, 50), wumpa = number(s["wumpasanity"], 0, 3);
    const int tier = number(s["shortcut_knowledge"], 0, 2);
    need(count || wumpa, "empty sanity feature");
    need(!out.capability.present || out.capability.shortcut_knowledge == tier, "sanity shortcut tier mismatch");
    const Table tracks = table(p.at("tracks"), 160), entries = table(p.at("entries"), 160);
    std::map<std::string, J> uses;
    std::set<std::string> standalone;
    std::map<std::string, const J *> packages;
    for (const auto &pkg : p.at("packages")) packages.emplace(pkg.at("id").get<std::string>(), &pkg);
    for (const auto &pad : p.at("pads")) {
        if (pad["entry_id"].is_null()) continue;
        const std::string id = pad["entry_id"];
        const J &entry = *entries.at(id);
        if (entry["kind"] != "track") continue;
        const std::string track = entry["track_id"];
        standalone.insert(track);
        for (const auto &mode : entry["modes"])
            if (mode == "trophy" || mode == "ctr")
                uses[track].push_back(route_record(id, mode.get<std::string>().c_str(), pad["id"], "pickup"));
    }
    std::map<std::string, const J *> encounters;
    std::map<std::string, std::string> pad_of;
    for (const auto &pad : p.at("pads")) if (!pad["entry_id"].is_null()) pad_of[pad["entry_id"]] = pad["id"];
    for (const auto &enc : p.at("encounters")) encounters[enc["entry_id"]] = &enc;
    for (const auto &entry : p.at("entries")) {
        const std::string id = entry["id"], kind = entry["kind"];
        const auto enc = encounters.find(id);
        if (enc == encounters.end() && (kind != "cup" || !pad_of.count(id))) continue;
        const std::string pad = enc == encounters.end() ? pad_of.at(id) : (*enc->second)["pad_id"].get<std::string>();
        auto add = [&](const std::string &track, const J &leg) {
            J r = route_record(id, kind.c_str(), pad, "pickup");
            r["leg_id"] = leg;
            if (enc != encounters.end()) r["encounter_id"] = (*enc->second)["id"];
            uses[track].push_back(r);
        };
        if (entry.contains("legs")) for (const auto &leg : entry["legs"]) add(leg["track_id"], leg["id"]);
        else {
            add(entry["track_id"], nullptr);
            if (enc != encounters.end()) {
                const int garage = std::stoi(pad.substr(7));
                const int boss = garage == 4 ? 4 + number((*enc->second)["index"], 0, 1) : garage;
                if (boss >= 0 && boss < 6) out.sanity_boss_tracks[boss] = entry["track_id"];
            }
        }
    }
    std::map<std::string, J> measured;
    // `pin` is a sanity row's lev_sha256. Cortex Vortex rows name the bundled
    // main LEV; a room from before CV1 named the all-in-one build, whose
    // crates are 1.1.5's at the same places and in the same order, so it
    // keeps its own catalog row (CTR_CFG_CORTEX_LEGACY_LEV_SHA256).
    auto measured_inventory = [&](const J &track, const J *pin = nullptr) -> const J * {
        std::string key;
        if (track["origin"] == "retail") key = "retail:" + std::to_string(number(track["retail_id"], 0, 17));
        else if (track["origin"] == "cortex_vortex") key = rules_version < 2 ? "cortex_vortex"
            : pin && *pin == CTR_CFG_CORTEX_LEGACY_LEV_SHA256 ? CTR_CFG_CORTEX_LEGACY_LEV_SHA256
            : CTR_CFG_CORTEX_LEV_SHA256;
        else if (track.contains("named_block")) {
            const J &pkg = *packages.at(track["package_id"]);
            for (const auto &file : pkg["files"]) if (file["role"] == "lev") key = file["sha256"];
            const auto &catalog = registry.at("sanity");
            const auto &reviewed_counts = registry.at("pickup_ruling_counts");
            const std::string source = ruling_pin(out, pkg, key,
                [&](const std::string &pin) { return catalog.contains(pin) || reviewed_counts.contains(pin); });
            if (!inherited_ruling_history(out, pkg, key) && catalog.contains(key)) return &catalog.at(key);
            const J &caps = pkg["capabilities"];
            if (caps.contains("weapon_crates") && caps.contains("fruit_crates") && caps.contains("loose_fruit")) {
                const J needs = catalog.contains(source) ? catalog.at(source).at("needs") : J::object();
                measured[key] = J{{"lev_sha256", key}, {"counts", J::array({caps["weapon_crates"], caps["fruit_crates"], caps["loose_fruit"]})}, {"needs", needs}};
                return &measured.at(key);
            }
            return nullptr;
        }
        return registry.at("sanity").contains(key) ? &registry.at("sanity").at(key) : nullptr;
    };
    // WF1: a frozen fill's weapon crates follow the LEV's own as ordinals
    // count + 1 on, each with the `needs` of its fill row (apworld sanity.py).
    auto with_fill = [&](const J &track, const J *inv) -> const J * {
        if (!inv || !track.contains("named_block")) return inv;
        const J &pkg = *packages.at(track["package_id"]);
        if (!pkg.contains("fill") || pkg["fill"]["families"]["weapon_crates"].empty()) return inv;
        static const char *const ranks[] = {"none", "boost", "usf"};
        J adjusted = *inv;
        const int lev = adjusted["counts"][0].get<int>();
        const J &weapons = pkg["fill"]["families"]["weapon_crates"];
        adjusted["counts"][0] = lev + (int)weapons.size();
        if (!adjusted["needs"].contains("weapon_crate")) adjusted["needs"]["weapon_crate"] = J::object();
        for (size_t i = 0; i < weapons.size(); ++i)
            adjusted["needs"]["weapon_crate"][std::to_string(lev + 1 + (int)i)] =
                J{{"needs", ranks[number(weapons[i]["rank"], 0, 2)]}};
        const std::string key = "fill:" + text(track["id"]);
        measured[key] = adjusted;
        return &measured.at(key);
    };
    auto inventory = [&](const J &track, const J *pin = nullptr) -> const J * {
        return with_fill(track, measured_inventory(track, pin));
    };
    std::set<std::string> wanted;
    for (const auto &t : tracks) {
        const auto &routes = uses[t.first];
        const bool eligible = std::any_of(routes.begin(), routes.end(),
                                          [&](const J &route) { return allowed_pickup_route(p, route); });
        if (eligible && inventory(*t.second)) wanted.insert(t.first);
    }
    need(s["tracks"].is_array() && s["tracks"].size() == wanted.size(), "sanity track count mismatch");
    static const char *const kinds[] = {"weapon_crate", "wumpa_crate", "wumpa_fruit"};
    static const char *const suffix[] = {"Weapon Crate", "Wumpa Crate", "Wumpa Fruit"};
    static const int budgets[] = {80, 16, 32};
    for (const auto &row : s["tracks"]) {
        fields(row, {"track_id", "lev_sha256", "counts", "weapon_crate", "wumpa_crate", "wumpa_fruit"});
        const std::string tid = text(row["track_id"]);
        need(wanted.erase(tid) == 1, "unknown or duplicate sanity track");
        const J &track = *tracks.at(tid), &inv = *inventory(track, &row["lev_sha256"]);
        need(row["lev_sha256"] == inv["lev_sha256"] && row["counts"] == inv["counts"], "sanity revision/count mismatch");
        const J *pkg = track["origin"] == "custom" ? packages.at(track["package_id"]) : nullptr;
        const std::string lev = row["lev_sha256"];
        const J *reviewed_counts = pkg ? reviewed_pickup_counts(out, *pkg, lev) : nullptr;
        const J *history = pkg ? inherited_ruling_history(out, *pkg, lev) : nullptr;
        const std::string raw_key = pkg ? text(pkg->at("content_id")) : std::string{};
        // A4-52, project ruling 2026-11-11: use the same data-driven eligibility
        // filter as the apworld. Older rooms that applied a raw-key ruling
        // persisted its settings, so keep their original selection.
        auto ruled_out = [&](const char *kind, int ordinal) {
            if (!pkg || difficulty >= 2 || blue) return false;
            const auto &rules = registry.at("object_rulings").at("rulings");
            const J *row = object_ruling(rules, raw_key, kind, ordinal, pkg->at("revision"), lev,
                                         history, out.track_ruling_version, context);
            return row && row->at("hard_rank") != 0;
        };
        ctr_content_sanity dest{};
        copy(dest.track_id, row["track_id"]); copy(dest.lev_sha256, row["lev_sha256"]);
        std::set<int> excluded_by_kind[3];
        // The current LEV supplies counts. The reviewed LEV supplies exclusions by model family.
        {
            const auto &catalog = registry.at("sanity_exclusions");
            std::string digest = row["lev_sha256"];
            if (track["origin"] == "custom") {
                const J &pkg = *packages.at(track["package_id"]);
                digest = ruling_pin(out, pkg, digest,
                                    [&](const std::string &pin) { return catalog.contains(pin); });
            }
            if (catalog.contains(digest)) for (const auto &c : catalog[digest]) {
                if (c.value("sanity_since", 1) > exclusion_version) continue;
                const std::string family = c.value("family", std::string("weapon_crate"));
                for (int f = 0; f < 3; ++f) if (family == kinds[f]) excluded_by_kind[f].insert(number(c["ordinal"], 1, 65536));
            }
        }
        std::set<int> &excluded = excluded_by_kind[0];
        if (track["origin"] == "custom" && pkg && pkg->contains("fill")) {
            // WF1: an AP box standing at a filled weapon crate's exact
            // position and angle takes that crate's seat (ap_boxes.c).
            const J &weapons = pkg->at("fill")["families"]["weapon_crates"];
            const int lev = inv["counts"][0].get<int>() - (int)weapons.size();
            // The spot positions are this pad's box checks' `spot` fields; the
            // check records are read after this section, so take them here.
            for (const auto &pad : out.pads) if (standalone.count(tid) && tid == pad.track_id && pad.box_spots) {
                const std::set<int64_t> codes(pad.box_locations, pad.box_locations + pad.box_count);
                for (const auto &check : p.at("checks"))
                    if (check.is_object() && check.contains("spot") && check.contains("location") &&
                        check["location"].is_number_integer() && codes.count(check["location"].get<int64_t>()))
                        for (size_t i = 0; i < weapons.size(); ++i)
                            if (weapons[i]["pos"] == check["spot"].value("pos", J()) &&
                                weapons[i]["rot_y"] == check["spot"].value("rot_y", J()))
                                excluded.insert(lev + 1 + (int)i);
            }
        }
        if (track["origin"] == "custom") {
            for (const auto &pad : out.pads) if (standalone.count(tid) && tid == pad.track_id && !pad.box_spots) {
                const std::set<int> carried(pad.box_excluded, pad.box_excluded + pad.box_excluded_count);
                if (exclusion_version == 1 && count)
                    need(std::includes(excluded.begin(), excluded.end(), carried.begin(), carried.end()),
                         "fallback exclusions require the updated sanity catalog");
                // Version 1 preserves the historical pool. New plans use
                // the carried exclusions to reproduce the physical seats.
                const bool legacy = exclusion_version == 1 && !pad.box_reviewed_crates;
                const auto &seat_excluded = legacy ? excluded : carried;
                const int raw_count = legacy ? inv["counts"][0].get<int>()
                    : pad.box_reviewed_crates ? pad.box_reviewed_crates : pad.box_crates;
                std::vector<int> pool;
                for (int n = 1; n <= raw_count; ++n) if (!seat_excluded.count(n)) pool.push_back(n);
                const int seats = std::min((int)pool.size(), pad.box_count);
                for (int i = 0; i < seats; ++i) excluded.insert(pool[i * pool.size() / seats]);
            }
        }
        for (int f = 0; f < 3; ++f) {
            dest.counts[f] = number(row["counts"][f], 0, 65536);
            std::set<int> pool;
            const J needs = inv.at("needs").value(kinds[f], J::object());
            int limit = std::min(dest.counts[f], budgets[f]);
            // WF1: filled weapon crates of a LEV measured without its own sit
            // outside a reviewed prefix and take their rank from the fill row.
            const int filled = f == 0 && pkg && pkg->contains("fill") &&
                pkg->at("capabilities").value("weapon_crates", -1) == 0
                ? (int)pkg->at("fill")["families"]["weapon_crates"].size() : 0;
            if (reviewed_counts && !filled) limit = std::min(limit, reviewed_counts->at(f).get<int>());
            for (int n = 1; n <= limit; ++n) {
                const J row_rule = needs.value(std::to_string(n), J::object());
                const J rule = row_rule.value("since", 0) <= rules_version ? row_rule : J::object();
                if (!excluded_by_kind[f].count(n) && rule.value("shortcut", 0) <= tier &&
                    (n <= dest.counts[f] - filled ? !ruled_out(kinds[f], n) : true)) pool.insert(n);
            }
            const int selected = f ? ((wumpa & f) ? (int)pool.size() : 0) : std::min((int)pool.size(), count);
            need(row[kinds[f]].is_array() && row[kinds[f]].size() == (size_t)selected, "sanity pick count mismatch");
            int previous = 0;
            for (const auto &ordinal : row[kinds[f]]) {
                const int n = number(ordinal, 1, budgets[f]);
                need(n > previous && pool.count(n), "invalid sanity ordinal"); previous = n;
                const std::string name = track.value("room_title", track["display_name"].get<std::string>());
                const int64_t code = registry.at("sanity_locations").at(name).at(f).get<int64_t>() + n - 1;
                J c{{"location", code}, {"owner", tid}, {"kind", kinds[f]},
                    {"display_name", name + ": " + suffix[f] + " " + std::to_string(n)}, {"routes", uses.at(tid)}};
                need(expected.emplace(code, c).second, "duplicate sanity identity");
                dest.locations[f][n - 1] = code;
                for (const auto &r : uses.at(tid)) {
                    const std::string pad = r["own_pad_id"];
                    if (pad.rfind("pad:", 0) == 0) out.sanity_pads[std::stoi(pad.substr(4))][f].insert(code);
                }
            }
        }
        out.sanity.emplace(tid, dest);
    }
}
