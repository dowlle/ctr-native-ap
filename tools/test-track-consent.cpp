// g++ -m32 -std=c++17 -Wall -Wextra -Werror -DAP_TRACK_CONSENT_TEST -DAP_TRACK_REGISTRY_TEST -I ap -I ap/vendor/json/include tools/test-track-consent.cpp ap/ap_track_consent.cpp ap/ap_track_registry.cpp -o /tmp/test-track-consent -pthread
//
// Track author permission check (A4-74): the one resolver
// (ap_track_permission) and the consent.json fetch, with a stub transport in
// place of GitHub and a registry fixture shaped like the public one:
//   1. consent.json format 1 parses; a wrong format, broken, deeply nested
//      or oversize file is refused,
//   2. the decision order: Saphi's value when present and understood, then
//      the author's recorded answer, otherwise unknown; a direct "no" beats a
//      Saphi allow; a Saphi deny beats a recorded yes; every credited author
//      must have said yes; a track entry beats its author's; an answer or
//      scope this client does not read is unknown, never allow; with no list
//      nothing is allowed, even with a Saphi allow,
//   3. matching: by the catalogue's author_id, by what the catalogue taught
//      for a Saphi track id (Current Seed, installed packages), and by the
//      registry's credits for the track key,
//   4. the failure cases: no network (or a 5xx) with a cache uses the cache;
//      no network and no cache, consent.json missing (HTTP 404, as before the
//      registry PR merges) or unreadable: everything unknown even with a
//      cache, a missing file also drops the cache, and the text says the list
//      is not loaded,
//   5. the Saphi track id in a download or source URL.
// With arguments <registry.json> <consent.json> <saphi page>... it prints,
// for recorded Saphi catalogue pages, which tracks the real files allow.
#include "ap_track_consent.h"
#include "ap_track_registry.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <stdlib.h>
#include <unistd.h>

void ap_track_consent_test_reset();
void ap_track_registry_test_reset();

using json = nlohmann::json;
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

// The stub transport: one canned answer per path, a 404, or offline.
static std::mutex g_mutex;
static bool g_up;
static std::map<std::string, std::string> g_files;
static std::vector<std::string> g_asked;

bool ap_https_get(const char *host, const std::string &path, size_t max_bytes,
                  std::vector<unsigned char> &body, std::string &error)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_asked.push_back(std::string(host) + path);
	body.clear();
	if (!g_up) { error = "Could not reach the track registry: stub offline"; return false; }
	auto it = g_files.find(path);
	// The production wording (transfer_error): a 4xx is a refusal.
	if (it == g_files.end()) { error = "the track registry refused the file (HTTP 404)."; return false; }
	if (it->second == "503") { error = "the track registry is unavailable (HTTP 503). Retry later."; return false; }
	if (it->second.size() > max_bytes) { error = "the track registry returned a file larger than this client accepts."; return false; }
	body.assign(it->second.begin(), it->second.end());
	return true;
}

static void serve(bool up, const std::string &consent, const std::string &registry = {})
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_up = up;
	g_files.clear();
	if (!consent.empty()) g_files[AP_TRACK_CONSENT_PATH] = consent;
	if (!registry.empty()) g_files[AP_TRACK_REGISTRY_PATH] = registry;
	g_asked.clear();
}

static std::vector<unsigned char> bytes(const std::string &s) { return std::vector<unsigned char>(s.begin(), s.end()); }

static json grant(const char *status = "granted", const char *scope = "all_saphi_tracks")
{
	json j = {{"status", status}, {"on", "2026-11-12"}, {"evidence", "private receipt held by the maintainer"}};
	if (scope) j["scope"] = scope;
	return j;
}

static json author(int id, const char *name, json listing)
{
	return {{"saphi_author_id", id}, {"name", name}, {"client_listing", listing}};
}

// The four authors who said yes on 2026-11-12, keyed as Saphi keys them.
static std::string consent_v1()
{
	json authors = json::array({author(1, "Boxic", grant()), author(12, "Lolothesamurai", grant()),
	                            author(5, "PanchoPew", grant()), author(3, "Lockheart", grant())});
	return json({{"format", 1}, {"authors", authors}, {"tracks", json::array()}}).dump();
}

// A registry fixture: one Boxic track, one shared Lolothesamurai and
// PanchoPew track, one track by an author who was not asked.
static std::string registry_fixture()
{
	auto revision = [](char lev, char vrm) {
		return json{{"revision", "1.0.0"}, {"hash_state", "verified"},
		            {"files", {{"lev", {{"sha256", std::string(64, lev)}, {"size", 100}}},
		                       {"vrm", {{"sha256", std::string(64, vrm)}, {"size", 200}}}}},
		            {"provenance", {{"saphi_downloads", json::array({{{"kind", "lev"}, {"download_id", 1}},
		                                                             {{"kind", "vrm"}, {"download_id", 2}}})}}}};
	};
	auto track = [&](const char *key, int saphi, json credits, char lev, char vrm) {
		return json{{"track_key", key}, {"display_title", key}, {"credits", credits},
		            {"provenance", {{"saphi", {{"track_id", saphi}}}}}, {"revisions", json::array({revision(lev, vrm)})}};
	};
	json tracks = json::array({
		track("dreamy-heights", 2, json::array({{{"name", "Boxic"}, {"role", "track"}}}), 'a', 'b'),
		track("sewer-baby-park", 60, json::array({{{"name", "Lolothesamurai"}, {"role", "track"}},
		                                           {{"name", "PanchoPew"}, {"role", "track"}}}), 'c', 'd'),
		track("dk-pass", 122, json::array({{{"name", "Blounard"}, {"role", "track"}}}), 'e', 'f'),
		track("half-known", 70, json::array({{{"name", "Boxic"}, {"role", "track"}}, {{"name", "Blounard"}, {"role", "track"}}}), '1', '2')});
	return json({{"schema_version", 1}, {"id_scheme", json::object()}, {"tracks", tracks}}).dump();
}

template <typename F> static int wait_for(F status)
{
	int state = 1;
	for (int i = 0; i < 2000 && state == 1; i++)
	{
		state = status();
		if (state == 1) std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return state;
}
static int wait_consent(int *source, std::string *detail = nullptr)
{
	char text[256] = "";
	const int state = wait_for([&] { return ap_track_consent_status(source, text, sizeof text); });
	if (detail) *detail = text;
	return state;
}
static int wait_registry()
{
	int source = 0;
	return wait_for([&] { return ap_track_registry_status(&source, nullptr, 0); });
}

static std::string slurp(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), {});
}
static void spit(const std::string &path, const std::string &text)
{
	std::ofstream out(path, std::ios::binary);
	out << text;
}

static int decide(const std::string &consent, int saphi, int author_id, std::vector<std::string> names = {},
                  const std::string &key = {})
{
	std::shared_ptr<const ApTrackConsentList> list;
	std::string error;
	if (!ap_track_consent_parse(bytes(consent), list, error)) return -1;
	return ap_track_consent_decide(list.get(), saphi, author_id, names, key);
}

enum { U = AP_TRACK_PERMISSION_UNKNOWN, A = AP_TRACK_PERMISSION_ALLOW, D = AP_TRACK_PERMISSION_DENY };

// Evidence mode: which recorded Saphi tracks the real files allow.
static int evidence(int argc, char **argv)
{
	std::ifstream rin(argv[1], std::ios::binary), cin(argv[2], std::ios::binary);
	std::vector<unsigned char> reg((std::istreambuf_iterator<char>(rin)), {}), con((std::istreambuf_iterator<char>(cin)), {});
	std::string error;
	std::shared_ptr<const ApTrackConsentList> list;
	if (ap_track_registry_parse_check(reg, error) < 0) { std::printf("registry refused: %s\n", error.c_str()); return 1; }
	if (!ap_track_consent_parse(con, list, error)) { std::printf("consent refused: %s\n", error.c_str()); return 1; }
	// Publish the registry through the real refresh, served by the stub.
	serve(true, std::string(con.begin(), con.end()), std::string(reg.begin(), reg.end()));
	ap_track_registry_refresh("");
	wait_registry();
	ap_track_consent_refresh("");
	int source = 0;
	wait_consent(&source);
	std::map<std::string, int> allowed, total;
	int yes = 0, all = 0, seed_yes = 0;
	for (int i = 3; i < argc; i++)
	{
		std::ifstream pin(argv[i], std::ios::binary);
		json page = json::parse(pin);
		for (const auto &t : page.at("data"))
		{
			const int id = t.at("id").get<int>(), aid = t.value("author_id", 0);
			const std::string name = t.at("author").get<std::string>();
			const int p = ap_track_permission(id, aid, U, nullptr);
			// The Current Seed path with no catalogue loaded: the Saphi id only,
			// matched through the registry's track key and credits.
			const int seed = ap_track_permission(id, 0, U, nullptr);
			total[name]++;
			all++;
			if (p == A) { allowed[name]++; yes++; }
			if (seed == A) seed_yes++;
			std::printf("%-4d %-36s %-18s %-8s seed-path %s\n", id, t.at("name").get<std::string>().c_str(), name.c_str(),
			            p == A ? "allow" : p == D ? "deny" : "unknown", seed == A ? "allow" : seed == D ? "deny" : "unknown");
		}
	}
	std::printf("\n%d of %d Saphi tracks allowed by author id; %d by Saphi id through the registry alone\n", yes, all, seed_yes);
	for (const auto &t : total) std::printf("  %-18s %d of %d\n", t.first.c_str(), allowed[t.first], t.second);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 4) return evidence(argc, argv);
	char dir[] = "/tmp/ctr-track-consent-XXXXXX";
	if (!mkdtemp(dir)) return 2;
	const std::string cache = std::string(dir) + "/" AP_TRACK_CONSENT_CACHE_NAME;
	const std::string v1 = consent_v1();
	int source = -1;
	std::string detail, error;
	std::shared_ptr<const ApTrackConsentList> list;

	// 1. Parsing.
	CHECK(ap_track_consent_parse(bytes(v1), list, error) && list);
	CHECK(!ap_track_consent_parse(bytes("{\"format\": 2, \"authors\": []}"), list, error) && !list);
	CHECK(!ap_track_consent_parse(bytes("{\"authors\": []}"), list, error));
	CHECK(!ap_track_consent_parse(bytes("{\"format\": 1, \"authors\": {}}"), list, error));
	CHECK(!ap_track_consent_parse(bytes("{\"format\": 1, \"authors\": [], \"tracks\": 3}"), list, error));
	CHECK(!ap_track_consent_parse(bytes("not json"), list, error) && error.find("not valid JSON") != std::string::npos);
	CHECK(!ap_track_consent_parse(bytes(std::string(40, '[') + std::string(40, ']')), list, error));
	CHECK(!ap_track_consent_parse(bytes(""), list, error));
	CHECK(!ap_track_consent_parse(std::vector<unsigned char>(AP_TRACK_CONSENT_MAX_BYTES + 1, ' '), list, error));
	CHECK(ap_track_consent_parse(bytes("{\"format\": 1, \"authors\": []}"), list, error)); // tracks is optional

	// 2. The decision.
	CHECK(decide(v1, U, 1) == A);                 // Boxic said yes for all his Saphi tracks
	CHECK(decide(v1, U, 7) == U);                 // Blounard was not asked
	CHECK(decide(v1, U, 0) == U);                 // no author known
	CHECK(decide(v1, U, 0, {"Boxic"}) == A);      // matched by registry credit
	CHECK(decide(v1, U, 0, {"boxic"}) == U);      // names match exactly
	CHECK(decide(v1, U, 12, {"Lolothesamurai", "PanchoPew"}) == A); // both credited authors said yes
	CHECK(decide(v1, U, 1, {"Boxic", "Blounard"}) == U);            // one credited author unknown
	CHECK(decide(v1, A, 7) == A);                 // Saphi allow, no direct answer
	CHECK(decide(v1, D, 1) == D);                 // Saphi deny beats a recorded yes
	{
		json c = json::parse(v1);
		c["authors"].push_back(author(9, "Tramadolls", grant("declined", nullptr)));
		c["authors"].push_back(author(18, "daviddg", grant("withdrawn")));
		c["authors"].push_back(author(19, "Late_One", grant("maybe")));
		c["authors"].push_back(author(21, "Papu_persia", grant("granted", "some_new_scope")));
		c["authors"].push_back(author(22, "EarlGrey", grant("granted", nullptr)));
		c["authors"].push_back(author(5, "PanchoPew", grant("withdrawn"))); // a later "no" for the same author
		c["tracks"].push_back({{"track_key", "dreamy-heights"}, {"client_listing", grant("declined", nullptr)}});
		c["tracks"].push_back({{"track_key", "dk-pass"}, {"client_listing", grant("granted", nullptr)}});
		c["tracks"].push_back({{"track_key", "Bad Key"}, {"client_listing", grant("granted", nullptr)}});
		const std::string v = c.dump();
		CHECK(decide(v, U, 9) == D && decide(v, U, 18) == D);        // declined, withdrawn
		CHECK(decide(v, A, 9) == D);                                 // a direct "no" beats a later Saphi allow
		CHECK(decide(v, U, 19) == U);                                // an answer this client does not read
		CHECK(decide(v, U, 21) == U && decide(v, U, 22) == U);       // a grant without the known scope
		CHECK(decide(v, U, 5) == D);                                 // two answers: the "no" wins
		CHECK(decide(v, U, 1, {"Boxic"}, "dreamy-heights") == D);    // a track entry beats its author's yes
		CHECK(decide(v, U, 7, {"Blounard"}, "dk-pass") == A);        // a track entry for an author not asked
		CHECK(decide(v, U, 0, {}, "Bad Key") == U);
	}
	CHECK(ap_track_consent_decide(nullptr, A, 1, {}, "") == U); // no list: nothing offered, even with Saphi allow
	CHECK(ap_track_consent_decide(nullptr, D, 1, {}, "") == D);

	// 5. URLs.
	CHECK(ap_track_saphi_id_from_url("https://www.projectsaphi.com/api/v2/tracks/101/downloads") == 101);
	CHECK(ap_track_saphi_id_from_url("https://www.projectsaphi.com/tracks/101") == 101);
	CHECK(ap_track_saphi_id_from_url("https://www.projectsaphi.com/tracks/x1") == 0);
	CHECK(ap_track_saphi_id_from_url("https://www.projectsaphi.com/tracks/12a") == 0);
	CHECK(ap_track_saphi_id_from_url(nullptr) == 0 && ap_track_saphi_id_from_url("") == 0);

	// 4a. Before any fetch: nothing allowed, and the screen says it is checking.
	CHECK(ap_track_consent_status(&source, nullptr, 0) == AP_TRACK_CONSENT_IDLE && source == AP_TRACK_CONSENT_SOURCE_NONE);
	CHECK(ap_track_permission(2, 1, U, nullptr) == U);
	CHECK(std::strstr(ap_track_permission_text(), "Checking"));

	// Live fetch: the fixed URL only, published live, cached; then the
	// shared resolver answers by author id.
	serve(true, v1, registry_fixture());
	uint32_t gen = ap_track_consent_generation();
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_DONE && source == AP_TRACK_CONSENT_SOURCE_LIVE);
	CHECK(g_asked.size() == 1 && g_asked[0] == "raw.githubusercontent.com/dowlle/ctr-track-registry/main/consent.json");
	CHECK(slurp(cache) == v1 && access((cache + ".part").c_str(), F_OK) != 0);
	CHECK(ap_track_consent_generation() != gen);
	CHECK(ap_track_permission(2, 1, U, nullptr) == A);
	CHECK(ap_track_permission(122, 7, U, nullptr) == U);
	CHECK(std::strstr(ap_track_permission_text(), "has not allowed"));

	// 3. Matching without an author id: first nothing (no catalogue, no
	// registry), then by registry credits, then by what the catalogue taught.
	CHECK(ap_track_permission(2, 0, U, nullptr) == U);
	ap_track_registry_refresh("");
	CHECK(wait_registry() == AP_TRACK_REGISTRY_DONE);
	CHECK(ap_track_permission(2, 0, U, nullptr) == A);            // dreamy-heights, credited to Boxic
	CHECK(ap_track_permission(0, 0, U, "dreamy-heights") == A);   // a seed package's track key
	CHECK(ap_track_permission(2, 0, U, "folder-name") == A);      // a seed folder that is not the registry key
	CHECK(ap_track_permission(0, 0, U, "folder-name") == U);
	CHECK(ap_track_permission(60, 0, U, nullptr) == A);           // both credited authors said yes
	CHECK(ap_track_permission(70, 1, U, nullptr) == U);           // a co-author was not asked
	CHECK(ap_track_permission(122, 0, U, nullptr) == U);          // dk-pass, Blounard not asked
	CHECK(ap_track_permission(900, 0, U, nullptr) == U);          // a track nobody knows
	gen = ap_track_consent_generation();
	ap_track_consent_note_saphi(900, 3, U);                        // the catalogue: Lockheart's track
	CHECK(ap_track_consent_generation() != gen && ap_track_consent_saphi_author(900) == 3);
	CHECK(ap_track_permission(900, 0, U, nullptr) == A);
	ap_track_consent_note_saphi(901, 3, D);                        // Saphi says no for this one
	CHECK(ap_track_permission(901, 0, U, nullptr) == D);
	gen = ap_track_consent_generation();
	ap_track_consent_note_saphi(900, 3, U);                        // no change, no new generation
	CHECK(ap_track_consent_generation() == gen);

	// 4b. No network, a cache: the cached copy, said so, still allowing.
	ap_track_consent_test_reset();
	serve(false, "");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source, &detail) == AP_TRACK_CONSENT_DONE && source == AP_TRACK_CONSENT_SOURCE_CACHED);
	CHECK(detail.find("stub offline") != std::string::npos);
	CHECK(ap_track_permission(2, 1, U, nullptr) == A);
	CHECK(slurp(cache) == v1);

	// 4c. No network, no cache: unknown everywhere, and the text says the
	// list could not be loaded instead of looking empty.
	ap_track_consent_test_reset();
	unlink(cache.c_str());
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source, &detail) == AP_TRACK_CONSENT_UNAVAILABLE && source == AP_TRACK_CONSENT_SOURCE_NONE);
	CHECK(ap_track_permission(2, 1, U, nullptr) == U && ap_track_permission(2, 1, A, nullptr) == U);
	CHECK(std::strstr(ap_track_permission_text(), "not loaded"));

	// 4d. consent.json missing on main (before the registry PR merges, or
	// removed later): the host refuses it with 404. Nothing is allowed, even
	// with a good cache, and the cache is dropped.
	ap_track_consent_test_reset();
	spit(cache, v1);
	serve(true, "", registry_fixture());
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source, &detail) == AP_TRACK_CONSENT_UNAVAILABLE && detail.find("404") != std::string::npos);
	CHECK(source == AP_TRACK_CONSENT_SOURCE_NONE && ap_track_permission(2, 1, U, nullptr) == U && access(cache.c_str(), F_OK) != 0);
	// A list fetched earlier this session is cleared by a later 404 too.
	serve(true, v1);
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1 && wait_consent(&source) == AP_TRACK_CONSENT_DONE);
	CHECK(ap_track_permission(2, 1, U, nullptr) == A);
	serve(true, "");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1 && wait_consent(&source) == AP_TRACK_CONSENT_UNAVAILABLE);
	CHECK(ap_track_permission(2, 1, U, nullptr) == U);

	// 4e. consent.json unreadable (broken or a format this client does not
	// read): nothing allowed, even with a good cache; the cache is kept for a
	// later offline start.
	ap_track_consent_test_reset();
	spit(cache, v1);
	serve(true, "{\"format\": 1, \"authors\": [");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source, &detail) == AP_TRACK_CONSENT_UNAVAILABLE && detail.find("not valid JSON") != std::string::npos);
	CHECK(ap_track_permission(2, 1, U, nullptr) == U && slurp(cache) == v1);
	ap_track_consent_test_reset();
	serve(true, "{\"format\": 9, \"authors\": []}");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_UNAVAILABLE && ap_track_permission(2, 1, U, nullptr) == U);
	// A server error is like no network: the cache is used.
	ap_track_consent_test_reset();
	serve(true, "503");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_DONE && source == AP_TRACK_CONSENT_SOURCE_CACHED &&
	      ap_track_permission(2, 1, U, nullptr) == A);
	// A broken cache and no network: nothing allowed.
	ap_track_consent_test_reset();
	spit(cache, "{broken");
	serve(false, "");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_UNAVAILABLE && ap_track_permission(2, 1, U, nullptr) == U);
	// An oversize answer is refused.
	ap_track_consent_test_reset();
	unlink(cache.c_str());
	serve(true, std::string(AP_TRACK_CONSENT_MAX_BYTES + 10, ' '));
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_UNAVAILABLE);

	// A withdrawal reaches the client with the next live fetch, no release.
	ap_track_consent_test_reset();
	{
		json c = json::parse(v1);
		c["authors"][0]["client_listing"] = grant("withdrawn");
		serve(true, c.dump());
	}
	spit(cache, v1);
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_DONE && source == AP_TRACK_CONSENT_SOURCE_LIVE);
	CHECK(ap_track_permission(2, 1, U, nullptr) == D && slurp(cache) != v1);

	unlink(cache.c_str());
	rmdir(dir);
	std::printf("%d checks, %d failures\n", checks, failures);
	if (!failures)
		std::printf("PASS: track author permission check: decision order, author and credit matching, live and cached lists, "
		            "missing, unreadable and offline lists\n");
	return failures != 0;
}
