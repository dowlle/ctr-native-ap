// g++ -m32 -std=c++17 -Wall -Wextra -Werror -DAP_TRACK_CONSENT_TEST -DAP_TRACK_REGISTRY_TEST -I ap -I include -I ap/vendor/json/include tools/test-track-consent.cpp ap/ap_track_consent.cpp ap/ap_track_registry.cpp platform/native_saphi_catalogue.cpp -o /tmp/test-track-consent -pthread
//
// Track author permission check (A4-74): the one resolver
// (ap_track_permission) and the consent.json fetch, with a stub transport in
// place of GitHub and a registry fixture shaped like the public one:
//   1. consent.json format 1 parses; a wrong format, broken, deeply nested
//      or oversize file is refused,
//   2. the decision order: the author's recorded answer first, then Saphi's
//      value when present and understood, otherwise unknown; a recorded answer
//      (yes or no) beats Saphi either way; every credited author
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
//   6. Saphi's own permission fields (A4-79): the catalogue adapter's mapping
//      (unspecified, allow, deny, malformed), a recorded yes over a Saphi
//      deny (A4-79b), a recorded decline over a Saphi allow, and the recorded
//      2026-11-14 catalogue giving 61 allowed tracks with today's consent.json.
// With arguments <registry.json> <consent.json> <saphi page>... it prints,
// for recorded Saphi catalogue pages, which tracks the real files allow.
#include "ap_track_consent.h"
#include "ap_track_registry.h"
#include <platform/native_saphi_catalogue.h>

#include <nlohmann/json.hpp>

#include <atomic>
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

static std::atomic<bool> g_hold{false}; // the stub waits here: a refresh in flight

bool ap_https_get(const char *host, const std::string &path, size_t max_bytes,
                  std::vector<unsigned char> &body, std::string &error)
{
	while (g_hold) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	std::lock_guard<std::mutex> lock(g_mutex);
	g_asked.push_back(std::string(host) + path);
	body.clear();
	if (!g_up) { error = "Could not reach the track registry: stub offline"; return false; }
	auto it = g_files.find(path);
	// The production wording (transfer_error): a 4xx is a refusal.
	if (it == g_files.end()) { error = "the track registry refused the file (HTTP 404)."; return false; }
	if (it->second == "503") { error = "the track registry is unavailable (HTTP 503). Retry later."; return false; }
	if (it->second == "429") { error = "the track registry is busy (HTTP 429). Retry in a minute."; return false; }
	if (it->second == "cut") { error = "the track registry returned an incomplete download."; return false; }
	if (it->second == "odd") { error = "the track registry returned an invalid HTTP response."; return false; }
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
	CHECK(decide(v1, D, 1) == A);                 // A4-79b: a recorded yes beats a Saphi deny
	CHECK(decide(v1, U, 1) == A);                 // consent grant, Saphi unspecified
	CHECK(decide(v1, A, 7) == A);                 // no consent entry, Saphi allow
	CHECK(decide(v1, D, 7) == D);                 // no consent entry, Saphi deny
	CHECK(decide(v1, U, 7) == U);                 // no consent entry, Saphi unspecified
	CHECK(decide(v1, D, 0) == D && decide(v1, A, 0) == A && decide(v1, U, 0) == U); // no author known: Saphi's value
	// Co-credit: every author must allow; each uses their DM answer, else Saphi's track value.
	CHECK(decide(v1, A, 1, {"Boxic", "Blounard"}) == A);  // Boxic by DM, Blounard only by Saphi allow
	CHECK(decide(v1, D, 1, {"Boxic", "Blounard"}) == D);  // Blounard has nothing but a Saphi deny
	CHECK(decide(v1, U, 1, {"Boxic", "Blounard"}) == U);  // Blounard unknown on both
	CHECK(decide(v1, D, 7, {"Boxic"}) == D);              // Saphi author 7 has nothing, Saphi deny
	CHECK(decide(v1, D, 1, {"Boxic"}) == A);              // both credits are DM-granted
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
		CHECK(decide(v, A, 9, {"Boxic"}) == D);                      // co-credit: one author declined, the other granted
		CHECK(decide(v, A, 7, {"Blounard", "Tramadolls"}) == D);     // a declined co-author beats Saphi allow for the other
		CHECK(decide(v, D, 1, {"Boxic"}, "dk-pass") == A);           // a track grant beats a Saphi deny
		CHECK(decide(v, A, 1, {"Boxic"}, "dreamy-heights") == D);    // a track decline beats a Saphi allow
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
	CHECK(ap_track_permission(2, 0, D, "folder-name") == A);      // a recorded yes under the registry key beats a Saphi deny, whatever the folder is called
	CHECK(ap_track_permission(2, 0, D, nullptr) == A);
	CHECK(ap_track_permission(122, 0, U, "dreamy-heights") == U); // a consenting track's name on dk-pass files lends nothing
	CHECK(ap_track_permission(0, 0, U, "folder-name") == U);
	CHECK(ap_track_permission(60, 0, U, nullptr) == A);           // both credited authors said yes
	CHECK(ap_track_permission(70, 1, U, nullptr) == U);           // a co-author was not asked
	CHECK(ap_track_permission(122, 0, U, nullptr) == U);          // dk-pass, Blounard not asked
	CHECK(ap_track_permission(900, 0, U, nullptr) == U);          // a track nobody knows
	gen = ap_track_consent_generation();
	ap_track_consent_note_saphi(900, 3, U);                        // the catalogue: Lockheart's track
	CHECK(ap_track_consent_generation() != gen && ap_track_consent_saphi_author(900) == 3);
	CHECK(ap_track_permission(900, 0, U, nullptr) == A);
	ap_track_consent_note_saphi(901, 3, D);                        // Saphi says no, but Lockheart's recorded yes wins (A4-79b)
	CHECK(ap_track_permission(901, 0, U, nullptr) == A);
	ap_track_consent_note_saphi(902, 7, D);                        // Saphi says no, nobody recorded anything
	CHECK(ap_track_permission(902, 0, U, nullptr) == D);
	ap_track_consent_note_saphi(903, 7, A);                        // Saphi says yes, nobody recorded anything
	CHECK(ap_track_permission(903, 0, U, nullptr) == A);
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
	ap_track_consent_test_reset();
	serve(true, "429");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_DONE && source == AP_TRACK_CONSENT_SOURCE_CACHED);
	// An answer that arrived but cannot be read (cut off, malformed, past
	// the size cap): unreadable, so nothing allowed even with the cache.
	for (const std::string &bad : {std::string("cut"), std::string("odd"), std::string(AP_TRACK_CONSENT_MAX_BYTES + 10, ' ')})
	{
		ap_track_consent_test_reset();
		serve(true, bad);
		CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
		CHECK(wait_consent(&source) == AP_TRACK_CONSENT_UNAVAILABLE && ap_track_permission(2, 1, U, nullptr) == U &&
		      slurp(cache) == v1);
	}
	// While a refresh runs no grant is in force, and the screen says it is
	// checking; a later network failure brings the session's list back.
	ap_track_consent_test_reset();
	serve(true, v1);
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1 && wait_consent(&source) == AP_TRACK_CONSENT_DONE);
	CHECK(ap_track_permission(2, 1, U, nullptr) == A);
	g_hold = true;
	serve(true, "");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(ap_track_consent_status(&source, nullptr, 0) == AP_TRACK_CONSENT_LOADING && source == AP_TRACK_CONSENT_SOURCE_NONE);
	CHECK(ap_track_permission(2, 1, U, nullptr) == U && std::strstr(ap_track_permission_text(), "Checking"));
	g_hold = false;
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_UNAVAILABLE && ap_track_permission(2, 1, U, nullptr) == U);
	spit(cache, v1);
	serve(true, v1);
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1 && wait_consent(&source) == AP_TRACK_CONSENT_DONE);
	g_hold = true;
	serve(false, "");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1 && ap_track_permission(2, 1, U, nullptr) == U);
	g_hold = false;
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_DONE && source == AP_TRACK_CONSENT_SOURCE_LIVE &&
	      ap_track_permission(2, 1, U, nullptr) == A);
	// A broken cache and no network: nothing allowed.
	ap_track_consent_test_reset();
	spit(cache, "{broken");
	serve(false, "");
	CHECK(ap_track_consent_refresh(cache.c_str()) == 1);
	CHECK(wait_consent(&source) == AP_TRACK_CONSENT_UNAVAILABLE && ap_track_permission(2, 1, U, nullptr) == U);

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

	// 6. Saphi's own permission fields (A4-79). The adapter is the catalogue
	// parser's: a one-track catalogue goes in, the revision's saphiPermission
	// and creditRequired come out, and the resolver gets that value.
	{
		auto one = [](json permissions, bool with, int *credit = nullptr) {
			json lev = {{"id", 11}, {"type", "lev"}, {"version", "1.0.0"}, {"modes", {"arcade"}}, {"file_size", 4096},
			            {"crc32_hash", 4294967295u}, {"download_url", "/api/v3/tracks/7/downloads/11"}, {"is_current", true}};
			json vrm = lev;
			vrm["id"] = 12;
			vrm["type"] = "vrm";
			vrm["download_url"] = "/api/v3/tracks/7/downloads/12";
			json track = {{"id", 7}, {"name", "Fixture"}, {"author", "Creator"}, {"author_id", 3}, {"type", "race"},
			              {"is_active", true}, {"lap_count", 3}, {"downloads", {lev, vrm}}};
			if (with) track["permissions"] = permissions;
			std::string text = json({{"data", json::array({track})}}).dump(), err(256, '\0');
			CustomSaphiCatalogue *c = nullptr;
			int result = -1;
			if (CustomSaphi_ParseCatalogue(text.data(), text.size(), &c, &err[0], err.size()) && CustomSaphi_Count(c) == 1)
			{
				result = CustomSaphi_Row(c, 0)->saphiPermission;
				if (credit) *credit = CustomSaphi_Row(c, 0)->creditRequired;
			}
			CustomSaphi_Free(&c);
			return result;
		};
		auto perm = [](bool specified, json pnp) {
			return json{{"specified", specified}, {"reach", "public"}, {"monetization", "non_profit"}, {"credit_required", true},
			            {"saphi", true}, {"local_non_profit", true}, {"local_commercial", false}, {"private_non_profit", true},
			            {"private_commercial", false}, {"public_non_profit", pnp}, {"public_commercial", false}};
		};
		int credit = -1;
		// Unspecified (every track uploaded before the fields existed): unknown.
		json unspecified = {{"specified", false}, {"reach", nullptr}, {"monetization", nullptr}, {"credit_required", nullptr},
		                    {"saphi", true}, {"local_non_profit", false}, {"local_commercial", false}, {"private_non_profit", false},
		                    {"private_commercial", false}, {"public_non_profit", false}, {"public_commercial", false}};
		CHECK(one(unspecified, true, &credit) == U && credit == CUSTOM_SAPHI_CREDIT_UNKNOWN);
		CHECK(one(perm(false, true), true) == U);                      // specified false wins over a true box
		CHECK(one(perm(true, true), true, &credit) == A && credit == CUSTOM_SAPHI_CREDIT_REQUIRED);
		CHECK(one(perm(true, false), true) == D);
		{
			json p = perm(true, true);
			p["credit_required"] = false;
			CHECK(one(p, true, &credit) == A && credit == CUSTOM_SAPHI_CREDIT_NOT_REQUIRED);
			p["credit_required"] = nullptr;
			CHECK(one(p, true, &credit) == A && credit == CUSTOM_SAPHI_CREDIT_UNKNOWN);
			p["credit_required"] = "yes";
			CHECK(one(p, true, &credit) == A && credit == CUSTOM_SAPHI_CREDIT_UNKNOWN);
		}
		// reach, monetization and saphi do not decide.
		{
			json p = perm(true, true);
			p["reach"] = "local";
			p["monetization"] = "commercial";
			p["saphi"] = false;
			p["public_commercial"] = false;
			CHECK(one(p, true) == A);
			p = perm(true, false);
			p["reach"] = "public";
			p["monetization"] = "non_profit";
			p["saphi"] = true;
			p["local_non_profit"] = true;
			CHECK(one(p, true) == D);
			p = perm(true, true);
			p.erase("reach");
			p.erase("monetization");
			p.erase("saphi");
			CHECK(one(p, true) == A);
		}
		// Malformed permissions never throw and never allow: unknown.
		CHECK(one(nullptr, false) == U);                                // missing
		CHECK(one(json("allowed"), true) == U);                         // a string
		CHECK(one(json(nullptr), true) == U);                           // null
		CHECK(one(json(true), true) == U);
		CHECK(one(json(1), true) == U);
		CHECK(one(json::array({true}), true) == U);
		CHECK(one(json::object(), true) == U);                          // no keys
		CHECK(one(json{{"public_non_profit", true}}, true) == U);       // specified missing
		for (const json &odd : {json("true"), json(1), json(nullptr), json::array(), json::object(), json(1.5)})
		{
			json p = perm(true, true);
			p["specified"] = odd;
			CHECK(one(p, true) == U);                                   // specified not a boolean
			p = perm(true, true);
			p["public_non_profit"] = odd;
			CHECK(one(p, true) == U);                                   // public_non_profit not a boolean
		}
		{
			json p = perm(true, true);
			p.erase("public_non_profit");
			CHECK(one(p, true, &credit) == U && credit == CUSTOM_SAPHI_CREDIT_REQUIRED);
		}
		// A malformed object does not reject the catalogue (one() returned a row above); neither
		// does one beside a good track.
		// The resolver (A4-79b): a recorded answer, yes or no, beats Saphi; Saphi answers only without one.
		CHECK(decide(v1, one(perm(true, false), true), 3) == A);        // Lockheart said yes by DM, Saphi says no: the DM wins
		CHECK(decide(v1, one(perm(true, true), true), 3) == A);
		CHECK(decide(v1, one(unspecified, true), 3) == A);              // unspecified: the recorded yes decides
		CHECK(decide(v1, one(perm(true, true), true), 7) == A);         // Saphi allow, nobody recorded
		CHECK(decide(v1, one(perm(true, false), true), 7) == D);        // Saphi deny, nobody recorded
		CHECK(decide(v1, one(perm(false, true), true), 7) == U);        // unspecified, not asked: unknown
		CHECK(decide(v1, one(json("x"), true), 7) == U);
		{
			json c = json::parse(v1);
			c["authors"].push_back(author(9, "Declined Author", grant("declined")));
			c["authors"].push_back(author(10, "Withdrawn Author", grant("withdrawn")));
			const std::string v2 = c.dump();
			CHECK(decide(v2, one(perm(true, true), true), 9) == D);     // declined beats a Saphi allow
			CHECK(decide(v2, one(perm(true, true), true), 10) == D);    // withdrawn beats a Saphi allow
			CHECK(decide(v2, one(perm(true, false), true), 9) == D);
		}
	}
	// The recorded 2026-11-14 catalogue (117 tracks) with today's consent.json:
	// 61 allowed. Lockheart's 12 are specified public non-profit on Saphi and
	// recorded yes; the 105 unspecified tracks follow consent.json.
	{
		const std::string dir_fixtures = "tools/fixtures/saphi-permissions/";
		std::string recorded = slurp(dir_fixtures + "catalogue-2026-11-14.json"), consent = slurp(dir_fixtures + "consent-2026-11-14.json");
		if (recorded.empty() || consent.empty()) // run from another directory
		{
			const char *root = getenv("CTR_REPO_ROOT");
			if (root) { recorded = slurp(std::string(root) + "/" + dir_fixtures + "catalogue-2026-11-14.json");
			            consent = slurp(std::string(root) + "/" + dir_fixtures + "consent-2026-11-14.json"); }
		}
		CHECK(!recorded.empty() && !consent.empty());
		json page = json::parse(recorded.empty() ? "{\"data\":[]}" : recorded);
		CHECK(page["data"].size() == 117);
		for (auto &t : page["data"])
		{
			const int id = t["id"].get<int>();
			json lev = {{"id", id * 10 + 1}, {"type", "lev"}, {"version", "1.0.0"}, {"modes", {"arcade"}}, {"file_size", 4096},
			            {"crc32_hash", 1}, {"download_url", "/api/v3/tracks/" + std::to_string(id) + "/downloads/" + std::to_string(id * 10 + 1)},
			            {"is_current", true}};
			json vrm = lev;
			vrm["id"] = id * 10 + 2;
			vrm["type"] = "vrm";
			vrm["download_url"] = "/api/v3/tracks/" + std::to_string(id) + "/downloads/" + std::to_string(id * 10 + 2);
			t["downloads"] = {lev, vrm};
			t["type"] = "race"; // the five adventure hubs would not make rows; judge them by their permissions too
		}
		std::string text = page.dump(), err(256, '\0');
		CustomSaphiCatalogue *c = nullptr;
		CHECK(CustomSaphi_ParseCatalogue(text.data(), text.size(), &c, &err[0], err.size()));
		CHECK(CustomSaphi_Count(c) == 117);
		ap_track_consent_test_reset();
		serve(true, consent);
		CHECK(ap_track_consent_refresh("") == 1 && wait_consent(&source) == AP_TRACK_CONSENT_DONE);
		int allowed = 0, specified = 0, saphi_allow = 0, saphi_unknown = 0, deny = 0, lockheart = 0;
		std::map<int, int> by_author;
		for (size_t i = 0; i < CustomSaphi_Count(c); i++)
		{
			const CustomSaphiRevision *r = CustomSaphi_Row(c, i);
			const int p = ap_track_permission(r->trackID, r->authorID, r->saphiPermission, nullptr);
			if (p == A) { allowed++; by_author[r->authorID]++; }
			if (p == D) deny++;
			if (r->saphiPermission == A) { saphi_allow++; if (r->authorID == 3) lockheart++; }
			if (r->saphiPermission == U) saphi_unknown++;
			if (r->creditRequired == CUSTOM_SAPHI_CREDIT_REQUIRED) specified++;
		}
		CHECK(allowed == 61);
		CHECK(saphi_allow == 12 && lockheart == 12 && saphi_unknown == 105 && deny == 0);
		CHECK(specified == 12);                                        // credit_required stored on exactly the specified tracks
		CHECK(by_author[3] == 12);                                     // Lockheart: Saphi and consent both allow
		// Without consent.json nothing is allowed, even Lockheart's Saphi allow.
		ap_track_consent_test_reset();
		for (size_t i = 0; i < CustomSaphi_Count(c); i++)
		{
			const CustomSaphiRevision *r = CustomSaphi_Row(c, i);
			CHECK(ap_track_permission(r->trackID, r->authorID, r->saphiPermission, nullptr) == U);
		}
		// A4-79b: Lockheart's Saphi setting turned to deny changes nothing, his recorded DM yes wins: still 61.
		json flipped = json::parse(text);
		for (auto &t : flipped["data"])
			if (t["permissions"]["specified"] == true) t["permissions"]["public_non_profit"] = false;
		std::string ftext = flipped.dump();
		CustomSaphiCatalogue *f = nullptr;
		CHECK(CustomSaphi_ParseCatalogue(ftext.data(), ftext.size(), &f, &err[0], err.size()));
		serve(true, consent);
		CHECK(ap_track_consent_refresh("") == 1 && wait_consent(&source) == AP_TRACK_CONSENT_DONE);
		int still = 0, denied = 0;
		for (size_t i = 0; i < CustomSaphi_Count(f); i++)
		{
			const CustomSaphiRevision *r = CustomSaphi_Row(f, i);
			const int p = ap_track_permission(r->trackID, r->authorID, r->saphiPermission, nullptr);
			still += p == A;
			denied += p == D;
		}
		CHECK(still == 61 && denied == 0);
		CustomSaphi_Free(&f);
		CustomSaphi_Free(&c);
		ap_track_consent_test_reset();
	}

	unlink(cache.c_str());
	rmdir(dir);
	std::printf("%d checks, %d failures\n", checks, failures);
	if (!failures)
		std::printf("PASS: track author permission check: decision order, author and credit matching, live and cached lists, "
		            "missing, unreadable and offline lists\n");
	return failures != 0;
}
