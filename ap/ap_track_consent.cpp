// Track author permission check (A4-74). See ap_track_consent.h for the
// order of sources and what unknown means. The fetch uses the Saphi
// downloader's HTTPS transport (ap_https_get), which refuses every host but
// Project Saphi and GitHub raw, like the track registry (ap_track_registry.cpp).
//
// consent.json, format 1 (dowlle/ctr-track-registry):
//   {"format": 1,
//    "authors": [{"saphi_author_id": 1, "name": "Boxic",
//                 "client_listing": {"status": "granted", "scope": "all_saphi_tracks",
//                                    "on": "2026-11-12", "evidence": "..."}}],
//    "tracks":  [{"track_key": "some-track", "client_listing": {"status": "declined", ...}}]}
// status granted, declined or withdrawn; absent or any other value is
// unknown. An author grant counts only with scope "all_saphi_tracks"; a scope
// this client does not know is unknown, never allow. A track entry is more
// specific than its author's and wins.

#include "ap_track_consent.h"
#include "ap_track_registry.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

enum ConsentStatus
{
	kGranted = 1,
	kRefused = 2 // declined or withdrawn
};

struct ApTrackConsentList
{
	std::unordered_map<int, int> by_author_id;
	std::unordered_map<std::string, int> by_name;
	std::unordered_map<std::string, int> by_track;
};

namespace
{
constexpr int kMaxDepth = 16;
constexpr size_t kMaxAuthors = 4096;
constexpr size_t kMaxTracks = 20000;
constexpr size_t kNameMax = 80;
constexpr long long kAuthorIdMax = 100000000LL;

struct LearnedTrack
{
	int author_id = 0;
	int saphi_permission = AP_TRACK_PERMISSION_UNKNOWN;
};

struct ConsentState
{
	std::mutex mutex;
	int state = AP_TRACK_CONSENT_IDLE;
	int source = AP_TRACK_CONSENT_SOURCE_NONE;
	std::string detail;
	std::shared_ptr<const ApTrackConsentList> list;
	std::unordered_map<int, LearnedTrack> learned;
	uint32_t generation = 0;
};

// Process lifetime, like the registry state: a detached worker must not race
// a static destructor at exit.
ConsentState &consent_state()
{
	static ConsentState *state = new ConsentState();
	return *state;
}

bool nesting_ok(const std::vector<unsigned char> &bytes)
{
	int depth = 0;
	bool in_string = false, escaped = false;
	for (unsigned char c : bytes)
	{
		if (in_string)
		{
			if (escaped) escaped = false;
			else if (c == '\\') escaped = true;
			else if (c == '"') in_string = false;
			continue;
		}
		if (c == '"') in_string = true;
		else if (c == '{' || c == '[')
		{
			if (++depth > kMaxDepth) return false;
		}
		else if (c == '}' || c == ']') --depth;
	}
	return true;
}

const nlohmann::json *member(const nlohmann::json &object, const char *name)
{
	if (!object.is_object()) return nullptr;
	auto it = object.find(name);
	return it == object.end() ? nullptr : &*it;
}

bool plain_text(const nlohmann::json *value, size_t max, std::string &out)
{
	if (!value || !value->is_string()) return false;
	out = value->get<std::string>();
	if (out.empty() || out.size() > max) return false;
	for (unsigned char c : out)
		if (c < 32 || c == 127) return false;
	return true;
}

bool track_key_ok(const std::string &key)
{
	if (key.empty() || key.size() > 64 || key.front() == '-' || key.back() == '-') return false;
	for (size_t i = 0; i < key.size(); i++)
	{
		const char c = key[i];
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
		if (c == '-' && key[i + 1] == '-') return false;
	}
	return true;
}

// 0 for an answer this client does not read: unknown, never allow.
int listing_status(const nlohmann::json *listing, bool author_level)
{
	const nlohmann::json *status = listing ? member(*listing, "status") : nullptr;
	if (!status || !status->is_string()) return 0;
	if (*status == "declined" || *status == "withdrawn") return kRefused;
	if (*status != "granted") return 0;
	if (author_level)
	{
		const nlohmann::json *scope = member(*listing, "scope");
		if (!scope || *scope != "all_saphi_tracks") return 0;
	}
	return kGranted;
}

// Two answers for one key: a refusal wins.
void merge(std::unordered_map<std::string, int> &map, const std::string &key, int status)
{
	auto it = map.find(key);
	if (it == map.end()) map.emplace(key, status);
	else if (status == kRefused) it->second = kRefused;
}

bool read_capped(const std::string &path, std::vector<unsigned char> &bytes)
{
	bytes.clear();
	FILE *file = std::fopen(path.c_str(), "rb");
	if (!file) return false;
	unsigned char chunk[16384];
	size_t got;
	bool ok = true;
	while ((got = std::fread(chunk, 1, sizeof chunk, file)) > 0)
	{
		if (bytes.size() + got > AP_TRACK_CONSENT_MAX_BYTES)
		{
			ok = false;
			break;
		}
		bytes.insert(bytes.end(), chunk, chunk + got);
	}
	ok = ok && !std::ferror(file);
	std::fclose(file);
	return ok && !bytes.empty();
}

// Write next to the cache, then rename over it, so a crash leaves the old
// copy or the new one, never half of one.
bool write_cache(const std::string &path, const std::vector<unsigned char> &bytes)
{
	const std::string part = path + ".part";
	FILE *file = std::fopen(part.c_str(), "wb");
	if (!file) return false;
	const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
	const bool closed = std::fclose(file) == 0;
	if (!written || !closed)
	{
		std::remove(part.c_str());
		return false;
	}
#ifdef _WIN32
	const bool moved = MoveFileExA(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	const bool moved = std::rename(part.c_str(), path.c_str()) == 0;
#endif
	if (!moved) std::remove(part.c_str());
	return moved;
}

void publish(std::shared_ptr<const ApTrackConsentList> list, int source)
{
	ConsentState &s = consent_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	s.list = std::move(list);
	s.source = source;
	s.generation++;
}

void finish(int state, const std::string &detail)
{
	ConsentState &s = consent_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	s.state = state;
	s.detail = detail;
}

// The host answered and refused the file (HTTP 4xx other than 429): the list
// is gone, not unreachable. The transport words every refusal this way
// (transfer_error in ap_custom_track_download.cpp; its wording is pinned by
// tools/test-track-registry-download-production.py).
bool list_refused(const std::string &error)
{
	return error.find("refused the file (HTTP 4") != std::string::npos;
}

// Only these failures mean GitHub could not be reached, so the saved copy
// may stand in: no connection, a timeout, no TLS, or the server busy or
// down (429, 5xx). Anything else (a refusal, an oversize, cut-off or
// malformed answer, an unknown message) means the live list is missing or
// unreadable, and nothing is allowed.
bool list_unreachable(const std::string &error)
{
	static const char *const kinds[] = {"Could not reach ", " request timed out", "is unavailable (HTTP 5",
	                                    "is busy (HTTP 429)", "No usable TLS certificate store",
	                                    "Could not start Windows HTTPS", "Could not set up Windows HTTPS"};
	for (const char *kind : kinds)
		if (error.find(kind) != std::string::npos) return true;
	return false;
}

void clear_list()
{
	ConsentState &s = consent_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (s.list) s.generation++;
	s.list.reset();
	s.source = AP_TRACK_CONSENT_SOURCE_NONE;
}

// Fail closed: the cached copy is used only when GitHub cannot be reached.
// It is not published before the live answer, so a list that was removed
// or broken on GitHub never allows a download, not even while loading. A
// missing (HTTP 4xx) or unreadable live file clears the list and, when
// missing, the cache.
void refresh_worker(std::string cache_path)
{
	std::vector<unsigned char> live;
	std::string error;
	std::shared_ptr<const ApTrackConsentList> list;
	try
	{
		if (!ap_https_get(AP_TRACK_CONSENT_HOST, AP_TRACK_CONSENT_PATH, AP_TRACK_CONSENT_MAX_BYTES, live, error))
		{
			if (error.empty()) error = "Could not reach the permission list.";
			if (list_refused(error) || !list_unreachable(error))
			{
				clear_list();
				if (list_refused(error) && !cache_path.empty()) std::remove(cache_path.c_str());
				std::fprintf(stderr, "[CustomTracks] Permission list missing or unreadable: %s\n", error.c_str());
				finish(AP_TRACK_CONSENT_UNAVAILABLE, error);
				return;
			}
		}
		else if (ap_track_consent_parse(live, list, error))
		{
			std::vector<unsigned char> cached;
			const bool same = !cache_path.empty() && read_capped(cache_path, cached) && cached == live;
			publish(list, AP_TRACK_CONSENT_SOURCE_LIVE);
			if (!cache_path.empty() && !same && !write_cache(cache_path, live))
				std::fprintf(stderr, "[CustomTracks] Could not save the permission list cache.\n");
			finish(AP_TRACK_CONSENT_DONE, "");
			return;
		}
		else
		{
			clear_list();
			std::fprintf(stderr, "[CustomTracks] Permission list unreadable: %s\n", error.c_str());
			finish(AP_TRACK_CONSENT_UNAVAILABLE, error);
			return;
		}
		// Unreachable: a list fetched earlier this session stays; otherwise
		// the last good copy on disk.
		bool have;
		{
			ConsentState &s = consent_state();
			std::lock_guard<std::mutex> lock(s.mutex);
			have = s.list != nullptr;
		}
		std::vector<unsigned char> cached;
		std::string cache_error;
		if (!have && !cache_path.empty() && read_capped(cache_path, cached) && ap_track_consent_parse(cached, list, cache_error))
		{
			publish(list, AP_TRACK_CONSENT_SOURCE_CACHED);
			have = true;
		}
		std::fprintf(stderr, "[CustomTracks] Permission list refresh failed: %s\n", error.c_str());
		finish(have ? AP_TRACK_CONSENT_DONE : AP_TRACK_CONSENT_UNAVAILABLE, error);
		return;
	}
	catch (const std::exception &e)
	{
		error = std::string("Permission list: ") + e.what();
	}
	clear_list();
	std::fprintf(stderr, "[CustomTracks] Permission list refresh failed: %s\n", error.c_str());
	finish(AP_TRACK_CONSENT_UNAVAILABLE, error);
}
} // namespace

bool ap_track_consent_parse(const std::vector<unsigned char> &bytes, std::shared_ptr<const ApTrackConsentList> &out,
                            std::string &error)
{
	out.reset();
	if (bytes.empty() || bytes.size() > AP_TRACK_CONSENT_MAX_BYTES)
	{
		error = "The permission list is empty or too large.";
		return false;
	}
	if (!nesting_ok(bytes))
	{
		error = "The permission list is not valid JSON.";
		return false;
	}
	nlohmann::json root = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
	if (root.is_discarded())
	{
		error = "The permission list is not valid JSON.";
		return false;
	}
	const nlohmann::json *format = member(root, "format");
	const nlohmann::json *authors = member(root, "authors");
	const nlohmann::json *tracks = member(root, "tracks");
	if (!format || !format->is_number_integer() || format->get<long long>() != 1 || !authors || !authors->is_array() ||
	    authors->size() > kMaxAuthors || (tracks && (!tracks->is_array() || tracks->size() > kMaxTracks)))
	{
		error = "The permission list is not a format 1 list this client reads.";
		return false;
	}
	auto list = std::make_shared<ApTrackConsentList>();
	for (const auto &author : *authors)
	{
		const int status = listing_status(member(author, "client_listing"), true);
		const nlohmann::json *id = member(author, "saphi_author_id");
		std::string name;
		if (!status) continue;
		if (id && id->is_number_integer() && id->get<long long>() > 0 && id->get<long long>() <= kAuthorIdMax)
		{
			const int key = static_cast<int>(id->get<long long>());
			auto it = list->by_author_id.find(key);
			if (it == list->by_author_id.end()) list->by_author_id.emplace(key, status);
			else if (status == kRefused) it->second = kRefused;
		}
		if (plain_text(member(author, "name"), kNameMax, name)) merge(list->by_name, name, status);
	}
	if (tracks)
		for (const auto &track : *tracks)
		{
			std::string key;
			const int status = listing_status(member(track, "client_listing"), false);
			if (!status || !plain_text(member(track, "track_key"), 64, key) || !track_key_ok(key)) continue;
			merge(list->by_track, key, status);
		}
	out = std::move(list);
	return true;
}

int ap_track_consent_decide(const ApTrackConsentList *list, int saphi_permission, int saphi_author_id,
                            const std::vector<std::string> &credit_names, const std::string &track_key)
{
	int recorded = AP_TRACK_PERMISSION_UNKNOWN;
	if (list)
	{
		auto track = track_key.empty() ? list->by_track.end() : list->by_track.find(track_key);
		if (track != list->by_track.end())
			recorded = track->second == kGranted ? AP_TRACK_PERMISSION_ALLOW : AP_TRACK_PERMISSION_DENY;
		else
		{
			// Every author we can name for the track must have said yes:
			// the Saphi author by id and each name the registry credits.
			bool any = false, all = true, refused = false;
			auto take = [&](int status) {
				if (!status) { all = false; return; }
				any = true;
				if (status == kRefused) refused = true;
			};
			if (saphi_author_id > 0)
			{
				auto it = list->by_author_id.find(saphi_author_id);
				take(it == list->by_author_id.end() ? 0 : it->second);
			}
			for (const auto &name : credit_names)
			{
				auto it = list->by_name.find(name);
				take(it == list->by_name.end() ? 0 : it->second);
			}
			recorded = refused ? AP_TRACK_PERMISSION_DENY
			           : any && all ? AP_TRACK_PERMISSION_ALLOW : AP_TRACK_PERMISSION_UNKNOWN;
		}
	}
	if (saphi_permission == AP_TRACK_PERMISSION_DENY) return AP_TRACK_PERMISSION_DENY;
	// An author's own "no" to us stays honoured until they lift it, even
	// when Saphi later says allow.
	if (recorded == AP_TRACK_PERMISSION_DENY) return AP_TRACK_PERMISSION_DENY;
	// Without the list a direct "no" cannot be ruled out: nothing is offered.
	if (!list) return AP_TRACK_PERMISSION_UNKNOWN;
	if (saphi_permission == AP_TRACK_PERMISSION_ALLOW) return AP_TRACK_PERMISSION_ALLOW;
	return recorded;
}

extern "C" int ap_track_consent_refresh(const char *cache_path)
{
	ConsentState &s = consent_state();
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		if (s.state == AP_TRACK_CONSENT_LOADING) return 0;
		s.state = AP_TRACK_CONSENT_LOADING;
		s.detail.clear();
	}
	try
	{
		std::thread(refresh_worker, std::string(cache_path ? cache_path : "")).detach();
		return 1;
	}
	catch (const std::exception &e)
	{
		bool usable;
		{
			std::lock_guard<std::mutex> lock(s.mutex);
			usable = s.list != nullptr;
		}
		finish(usable ? AP_TRACK_CONSENT_DONE : AP_TRACK_CONSENT_UNAVAILABLE,
		       std::string("Could not start the permission check: ") + e.what());
		return 0;
	}
}

extern "C" int ap_track_consent_refresh_assets(const char *assets)
{
	std::string path;
	if (assets && *assets)
	{
		path = assets;
		if (path.back() != '/' && path.back() != '\\') path += '/';
		path += AP_TRACK_CONSENT_CACHE_NAME;
	}
	return ap_track_consent_refresh(path.c_str());
}

extern "C" int ap_track_consent_status(int *source, char *detail, int detail_bytes)
{
	ConsentState &s = consent_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	// While a refresh runs no list is in force: a list about to be found
	// missing must not allow a download meanwhile.
	if (source) *source = s.list && s.state != AP_TRACK_CONSENT_LOADING ? s.source : AP_TRACK_CONSENT_SOURCE_NONE;
	if (detail && detail_bytes > 0)
		std::snprintf(detail, static_cast<size_t>(detail_bytes), "%s", s.detail.c_str());
	return s.state;
}

extern "C" uint32_t ap_track_consent_generation(void)
{
	ConsentState &s = consent_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return s.generation;
}

extern "C" void ap_track_consent_note_saphi(int saphi_track_id, int saphi_author_id, int saphi_permission)
{
	if (saphi_track_id <= 0) return;
	if (saphi_permission != AP_TRACK_PERMISSION_ALLOW && saphi_permission != AP_TRACK_PERMISSION_DENY)
		saphi_permission = AP_TRACK_PERMISSION_UNKNOWN;
	ConsentState &s = consent_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	LearnedTrack &t = s.learned[saphi_track_id];
	if (t.author_id == (saphi_author_id > 0 ? saphi_author_id : 0) && t.saphi_permission == saphi_permission) return;
	t.author_id = saphi_author_id > 0 ? saphi_author_id : 0;
	t.saphi_permission = saphi_permission;
	s.generation++;
}

extern "C" int ap_track_consent_saphi_author(int saphi_track_id)
{
	ConsentState &s = consent_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	auto it = s.learned.find(saphi_track_id);
	return it == s.learned.end() ? 0 : it->second.author_id;
}

extern "C" int ap_track_permission(int saphi_track_id, int saphi_author_id, int saphi_permission, const char *track_key)
{
	std::shared_ptr<const ApTrackConsentList> list;
	{
		ConsentState &s = consent_state();
		std::lock_guard<std::mutex> lock(s.mutex);
		if (s.state != AP_TRACK_CONSENT_LOADING) list = s.list; // suspended while a refresh runs
		auto it = saphi_track_id > 0 ? s.learned.find(saphi_track_id) : s.learned.end();
		if (it != s.learned.end())
		{
			if (saphi_author_id <= 0) saphi_author_id = it->second.author_id;
			if (saphi_permission == AP_TRACK_PERMISSION_UNKNOWN) saphi_permission = it->second.saphi_permission;
		}
	}
	// The key the caller knows (a seed's folder may differ from the registry
	// key) and the registry key for the Saphi track id: a refusal under either
	// refuses; otherwise either can allow.
	std::string key = track_key ? track_key : "", registry_key;
	if (saphi_track_id > 0)
	{
		char found[65];
		if (ap_track_registry_key_for_saphi(saphi_track_id, found, sizeof found)) registry_key = found;
	}
	if (key.empty()) key.swap(registry_key);
	std::vector<std::string> names;
	if (!key.empty()) ap_track_registry_credits(key, names);
	const int first = ap_track_consent_decide(list.get(), saphi_permission, saphi_author_id, names, key);
	if (registry_key.empty() || registry_key == key) return first;
	ap_track_registry_credits(registry_key, names);
	const int second = ap_track_consent_decide(list.get(), saphi_permission, saphi_author_id, names, registry_key);
	if (first == AP_TRACK_PERMISSION_DENY || second == AP_TRACK_PERMISSION_DENY) return AP_TRACK_PERMISSION_DENY;
	return first == AP_TRACK_PERMISSION_ALLOW || second == AP_TRACK_PERMISSION_ALLOW ? AP_TRACK_PERMISSION_ALLOW
	                                                                                  : AP_TRACK_PERMISSION_UNKNOWN;
}

extern "C" int ap_track_saphi_id_from_url(const char *url)
{
	static const char marker[] = "/tracks/";
	const char *at = url ? std::strstr(url, marker) : nullptr;
	if (!at) return 0;
	at += sizeof marker - 1;
	long value = 0;
	int digits = 0;
	for (; *at >= '0' && *at <= '9' && digits < 9; at++, digits++) value = value * 10 + (*at - '0');
	if (!digits || (*at && *at != '/')) return 0;
	return static_cast<int>(value);
}

extern "C" const char *ap_track_permission_text(void)
{
	int source = AP_TRACK_CONSENT_SOURCE_NONE;
	const int state = ap_track_consent_status(&source, nullptr, 0);
	if (source != AP_TRACK_CONSENT_SOURCE_NONE) return "The author has not allowed in-game download.";
	if (state == AP_TRACK_CONSENT_LOADING || state == AP_TRACK_CONSENT_IDLE)
		return "Checking which authors allow in-game download...";
	return "Permission list not loaded, so download is off.";
}

#ifdef AP_TRACK_CONSENT_TEST
// Harness only: forget everything, as a fresh client start would.
void ap_track_consent_test_reset()
{
	ConsentState &s = consent_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	s.state = AP_TRACK_CONSENT_IDLE;
	s.source = AP_TRACK_CONSENT_SOURCE_NONE;
	s.detail.clear();
	s.list.reset();
	s.learned.clear();
	s.generation++;
}
#endif
