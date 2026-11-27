"""PS2: two installs of one track key and version in Play Solo's track list.

A Saphi download of Big Forest 1.0.0 beside the Big Forest 1.0.0 that
LRG2Pancho.xdelta's importer cuts: same key and version, other files. This
drives the production row code from ap/ap_solo.cpp (AddNeedsLapsRows,
LabelBuilds, ParseTracks, AP_Solo_TrackKeepLaps): a build waiting for its laps
is listed beside the other build with its own row id and file, every title
names its source so the player can tell the builds apart, and picking laps
writes the waiting build's own file without touching the other build's entry.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_solo.cpp").read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


def between(first, last):
    start = source.index(first)
    return source[start:source.index(last, start)]


PATCH = "52a420e582fc44786210ae65817d7105c7d881c2f0b4387cebde2dce86064526"

fixture = r'''
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "ap/ap_solo.h"
#include "ap/ap_solo_logic.hpp"
#include "include/platform/native_custom_track_visibility.h"
namespace fs = std::filesystem;
#define AP_SOLO_GETPID() getpid()
#define CTR_CUSTOM_PACKAGES 1
static const size_t kDescriptorMax = 64 * 1024;
static std::string base;
static std::vector<std::string> logged;
static void Log(const std::string &line) { logged.push_back(line); }
static std::string BaseDir() { return base + "/"; }
static std::string SoloPath(const std::string &rel) { return BaseDir() + "solo/" + rel; }
static std::string AssetsDir() { return base + "/assets"; }
// The track source table's answer for the LRG2Pancho patch (ap_track_registry.cpp).
extern "C" const char *CustomTrackCredit_PatchSource(const char *name, const char *sha256) {
    if (name && *name) return name;
    return sha256 && std::string(sha256) == "@PATCH@" ? "LRG2Pancho.xdelta" : "an imported xdelta pack";
}
// No store in this fixture: a waiting build counts as installed by its loose files.
extern "C" int CustomPackage_FindPinnedFiles(const char *, const char *, const char *, const char *, unsigned long,
                                             const char *, unsigned long, char *, size_t, char *, size_t) { return 0; }
static std::vector<APSoloModePair> g_pairs; // the client's known mode pairs
std::vector<APSoloModePair> AP_SoloKnownModePairs() { return g_pairs; }
std::set<std::string> AP_SoloInstalledVersionsByPins(const nlohmann::json &) { return {}; }
namespace {
std::vector<APSoloTrackInfo> g_tracks;
std::map<std::string, int> g_trackStarts;
void AddNeedsLapsRows(std::vector<APSoloTrackInfo> &rows);
std::string NeedsLapsFileName(const APSoloTrackInfo &t);
'''.replace("@PATCH@", PATCH)
for signature in ("bool SoloRootIsLink()", "std::string ReadSmallPlainFile(const fs::path &path, size_t limit)",
                  "int WriteNewFile(const fs::path &path, const std::string &text)", "bool CopyText(",
                  "void ReadTrackCreditPins(", "bool HiddenTrackDescriptor("):
    fixture += function(signature)
fixture += between("// PS2: the source a Play Solo title names when two builds share a track key",
                   "void ParseList(const nlohmann::json &r)")
fixture += between("// Keeps `json` as solo/<folder>/<name>; true once the file holds it.",
                   "} // namespace\n\nnamespace\n{\n// \"<revision>|lev:<sha256>:<bytes>|vrm:<sha256>:<bytes>\"")
fixture += function("nlohmann::json WithModePair(nlohmann::json d, const nlohmann::json &kept)\n{")
fixture += "}\n"
fixture += function('extern "C" int AP_Solo_TrackKeepLaps(int index)')
fixture += r'''
static std::string hex(char c) { return std::string(64, c); }
static nlohmann::json descriptor(const std::string &lev, const std::string &vrm, bool laps, bool patch) {
    nlohmann::json d = {{"id", "package/big-forest/1.0.0"}, {"content_id", "big-forest"}, {"revision", "1.0.0"},
                        {"display_name", "Big Forest"}, {"author", "Lolothesamurai"},
                        {"files", {{{"role", "lev"}, {"sha256", lev}, {"bytes", 100}},
                                   {{"role", "vrm"}, {"sha256", vrm}, {"bytes", 200}}}}};
    if (laps) d["laps"] = 3;
    if (patch) d["locator"] = {{"kind", "xdelta"}, {"patch_sha256", "@PATCH@"}, {"bigfile_index", 73}};
    return d;
}
static void write(const std::string &rel, const std::string &text) {
    fs::create_directories(fs::path(SoloPath(rel)).parent_path());
    std::ofstream(SoloPath(rel), std::ios::binary) << text;
}
static std::string read(const std::string &rel) {
    std::ifstream in(SoloPath(rel), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
static void clean() {
    std::error_code ec;
    fs::remove_all(base + "/solo", ec); fs::remove_all(base + "/assets", ec);
    fs::create_directories(base + "/assets/tracks/big-forest/original");
    std::ofstream(base + "/assets/tracks/big-forest/original/a.lev") << std::string(100, 'x');
    std::ofstream(base + "/assets/tracks/big-forest/original/a.vrm") << std::string(200, 'x');
    g_tracks.clear(); logged.clear();
}
static nlohmann::json runtimeRow(const std::string &id, const std::string &build, const nlohmann::json &origin,
                                 const char *modes) {
    return {{"id", id}, {"content_id", "big-forest"}, {"revision", "1.0.0"}, {"display_name", "Big Forest"},
            {"author", "Lolothesamurai"}, {"modes", nlohmann::json::parse(modes)}, {"time_trial", true},
            {"source", "file"}, {"build", build}, {"origin", origin}, {"start", 0}};
}
int main(int, char **argv) {
    base = argv[1];
    const std::string saphiTag = "aaaaaaaabbbbbbbb", patchTag = "ccccccccdddddddd";
    const auto saphi = descriptor(hex('a'), hex('b'), true, false);
    const auto patchWait = descriptor(hex('c'), hex('d'), false, true);
    const std::string patchFile = "big-forest__1.0.0@" + patchTag + ".json";

    // 1. Install order Saphi then import (the importer's waiting build under
    // its own name): the runtime lists the Saphi build, the waiting LRG2
    // build is listed beside it with its own row id and file, and each title
    // names its source.
    clean();
    write("tracks/big-forest__1.0.0.json", saphi.dump());
    write("needs-laps/" + patchFile, patchWait.dump());
    ParseTracks({{"tracks", {runtimeRow("package/big-forest/1.0.0", saphiTag, {{"kind", "saphi"}}, "[]")}}});
    assert(g_tracks.size() == 2);
    assert(std::string(g_tracks[0].id) == "package/big-forest/1.0.0" && std::string(g_tracks[0].title) == "Big Forest - Saphi");
    assert(std::string(g_tracks[0].file) == "big-forest__1.0.0.json" && std::string(g_tracks[0].creditLev) == hex('a'));
    assert(std::string(g_tracks[1].id) == "package/big-forest/1.0.0@" + patchTag && g_tracks[1].needsLaps);
    assert(std::string(g_tracks[1].title) == "Big Forest - LRG2Pancho patch" && std::string(g_tracks[1].file) == patchFile);

    // Picking its laps writes the LRG2 build's own complete file; the Saphi
    // build's entry is untouched.
    g_tracks[1].laps = 5;
    assert(AP_Solo_TrackKeepLaps(1));
    const auto kept = nlohmann::json::parse(read("tracks/" + patchFile));
    assert(kept["laps"] == 5 && kept["files"] == patchWait["files"]);
    assert(!fs::exists(SoloPath("needs-laps/" + patchFile)) && nlohmann::json::parse(read("tracks/big-forest__1.0.0.json")) == saphi);

    // 2. The runtime lists both builds (row ids with the build tag): the
    // client reads each build's own file and labels both.
    ParseTracks({{"tracks", {runtimeRow("package/big-forest/1.0.0@" + saphiTag, saphiTag, {{"kind", "saphi"}}, "[]"),
                             runtimeRow("package/big-forest/1.0.0@" + patchTag, patchTag,
                                        {{"kind", "patch"}, {"patch_sha256", "@PATCH@"}}, "[\"trophy\",\"ctr\"]")}}});
    assert(g_tracks.size() == 2 && !g_tracks[0].needsLaps && !g_tracks[1].needsLaps);
    assert(std::string(g_tracks[0].title) == "Big Forest - Saphi" && std::string(g_tracks[1].title) == "Big Forest - LRG2Pancho patch");
    assert(std::string(g_tracks[1].file) == patchFile && std::string(g_tracks[1].creditLev) == hex('c'));
    assert(std::string(g_tracks[1].modes) == "Trophy, CTR" && g_tracks[0].modes[0] == '\0');

    // 3. Install order import then Saphi: the waiting LRG2 build holds the
    // plain name, the runtime lists the Saphi build from its build name.
    clean();
    write("tracks/big-forest__1.0.0@" + saphiTag + ".json", saphi.dump());
    write("needs-laps/big-forest__1.0.0.json", patchWait.dump());
    ParseTracks({{"tracks", {runtimeRow("package/big-forest/1.0.0", saphiTag, {{"kind", "saphi"}}, "[]")}}});
    assert(g_tracks.size() == 2 && std::string(g_tracks[0].file) == "big-forest__1.0.0@" + saphiTag + ".json");
    assert(std::string(g_tracks[1].id) == "package/big-forest/1.0.0@" + patchTag && std::string(g_tracks[1].file) == "big-forest__1.0.0.json");
    assert(std::string(g_tracks[0].title) == "Big Forest - Saphi" && std::string(g_tracks[1].title) == "Big Forest - LRG2Pancho patch");
    // Laps for a waiting build under the plain name: written there, as before.
    assert(AP_Solo_TrackKeepLaps(1) && nlohmann::json::parse(read("tracks/big-forest__1.0.0.json"))["laps"] == 3);

    // 4. One build only: no label, plain row id, as before PS2.
    clean();
    write("needs-laps/big-forest__1.0.0.json", patchWait.dump());
    ParseTracks({{"tracks", nlohmann::json::array()}});
    assert(g_tracks.size() == 1 && std::string(g_tracks[0].id) == "package/big-forest/1.0.0");
    assert(std::string(g_tracks[0].title) == "Big Forest");
    // The same build listed by the runtime and waiting: one row.
    ParseTracks({{"tracks", {runtimeRow("package/big-forest/1.0.0", patchTag, {{"kind", "patch"}}, "[\"trophy\"]")}}});
    assert(g_tracks.size() == 1 && !g_tracks[0].needsLaps);

    // 5. Two builds from one source (two cuts nobody folded, or two local
    // copies): the label adds the build tag so they still differ.
    std::vector<APSoloTrackInfo> rows(2);
    std::memset(rows.data(), 0, sizeof(APSoloTrackInfo) * 2);
    for (int i = 0; i < 2; i++) {
        CopyText(rows[i].title, sizeof rows[i].title, "Fixture");
        CopyText(rows[i].build, sizeof rows[i].build, i ? patchTag : saphiTag);
        CopyText(rows[i].id, sizeof rows[i].id, std::string("package/fixture/1@") + rows[i].build);
    }
    LabelBuilds(rows, {{rows[0].id, {{"kind", "local"}}}, {rows[1].id, {{"kind", "local"}}}});
    assert(std::string(rows[0].title) == "Fixture - local files aaaaaaaa" && std::string(rows[1].title) == "Fixture - local files cccccccc");
    assert(BuildSourceLabel({{"kind", "patch"}, {"patch_sha256", hex('e')}}) == "patch");
    assert(BuildSourceLabel(nlohmann::json()).empty());
    // A long title is cut so the source still fits, never inside UTF-8.
    std::string longTitle;
    for (int i = 0; i < 70; i++) longTitle += "\xc3\xa9";
    CopyText(rows[0].title, sizeof rows[0].title, longTitle);
    CopyText(rows[1].title, sizeof rows[1].title, longTitle);
    LabelBuilds(rows, {{rows[0].id, {{"kind", "saphi"}}}, {rows[1].id, {{"kind", "patch"}, {"patch_sha256", "@PATCH@"}}}});
    const std::string cut = rows[1].title;
    assert(cut.size() < sizeof rows[1].title && cut.size() > 30 && cut.compare(cut.size() - 19, 19, " - LRG2Pancho patch") == 0);
    assert((((unsigned char)cut[cut.size() - 20]) & 0xc0) != 0xc0);
    // 6. MR2 round 4, finding 3: an old Relic-only file left waiting for laps
    // (legacy loose files, no store) is not offered as a track to pick laps for.
    clean();
    APSoloModePair pair;
    pair.trackKey = "big-forest"; pair.raceRevision = "1.0.0"; pair.relicRevision = "1.0.0-relic";
    pair.raceLev = hex('e'); pair.raceVrm = hex('f'); pair.relicLev = hex('c'); pair.relicVrm = hex('d');
    pair.raceLevBytes = 100; pair.raceVrmBytes = 200; pair.relicLevBytes = 100; pair.relicVrmBytes = 200;
    pair.targets[0] = 3; pair.targets[1] = 2; pair.targets[2] = 1; pair.source = "test";
    g_pairs = {pair};
    write("needs-laps/big-forest__1.0.0.json", patchWait.dump());
    ParseTracks({{"tracks", nlohmann::json::array()}});
    assert(g_tracks.empty());
    g_pairs.clear();
    ParseTracks({{"tracks", nlohmann::json::array()}});
    assert(g_tracks.size() == 1);
    puts("PASS: two builds of one track version are two Play Solo rows, each named by its source; laps go to the right file");
}
'''.replace("@PATCH@", PATCH)
with tempfile.TemporaryDirectory(prefix="ctr-ps2-rows-") as directory:
    path = Path(directory)
    (path / "rows.cpp").write_text(fixture)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-I", str(root),
                    "-I", str(root / "ap/vendor/json/include"), str(path / "rows.cpp"),
                    "-o", str(path / "rows")], check=True)
    (path / "client").mkdir()
    subprocess.run([str(path / "rows"), str(path / "client")], check=True, timeout=30)
