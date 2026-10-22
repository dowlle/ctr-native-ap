"""Current Seed downloads refuse before any state change (#133 PR 7).

Compiles the production Project Saphi download worker out of
ap/ap_custom_track_download.cpp (everything but its HTTPS transport) with a
stub https_get that serves canned metadata and files, then drives
ap_custom_track_download_start_pinned against a temporary assets folder.

Cases, each checked against the file system afterwards:
  1. a package root outside tracks/<one folder> is refused before any request,
  2. Saphi listing a size other than the room's pin refuses before the files
     are fetched, and creates no folder,
  3. a network failure on either file, a hash mismatch, and bytes that differ
     from the pin all refuse with no folder created,
  4. an existing different track.lev is never replaced: refused, the file and
     the folder are unchanged, no .part file is left,
  5. an existing identical track.lev is kept and only the VRM is written,
  6. a clean install creates the folder only after verification, writes the
     pinned bytes and leaves no .part file,
  7. the legacy unpinned download keeps its behaviour (no size pin, replaces
     track.lev in the folder the caller prepared).
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_custom_track_download.cpp").read_text(encoding="utf-8")
part_a = source[source.index("namespace\n{\n"):source.index("#ifdef _WIN32\nstd::wstring widen_ascii")]
start_b = source.index("bool hash_matches(")
part_b = source[start_b:source.index("#ifdef CTR_CUSTOM_PACKAGES", start_b)]

prelude = r'''
#include "ap_custom_track_download.h"
#include "ap_seed_content_logic.h"
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <unistd.h>
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
'''

stub = r'''
// The transport is the only stub: path -> body, or a failure.
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
static bool exists(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0; }
static std::vector<unsigned char> slurp(const std::string &p)
{
	std::ifstream f(p, std::ios::binary);
	return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void spit(const std::string &p, const std::vector<unsigned char> &b)
{
	std::ofstream f(p, std::ios::binary); f.write((const char *)b.data(), (std::streamsize)b.size());
}
static const char *api = "https://www.projectsaphi.com/api/v2/tracks/101/downloads";
static std::vector<unsigned char> lev(3000, 0x4c), vrm(1200, 0x56);
static void serve(long long lev_listed, long long vrm_listed, bool lev_up = true, bool vrm_up = true,
                  const std::vector<unsigned char> *lev_body = nullptr)
{
	std::lock_guard<std::mutex> lock(served_mutex);
	served.clear(); requested.clear();
	nlohmann::json meta = {{"data", nlohmann::json::array({
		{{"type", "lev"}, {"version", "1.0.2"}, {"download_url", "/api/v2/tracks/101/downloads/1"}, {"file_size", lev_listed}},
		{{"type", "vrm"}, {"version", "1.0.2"}, {"download_url", "/api/v2/tracks/101/downloads/2"}, {"file_size", vrm_listed}}})}};
	const std::string text = meta.dump();
	served["/api/v2/tracks/101/downloads"] = std::vector<unsigned char>(text.begin(), text.end());
	if (lev_up) served["/api/v2/tracks/101/downloads/1"] = lev_body ? *lev_body : lev;
	if (vrm_up) served["/api/v2/tracks/101/downloads/2"] = vrm;
}
static int finish(std::string &detail)
{
	char text[512];
	int state;
	for (int i = 0; i < 2000; i++)
	{
		state = ap_custom_track_download_status(text, sizeof text);
		if (state != AP_CT_DOWNLOAD_RUNNING) { detail = text; return state; }
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	detail = "timeout";
	return -1;
}
static int requests()
{
	std::lock_guard<std::mutex> lock(served_mutex);
	return (int)requested.size();
}
int main()
{
	char tmpl[] = "/tmp/ctr-seed-download-XXXXXX";
	if (!mkdtemp(tmpl)) return 2;
	const std::string assets = tmpl, tracks = assets + "/tracks", pkg = tracks + "/baby-t-park-1.0.2";
	const std::string orig = pkg + "/original", tlev = orig + "/track.lev", tvrm = orig + "/track.vrm";
	mkdir(tracks.c_str(), 0755);
	const std::string ls = sha(lev), vs = sha(vrm);
	const uint32_t lb = (uint32_t)lev.size(), vb = (uint32_t)vrm.size();
	std::string detail;

	// 1. Unsafe roots.
	serve(lb, vb);
	expect(!ap_custom_track_download_start_pinned(tracks.c_str(), (tracks + "/../escape").c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb),
	       "a traversal root is refused at start");
	expect(!ap_custom_track_download_start_pinned(tracks.c_str(), (tracks + "/a/b").c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb),
	       "a nested root is refused at start");
	expect(!ap_custom_track_download_start_pinned(tracks.c_str(), tracks.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb),
	       "tracks itself is refused at start");
	expect(!ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), 0, vs.c_str(), vb),
	       "a zero size pin is refused at start");
	expect(!ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), "https://example.com/x", "1.0.2", ls.c_str(), lb, vs.c_str(), vb) ||
	       (finish(detail) == AP_CT_DOWNLOAD_FAILED && requests() == 0), "a foreign API host never sends a request");
	expect(requests() == 0 && !exists(pkg), "no request and no folder after the start refusals");

	// 2. Listed size disagrees with the pin.
	serve(lb + 1, vb);
	expect(ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb), "start");
	expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && detail.find("file size") != std::string::npos, "listed size mismatch refuses: " + detail);
	expect(requests() == 1 && !exists(pkg), "only the metadata was fetched; no folder");

	// 3. Network failures, hash and size mismatches.
	serve(lb, vb, false, true);
	ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb);
	expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && !exists(pkg), "LEV network failure: no folder");
	serve(lb, vb, true, false);
	ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb);
	expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && !exists(pkg), "VRM network failure after a good LEV: no folder");
	{
		std::vector<unsigned char> other = lev; other[10] ^= 1;
		serve(lb, vb, true, true, &other);
		ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb);
		expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && detail.find("does not match") != std::string::npos && !exists(pkg),
		       "LEV hash mismatch: no folder");
		std::vector<unsigned char> shorter(lev.begin(), lev.end() - 1);
		serve(lb, vb, true, true, &shorter);
		ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb);
		expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && !exists(pkg), "a short LEV: no folder");
		// Saphi agrees with itself but not with the room: listed and served size both differ from the pin.
		serve((long long)shorter.size(), vb, true, true, &shorter);
		ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", sha(shorter).c_str(), lb, vs.c_str(), vb);
		expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && !exists(pkg), "listed and served size both off the pin: no folder");
	}

	// 4. An existing different track.lev is never replaced.
	mkdir(pkg.c_str(), 0755); mkdir(orig.c_str(), 0755);
	const std::vector<unsigned char> player(77, 0x01);
	spit(tlev, player);
	serve(lb, vb);
	ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb);
	expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && detail.find("Move it out") != std::string::npos, "different track.lev refuses: " + detail);
	expect(slurp(tlev) == player && !exists(tvrm) && !exists(orig + "/.saphi-track.lev.part") && !exists(orig + "/.saphi-track.vrm.part"),
	       "the player's file is unchanged, nothing else written");

	// 5. An existing identical track.lev is kept; only the VRM is written.
	spit(tlev, lev);
	struct stat before; stat(tlev.c_str(), &before);
	serve(lb, vb);
	ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb);
	expect(finish(detail) == AP_CT_DOWNLOAD_SUCCEEDED, "identical track.lev: succeeds " + detail);
	struct stat after; stat(tlev.c_str(), &after);
	expect(slurp(tlev) == lev && slurp(tvrm) == vrm && before.st_ino == after.st_ino, "LEV kept in place, VRM written");
	unlink(tlev.c_str()); unlink(tvrm.c_str()); rmdir(orig.c_str()); rmdir(pkg.c_str());

	// 6. A clean install.
	serve(lb, vb);
	ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb);
	expect(finish(detail) == AP_CT_DOWNLOAD_SUCCEEDED && requests() == 3, "clean install succeeds with three requests");
	expect(slurp(tlev) == lev && slurp(tvrm) == vrm && !exists(orig + "/.saphi-track.lev.part") && !exists(orig + "/.saphi-track.vrm.part"),
	       "pinned bytes in place, no .part left");
	expect(!ap_custom_track_download_start_pinned(tracks.c_str(), pkg.c_str(), api, "1.0.2", ls.c_str(), lb, vs.c_str(), vb) ||
	       finish(detail) == AP_CT_DOWNLOAD_SUCCEEDED, "a second install over the same files keeps them");
	expect(slurp(tlev) == lev && slurp(tvrm) == vrm, "files unchanged by the repeat");

	// 7. The legacy download is unchanged: no pin, replaces track.lev.
	spit(tlev, player);
	serve(lb + 5, vb);  // no pin: the listed size only has to match what Saphi serves
	{
		std::vector<unsigned char> served_lev(lev.begin(), lev.end()); served_lev.resize(lb + 5, 0x4c);
		serve((long long)served_lev.size(), vb, true, true, &served_lev);
		ap_custom_track_download_start(pkg.c_str(), api, "1.0.2", sha(served_lev).c_str(), vs.c_str());
		expect(finish(detail) == AP_CT_DOWNLOAD_SUCCEEDED && slurp(tlev) == served_lev, "legacy download replaces track.lev as before");
	}
	unlink(tlev.c_str()); unlink(tvrm.c_str()); rmdir(orig.c_str()); rmdir(pkg.c_str()); rmdir(tracks.c_str()); rmdir(assets.c_str());
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "seed_download.cpp", Path(tmp) / "seed_download"
    src.write_text(prelude + part_a + stub + part_b + tests, encoding="utf-8")
    subprocess.run(["g++", "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-I" + str(root / "ap"), "-I" + str(root / "ap/vendor/json/include"),
                    str(src), "-o", str(exe), "-lcrypto", "-pthread"], check=True, cwd=root)
    subprocess.run([str(exe)], check=True, cwd=root)
print("PASS: Current Seed download: unsafe roots, size pins, network failures, hash mismatches and an existing "
      "different file all refuse with no folder or file changed; identical files kept; legacy download unchanged")
