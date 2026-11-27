"""Run the production Play Solo descriptor writers against a scratch client folder.

A tester report on 0.3.0-alpha1 showed only "Could not publish the Play Solo
descriptor" after a custom track install. This drives the real record, keep,
duplicate and apply functions from ap/ap_solo.cpp through the refusals a
player can hit: a reinstall whose files changed under the same version, two
installed copies with one track id and version, a read-only folder, a linked
folder and bad kept laps. Every refusal must log its file name and give a
reason that names what to do; stale descriptors are replaced, live ones are not.

PS2: two installed builds of one track id and version with other files (a
Saphi download beside an LRG2Pancho import) are never refused: the second
build keeps its own descriptor file, in both install orders, on a full scan,
a fresh install and the room check, and an old descriptor owned by the other
build heals on the next publication.
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
#include "ap/ap_track_key_aliases.hpp"
#include "include/platform/native_custom_package.h"
#include "include/platform/native_custom_track_visibility.h"
#ifndef CTR_AP
#define CTR_AP 1 // the client builds ap_solo.cpp with it; the SHA-256 helpers need it
#endif
#include "include/platform/native_sha256.h"
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
static std::string ap_track_registry_canonical_key(const std::string &key) {
    for (const auto &alias : kTrackKeyAliases) if (key == alias[0]) return alias[1];
    return key;
}
static bool storeBusy;
static bool StorePublicationBusy() { return storeBusy; }
extern "C" int AP_Solo_PublishInstalledStoreStart() { storeBusy = false; return 1; }
static std::vector<APSoloModePair> g_pairs; // MR2: the pairs WithModePair sees
std::vector<APSoloModePair> AP_SoloKnownModePairs() { return g_pairs; }
static std::set<std::string> g_installed; // MR2: versions the Relic pins are installed under
std::set<std::string> AP_SoloInstalledVersionsByPins(const nlohmann::json &) { return g_installed; }
namespace {
'''
for signature in ("bool SoloRootIsLink()", "std::string ReadSmallPlainFile(const fs::path &path, size_t limit)",
                  "int WriteNewFile(const fs::path &path, const std::string &text)"):
    fixture += function(signature)
fixture += between("// Keeps `json` as solo/<folder>/<name>; true once the file holds it.",
                   "// solo/needs-laps/<track key>__<revision>.json of a listed row")
fixture += function("bool PinnedFilePresent(const std::string &key, const nlohmann::json &pin)")
fixture += function("bool HiddenTrackDescriptor(")
fixture += "}\n"
fixture += between("namespace\n{\n// \"<revision>|lev:<sha256>:<bytes>|vrm:<sha256>:<bytes>\"",
                   "#ifdef CTR_CUSTOM_PACKAGES\nnamespace {\n// PinSignature of a verified store package")
fixture += "namespace {\n"
fixture += between("// PinSignature of a verified store package", "void PublishStoreEntry(")
fixture += between("void KeepOneCopyPerEntry(",
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

    // 2. Both copies still installed, full rescan, pins that are not SHA-256
    // hashes (no build tag, so no second file name): the owner keeps the
    // entry and the other copy is refused as before PS2.
    clean(); write(std::string("tracks/") + name, oldA);
    scan.live = liveAB; std::string message;
    assert(!ApplyStoreDescriptor(entry(1, "Nina's Nightmare 99", newB, "b"), scan, message));
    assert(message.rfind("Nina's Nightmare 99 1.0.0: another installed copy", 0) == 0);
    assert(message.find("reinstall the copy you want to play") != std::string::npos && message.size() <= 170);
    assert(read(std::string("tracks/") + name) == oldA && loggedFile(name));

    // A hidden copy retains its pinned files, and its discovery entry can be
    // replaced by an eligible copy sharing its key and version.
    const char *hidden = kCustomTrackHiddenLevs[0];
    const auto hiddenPins = pins(hidden, "ab");
    for (const char *folder : {"tracks/", "needs-laps/"}) {
        clean();
        write(std::string(folder) + name, descriptor("Nina", hiddenPins, std::strcmp(folder, "tracks/") == 0 ? 3 : 0));
        write("rooms/old/room.json", hiddenPins);
        scan.live = {sig(hidden, "ab"), sig("ba", "bb")};
        assert(ApplyStoreDescriptor(entry(1, "Nina", newB, "b"), scan, why));
        assert(read(std::string("tracks/") + name) == newB);
        assert(read("rooms/old/room.json") == hiddenPins && scan.live.count(sig(hidden, "ab")));
        assert(loggedFile("the previous copy is hidden"));
    }
    clean(); write(std::string("tracks/") + name, oldA);

    // Importing the chosen copy can update the active Play Solo entry.
    storeBusy = true;
    assert(AP_Solo_RecordImportedTrack("nina", "1.0.0", newB.c_str(), 0));
    assert(!storeBusy); // a worker with an older snapshot must stop publishing
    assert(read(std::string("tracks/") + name) == newB);
    clean(); write(std::string("tracks/") + name, oldA);

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

    // 11. PS2: two installed builds with one id and version and other files in
    // a full scan: both stay listed, nothing is left out, nothing fails. The
    // build that holds the plain name keeps it; the other gets its own file.
    const std::string hexA = descriptor("Nina", pins(std::string(64, 'a').c_str(), std::string(64, 'b').c_str()));
    const std::string hexB = descriptor("Nina", pins(std::string(64, 'c').c_str(), std::string(64, 'd').c_str()));
    const std::string buildB = "nina__1.0.0@ccccccccccccccccdddddddddddddddd.json", buildA = "nina__1.0.0@aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbb.json";
    const std::set<std::string> liveHex = {"1.0.0|lev:" + std::string(64, 'a') + ":100|vrm:" + std::string(64, 'b') + ":200",
                                           "1.0.0|lev:" + std::string(64, 'c') + ":100|vrm:" + std::string(64, 'd') + ":200"};
    clean(); write(std::string("tracks/") + name, hexA);
    StorePublication two; two.assets = assets; two.live = liveHex;
    two.descriptors = {entry(1, "Nina B", hexB, "b"), entry(1, "Nina A", hexA, "a")};
    KeepOneCopyPerEntry(two);
    assert(two.descriptors.size() == 2 && two.notes.empty());
    for (const auto &d : two.descriptors) assert(ApplyStoreDescriptor(d, two, why));
    assert(read(std::string("tracks/") + name) == hexA && read("tracks/" + buildB) == hexB);
    assert(two.listed.size() == 2 && !loggedFile("refused"));
    two.descriptors = {entry(1, "Nina", newB, "b"), entry(1, "Nina again", descriptor("Nina", pins("ba", "bb"), 4), "c")};
    KeepOneCopyPerEntry(two);
    assert(two.descriptors.size() == 2); // same files: both stay, as before

    // PS2: different package UUIDs and different pins under two keys of one
    // track are two builds: both stay listed, nothing is retired.
    clean();
    auto alias = entry(1, "Lockbox track 11", oldA, "a");
    auto canonical = entry(1, "Cortex Vortex", newB, "b");
    alias.key = "lockbox-adventure-track-11";
    canonical.key = "cortex-vortex";
    for (auto *d : {&alias, &canonical}) {
        auto body = nlohmann::json::parse(d->json);
        body["content_id"] = d->key;
        body["id"] = "package/" + d->key + "/1.0.0";
        body["uuid"] = d->pin;
        d->json = body.dump();
        write("tracks/" + AP_SoloDescriptorFileName(d->key, d->revision), d->json);
    }
    const std::string aliasName = AP_SoloDescriptorFileName(alias.key, alias.revision);
    fs::last_write_time(base + "/solo/tracks/" + aliasName,
                        fs::file_time_type::clock::now() - std::chrono::hours(1));
    two.descriptors = {alias, canonical};
    two.listed.clear();
    KeepOneCopyPerEntry(two);
    assert(two.descriptors.size() == 2 && two.descriptors[0].kind == 1 && two.descriptors[1].kind == 1);
    for (const auto &d : two.descriptors) assert(ApplyStoreDescriptor(d, two, why));
    assert(read("tracks/" + aliasName) == alias.json);
    assert(read("tracks/" + AP_SoloDescriptorFileName(canonical.key, canonical.revision)) == canonical.json);
    // The same files under two keys of one track are still one row: the
    // older alias is retired (A3-29).
    clean();
    {
        auto body = nlohmann::json::parse(alias.json);
        body["files"] = nlohmann::json::parse(canonical.json)["files"];
        alias.json = body.dump();
    }
    write("tracks/" + aliasName, alias.json);
    write("tracks/" + AP_SoloDescriptorFileName(canonical.key, canonical.revision), canonical.json);
    fs::last_write_time(base + "/solo/tracks/" + aliasName,
                        fs::file_time_type::clock::now() - std::chrono::hours(1));
    two.descriptors = {alias, canonical};
    two.listed.clear();
    KeepOneCopyPerEntry(two);
    assert(two.descriptors.size() == 2 && two.descriptors[0].pin == canonical.pin && two.descriptors[1].kind == 0);

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
    // MR2: a Relic-only package (kind 3) is never listed. Our old file for the
    // same key, version and pins is removed from both folders; a file with
    // other pins (another copy) and other names stay.
    {
        clean();
        const std::string relic = descriptor("Nina", pins("ca", "cb")), other = descriptor("Nina", pins("da", "db"));
        write(std::string("tracks/") + name, relic);
        write(std::string("needs-laps/") + name, descriptor("Nina", pins("ca", "cb"), 0));
        write("tracks/other__1.0.0.json", relic);
        StorePublication scan;
        std::string message;
        assert(ApplyStoreDescriptor(entry(3, "Nina", nlohmann::json::parse(pins("ca", "cb")).dump(), "c"), scan, message));
        assert(!fs::exists(base + "/solo/tracks/" + name) && !fs::exists(base + "/solo/needs-laps/" + name));
        assert(read("tracks/other__1.0.0.json") == relic && loggedFile("it is the Relic Race file of a split track"));
        assert(scan.listed.empty());
        write(std::string("tracks/") + name, other);
        assert(ApplyStoreDescriptor(entry(3, "Nina", nlohmann::json::parse(pins("ca", "cb")).dump(), "c"), scan, message));
        assert(read(std::string("tracks/") + name) == other);
    }
    // MR2: a race pair whose laps are kept in solo/tracks keeps its Relic pair
    // when a reinstall republishes it as a needs-laps row.
    {
        clean();
        APSoloModePair p;
        p.trackKey = "nina"; p.raceRevision = "1.0.0"; p.relicRevision = "1.0.1";
        p.raceLev = std::string(64, 'a'); p.raceVrm = std::string(64, 'b');
        p.relicLev = std::string(64, 'c'); p.relicVrm = std::string(64, 'd');
        p.raceLevBytes = 100; p.raceVrmBytes = 200; p.relicLevBytes = 300; p.relicVrmBytes = 400;
        p.targets[0] = 3; p.targets[1] = 2; p.targets[2] = 1; p.source = "test";
        g_pairs = {p};
        const std::string race = pins(std::string(64, 'a').c_str(), std::string(64, 'b').c_str());
        auto kept = nlohmann::json::parse(descriptor("Nina", race));
        kept["capabilities"] = {{"relic", false}, {"time_trial", false}};
        write(std::string("tracks/") + name, kept.dump());
        auto fresh = kept; fresh.erase("laps");
        StorePublication scan;
        std::string message;
        assert(ApplyStoreDescriptor(entry(2, "Nina", fresh.dump(), "a"), scan, message));
        const auto now = nlohmann::json::parse(read(std::string("tracks/") + name));
        assert(now["laps"] == 3 && now.contains("relic_files") && now["relic_files"][0]["sha256"] == std::string(64, 'c'));
        assert(now["relic_revision"] == "1.0.1" && now["capabilities"]["relic"] == true);
        // MR2 review 2, finding 1: a kept row whose relic_revision is a label
        // the installed Relic files do not carry (saved laps wrote it) takes
        // the installed version on the next scan, so a room finds the files.
        auto stale = nlohmann::json::parse(read(std::string("tracks/") + name));
        stale["relic_revision"] = "1.0.1-relic";
        write(std::string("tracks/") + name, stale.dump());
        g_installed = {"1.0.1"};
        assert(ApplyStoreDescriptor(entry(2, "Nina", fresh.dump(), "a"), scan, message));
        auto healed = nlohmann::json::parse(read(std::string("tracks/") + name));
        assert(healed["laps"] == 3 && healed["relic_revision"] == "1.0.1" && healed.contains("relic_files"));
        // Installed under another version than the label (Saphi keeps its own).
        write(std::string("tracks/") + name, kept.dump());
        g_installed = {"0.9"};
        assert(ApplyStoreDescriptor(entry(2, "Nina", fresh.dump(), "a"), scan, message));
        healed = nlohmann::json::parse(read(std::string("tracks/") + name));
        assert(healed["relic_revision"] == "0.9" && loggedFile("installed as 0.9, not 1.0.1"));
        g_installed.clear();
        g_pairs.clear();
    }
    // MR2 review 2, finding 3: laps moved to a replacement key (kind 0) keep
    // the Relic pair; the moved row is paired like any other.
    {
        clean();
        APSoloModePair p;
        p.trackKey = "my-name"; p.raceRevision = "1.0.0"; p.relicRevision = "1.0.1";
        p.raceLev = std::string(64, 'a'); p.raceVrm = std::string(64, 'b');
        p.relicLev = std::string(64, 'c'); p.relicVrm = std::string(64, 'd');
        p.raceLevBytes = 100; p.raceVrmBytes = 200; p.relicLevBytes = 300; p.relicVrmBytes = 400;
        p.targets[0] = 3; p.targets[1] = 2; p.targets[2] = 1; p.source = "test";
        g_pairs = {p};
        g_installed = {"1.0.1"};
        const std::string race = pins(std::string(64, 'a').c_str(), std::string(64, 'b').c_str());
        auto row = [&](const char *key, const char *title, bool laps) {
            nlohmann::json d = {{"id", std::string("package/") + key + "/1.0.0"}, {"content_id", key}, {"revision", "1.0.0"},
                                {"display_name", title}, {"author", "A"}, {"uuid", "u1"}, {"files", nlohmann::json::parse(race)},
                                {"capabilities", {{"relic", false}, {"time_trial", false}}}};
            if (laps) d["laps"] = 4;
            return d;
        };
        const std::string oldName = "generic-slot__1.0.0.json", newName = "my-name__1.0.0.json";
        StorePublication::Descriptor keeper = {2, "my-name", "1.0.0", row("my-name", "My Name", false).dump(), "My Name",
                                               std::string(64, 'n')};
        StorePublication::Descriptor retire = {0, "generic-slot", "1.0.0", "u1", "Generic", std::string(64, 'o'),
                                               std::string(64, 'n'), "my-name"};
        StorePublication live;
        live.live = {"1.0.0|lev:" + std::string(64, 'a') + ":100|vrm:" + std::string(64, 'b') + ":200"};
        write("tracks/" + oldName, row("generic-slot", "Generic", true).dump());
        std::string why;
        assert(ApplyStoreDescriptor(keeper, live, why) && ApplyStoreDescriptor(retire, live, why));
        assert(read("tracks/" + oldName).empty());
        const auto moved = nlohmann::json::parse(read("tracks/" + newName), nullptr, false);
        assert(moved.is_object() && moved["laps"] == 4 && moved["display_name"] == "My Name");
        assert(moved.contains("relic_files") && moved["relic_files"][0]["sha256"] == std::string(64, 'c'));
        assert(moved["relic_revision"] == "1.0.1" && moved["capabilities"]["relic"] == true && moved.contains("relic_targets"));
        g_installed.clear();
        g_pairs.clear();
    }
    // MR2 review 2, finding 2: the solo runtime's mode pairs file (kind 4).
    // Without it an older room's Relic-only entry would be offered as a
    // track, so a failed write fails the publication instead of passing.
    {
        clean();
        StorePublication scan;
        std::string message;
        const StorePublication::Descriptor pairs = {4, "", "", "{\"schema_version\":1,\"relic_only\":[]}", "", ""};
        assert(ApplyStoreDescriptor(pairs, scan, message) && read("pairs/mode-pairs.json") == pairs.json);
        clean();
        write("pairs", "not a folder");
        assert(!ApplyStoreDescriptor(pairs, scan, message));
        assert(message.find("mode-pairs.json") != std::string::npos && message.size() <= 170);
        assert(loggedFile("mode pairs for the solo runtime not written"));
    }
    // MR2 review 3, finding 2: an older client listed a split track's Relic
    // build as a track of its own, under the race build's key and version
    // (Sunken Sprint, Cubic Circuit), and its files are a legacy loose copy
    // with no store entry for the kind 3 cleanup. The full scan's race row
    // replaces that row; Play Solo never stops on it. A kept row with other
    // pins that is not the pair's Relic build is still refused.
    {
        APSoloModePair p;
        p.trackKey = "nina"; p.raceRevision = "1.0.0"; p.relicRevision = "1.0.0-relic";
        p.raceLev = std::string(64, 'a'); p.raceVrm = std::string(64, 'b');
        p.relicLev = std::string(64, 'c'); p.relicVrm = std::string(64, 'd');
        p.raceLevBytes = 100; p.raceVrmBytes = 200; p.relicLevBytes = 300; p.relicVrmBytes = 400;
        p.targets[0] = 3; p.targets[1] = 2; p.targets[2] = 1; p.source = "test";
        const std::string a = std::string(64, 'a'), b = std::string(64, 'b'), c = std::string(64, 'c'), d = std::string(64, 'd');
        const std::string relicRow = descriptor("Nina Relic", pins(c.c_str(), d.c_str(), 300, 400));
        const std::string raceRow = descriptor("Nina", pins(a.c_str(), b.c_str()));
        const std::string raceWait = descriptor("Nina", pins(a.c_str(), b.c_str()), 0);
        const auto loose = [&] {
            fs::create_directories(assets + "/tracks/nina/original");
            std::ofstream(assets + "/tracks/nina/original/relic.lev") << std::string(300, 'x');
            std::ofstream(assets + "/tracks/nina/original/relic.vrm") << std::string(400, 'x');
        };
        StorePublication full;
        full.live = {"1.0.0|lev:" + a + ":100|vrm:" + b + ":200"};
        std::string message;
        for (int kind : {1, 2})
        {
            clean(); loose();
            g_pairs = {p};
            write(std::string("tracks/") + name, relicRow);
            write(std::string("needs-laps/") + name, relicRow);
            full.listed.clear();
            assert(ApplyStoreDescriptor(entry(kind, "Nina", kind == 1 ? raceRow : raceWait, "a"), full, message));
            assert(loggedFile("it is the Relic Race file of this track"));
            if (kind == 1)
                assert(nlohmann::json::parse(read(std::string("tracks/") + name))["files"][0]["sha256"] == a);
            else
                assert(!fs::exists(base + "/solo/tracks/" + name) && read(std::string("needs-laps/") + name) == raceWait);
            assert(full.listed.count(std::string(64, 'a')));
        }
        // Without the pair the old row is an installed copy with other files:
        // it keeps its name and the race row gets its own file (PS2).
        clean(); loose();
        g_pairs.clear();
        write(std::string("tracks/") + name, relicRow);
        assert(ApplyStoreDescriptor(entry(1, "Nina", raceRow, "a"), full, message));
        assert(read(std::string("tracks/") + name) == relicRow);
        assert(read("tracks/nina__1.0.0@aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbb.json") == raceRow);
        // The pair's Relic row never replaces another race build's row, and
        // never takes that build's own file either.
        clean(); loose();
        g_pairs = {p};
        const std::string otherRace = descriptor("Nina", pins(std::string(64, 'e').c_str(), b.c_str()));
        write(std::string("tracks/") + name, relicRow);
        assert(ApplyStoreDescriptor(entry(1, "Nina", otherRace, "e"), full, message));
        assert(read(std::string("tracks/") + name) == relicRow);
        assert(read("tracks/nina__1.0.0@eeeeeeeeeeeeeeeebbbbbbbbbbbbbbbb.json") == otherRace);
        g_pairs.clear();
    }
    // PS2: a Saphi download and an LRG2Pancho import of one track: same key
    // and version, other files. Every writer, both install orders, the room
    // check, the heal of an old client's state and a folded re-cut. The
    // loose files' bytes hash to the pins, so "installed" is checked by hash.
    {
        const std::string a = "f73e275ca463c6c2fea00d4e2cefaaca0795ba520bd725a86988f625d38b4a84", b = "ca9a62692f0dcf43e1fdc03fdc03b47e97613f7b791fe3a17f7c6cb29d79121f", c = "8e9d44da1cd3339b33b8fa50ef68a47498cd18ab902666ee79f1be49432c0e73", d = "4464fd9544c57c5d17e1204046ca73f4cc976088abe8c9018f3f5a2a8adb9ea0";
        const std::string saphi = descriptor("Big Forest", pins(a.c_str(), b.c_str()));
        const std::string lrg2 = descriptor("Big Forest", pins(c.c_str(), d.c_str()));
        const std::string lrg2Wait = descriptor("Big Forest", pins(c.c_str(), d.c_str()), 0);
        const std::string sb = AP_SoloBuildDescriptorFileName("nina", "1.0.0", AP_SoloBuildTag(nlohmann::json::parse(pins(a.c_str(), b.c_str()))));
        const std::string lb = AP_SoloBuildDescriptorFileName("nina", "1.0.0", AP_SoloBuildTag(nlohmann::json::parse(pins(c.c_str(), d.c_str()))));
        assert(lb == "nina__1.0.0@" + c.substr(0, 16) + d.substr(0, 16) + ".json" && sb != lb);
        const std::string n = name;
        // Loose files of both builds: either counts as installed.
        const auto installed = [&] {
            fs::create_directories(assets + "/tracks/nina/original");
            std::ofstream(assets + "/tracks/nina/original/s.lev") << std::string(100, 'L');
            std::ofstream(assets + "/tracks/nina/original/s.vrm") << std::string(200, 'V');
            std::ofstream(assets + "/tracks/nina/original/p.lev") << std::string(100, 'M');
            std::ofstream(assets + "/tracks/nina/original/p.vrm") << std::string(200, 'W');
        };
        StorePublication full;
        full.live = {"1.0.0|lev:" + a + ":100|vrm:" + b + ":200", "1.0.0|lev:" + c + ":100|vrm:" + d + ":200"};
        std::string message;

        // a) The tester report: the Saphi build holds the plain name, a room
        // pinning the LRG2 build checks its files. Its descriptor goes to its
        // own file; nothing is refused.
        clean(); write("tracks/" + n, saphi);
        assert(AP_Solo_RecordTrackDescriptor("nina", "1.0.0", lrg2.c_str()));
        assert(read("tracks/" + n) == saphi && read("tracks/" + lb) == lrg2);
        assert(!loggedFile("refused") && loggedFile("listing nina__1.0.0.json beside another installed build"));
        logged.clear(); // the next room check writes the same file again, quietly
        assert(AP_Solo_RecordTrackDescriptor("nina", "1.0.0", lrg2.c_str()) && logged.empty());
        // The full scan after it (heal): both builds publish, nothing is deleted by hand.
        for (const auto &e : {entry(1, "Big Forest", saphi, "a"), entry(1, "Big Forest", lrg2, "c")})
            assert(ApplyStoreDescriptor(e, full, message));
        assert(read("tracks/" + n) == saphi && read("tracks/" + lb) == lrg2 && !fs::exists(base + "/solo/tracks/" + sb));
        assert(!loggedFile("refused"));

        // b) Install order Saphi then import: the importer's fresh LRG2 build
        // waits for laps under its own name; the installed Saphi build keeps
        // its entry (main replaced it, and Play Solo lost the Saphi build).
        clean(); installed(); write("tracks/" + n, saphi);
        assert(AP_Solo_RecordImportedTrack("nina", "1.0.0", lrg2Wait.c_str(), 1));
        assert(read("tracks/" + n) == saphi && read("needs-laps/" + lb) == lrg2Wait);
        assert(AP_SoloNeedsLapsDescriptor(nlohmann::json::parse(lrg2Wait), lb));
        assert(!AP_SoloNeedsLapsDescriptor(nlohmann::json::parse(lrg2Wait), sb));
        assert(ApplyStoreDescriptor(entry(2, "Big Forest", lrg2Wait, "c"), full, message));
        assert(ApplyStoreDescriptor(entry(1, "Big Forest", saphi, "a"), full, message));
        assert(read("tracks/" + n) == saphi && read("needs-laps/" + lb) == lrg2Wait && !fs::exists(base + "/solo/needs-laps/" + n));

        // c) Install order import then Saphi: the LRG2 build waits under the
        // plain name; the Saphi install (one package's publication) gets its
        // own file and the waiting entry stays.
        clean(); installed(); write("needs-laps/" + n, lrg2Wait);
        StorePublication single; single.pin = a;
        assert(ApplyStoreDescriptor(entry(1, "Big Forest", saphi, "a"), single, message));
        assert(read("needs-laps/" + n) == lrg2Wait && read("tracks/" + sb) == saphi && !fs::exists(base + "/solo/tracks/" + n));
        // Laps picked for the waiting build complete it under the plain name.
        write("tracks/" + n, lrg2);
        assert(ApplyStoreDescriptor(entry(2, "Big Forest", lrg2Wait, "c"), full, message));
        assert(read("tracks/" + n) == lrg2 && !fs::exists(base + "/solo/needs-laps/" + n) && read("tracks/" + sb) == saphi);

        // d) The other build's files are gone: a fresh install still takes the
        // plain name over (a reinstall with new files), as before.
        clean(); write("tracks/" + n, saphi);
        assert(AP_Solo_RecordImportedTrack("nina", "1.0.0", lrg2.c_str(), 0));
        assert(read("tracks/" + n) == lrg2 && !fs::exists(base + "/solo/tracks/" + lb));

        // e) An older client's mixed state under one name: the Saphi build's
        // complete entry and the LRG2 build waiting. The LRG2 build's complete
        // descriptor moves to its own file and its plain waiting entry goes.
        clean(); installed(); write("tracks/" + n, saphi); write("needs-laps/" + n, lrg2Wait);
        assert(AP_Solo_RecordTrackDescriptor("nina", "1.0.0", lrg2.c_str()));
        assert(read("tracks/" + n) == saphi && read("tracks/" + lb) == lrg2 && !fs::exists(base + "/solo/needs-laps/" + n));
        // And the Saphi build republished there leaves the LRG2 entries alone.
        write("needs-laps/" + n, lrg2Wait);
        assert(ApplyStoreDescriptor(entry(1, "Big Forest", saphi, "a"), full, message));
        assert(read("tracks/" + n) == saphi && read("needs-laps/" + n) == lrg2Wait);

        // f) A build that already has its own file keeps it, also once the
        // plain name is free: one build never has two entries.
        clean(); write("tracks/" + lb, lrg2);
        assert(AP_Solo_RecordTrackDescriptor("nina", "1.0.0", lrg2.c_str()));
        assert(!fs::exists(base + "/solo/tracks/" + n) && read("tracks/" + lb) == lrg2);

        // g) Both builds in one full scan, either order, on an empty folder.
        for (bool saphiFirst : {true, false})
        {
            clean();
            StorePublication scan2; scan2.assets = assets; scan2.live = full.live;
            scan2.descriptors = saphiFirst ? std::vector<StorePublication::Descriptor>{entry(1, "Big Forest", saphi, "a"), entry(2, "Big Forest", lrg2Wait, "c")}
                                           : std::vector<StorePublication::Descriptor>{entry(2, "Big Forest", lrg2Wait, "c"), entry(1, "Big Forest", saphi, "a")};
            KeepOneCopyPerEntry(scan2);
            assert(scan2.descriptors.size() == 2 && scan2.notes.empty());
            for (const auto &e : scan2.descriptors) assert(ApplyStoreDescriptor(e, scan2, message));
            if (saphiFirst)
                assert(read("tracks/" + n) == saphi && read("needs-laps/" + lb) == lrg2Wait);
            else
                assert(read("needs-laps/" + n) == lrg2Wait && read("tracks/" + sb) == saphi);
            assert(scan2.listed.size() == 2 && !loggedFile("refused"));
        }

        // h) A re-cut of one package (same uuid and version, other files, an
        // older cut still installed) is one row, the newest install (A3-29),
        // and the older cut no longer holds the plain name.
        clean();
        auto cut = [&](const std::string &files, const char *pin) {
            auto body = nlohmann::json::parse(descriptor("Big Forest", files));
            body["uuid"] = "u-cut";
            return entry(1, "Big Forest", body.dump(), pin);
        };
        const auto oldCut = cut(pins(a.c_str(), b.c_str()), "a"), newCut = cut(pins(c.c_str(), d.c_str()), "c");
        write("tracks/" + n, oldCut.json);
        StorePublication recut; recut.assets = assets; recut.live = full.live;
        recut.descriptors = {oldCut, newCut};
        KeepOneCopyPerEntry(recut);
        assert(recut.descriptors.size() == 2 && recut.descriptors[0].pin == newCut.pin && recut.descriptors[1].kind == 5);
        assert(!recut.live.count("1.0.0|lev:" + a + ":100|vrm:" + b + ":200"));
        for (const auto &e : recut.descriptors) assert(ApplyStoreDescriptor(e, recut, message));
        assert(read("tracks/" + n) == newCut.json && !fs::exists(base + "/solo/tracks/" + lb));

        // j) Loose files of the right sizes but other bytes are not the other
        // build: a fresh install takes the name over, as before PS2.
        clean(); write("tracks/" + n, saphi);
        fs::create_directories(assets + "/tracks/nina/original");
        std::ofstream(assets + "/tracks/nina/original/x.lev") << std::string(100, 'x');
        std::ofstream(assets + "/tracks/nina/original/x.vrm") << std::string(200, 'x');
        assert(AP_Solo_RecordImportedTrack("nina", "1.0.0", lrg2.c_str(), 0));
        assert(read("tracks/" + n) == lrg2 && !fs::exists(base + "/solo/tracks/" + lb));

        // k) A newer cut of the same package (one uuid, other files) installed
        // while the older cut is still there takes the name over (A3-29).
        clean(); installed();
        {
            auto older = nlohmann::json::parse(saphi), newer = nlohmann::json::parse(lrg2);
            older["uuid"] = newer["uuid"] = "u-same";
            write("tracks/" + n, older.dump());
            assert(AP_Solo_RecordImportedTrack("nina", "1.0.0", newer.dump().c_str(), 0));
            assert(read("tracks/" + n) == newer.dump() && !fs::exists(base + "/solo/tracks/" + lb));
            assert(loggedFile("a newer cut of the same package was just installed"));
            // The full scan after an older client already gave the newer cut
            // its own file: the older cut's entry goes once the newer is listed.
            clean(); installed();
            write("tracks/" + n, older.dump()); write("tracks/" + lb, newer.dump());
            StorePublication recut2; recut2.assets = assets; recut2.live = full.live;
            recut2.descriptors = {entry(1, "Big Forest", older.dump(), "a"), entry(1, "Big Forest", newer.dump(), "c")};
            KeepOneCopyPerEntry(recut2);
            assert(recut2.descriptors.size() == 2 && recut2.descriptors[1].kind == 5);
            for (const auto &e : recut2.descriptors) assert(ApplyStoreDescriptor(e, recut2, message));
            assert(!fs::exists(base + "/solo/tracks/" + n) && read("tracks/" + lb) == newer.dump());
            assert(loggedFile("a newer cut of the same package is listed"));
        }

        // l) A3-20 laps moved to a replacement key whose plain name holds
        // another build's complete entry: the laps go to the keeper's own
        // build file, the other build's entry and laps stay.
        clean(); installed();
        {
            auto oldCopy = nlohmann::json::parse(descriptor("Generic", pins(c.c_str(), d.c_str()), 4));
            oldCopy["id"] = "package/generic-slot/1.0.0"; oldCopy["content_id"] = "generic-slot"; oldCopy["uuid"] = "u-lrg2";
            auto keeperWait = nlohmann::json::parse(lrg2Wait); keeperWait["uuid"] = "u-lrg2";
            write("tracks/generic-slot__1.0.0.json", oldCopy.dump());
            write("tracks/" + n, descriptor("Big Forest", pins(a.c_str(), b.c_str()), 6));
            write("needs-laps/" + n, keeperWait.dump());
            std::string why2;
            assert(AP_Solo_RetireReplacedTrack("generic-slot", "1.0.0", "u-lrg2", "nina", "1.0.0"));
            assert(nlohmann::json::parse(read("tracks/" + n))["laps"] == 6);
            assert(nlohmann::json::parse(read("tracks/" + n))["files"] == nlohmann::json::parse(pins(a.c_str(), b.c_str())));
            const auto moved = nlohmann::json::parse(read("tracks/" + lb));
            assert(moved["laps"] == 4 && moved["files"] == keeperWait["files"]);
            assert(!fs::exists(base + "/solo/needs-laps/" + n) && !fs::exists(base + "/solo/tracks/generic-slot__1.0.0.json"));
        }

        // i) The build file name: the tag of the LEV and VRM pins, whatever
        // their order, and none for pins that are not SHA-256 hashes.
        assert(AP_SoloBuildTag(nlohmann::json::parse(pins(c.c_str(), d.c_str()))) == c.substr(0, 16) + d.substr(0, 16));
        assert(AP_SoloBuildTag(nlohmann::json::parse("[{\"role\":\"vrm\",\"sha256\":\"" + d + "\",\"bytes\":2},"
                                                     "{\"role\":\"lev\",\"sha256\":\"" + c + "\",\"bytes\":1}]")) ==
               c.substr(0, 16) + d.substr(0, 16));
        assert(AP_SoloBuildTag(nlohmann::json::parse(pins("ca", "cb"))).empty());
        assert(AP_SoloBuildDescriptorFileName("big-forest", "1.0.0", "6598755fd3d9b31753bbf1926ecb8b85") == "big-forest__1.0.0@6598755fd3d9b31753bbf1926ecb8b85.json");
        assert(AP_SoloBuildDescriptorFileName("Big", "1.0.0", "6598755fd3d9b31753bbf1926ecb8b85").empty());
        assert(AP_SoloBuildDescriptorFileName("big-forest", "1.0.0", "../../../../../../../../etc/pass").empty());
        assert(AP_SoloBuildDescriptorFileName("big-forest", "1.0.0", "6598755FD3D9B31753BBF1926ECB8B85").empty());
        assert(AP_SoloBuildRowId("package/big-forest/1.0.0", "6598755fd3d9b31753bbf1926ecb8b85") == "package/big-forest/1.0.0@6598755fd3d9b31753bbf1926ecb8b85");
        assert(AP_SoloRowBaseId("package/big-forest/1.0.0@6598755fd3d9b31753bbf1926ecb8b85") == "package/big-forest/1.0.0");
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
