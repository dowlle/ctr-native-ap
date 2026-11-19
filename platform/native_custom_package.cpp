#include <platform/native_custom_package.h>
#include <platform/native_sha256.h>
extern "C" {
#include <platform/native_custom_content_verify.h>
}
#include <platform/native_custom_tracks_policy.h>
#ifdef CTR_RECORDED_RACE
#include <platform/native_recorded_race_data.h>
#include "../ap/ap_navrec_playback_logic.h"
#endif
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <climits>
#include <memory>

// Package manifest parsing and the pinned sidecars (race settings, relic
// targets). Authoring builds only: CMakeLists.txt builds this file into the
// custom_package library when CTR_CUSTOM_PACKAGES is on.

using json = nlohmann::json;

extern "C" int CustomPackage_CopyManifestJSON(const CustomPackageOwned *package, void *destination,
                                              size_t capacity, size_t *written)
{
    if (written) *written = 0;
    try
    {
        CustomPackageManifest metadata;
        if (!destination || !CustomPackage_GetManifest(package, &metadata)) return 0;
        json manifest = {{"schema_version", 1}, {"package_uuid", metadata.uuid},
            {"version", metadata.version}, {"title", metadata.title},
            {"compatibility", {{"native_contract", 1}, {"apworld_contract", 1}}}, {"files", json::array()}};
        for (unsigned int i = 0; i < metadata.count; i++)
        {
            const auto &file = metadata.files[i];
            manifest["files"].push_back({{"role", file.role}, {"path", file.path},
                {"sha256", file.sha256}, {"bytes", file.bytes}});
        }
        auto text = manifest.dump(-1, ' ', false, json::error_handler_t::strict);
        CustomPackageManifest checked;
        if (text.size() > capacity || !CustomPackage_ParseManifest(text.data(), text.size(), metadata.sha256,
                                                                  &checked, nullptr, 0)) return 0;
        std::memcpy(destination, text.data(), text.size());
        if (written) *written = text.size();
        return 1;
    }
    catch (const std::exception &) { return 0; }
}

static void package_fields(const json &value, std::initializer_list<const char *> fields)
{
    if (!value.is_object() || value.size() != fields.size()) throw std::runtime_error("Unexpected manifest fields");
    for (const char *field : fields) if (!value.contains(field)) throw std::runtime_error("Missing manifest field");
}

static std::string package_text(const json &value, size_t maxBytes)
{
    if (!value.is_string()) throw std::runtime_error("Expected manifest string");
    auto text = value.get<std::string>();
    if (text.empty() || text.size() > maxBytes) throw std::runtime_error("Manifest text size invalid");
    for (unsigned char c : text) if (c < 32 || c == 127) throw std::runtime_error("Manifest text contains controls");
    return text;
}

static unsigned int package_integer(const json &value, unsigned int maximum)
{
    if (!value.is_number_integer() || value.is_boolean() || value < 1 || value > maximum)
        throw std::runtime_error("Manifest integer outside bounds");
    return value.get<unsigned int>();
}

static bool package_hex(const std::string &text)
{
    return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == std::string::npos;
}

#ifdef CTR_RECORDED_RACE
static std::vector<unsigned char> recorded_file(const CustomPackageOwned *package,
    const CustomPackageManifest &manifest, const char *role)
{
    for (unsigned int i = 0; i < manifest.count; i++)
    {
        const auto &file = manifest.files[i];
        if (std::strcmp(file.role, role)) continue;
        std::vector<unsigned char> bytes(file.bytes);
        if (!CustomPackage_CopyFile(package, role, bytes.data(), bytes.size(), nullptr))
            throw std::runtime_error("Cannot read owned race file");
        return bytes;
    }
    return {};
}

static json recorded_json(const std::vector<unsigned char> &bytes, const char *format, const char *lev)
{
    std::vector<std::set<std::string>> keys;
    const auto callback = [&](int depth, json::parse_event_t event, json &value) {
        if (depth > 8) throw std::runtime_error("Placement file too deeply nested");
        if (event == json::parse_event_t::object_start) keys.emplace_back();
        if (event == json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("Duplicate placement key");
        if (event == json::parse_event_t::object_end) keys.pop_back();
        return true;
    };
    const auto doc = json::parse(bytes.begin(), bytes.end(), callback);
    if (doc.at("format") != format || package_integer(doc.at("version"), 1) != 1 ||
        doc.at("track").at("kind") != "custom" || doc.at("track").at("track_key") != "wumpa-world" ||
        doc.at("track").at("lev_sha256") != json::array({lev}))
        throw std::runtime_error("Placement identity does not match");
    return doc;
}

static int16_t placement_number(const json &value, int low, int high)
{
    if (!value.is_number_integer() || value < low || value > high)
        throw std::runtime_error("Placement coordinate or angle out of range");
    return value.get<int16_t>();
}

static void placement_vector(const json &value, int16_t out[3], int low, int high)
{
    if (!value.is_array() || value.size() != 3) throw std::runtime_error("Expected a 3-vector");
    for (int i = 0; i < 3; i++) out[i] = placement_number(value[i], low, high);
}

static void recorded_grid(const json &doc, CustomRecordedRace &race)
{
    package_fields(doc, {"format", "version", "track", "spawns"});
    const auto &spawns = doc.at("spawns");
    if (!spawns.is_array() || spawns.size() != CTR_RECORDED_GRID_COUNT)
        throw std::runtime_error("Expected eight spawns");
    for (unsigned int i = 0; i < spawns.size(); i++)
    {
        const auto &spot = spawns[i];
        package_fields(spot, {"pos", "rot"});
        placement_vector(spot.at("pos"), race.grid[i].pos, INT16_MIN, INT16_MAX);
        placement_vector(spot.at("rot"), race.grid[i].rot, -2048, 4095);
    }
}

static void recorded_crates(const json &doc, CustomRecordedRace &race)
{
    package_fields(doc, {"format", "version", "track", "rows"});
    const auto &rows = doc.at("rows");
    if (!rows.is_array() || rows.size() < 4 || rows.size() > CTR_RECORDED_CRATE_MAX / 4)
        throw std::runtime_error("Expected four to eight rows");
    unsigned int rowNumber = 0;
    for (const auto &row : rows)
    {
        package_fields(row, {"row", "crates", "source", "credit", "review"});
        const auto &crates = row.at("crates");
        if (package_integer(row.at("row"), CTR_RECORDED_CRATE_MAX / 4) != ++rowNumber ||
            !crates.is_array() || crates.size() != 4 ||
            !row.at("source").is_string() || !row.at("credit").is_string() || !row.at("review").is_string())
            throw std::runtime_error("Invalid crate row");
        for (const auto &crate : crates)
        {
            package_fields(crate, {"pos", "rot_y"});
            auto &spot = race.crates[race.crateCount++];
            placement_vector(crate.at("pos"), spot.pos, INT16_MIN, INT16_MAX);
            spot.rot[1] = placement_number(crate.at("rot_y"), -2048, 4095);
        }
    }
}

extern "C" int CustomPackage_GetRecordedRace(const CustomPackageOwned *package,
    CustomRecordedRace **out, char *reason, size_t reasonSize)
{
    // Slice 1 approves an exact pair. A user-created manifest may pin its
    // files, but cannot designate unrelated geometry as a recorded race.
#ifdef CTR_RECORDED_RACE_TEST_PINS
    static const char wumpaLev[] = CTR_RECORDED_RACE_TEST_LEV;
    static const char wumpaVrm[] = CTR_RECORDED_RACE_TEST_VRM;
#else
    static const char wumpaLev[] = "bbab52f2c65c9c1ad7b697e168013fd849d7fe6ae8f0911ae361999f7c37a132";
    static const char wumpaVrm[] = "27fe7959a7798c42ead15015a78c9a38f3212507a154f66bec9bc79b79c11ca9";
#endif
    if (out && *out) return -1;
    CustomContentVerification report;
    if (!CustomPackage_GetPairReport(package, &report)) return 0;
    const bool levMatches = NativeSha256_HexEquals(report.levSha256, wumpaLev);
    const bool vrmMatches = NativeSha256_HexEquals(report.vrmSha256, wumpaVrm);
    if (!levMatches && (!vrmMatches || report.measured.navPaths || report.measured.spawns)) return 0;
    if (reason && reasonSize) reason[0] = 0;
    const auto refuse = [&](const char *text) {
        if (reason && reasonSize) std::snprintf(reason, reasonSize, "%s", text);
        return -1;
    };
    if (!levMatches)
    {
        // A VRM can be shared. Refuse a mismatched recorded-race request
        // without changing the regular mode verdict for another track.
        CustomPackageManifest manifest;
        if (!CustomPackage_GetManifest(package, &manifest)) return 0;
        for (unsigned int i = 0; i < manifest.count; i++)
            if (!std::strcmp(manifest.files[i].role, "recorded_ai") ||
                !std::strcmp(manifest.files[i].role, "race_grid") ||
                !std::strcmp(manifest.files[i].role, "weapon_crate_spots"))
                return refuse("Wrong LEV hash");
        return 0;
    }
    if (!vrmMatches) return refuse("Wrong VRM hash");
    if (!CustomContentVerify_TimeTrialReason(&report, nullptr, 0) ||
        report.measured.navPaths || report.measured.spawns || report.measured.weaponCrates)
        return refuse("Invalid race files");

    const char *invalid = "Invalid recordings";
    try
    {
        CustomPackageManifest manifest;
        if (!CustomPackage_GetManifest(package, &manifest)) return refuse("Invalid package");
        auto bytes = recorded_file(package, manifest, "recorded_ai");
        if (bytes.empty()) return refuse("Missing recordings");
        std::unique_ptr<CustomRecordedRace> race(new CustomRecordedRace{});
        if (AP_NavSet_Read(bytes.data(), bytes.size(), &race->set, race->nodes, race->stamps) != AP_NAVREC_OK)
            return refuse(invalid);
        unsigned char uuid[CTR_CT_NAV_UUID_BYTES];
        const auto &header = race->set.header;
        if (!CustomTrackPolicy_ParseNavUuid(manifest.uuid, uuid) ||
            std::memcmp(uuid, header.trackUuid, sizeof uuid) ||
            header.navRevision != CustomTrackPolicy_NavRevision(wumpaLev))
            return refuse("Wrong recording ID");
        if (header.lapCount != AP_NAVSET_MAX_LAPS) return refuse("Need seven laps");
        for (unsigned int i = 0; i < header.lapCount; i++)
        {
            const auto &lap = race->set.laps[i];
            if (lap.clean != 1 || !AP_NavRecFormat_LapClosed(race->nodes[i], lap.nodeCount) ||
                AP_NavRecSeam_Process(race->nodes[i], lap.nodeCount, 1, nullptr) != AP_NAVREC_SEAM_SMOOTH)
                return refuse("Unusable lap");
            for (unsigned int j = 0; j < i; j++)
                if (AP_NavRecPlayback_SameLine(race->nodes[i], lap.nodeCount,
                        race->nodes[j], race->set.laps[j].nodeCount))
                    return refuse("Duplicate laps");
        }
        invalid = "Invalid grid file";
        bytes = recorded_file(package, manifest, "race_grid");
        if (bytes.empty()) return refuse("Missing grid");
        recorded_grid(recorded_json(bytes, "ctr-ap-grid-spots", wumpaLev), *race);
        invalid = "Invalid crate file";
        bytes = recorded_file(package, manifest, "weapon_crate_spots");
        if (bytes.empty()) return refuse("Missing crates");
        recorded_crates(recorded_json(bytes, "ctr-ap-weapon-crate-spots", wumpaLev), *race);
        invalid = "Invalid race files";
        bytes = recorded_file(package, manifest, "lev");
        if (const char *failure = CustomRecordedRace_CheckGeometry(
                bytes.data(), bytes.size(), race.get()))
            return refuse(failure);
        if (out) *out = race.release();
        return 1;
    }
    catch (const std::exception &) { return refuse(invalid); }
}

extern "C" void CustomRecordedRace_Free(CustomRecordedRace *race)
{
    delete race;
}
#endif

extern "C" int CustomPackage_GetRaceLaps(const CustomPackageOwned *package, unsigned int *laps,
    char *error, size_t errorSize)
{
    if (laps) *laps = 0;
    if (error && errorSize) error[0] = 0;
    try
    {
        CustomPackageManifest manifest;
        if (!laps || !CustomPackage_GetManifest(package, &manifest))
            throw std::runtime_error("Expected owned package and lap output");
        const CustomPackageFile *sidecar = nullptr;
        std::string lev, vrm;
        for (unsigned int i = 0; i < manifest.count; i++)
        {
            const auto &file = manifest.files[i];
            if (!std::strcmp(file.role, "race_settings")) sidecar = &file;
            if (!std::strcmp(file.role, "lev")) lev = file.sha256;
            if (!std::strcmp(file.role, "vrm")) vrm = file.sha256;
        }
        if (!sidecar || !sidecar->bytes || sidecar->bytes > 4096)
            throw std::runtime_error("Package needs authored race_settings within 4096 bytes");
        std::vector<char> bytes(sidecar->bytes);
        if (!CustomPackage_CopyFile(package, "race_settings", bytes.data(), bytes.size(), nullptr))
            throw std::runtime_error("Cannot read owned race settings");
        std::set<std::string> keys;
        auto callback = [&](int depth, json::parse_event_t event, json &value) {
            if (depth > 1) throw std::runtime_error("Race settings must be a flat object");
            if (event == json::parse_event_t::key && !keys.insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate race settings key");
            return true;
        };
        const auto value = json::parse(bytes.begin(), bytes.end(), callback);
        package_fields(value, {"schema_version", "lev_sha256", "vrm_sha256", "laps"});
        package_integer(value["schema_version"], 1);
        if (package_text(value["lev_sha256"], 64) != lev || package_text(value["vrm_sha256"], 64) != vrm)
            throw std::runtime_error("Race settings geometry does not match package");
        *laps = package_integer(value["laps"], 127);
        return 1;
    }
    catch (const std::exception &exception)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", exception.what());
        return 0;
    }
}

extern "C" int CustomPackage_GetRelicTargets(const CustomPackageOwned *package,
    CustomPackageRelicTargets *out, char *error, size_t errorSize)
{
    if (out) std::memset(out, 0, sizeof *out);
    if (error && errorSize) error[0] = 0;
    try
    {
        CustomPackageManifest manifest;
        if (!out || !CustomPackage_GetManifest(package, &manifest))
            throw std::runtime_error("Expected owned package and relic target output");
        const CustomPackageFile *sidecar = nullptr;
        std::string lev, vrm;
        for (unsigned int i = 0; i < manifest.count; i++)
        {
            const auto &file = manifest.files[i];
            if (!std::strcmp(file.role, "relic_targets")) sidecar = &file;
            if (!std::strcmp(file.role, "lev")) lev = file.sha256;
            if (!std::strcmp(file.role, "vrm")) vrm = file.sha256;
        }
        if (!sidecar) throw std::runtime_error("Package has no authored relic_targets sidecar");
        if (!sidecar->bytes || sidecar->bytes > 4096)
            throw std::runtime_error("Relic target schema 1 exceeds 4096-byte limit");
        std::vector<char> bytes(sidecar->bytes);
        if (!CustomPackage_CopyFile(package, "relic_targets", bytes.data(), bytes.size(), nullptr))
            throw std::runtime_error("Cannot read owned relic targets");
        std::set<std::string> keys;
        auto callback = [&](int depth, json::parse_event_t event, json &value) {
            if (depth > 1) throw std::runtime_error("Relic targets must be a flat object");
            if (event == json::parse_event_t::key && !keys.insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate relic target key");
            return true;
        };
        const auto value = json::parse(bytes.begin(), bytes.end(), callback);
        package_fields(value, {"schema_version", "lev_sha256", "vrm_sha256", "laps",
                               "ticks_per_second", "sapphire", "gold", "platinum"});
        if (package_integer(value["schema_version"], 1) != 1 ||
            package_integer(value["ticks_per_second"], 960) != 960)
            throw std::runtime_error("Unsupported relic target schema or timer unit");
        if (package_text(value["lev_sha256"], 64) != lev || package_text(value["vrm_sha256"], 64) != vrm)
            throw std::runtime_error("Relic target geometry hashes do not match this package");
        CustomPackageRelicTargets candidate = {};
        candidate.laps = package_integer(value["laps"], 127); /* Native numLaps is signed char. */
        const char *tiers[] = {"sapphire", "gold", "platinum"};
        for (int i = 0; i < 3; i++)
            candidate.ticks[i] = static_cast<int>(package_integer(value[tiers[i]], INT_MAX / 100));
        /* The existing HUD multiplies ticks by 100 in signed arithmetic. */
        if (candidate.ticks[0] < candidate.ticks[1] || candidate.ticks[1] < candidate.ticks[2])
            throw std::runtime_error("Relic targets must descend sapphire to platinum");
        *out = candidate;
        return 1;
    }
    catch (const std::exception &exception)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", exception.what());
        return 0;
    }
}

static void package_version(const std::string &version)
{
    static const std::regex syntax("(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?");
    if (!std::regex_match(version, syntax)) throw std::runtime_error("Invalid package version");
    const auto build = version.find('+');
    const auto core = version.substr(0, build);
    const auto prerelease = core.find('-');
    auto identifiers = [](const std::string &text, bool numericLeadingZeros) {
        size_t start = 0;
        for (;;)
        {
            const auto end = text.find('.', start);
            const auto part = text.substr(start, end == std::string::npos ? end : end - start);
            if (part.empty() || (numericLeadingZeros && part.size() > 1 && part[0] == '0' &&
                                part.find_first_not_of("0123456789") == std::string::npos))
                throw std::runtime_error("Invalid package version identifier");
            if (end == std::string::npos) break;
            start = end + 1;
        }
    };
    if (prerelease != std::string::npos) identifiers(core.substr(prerelease + 1), true);
    if (build != std::string::npos) identifiers(version.substr(build + 1), false);
}

static std::string package_path(const json &value)
{
    auto path = package_text(value, 240);
    auto folded = path;
    for (char &c : folded) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (folded == "manifest.json" || folded.rfind("manifest.json/", 0) == 0)
        throw std::runtime_error("Reserved package manifest path");
    static const std::regex component("[A-Za-z0-9_][A-Za-z0-9_.-]{0,95}");
    static const std::set<std::string> reserved = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4",
        "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    size_t start = 0;
    for (;;)
    {
        auto end = path.find('/', start);
        auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (!std::regex_match(part, component) || part.back() == '.') throw std::runtime_error("Unsafe package path");
        auto base = part.substr(0, part.find('.'));
        for (char &c : base) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (reserved.count(base)) throw std::runtime_error("Reserved package path");
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return path;
}

extern "C" int CustomPackage_ParseManifest(const char *input, size_t size, const char *expected,
                                            CustomPackageManifest *out, char *error, size_t errorSize)
{
    if (out) std::memset(out, 0, sizeof *out);
    if (error && errorSize) error[0] = 0;
    try
    {
        if (!input || !out || !expected || size == 0 || size > 65536 || !package_hex(expected))
            throw std::runtime_error("Manifest input or expected pin invalid");
        std::vector<std::set<std::string>> keys;
        bool duplicate = false;
        auto callback = [&](int depth, json::parse_event_t event, json &parsed) {
            if (depth > 16) throw std::runtime_error("Manifest nesting exceeds limit");
            if (event == json::parse_event_t::object_start) keys.emplace_back();
            if (event == json::parse_event_t::key && !keys.back().insert(parsed.get<std::string>()).second) duplicate = true;
            if (event == json::parse_event_t::object_end) keys.pop_back();
            return true;
        };
        auto manifest = json::parse(input, input + size, callback);
        if (duplicate) throw std::runtime_error("Duplicate manifest key");
        package_fields(manifest, {"schema_version", "package_uuid", "version", "title", "files", "compatibility"});
        if (package_integer(manifest["schema_version"], 65535) != 1) throw std::runtime_error("Unsupported manifest schema");
        auto uuid = package_text(manifest["package_uuid"], 36);
        if (!std::regex_match(uuid, std::regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")) ||
            uuid == "00000000-0000-0000-0000-000000000000") throw std::runtime_error("Invalid package UUID");
        auto version = package_text(manifest["version"], 64);
        package_version(version);
        auto title = package_text(manifest["title"], 640);
        if (!CustomPackage_ValidateTitle(title.data(), title.size()))
            throw std::runtime_error("Expected bounded Unicode 15 NFC title without controls or edge whitespace");
        package_fields(manifest["compatibility"], {"native_contract", "apworld_contract"});
        for (const char *field : {"native_contract", "apworld_contract"})
            if (package_integer(manifest["compatibility"][field], 65535) != 1) throw std::runtime_error("Unsupported package compatibility contract");
        auto &files = manifest["files"];
        if (!files.is_array() || files.size() < 2 || files.size() > CTR_PACKAGE_FILE_MAX) throw std::runtime_error("Invalid file count");
        const std::map<std::string, unsigned int> limits = {{"lev", 16*1024*1024}, {"vrm", 4*1024*1024},
            {"navigation", 16*1024*1024}, {"ap_boxes", 4*1024*1024}, {"ctr_letters", 1024*1024},
            {"relic_targets", 1024*1024}, {"presentation", 4*1024*1024}, {"race_settings", 4096},
            {"sca", 1024*1024}, {"recorded_ai", AP_NAVSET_MAX_FILE_BYTES},
            {"race_grid", 16384}, {"weapon_crate_spots", 16384}};
        std::set<std::string> roles, paths;
        for (const auto &file : files)
        {
            package_fields(file, {"role", "path", "sha256", "bytes"});
            auto role = package_text(file["role"], 23);
            if (!limits.count(role) || !roles.insert(role).second) throw std::runtime_error("Unknown or duplicate file role");
            auto path = package_path(file["path"]);
            for (char &c : path) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            for (const auto &previous : paths)
                if (path == previous || path.rfind(previous + "/", 0) == 0 || previous.rfind(path + "/", 0) == 0)
                    throw std::runtime_error("Package path collision");
            paths.insert(path);
            if (!package_hex(package_text(file["sha256"], 64))) throw std::runtime_error("Invalid file SHA-256");
            package_integer(file["bytes"], limits.at(role));
        }
        if (!roles.count("lev") || !roles.count("vrm")) throw std::runtime_error("Package needs LEV and VRM");
        std::sort(files.begin(), files.end(), [](const json &a, const json &b) {
            return a["role"] == b["role"] ? a["path"] < b["path"] : a["role"] < b["role"];
        });
        auto canonical = manifest.dump(-1, ' ', false, json::error_handler_t::strict);
        NativeSha256Ctx context;
        unsigned char digest[32];
        char actual[65];
        NativeSha256_Init(&context);
        NativeSha256_Update(&context, canonical.data(), canonical.size());
        NativeSha256_Final(&context, digest);
        NativeSha256_ToHex(digest, actual);
        if (!NativeSha256_HexEquals(expected, actual)) throw std::runtime_error("Complete package pin mismatch");
        CustomPackageManifest result = {};
        std::strcpy(result.sha256, actual);
        std::strcpy(result.uuid, uuid.c_str());
        std::strcpy(result.version, version.c_str());
        std::strcpy(result.title, title.c_str());
        result.count = files.size();
        for (size_t i = 0; i < files.size(); i++)
        {
            std::strcpy(result.files[i].role, files[i]["role"].get<std::string>().c_str());
            std::strcpy(result.files[i].path, files[i]["path"].get<std::string>().c_str());
            std::strcpy(result.files[i].sha256, files[i]["sha256"].get<std::string>().c_str());
            result.files[i].bytes = files[i]["bytes"].get<unsigned int>();
        }
        *out = result;
        return 1;
    }
    catch (const std::exception &exception)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", exception.what());
        return 0;
    }
}
