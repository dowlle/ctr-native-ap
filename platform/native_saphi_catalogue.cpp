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
unsigned int source_modes(const json &modes)
{
    static const char *names[] = {"arcade", "time_trial", "relic_race", "ctr_challenge", "crystal_challenge", "battle"};
    if (!modes.is_array() || modes.size() > 6) throw std::runtime_error("Invalid Saphi mode tags");
    unsigned int result = 0;
    for (const auto &mode : modes)
    {
        if (!mode.is_string()) throw std::runtime_error("Invalid Saphi mode name");
        unsigned int bit = 0;
        for (unsigned int i = 0; i < 6; i++) if (mode == names[i]) bit = 1u << i;
        if (!bit || (result & bit)) throw std::runtime_error("Unknown or duplicate Saphi mode tag");
        result |= bit;
    }
    return result;
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
            if (track.contains("lap_count") && !track["lap_count"].is_null())
                base.sourceLaps = source_integer(track["lap_count"], 2147483647);
            const auto &downloads = track.at("downloads");
            if (!downloads.is_array() || downloads.size() > 512) throw std::runtime_error("Too many Saphi media rows");
            struct Pair { CustomSaphiRevision revision{}; int levCount = 0, vrmCount = 0; bool levCurrent = false, vrmCurrent = false; };
            std::map<std::pair<std::string, unsigned int>, Pair> pairs;
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
                row.modeTags = source_modes(media.at("modes"));
                auto file = source_media(media, id);
                if (!mediaIDs.insert(file.id).second) throw std::runtime_error("Duplicate Saphi media identity");
                if (!media.at("is_current").is_boolean()) throw std::runtime_error("Invalid Saphi current flag");
                auto &pair = pairs[{row.version, row.modeTags}];
                if (!pair.levCount && !pair.vrmCount) pair.revision = row;
                if (type == "lev") { pair.revision.lev = file; pair.levCount++; pair.levCurrent = media["is_current"]; }
                else { pair.revision.vrm = file; pair.vrmCount++; pair.vrmCurrent = media["is_current"]; }
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
