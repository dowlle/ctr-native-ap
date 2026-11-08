"""Exercise production track selection, password setters and child arguments."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_solo.cpp").read_text()

def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]

fixture = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include "ap/ap_solo.h"
#include "ap/ap_solo_logic.hpp"
static std::vector<APSoloTrackInfo> g_tracks;
static std::map<std::string, int> g_trackStarts;
static std::vector<APSoloOption> g_options;
namespace fs = std::filesystem;
static std::string g_optionOverridesFile;
static struct { int state = AP_SOLO_JOB_IDLE; } g_job;
static bool TrackPublicationBusy() { return false; }
static bool StorePublicationBusy() { return false; }
static bool SoloRootIsLink() { return false; }
static void JobFail(const char*, const char*, const char*) { assert(false); }
static const int JOB_GENERATE = 1, kGenerateLimitMs = 120000;
static std::string g_sessionPassword;
static bool active;
extern "C" int AP_Solo_SessionActive(void) { return active; }
static std::vector<std::string> childArgs;
static std::string AssetsDir() { return "assets"; }
static bool JobStart(int, const std::vector<std::string>& args, int) {
    childArgs = args; return true;
}
struct Child {
    void release() {}
    bool start(const std::vector<std::string>& a, const std::string&) {
        childArgs = a; return true;
    }
};
static struct { std::string roomId; Child child; int state, phaseStart; } g_session;
#define AP_SOLO_GETPID() 123
static int SDL_GetTicks() { return 42; }
static std::string SoloPath(const std::string& s) { return s; }
'''
for signature in ('int WriteNewFile(', 'void ClearOptionOverrides()', 'bool AddOptionOverrides(',
                  'extern "C" void AP_Solo_TrackCycle(int index)',
                  'extern "C" int AP_Solo_GenerateStart(',
                  'extern "C" int AP_Solo_SetPassword(const char *password)',
                  'bool SessionSpawn()'):
    fixture += function(signature)
fixture += r'''
static APSoloTrackInfo track(const char *id) {
    APSoloTrackInfo t = {};
    strcpy(t.id, id); strcpy(t.modes, "Trophy"); return t;
}
int main() {
    g_tracks = {track("package/baby-t/1.0"), track("package/baby-t/1.1"),
                track("package/baby-t-remix/1.0"), track("retail/1")};
    AP_Solo_TrackCycle(0);
    AP_Solo_TrackCycle(2);
    AP_Solo_TrackCycle(1);
    assert(g_tracks[0].pick == AP_SOLO_TRACK_OFF);
    assert(g_tracks[1].pick == AP_SOLO_TRACK_POOL);
    assert(g_tracks[2].pick == AP_SOLO_TRACK_POOL);
    AP_Solo_TrackCycle(1);
    assert(g_tracks[1].pick == AP_SOLO_TRACK_GUARANTEED);
    AP_Solo_TrackCycle(0);
    assert(g_tracks[1].pick == AP_SOLO_TRACK_OFF);
    g_tracks[1].needsLaps = 1;
    AP_Solo_TrackCycle(1);
    assert(g_tracks[0].pick == AP_SOLO_TRACK_POOL);
    assert(g_tracks[1].pick == AP_SOLO_TRACK_OFF);
    AP_Solo_TrackCycle(-1); AP_Solo_TrackCycle(4);
    assert(!AP_SoloTrackSameContent("package/a", "package/a"));
    assert(!AP_SoloTrackSameContent("package/a/", "package/a/1"));
    assert(!AP_SoloTrackSameContent("retail/1", "retail/2"));
    assert(SessionSpawn());
    assert(childArgs.size() == 5); // No generated or hidden password.
    assert(AP_Solo_SetPassword("chosen password"));
    assert(SessionSpawn());
    assert(childArgs.size() == 7 && childArgs[5] == "--password");
    assert(childArgs[6] == "chosen password"); // One argument, spaces preserved.
    active = true;
    assert(!AP_Solo_SetPassword("replacement"));
    assert(g_sessionPassword == "chosen password");
    active = false;
    assert(!AP_Solo_SetPassword(nullptr));
    assert(!AP_Solo_SetPassword(std::string(64, 'x').c_str()));
    assert(AP_Solo_SetPassword(""));
    assert(SessionSpawn() && childArgs.size() == 5);
    // An imported YAML's unchanged options and track page send no overrides,
    // including tracks selected by the YAML and unavailable extra fields.
    g_tracks = {track("package/baby-t/1.0"), track("package/other/1.0")};
    g_tracks[0].pick = AP_SOLO_TRACK_POOL;
    g_trackStarts = {{g_tracks[0].id, AP_SOLO_TRACK_POOL}, {g_tracks[1].id, 0}};
    APSoloOption option;
    option.name = "goal_wumpa"; option.kind = "range";
    option.value = option.initial = "150";
    g_options = {option};
    assert(AP_Solo_GenerateStart("imported.yaml", "ImportedPlayer", "", 1));
    assert((childArgs == std::vector<std::string>{"generate", "--yaml", "imported.yaml",
        "--assets", "assets", "--name", "ImportedPlayer"}));
    // A changed row goes alone, leaving weighted and unsupported values alone.
    g_options[0].value = "200";
    assert(AP_Solo_GenerateStart("imported.yaml", "ImportedPlayer", "", 1));
    assert(childArgs.size() == 9 && childArgs[7] == "--set" && childArgs[8] == "goal_wumpa=200");
    // Explicitly turning off a track still sends the track page, even empty.
    g_tracks[0].pick = AP_SOLO_TRACK_OFF;
    assert(AP_Solo_GenerateStart("imported.yaml", "ImportedPlayer", "", 1));
    assert(childArgs.back() == "--tracks-page");
    // Returning every row to its initial pick restores the untouched path.
    g_tracks[0].pick = AP_SOLO_TRACK_POOL;
    g_options[0].value = g_options[0].initial;
    assert(AP_Solo_GenerateStart("imported.yaml", "ImportedPlayer", "", 1));
    assert(childArgs.size() == 7);
    // Unedited imported names, including weighted expressions, are not sent.
    assert(AP_Solo_GenerateStart("imported.yaml", "", "", 1));
    assert(childArgs.size() == 5);
    // A selection larger than OS argument limits travels as a JSON sidecar.
    g_options[0].value = std::string(250000, 'x');
    assert(AP_Solo_GenerateStart("imported.yaml", "", "", 1));
    assert(childArgs.size() == 7 && childArgs[5] == "--set-file");
    const std::string sidecar = childArgs[6];
    std::ifstream input(sidecar);
    nlohmann::json saved; input >> saved;
    assert(saved == nlohmann::json(AP_SoloSetArgs(g_options)));
    ClearOptionOverrides();
    assert(!fs::exists(sidecar));
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "solo.cpp").write_text(fixture)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                    "-I", str(root / "ap/vendor/json/include"), str(path / "solo.cpp"),
                    "-o", str(path / "solo")], check=True)
    subprocess.run([str(path / "solo")], check=True, cwd=directory)
print("PASS: selection replaces revisions; password is optional, editable and passed intact")
