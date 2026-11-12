#ifndef AP_TRACK_REGISTRY_H
#define AP_TRACK_REGISTRY_H

// Live track registry lookup for Current Seed (one generator row 10k).
//
// The public registry dowlle/ctr-track-registry lists each custom track's
// names, credits, file hashes and where Project Saphi publishes it. It never
// holds track files. The client fetches registry.json at run time, so a track
// registered after a client release can still be downloaded.
//
// Trust: the registry only says where on Project Saphi a track lives. The
// room pins every package's exact LEV and VRM hashes and sizes, a lookup
// matches on those pins, and the download installs nothing unless the bytes
// equal them. A wrong or tampered registry can only make a download fail,
// never install other files, so the registry is not signed.
//
// Source: HTTPS only, from one fixed GitHub raw URL (host allow-list in the
// transport), size cap AP_TRACK_REGISTRY_MAX_BYTES, the transport's timeout.
// The last good copy is kept on disk and used when GitHub cannot be reached.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AP_TRACK_REGISTRY_HOST "raw.githubusercontent.com"
#define AP_TRACK_REGISTRY_PATH "/dowlle/ctr-track-registry/main/registry.json"
#define AP_TRACK_REGISTRY_MAX_BYTES (4u * 1024u * 1024u)

// Where the current index came from.
enum
{
	AP_TRACK_REGISTRY_SOURCE_NONE = 0,
	AP_TRACK_REGISTRY_SOURCE_LIVE,   // fetched from GitHub this session
	AP_TRACK_REGISTRY_SOURCE_CACHED  // the last good copy on disk
};

// What the refresh is doing.
enum
{
	AP_TRACK_REGISTRY_IDLE = 0,    // never started
	AP_TRACK_REGISTRY_LOADING,     // a refresh is running
	AP_TRACK_REGISTRY_DONE,        // finished; the source says what is in use
	AP_TRACK_REGISTRY_UNAVAILABLE  // finished with no live copy and no cache
};

// One lookup's answer.
enum
{
	AP_TRACK_REGISTRY_NO_INDEX = 0, // nothing loaded (yet)
	AP_TRACK_REGISTRY_MATCH,        // registered, with a Project Saphi LEV and VRM
	AP_TRACK_REGISTRY_NOT_ON_SAPHI, // registered, but no Saphi download for this revision
	AP_TRACK_REGISTRY_NOT_LISTED,   // no registered revision has these two hashes
	AP_TRACK_REGISTRY_PIN_CONFLICT  // the hashes match but a listed size differs from the pin
};

struct ApTrackRegistryMatch
{
	int saphi_track_id;
	int lev_download_id;
	int vrm_download_id;
	char track_key[65];
	char revision[65];
};

// Start one background refresh unless one is running: load the cached copy at
// cache_path (published at once when valid), fetch the live registry, and on
// success publish it and replace the cache. Returns 1 when a worker started.
int ap_track_registry_refresh(const char *cache_path);

// The refresh state (AP_TRACK_REGISTRY_IDLE...) and the source of the index in
// use. detail gets a short reason when the live fetch failed.
int ap_track_registry_status(int *source, char *detail, int detail_bytes);

// Changes whenever a new index is published, so callers can cache lookups.
uint32_t ap_track_registry_generation(void);

// Look up a room package by its pinned hashes and sizes. Hashes compare
// without case. Fills out on AP_TRACK_REGISTRY_MATCH. Returns an
// AP_TRACK_REGISTRY_NO_INDEX... value.
int ap_track_registry_lookup(const char *lev_sha256, uint32_t lev_bytes,
                             const char *vrm_sha256, uint32_t vrm_bytes,
                             struct ApTrackRegistryMatch *out);

// The registry track key for a Saphi track id (permission check, A4-74).
// Returns 1 and fills key when exactly one registry track names that id.
int ap_track_registry_key_for_saphi(int saphi_track_id, char *key, int key_bytes);

#ifdef __cplusplus
}

#include <string>
#include <vector>

// The HTTPS transport, defined with the Saphi downloader. host must be on its
// allow-list (Project Saphi, GitHub raw); anything else is refused.
bool ap_https_get(const char *host, const std::string &path, size_t max_bytes,
                  std::vector<unsigned char> &body, std::string &error);

// Parse and check registry bytes without publishing them (harnesses and the
// cache loader). Returns the number of downloadable revisions indexed, or -1
// with error set when the bytes are not a usable registry.
int ap_track_registry_parse_check(const std::vector<unsigned char> &bytes, std::string &error);

// The names a registry track credits, in registry order (permission check,
// A4-74). False when the track is unknown or a credit is unreadable.
bool ap_track_registry_credits(const std::string &track_key, std::vector<std::string> &names);
#endif

#endif
