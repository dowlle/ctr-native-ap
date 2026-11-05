"""Run the production store-job lifecycle while verification is held open.

The verifier is a controlled dependency: it cannot finish before the main
thread releases it. This detects a blocking start/poll/cancel without timing
assertions. Descriptor publication and child handoff are instrumented.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_solo.cpp").read_text()

def function(signature):
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}\n", start) + 3]

fixture = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "ap/ap_solo.h"
#include "ap/ap_solo_logic.hpp"
#define CTR_CUSTOM_PACKAGES
enum JobKind { JOB_NONE, JOB_TRACKS };
static const uint64_t kQuickLimitMs = 60000;
struct Child { void release() {} void kill() {} };
static struct {
    Child child; JobKind kind = JOB_NONE; int state = AP_SOLO_JOB_IDLE;
    std::string phase, errorText, errorOption, resultRoom;
    nlohmann::json result;
} g_job;
static bool runtime = true, link = false;
extern "C" int AP_Solo_RuntimePresent(void) { return runtime; }
static bool SoloRootIsLink() { return link; }
static std::string AssetsDir() { return "test-assets"; }
static void JobFail(const std::string&, const std::string& msg, const std::string&) {
    g_job.state = AP_SOLO_JOB_FAILED; g_job.errorText = msg;
}
static void Log(const std::string&) {}
bool TrackPublicationBusy();
static int spawned, applied;
static std::vector<std::string> childArgs;
static bool JobStart(JobKind kind, const std::vector<std::string>& args, uint64_t limit) {
    assert(!TrackPublicationBusy() && g_job.state == AP_SOLO_JOB_IDLE);
    assert(kind == JOB_TRACKS && limit == kQuickLimitMs);
    childArgs = args; spawned++; g_job.state = AP_SOLO_JOB_RUNNING;
    return true;
}
'''
start = source.index("struct StorePublication\n{")
fixture += source[start:source.index("void PublishStoreEntry(", start)]
start = source.index("std::future<StorePublication> g_trackPublication;")
fixture += source[start:source.index("} // namespace", start)]
fixture += r'''
static std::atomic<bool> entered{false}, releaseWorker{false};
static int verifierFailure;
static bool publishFailure;
static std::thread::id frameThread;
static StorePublication CollectInstalledStore(const std::string& assets, const std::string& pin,
                                             const std::atomic<bool>* cancel) {
    assert(std::this_thread::get_id() != frameThread);
    assert(assets == "test-assets" && pin.empty());
    entered.store(true);
    while (!releaseWorker.load() && !cancel->load()) std::this_thread::yield();
    StorePublication r; r.failed = verifierFailure; r.error = "damaged package";
    for (int i = 0; i < 118; i++) r.descriptors.push_back({1, std::to_string(i), "1", "{}", "Track " + std::to_string(i), ""});
    return r;
}
static bool ApplyStoreDescriptor(const StorePublication::Descriptor& d, const StorePublication&, std::string& message) {
    assert(std::this_thread::get_id() == frameThread);
    applied++;
    if (publishFailure && applied % 2) message = "Track " + d.key + " 1: solo/tracks is read-only";
    return !(publishFailure && applied % 2);
}
'''
for signature in ("bool TrackPublicationBusy()", "bool StartTrackPublication(const std::vector<std::string> &args)",
                  "bool TickTrackPublication()", "void CancelTrackPublication()", "void FinishTrackPublication()",
                  'extern "C" int AP_Solo_TracksStart(const char *yamlPath)',
                  'extern "C" void AP_Solo_JobCancel(void)'):
    fixture += function(signature)
fixture += r'''
static void enter() {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!entered.load()) {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::yield();
    }
}
static void retire() {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (TrackPublicationBusy()) {
        int before = applied;
        TickTrackPublication();
        assert(applied - before <= 1);
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::yield();
    }
}
static void reset() {
    assert(!TrackPublicationBusy());
    entered = false; releaseWorker = false;
    g_job.state = AP_SOLO_JOB_IDLE; spawned = applied = 0;
    verifierFailure = 0; publishFailure = false;
}
int main() {
    frameThread = std::this_thread::get_id();
    reset();
    assert(AP_Solo_TracksStart("chosen.yaml")); enter();
    assert(g_job.state == AP_SOLO_JOB_RUNNING && g_job.phase == "Verifying installed tracks");
    for (int i = 0; i < 100; i++) assert(TickTrackPublication());
    assert(!spawned && !applied); // Frame has control while verifier is held.
    assert(!AP_Solo_TracksStart("other.yaml"));
    AP_Solo_JobCancel(); // Must return while the verifier is still held.
    assert(g_job.state == AP_SOLO_JOB_IDLE);
    retire(); assert(!spawned && !applied);

    reset(); assert(AP_Solo_TracksStart("chosen.yaml")); enter();
    releaseWorker = true; retire();
    assert(applied == 118 && spawned == 1);
    assert(childArgs == std::vector<std::string>({"tracks", "--assets", "test-assets", "--yaml", "chosen.yaml"}));

    reset(); assert(AP_Solo_TracksStart(nullptr)); enter(); releaseWorker = true;
    while (applied < 2) { TickTrackPublication(); std::this_thread::yield(); }
    AP_Solo_JobCancel();
    assert(!TrackPublicationBusy() && applied == 2 && !spawned);
    assert(!TickTrackPublication()); // No later writes or stale child handoff.

    reset(); verifierFailure = 1;
    assert(AP_Solo_TracksStart(nullptr)); enter(); releaseWorker = true; retire();
    assert(!applied && !spawned && g_job.state == AP_SOLO_JOB_FAILED);
    assert(g_job.errorText == "damaged package");

    reset(); publishFailure = true; // A refused track does not stop the others; the first is named.
    assert(AP_Solo_TracksStart(nullptr)); enter(); releaseWorker = true; retire();
    assert(applied == 118 && !spawned && g_job.state == AP_SOLO_JOB_FAILED);
    assert(g_job.errorText == "Track 0 1: solo/tracks is read-only");

    reset(); assert(AP_Solo_TracksStart(nullptr)); enter();
    FinishTrackPublication(); // Quit joins a cancelled worker and retains no future.
    assert(!TrackPublicationBusy() && !applied && !spawned);

    reset(); runtime = false; assert(!AP_Solo_TracksStart(nullptr));
    assert(!TrackPublicationBusy());
    runtime = true; link = true; assert(AP_Solo_TracksStart(nullptr));
    assert(g_job.state == AP_SOLO_JOB_FAILED && !TrackPublicationBusy());
    puts("PASS: start/poll/cancel return during verification; 118 entries publish one per frame; a refusal is named and never spawns the child");
}
'''
# The frame dispatcher must service the verifier before polling a child that
# does not exist yet. The full production compile checks the surrounding unit.
assert function("void TickJob()").index("TickTrackPublication()") < function("void TickJob()").index("g_job.child.poll()")
assert "AP_Solo_PublishInstalledStore" not in function('extern "C" int AP_Solo_TracksStart(const char *yamlPath)')
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "publication.cpp").write_text(fixture)
    subprocess.run(["g++", "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                    "-I", str(root / "ap/vendor/json/include"), str(path / "publication.cpp"),
                    "-o", str(path / "publication")], check=True)
    subprocess.run([str(path / "publication")], check=True, timeout=20)
