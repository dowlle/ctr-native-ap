"""Every install route keeps a track's own music (.sca) with its package (A2-30).

Compiles the production Project Saphi download code out of
ap/ap_custom_track_download.cpp (everything but its HTTPS transport, as
tools/test-seed-content-download-production.py does), with CTR_CUSTOM_PACKAGES
and the real package library, and drives:

  1. the Current Seed / This Seed shared-store install by registry ids
     (ap_custom_track_download_start_shared with a Saphi track id) and by
     version (a compiled profile's download API),
  2. the Track Manager install (CustomSaphi_StartInstall),
  3. the lookup the race arm step uses (CustomPackage_FindPinnedMusic).

Cases, each checked against the package store afterwards:
  a. a current .sca with an independent version is fetched and pinned in the manifest
     (role "sca"), the file is in the store with the pinned bytes, and the
     lookup returns its path, sha256 and size,
  b. no .sca listed, a historical track pair, a noncurrent .sca, two current ones, or one
     past the 1 MiB cap: the track installs without music and the .sca is
     never requested,
  c. a failed or short .sca download installs the track without music,
  d. the seed pins stay the room's: tampered LEV bytes still refuse,
  e. the Track Manager install stores the .sca it was handed,
  f. the lookup ignores a copy of the same pair installed without music and
     another revision or uuid, and prefers the package with the asked UUID,
  g. a malformed music row (null download_url, non-numeric file_size, null type) installs
     the track without music.
  h. the library repair verifies provenance, consent and downloads, preserves the
     existing package pin and track files, and supplies music to both play paths.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_custom_track_download.cpp").read_text(encoding="utf-8")
part_a = source[source.index("namespace\n{\n"):source.index("#ifdef _WIN32\nstd::wstring widen_ascii")]
start_b = source.index("bool hash_matches(")
part_b = source[start_b:source.index("#ifdef CTR_CUSTOM_PACKAGES\n// Track Manager", start_b)]
start_c = source.index("#ifdef CTR_CUSTOM_PACKAGES\n// Track Manager", start_b)
part_c = source[start_c:source.index("#endif // CTR_CUSTOM_PACKAGES", start_c) + len("#endif // CTR_CUSTOM_PACKAGES")]

prelude = r'''
#include "ap_custom_track_download.h"
#include "ap_seed_content_logic.h"
#include <platform/native_saphi_catalogue.h>
#include <platform/native_custom_package.h>
#include <platform/native_custom_music.h>
#include <platform/native_custom_offline.h>
#include <zlib.h>
#include <future>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "ap_track_consent.h"
extern "C" void CustomPackage_TestInstallFailure(int checkpoints);
// The track author permission check (A4-74): the real resolver is
// ap/ap_track_consent.cpp (tools/test-track-consent.cpp). Here it allows,
// unless a case sets g_permission, and records which Saphi track was asked.
static int g_permission = AP_TRACK_PERMISSION_ALLOW, g_permission_track = -1;
static bool g_permission_given_key;
extern "C" int ap_track_permission(int track, int author, int saphi, const char *key)
{ (void)author; (void)saphi; g_permission_given_key = key != nullptr; g_permission_track = track; return g_permission; }
extern "C" const char *ap_track_permission_text(void) { return "The author has not allowed in-game download."; }
extern "C" int ap_track_consent_saphi_author(int track) { return track == 121 ? 12 : 0; }
extern "C" int ap_track_registry_key_for_saphi(int track, char *key, int bytes)
{ if (track != 121 || !key || bytes < 16) return 0; std::snprintf(key, bytes, "acid-lake"); return 1; }
extern "C" void ap_track_consent_note_saphi(int track, int author, int saphi) { (void)track; (void)author; (void)saphi; }
extern "C" int ap_track_saphi_id_from_url(const char *url)
{
	const char *at = url ? std::strstr(url, "/tracks/") : nullptr;
	return at ? std::atoi(at + 8) : 0;
}
'''

stub = r'''
std::map<std::string, std::vector<unsigned char>> served;
std::vector<std::string> requested;
std::mutex served_mutex;
bool https_get(const std::string &path, size_t max_bytes, std::vector<unsigned char> &body, std::string &error)
{
	std::lock_guard<std::mutex> lock(served_mutex);
	requested.push_back(path);
	auto it = served.find(path);
	if (it == served.end()) { error = "Could not reach Project Saphi: stub network failure"; return false; }
	if (it->second.size() > max_bytes) { error = "too large"; return false; }
	body = it->second;
	return true;
}
'''

tests = r'''
static int failures, checks;
static void expect(bool ok, const std::string &what)
{
	checks++;
	if (!ok) { failures++; std::printf("FAIL: %s\n", what.c_str()); }
}
static std::string sha(const std::vector<unsigned char> &b)
{
	unsigned char d[32]; unsigned int n = 0;
	EVP_Digest(b.data(), b.size(), d, &n, EVP_sha256(), nullptr);
	static const char hex[] = "0123456789abcdef";
	std::string out;
	for (unsigned i = 0; i < n; i++) { out += hex[d[i] >> 4]; out += hex[d[i] & 15]; }
	return out;
}
static bool exists(const std::string &p) { struct stat st; return lstat(p.c_str(), &st) == 0; }
static std::vector<unsigned char> slurp(const std::string &p)
{
	std::ifstream f(p, std::ios::binary);
	return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
// The smallest LEV and VRM the package verifier accepts (the structural
// fixture of tools/test-custom-content-verify.c with no instances).
static std::vector<unsigned char> make_lev()
{
	std::vector<unsigned char> b(4 + 0x1200 + 4, 0);
	b[1] = 0x12; // u32 0x1200 at offset 0
	return b;
}
static std::vector<unsigned char> make_vrm()
{
	std::vector<unsigned char> b(22, 0);
	b[16] = 1; b[18] = 1;
	return b;
}
static std::vector<unsigned char> lev = make_lev(), vrm = make_vrm();
static std::vector<unsigned char> make_sca(unsigned char tone, bool truncatedSong = false)
{
	std::vector<unsigned char> bytes = {'S', 'C', 'A', 1};
	auto chunk = [&](const char *tag, const std::vector<unsigned char> &body) {
		bytes.insert(bytes.end(), tag, tag + 4);
		for (unsigned int shift = 0; shift < 32; shift += 8)
			bytes.push_back(static_cast<unsigned char>(body.size() >> shift));
		bytes.insert(bytes.end(), body.begin(), body.end());
		while (bytes.size() % 4) bytes.push_back(0);
	};
	std::vector<unsigned char> bank(CTR_SCA_SECTOR + 16, 0);
	bank[0] = 1;
	bank[2] = 5;
	bank.back() = tone;
	chunk("BANK", bank);
	std::vector<unsigned char> cseq(40, 0);
	cseq[0] = truncatedSong ? 25 : 40;
	cseq[4] = cseq[6] = 1;
	cseq[8] = 1; cseq[9] = 127; cseq[12] = 60; cseq[14] = 5;
	cseq[25] = 1; cseq[26] = 120; cseq[28] = 48;
	cseq[35] = 5; cseq[36] = 60; cseq[37] = 100;
	cseq[38] = 1; cseq[39] = 3;
	chunk("CSEQ", cseq);
	chunk("SIZE", {2, 0});
	return bytes;
}
static const auto music = make_sca(0x5a);
static const std::string UUID = "11111111-2222-5333-8444-555555555555";
static std::string seed_uuid = UUID;

struct Sca { int id; const char *version; bool current; long long size; };
static bool malformed_url, malformed_size, malformed_type;
static bool pair_current = true, wrong_crc;
static std::vector<Sca> scas;
static bool serve_sca = true;
static std::vector<unsigned char> sca_body_override;
static void serve()
{
	std::lock_guard<std::mutex> lock(served_mutex);
	served.clear(); requested.clear();
	nlohmann::json rows = nlohmann::json::array({
		{{"id", 872}, {"type", "lev"}, {"version", "1.0.0"}, {"is_current", pair_current}, {"file_size", (long long)lev.size()},
		 {"download_url", "/api/v2/tracks/121/downloads/872"}},
		{{"id", 873}, {"type", "vrm"}, {"version", "1.0.0"}, {"is_current", pair_current}, {"file_size", (long long)vrm.size()},
		 {"download_url", "/api/v2/tracks/121/downloads/873"}},
		{{"id", 700}, {"type", "lev"}, {"version", "0.9.0"}, {"is_current", false}, {"file_size", 10},
		 {"download_url", "/api/v2/tracks/121/downloads/700"}},
		{{"id", 871}, {"type", "track_banner"}, {"version", nullptr}, {"is_current", true}, {"file_size", 10},
		 {"download_url", "/api/v2/tracks/121/downloads/871"}}});
	for (const Sca &s : scas)
	{
		nlohmann::json row = {{"id", s.id}, {"type", "sca"}, {"version", s.version}, {"is_current", s.current},
		                      {"file_size", s.size}, {"download_url", "/api/v2/tracks/121/downloads/" + std::to_string(s.id)}};
		const auto &payload = sca_body_override.empty() ? music : sca_body_override;
		row["crc32_hash"] = crc32(0, payload.data(), (uInt)payload.size()) ^ (wrong_crc ? 1u : 0u);
		if (malformed_url) row["download_url"] = nullptr;
		if (malformed_size) row["file_size"] = "5000";
		if (malformed_type) row["type"] = nullptr;
		rows.push_back(row);
		if (serve_sca) served["/api/v2/tracks/121/downloads/" + std::to_string(s.id)] =
			sca_body_override.empty() ? music : sca_body_override;
	}
	const std::string text = nlohmann::json({{"data", rows}}).dump();
	served["/api/v2/tracks/121/downloads"] = std::vector<unsigned char>(text.begin(), text.end());
	served["/api/v2/tracks/121/downloads/872"] = lev;
	served["/api/v2/tracks/121/downloads/873"] = vrm;
}
static int finish(std::string &detail)
{
	char text[512];
	for (int i = 0; i < 3000; i++)
	{
		const int state = ap_custom_track_download_status(text, sizeof text);
		if (state != AP_CT_DOWNLOAD_RUNNING) { detail = text; return state; }
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	detail = "timeout";
	return -1;
}
static bool asked(const std::string &path)
{
	std::lock_guard<std::mutex> lock(served_mutex);
	return std::find(requested.begin(), requested.end(), path) != requested.end();
}
static const std::string SCA_PATH = "/api/v2/tracks/121/downloads/874";

struct Found { bool ok; std::string path, sha; unsigned long bytes; };
static Found find_music_for(const std::string &assets, const char *revision = "1.0.0",
                            const char *uuid = nullptr, int exact = 1)
{
	char path[512], digest[65];
	Found f{};
	f.ok = CustomPackage_FindPinnedMusic(assets.c_str(), revision, uuid, exact, sha(lev).c_str(), (unsigned long)lev.size(),
	                                     sha(vrm).c_str(), (unsigned long)vrm.size(), path, sizeof path, digest, &f.bytes) != 0;
	if (f.ok) { f.path = path; f.sha = digest; }
	return f;
}
// One seed install into a fresh assets folder.
static std::string fresh(const char *name)
{
	std::string dir = std::string("/tmp/ctr-music-routes-") + name + "-XXXXXX";
	std::vector<char> t(dir.begin(), dir.end()); t.push_back(0);
	if (!mkdtemp(t.data())) std::exit(2);
	return t.data();
}
static int install_seed(const std::string &assets, bool by_ids, std::string &detail)
{
	const std::string tracks = assets + "/tracks";
	mkdir(tracks.c_str(), 0755);
	const std::string pkg = tracks + "/acid-lake";
	const std::string ls = sha(lev), vs = sha(vrm);
	const int ok = by_ids
		? ap_custom_track_download_start_shared(tracks.c_str(), pkg.c_str(), nullptr, "1.0.0", seed_uuid.c_str(), "Acid Lake",
		      "Author", 3, 0, 0, 0, ls.c_str(), (uint32_t)lev.size(), vs.c_str(), (uint32_t)vrm.size(), 121, 872, 873, "acid-lake")
		: ap_custom_track_download_start_shared(tracks.c_str(), pkg.c_str(),
		      "https://www.projectsaphi.com/api/v2/tracks/121/downloads", "1.0.0", seed_uuid.c_str(), "Acid Lake",
		      "Author", 3, 0, 0, 0, ls.c_str(), (uint32_t)lev.size(), vs.c_str(), (uint32_t)vrm.size(), 0, 0, 0, "acid-lake");
	if (!ok) { detail = "start refused"; return -1; }
	return finish(detail);
}
// The sca role of the installed manifest, if any.
static bool manifest_has_sca(const std::string &assets)
{
	const std::string dir = assets + "/tracks/packages";
	DIR *d = opendir(dir.c_str());
	bool found = false;
	while (d)
	{
		dirent *e = readdir(d);
		if (!e) break;
		if (e->d_name[0] == '.') continue;
		std::ifstream f(dir + "/" + e->d_name + "/manifest.json");
		std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		if (text.find("\"role\":\"sca\"") != std::string::npos) found = true;
	}
	if (d) closedir(d);
	return found;
}

static CustomSaphiCatalogue *repair_catalogue(const char *trackVersion, const std::string &levHash,
                                             bool current = true, const char *audioVersion = "0.9.0",
                                             const std::vector<unsigned char> &audio = music)
{
	auto media = [current](int id, const char *type, const char *version,
	                const std::vector<unsigned char> &bytes, const std::string &hash) {
		return nlohmann::json{{"id", id}, {"type", type}, {"version", version},
		    {"is_current", current || !std::strcmp(type, "sca")},
		    {"file_size", bytes.size()}, {"crc32_hash", crc32(0, bytes.data(), (uInt)bytes.size())},
		    {"sha256_hash", hash}, {"download_url", "/api/v3/tracks/121/downloads/" + std::to_string(id)},
		    {"modes", nlohmann::json::array({"arcade"})}};
	};
	const nlohmann::json track = {{"id", 121}, {"author_id", 12}, {"author", "Author"},
	    {"name", "Acid Lake"}, {"type", "race"}, {"is_active", true},
	    {"downloads", nlohmann::json::array({
	        media(872, "lev", trackVersion, lev, levHash), media(873, "vrm", trackVersion, vrm, sha(vrm)),
	        media(874, "sca", audioVersion, audio, sha(audio))})}};
	const auto json = nlohmann::json({{"data", nlohmann::json::array({track})}}).dump();
	CustomSaphiCatalogue *out = nullptr;
	char error[256];
	expect(CustomSaphi_ParseCatalogue(json.data(), json.size(), &out, error, sizeof error), "repair catalogue parsed");
	return out;
}

static int repair_library(CustomSaphiCatalogue *catalogue, const std::string &assets)
{
	if (!CustomSaphi_StartMusicRepair(catalogue, assets.c_str())) return -1;
	int repaired = -1, status = 0;
	for (int i = 0; i < 3000 && (status = CustomSaphi_PollMusicRepair(&repaired)) == 0; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	return status == 1 ? repaired : -1;
}

int main()
{
	std::string detail;
	for (int by_ids = 1; by_ids >= 0; by_ids--)
	{
		const std::string route = by_ids ? "registry ids: " : "version: ";
		// a. The current .sca has its own version and is stored with the package.
		scas = {{874, "0.9.0", true, (long long)music.size()}};
		serve_sca = true;
		serve();
		std::string assets = fresh("a");
		expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_SUCCEEDED, route + "clean install succeeds: " + detail);
		expect(asked(SCA_PATH), route + "the .sca was fetched");
		expect(manifest_has_sca(assets), route + "the manifest pins role sca");
		Found f = find_music_for(assets);
		expect(f.ok && f.sha == sha(music) && f.bytes == music.size(), route + "the lookup returns the pinned sha and size");
		expect(f.ok && slurp(f.path) == music, route + "the stored file is the downloaded .sca");
		expect(f.ok && f.path.find("/original/sca-874.sca") != std::string::npos, route + "stored as original/sca-<id>.sca");
		expect(find_music_for(assets, "2.0.0").ok == false, route + "another revision finds none");
		expect(find_music_for(assets, "1.0.0", "99999999-2222-5333-8444-555555555555").ok == false, route + "another uuid finds none");
		// A4-74: a Current Seed install records the Saphi track and author
		// ids it was matched on; the store scan reads them back.
		{
			struct Seen { int author = 0, track = 0; std::string key; } seen;
			char scanError[256] = {};
			CustomPackage_ScanStore((assets + "/tracks/packages").c_str(), [](const CustomPackageStoreEntry *e, void *c) {
				auto *s = static_cast<Seen *>(c);
				s->author = e->local.authorID; s->track = e->local.permissionTrackID; s->key = e->local.trackKey;
			}, &seen, scanError, sizeof scanError);
			expect(seen.track == 121 && seen.author == 12 && seen.key == "acid-lake",
			       route + "seed-source.json gives the permission check the Saphi ids and the track key");
		}

		// b. Listings without one eligible current .sca.
		struct Case { const char *name; std::vector<Sca> list; };
		const Case cases[] = {
			{"no .sca listed", {}},
			{"not current", {{874, "1.0.0", false, 5000}}},
			{"two current", {{874, "1.0.0", true, 5000}, {875, "1.0.0", true, 5000}}},
			{"valid and malformed current", {{874, "1.0.0", true, 5000}, {875, "1.0.0", true, 1024 * 1024 + 1}}},
			{"past the 1 MiB cap", {{874, "1.0.0", true, 1024 * 1024 + 1}}},
		};
		for (const Case &c : cases)
		{
			scas = c.list; serve_sca = true; serve();
			assets = fresh("b");
			expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_SUCCEEDED, route + c.name + ": the track installs: " + detail);
			expect(!asked(SCA_PATH) && !asked("/api/v2/tracks/121/downloads/875"), route + c.name + ": no .sca requested");
			expect(!manifest_has_sca(assets) && !find_music_for(assets).ok, route + c.name + ": installed without music");
		}
		pair_current = false;
		scas = {{874, "0.9.0", true, (long long)music.size()}};
		serve();
		assets = fresh("historical");
		expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_SUCCEEDED &&
		       !asked(SCA_PATH) && !find_music_for(assets).ok, route + "historical pair does not take current music");
		scas = {{874, "1.0.0", true, (long long)music.size()}};
		serve();
		assets = fresh("historical-exact");
		expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_SUCCEEDED &&
		       asked(SCA_PATH) && find_music_for(assets).ok, route + "historical pair retains exact-version music");
		pair_current = true;

		// b2. A malformed music row costs the music only, never the install.
		for (int kind = 0; kind < 3; kind++)
		{
			scas = {{874, "1.0.0", true, (long long)music.size()}};
			malformed_url = kind == 0; malformed_size = kind == 1; malformed_type = kind == 2;
			serve_sca = true; serve();
			assets = fresh("b2");
			expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_SUCCEEDED,
			       route + (kind == 2 ? "null type" : kind ? "non-numeric file_size" : "null download_url") + ": the track installs: " + detail);
			expect(!asked(SCA_PATH) && !manifest_has_sca(assets) && !find_music_for(assets).ok,
			       route + (kind == 2 ? "null type" : kind ? "non-numeric file_size" : "null download_url") + ": installed without music");
			malformed_url = malformed_size = malformed_type = false;
		}

		// c. A failed or short download costs the music only.
		scas = {{874, "1.0.0", true, (long long)music.size()}};
		serve_sca = false; serve();
		assets = fresh("c");
		expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_SUCCEEDED, route + "failed .sca download still installs: " + detail);
		expect(asked(SCA_PATH) && !manifest_has_sca(assets) && !find_music_for(assets).ok, route + "failed .sca: no music");
		serve_sca = true; sca_body_override.assign(100, 1); serve();
		assets = fresh("c");
		expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_SUCCEEDED, route + "short .sca still installs: " + detail);
		expect(!manifest_has_sca(assets) && !find_music_for(assets).ok, route + "short .sca: no music");
		sca_body_override.clear();
		wrong_crc = true; serve();
		assets = fresh("wrong-crc");
		expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_SUCCEEDED &&
		       asked(SCA_PATH) && !find_music_for(assets).ok, route + "a same-size CRC mismatch costs only the music");
		wrong_crc = false;
		sca_body_override = std::vector<unsigned char>(music.size(), 0x5a);
		serve();
		assets = fresh("bad-container");
		expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_SUCCEEDED &&
		       asked(SCA_PATH) && !find_music_for(assets).ok, route + "a checksum-valid broken SCA costs only the music");
		sca_body_override.clear();

		// d. A tampered LEV still refuses, whatever the music says.
		serve();
		{
			std::lock_guard<std::mutex> lock(served_mutex);
			served["/api/v2/tracks/121/downloads/872"][100] ^= 1;
		}
		assets = fresh("d");
		expect(install_seed(assets, by_ids, detail) == AP_CT_DOWNLOAD_FAILED && !exists(assets + "/tracks/packages"),
		       route + "a tampered LEV refuses, nothing installed");
	}

	// h. Restore optional music into the already installed seed snapshot.
	{
		const auto assets = fresh("repair");
		const auto store = assets + "/tracks/packages";
		scas.clear(); serve();
		expect(install_seed(assets, true, detail) == AP_CT_DOWNLOAD_SUCCEEDED, "preexisting seed install has no SCA");
		std::string pin;
		CustomPackage_ScanStore(store.c_str(), [](const CustomPackageStoreEntry *entry, void *out) {
			if (entry->verified) *static_cast<std::string *>(out) = entry->pin;
		}, &pin, nullptr, 0);
		expect(pin.size() == 64, "preexisting package pin recorded");
		const auto directory = store + "/" + pin;
		const auto manifest = slurp(directory + "/manifest.json");
		const auto localLev = slurp(directory + "/original/track.lev");
		const auto localVrm = slurp(directory + "/original/track.vrm");
		const auto settings = slurp(directory + "/metadata/race-settings.json");

		const std::string path = "/api/v3/tracks/121/downloads/874";
		auto catalogue = repair_catalogue("1.0.0", sha(lev));
		auto wrongTrack = repair_catalogue("1.0.0", sha(vrm));
		served[path] = music;
		expect(repair_library(wrongTrack, assets) == 0 && !asked(path), "a different LEV pin cannot trigger repair");
		CustomSaphi_Free(&wrongTrack);
		auto wrongVersion = repair_catalogue("1.1.0", sha(lev));
		expect(repair_library(wrongVersion, assets) == 0 && !asked(path), "a different track version cannot trigger repair");
		CustomSaphi_Free(&wrongVersion);
		for (int answer : {AP_TRACK_PERMISSION_UNKNOWN, AP_TRACK_PERMISSION_DENY})
		{
			g_permission = answer;
			g_permission_given_key = true;
			expect(repair_library(catalogue, assets) == 0 && !asked(path) && !g_permission_given_key,
			       "repair honors source permission without supplying the installed package's key");
		}
		g_permission = AP_TRACK_PERMISSION_ALLOW;
		served.erase(path);
		expect(repair_library(catalogue, assets) == 0 && asked(path) && !find_music_for(assets).ok,
		       "network failure leaves the original snapshot usable");
		served[path] = music;
		served[path][0] ^= 1;
		expect(repair_library(catalogue, assets) == 0 && !find_music_for(assets).ok, "corrupt audio cannot publish");
		const auto malformedSong = make_sca(0x5a, true);
		auto badSongCatalogue = repair_catalogue("1.0.0", sha(lev), true, "0.9.0", malformedSong);
		served[path] = malformedSong;
		expect(repair_library(badSongCatalogue, assets) == 0 && !find_music_for(assets).ok,
		       "checksum-valid audio with a truncated song header cannot publish");
		CustomSaphi_Free(&badSongCatalogue);

		CustomPackage_TestInstallFailure(1);
		expect(!CustomPackage_InstallMusic(directory.c_str(), pin.c_str(), music.data(), music.size(), sha(music).c_str(), 874),
		       "interruption between audio and metadata leaves no publication");
		CustomPackage_TestInstallFailure(-1);
		expect(!find_music_for(assets).ok && !exists(directory + "/.saphi-music/source.json"),
		       "the race lookup ignores interrupted staging");
		serve();
		served[path] = music;
		expect(repair_library(catalogue, assets) == 1 && asked(path), "repair retries and downloads only the SCA");
		expect(!asked("/api/v3/tracks/121/downloads/872") && !asked("/api/v3/tracks/121/downloads/873"),
		       "repair does not download either track file");
		const auto audio = find_music_for(assets, "1.0.0", UUID.c_str());
		expect(audio.ok && audio.sha == sha(music) && slurp(audio.path) == music, "seed lookup finds verified repaired audio");
		expect(slurp(directory + "/manifest.json") == manifest &&
		       slurp(directory + "/original/track.lev") == localLev && localLev == lev &&
		       slurp(directory + "/original/track.vrm") == localVrm && localVrm == vrm &&
		       slurp(directory + "/metadata/race-settings.json") == settings && !manifest_has_sca(assets),
		       "repair keeps original manifest, pinned geometry and settings byte-identical");
		char levPath[1024], vrmPath[1024];
		expect(CustomPackage_FindPinnedFiles(assets.c_str(), "1.0.0", UUID.c_str(), sha(lev).c_str(), lev.size(),
		      sha(vrm).c_str(), vrm.size(), levPath, sizeof levPath, vrmPath, sizeof vrmPath) &&
		      levPath == directory + "/original/track.lev" && vrmPath == directory + "/original/track.vrm",
		      "same seed identity still resolves to the original files");
		CustomPackageOwned *owned = nullptr;
		expect(CustomPackage_AcquireDirectory(directory.c_str(), pin.c_str(), &owned, nullptr, 0), "old package pin still verifies");
		const void *offline = nullptr; size_t size = 0;
		CustomMusicSca parsed{};
		uint16_t spuTable[32 * 2] = {};
		expect(CustomPackage_GetAudio(owned, &offline, &size) && size == music.size() &&
		       !std::memcmp(offline, music.data(), size) &&
		       CustomMusic_Parse(offline, size, 32, &parsed, nullptr, 0) &&
		       CustomMusic_Admit(&parsed, spuTable, 32, 0, nullptr, 0, nullptr, nullptr, 0),
		       "the offline snapshot borrows valid SCA that passes playback admission");
		CustomPackage_Free(&owned);
		serve();
		expect(repair_library(catalogue, assets) == 0 && !asked(path), "repeat repair sends no request");
		if (audio.ok)
		{
			std::ofstream broken(audio.path, std::ios::binary | std::ios::trunc);
			broken.write("bad", 3);
		}
		expect(!find_music_for(assets).ok, "seed lookup refuses damaged restored audio");
		expect(CustomPackage_AcquireDirectory(directory.c_str(), pin.c_str(), &owned, nullptr, 0) &&
		       !CustomPackage_GetAudio(owned, &offline, &size), "damaged restored audio leaves the original package usable");
		CustomPackage_Free(&owned);
		serve();
		served[path] = music;
		expect(repair_library(catalogue, assets) == 1 && asked(path) && find_music_for(assets).ok,
		       "refresh replaces damaged audio using the original pin");
		const auto sourcePath = directory + "/.saphi-music/source.json";
		{
			auto source = nlohmann::json::parse(slurp(sourcePath));
			source["media_id"] = 875;
			std::ofstream damaged(sourcePath, std::ios::trunc);
			damaged << source.dump();
		}
		serve();
		expect(repair_library(catalogue, assets) == 1 && !asked(path) &&
		       nlohmann::json::parse(slurp(sourcePath)).at("media_id") == 874,
		       "repair reconciles a damaged ID without fetching already verified audio");
		CustomSaphi_Free(&catalogue);
		{
			std::ofstream broken(sourcePath, std::ios::trunc);
			broken << "bad";
		}
		const auto competingMusic = make_sca(0x77);
		auto publish = [&](const std::vector<unsigned char> &bytes, int id) {
			return CustomPackage_InstallMusic(directory.c_str(), pin.c_str(), bytes.data(), bytes.size(), sha(bytes).c_str(), id);
		};
		auto first = std::async(std::launch::async, [&] { return publish(music, 874); });
		auto second = std::async(std::launch::async, [&] { return publish(competingMusic, 875); });
		const int firstPublished = first.get(), secondPublished = second.get();
		const auto &winner = firstPublished ? music : competingMusic;
		expect(firstPublished + secondPublished == 1 && find_music_for(assets).sha == sha(winner),
		       "two writers with different audio preserve the first verified publication");
		DIR *sidecar = opendir((directory + "/.saphi-music").c_str());
		int pending = 0;
		if (sidecar)
		{
			while (dirent *file = readdir(sidecar))
				if (!std::strncmp(file->d_name, ".pending-", 9)) pending++;
			closedir(sidecar);
		}
		expect(sidecar && !pending && slurp(directory + "/manifest.json") == manifest,
		       "concurrent repair leaves the original pin and releases staging files");
		expect(CustomPackage_AcquireDirectory(directory.c_str(), pin.c_str(), &owned, nullptr, 0) &&
		       CustomPackage_GetAudio(owned, &offline, &size) && size == winner.size() &&
		       !std::memcmp(offline, winner.data(), size), "offline audio also recovers under the original pin");
		CustomPackage_Free(&owned);
		const auto pinnedMusic = make_sca(0x66);
		scas = {{874, "0.9.0", true, (long long)pinnedMusic.size()}};
		sca_body_override = pinnedMusic;
		serve();
		seed_uuid = "bbbbbbbb-2222-5333-8444-555555555555";
		expect(install_seed(assets, true, detail) == AP_CT_DOWNLOAD_SUCCEEDED, "an alternate UUID pins its own audio");
		expect(find_music_for(assets, "1.0.0", UUID.c_str(), 0).sha == sha(pinnedMusic),
		       "manifest-pinned audio on the same pair wins even against a restored exact UUID");
		seed_uuid = UUID;
		expect(install_seed(assets, true, detail) == AP_CT_DOWNLOAD_SUCCEEDED, "a sibling package pins its own audio");
		const auto pinned = find_music_for(assets, "1.0.0", UUID.c_str());
		expect(pinned.ok && pinned.sha == sha(pinnedMusic) && slurp(pinned.path) == pinnedMusic,
		       "the seed prefers manifest-pinned audio over a local repair for the same UUID");
		sca_body_override.clear();
	}

	{
		const auto assets = fresh("repair-historical");
		scas.clear(); serve();
		expect(install_seed(assets, true, detail) == AP_CT_DOWNLOAD_SUCCEEDED, "historical repair starts with a silent package");
		const std::string path = "/api/v3/tracks/121/downloads/874";
		served[path] = music;
		auto different = repair_catalogue("1.0.0", sha(lev), false);
		expect(repair_library(different, assets) == -1 && !asked(path), "historical repair skips another audio version");
		CustomSaphi_Free(&different);
		auto exact = repair_catalogue("1.0.0", sha(lev), false, "1.0.0");
		expect(repair_library(exact, assets) == 1 && asked(path) && find_music_for(assets).ok,
		       "historical repair restores exact-version audio");
		CustomSaphi_Free(&exact);
	}

	// f. A copy of the pair without music does not hide the copy with music.
	{
		scas = {}; serve();
		std::string assets = fresh("f");
		expect(install_seed(assets, true, detail) == AP_CT_DOWNLOAD_SUCCEEDED, "the pair installs without music first: " + detail);
		scas = {{874, "1.0.0", true, (long long)music.size()}}; serve();
		expect(install_seed(assets, true, detail) == AP_CT_DOWNLOAD_SUCCEEDED, "the same pair installs again with music: " + detail);
		Found f = find_music_for(assets);
		expect(f.ok && f.sha == sha(music), "the lookup finds the copy that pins music");
	}

	// g. Two packages of the same pair under different UUIDs: the lookup
	// prefers the one asked for and falls back to any.
	{
		const std::string assets = fresh("g");
		const auto other = make_sca(0x33);
		scas = {{874, "1.0.0", true, (long long)music.size()}}; sca_body_override.clear(); serve();
		seed_uuid = "aaaaaaaa-2222-5333-8444-555555555555";
		expect(install_seed(assets, true, detail) == AP_CT_DOWNLOAD_SUCCEEDED, "uuid A installs: " + detail);
		scas = {{874, "1.0.0", true, (long long)other.size()}}; sca_body_override = other; serve();
		seed_uuid = "bbbbbbbb-2222-5333-8444-555555555555";
		expect(install_seed(assets, true, detail) == AP_CT_DOWNLOAD_SUCCEEDED, "uuid B installs: " + detail);
		Found a = find_music_for(assets, "1.0.0", "aaaaaaaa-2222-5333-8444-555555555555");
		Found b = find_music_for(assets, "1.0.0", "bbbbbbbb-2222-5333-8444-555555555555");
		expect(a.ok && a.sha == sha(music) && b.ok && b.sha == sha(other), "each UUID finds its own music");
		expect(find_music_for(assets).ok, "no UUID finds one of them");
		seed_uuid = UUID; sca_body_override.clear();
	}

	// e. Track Manager install of a catalogue revision with a .sca.
	{
		scas = {{874, "1.0.0", true, (long long)music.size()}}; serve();
		const std::string assets = fresh("e");
		CustomSaphiRevision rev{};
		rev.trackID = 121;
		std::snprintf(rev.title, sizeof rev.title, "Acid Lake");
		std::snprintf(rev.author, sizeof rev.author, "Author");
		std::snprintf(rev.version, sizeof rev.version, "1.0.0");
		auto media = [](CustomSaphiMedia &m, int id, const std::vector<unsigned char> &b) {
			m.id = id; m.bytes = (unsigned)b.size(); m.crc32 = (uint32_t)crc32(0, b.data(), (uInt)b.size());
			std::snprintf(m.path, sizeof m.path, "/api/v3/tracks/121/downloads/%d", id);
		};
		media(rev.lev, 872, lev); media(rev.vrm, 873, vrm); media(rev.sca, 874, music);
		rev.current = 1;
		{
			std::lock_guard<std::mutex> lock(served_mutex);
			served["/api/v3/tracks/121/downloads/872"] = lev;
			served["/api/v3/tracks/121/downloads/873"] = vrm;
			served["/api/v3/tracks/121/downloads/874"] = music;
		}
		rev.authorID = 12;
		// A4-74: a revision whose author has not said yes never starts.
		for (int answer : {AP_TRACK_PERMISSION_UNKNOWN, AP_TRACK_PERMISSION_DENY})
		{
			g_permission = answer; g_permission_track = -1;
			expect(CustomSaphi_StartInstall(&rev, assets.c_str()) == 0 && g_permission_track == 121,
			       "Track Manager install refused when the author has not allowed in-game download");
		}
		g_permission = AP_TRACK_PERMISSION_ALLOW;
		expect(CustomSaphi_StartInstall(&rev, assets.c_str()) == 1, "Track Manager install starts");
		int state = 0; char pin[65] = {}, error[256] = {};
		for (int i = 0; i < 3000 && (state = CustomSaphi_PollInstall(pin, error, sizeof error)) == 0; i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		expect(state == 1, std::string("Track Manager install succeeds: ") + error);
		expect(manifest_has_sca(assets), "Track Manager: the manifest pins role sca");
		expect(slurp(assets + "/tracks/packages/" + pin + "/original/sca-874.sca") == music, "Track Manager: the .sca is in the store");
		// A4-74: saphi-source.json records the author id and the registry
		// track key, and the store scan reads them back for the permission check.
		{
			auto source = nlohmann::json::parse(slurp(assets + "/tracks/packages/" + pin + "/metadata/saphi-source.json"));
			expect(source.value("author_id", 0) == 12 && source.value("track_key", std::string()) == "acid-lake",
			       "saphi-source.json records author_id and track_key: " + source.dump());
			struct Seen { int author = 0, track = 0; std::string key; } seen;
			char scanError[256] = {};
			CustomPackage_ScanStore((assets + "/tracks/packages").c_str(), [](const CustomPackageStoreEntry *e, void *c) {
				auto *s = static_cast<Seen *>(c);
				s->author = e->local.authorID; s->track = e->local.permissionTrackID; s->key = e->local.trackKey;
			}, &seen, scanError, sizeof scanError);
			expect(seen.author == 12 && seen.track == 121 && seen.key == "acid-lake",
			       "the store scan reads the author id, the Saphi track id and the track key back");
		}
	}

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
'''

units = ["platform/native_custom_package.cpp", "platform/native_custom_package_files.cpp",
         "platform/native_saphi_catalogue.cpp"]
c_units = ["platform/native_custom_package_owned.c", "platform/native_custom_content_verify.c",
           "platform/native_custom_offline.c", "platform/native_custom_music.c", "platform/native_custom_unicode.c",
           "platform/native_custom_track_manager.c", "vendor/utf8proc/utf8proc.c"]

def run_harness(body=tests, extra=""):
    with tempfile.TemporaryDirectory() as tmp:
        build_harness(Path(tmp), body, extra)


def build_harness(work, body, extra):
    src, exe = work / "music_routes.cpp", work / "music_routes"
    src.write_text(prelude + part_a + stub + part_b + part_c + extra + body, encoding="utf-8")
    includes = ["-I" + str(root / "ap"), "-I" + str(root / "include"), "-I" + str(root),
                "-I" + str(root / "ap/vendor/json/include"), "-I" + str(root / "vendor/utf8proc")]
    defines = ["-DCTR_CUSTOM_TRACKS", "-DCTR_CUSTOM_PACKAGES", "-DCTR_PACKAGE_TESTING", "-DUTF8PROC_STATIC"]
    objects = []
    for unit in [src] + [root / u for u in units]:
        obj = work / (unit.stem + ".o")
        subprocess.run(["g++", "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                        *defines, *includes, "-c", str(unit), "-o", str(obj)], check=True, cwd=root)
        objects.append(str(obj))
    for unit in c_units:
        obj = work / (Path(unit).stem + ".o")
        subprocess.run(["gcc", "-m32", "-w", *defines, *includes, "-c", str(root / unit), "-o", str(obj)],
                       check=True, cwd=root)
        objects.append(str(obj))
    subprocess.run(["g++", "-m32", *objects, "-o", str(exe), "-lm", "-lcrypto", "-lz", "-pthread"], check=True, cwd=root)
    subprocess.run([str(exe)], check=True, cwd=root)


if __name__ == "__main__":
    run_harness()
