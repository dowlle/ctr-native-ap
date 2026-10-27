// Included after the common seed rejection helpers in ap_seedcfg.cpp (#344).
// `ctr_options.lap_completion_checks: true` with `lap_checks: {version: 1,
// tracks: {"<LevelID>": code}, custom_slots: {"<slot>": code}}`. Absent both:
// the option is off. Every code must be the frozen one for its key; anything
// else refuses the seed, as an inconsistent cup sweep block does.
static bool parse_lap_checks(const nlohmann::json &j, const nlohmann::json &opt, int schema)
{
    auto flag = opt.find("lap_completion_checks");
    const bool block = j.contains("lap_checks");
    if (flag == opt.end() && !block) return true;
    try {
        if (flag == opt.end() || !flag->is_boolean()) throw 0;
        if (!flag->get<bool>()) { if (block) throw 0; return true; }
        if (schema < 18 || !block) throw 0;
        const auto &b = j.at("lap_checks");
        if (!b.is_object() || b.size() != 3 || !b.contains("version") || !b["version"].is_number_integer() ||
            b["version"] != 1 || !b.contains("tracks") || !b["tracks"].is_object() ||
            !b.contains("custom_slots") || !b["custom_slots"].is_object()) throw 0;
        AP_LapChecks parsed;
        AP_LapChecksReset(&parsed);
        parsed.enabled = 1;
        for (auto it = b["tracks"].begin(); it != b["tracks"].end(); ++it) {
            const std::string &key = it.key();
            if (key.empty() || key.size() > 3 || (key.size() > 1 && key[0] == '0') ||
                key.find_first_not_of("0123456789") != std::string::npos || !it.value().is_number_integer()) throw 0;
            const int lid = std::stoi(key);
            const long code = it.value().get<long>();
            if (lid == AP_LAP_CORTEX_KEY) {
                if (code != AP_LAP_CORTEX_CODE) throw 0;
                parsed.cortex = code;
            } else {
                if (lid < 0 || lid >= AP_LAP_RETAIL_TRACKS || code != AP_LapRetailCode(lid)) throw 0;
                parsed.tracks[lid] = code;
            }
        }
        const auto &custom = b["custom_slots"];
        if (custom.size() > 1) throw 0;
        for (auto it = custom.begin(); it != custom.end(); ++it) {
            const std::string &key = it.key();
            if (key.empty() || key.size() > 3 || key[0] == '0' ||
                key.find_first_not_of("0123456789") != std::string::npos || !it.value().is_number_integer()) throw 0;
            const int slot = std::stoi(key);
            // Only the seed's one legacy custom track has a lap check.
            if (!ctr_cfg.custom_tracks_ok || slot != ctr_cfg.custom_track.slot) throw 0;
            const long want = ctr_cfg.custom_track.named_block >= 0
                ? (long)ap_named_location_code(ctr_cfg.custom_track.named_block, AP_NAMED_OFF_LAP)
                : AP_LAP_CUSTOM_CODE_BASE + slot - 1;
            if (it.value().get<long>() != want) throw 0;
            parsed.custom_slot = slot;
            parsed.custom = want;
        }
        ctr_lap_checks = parsed;
        return true;
    } catch (...) {
        hit_reject("lap_checks: inconsistent option, track keys or lap codes");
        return false;
    }
}
