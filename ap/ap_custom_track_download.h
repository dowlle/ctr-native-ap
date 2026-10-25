#ifndef AP_CUSTOM_TRACK_DOWNLOAD_H
#define AP_CUSTOM_TRACK_DOWNLOAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	AP_CT_DOWNLOAD_IDLE = 0,
	AP_CT_DOWNLOAD_RUNNING,
	AP_CT_DOWNLOAD_SUCCEEDED,
	AP_CT_DOWNLOAD_FAILED
};

// Start one background, HTTPS-only Project Saphi install. The public metadata
// endpoint is resolved at runtime so a media-row ID can change without pinning
// Alpha6 to stale numeric download URLs. Files are staged as .part, checked
// against the release-owned SHA-256 identities, then installed as track.lev and
// track.vrm. Returns 1 when a worker was started.
int ap_custom_track_download_start(const char *package_root,
	                               const char *download_api_url,
	                               const char *package_version,
	                               const char *lev_sha256,
	                               const char *vrm_sha256);

// Current Seed (#133 PR 7): the same download for one compiled release
// profile a content-plan room requires, bound to the room's size pins. The
// package root must be exactly <tracks_dir>/<one safe folder>. Saphi's listed
// sizes and the downloaded bytes must equal the pins and the hashes must match
// before anything is written; the folder is created only then, and an
// existing track.lev / track.vrm is kept when it already is the pinned file
// and refuses the install when it is not. Returns 1 when a worker was started.
int ap_custom_track_download_start_pinned(const char *tracks_dir,
	                                      const char *package_root,
	                                      const char *download_api_url,
	                                      const char *package_version,
	                                      const char *lev_sha256, uint32_t lev_bytes,
	                                      const char *vrm_sha256, uint32_t vrm_bytes);

// Thread-safe snapshot for the game thread. Returns an AP_CT_DOWNLOAD_* value.
int ap_custom_track_download_status(char *detail, int detail_bytes);

#ifdef __cplusplus
}
#endif

#endif
