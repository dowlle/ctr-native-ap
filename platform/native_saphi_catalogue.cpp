#include <platform/native_saphi_catalogue.h>
#include <platform/native_custom_music.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

struct CustomSaphiCatalogue { std::vector<CustomSaphiRevision> rows; };
namespace {
using json = nlohmann::json;
int source_integer(const json &value, int maximum)
{
    if (!value.is_number_integer() || value < 1 || value > maximum)
        throw std::runtime_error("Invalid Saphi source integer");
    return value.get<int>();
}
template<size_t N> void source_text(const json &value, char (&out)[N], bool empty = false)
{
    if (!value.is_string()) throw std::runtime_error("Invalid Saphi display text");
    auto text = value.get<std::string>();
    if ((!empty && text.empty()) || text.size() >= N ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Saphi display text exceeds supported bounds");
    std::memcpy(out, text.c_str(), text.size() + 1);
}
/* Known tags become bits. Saphi adds modes over time (ring_rally, 2026-09):
   a tag this client does not know is left out of the bits instead of failing
   the whole catalogue, and goes into *other so revisions that differ only in
   such a tag still pair apart. */
unsigned int source_modes(const json &modes, std::string *other)
{
    static const char *names[] = {"arcade", "time_trial", "relic_race", "ctr_challenge", "crystal_challenge", "battle"};
    if (!modes.is_array() || modes.size() > 16) throw std::runtime_error("Invalid Saphi mode tags");
    unsigned int result = 0;
    std::set<std::string> unknown;
    for (const auto &mode : modes)
    {
        if (!mode.is_string()) throw std::runtime_error("Invalid Saphi mode name");
        unsigned int bit = 0;
        for (unsigned int i = 0; i < 6; i++) if (mode == names[i]) bit = 1u << i;
        if (!bit)
        {
            if (!unknown.insert(mode.get<std::string>()).second) throw std::runtime_error("Duplicate Saphi mode tag");
            continue;
        }
        if (result & bit) throw std::runtime_error("Unknown or duplicate Saphi mode tag");
        result |= bit;
    }
    if (other)
    {
        other->clear();
        for (const auto &name : unknown) { *other += name; *other += '\n'; }
    }
    return result;
}
/* Project Saphi's own permission setting (A4-74, filled in by A4-79).
   Since 2026-11-13 every track object carries a track-level "permissions"
   object: uploaders say which project types (local, private, public; non-
   profit, commercial) may use the track and whether credit is required.
   Saphi publishes no per-file field, so the track's value is returned for
   file rows too and the revision rule below sees all three agree. The
   CTR Archipelago is a free public project, so the one field that answers
   for it is public_non_profit:
     permissions missing or not an object, specified not a boolean, or
     specified false (the "unspecified" state of every track uploaded before
     the fields existed)                                         -> unknown
     specified true, public_non_profit true (a boolean)          -> allow
     specified true, public_non_profit false (a boolean)         -> deny
     specified true, public_non_profit missing or not a boolean  -> unknown
   reach, monetization and saphi are not part of the decision, and nothing
   in permissions ever rejects the catalogue: a malformed object only makes
   that track unknown. auto_download is Saphi's own client hint (A4-67) and
   is not permission. */
const json *saphi_permissions_object(const json &track)
{
    if (!track.is_object()) return nullptr;
    auto it = track.find("permissions");
    if (it == track.end() || !it->is_object()) return nullptr;
    auto specified = it->find("specified");
    if (specified == it->end() || !specified->is_boolean() || !specified->get<bool>()) return nullptr;
    return &*it;
}
int saphi_permission(const json &track, const json *file)
{
    (void)file; /* No per-file field exists; the track level answers. */
    const json *permissions = saphi_permissions_object(track);
    if (!permissions) return CUSTOM_SAPHI_PERMISSION_UNKNOWN;
    auto field = permissions->find("public_non_profit");
    if (field == permissions->end() || !field->is_boolean()) return CUSTOM_SAPHI_PERMISSION_UNKNOWN;
    return field->get<bool>() ? CUSTOM_SAPHI_PERMISSION_ALLOW : CUSTOM_SAPHI_PERMISSION_DENY;
}
/* Whether the author asks for credit. Meaningful only once the author has
   specified; a missing or non-boolean value stays unknown. Stored so a later
   credits job can use it; nothing reads it for the decision. */
int saphi_credit_required(const json &track)
{
    const json *permissions = saphi_permissions_object(track);
    if (!permissions) return CUSTOM_SAPHI_CREDIT_UNKNOWN;
    auto field = permissions->find("credit_required");
    if (field == permissions->end() || !field->is_boolean()) return CUSTOM_SAPHI_CREDIT_UNKNOWN;
    return field->get<bool>() ? CUSTOM_SAPHI_CREDIT_REQUIRED : CUSTOM_SAPHI_CREDIT_NOT_REQUIRED;
}
/* A revision is allowed by Saphi only when the track and both its files
   allow; any deny denies. */
int saphi_revision_permission(int track, int lev, int vrm)
{
    if (track == CUSTOM_SAPHI_PERMISSION_DENY || lev == CUSTOM_SAPHI_PERMISSION_DENY || vrm == CUSTOM_SAPHI_PERMISSION_DENY)
        return CUSTOM_SAPHI_PERMISSION_DENY;
    if (track == CUSTOM_SAPHI_PERMISSION_ALLOW &&
        lev == CUSTOM_SAPHI_PERMISSION_ALLOW && vrm == CUSTOM_SAPHI_PERMISSION_ALLOW)
        return CUSTOM_SAPHI_PERMISSION_ALLOW;
    return CUSTOM_SAPHI_PERMISSION_UNKNOWN;
}
CustomSaphiMedia source_media(const json &media, int track)
{
    CustomSaphiMedia result{};
    result.id = source_integer(media.at("id"), 2147483647);
    /* Saphi also publishes checksum-reference rows with no downloadable
       payload. Keep them visible, but never manufacture a download URL. */
    if (media.at("download_url").is_null() && media.at("file_size") == 0) return result;
    result.bytes = source_integer(media.at("file_size"), 8 * 1024 * 1024);
    const auto &crc = media.at("crc32_hash");
    if (!crc.is_number_integer() || crc < 0 || crc > UINT32_MAX)
        throw std::runtime_error("Invalid Saphi media checksum");
    result.crc32 = crc.get<uint32_t>();
    std::string expected = "/api/v3/tracks/" + std::to_string(track) + "/downloads/" + std::to_string(result.id);
    std::string path = media.at("download_url").get<std::string>();
    const std::string origin = "https://www.projectsaphi.com";
    if (path.compare(0, origin.size(), origin) == 0) path.erase(0, origin.size());
    if (path != expected) throw std::runtime_error("Saphi media URL does not match its source identity");
    std::snprintf(result.path, sizeof result.path, "%s", path.c_str());
    return result;
}
}

extern "C" int CustomSaphi_ParseCatalogue(const char *input, size_t size,
    CustomSaphiCatalogue **out, char *error, size_t errorSize)
{
    try
    {
        if (!input || !size || size > 4 * 1024 * 1024 || !out || *out)
            throw std::runtime_error("Expected bounded Saphi catalogue and empty output");
        json root = json::parse(input, input + size);
        if (!root.is_object() || !root.at("data").is_array() || root["data"].size() > 1024)
            throw std::runtime_error("Invalid Saphi catalogue response");
        auto result = std::make_unique<CustomSaphiCatalogue>();
        std::set<int> trackIDs, mediaIDs;
        for (const auto &track : root["data"])
        {
            if (!track.is_object()) throw std::runtime_error("Invalid Saphi track");
            int id = source_integer(track.at("id"), 2147483647);
            if (!trackIDs.insert(id).second) throw std::runtime_error("Duplicate Saphi track identity");
            if (!track.at("is_active").is_boolean()) throw std::runtime_error("Invalid Saphi active flag");
            if (!track["is_active"].get<bool>()) continue;
            if (track.at("type") != "race" && track.at("type") != "battle") continue;
            CustomSaphiRevision base{};
            base.trackID = id;
            source_text(track.at("name"), base.title);
            source_text(track.at("author"), base.author, true);
            /* Optional: a missing or odd author_id leaves the author unknown
               (so the track is not offered), never fails the catalogue. */
            if (track.contains("author_id") && track["author_id"].is_number_integer() &&
                track["author_id"] >= 1 && track["author_id"] <= 100000000)
                base.authorID = track["author_id"].get<int>();
            const int trackPermission = saphi_permission(track, nullptr);
            base.creditRequired = saphi_credit_required(track);
            if (track.contains("lap_count") && !track["lap_count"].is_null())
                base.sourceLaps = source_integer(track["lap_count"], 2147483647);
            const auto &downloads = track.at("downloads");
            if (!downloads.is_array() || downloads.size() > 512) throw std::runtime_error("Too many Saphi media rows");
            struct Pair { CustomSaphiRevision revision{}; int levCount = 0, vrmCount = 0; bool levCurrent = false, vrmCurrent = false;
                          int levPermission = CUSTOM_SAPHI_PERMISSION_UNKNOWN, vrmPermission = CUSTOM_SAPHI_PERMISSION_UNKNOWN; };
            std::map<std::tuple<std::string, unsigned int, std::string>, Pair> pairs;
            /* Audio is optional: a malformed or ambiguous .sca row leaves the
               track without its own music instead of failing the catalogue. */
            CustomSaphiMedia sca{};
            int currentSca = 0;
            for (const auto &media : downloads)
            {
                try
                {
                    if (!media.is_object() || media.at("type") != "sca" || media.at("is_current") != true) continue;
                    auto file = source_media(media, id);
                    if (!file.bytes || file.bytes > CTR_SCA_MAX_BYTES) continue;
                    sca = file;
                    currentSca++;
                }
                catch (const std::exception &) {}
            }
            if (currentSca != 1) sca = CustomSaphiMedia{};
            for (const auto &media : downloads)
            {
                const auto &type = media.at("type");
                if (type != "lev" && type != "vrm") continue;
                CustomSaphiRevision row = base;
                source_text(media.at("version"), row.version);
                std::string otherModes;
                row.modeTags = source_modes(media.at("modes"), &otherModes);
                auto file = source_media(media, id);
                if (!mediaIDs.insert(file.id).second) throw std::runtime_error("Duplicate Saphi media identity");
                if (!media.at("is_current").is_boolean()) throw std::runtime_error("Invalid Saphi current flag");
                auto &pair = pairs[std::make_tuple(std::string(row.version), row.modeTags, otherModes)];
                if (!pair.levCount && !pair.vrmCount) pair.revision = row;
                if (type == "lev") { pair.revision.lev = file; pair.levCount++; pair.levCurrent = media["is_current"]; pair.levPermission = saphi_permission(track, &media); }
                else { pair.revision.vrm = file; pair.vrmCount++; pair.vrmCurrent = media["is_current"]; pair.vrmPermission = saphi_permission(track, &media); }
            }
            for (auto &item : pairs)
            {
                auto &pair = item.second;
                if (pair.levCount != 1 || pair.vrmCount != 1)
                    std::snprintf(pair.revision.disabledReason, sizeof pair.revision.disabledReason,
                        "%s %s", pair.levCount != 1 ? pair.levCount ? "Ambiguous" : "Missing" : pair.vrmCount ? "Ambiguous" : "Missing",
                        pair.levCount != 1 ? "LEV" : "VRM");
                else if (!pair.revision.lev.bytes || !pair.revision.vrm.bytes)
                    std::snprintf(pair.revision.disabledReason, sizeof pair.revision.disabledReason,
                        "Files not hosted");
                pair.revision.current = pair.levCurrent && pair.vrmCurrent;
                pair.revision.saphiPermission = saphi_revision_permission(trackPermission, pair.levPermission, pair.vrmPermission);
                if (pair.revision.current) pair.revision.sca = sca;
                result->rows.push_back(pair.revision);
                if (result->rows.size() > 1024) throw std::runtime_error("Too many Saphi revisions");
            }
        }
        auto folded = [](const char *text) {
            std::string value(text);
            for (char &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            return value;
        };
        std::sort(result->rows.begin(), result->rows.end(), [&](const auto &a, const auto &b) {
            auto at = folded(a.title), bt = folded(b.title);
            if (at != bt) return at < bt;
            if (a.current != b.current) return a.current > b.current;
            int version = std::strcmp(a.version,b.version);
            if (version) return version < 0;
            if (a.trackID != b.trackID) return a.trackID < b.trackID;
            if (a.lev.id != b.lev.id) return a.lev.id < b.lev.id;
            return a.vrm.id < b.vrm.id;
        });
        *out = result.release();
        if (error && errorSize) error[0] = 0;
        return 1;
    }
    catch (const std::exception &e)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", e.what());
        return 0;
    }
}
extern "C" size_t CustomSaphi_Count(const CustomSaphiCatalogue *c) { return c ? c->rows.size() : 0; }
extern "C" const CustomSaphiRevision *CustomSaphi_Row(const CustomSaphiCatalogue *c, size_t index)
{ return c && index < c->rows.size() ? &c->rows[index] : nullptr; }
extern "C" void CustomSaphi_Free(CustomSaphiCatalogue **c) { if (c) { delete *c; *c = nullptr; } }
