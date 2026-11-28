// g++ -m32 -std=c++17 -Wall -Wextra -Werror -DAP_TRACK_REGISTRY_TEST -I ap -I include -I ap/vendor/json/include tools/test-track-registry.cpp ap/ap_track_registry.cpp -o /tmp/test-track-registry -pthread
//
// Live track registry lookup for Current Seed (one generator row 10k), with a
// local fixture registry and a stub transport in place of GitHub:
//   1. a live fetch asks only the fixed GitHub raw URL, is published as live
//      and saved as the cache,
//   2. a lookup matches by the room's pinned LEV and VRM hashes (any case)
//      and returns where Saphi has the files,
//   3. a hash mismatch (either file), a size that differs from the pin, a
//      revision with no Saphi download and an ambiguous one never match,
//   4. offline with a cache: the cached copy is used and says so,
//   5. offline without a cache: nothing is located, with a reason,
//   6. a malformed, wrong-schema or deeply nested registry is refused and the
//      cache is kept; a malformed cache is ignored,
//   7. an oversize response or cache is refused,
//   8. a track registered after the cache was written becomes downloadable
//      with the next live fetch (no client release).
// With one argument it parses that registry file and prints what it indexes.
#include "ap_track_registry.h"
#include <platform/native_track_credit.h>
#include <platform/native_cortex_asset_pins.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <stdlib.h>
#include <unistd.h>

void ap_track_registry_test_reset();

using json = nlohmann::json;
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

// The stub transport: one canned answer, or a failure.
static std::mutex g_mutex;
static bool g_up;
static std::vector<unsigned char> g_body;
static std::vector<std::string> g_asked;

bool ap_https_get(const char *host, const std::string &path, size_t max_bytes,
                  std::vector<unsigned char> &body, std::string &error)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_asked.push_back(std::string(host) + path);
	body.clear();
	if (!g_up) { error = "Could not reach the track registry: stub offline"; return false; }
	// The real transport stops reading past max_bytes; so does the stub.
	if (g_body.size() > max_bytes) { error = "the track registry returned a file larger than this client accepts."; return false; }
	body = g_body;
	return true;
}

static void serve(bool up, const std::string &text = {})
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_up = up;
	g_body.assign(text.begin(), text.end());
	g_asked.clear();
}

static std::string h(char c) { return std::string(64, c); }

static json revision(const std::string &rev, char lev, char vrm, int lev_size, int vrm_size,
                     int lev_id, int vrm_id)
{
	json downloads = json::array();
	if (lev_id) downloads.push_back({{"kind", "lev"}, {"download_id", lev_id}, {"crc32", "00000000"}});
	if (vrm_id) downloads.push_back({{"kind", "vrm"}, {"download_id", vrm_id}, {"crc32", "00000000"}});
	return {{"revision", rev}, {"hash_state", "verified"},
	        {"files", {{"lev", {{"sha256", h(lev)}, {"size", lev_size}}}, {"vrm", {{"sha256", h(vrm)}, {"size", vrm_size}}}}},
	        {"provenance", {{"saphi_downloads", downloads}}}};
}

static json track(const std::string &key, int saphi, json revisions)
{
	json t = {{"track_key", key}, {"display_title", key}, {"revisions", revisions}};
	t["provenance"] = saphi ? json{{"saphi", {{"track_id", saphi}, {"category", "community"}}}} : json::object();
	return t;
}

static std::string registry(bool with_new_track)
{
	json tracks = json::array();
	tracks.push_back(track("acid-lake", 121, json::array({revision("1.0.0", 'a', 'b', 810876, 458808, 872, 873),
	                                                      revision("LRG2Pancho", 'c', 'd', 880856, 458808, 0, 0)})));
	tracks.push_back(track("no-saphi", 0, json::array({revision("1.0.0", 'e', 'f', 100, 200, 5, 6)})));
	json twice = revision("2.0.0", '1', '2', 100, 200, 7, 8);
	twice["provenance"]["saphi_downloads"].push_back({{"kind", "lev"}, {"download_id", 9}});
	tracks.push_back(track("ambiguous", 55, json::array({twice})));
	json lev_only = revision("1.0.0", '5', '6', 100, 200, 10, 11);
	lev_only["files"].erase("vrm");
	tracks.push_back(track("lev-only", 0, json::array({lev_only})));
	if (with_new_track)
		tracks.push_back(track("tv-world", 100, json::array({revision("1.1.0", '3', '4', 913816, 458808, 715, 716)})));
	return json({{"schema_version", 1}, {"id_scheme", json::object()}, {"tracks", tracks},
	             {"track_key_aliases", {{"old-lake", "acid-lake"}}},
	             {"road_sha256", {{h('8'), "acid-lake"}}},
	             {"patch_lev_keys", {{h('9'), "no-saphi"}}}}).dump();
}

static int wait_done(int *source, std::string *detail = nullptr)
{
	char text[256] = "";
	int state = AP_TRACK_REGISTRY_LOADING;
	for (int i = 0; i < 2000 && state == AP_TRACK_REGISTRY_LOADING; i++)
	{
		state = ap_track_registry_status(source, text, sizeof text);
		if (state == AP_TRACK_REGISTRY_LOADING) std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	if (detail) *detail = text;
	return state;
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

static bool exists(const std::string &path) { return access(path.c_str(), F_OK) == 0; }

static int lookup(char lev, int lev_size, char vrm, int vrm_size, ApTrackRegistryMatch *m = nullptr)
{
	return ap_track_registry_lookup(h(lev).c_str(), (uint32_t)lev_size, h(vrm).c_str(), (uint32_t)vrm_size, m);
}

int main(int argc, char **argv)
{
	if (argc == 2)
	{
		std::ifstream in(argv[1], std::ios::binary);
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
		std::string error;
		const int n = ap_track_registry_parse_check(bytes, error);
		if (n < 0) { std::printf("refused: %s\n", error.c_str()); return 1; }
		std::printf("registry: %zu bytes, %d revisions downloadable from Saphi\n", bytes.size(), n);
		return 0;
	}
	char dir[] = "/tmp/ctr-track-registry-XXXXXX";
	if (!mkdtemp(dir)) return 2;
	const std::string cache = std::string(dir) + "/track-registry-cache.json";
	const std::string v1 = registry(false), v2 = registry(true);
	int source = -1;
	std::string detail;
	ApTrackRegistryMatch m;

	// Before any fetch nothing is located.
	CHECK(ap_track_registry_status(&source, nullptr, 0) == AP_TRACK_REGISTRY_IDLE);
	CHECK(lookup('a', 810876, 'b', 458808) == AP_TRACK_REGISTRY_NO_INDEX);
	{
		char credit[CTR_TRACK_CREDIT_MAX];
		CustomTrackCredit_Format(credit, sizeof credit,
		    "cc0aaa1dacfbc6f43f1efc73989e7b7e2479ba8f81583784d507bac97f3d0a87",
		    "e7a6a0b82cef9ec3e5c4bb5ab9ce2317b28aab198d7cb5263ef0b883c555e2e6",
		    0, "Lockheart", nullptr);
		CHECK(!std::strcmp(credit, "Track by Lockheart / Music by arbee"));
		CustomTrackCredit_Format(credit, sizeof credit,
		    CTR_CFG_CORTEX_LEV_SHA256, CTR_CFG_CORTEX_VRM_SHA256, 0, nullptr, nullptr);
		CHECK(!std::strcmp(credit, CTR_CORTEX_CREDIT));
		// CV1: a Relic Race serves the bundled relic pair; same credit.
		CustomTrackCredit_Format(credit, sizeof credit,
		    CTR_CFG_CORTEX_RELIC_LEV_SHA256, CTR_CFG_CORTEX_RELIC_VRM_SHA256, 0, nullptr, nullptr);
		CHECK(!std::strcmp(credit, CTR_CORTEX_CREDIT));
	}
	{
		// Identical geometry shipped in two packs. A room's locator names
		// its import even when it has no filename.
		const char *lev = "185e0a49a9cd7b0f1ffd84abed1d01195937cac305f9119bf1d4d98cd4a081d4";
		const char *vrm = "39d80a29475891fa9b236fe8483fd775a732788acaf6ab9577caae6e75112e92";
		const char *patch = "92f4ed21e1c4ada25ce095b0fdaf11c2f3a075f160eb8cf7dcefec9627cddca2";
		char credit[CTR_TRACK_CREDIT_MAX];
		CustomTrackCredit_Format(credit, sizeof credit, lev, vrm, 0, "Unknown",
		                         CustomTrackCredit_PatchSource("", patch));
		CHECK(!std::strcmp(credit, "From Crash-30Anniversary_Adventure.xdelta"));
		CustomTrackCredit_Format(credit, sizeof credit, lev, vrm, 0, "Unknown", nullptr);
		CHECK(!std::strcmp(credit, "From an imported xdelta pack"));
	}

	// 1. Live fetch: the fixed URL only, published live, cached.
	serve(true, v1);
	uint32_t gen = ap_track_registry_generation();
	CHECK(ap_track_registry_refresh(cache.c_str()) == 1);
	CHECK(wait_done(&source) == AP_TRACK_REGISTRY_DONE && source == AP_TRACK_REGISTRY_SOURCE_LIVE);
	CHECK(g_asked.size() == 1 && g_asked[0] == "raw.githubusercontent.com/dowlle/ctr-track-registry/main/registry.json");
	CHECK(slurp(cache) == v1 && !exists(cache + ".part"));
	CHECK(ap_track_registry_generation() != gen);
	{
		char credit[CTR_TRACK_CREDIT_MAX];
		CustomTrackCredit_Format(credit, sizeof credit, h('a').c_str(), h('b').c_str(), 0, "Unknown", nullptr);
		CHECK(!std::strcmp(credit, "From Project Saphi"));
		CustomTrackCredit_Format(credit, sizeof credit, h('a').c_str(), h('b').c_str(), 0, "Unknown", "Example Pack");
		CHECK(!std::strcmp(credit, "From Example Pack"));
		CustomTrackCredit_Format(credit, sizeof credit, h('e').c_str(), h('f').c_str(), 0, "Unknown", nullptr);
		CHECK(!std::strcmp(credit, "From a local custom track package"));
	}

	// Credits are pinned independently of the registry's download eligibility.
	{
		auto credited = json::parse(v1);
		credited["tracks"][0]["credits"] = json::array({
			{{"name", "PanchoPew"}, {"role", "track"}},
			{{"name", "Music Writer"}, {"role", "music"}},
			{{"name", "SecondAuthor"}, {"role", "track"}},
		});
		serve(true, credited.dump());
		CHECK(ap_track_registry_refresh(nullptr) == 1);
		CHECK(wait_done(&source) == AP_TRACK_REGISTRY_DONE);
		char credit[CTR_TRACK_CREDIT_MAX];
		CustomTrackCredit_Format(credit, sizeof credit, h('a').c_str(), h('b').c_str(), 0, "Old name", nullptr);
		CHECK(!std::strcmp(credit, "Track by PanchoPew and SecondAuthor / Music by Music Writer"));
		CustomTrackCredit_Format(credit, sizeof credit, h('c').c_str(), h('d').c_str(), 0, nullptr, nullptr);
		CHECK(!std::strcmp(credit, "Track by PanchoPew and SecondAuthor / Music by Music Writer"));
		CustomTrackCredit_Format(credit, sizeof credit, h('a').c_str(), h('f').c_str(), 121, "Unknown", "Example Pack");
		CHECK(!std::strcmp(credit, "From Example Pack"));
		// MM1b: a role this client does not know is skipped, never drops the entry or its other credits.
		credited["tracks"][0]["credits"] = json::array({
			{{"name", "MisterM"}, {"role", "track"}},
			{{"name", "Some Tester"}, {"role", "playtest"}},
			{{"name", "Boxic"}, {"role", "music"}},
		});
		serve(true, credited.dump());
		CHECK(ap_track_registry_refresh(nullptr) == 1);
		CHECK(wait_done(&source) == AP_TRACK_REGISTRY_DONE);
		CustomTrackCredit_Format(credit, sizeof credit, h('a').c_str(), h('b').c_str(), 0, "Old name", nullptr);
		CHECK(!std::strcmp(credit, "Track by MisterM / Music by Boxic"));
		serve(true, v1);
		CHECK(ap_track_registry_refresh(cache.c_str()) == 1);
		CHECK(wait_done(&source) == AP_TRACK_REGISTRY_DONE);
	}
	{
		std::string key = "old-lake", title;
		CHECK(ap_track_registry_identity("", "", key, title) && key == "acid-lake" && title == "acid-lake");
		key.clear();
		CHECK(ap_track_registry_identity("", h('8'), key, title) && key == "acid-lake" && title == "acid-lake");
		CHECK(ap_track_registry_identity(h('9'), h('8'), key, title) && key == "no-saphi" && title == "no-saphi");
		CHECK(ap_track_registry_identity(h('5'), "", key, title) && key == "lev-only" && title == "lev-only");
		key = "unknown";
		CHECK(!ap_track_registry_identity("", "", key, title) && key == "unknown" && title.empty());
		CHECK(ap_track_registry_canonical_key("release-st-patrick-adventure-slot-7") ==
		      "lego-alien-rally-asteroid");
	}
	{
		auto legacy = json::parse(v1);
		for (const char *field : {"road_sha256", "track_key_aliases", "patch_lev_keys"})
			legacy.erase(field);
		serve(true, legacy.dump());
		{
			std::lock_guard<std::mutex> blocked_transport(g_mutex);
			CHECK(ap_track_registry_refresh(cache.c_str()) == 1);
			ap_track_registry_prepare_identity(cache.c_str(), [](const void *) { return 1; }, nullptr);
			CHECK(ap_track_registry_status(nullptr, nullptr, 0) == AP_TRACK_REGISTRY_LOADING);
		}
		ap_track_registry_prepare_identity(cache.c_str(), nullptr, nullptr);
		CHECK(ap_track_registry_status(&source, nullptr, 0) == AP_TRACK_REGISTRY_DONE &&
		      source == AP_TRACK_REGISTRY_SOURCE_LIVE);
		std::string key = "old-lake", title;
		CHECK(ap_track_registry_identity("", "", key, title) && key == "acid-lake" && title == "acid-lake");
		CHECK(slurp(cache) == v1);
	}

	// 2. Match by the room's pinned hashes, in any case.
	std::memset(&m, 0, sizeof m);
	CHECK(lookup('a', 810876, 'b', 458808, &m) == AP_TRACK_REGISTRY_MATCH);
	CHECK(m.saphi_track_id == 121 && m.lev_download_id == 872 && m.vrm_download_id == 873);
	CHECK(!std::strcmp(m.track_key, "acid-lake") && !std::strcmp(m.revision, "1.0.0"));
	{
		std::string up_lev(64, 'A'), up_vrm(64, 'B');
		CHECK(ap_track_registry_lookup(up_lev.c_str(), 810876, up_vrm.c_str(), 458808, &m) == AP_TRACK_REGISTRY_MATCH);
	}

	// 3. Never a match on anything but both pinned hashes and sizes.
	CHECK(lookup('a', 810876, 'f', 458808) == AP_TRACK_REGISTRY_NOT_LISTED);  // VRM hash differs
	CHECK(lookup('9', 810876, 'b', 458808) == AP_TRACK_REGISTRY_NOT_LISTED);  // LEV hash differs
	CHECK(lookup('b', 458808, 'a', 810876) == AP_TRACK_REGISTRY_NOT_LISTED);  // swapped
	CHECK(lookup('a', 810877, 'b', 458808) == AP_TRACK_REGISTRY_PIN_CONFLICT); // size off the pin
	CHECK(lookup('c', 880856, 'd', 458808) == AP_TRACK_REGISTRY_NOT_ON_SAPHI); // xdelta-only revision
	CHECK(lookup('e', 100, 'f', 200) == AP_TRACK_REGISTRY_NOT_ON_SAPHI);       // track not on Saphi
	CHECK(lookup('1', 100, '2', 200) == AP_TRACK_REGISTRY_NOT_ON_SAPHI);       // two LEV downloads
	CHECK(ap_track_registry_lookup("short", 1, h('b').c_str(), 1, nullptr) == AP_TRACK_REGISTRY_NOT_LISTED);
	CHECK(ap_track_registry_lookup(nullptr, 1, nullptr, 1, nullptr) == AP_TRACK_REGISTRY_NOT_LISTED);

	// 4. Offline with the cache: the cached copy, said so, still matching.
	ap_track_registry_test_reset();
	serve(false);
	CHECK(ap_track_registry_refresh(cache.c_str()) == 1);
	CHECK(wait_done(&source, &detail) == AP_TRACK_REGISTRY_DONE && source == AP_TRACK_REGISTRY_SOURCE_CACHED);
	CHECK(detail.find("stub offline") != std::string::npos);
	CHECK(lookup('a', 810876, 'b', 458808, &m) == AP_TRACK_REGISTRY_MATCH && m.saphi_track_id == 121);
	CHECK(slurp(cache) == v1);

	// 5. Offline without a cache: nothing located, with a reason.
	ap_track_registry_test_reset();
	unlink(cache.c_str());
	serve(false);
	CHECK(ap_track_registry_refresh(cache.c_str()) == 1);
	CHECK(wait_done(&source, &detail) == AP_TRACK_REGISTRY_UNAVAILABLE && source == AP_TRACK_REGISTRY_SOURCE_NONE);
	CHECK(!detail.empty());
	CHECK(lookup('a', 810876, 'b', 458808) == AP_TRACK_REGISTRY_NO_INDEX);
	CHECK(!exists(cache));

	// 6. Malformed registries are refused; the cache stays as it was.
	spit(cache, v1);
	const char *bad[] = {"{not json", "[]", "{\"schema_version\":2,\"tracks\":[]}", "{\"schema_version\":1}",
	                     "{\"schema_version\":\"1\",\"tracks\":[]}", "{\"schema_version\":1,\"tracks\":{}}"};
	for (const char *text : bad)
	{
		ap_track_registry_test_reset();
		serve(true, text);
		ap_track_registry_refresh(cache.c_str());
		CHECK(wait_done(&source, &detail) == AP_TRACK_REGISTRY_DONE && source == AP_TRACK_REGISTRY_SOURCE_CACHED);
		CHECK(detail.find("registry") != std::string::npos);
		CHECK(slurp(cache) == v1);
	}
	{
		// Deep nesting is refused before the parser sees it.
		std::string deep = "{\"schema_version\":1,\"tracks\":[" + std::string(5000, '[') + std::string(5000, ']') + "]}";
		std::vector<unsigned char> bytes(deep.begin(), deep.end());
		std::string error;
		CHECK(ap_track_registry_parse_check(bytes, error) == -1 && !error.empty());
		ap_track_registry_test_reset();
		serve(true, deep);
		ap_track_registry_refresh(cache.c_str());
		CHECK(wait_done(&source) == AP_TRACK_REGISTRY_DONE && source == AP_TRACK_REGISTRY_SOURCE_CACHED);
	}
	{
		// Bad entries are skipped, good ones kept.
		json j = json::parse(v1);
		j["tracks"].push_back("not an object");
		j["tracks"].push_back({{"track_key", "bad-hash"}, {"provenance", {{"saphi", {{"track_id", 9}}}}},
		                       {"revisions", json::array({revision("1", 'z', 'b', 1, 1, 1, 2)})}});
		j["tracks"].push_back({{"track_key", "bad-size"}, {"provenance", {{"saphi", {{"track_id", -4}}}}},
		                       {"revisions", json::array({revision("1", '7', '8', 0, 1, 1, 2)})}});
		const std::string text = j.dump();
		std::vector<unsigned char> bytes(text.begin(), text.end());
		std::string error;
		CHECK(ap_track_registry_parse_check(bytes, error) == 1);
	}
	// A malformed cache is ignored, not trusted.
	ap_track_registry_test_reset();
	spit(cache, "{\"schema_version\":1,\"tracks\":[");
	serve(false);
	ap_track_registry_refresh(cache.c_str());
	CHECK(wait_done(&source) == AP_TRACK_REGISTRY_UNAVAILABLE && lookup('a', 810876, 'b', 458808) == AP_TRACK_REGISTRY_NO_INDEX);

	// 7. Oversize: the response and a cache file past the cap are refused.
	spit(cache, v1);
	ap_track_registry_test_reset();
	{
		std::string big = v1;
		big.insert(big.size() - 1, std::string(AP_TRACK_REGISTRY_MAX_BYTES, ' '));
		serve(true, big);
		ap_track_registry_refresh(cache.c_str());
		CHECK(wait_done(&source, &detail) == AP_TRACK_REGISTRY_DONE && source == AP_TRACK_REGISTRY_SOURCE_CACHED);
		CHECK(detail.find("larger") != std::string::npos && slurp(cache) == v1);
		std::vector<unsigned char> bytes(big.begin(), big.end());
		std::string error;
		CHECK(ap_track_registry_parse_check(bytes, error) == -1);
		ap_track_registry_test_reset();
		spit(cache, big);
		serve(false);
		ap_track_registry_refresh(cache.c_str());
		CHECK(wait_done(&source) == AP_TRACK_REGISTRY_UNAVAILABLE);
	}

	// 8. A new registry entry: absent from the old cache, downloadable after the next live fetch.
	spit(cache, v1);
	ap_track_registry_test_reset();
	serve(false);
	ap_track_registry_refresh(cache.c_str());
	CHECK(wait_done(&source) == AP_TRACK_REGISTRY_DONE && source == AP_TRACK_REGISTRY_SOURCE_CACHED);
	CHECK(lookup('3', 913816, '4', 458808) == AP_TRACK_REGISTRY_NOT_LISTED);
	serve(true, v2);
	gen = ap_track_registry_generation();
	ap_track_registry_refresh(cache.c_str());
	CHECK(wait_done(&source) == AP_TRACK_REGISTRY_DONE && source == AP_TRACK_REGISTRY_SOURCE_LIVE);
	CHECK(ap_track_registry_generation() != gen);
	CHECK(lookup('3', 913816, '4', 458808, &m) == AP_TRACK_REGISTRY_MATCH && m.saphi_track_id == 100 &&
	      m.lev_download_id == 715 && m.vrm_download_id == 716);
	CHECK(slurp(cache) == v2);
	// A later failed refresh keeps the copy this session fetched.
	serve(false);
	ap_track_registry_refresh(cache.c_str());
	CHECK(wait_done(&source) == AP_TRACK_REGISTRY_DONE && lookup('3', 913816, '4', 458808) == AP_TRACK_REGISTRY_MATCH);

	// 9. MR2 mode_pairs: a race and a Relic revision of one track, by label,
	// with targets. Unreadable pairs are skipped whole; the built-in table
	// follows the registry's pairs and released clients ignore the field.
	{
		auto rev = [](const char *label, char lev, char vrm) {
			return json{{"revision", label}, {"hash_state", "verified"},
			            {"files", {{"lev", {{"sha256", std::string(64, lev)}, {"size", 1000}}},
			                       {"vrm", {{"sha256", std::string(64, vrm)}, {"size", 2000}}}}}};
		};
		auto doc = [&](json pairs) {
			json track = {{"track_key", "toasty-terrace"}, {"display_title", "Toasty Terrace"},
			              {"revisions", json::array({rev("1.3.0", 'a', 'b'), rev("1.3.1", 'c', 'd'), rev("1.3.1x", 'c', 'b')})}};
			if (!pairs.is_null()) track["mode_pairs"] = pairs;
			const std::string text = json{{"schema_version", 1}, {"tracks", json::array({track})}}.dump();
			return std::vector<unsigned char>(text.begin(), text.end());
		};
		const json targets = {{"sapphire", 86400}, {"gold", 67200}, {"platinum", 62400}};
		std::vector<ApTrackModePairPins> pairs;
		std::string error;
		CHECK(ap_track_registry_parse_mode_pairs(doc(json::array({{{"race", "1.3.0"}, {"relic", "1.3.1"}, {"relic_targets", targets}}})),
		                                         pairs, error) == 1);
		CHECK(pairs.size() == 1 && pairs[0].key == "toasty-terrace" && pairs[0].race_revision == "1.3.0" &&
		      pairs[0].relic_revision == "1.3.1" && pairs[0].race_lev == std::string(64, 'a') && pairs[0].race_lev_bytes == 1000 &&
		      pairs[0].race_vrm == std::string(64, 'b') && pairs[0].relic_lev == std::string(64, 'c') &&
		      pairs[0].relic_vrm == std::string(64, 'd') && pairs[0].relic_vrm_bytes == 2000 && pairs[0].targets[0] == 86400 &&
		      pairs[0].targets[1] == 67200 && pairs[0].targets[2] == 62400 && pairs[0].registry);
		CHECK(ap_track_registry_parse_mode_pairs(doc(nullptr), pairs, error) == 0);
		const json bad[] = {
			{{"race", "1.3.0"}, {"relic", "9.9.9"}, {"relic_targets", targets}},
			{{"race", "1.3.0"}, {"relic", "1.3.0"}, {"relic_targets", targets}},
			{{"race", "1.3.0"}, {"relic", "1.3.1"}},
			{{"race", "1.3.0"}, {"relic", "1.3.1"}, {"relic_targets", {{"sapphire", 1}, {"gold", 2}, {"platinum", 3}}}},
			{{"race", "1.3.0"}, {"relic", "1.3.1"}, {"relic_targets", {{"sapphire", 86400}, {"gold", 67200}}}},
			{{"race", "1.3.0"}, {"relic", "1.3.1"}, {"relic_targets", {{"sapphire", 86400.5}, {"gold", 67200}, {"platinum", 1}}}},
			{{"race", 130}, {"relic", "1.3.1"}, {"relic_targets", targets}},
		};
		for (const json &pair : bad)
			CHECK(ap_track_registry_parse_mode_pairs(doc(json::array({pair})), pairs, error) == 0);
		CHECK(ap_track_registry_parse_mode_pairs(doc(json{{"race", "1.3.0"}}), pairs, error) == 0);
		// The rest of the registry still reads with mode_pairs present.
		CHECK(ap_track_registry_parse_check(doc(json::array({{{"race", "1.3.0"}, {"relic", "1.3.1"}, {"relic_targets", targets}}})), error) == 0);
		// The built-in table: six tracks, each a valid pair.
		const auto builtin = ap_track_builtin_mode_pairs();
		CHECK(builtin.size() == 6);
		for (const auto &row : builtin)
			CHECK(!row.registry && row.race_lev.size() == 64 && row.relic_lev.size() == 64 && row.race_lev != row.relic_lev &&
			      row.targets[0] > row.targets[1] && row.targets[1] > row.targets[2] && row.targets[2] > 0);
		bool toasty = false;
		for (const auto &row : builtin)
			toasty |= row.key == "toasty-terrace" && row.race_revision == "1.3.0" && row.relic_revision == "1.3.1" &&
			          row.race_lev == "2b18c6305d3cfe591e979eabe42e7815fe475479d49f59d5b7b50b06f701ac5d" &&
			          row.relic_lev == "db74f5ae84d1b57cef68fde1af498b799e8c0bc04262a7fefdfe47c4f5ea181e" && row.targets[2] == 62400;
		CHECK(toasty);
		// Live index first, then the built-in rows.
		const auto bytes = doc(json::array({{{"race", "1.3.0"}, {"relic", "1.3.1"}, {"relic_targets", targets}}}));
		const std::string text(bytes.begin(), bytes.end());
		serve(true, text);
		ap_track_registry_refresh(cache.c_str());
		CHECK(wait_done(&source) == AP_TRACK_REGISTRY_DONE && source == AP_TRACK_REGISTRY_SOURCE_LIVE);
		const auto all = ap_track_mode_pairs();
		CHECK(all.size() == 7 && all[0].registry && all[0].race_lev == std::string(64, 'a') && !all[1].registry);
	}

	unlink(cache.c_str());
	rmdir(dir);
	std::printf("%d checks, %d failures\n", checks, failures);
	if (!failures)
		std::printf("PASS: track registry lookup: pinned-hash match, mismatches refused, live and cached copies, "
		            "offline without cache, malformed and oversize registries\n");
	return failures ? 1 : 0;
}
