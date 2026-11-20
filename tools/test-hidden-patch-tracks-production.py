"""Exercise Play Solo discovery against saved descriptors and room metadata."""

from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_solo.cpp").read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


fixture = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ap/ap_solo.h"
#include "ap/ap_solo_logic.hpp"
#include <platform/native_custom_track_visibility.h>
namespace fs = std::filesystem;
using nlohmann::json;
static const size_t kDescriptorMax = 64 * 1024;
static std::string base;
static std::string BaseDir() { return base + "/"; }
static std::string SoloPath(const std::string &rel) { return BaseDir() + "solo/" + rel; }
static const std::string &AssetsDir() { return base; }
static std::vector<APSoloTrackInfo> g_tracks;
static std::map<std::string, int> g_trackStarts;
'''
for signature in (
    "bool SoloRootIsLink()", "std::string ReadSmallPlainFile(",
    "bool CopyText(", "bool PinnedFilePresent(",
):
    fixture += function(signature)
start = source.index("void AddNeedsLapsRows(std::vector<APSoloTrackInfo> &rows);")
fixture += source[start:source.index("void ParseList(", start)]
fixture += function("void AddNeedsLapsRows(std::vector<APSoloTrackInfo> &rows)\n")
fixture += r'''
static void put(const fs::path &path, const std::string &bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    base = argv[1];
    const char *hidden[] = {
        "268064f31236405f384778077e544c69079fb39587ebea60368102fa99ce945a",
        "e9c3b0bb1793809a0bf7e396897ddb54058c94e8dc796b3f587d0921d543857c",
        "b1754b2e860f94938de583be6be6e0b66976f843f1aa8805d8dc6c67fcad270d",
        "20576dcd3f777402c3c39e7532ccbd71b2a2f3368d813bfcbb479cd1300810f3",
        "87f416ec29a93f16013f7497de35c0832c13712aaf94695b6597d309e37edf80",
    };
    for (const char *hash : hidden) assert(CustomTrack_IsHiddenLev(hash));
    const std::string visibleHash = "60982d262c4a150f0b625495b353795386609217b40837b47bcc395507e66cd1";
    assert(!CustomTrack_IsHiddenLev(visibleHash.c_str()));
    auto descriptor = [](const std::string &key, const std::string &hash) {
        return json{{"id", "package/" + key + "/1.0.0"}, {"content_id", key},
                    {"revision", "1.0.0"}, {"display_name", key}, {"author", "Tester"},
                    {"uuid", "07395277-d2c2-5544-9cde-5ad9976c06fd"},
                    {"files", json::array({{{"role", "lev"}, {"sha256", hash}, {"bytes", 4u}},
                                           {{"role", "vrm"}, {"sha256", std::string(64, 'a')}, {"bytes", 4u}}})}};
    };
    const auto installed = descriptor("lockbox", hidden[4]);
    const auto oldRoom = descriptor("anniversary", hidden[4]);
    const auto visible = descriptor("time-trial", visibleHash);
    const auto waiting = descriptor("waiting", hidden[0]);
    const auto unhiddenWaiting = descriptor("other-waiting", visibleHash);
    const fs::path installedPath = SoloPath("tracks/lockbox__1.0.0.json");
    const fs::path roomPath = SoloPath("rooms/existing/room.json");
    const auto room = json{{"custom_tracks", json::array({oldRoom})}}.dump();
    put(installedPath, installed.dump());
    put(roomPath, room);
    put(SoloPath("tracks/time-trial__1.0.0.json"), visible.dump());
    for (const auto &d : {waiting, unhiddenWaiting})
    {
        const std::string key = d["content_id"];
        put(SoloPath("needs-laps/" + key + "__1.0.0.json"), d.dump());
        put(fs::path(base) / "tracks" / key / "original/level.lev", "lev!");
        put(fs::path(base) / "tracks" / key / "original/level.vrm", "vrm!");
    }
    put(fs::path(base) / "tracks/lockbox/original/level.lev", "lev!");
    json rows = json::array();
    for (const auto &d : {installed, oldRoom, visible})
    {
        auto row = d;
        row.erase("files"); // The bundled runtime omits pins in list output.
        row["source"] = d == oldRoom ? "room" : "file";
        row["time_trial"] = true;
        rows.push_back(row);
    }
    ParseTracks(json{{"tracks", rows}});
    assert(g_tracks.size() == 2 && g_tracks[0].timeTrial && g_tracks[1].needsLaps);
    assert(std::strcmp(g_tracks[0].id, "package/time-trial/1.0.0") == 0);
    assert(std::strcmp(g_tracks[1].id, "package/other-waiting/1.0.0") == 0);
    assert(g_trackStarts.size() == 1);
    assert(ReadSmallPlainFile(installedPath, kDescriptorMax) == installed.dump());
    assert(ReadSmallPlainFile(roomPath, kDescriptorMax) == room);
    assert(ReadSmallPlainFile(fs::path(base) / "tracks/lockbox/original/level.lev", 64) == "lev!");
    assert(fs::is_regular_file(SoloPath("needs-laps/waiting__1.0.0.json")));
}
'''
with tempfile.TemporaryDirectory(prefix="ctr-hidden-patch-tracks-") as temp:
    work = Path(temp)
    src = work / "probe.cpp"
    exe = work / "probe"
    src.write_text(fixture)
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I.", "-Iinclude",
         "-Iap/vendor/json/include", str(src), "-o", str(exe)],
        cwd=root, check=True,
    )
    subprocess.run([str(exe), str(work)], check=True)
