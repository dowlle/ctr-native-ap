#pragma once

#include "native_track_identity_data.hpp"
#include "../ap/ap_track_key_aliases.hpp"
extern "C" {
#include <platform/native_custom_content_verify.h>
}
#include <platform/native_custom_package.h>
#include <string>
#include <utility>

#ifdef CTR_TRACK_IDENTITY_LIVE
#include "../ap/ap_track_registry.h"
#endif

namespace TrackIdentity
{
template <size_t N>
const char *find(const std::string &key, const char *const (&rows)[N][2])
{
    if (!key.empty())
        for (const auto &row : rows)
            if (key == row[0]) return row[1];
    return nullptr;
}

inline void refresh(const std::string &cache)
{
#ifdef CTR_TRACK_IDENTITY_LIVE
    if (ap_track_registry_status(nullptr, nullptr, 0) == AP_TRACK_REGISTRY_IDLE)
        ap_track_registry_refresh(cache.c_str());
#else
    (void)cache;
#endif
}

inline bool resolve(const unsigned char *lev, size_t bytes, const std::string &levSha,
                    std::string &key, std::string &title)
{
    char road[NATIVE_SHA256_HEX_BYTES] = {};
    CustomContentVerify_RoadHash(lev, bytes, road);
    std::string candidate = key, name;
#ifdef CTR_TRACK_IDENTITY_LIVE
    const bool have_live = ap_track_registry_identity(levSha, road, candidate, name);
#else
    const bool have_live = false;
#endif
    if (!have_live)
    {
        const char *match = find(levSha, TrackIdentityData::levs);
        if (!match) match = find(road, TrackIdentityData::roads);
        if (!match) match = find(candidate, kTrackKeyAliases);
        if (match) candidate = match;
        if (const char *known = find(candidate, TrackIdentityData::titles)) name = known;
    }
    if (name.empty() || !CustomPackage_ValidateTitle(name.data(), name.size())) return false;
    if (candidate.empty() || candidate.size() > 64 || candidate.front() == '-' ||
        candidate.back() == '-' || candidate.find("--") != std::string::npos ||
        candidate.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos) return false;
    key = std::move(candidate);
    title = std::move(name);
    return true;
}
}
