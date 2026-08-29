#ifndef AP_CUSTOM_TRACK_DOWNLOAD_H
#define AP_CUSTOM_TRACK_DOWNLOAD_H

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

// Thread-safe snapshot for the game thread. Returns an AP_CT_DOWNLOAD_* value.
int ap_custom_track_download_status(char *detail, int detail_bytes);

#ifdef __cplusplus
}
#endif

#endif
