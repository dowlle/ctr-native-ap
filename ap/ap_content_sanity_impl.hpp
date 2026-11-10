// Included inside the content-plan parser namespace. No runtime dependencies.
static void sanity_checks(const J &p, State &out, const J &registry, std::map<int64_t, J> &expected) {
    const J &s = p.at("sanity");
    fields(s, {"itemboxsanity", "wumpasanity", "shortcut_knowledge", "tracks"});
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
    auto inventory = [&](const J &track) -> const J * {
        std::string key;
        if (track["origin"] == "retail") key = "retail:" + std::to_string(number(track["retail_id"], 0, 17));
        else if (track["origin"] == "cortex_vortex") key = "cortex_vortex";
        else if (track.contains("named_block")) {
            const J &pkg = *packages.at(track["package_id"]);
            for (const auto &file : pkg["files"]) if (file["role"] == "lev") key = file["sha256"];
            const J &caps = pkg["capabilities"];
            if (!registry.at("sanity").contains(key) && caps.contains("weapon_crates") && caps.contains("fruit_crates") && caps.contains("loose_fruit")) {
                measured[key] = J{{"lev_sha256", key}, {"counts", J::array({caps["weapon_crates"], caps["fruit_crates"], caps["loose_fruit"]})}, {"needs", J::object()}};
                return &measured.at(key);
            }
        }
        return registry.at("sanity").contains(key) ? &registry.at("sanity").at(key) : nullptr;
    };
    std::set<std::string> wanted;
    for (const auto &t : tracks) if (!uses[t.first].empty() && inventory(*t.second)) wanted.insert(t.first);
    need(s["tracks"].is_array() && s["tracks"].size() == wanted.size(), "sanity track count mismatch");
    static const char *const kinds[] = {"weapon_crate", "wumpa_crate", "wumpa_fruit"};
    static const char *const suffix[] = {"Weapon Crate", "Wumpa Crate", "Wumpa Fruit"};
    static const int budgets[] = {80, 16, 32};
    for (const auto &row : s["tracks"]) {
        fields(row, {"track_id", "lev_sha256", "counts", "weapon_crate", "wumpa_crate", "wumpa_fruit"});
        const std::string tid = text(row["track_id"]);
        need(wanted.erase(tid) == 1, "unknown or duplicate sanity track");
        const J &track = *tracks.at(tid), &inv = *inventory(track);
        need(row["lev_sha256"] == inv["lev_sha256"] && row["counts"] == inv["counts"], "sanity revision/count mismatch");
        ctr_content_sanity dest{};
        copy(dest.track_id, row["track_id"]); copy(dest.lev_sha256, row["lev_sha256"]);
        std::set<int> excluded_by_kind[3];
        // Reviewed exclusions are keyed by exact LEV and model family ("family" defaults to a weapon
        // crate) and apply to every origin (the retail Roo's Tubes crate and the Hot Air Skyway Wumpa
        // crate included), even on a track with authored spots.
        {
            const auto &catalog = registry.at("sanity_exclusions");
            const std::string digest = row["lev_sha256"];
            if (catalog.contains(digest)) for (const auto &c : catalog[digest]) {
                const std::string family = c.value("family", std::string("weapon_crate"));
                for (int f = 0; f < 3; ++f) if (family == kinds[f]) excluded_by_kind[f].insert(number(c["ordinal"], 1, 65536));
            }
        }
        std::set<int> &excluded = excluded_by_kind[0];
        if (track["origin"] == "custom") {
            for (const auto &pad : out.pads) if (standalone.count(tid) && tid == pad.track_id && !pad.box_spots) {
                std::vector<int> pool;
                for (int n = 1; n <= inv["counts"][0].get<int>(); ++n) if (!excluded.count(n)) pool.push_back(n);
                const int seats = std::min((int)pool.size(), pad.box_count);
                for (int i = 0; i < seats; ++i) excluded.insert(pool[i * pool.size() / seats]);
            }
        }
        for (int f = 0; f < 3; ++f) {
            dest.counts[f] = number(row["counts"][f], 0, 65536);
            std::set<int> pool;
            const J needs = inv.at("needs").value(kinds[f], J::object());
            for (int n = 1; n <= std::min(dest.counts[f], budgets[f]); ++n) {
                const J rule = needs.value(std::to_string(n), J::object());
                if (!excluded_by_kind[f].count(n) && rule.value("shortcut", 0) <= tier) pool.insert(n);
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
