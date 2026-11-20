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
extern "C" int ap_track_registry_status(int *, char *, int) { return AP_TRACK_REGISTRY_UNAVAILABLE; }
extern "C" int ap_track_registry_refresh(const char *) { return 0; }
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
    auto scan = CollectInstalledStore(assets, "");
    assert(!scan.failed && scan.error.empty() && scan.descriptors.size() == 2);
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
    assert(!scan.failed && scan.descriptors.size() == 2);
    bool baby = false;
    for (const auto &d : scan.descriptors) {
        const auto j = nlohmann::json::parse(d.json);
        assert(j["id"].get<std::string>().size() <= 64);
        if (d.key == "baby-t-park") baby = j["id"] == "package/baby-t-park/1.0.0";
    }
    assert(baby);
    puts("PASS: legacy dotted/versioned keys recover seed identity; new downloads preserve content_id; 64-byte boundary; bad package logged and skipped; pinned lookup still resolves");
#endif
}
'''
if "--baseline" in sys.argv:
    extra = "\n#define A5_BASELINE\n" + extra
helper["run_harness"](body, extra)
