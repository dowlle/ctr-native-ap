"""Seed metadata, real package verification and Solo collection regression (A5-19).

Pass --baseline in an unmodified base checkout to reproduce the old failure.
The synthetic LEV/VRM and downloader transport come from the music harness.
"""
from pathlib import Path
import runpy
import sys

root = Path(__file__).resolve().parents[1]
helper = runpy.run_path(str(root / "tools/test-custom-music-routes-production.py"))
solo = (root / "ap/ap_solo.cpp").read_text()


def between(first, last):
    start = solo.index(first)
    return solo[start:solo.index(last, start)]


extra = r'''
#include <atomic>
#include <cassert>
#include <filesystem>
#include <set>
#include <platform/native_custom_track_visibility.h>
#include "ap/ap_track_registry.h"
#include "ap/ap_content_registry.hpp"
#include "ap/ap_solo_logic.hpp"
namespace fs = std::filesystem;
static const size_t kDescriptorMax = 64 * 1024;
static std::string SoloPath(const std::string &) { return "/nonexistent-solo-test"; }
static std::string ReadSmallPlainFile(const fs::path &, size_t) { return {}; }
'''
extra += between("// PinSignature of a verified store package", "// Writes one collected descriptor.")
extra += r'''
extern "C" int ap_track_registry_lookup(const char *, uint32_t, const char *, uint32_t,
                                        ApTrackRegistryMatch *) { return AP_TRACK_REGISTRY_NO_INDEX; }
static std::vector<APSoloModePair> g_pairs; // MR2: the pairs a scan sees
// MR2c: pairs only the registry cache knows; loading the cache adds them.
static std::vector<APSoloModePair> g_cachePairs;
static int g_registryStatus = AP_TRACK_REGISTRY_UNAVAILABLE;
static void load_cache() { g_pairs.insert(g_pairs.end(), g_cachePairs.begin(), g_cachePairs.end()); g_cachePairs.clear(); }
extern "C" void ap_track_registry_prepare_identity(const char *, int (*)(const void *), const void *) { load_cache(); }
extern "C" int ap_track_registry_status(int *, char *, int) { return g_registryStatus; }
extern "C" int ap_track_registry_refresh(const char *) { load_cache(); return 1; }
std::string ap_track_registry_canonical_key(const std::string &key) { return key; }
std::vector<APSoloModePair> AP_SoloKnownModePairs() { return g_pairs; }
'''

body = helper["tests"].split("static int install_seed(")[0]
body += r'''
static std::string install_metadata(const std::string &assets, const char *key, const char *title,
                                    const char *version = "1.0.2")
{
    using J = nlohmann::json;
    const std::string source = J({{"schema_version", 1}, {"provider", "ctr-seed"},
                                  {"track_key", key}, {"author", "Author"}}).dump();
    const auto digest = [](const std::string &s) { return sha(std::vector<unsigned char>(s.begin(), s.end())); };
    const std::string manifest = J({{"schema_version", 1}, {"package_uuid", std::string(title) == "Baby T Park" ? UUID : "22222222-2222-5333-8444-555555555555"},
        {"version", version}, {"title", title},
        {"compatibility", {{"native_contract", 1}, {"apworld_contract", 1}}},
        {"files", J::array({
            {{"role", "lev"}, {"path", "original/track.lev"}, {"sha256", sha(lev)}, {"bytes", lev.size()}},
            {{"role", "presentation"}, {"path", "metadata/seed-source.json"}, {"sha256", digest(source)}, {"bytes", source.size()}},
            {{"role", "vrm"}, {"path", "original/track.vrm"}, {"sha256", sha(vrm)}, {"bytes", vrm.size()}}
        })}}).dump();
    const std::string pin = digest(manifest);
    CustomPackageInput inputs[] = {{"lev", lev.data(), lev.size()}, {"presentation", source.data(), source.size()},
                                    {"vrm", vrm.data(), vrm.size()}};
    CustomPackageOwned *owned = nullptr;
    char error[512] = {};
    const int acquired = CustomPackage_AcquireBuffers(manifest.data(), manifest.size(), pin.c_str(), inputs, 3,
                                                      &owned, error, sizeof error);
    if (!acquired) std::fprintf(stderr, "fixture %s: %s\n", title, error);
    assert(acquired);
    assert(CustomPackage_InstallAssets(owned, assets.c_str(), error, sizeof error));
    CustomPackage_Free(&owned);
    return pin;
}
// MR2: one package of a split track, with authored laps so its descriptor is
// complete (a mode pair needs laps), under its own uuid and version.
static std::string install_split(const std::string &assets, const char *version, const char *uuid,
                                 const std::vector<unsigned char> &l, const std::vector<unsigned char> &v)
{
    using J = nlohmann::json;
    const auto digest = [](const std::string &s) { return sha(std::vector<unsigned char>(s.begin(), s.end())); };
    const std::string source = J({{"schema_version", 1}, {"provider", "ctr-seed"},
                                  {"track_key", "cubic-circuit"}, {"author", "Author"}}).dump();
    const std::string race = J({{"schema_version", 1}, {"lev_sha256", sha(l)}, {"vrm_sha256", sha(v)}, {"laps", 5}}).dump();
    const std::string manifest = J({{"schema_version", 1}, {"package_uuid", uuid}, {"version", version},
        {"title", "Cubic Circuit"}, {"compatibility", {{"native_contract", 1}, {"apworld_contract", 1}}},
        {"files", J::array({
            {{"role", "lev"}, {"path", "original/track.lev"}, {"sha256", sha(l)}, {"bytes", l.size()}},
            {{"role", "presentation"}, {"path", "metadata/seed-source.json"}, {"sha256", digest(source)}, {"bytes", source.size()}},
            {{"role", "race_settings"}, {"path", "metadata/race-settings.json"}, {"sha256", digest(race)}, {"bytes", race.size()}},
            {{"role", "vrm"}, {"path", "original/track.vrm"}, {"sha256", sha(v)}, {"bytes", v.size()}}
        })}}).dump();
    const std::string pin = digest(manifest);
    CustomPackageInput inputs[] = {{"lev", l.data(), l.size()}, {"presentation", source.data(), source.size()},
                                    {"race_settings", race.data(), race.size()}, {"vrm", v.data(), v.size()}};
    CustomPackageOwned *owned = nullptr;
    char error[512] = {};
    const int acquired = CustomPackage_AcquireBuffers(manifest.data(), manifest.size(), pin.c_str(), inputs, 4,
                                                      &owned, error, sizeof error);
    if (!acquired) std::fprintf(stderr, "split fixture %s: %s\n", version, error);
    assert(acquired);
    assert(CustomPackage_InstallAssets(owned, assets.c_str(), error, sizeof error));
    CustomPackage_Free(&owned);
    return pin;
}
static int descriptor(const std::string &assets, const std::string &pin, std::string *out = nullptr)
{
    char key[65], revision[65], json[16384], error[512];
    const int kind = CustomPackage_StoreDescriptor(assets.c_str(), pin.c_str(), nullptr,
        key, sizeof key, revision, sizeof revision, json, sizeof json, error, sizeof error);
    if (kind && out) *out = json;
    return kind;
}
int main()
{
    const std::string assets = fresh("solo-keys");
    const auto dotted = install_metadata(assets, "baby.t.park", "Baby T Park");
    const auto suffix = install_metadata(assets, "baby-t-park-1.0.2", "Baby T Park");
    const auto normal = install_metadata(assets, "normal-track", "Normal Track");
#ifdef A5_BASELINE
    assert(!descriptor(assets, dotted) && !descriptor(assets, suffix));
    assert(descriptor(assets, normal));
    const auto scan = CollectInstalledStore(assets, "");
    assert(scan.failed == 2 && !scan.error.empty() && scan.descriptors.size() == 1);
    for (const auto &note : scan.notes) puts(note.c_str());
    puts("BASELINE: both legacy keys fail, the normal descriptor survives, the overall job reports failure and blocks the Solo list");
#else
    for (const auto &pin : {dotted, suffix})
    {
        std::string out;
        assert(descriptor(assets, pin, &out));
        const auto d = nlohmann::json::parse(out);
        assert(d["content_id"] == "baby-t-park" && d["id"] == "package/baby-t-park/1.0.2");
        assert("custom/" + d["content_id"].get<std::string>() + "/" + d["revision"].get<std::string>() == "custom/baby-t-park/1.0.2");
        char lp[512], vp[512];
        assert(CustomPackage_FindPinnedFiles(assets.c_str(), "1.0.2", UUID.c_str(), sha(lev).c_str(), lev.size(),
                                             sha(vrm).c_str(), vrm.size(), lp, sizeof lp, vp, sizeof vp));
    }
    // A version that cannot fit in either id remains a per-package refusal.
    const auto bad = install_metadata(assets, "bad-track", "Too Long Revision", ("1.0.0-" + std::string(54, 'a')).c_str());
    assert(!descriptor(assets, bad));
    // Track rows only: a full scan also carries the solo runtime's mode pairs file (kind 4, MR2).
    const auto rows = [](const StorePublication &s) {
        size_t n = 0;
        for (const auto &d : s.descriptors) n += d.kind == 1 || d.kind == 2;
        return n;
    };
    auto scan = CollectInstalledStore(assets, "");
    assert(!scan.failed && scan.error.empty() && rows(scan) == 2 && scan.descriptors.back().kind == 4);
    bool named = false;
    for (const auto &note : scan.notes) {
        puts(note.c_str());
        if (note.find("Too Long Revision") != std::string::npos) named = true;
    }
    assert(named);
    auto single = CollectInstalledStore(assets, bad);
    assert(single.failed && single.error.find("Too Long Revision") != std::string::npos);
    // New shared downloads must use the seed key even when the folder has a version.
    serve();
    const std::string downloaded = fresh("solo-download"), tracks = downloaded + "/tracks";
    mkdir(tracks.c_str(), 0755);
    const auto start = [&](const char *key) {
        return ap_custom_track_download_start_shared(tracks.c_str(), (tracks + "/baby-t-park-1.0.0").c_str(),
            "https://www.projectsaphi.com/api/v2/tracks/121/downloads", "1.0.0",
            std::string(key) == "baby-t-park" ? UUID.c_str() : "22222222-2222-5333-8444-555555555555", "Baby T Park", "Author", 3,
            0, 0, 0, sha(lev).c_str(), lev.size(), sha(vrm).c_str(), vrm.size(), 0, 0, 0, key);
    };
    assert(!start("baby.t.park") && !start("baby--t-park") && !start(std::string(51, 'a').c_str()));
    assert(start(std::string(50, 'a').c_str())); // package id is exactly 64 bytes
    std::string detail;
    assert(finish(detail) == AP_CT_DOWNLOAD_SUCCEEDED);
    assert(start("baby-t-park"));
    assert(finish(detail) == AP_CT_DOWNLOAD_SUCCEEDED);
    scan = CollectInstalledStore(downloaded, "");
    assert(!scan.failed && rows(scan) == 2);
    bool baby = false;
    for (const auto &d : scan.descriptors) {
        if (d.kind == 4) continue;
        const auto j = nlohmann::json::parse(d.json);
        assert(j["id"].get<std::string>().size() <= 64);
        if (d.key == "baby-t-park") baby = j["id"] == "package/baby-t-park/1.0.0";
    }
    assert(baby);
    // MR2 review 2, finding 1: a Saphi Library install keeps Saphi's version.
    // Cubic Circuit's race and Relic builds are both 1.1.2 on Saphi; the
    // registry labels the Relic build 1.1.2-relic. A room finds the Relic
    // files by revision and pins, so every writer must name the installed
    // version: a full scan, one install's publication and the Library export.
    {
        const std::string split = fresh("solo-split");
        auto relicVrm = vrm;
        relicVrm[20] ^= 1; // another pixel: the Relic build's own VRM, same size
        APSoloModePair p;
        p.trackKey = "cubic-circuit"; p.raceRevision = "1.1.2"; p.relicRevision = "1.1.2-relic";
        p.raceLev = sha(lev); p.raceVrm = sha(vrm); p.relicLev = sha(lev); p.relicVrm = sha(relicVrm);
        p.raceLevBytes = lev.size(); p.raceVrmBytes = vrm.size(); p.relicLevBytes = lev.size(); p.relicVrmBytes = relicVrm.size();
        p.targets[0] = 38400; p.targets[1] = 28800; p.targets[2] = 19200; p.source = "test";
        g_pairs = {p};
        const auto racePin = install_split(split, "1.1.2", "33333333-2222-5333-8444-555555555555", lev, vrm);
        const auto relicPin = install_split(split, "1.1.2", "44444444-2222-5333-8444-555555555555", lev, relicVrm);
        const auto relicFound = [&](const nlohmann::json &d) {
            char lp[512], vp[512];
            return d.contains("relic_revision") &&
                   CustomPackage_FindPinnedFiles(split.c_str(), d["relic_revision"].get<std::string>().c_str(), nullptr,
                       sha(lev).c_str(), lev.size(), sha(relicVrm).c_str(), relicVrm.size(), lp, sizeof lp, vp, sizeof vp);
        };
        const auto raceRow = [](const StorePublication &s) {
            nlohmann::json row;
            for (const auto &d : s.descriptors)
                if ((d.kind == 1 || d.kind == 2) && nlohmann::json::parse(d.json).contains("relic_files")) row = nlohmann::json::parse(d.json);
            return row;
        };
        auto full = CollectInstalledStore(split, "");
        for (const auto &note : full.notes) puts(note.c_str());
        auto row = raceRow(full);
        assert(!full.failed && row["relic_revision"] == "1.1.2" && relicFound(row) && full.descriptors.back().kind == 4);
        // One install publishes only its own revision (A3-46) and looks the Relic pins up.
        auto single = CollectInstalledStore(split, racePin);
        for (const auto &note : single.notes) puts(note.c_str());
        row = raceRow(single);
        assert(!single.failed && row["relic_revision"] == "1.1.2" && relicFound(row));
        // The Library export takes the same path.
        std::string out, note;
        assert(descriptor(split, racePin, &out));
        auto exported = nlohmann::json::parse(out);
        assert(PairExportDescriptor(exported, g_pairs, split, note) == AP_SOLO_PAIR_MERGED);
        assert(exported["relic_revision"] == "1.1.2" && relicFound(exported) && note.find("installed as 1.1.2") != std::string::npos);
        assert(descriptor(split, relicPin, &out));
        exported = nlohmann::json::parse(out);
        assert(PairExportDescriptor(exported, g_pairs, split, note) == AP_SOLO_PAIR_RELIC_ONLY);
        // Not installed: the label stays, a room installs the pair under it.
        const std::string bare = fresh("solo-split-bare");
        install_split(bare, "1.1.2", "33333333-2222-5333-8444-555555555555", lev, vrm);
        row = raceRow(CollectInstalledStore(bare, ""));
        assert(row["relic_revision"] == "1.1.2-relic");
        g_pairs.clear();
    }
    // MR2 review 2, finding 2: a client without a package store (legacy
    // loose-file installs) still writes the solo runtime's mode pairs file.
    {
        const std::string none = fresh("solo-no-store");
        const auto scan = CollectInstalledStore(none, "");
        assert(!scan.failed && scan.descriptors.size() == 1 && scan.descriptors[0].kind == 4);
        assert(nlohmann::json::parse(scan.descriptors[0].json)["schema_version"] == 1);
    }
    // MR2 review 3, finding 3: on a cold start without a store the registry
    // cache is loaded before the mode pairs file is built, so a pair only the
    // registry knows is in it. Play Solo's worker (with a cancel flag) and a
    // call without one both load it first.
    {
        APSoloModePair p;
        p.trackKey = "registry-only"; p.raceRevision = "2.0.0"; p.relicRevision = "2.0.0-relic";
        p.raceLev = std::string(64, 'a'); p.raceVrm = std::string(64, 'b');
        p.relicLev = std::string(64, 'c'); p.relicVrm = std::string(64, 'd');
        p.raceLevBytes = 100; p.raceVrmBytes = 200; p.relicLevBytes = 300; p.relicVrmBytes = 400;
        p.targets[0] = 3; p.targets[1] = 2; p.targets[2] = 1; p.source = "registry";
        const auto listed = [](const StorePublication &s) {
            const auto file = nlohmann::json::parse(s.descriptors.back().json);
            for (const auto &row : file["relic_only"])
                if (row["track_key"] == "registry-only" && row["files"][0]["sha256"] == std::string(64, 'c') &&
                    row["files"][1]["bytes"] == 400)
                    return true;
            return false;
        };
        const std::string none = fresh("solo-no-store-cold");
        std::atomic<bool> cancel{false};
        g_pairs.clear();
        g_cachePairs = {p};
        auto scan = CollectInstalledStore(none, "", &cancel);
        assert(!scan.failed && scan.descriptors.size() == 1 && scan.descriptors[0].kind == 4 && listed(scan));
        g_pairs.clear();
        g_cachePairs = {p};
        g_registryStatus = AP_TRACK_REGISTRY_IDLE;
        scan = CollectInstalledStore(none, "");
        assert(!scan.failed && scan.descriptors.size() == 1 && listed(scan));
        g_registryStatus = AP_TRACK_REGISTRY_UNAVAILABLE;
        g_pairs.clear();
        g_cachePairs.clear();
    }
    puts("PASS: legacy dotted/versioned keys recover seed identity; new downloads preserve content_id; 64-byte boundary; bad package logged and skipped; pinned lookup still resolves");
#endif
}
'''
if "--baseline" in sys.argv:
    extra = "\n#define A5_BASELINE\n" + extra
helper["run_harness"](body, extra)
