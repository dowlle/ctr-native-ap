// Live track registry lookup for Current Seed (one generator row 10k). See
// ap_track_registry.h for the trust model. The fetch uses the Saphi
// downloader's HTTPS transport (ap_https_get), which refuses every host but
// Project Saphi and GitHub raw.

#include "ap_track_registry.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
using Index = std::vector<Revision>;

struct RegistryState
{
	std::mutex mutex;
	int state = AP_TRACK_REGISTRY_IDLE;
	int source = AP_TRACK_REGISTRY_SOURCE_NONE;
	std::string detail;
	std::shared_ptr<const Index> index;
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

bool parse_index(const std::vector<unsigned char> &bytes, Index &index, std::string &error)
{
	index.clear();
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
		const nlohmann::json *revisions = member(track, "revisions");
		const nlohmann::json *prov = member(track, "provenance");
		const nlohmann::json *saphi = prov ? member(*prov, "saphi") : nullptr;
		const nlohmann::json *id = saphi ? member(*saphi, "track_id") : nullptr;
		long long track_id = 0;
		if (!revisions || !revisions->is_array() || revisions->size() > kMaxRevisions) continue;
		if (!id || !positive(*id, kSaphiIdMax, track_id)) track_id = 0;
		for (const auto &revision : *revisions)
		{
			Revision row;
			const nlohmann::json *files = member(revision, "files");
			if (!file_pin(files, "lev", row.lev, row.lev_size) || !file_pin(files, "vrm", row.vrm, row.vrm_size))
				continue;
			row.track_id = static_cast<int>(track_id);
			row.key = short_text(member(track, "track_key"));
			row.revision = short_text(member(revision, "revision"));
			saphi_downloads(revision, row);
			index.push_back(std::move(row));
		}
	}
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
}

void refresh_worker(std::string cache_path)
{
	std::vector<unsigned char> cached, live;
	std::string error;
	Index index;
	bool have_cache = false;
	try
	{
		if (!cache_path.empty() && read_capped(cache_path, cached) && parse_index(cached, index, error))
		{
			have_cache = true;
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
			publish(std::move(index), AP_TRACK_REGISTRY_SOURCE_LIVE);
			if (!cache_path.empty() && (!have_cache || live != cached) && !write_cache(cache_path, live))
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
	for (const auto &r : index)
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
	for (const auto &r : *index)
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
	s.generation++;
}
#endif
