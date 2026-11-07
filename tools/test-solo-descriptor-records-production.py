"""Run the production Play Solo descriptor writers against a scratch client folder.

A tester report on 0.3.0-alpha1 showed only "Could not publish the Play Solo
descriptor" after a custom track install. This drives the real record, keep,
duplicate and apply functions from ap/ap_solo.cpp through the refusals a
player can hit: a reinstall whose files changed under the same version, two
installed copies with one track id and version, a read-only folder, a linked
folder and bad kept laps. Every refusal must log its file name and give a
reason that names what to do; stale descriptors are replaced, live ones are not.
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


fixture = r'''
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>
#include "ap/ap_solo.h"
#include "ap/ap_solo_logic.hpp"
#include "include/platform/native_custom_package.h"
namespace fs = std::filesystem;
#define AP_SOLO_GETPID() getpid()
static const size_t kDescriptorMax = 64 * 1024;
static const size_t kMenuMessageMax = 170;
static std::string base, assets;
static std::vector<std::string> logged;
static void Log(const std::string &line) { logged.push_back(line); }
static std::string BaseDir() { return base + "/"; }
static std::string SoloPath(const std::string &rel) { return BaseDir() + "solo/" + rel; }
static std::string AssetsDir() { return assets; }
namespace {
'''
for signature in ("bool SoloRootIsLink()", "std::string ReadSmallPlainFile(const fs::path &path, size_t limit)",
                  "int WriteNewFile(const fs::path &path, const std::string &text)"):
    fixture += function(signature)
fixture += between("// Keeps `json` as solo/<folder>/<name>; true once the file holds it.",
                   "// solo/needs-laps/<track key>__<revision>.json of a listed row")
fixture += function("bool PinnedFilePresent(const std::string &key, const nlohmann::json &pin)")
fixture += "}\n"
fixture += between("namespace\n{\n// \"<revision>|lev:<sha256>:<bytes>|vrm:<sha256>:<bytes>\"",
                   "#ifdef CTR_CUSTOM_PACKAGES\nnamespace {\n// PinSignature of a verified store package")
fixture += "namespace {\n"
fixture += between("// PinSignature of a verified store package", "void PublishStoreEntry(")
fixture += between("// Two installed packages can resolve to one Play Solo entry",
                   "StorePublication CollectInstalledStore(")
fixture += between("// Writes one collected descriptor.", "std::future<StorePublication> g_trackPublication;")
fixture += r'''
}
static std::string pins(const char *lev, const char *vrm, unsigned levBytes = 100, unsigned vrmBytes = 200) {
    return std::string("[{\"role\":\"lev\",\"sha256\":\"") + lev + "\",\"bytes\":" + std::to_string(levBytes) +
           "},{\"role\":\"vrm\",\"sha256\":\"" + vrm + "\",\"bytes\":" + std::to_string(vrmBytes) + "}]";
}
static std::string descriptor(const char *title, const std::string &files, int laps = 3) {
    std::string d = std::string("{\"id\":\"package/nina/1.0.0\",\"content_id\":\"nina\",\"revision\":\"1.0.0\",") +
                    "\"display_name\":\"" + title + "\",\"author\":\"A\",\"files\":" + files;
    if (laps) d += ",\"laps\":" + std::to_string(laps);
    return d + "}";
}
static std::string sig(const char *lev, const char *vrm) {
    return "1.0.0|lev:" + std::string(lev) + ":100|vrm:" + vrm + ":200";
}
static std::string read(const std::string &rel) {
    std::ifstream in(base + "/solo/" + rel, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
static void write(const std::string &rel, const std::string &text) {
    fs::create_directories(fs::path(base + "/solo/" + rel).parent_path());
    std::ofstream(base + "/solo/" + rel, std::ios::binary) << text;
}
static bool loggedFile(const char *name) {
    for (const auto &l : logged) if (l.find(name) != std::string::npos) return true;
    return false;
}
static void clean() {
    std::error_code ec;
    fs::permissions(base + "/solo/tracks", fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(base + "/solo", ec); fs::remove_all(assets, ec);
    fs::create_directories(assets + "/tracks/packages");
    logged.clear();
}
static StorePublication::Descriptor entry(int kind, const char *title, const std::string &json, const char *pin) {
    return {kind, "nina", "1.0.0", json, title, std::string(64, pin[0])};
}
int main(int, char **argv) {
    base = argv[1]; assets = base + "/assets";
    const char *name = "nina__1.0.0.json";
    const std::string oldA = descriptor("Nina", pins("aa", "ab")), newB = descriptor("Nina", pins("ba", "bb"));
    std::set<std::string> liveB = {sig("ba", "bb")}, liveAB = {sig("aa", "ab"), sig("ba", "bb")};
    std::string why;

    // 1. The tester's likely path: the track was reinstalled with new files
    // under the same version. The old files are gone: the stale entry is replaced.
    clean(); write(std::string("tracks/") + name, oldA);
    assert(!AP_Solo_RecordTrackDescriptor("nina", "1.0.0", newB.c_str())); // seed path stays strict
    assert(read(std::string("tracks/") + name) == oldA && loggedFile(name));
    StorePublication scan; scan.live = liveB;
    assert(ApplyStoreDescriptor(entry(1, "Nina", newB, "b"), scan, why));
    assert(read(std::string("tracks/") + name) == newB);
    assert(loggedFile("its files are no longer installed"));

    // 2. Both copies still installed, full rescan: the owner keeps the entry.
    clean(); write(std::string("tracks/") + name, oldA);
    scan.live = liveAB; std::string message;
    assert(!ApplyStoreDescriptor(entry(1, "Nina's Nightmare 99", newB, "b"), scan, message));
    assert(message.rfind("Nina's Nightmare 99 1.0.0: another installed copy", 0) == 0);
    assert(message.find("reinstall the copy you want to play") != std::string::npos && message.size() <= 170);
    assert(read(std::string("tracks/") + name) == oldA && loggedFile(name));

    // 3. The player just installed the new copy: it takes over the entry.
    scan.pin = std::string(64, 'b');
    assert(ApplyStoreDescriptor(entry(1, "Nina", newB, "b"), scan, why));
    assert(read(std::string("tracks/") + name) == newB);
    scan.pin.clear();

    // 4. Old pins still present as loose files in assets/tracks/nina/original:
    // not stale, so a rescan does not replace them.
    clean(); write(std::string("tracks/") + name, oldA);
    fs::create_directories(assets + "/tracks/nina/original");
    std::ofstream(assets + "/tracks/nina/original/n.lev") << std::string(100, 'x');
    std::ofstream(assets + "/tracks/nina/original/n.vrm") << std::string(200, 'x');
    scan.live = liveB;
    assert(!ApplyStoreDescriptor(entry(1, "Nina", newB, "b"), scan, message));
    assert(read(std::string("tracks/") + name) == oldA);

    // 5. A reinstall that needs laps replaces a stale complete entry: the new
    // one waits for laps and the stale one goes.
    clean(); write(std::string("tracks/") + name, oldA);
    const std::string waitB = descriptor("Nina", pins("ba", "bb"), 0);
    assert(ApplyStoreDescriptor(entry(2, "Nina", waitB, "b"), scan, why));
    assert(read(std::string("needs-laps/") + name) == waitB && !fs::exists(base + "/solo/tracks/" + name));

    // 6. Same pins: the chosen laps stay; bad kept laps are named with the file.
    clean(); write(std::string("tracks/") + name, descriptor("Old", pins("ba", "bb"), 5));
    assert(ApplyStoreDescriptor(entry(2, "Nina", waitB, "b"), scan, why));
    assert(nlohmann::json::parse(read(std::string("tracks/") + name)) == nlohmann::json::parse(descriptor("Nina", pins("ba", "bb"), 5)));
    write(std::string("tracks/") + name, descriptor("Old", pins("ba", "bb"), 9));
    assert(!ApplyStoreDescriptor(entry(2, "Nina", waitB, "b"), scan, message));
    assert(message.find("its kept lap count is not 1 to 7; delete its file in solo/tracks") != std::string::npos);

    // 7. A pending entry for stale files is replaced too.
    clean(); write(std::string("needs-laps/") + name, descriptor("Nina", pins("aa", "ab"), 0));
    assert(ApplyStoreDescriptor(entry(1, "Nina", newB, "b"), scan, why));
    assert(read(std::string("tracks/") + name) == newB && !fs::exists(base + "/solo/needs-laps/" + name));

    // 7b. A stale entry that cannot be removed: refused before anything is
    // written, so only one state is left.
    clean(); write(std::string("tracks/") + name, oldA);
    fs::permissions(base + "/solo/tracks", fs::perms::owner_read | fs::perms::owner_exec);
    if (geteuid() != 0) {
        assert(!ApplyStoreDescriptor(entry(2, "Nina", waitB, "b"), scan, message));
        assert(message == "Nina 1.0.0: its file in solo/tracks is read-only or in use (antivirus?)");
        assert(!fs::exists(base + "/solo/needs-laps/" + name) && loggedFile(name));
    }
    fs::permissions(base + "/solo/tracks", fs::perms::owner_all, fs::perm_options::add);

    // 8. A read-only solo/tracks folder: refused with the reason and file name.
    clean(); fs::create_directories(base + "/solo/tracks");
    fs::permissions(base + "/solo/tracks", fs::perms::owner_read | fs::perms::owner_exec);
    if (geteuid() != 0) {
        assert(!ApplyStoreDescriptor(entry(1, "Nina", newB, "b"), scan, message));
        assert(message.find("could not write in solo/tracks (read-only") != std::string::npos);
        assert(loggedFile(name));
    }
    fs::permissions(base + "/solo/tracks", fs::perms::owner_all, fs::perm_options::add);

    // 9. A read-only kept file is still replaced (Windows will not replace it).
    clean(); write(std::string("tracks/") + name, descriptor("Old", pins("ba", "bb")));
    fs::permissions(base + "/solo/tracks/" + name, fs::perms::owner_read);
    assert(ApplyStoreDescriptor(entry(1, "Nina", newB, "b"), scan, why));
    assert(read(std::string("tracks/") + name) == newB);

    // 10. A linked solo/tracks folder is named.
    clean(); fs::create_directories(base + "/elsewhere"); fs::create_directories(base + "/solo");
    fs::create_directory_symlink(base + "/elsewhere", base + "/solo/tracks");
    assert(!ApplyStoreDescriptor(entry(1, "Nina", newB, "b"), scan, message));
    assert(message.find("solo/tracks is a link") != std::string::npos && loggedFile(name));
    fs::remove(base + "/solo/tracks");

    // 11. Two installed copies with one id and version in a full scan: one
    // publishes, the other is left out with a log line, nothing fails.
    clean(); write(std::string("tracks/") + name, oldA);
    StorePublication two; two.assets = assets; two.live = liveAB;
    two.descriptors = {entry(1, "Nina B", newB, "b"), entry(1, "Nina A", oldA, "a")};
    KeepOneCopyPerEntry(two);
    assert(two.descriptors.size() == 1 && two.descriptors[0].title == "Nina A"); // owns the entry now
    assert(two.notes.size() == 1 && two.notes[0].rfind("left Nina B (package bbbbbbbbbbbb) out of Play Solo", 0) == 0);
    clean();
    for (const char *p : {"a", "b"}) fs::create_directories(assets + "/tracks/packages/" + std::string(64, p[0]));
    std::ofstream(assets + "/tracks/packages/" + std::string(64, 'a') + "/manifest.json") << "{}";
    std::ofstream(assets + "/tracks/packages/" + std::string(64, 'b') + "/manifest.json") << "{}";
    fs::last_write_time(assets + "/tracks/packages/" + std::string(64, 'a') + "/manifest.json",
                        fs::file_time_type::clock::now() - std::chrono::hours(1));
    two.descriptors = {entry(1, "Nina B", newB, "b"), entry(1, "Nina A", oldA, "a")};
    KeepOneCopyPerEntry(two);
    assert(two.descriptors.size() == 1 && two.descriptors[0].title == "Nina B"); // newest install
    for (const auto &d : two.descriptors) assert(ApplyStoreDescriptor(d, two, why));
    two.descriptors = {entry(1, "Nina", newB, "b"), entry(1, "Nina again", descriptor("Nina", pins("ba", "bb"), 4), "c")};
    KeepOneCopyPerEntry(two);
    assert(two.descriptors.size() == 2); // same files: both stay, as before

    // 12. A store package's pins read the same as its descriptor's.
    CustomPackageManifest m = {};
    std::strcpy(m.version, "1.0.0"); m.count = 3;
    std::strcpy(m.files[0].role, "presentation"); m.files[0].bytes = 7;
    std::strcpy(m.files[1].role, "vrm"); std::strcpy(m.files[1].sha256, "bb"); m.files[1].bytes = 200;
    std::strcpy(m.files[2].role, "lev"); std::strcpy(m.files[2].sha256, "ba"); m.files[2].bytes = 100;
    assert(StoreSignature(m) == sig("ba", "bb"));
    assert(StoreSignature(m) == PinSignature("1.0.0", nlohmann::json::parse(pins("ba", "bb"))));

    // A refused package is named by title, else by its pin.
    CustomPackageStoreEntry e = {};
    std::strcpy(e.pin, std::string(64, 'c').c_str());
    assert(PackageLabel(e) == "package cccccccccccc");
    std::strcpy(e.manifest.title, "Space Labs");
    assert(PackageLabel(e) == "Space Labs");

    // 13. A long title is cut so the whole message fits the 170-character menus.
    clean(); write(std::string("tracks/") + name, oldA); scan.live = liveAB;
    assert(!ApplyStoreDescriptor(entry(1, "A Very Long Custom Track Title From Saphi Number Nine", newB, "b"), scan, message));
    assert(message.size() <= 170 && message.rfind("A Very Long Custom Track Title From S... 1.0.0: ", 0) == 0);
    std::string accents;
    for (int k = 0; k < 21; k++) accents += "\xc3\xa9"; // 42 bytes; byte 37 is inside one
    assert(!ApplyStoreDescriptor(entry(1, "", newB, "b"), scan, message) && message.rfind("nina 1.0.0: ", 0) == 0);
    assert(!ApplyStoreDescriptor(entry(1, accents.c_str(), newB, "b"), scan, message));
    assert(message.rfind(accents.substr(0, 36) + "... 1.0.0: ", 0) == 0);
    // 14. A long version and reason still fit the menu, cut on a UTF-8 boundary.
    clean(); write(std::string("tracks/") + name, oldA); scan.live = liveAB;
    StorePublication::Descriptor longRev = entry(1, accents.c_str(), newB, "b");
    longRev.revision = "1.0.0-" + std::string(58, 'r');
    write("tracks/nina__" + longRev.revision + ".json", oldA);
    scan.live.insert(longRev.revision + "|lev:aa:100|vrm:ab:200");
    assert(!ApplyStoreDescriptor(longRev, scan, message)); // the live copy keeps the entry
    assert(message.size() <= 170 && message.find("1.0.0-rrrrrrr...: ") != std::string::npos);
    // 15. A3-29: the same package installed again under another name. The newer
    // copy lists the track under its key, then the older key's files go and the
    // laps the player picked for it move to the new key.
    {
        const std::string files = pins("ca", "cb");
        auto copy = [&](const char *key, const char *title, bool laps, const char *uuid) {
            std::string d = std::string("{\"id\":\"package/") + key + "/1.0.0\",\"content_id\":\"" + key +
                            "\",\"revision\":\"1.0.0\",\"display_name\":\"" + title + "\",\"author\":\"A\",\"uuid\":\"" +
                            uuid + "\",\"files\":" + files;
            if (laps) d += ",\"laps\":4";
            return d + "}";
        };
        const std::string oldName = "generic-slot__1.0.0.json", newName = "my-name__1.0.0.json";
        const std::string newWaiting = copy("my-name", "My Name", false, "u1");
        StorePublication::Descriptor keeper = {2, "my-name", "1.0.0", newWaiting, "My Name", std::string(64, 'n')};
        StorePublication::Descriptor retire = {0, "generic-slot", "1.0.0", "u1", "Generic", std::string(64, 'o'),
                                               std::string(64, 'n'), "my-name"};
        StorePublication live;
        live.live = {sig("ca", "cb")};
        clean(); write("tracks/" + oldName, copy("generic-slot", "Generic", true, "u1"));
        assert(ApplyStoreDescriptor(keeper, live, why) && ApplyStoreDescriptor(retire, live, why));
        assert(read("tracks/" + oldName).empty() && read("needs-laps/" + oldName).empty());
        assert(read("needs-laps/" + newName).empty());
        const auto moved = nlohmann::json::parse(read("tracks/" + newName), nullptr, false);
        assert(moved.is_object() && moved["laps"] == 4 && moved["display_name"] == "My Name");
        assert(loggedFile(oldName.c_str()));

        // The replacement was never listed (its write failed): the old row stays.
        clean(); write("tracks/" + oldName, copy("generic-slot", "Generic", true, "u1"));
        StorePublication none;
        assert(ApplyStoreDescriptor(retire, none, why));
        assert(!read("tracks/" + oldName).empty());

        // A file of another package under the old key is not ours to retire.
        clean(); write("tracks/" + oldName, copy("generic-slot", "Other pack", true, "u2"));
        assert(ApplyStoreDescriptor(keeper, live, why) && ApplyStoreDescriptor(retire, live, why));
        assert(!read("tracks/" + oldName).empty() && read("tracks/" + newName).empty());

        // The laps cannot be moved (the new key's file is blocked): the old file keeps them and the failure is named.
        clean(); write("tracks/" + oldName, copy("generic-slot", "Generic", true, "u1"));
        fs::create_directories(base + "/solo/tracks/" + newName);
        assert(ApplyStoreDescriptor(keeper, live, why));
        std::string moveFailure;
        assert(!ApplyStoreDescriptor(retire, live, moveFailure) && moveFailure.find("laps could not be moved") != std::string::npos);
        assert(!read("tracks/" + oldName).empty());

        // A track still waiting for laps under the old key is retired too.
        clean(); write("needs-laps/" + oldName, copy("generic-slot", "Generic", false, "u1"));
        assert(ApplyStoreDescriptor(keeper, live, why) && ApplyStoreDescriptor(retire, live, why));
        assert(read("needs-laps/" + oldName).empty() && !read("needs-laps/" + newName).empty());
    }
    puts("PASS: stale descriptors are replaced, live ones are named and kept, every refusal logs its file");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "records.cpp").write_text(fixture)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                    "-I", str(root / "ap/vendor/json/include"), str(path / "records.cpp"),
                    "-o", str(path / "records")], check=True)
    (path / "client").mkdir()
    subprocess.run([str(path / "records"), str(path / "client")], check=True, timeout=30)
