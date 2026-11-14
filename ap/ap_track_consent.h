#ifndef AP_TRACK_CONSENT_H
#define AP_TRACK_CONSENT_H

// Track author permission check (0.3.0-alpha4). The client offers a custom
// track for in-game download only when its author said yes.
//
// One resolver, ap_track_permission, answers allow, deny or unknown from, in
// order:
//   1. the author's recorded answer in the public registry's consent.json
//      (dowlle/ctr-track-registry), fetched live like registry.json: a track
//      entry first, then the author's; granted allows, declined or withdrawn
//      denies. This is what the author told the maintainer directly, so it
//      wins over Project Saphi's setting either way (A4-79b),
//   2. only when consent.json has no applicable answer: Project Saphi's own
//      permission value (the adapter in platform/native_saphi_catalogue.cpp
//      reads public_non_profit from the track's permissions object;
//      unspecified or malformed is unknown),
//   3. otherwise unknown.
// With several credited authors every one must allow; each uses their own
// consent.json answer if any, else Saphi's track value.
// Unknown and deny both mean: not offered for download. Nothing on the
// player's disk is ever deleted or disabled by this check; it only decides
// whether the client downloads, updates or lists a track for download.
//
// Source: HTTPS only, one fixed GitHub raw URL (the transport's host
// allow-list), size cap AP_TRACK_CONSENT_MAX_BYTES, the transport's timeout.
// The last good copy is kept on disk and used only when GitHub cannot be
// reached. A missing (HTTP 4xx) or unreadable live file, or no live copy and
// no cache, leaves every track unknown.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AP_TRACK_CONSENT_HOST "raw.githubusercontent.com"
#define AP_TRACK_CONSENT_PATH "/dowlle/ctr-track-registry/main/consent.json"
#define AP_TRACK_CONSENT_MAX_BYTES (1024u * 1024u)
#define AP_TRACK_CONSENT_CACHE_NAME "track-consent-cache.json"

// The resolver's answer. CUSTOM_SAPHI_PERMISSION_* in
// native_saphi_catalogue.h uses the same values.
enum
{
	AP_TRACK_PERMISSION_UNKNOWN = 0,
	AP_TRACK_PERMISSION_ALLOW = 1,
	AP_TRACK_PERMISSION_DENY = 2
};

// Where the permission list in use came from.
enum
{
	AP_TRACK_CONSENT_SOURCE_NONE = 0,
	AP_TRACK_CONSENT_SOURCE_LIVE,  // fetched from GitHub this session
	AP_TRACK_CONSENT_SOURCE_CACHED // the last good copy on disk
};

// What the refresh is doing.
enum
{
	AP_TRACK_CONSENT_IDLE = 0,    // never started
	AP_TRACK_CONSENT_LOADING,     // a refresh is running
	AP_TRACK_CONSENT_DONE,        // finished; the source says what is in use
	AP_TRACK_CONSENT_UNAVAILABLE  // finished with no live copy and no cache
};

// Start one background refresh unless one is running: fetch consent.json and
// on success publish it and replace the cache at cache_path; when GitHub
// cannot be reached, publish the cache instead. Returns 1 when a worker started.
int ap_track_consent_refresh(const char *cache_path);

// The same, with the cache next to tracks/ in the assets folder.
int ap_track_consent_refresh_assets(const char *assets);

// The refresh state (AP_TRACK_CONSENT_IDLE...) and the source of the list in
// use. detail gets a short reason when the live fetch failed.
int ap_track_consent_status(int *source, char *detail, int detail_bytes);

// Changes whenever a new list is published or a Saphi author is learned.
uint32_t ap_track_consent_generation(void);

// What the Saphi catalogue says about a track: its author id and Saphi's
// own permission value. Lets a seed or an installed package that only knows
// its Saphi track id be matched to its author.
void ap_track_consent_note_saphi(int saphi_track_id, int saphi_author_id, int saphi_permission);

// The Saphi author id learned for a track id, 0 when unknown.
int ap_track_consent_saphi_author(int saphi_track_id);

// The permission check. Any argument may be unknown (0, or NULL/""): the
// author id and Saphi value are then taken from what the catalogue taught,
// the track key from the registry by Saphi track id. Returns an
// AP_TRACK_PERMISSION_* value.
int ap_track_permission(int saphi_track_id, int saphi_author_id, int saphi_permission, const char *track_key);

// The Saphi track id at the end of ".../tracks/<id>/downloads" or
// ".../tracks/<id>", 0 when the URL has none.
int ap_track_saphi_id_from_url(const char *url);

// One plain line for a track that is not allowed, by the list's state:
// checking, could not be loaded, or the author has not allowed it.
const char *ap_track_permission_text(void);

#ifdef __cplusplus
}

#include <memory>
#include <string>
#include <vector>

// The decision without the shared state (harnesses, the resolver itself).
// A null list means no permission list is loaded.
struct ApTrackConsentList;
bool ap_track_consent_parse(const std::vector<unsigned char> &bytes, std::shared_ptr<const ApTrackConsentList> &out,
                            std::string &error);
int ap_track_consent_decide(const ApTrackConsentList *list, int saphi_permission, int saphi_author_id,
                            const std::vector<std::string> &credit_names, const std::string &track_key);
#endif

#endif
