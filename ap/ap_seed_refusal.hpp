#ifndef CTR_AP_SEED_REFUSAL_HPP
#define CTR_AP_SEED_REFUSAL_HPP

#include "ap_version.h"
#include <nlohmann/json.hpp>
#include <string>

// A checksum identifies item/location tables, not the content-plan format.
// Keep family advice tentative, especially for modified apworlds.
namespace ap_seed_refusal {
inline std::string family(const std::string &checksum)
{
    if (checksum == "06576a4d1654a8c0ecaa9298efc5066c9f6dbab1") return "ALPHA1/ALPHA2";
    if (checksum == "dfef5220b67ad917f534bac828b5bf882156caad") return "ALPHA3";
    if (checksum == "df96b8611492503ee9aeeb1b5fec2ad2b9779ae5") return "ALPHA4/ALPHA5";
    return {};
}

inline std::string text_field(const nlohmann::json &object, const char *key)
{
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// Only bounded, version-shaped labels enter the credits font. The untrusted
// metadata is still recorded as escaped JSON in the diagnostic log.
inline std::string version_label(std::string value)
{
    if (value.empty() || value.size() > 20 || value[0] < '0' || value[0] > '9') return {};
    for (char &c : value) {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return {};
    }
    // Require three numeric version components, then an optional nonempty
    // prerelease label. Glyph safety alone would accept false advice like 0.
    size_t pos = 0;
    for (int component = 0; component < 3; ++component) {
        const size_t begin = pos;
        while (pos < value.size() && value[pos] >= '0' && value[pos] <= '9') ++pos;
        if (pos == begin || (pos > begin + 1 && value[begin] == '0')) return {};
        if (component < 2 && (pos == value.size() || value[pos++] != '.')) return {};
    }
    if (pos == value.size()) return value;
    if (value[pos++] != '-' || pos == value.size()) return {};
    bool need_part = true;
    for (; pos < value.size(); ++pos) {
        if (value[pos] == '.' || value[pos] == '-') {
            if (need_part) return {};
            need_part = true;
        } else need_part = false;
    }
    if (need_part) return {};
    return value;
}

inline std::string build_label(const nlohmann::json &slot)
{
    std::string value = text_field(slot, "build_version");
    const auto co = slot.find("ctr_options");
    if (value.empty() && co != slot.end() && co->is_object())
        value = text_field(*co, "build_version");
    if (value.empty() && co != slot.end() && co->is_object()) {
        const std::string world = text_field(*co, "world_version");
        // Released 0.3.0 alphas emit only "0.3.0" here, not their build.
        if (world.find('-') != std::string::npos) value = world;
    }
    return version_label(value);
}

inline std::string message(const nlohmann::json &slot, const std::string &checksum)
{
    const std::string build = build_label(slot);
    const auto plan = slot.find("content_plan");
    if (plan != slot.end() && plan->is_object() &&
        (plan->contains("version") || plan->contains("room_compatibility"))) {
        // The connection screen has two 33-character lines at its widest font.
        const auto options = slot.find("ctr_options");
        std::string source;
        if (options != slot.end() && options->is_object())
            source = version_label(text_field(*options, "world_version"));
        if (source.empty()) source = build.empty() ? "UNKNOWN" : build;
        std::string current = CTR_AP_VERSION;
        if (!current.empty() && current.front() == 'v') current.erase(0, 1);
        current = version_label(current);
        if (current.empty()) current = "UNKNOWN";
        return (source.size() > 17 ? "ROOM BUILD " : "GENERATED WITH ") + source +
               (current.size() > 18 ? ". THIS BUILD " : ". THIS BUILD IS ") + current + ".";
    }
    if (!build.empty()) return "CTR APWORLD VERSION MISMATCH. USE CLIENT " + build + ".";
    const std::string hint = family(checksum);
    if (!hint.empty()) return "CTR APWORLD VERSION MISMATCH. CHECK " + hint + " CLIENT.";
    return "CTR APWORLD VERSION MISMATCH. ASK HOST FOR MATCHING CLIENT.";
}

inline std::string diagnostics(const nlohmann::json &slot, const std::string &checksum)
{
    nlohmann::json metadata = {{"datapackage_checksum", checksum}};
    for (const char *key : {"schema_version", "build_version"})
        if (slot.contains(key)) metadata[key] = slot[key];
    const auto co = slot.find("ctr_options");
    if (co != slot.end() && co->is_object())
        for (const char *key : {"schema_version", "world_version", "build_version"})
            if (co->contains(key)) metadata[std::string("ctr_options.") + key] = (*co)[key];
    return "[AP NET] This room may use a different CTR apworld version or modified/invalid slot data. "
           "Use the client matching the host's apworld build. A checksum family only identifies "
           "item/location tables, not an exact build or compatible content plan. Metadata: " + metadata.dump() + "\n";
}
}
#endif
