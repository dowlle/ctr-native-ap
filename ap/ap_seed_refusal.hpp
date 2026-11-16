#ifndef CTR_AP_SEED_REFUSAL_HPP
#define CTR_AP_SEED_REFUSAL_HPP

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
    if (value.empty() || value.size() > 20 || value[0] < '0' || value[0] > '9') return {};
    for (char &c : value) {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return {};
    }
    return value;
}

inline std::string message(const nlohmann::json &slot, const std::string &checksum)
{
    const std::string build = build_label(slot);
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
