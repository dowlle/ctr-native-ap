// Live track registry lookup for Current Seed (one generator row 10k). See
// ap_track_registry.h for the trust model. The fetch uses the Saphi
// downloader's HTTPS transport (ap_https_get), which refuses every host but
// Project Saphi and GitHub raw.

#include "ap_track_registry.h"
#include "ap_track_key_aliases.hpp"
#include <platform/native_cortex_asset_pins.h>
#include <platform/native_track_credit.h>
#include <platform/native_custom_package.h>
#include <platform/native_track_sources.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
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

namespace
{
constexpr int kMaxDepth = 32;          // registry.json nests 6 deep; refuse a crafted deep file before parsing
constexpr size_t kMaxTracks = 20000;   // the id scheme has room for 10,000
constexpr size_t kMaxRevisions = 64;   // per track
constexpr long long kTrackFileMax = 8LL * 1024 * 1024;
constexpr long long kSaphiIdMax = 100000000LL;

struct Revision
{
	std::string lev, vrm; // lower-case hex
	uint32_t lev_size = 0, vrm_size = 0;
	int track_id = 0, lev_download = 0, vrm_download = 0;
	std::string key, revision;
};
// The downloadable revisions, plus what the permission check (A4-74) needs:
// which track key a Saphi track id has, and each track's credited names.
struct Index
{
	std::vector<Revision> rows;
	std::unordered_map<int, std::string> key_by_saphi;
	struct Credit { std::string name, role; };
	std::unordered_map<std::string, std::vector<Credit>> credits;
	std::unordered_map<std::string, std::string> titles, lev_keys, roads, aliases;
	std::vector<ApTrackModePairPins> mode_pairs; // MR2: race pair, Relic pair, Relic targets
	bool has_identity = false;
};
constexpr size_t kMaxModePairs = 8;     // per track, like the registry checker
constexpr int kMaxRelicTicks = 960 * 60 * 60;
constexpr size_t kMaxCredits = 16;     // per track
constexpr size_t kCreditNameMax = 80;  // bytes, like a Saphi author

struct RegistryState
{
	std::mutex mutex;
	std::condition_variable finished;
	int state = AP_TRACK_REGISTRY_IDLE;
	int source = AP_TRACK_REGISTRY_SOURCE_NONE;
	std::string detail;
	std::shared_ptr<const Index> index;
	std::shared_ptr<const Index> identity;
	uint32_t generation = 0;
};

// Process lifetime, like the download state: a detached worker must not race
// a static destructor at exit.
RegistryState &registry_state()
{
	static RegistryState *state = new RegistryState();
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

bool hex64(const nlohmann::json &value, std::string &out)
{
	if (!value.is_string()) return false;
	const std::string &s = value.get_ref<const std::string &>();
	if (s.size() != 64) return false;
	out.clear();
	for (char c : s)
	{
		if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
		out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return true;
}

bool positive(const nlohmann::json &value, long long max, long long &out)
{
	if (!value.is_number_integer()) return false;
	out = value.get<long long>();
	return out > 0 && out <= max;
}

const nlohmann::json *member(const nlohmann::json &object, const char *name)
{
	if (!object.is_object()) return nullptr;
	auto it = object.find(name);
	return it == object.end() ? nullptr : &*it;
}

bool file_pin(const nlohmann::json *files, const char *kind, std::string &sha, uint32_t &size)
{
	const nlohmann::json *file = files ? member(*files, kind) : nullptr;
	const nlohmann::json *hash = file ? member(*file, "sha256") : nullptr;
	const nlohmann::json *bytes = file ? member(*file, "size") : nullptr;
	long long n = 0;
	if (!hash || !bytes || !hex64(*hash, sha) || !positive(*bytes, kTrackFileMax, n)) return false;
	size = static_cast<uint32_t>(n);
	return true;
}

std::string short_text(const nlohmann::json *value)
{
	if (!value || !value->is_string()) return {};
	std::string s = value->get<std::string>();
	return s.size() > 64 ? s.substr(0, 64) : s;
}

// Exactly one LEV and one VRM download id, else none: an ambiguous revision
// is not downloadable.
void saphi_downloads(const nlohmann::json &revision, Revision &out)
{
	const nlohmann::json *prov = member(revision, "provenance");
	const nlohmann::json *list = prov ? member(*prov, "saphi_downloads") : nullptr;
	int lev = 0, vrm = 0, levs = 0, vrms = 0;
	if (!list || !list->is_array()) return;
	for (const auto &row : *list)
	{
		const nlohmann::json *kind = member(row, "kind");
		const nlohmann::json *id = member(row, "download_id");
		long long n = 0;
		if (!kind || !kind->is_string() || !id || !positive(*id, kSaphiIdMax, n)) continue;
		if (*kind == "lev") { lev = static_cast<int>(n); levs++; }
		else if (*kind == "vrm") { vrm = static_cast<int>(n); vrms++; }
	}
	if (levs == 1 && vrms == 1)
	{
		out.lev_download = lev;
		out.vrm_download = vrm;
	}
}

// MR2: a track's optional `mode_pairs` (registry checker: race and relic name
// two of this track's revisions, relic_targets slower to faster). A pair this
// client cannot read in full is skipped, never half used: the client then
// writes the track as before. Revision labels must be unique in the track.
void read_mode_pairs(const nlohmann::json &track, const std::string &key,
                     const std::unordered_map<std::string, Revision> &labelled,
                     const std::unordered_map<std::string, int> &label_count, std::vector<ApTrackModePairPins> &out)
{
	const nlohmann::json *pairs = member(track, "mode_pairs");
	if (key.empty() || !pairs || !pairs->is_array() || pairs->size() > kMaxModePairs) return;
	for (const auto &pair : *pairs)
	{
		const std::string race = short_text(member(pair, "race")), relic = short_text(member(pair, "relic"));
		const nlohmann::json *targets = member(pair, "relic_targets");
		auto a = labelled.find(race), b = labelled.find(relic);
		if (race.empty() || relic.empty() || race == relic || a == labelled.end() || b == labelled.end() ||
		    label_count.at(race) != 1 || label_count.at(relic) != 1 || !targets)
			continue;
		ApTrackModePairPins row;
		bool ok = true;
		const char *tiers[3] = {"sapphire", "gold", "platinum"};
		for (int i = 0; i < 3; i++)
		{
			long long n = 0;
			const nlohmann::json *t = member(*targets, tiers[i]);
			ok = ok && t && positive(*t, kMaxRelicTicks, n);
			row.targets[i] = ok ? static_cast<int>(n) : 0;
		}
		if (!ok || !(row.targets[0] > row.targets[1] && row.targets[1] > row.targets[2])) continue;
		if (a->second.lev == b->second.lev && a->second.vrm == b->second.vrm) continue;
		row.key = key;
		row.race_revision = race;
		row.relic_revision = relic;
		row.race_lev = a->second.lev; row.race_lev_bytes = a->second.lev_size;
		row.race_vrm = a->second.vrm; row.race_vrm_bytes = a->second.vrm_size;
		row.relic_lev = b->second.lev; row.relic_lev_bytes = b->second.lev_size;
		row.relic_vrm = b->second.vrm; row.relic_vrm_bytes = b->second.vrm_size;
		row.registry = true;
		out.push_back(std::move(row));
	}
}

bool parse_index(const std::vector<unsigned char> &bytes, Index &index, std::string &error)
{
	index = {};
	if (bytes.empty() || bytes.size() > AP_TRACK_REGISTRY_MAX_BYTES)
	{
		error = "The track registry is empty or too large.";
		return false;
	}
	if (!nesting_ok(bytes))
	{
		error = "The track registry is not valid JSON.";
		return false;
	}
	nlohmann::json root = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
	if (root.is_discarded())
	{
		error = "The track registry is not valid JSON.";
		return false;
	}
	const nlohmann::json *schema = member(root, "schema_version");
	const nlohmann::json *tracks = member(root, "tracks");
	if (!schema || !schema->is_number_integer() || schema->get<long long>() != 1 || !tracks || !tracks->is_array() ||
	    tracks->size() > kMaxTracks)
	{
		error = "The track registry is not a version 1 registry this client reads.";
		return false;
	}
	for (const auto &track : *tracks)
	{
		const std::string key = short_text(member(track, "track_key"));
		const auto *title = member(track, "display_title");
		if (!key.empty() && title && title->is_string())
		{
			const auto &name = title->get_ref<const std::string &>();
			if (!name.empty() && name.size() <= 128 && name.find('\0') == std::string::npos)
				index.titles.emplace(key, name);
		}
		const nlohmann::json *credits = member(track, "credits");
		if (!key.empty() && credits && credits->is_array() && credits->size() <= kMaxCredits)
		{
			std::vector<Index::Credit> names;
			bool ok = true;
			for (const auto &credit : *credits)
			{
				const nlohmann::json *name = member(credit, "name");
				if (!name || !name->is_string() || name->get_ref<const std::string &>().empty() ||
				    name->get_ref<const std::string &>().size() > kCreditNameMax)
				{
					ok = false; // a credit this client cannot read: no names, so no permission by name
					break;
				}
				const auto *role = member(credit, "role");
				const auto &text = name->get_ref<const std::string &>();
				names.push_back({text, role && role->is_string() ? role->get<std::string>() : ""});
			}
			if (ok && !names.empty() && !index.credits.count(key)) index.credits.emplace(key, std::move(names));
		}
		const nlohmann::json *revisions = member(track, "revisions");
		const nlohmann::json *prov = member(track, "provenance");
		const nlohmann::json *saphi = prov ? member(*prov, "saphi") : nullptr;
		const nlohmann::json *id = saphi ? member(*saphi, "track_id") : nullptr;
		long long track_id = 0;
		if (!revisions || !revisions->is_array() || revisions->size() > kMaxRevisions) continue;
		if (!id || !positive(*id, kSaphiIdMax, track_id)) track_id = 0;
		// Two keys claiming one Saphi track: neither is trusted.
		if (track_id && !key.empty())
		{
			auto found = index.key_by_saphi.find(static_cast<int>(track_id));
			if (found == index.key_by_saphi.end()) index.key_by_saphi.emplace(static_cast<int>(track_id), key);
			else if (found->second != key) found->second.clear();
		}
		std::unordered_map<std::string, Revision> labelled; // this track's revisions with both pins
		std::unordered_map<std::string, int> label_count;
		for (const auto &revision : *revisions)
		{
			Revision row;
			const nlohmann::json *files = member(revision, "files");
			if (!file_pin(files, "lev", row.lev, row.lev_size)) continue;
			auto lev = index.lev_keys.emplace(row.lev, key);
			if (!lev.second && lev.first->second != key) lev.first->second.clear();
			if (!file_pin(files, "vrm", row.vrm, row.vrm_size)) continue;
			row.track_id = static_cast<int>(track_id);
			row.key = key;
			row.revision = short_text(member(revision, "revision"));
			saphi_downloads(revision, row);
			if (!row.revision.empty() && ++label_count[row.revision] == 1) labelled.emplace(row.revision, row);
			index.rows.push_back(std::move(row));
		}
		read_mode_pairs(track, key, labelled, label_count, index.mode_pairs);
	}
	auto read_keys = [&](const char *field, auto &out, bool hashes) {
		const auto *mapping = member(root, field);
		if (!mapping || !mapping->is_object()) return false;
		for (auto it = mapping->begin(); it != mapping->end(); ++it)
		{
			if (!it->is_string() || !index.titles.count(it->get_ref<const std::string &>())) return false;
			std::string from = it.key();
			if (hashes && !hex64(nlohmann::json(it.key()), from)) return false;
			if (!hashes && index.titles.count(from)) return false;
			if (!out.emplace(from, it->get<std::string>()).second) return false;
		}
		return true;
	};
	index.has_identity = read_keys("road_sha256", index.roads, true) &&
	                     read_keys("track_key_aliases", index.aliases, false) &&
	                     read_keys("patch_lev_keys", index.lev_keys, true);
	if (!index.has_identity) index.aliases.clear();
	return true;
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
		if (bytes.size() + got > AP_TRACK_REGISTRY_MAX_BYTES)
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

void publish(Index index, int source)
{
	auto shared = std::make_shared<const Index>(std::move(index));
	RegistryState &s = registry_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (shared->has_identity) s.identity = shared;
	s.index = std::move(shared);
	s.source = source;
	s.generation++;
}

void finish(int state, const std::string &detail)
{
	RegistryState &s = registry_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	s.state = state;
	s.detail = detail;
	s.finished.notify_all();
}

void refresh_worker(std::string cache_path)
{
	std::vector<unsigned char> cached, live;
	std::string error;
	Index index;
	bool have_cache = false, cache_has_identity = false;
	try
	{
		if (!cache_path.empty() && read_capped(cache_path, cached) && parse_index(cached, index, error))
		{
			have_cache = true;
			cache_has_identity = index.has_identity;
			bool empty;
			{
				RegistryState &s = registry_state();
				std::lock_guard<std::mutex> lock(s.mutex);
				empty = !s.index;
			}
			// A copy fetched earlier this session is newer than the disk.
			if (empty) publish(std::move(index), AP_TRACK_REGISTRY_SOURCE_CACHED);
		}
		error.clear();
		if (!ap_https_get(AP_TRACK_REGISTRY_HOST, AP_TRACK_REGISTRY_PATH, AP_TRACK_REGISTRY_MAX_BYTES, live, error))
		{
			if (error.empty()) error = "Could not reach the track registry.";
		}
		else if (parse_index(live, index, error))
		{
			const bool preserve_names = cache_has_identity && !index.has_identity;
			publish(std::move(index), AP_TRACK_REGISTRY_SOURCE_LIVE);
			if (!preserve_names && !cache_path.empty() && (!have_cache || live != cached) && !write_cache(cache_path, live))
				std::fprintf(stderr, "[CustomTracks] Could not save the track registry cache.\n");
			finish(AP_TRACK_REGISTRY_DONE, "");
			return;
		}
	}
	catch (const std::exception &e)
	{
		error = std::string("Track registry: ") + e.what();
	}
	bool usable;
	{
		RegistryState &s = registry_state();
		std::lock_guard<std::mutex> lock(s.mutex);
		usable = s.index != nullptr;
	}
	std::fprintf(stderr, "[CustomTracks] Track registry refresh failed: %s\n", error.c_str());
	finish(usable ? AP_TRACK_REGISTRY_DONE : AP_TRACK_REGISTRY_UNAVAILABLE, error);
}

std::string lower(const char *s)
{
	std::string out = s ? s : "";
	for (char &c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}
} // namespace

int ap_track_registry_parse_check(const std::vector<unsigned char> &bytes, std::string &error)
{
	Index index;
	if (!parse_index(bytes, index, error)) return -1;
	int n = 0;
	for (const auto &r : index.rows)
		if (r.track_id && r.lev_download && r.vrm_download) n++;
	return n;
}

extern "C" int ap_track_registry_refresh(const char *cache_path)
{
	RegistryState &s = registry_state();
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		if (s.state == AP_TRACK_REGISTRY_LOADING) return 0;
		s.state = AP_TRACK_REGISTRY_LOADING;
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
			usable = s.index != nullptr;
		}
		finish(usable ? AP_TRACK_REGISTRY_DONE : AP_TRACK_REGISTRY_UNAVAILABLE,
		       std::string("Could not start the registry check: ") + e.what());
		return 0;
	}
}

extern "C" void ap_track_registry_prepare_identity(const char *cache_path,
                                                    int (*cancelled)(const void *), const void *context)
{
	if (ap_track_registry_status(nullptr, nullptr, 0) == AP_TRACK_REGISTRY_IDLE)
		ap_track_registry_refresh(cache_path);
	auto &state = registry_state();
	std::unique_lock<std::mutex> lock(state.mutex);
	while (state.state == AP_TRACK_REGISTRY_LOADING && (!cancelled || !cancelled(context)))
	{
		if (cancelled) state.finished.wait_for(lock, std::chrono::milliseconds(25));
		else state.finished.wait(lock);
	}
}

extern "C" int ap_track_registry_status(int *source, char *detail, int detail_bytes)
{
	RegistryState &s = registry_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (source) *source = s.index ? s.source : AP_TRACK_REGISTRY_SOURCE_NONE;
	if (detail && detail_bytes > 0)
		std::snprintf(detail, static_cast<size_t>(detail_bytes), "%s", s.detail.c_str());
	return s.state;
}

extern "C" uint32_t ap_track_registry_generation(void)
{
	RegistryState &s = registry_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return s.generation;
}

extern "C" int ap_track_registry_lookup(const char *lev_sha256, uint32_t lev_bytes,
                                        const char *vrm_sha256, uint32_t vrm_bytes,
                                        struct ApTrackRegistryMatch *out)
{
	std::shared_ptr<const Index> index;
	{
		RegistryState &s = registry_state();
		std::lock_guard<std::mutex> lock(s.mutex);
		index = s.index;
	}
	if (!index) return AP_TRACK_REGISTRY_NO_INDEX;
	const std::string lev = lower(lev_sha256), vrm = lower(vrm_sha256);
	bool not_on_saphi = false, conflict = false;
	if (lev.size() != 64 || vrm.size() != 64) return AP_TRACK_REGISTRY_NOT_LISTED;
	for (const auto &r : index->rows)
	{
		if (r.lev != lev || r.vrm != vrm) continue;
		if ((lev_bytes && r.lev_size != lev_bytes) || (vrm_bytes && r.vrm_size != vrm_bytes))
		{
			conflict = true;
			continue;
		}
		if (!r.track_id || !r.lev_download || !r.vrm_download)
		{
			not_on_saphi = true;
			continue;
		}
		if (out)
		{
			std::memset(out, 0, sizeof *out);
			out->saphi_track_id = r.track_id;
			out->lev_download_id = r.lev_download;
			out->vrm_download_id = r.vrm_download;
			std::snprintf(out->track_key, sizeof out->track_key, "%s", r.key.c_str());
			std::snprintf(out->revision, sizeof out->revision, "%s", r.revision.c_str());
		}
		return AP_TRACK_REGISTRY_MATCH;
	}
	return not_on_saphi ? AP_TRACK_REGISTRY_NOT_ON_SAPHI
	                    : conflict ? AP_TRACK_REGISTRY_PIN_CONFLICT : AP_TRACK_REGISTRY_NOT_LISTED;
}

extern "C" int ap_track_registry_key_for_saphi(int saphi_track_id, char *key, int key_bytes)
{
	std::shared_ptr<const Index> index;
	{
		RegistryState &s = registry_state();
		std::lock_guard<std::mutex> lock(s.mutex);
		index = s.index;
	}
	if (key && key_bytes > 0) key[0] = 0;
	if (!index || saphi_track_id <= 0) return 0;
	auto found = index->key_by_saphi.find(saphi_track_id);
	if (found == index->key_by_saphi.end() || found->second.empty()) return 0;
	if (key && key_bytes > 0) std::snprintf(key, static_cast<size_t>(key_bytes), "%s", found->second.c_str());
	return 1;
}

static std::shared_ptr<const Index> current_index()
{
	RegistryState &s = registry_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return s.index;
}

static std::string canonical_key(const Index *index, const std::string &key)
{
	if (index && index->has_identity)
	{
		auto alias = index->aliases.find(key);
		if (alias != index->aliases.end()) return alias->second;
	}
	for (const auto &alias : kTrackKeyAliases)
		if (key == alias[0]) return alias[1];
	return key;
}

std::string ap_track_registry_canonical_key(const std::string &key)
{
	auto &state = registry_state();
	std::lock_guard<std::mutex> lock(state.mutex);
	return canonical_key(state.identity.get(), key);
}

bool ap_track_registry_credits(const std::string &track_key, std::vector<std::string> &names)
{
	std::shared_ptr<const Index> index;
	std::shared_ptr<const Index> identity;
	{
		RegistryState &s = registry_state();
		std::lock_guard<std::mutex> lock(s.mutex);
		index = s.index;
		identity = s.identity;
	}
	names.clear();
	if (!index || track_key.empty()) return false;
	auto found = index->credits.find(canonical_key(identity.get(), track_key));
	if (found == index->credits.end()) return false;
	for (const auto &credit : found->second) names.push_back(credit.name);
	return true;
}

static void add_credit_name(std::string &names, const std::string &name)
{
	/* Presentation validation is independent of the names used for consent. */
	if (name.find_first_not_of(' ') == std::string::npos ||
	    std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
		return;
	if (!names.empty()) names += " and ";
	names += name;
}

extern "C" void CustomTrackCredit_Format(char *out, size_t size, const char *lev, const char *vrm,
                                         int saphiID, const char *author, const char *source)
{
	if (!out || !size) return;
	if (lev && vrm && ((TrackSources_HexEqual(lev, CTR_CFG_CORTEX_LEV_SHA256) &&
	                    TrackSources_HexEqual(vrm, CTR_CFG_CORTEX_VRM_SHA256)) ||
	                   (TrackSources_HexEqual(lev, CTR_CFG_CORTEX_RELIC_LEV_SHA256) &&
	                    TrackSources_HexEqual(vrm, CTR_CFG_CORTEX_RELIC_VRM_SHA256))))
	{
		std::snprintf(out, size, "%s", CTR_CORTEX_CREDIT);
		return;
	}
	const auto index = current_index();
	std::string key, track, music;
	bool matchedSaphi = false;
	if (index)
	{
		if (lev && vrm && *lev && *vrm)
		{
			for (const auto &row : index->rows)
			{
				if (row.lev != lev || row.vrm != vrm || row.key.empty()) continue;
				if (!key.empty() && key != row.key)
				{
					key.clear();
					matchedSaphi = false;
					break;
				}
				key = row.key;
				matchedSaphi = matchedSaphi || row.track_id > 0;
			}
		}
		else if (const auto found = index->key_by_saphi.find(saphiID); found != index->key_by_saphi.end())
		{
			key = found->second;
			matchedSaphi = !key.empty();
		}
		if (key == "cortex-vortex")
		{
			std::snprintf(out, size, "%s", CTR_CORTEX_CREDIT);
			return;
		}
		const auto found = index->credits.find(key);
		if (found != index->credits.end())
		{
			for (const auto &credit : found->second)
			{
				if (credit.role == "track") add_credit_name(track, credit.name);
				if (credit.role == "music") add_credit_name(music, credit.name);
			}
		}
	}
	if (track.empty() && author && std::strcmp(author, "Unknown") &&
	    std::strcmp(author, "Unknown author"))
		add_credit_name(track, author);
	if (matchedSaphi && (!source || !*source))
		source = "Project Saphi";
	struct TrackSource known;
	if ((key.empty() || (track.empty() && (!source || !*source))) &&
	    lev && *lev && vrm && *vrm && TrackSources_Find(lev, vrm, &known))
	{
		/* A cold offline library can still identify a shipped Cortex pair. */
		if (key.empty() && known.kind == TRACK_SOURCE_SAPHI && known.saphiTrack == 44)
		{
			std::snprintf(out, size, "%s", CTR_CORTEX_CREDIT);
			return;
		}
		if (track.empty() && (!source || !*source))
			source = known.kind == TRACK_SOURCE_PATCH
				? (known.alsoPatch ? "an imported xdelta pack" : known.patchFile)
				: "Project Saphi";
	}
	if (track.empty())
		std::snprintf(out, size, "From %s", source && *source ? source : "a local custom track package");
	else if (!music.empty() && track == music)
		std::snprintf(out, size, "Track and music by %s", track.c_str());
	else
		std::snprintf(out, size, "Track by %s", track.c_str());
	if (!music.empty() && track != music)
	{
		const size_t used = std::strlen(out);
		std::snprintf(out + used, size - used, " / Music by %s", music.c_str());
	}
}

extern "C" const char *CustomTrackCredit_PatchSource(const char *name, const char *sha256)
{
	if (name && *name) return name;
	if (!sha256 || !*sha256) return nullptr;
	for (const auto &row : g_trackSources)
		if (row.kind == TRACK_SOURCE_PATCH && TrackSources_HexEqual(row.patchSha256, sha256))
			return row.patchFile;
	return "an imported xdelta pack";
}

extern "C" void CustomPackage_TrackCredit(const struct CustomPackageLocalInfo *local, const char *fallbackAuthor,
                                          char *out, size_t size)
{
	if (!out || !size) return;
	const char *source = nullptr;
	if (local->xdelta)
		source = CustomTrackCredit_PatchSource(nullptr, local->patchSha256);
	else if (local->saphi || local->permissionTrackID)
		source = "Project Saphi";
	CustomTrackCredit_Format(out, size, local->levSha256, local->vrmSha256, local->permissionTrackID,
	                         local->author[0] ? local->author : fallbackAuthor, source);
}

extern "C" void CustomTrackCredit_EnsureRegistry(const char *assets)
{
	if (assets && *assets && ap_track_registry_status(nullptr, nullptr, 0) == AP_TRACK_REGISTRY_IDLE)
	{
		std::string path(assets);
		if (path.back() != '/' && path.back() != '\\') path += '/';
		path += "track-registry-cache.json";
		ap_track_registry_refresh(path.c_str());
	}
}

bool ap_track_registry_identity(const std::string &lev, const std::string &road,
                                std::string &key, std::string &title)
{
	auto &state = registry_state();
	std::lock_guard<std::mutex> lock(state.mutex);
	const auto &index = state.identity;
	if (!index) return false;
	auto exact = index->lev_keys.find(lev);
	auto layout = index->roads.find(road);
	auto alias = index->aliases.find(key);
	if (exact != index->lev_keys.end() && !exact->second.empty()) key = exact->second;
	else if (layout != index->roads.end()) key = layout->second;
	else if (alias != index->aliases.end()) key = alias->second;
	auto name = index->titles.find(key);
	title = name == index->titles.end() ? std::string() : name->second;
	return !title.empty();
}

#ifdef AP_TRACK_REGISTRY_TEST
// Harness only: forget everything, as a fresh client start would.
void ap_track_registry_test_reset()
{
	RegistryState &s = registry_state();
	std::lock_guard<std::mutex> lock(s.mutex);
	s.state = AP_TRACK_REGISTRY_IDLE;
	s.source = AP_TRACK_REGISTRY_SOURCE_NONE;
	s.detail.clear();
	s.index.reset();
	s.identity.reset();
	s.generation++;
}
#endif

int ap_track_registry_parse_mode_pairs(const std::vector<unsigned char> &bytes, std::vector<ApTrackModePairPins> &pairs,
                                       std::string &error)
{
	Index index;
	pairs.clear();
	if (!parse_index(bytes, index, error)) return -1;
	pairs = index.mode_pairs;
	return static_cast<int>(pairs.size());
}

// The client's own rows (include/platform/native_track_mode_pairs.inc).
std::vector<ApTrackModePairPins> ap_track_builtin_mode_pairs()
{
	std::vector<ApTrackModePairPins> out;
#define MP(k_, rr, rl, rlb, rv, rvb, qr, ql, qlb, qv, qvb, s, g, p)                                                    \
	{                                                                                                                   \
		ApTrackModePairPins row;                                                                                        \
		row.key = k_; row.race_revision = rr; row.relic_revision = qr;                                                 \
		row.race_lev = rl; row.race_lev_bytes = rlb; row.race_vrm = rv; row.race_vrm_bytes = rvb;                       \
		row.relic_lev = ql; row.relic_lev_bytes = qlb; row.relic_vrm = qv; row.relic_vrm_bytes = qvb;                   \
		row.targets[0] = s; row.targets[1] = g; row.targets[2] = p;                                                     \
		out.push_back(row);                                                                                             \
	}
#include <platform/native_track_mode_pairs.inc>
#undef MP
	return out;
}

std::vector<ApTrackModePairPins> ap_track_mode_pairs()
{
	std::shared_ptr<const Index> index;
	{
		RegistryState &s = registry_state();
		std::lock_guard<std::mutex> lock(s.mutex);
		index = s.index;
	}
	std::vector<ApTrackModePairPins> out = index ? index->mode_pairs : std::vector<ApTrackModePairPins>();
	for (auto &row : ap_track_builtin_mode_pairs())
		out.push_back(std::move(row));
	return out;
}
