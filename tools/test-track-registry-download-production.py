"""Current Seed registry downloads install only the room's pinned bytes (row 10k).

Compiles the production Project Saphi download worker out of
ap/ap_custom_track_download.cpp (everything but its HTTPS transport, as
tools/test-seed-content-download-production.py does) with a stub https_get,
then drives ap_custom_track_download_start_registry, the call Current Seed
makes for a package the live track registry located on Saphi.

Cases, each checked against the file system afterwards:
  1. bad registry answers are refused at start: no track id, no or equal
     download ids, an unsafe package root,
  2. a clean install fetches only Saphi's listing for the named track and the
     two named download ids, and writes the pinned bytes to
     tracks/<key>/original,
  3. a tampered LEV or VRM (same size, other bytes) refuses with no folder,
  4. a download id Saphi lists under another track, or does not list, or
     lists twice, refuses before any file is fetched,
  5. Saphi listing another size than the room pins refuses before any file,
  6. the transport allow-list takes Project Saphi and GitHub raw only, and the
     HTTP decoder refuses a body past its cap,
  7. a failed transfer keeps its HTTP status and Retry-After, names the status
     in its message, and only a 429, 502-504 or a cut-off 200 is retried, after
     the server's Retry-After (A2-20: Saphi rate-limits rapid downloads).
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_custom_track_download.cpp").read_text(encoding="utf-8")
part_a = source[source.index("namespace\n{\n"):source.index("#ifdef _WIN32\nstd::wstring widen_ascii")]
start_b = source.index("bool hash_matches(")
part_b = source[start_b:source.index("#ifdef CTR_CUSTOM_PACKAGES\n// Track Manager", start_b)]

prelude = r'''
#include "ap_custom_track_download.h"
#include "ap_seed_content_logic.h"
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
static std::vector<unsigned char> lev(5000, 0x4c), vrm(1500, 0x56);
// Saphi's listing for track 121: files 872 (LEV) and 873 (VRM), and an older
// pair the registry did not name.
static void serve(long long lev_listed, const std::vector<unsigned char> *lev_body = nullptr,
                  const std::vector<unsigned char> *vrm_body = nullptr, const char *lev_path = nullptr,
                  bool twice = false)
{
	std::lock_guard<std::mutex> lock(served_mutex);
	served.clear(); requested.clear();
	nlohmann::json rows = nlohmann::json::array({
		{{"id", 872}, {"type", "lev"}, {"version", "1.0.0"}, {"file_size", lev_listed},
		 {"download_url", lev_path ? lev_path : "/api/v2/tracks/121/downloads/872"}},
		{{"id", 873}, {"type", "vrm"}, {"version", "1.0.0"}, {"file_size", (long long)vrm.size()},
		 {"download_url", "/api/v2/tracks/121/downloads/873"}},
		{{"id", 700}, {"type", "lev"}, {"version", "0.9.0"}, {"file_size", 10}, {"download_url", "/api/v2/tracks/121/downloads/700"}},
		{{"id", 871}, {"type", "track_banner"}, {"version", nullptr}, {"file_size", 10}, {"download_url", "/api/v2/tracks/121/downloads/871"}}});
	if (twice) rows.push_back(rows[0]);
	const std::string text = nlohmann::json({{"data", rows}}).dump();
	served["/api/v2/tracks/121/downloads"] = std::vector<unsigned char>(text.begin(), text.end());
	served["/api/v2/tracks/121/downloads/872"] = lev_body ? *lev_body : lev;
	served["/api/v2/tracks/121/downloads/873"] = vrm_body ? *vrm_body : vrm;
	served["/api/v2/tracks/9/downloads/872"] = lev;
}
static int finish(std::string &detail)
{
	char text[512];
	for (int i = 0; i < 2000; i++)
	{
		const int state = ap_custom_track_download_status(text, sizeof text);
		if (state != AP_CT_DOWNLOAD_RUNNING) { detail = text; return state; }
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	detail = "timeout";
	return -1;
}
static std::vector<std::string> asked()
{
	std::lock_guard<std::mutex> lock(served_mutex);
	return requested;
}
int main()
{
	char tmpl[] = "/tmp/ctr-registry-download-XXXXXX";
	if (!mkdtemp(tmpl)) return 2;
	const std::string tracks = std::string(tmpl) + "/tracks", pkg = tracks + "/acid-lake";
	const std::string orig = pkg + "/original", tlev = orig + "/track.lev", tvrm = orig + "/track.vrm";
	mkdir(tracks.c_str(), 0755);
	const std::string ls = sha(lev), vs = sha(vrm);
	const uint32_t lb = (uint32_t)lev.size(), vb = (uint32_t)vrm.size();
	std::string detail;
	auto start = [&](int track, int lev_id, int vrm_id, const std::string &root = std::string()) {
		return ap_custom_track_download_start_registry(tracks.c_str(), (root.empty() ? pkg : root).c_str(), track,
		                                               lev_id, vrm_id, ls.c_str(), lb, vs.c_str(), vb);
	};

	// 1. Refused at start.
	serve(lb);
	expect(!start(0, 872, 873), "no Saphi track id");
	expect(!start(121, 0, 873) && !start(121, 872, 0), "a missing download id");
	expect(!start(121, 872, 872), "the same id for LEV and VRM");
	expect(!start(-1, 872, 873), "a negative track id");
	expect(!start(121, 872, 873, tracks + "/../escape") && !start(121, 872, 873, tracks + "/a/b"), "an unsafe root");
	expect(asked().empty() && !exists(pkg), "no request and no folder after the start refusals");

	// 2. A clean install from the named ids.
	serve(lb);
	expect(start(121, 872, 873), "start");
	expect(finish(detail) == AP_CT_DOWNLOAD_SUCCEEDED, "clean install succeeds: " + detail);
	{
		const auto paths = asked();
		expect(paths == std::vector<std::string>({"/api/v2/tracks/121/downloads", "/api/v2/tracks/121/downloads/872",
		                                          "/api/v2/tracks/121/downloads/873"}),
		       "only the track's listing and the two named files were fetched");
	}
	expect(slurp(tlev) == lev && slurp(tvrm) == vrm, "the pinned bytes are in tracks/acid-lake/original");
	unlink(tlev.c_str()); unlink(tvrm.c_str()); rmdir(orig.c_str()); rmdir(pkg.c_str());

	// 3. A tampered file of the right size refuses; nothing is created.
	{
		std::vector<unsigned char> other_lev = lev, other_vrm = vrm;
		other_lev[100] ^= 1; other_vrm[7] ^= 1;
		serve(lb, &other_lev);
		start(121, 872, 873);
		expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && detail.find("does not match") != std::string::npos && !exists(pkg),
		       "tampered LEV refuses, no folder: " + detail);
		serve(lb, nullptr, &other_vrm);
		start(121, 872, 873);
		expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && detail.find("does not match") != std::string::npos && !exists(pkg),
		       "tampered VRM refuses, no folder: " + detail);
		// A registry pointing at another track's files: Saphi serves bytes, the room's pins refuse them.
		std::vector<unsigned char> foreign(lev.size(), 0x99);
		serve(lb, &foreign);
		start(121, 872, 873);
		expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && !exists(pkg), "another track's bytes refuse, no folder");
	}

	// 4. Ids outside the named track, unlisted or listed twice: refused before any file.
	serve(lb, nullptr, nullptr, "/api/v2/tracks/9/downloads/872");
	start(121, 872, 873);
	expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && asked().size() == 1 && !exists(pkg),
	       "a LEV listed under another track's path refuses before download: " + detail);
	serve(lb);
	start(121, 874, 873);
	expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && asked().size() == 1 && !exists(pkg), "an unlisted id refuses");
	serve(lb, nullptr, nullptr, nullptr, true);
	start(121, 872, 873);
	expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && asked().size() == 1 && !exists(pkg), "an id listed twice refuses");
	serve(lb);
	start(121, 700, 873);
	expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && asked().size() == 1 && !exists(pkg),
	       "an old revision's id is refused by its listed size");

	// 5. Saphi lists another size than the room pins.
	serve(lb + 1);
	start(121, 872, 873);
	expect(finish(detail) == AP_CT_DOWNLOAD_FAILED && detail.find("file size") != std::string::npos &&
	       asked().size() == 1 && !exists(pkg), "listed size off the pin refuses before download: " + detail);

	// 6. Transport allow-list and the HTTP body cap.
	expect(host_allowed("www.projectsaphi.com") && host_allowed("raw.githubusercontent.com"), "the two hosts are allowed");
	expect(!host_allowed("projectsaphi.com") && !host_allowed("raw.githubusercontent.com.example.org") &&
	       !host_allowed("github.com") && !host_allowed(""), "every other host is refused");
	{
		std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n" + std::string(20, 'x');
		std::vector<unsigned char> bytes(raw.begin(), raw.end()), body;
		std::string error;
		expect(!decode_http_response(bytes, 19, body, error, "the track registry") &&
		       error.find("larger") != std::string::npos, "a body past the cap is refused: " + error);
		error.clear();
		expect(decode_http_response(bytes, 20, body, error, "the track registry") && body.size() == 20, "a body at the cap is read");
		std::string moved = "HTTP/1.1 302 Found\r\nLocation: https://example.org/\r\n\r\n";
		std::vector<unsigned char> redirect(moved.begin(), moved.end());
		expect(!decode_http_response(redirect, 100, body, error, "the track registry"), "a redirect is not followed");
	}

	// 7. Status, Retry-After and the retry policy.
	{
		auto decode = [](const std::string &raw, HttpOutcome &outcome, std::string &error) {
			std::vector<unsigned char> bytes(raw.begin(), raw.end()), body;
			error.clear();
			return decode_http_response(bytes, 100, body, error, "Project Saphi", &outcome);
		};
		HttpOutcome limited;
		std::string error;
		expect(!decode("HTTP/1.1 429 Too Many Requests\r\nRetry-After: 3\r\nContent-Length: 2\r\n\r\n{}", limited, error) &&
		       limited.status == 429 && limited.retry_after == 3 && transfer_retryable(limited) &&
		       error == "Project Saphi is busy (HTTP 429). Retry in a minute.", "a 429 keeps status and Retry-After: " + error);
		HttpOutcome missing;
		expect(!decode("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", missing, error) && missing.status == 404 &&
		       missing.retry_after == -1 && !transfer_retryable(missing) &&
		       error == "Project Saphi refused the file (HTTP 404).", "a 404 is named and not retried: " + error);
		HttpOutcome later;
		expect(!decode("HTTP/1.1 429 Too Many Requests\r\nRetry-After: 7200\r\n\r\n", later, error) &&
		       transfer_retry_delay(later, 1) == 60, "a Retry-After past an hour still waits the 60 s cap");
		HttpOutcome bare;
		expect(decode("HTTP/1.1 200\r\nContent-Length: 2\r\n\r\nok", bare, error) && bare.status == 200,
		       "a 200 without a reason phrase is read");
		HttpOutcome cut;
		expect(!decode("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10\r\nabc", cut, error) &&
		       cut.status == 200 && cut.interrupted && transfer_retryable(cut), "a cut-off chunked 200 is retried");
		HttpOutcome shortened;
		expect(!decode("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabcde", shortened, error) &&
		       shortened.status == 200 && shortened.interrupted && transfer_retryable(shortened) &&
		       error.find("incomplete") != std::string::npos, "a 200 shorter than its Content-Length is retried");
		HttpOutcome garbage;
		expect(!decode("XTTP/1.1 200 OK\r\n\r\n", garbage, error) && garbage.status == 0 &&
		       error.find("invalid HTTP response") != std::string::npos, "a status line that is not HTTP is refused");
		HttpOutcome slow;
		slow.status = 503; slow.retry_after = 100;
		expect(transfer_retry_delay(slow, 1) == 60 && transfer_retry_delay(HttpOutcome{}, 2) == 4,
		       "Retry-After is capped at 60 s; without one the wait grows 2, 4, 6");

		// A fresh gate on a fake clock that the fake wait advances.
		static long long fake_ms = 0;
		RateGate gate;
		gate.clock = [] { return fake_ms; };
		auto run = [&](std::vector<HttpOutcome> answers, std::vector<int> &waits, int &calls, std::string &error) {
			waits.clear(); calls = 0; error.clear(); fake_ms = 0; gate.until_ms = 0;
			return transfer_with_retries("www.projectsaphi.com", "/api/v3/tracks/21/downloads/717",
				[&](HttpOutcome &outcome, std::string &failure) {
					outcome = answers[std::min<size_t>(calls, answers.size() - 1)];
					calls++;
					if (outcome.status == 200 && !outcome.interrupted) return true;
					failure = transfer_error("Project Saphi", outcome);
					return false;
				},
				[&](int seconds) { waits.push_back(seconds); fake_ms += 1000LL * seconds; }, error, gate);
		};
		HttpOutcome ok; ok.status = 200;
		HttpOutcome busy; busy.status = 429; busy.retry_after = 3;
		HttpOutcome down; down.status = 503;
		std::vector<int> waits; int calls = 0;
		expect(run({busy, down, ok}, waits, calls, error) && calls == 3 && waits == std::vector<int>({3, 4}) && error.empty(),
		       "429 then 503 then 200 succeeds after waits of 3 and 4 s");
		expect(!run({missing}, waits, calls, error) && calls == 1 && waits.empty() &&
		       error == "Project Saphi refused the file (HTTP 404).", "a 404 is asked once");
		expect(!run({busy}, waits, calls, error) && calls == kRateLimitAttempts && (int)waits.size() == kRateLimitAttempts - 1 &&
		       error.find("HTTP 429") != std::string::npos, "a lasting 429 gives up after the last attempt: " + error);
		// A 429 is a queue: ten of them in a row (the live limit after a burst) still end in the file.
		{
			std::vector<HttpOutcome> queue(10, busy);
			queue.push_back(ok);
			expect(run(queue, waits, calls, error) && calls == 11 && error.empty(),
			       "ten 429 answers in a row still end in the file");
		}
		// Waiting on 429 is bounded: Retry-After 60 twice spends the 120 s budget, a third would pass it.
		{
			HttpOutcome minute; minute.status = 429; minute.retry_after = 60;
			expect(!run({minute}, waits, calls, error) && calls == 3 && waits == std::vector<int>({60, 60}),
			       "a 429 stops once the next wait would pass the budget");
		}
		// Five 503 answers are not a queue: the other retries still stop at kTransferAttempts.
		expect(!run({down}, waits, calls, error) && calls == kTransferAttempts, "a lasting 503 gives up after four attempts");
		// A second transfer starting while the first holds the gate waits for it before its first request.
		{
			run({ok}, waits, calls, error);
			gate.hold(5);
			expect(run({ok}, waits, calls, error) && calls == 1 && waits.empty(), "reset clears the hold in the test rig");
			fake_ms = 0; gate.until_ms = 0; gate.hold(5);
			std::vector<int> second_waits; int second_calls = 0;
			bool done = transfer_with_retries("www.projectsaphi.com", "/api/v3/tracks/21/downloads/718",
				[&](HttpOutcome &outcome, std::string &) { outcome = ok; second_calls++; return true; },
				[&](int seconds) { second_waits.push_back(seconds); fake_ms += 1000LL * seconds; }, error, gate);
			expect(done && second_calls == 1 && second_waits == std::vector<int>({5}),
			       "a transfer waits for the shared 429 hold before it asks");
		}
	}

	rmdir(tracks.c_str()); rmdir(tmpl);
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "registry_download.cpp", Path(tmp) / "registry_download"
    src.write_text(prelude + part_a + stub + part_b + tests, encoding="utf-8")
    subprocess.run(["g++", "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-I" + str(root / "ap"), "-I" + str(root / "ap/vendor/json/include"),
                    str(src), "-o", str(exe), "-lcrypto", "-pthread"], check=True, cwd=root)
    subprocess.run([str(exe)], check=True, cwd=root)
print("PASS: registry download: start refusals, clean install of the named Saphi ids, tampered or foreign bytes, "
      "ids outside the track, unlisted or doubled, and a listed size off the pin all refuse with no folder; host "
      "allow-list; HTTP body cap; no redirect; HTTP status, Retry-After and the 429/5xx retry policy")
