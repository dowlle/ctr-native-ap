#pragma once

#include "ap_track_key_aliases.hpp"
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>

namespace content_plan {

inline const char *canonical_track_key(const char *key) {
    for (const auto &alias : kTrackKeyAliases)
        if (std::strcmp(key, alias[0]) == 0) return alias[1];
    return key;
}

inline const nlohmann::json *object_ruling(const nlohmann::json &rules, const std::string &raw_key,
                                          const char *kind, int ordinal, const nlohmann::json &revision,
                                          const std::string &lev,
                                          const nlohmann::json *history, int version, bool legacy) {
    const std::string road = canonical_track_key(raw_key.c_str());
    auto matches_object = [&](const auto &rule) { return rule.at("kind") == kind && rule.at("ordinal") == ordinal; };
    auto for_source = [&](const auto &source_revision, const auto &pin) {
        const nlohmann::json *best = nullptr;
        for (const auto &rule : rules) {
            const auto &content = rule.at("content_id");
            if ((content != road && content != raw_key) || !matches_object(rule) || rule.at("lev_sha256") != pin)
                continue;
            const int since = rule.value("since", 0);
            if (rule.contains("revision") && (rule.at("revision") != source_revision || since > version)) continue;
            if (!best || since > best->value("since", 0) ||
                (since == best->value("since", 0) && content == raw_key))
                best = &rule;
        }
        return best;
    };
    const auto *exact = for_source(revision, lev);
    if (exact && (!history || exact->contains("revision"))) return exact;
    if (history) {
        // Review identities belong to the package key. Road aliases can find
        // rows at those pins without sharing another copy's review history.
        for (const auto &review : *history)
            if (const auto *row = for_source(review.at("revision"), review.at("lev_sha256"))) return row;
    }
    if ((legacy && version == 0) || (version > 0 && version < 4)) {
        for (const auto &rule : rules)
            if (rule.at("content_id") == raw_key && matches_object(rule) && !rule.contains("revision"))
                return &rule;
    }
    return nullptr;
}

}
