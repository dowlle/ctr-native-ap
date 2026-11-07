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

// Legacy pinned room-folder download, retained for compatibility tests.
// New Current Seed installs use ap_custom_track_download_start_shared below.
int ap_custom_track_download_start_pinned(const char *tracks_dir,
	                                      const char *package_root,
	                                      const char *download_api_url,
	                                      const char *package_version,
	                                      const char *lev_sha256, uint32_t lev_bytes,
	                                      const char *vrm_sha256, uint32_t vrm_bytes);

// Legacy registry download into a room folder. New Current Seed installs
// use the shared store API; this entry point remains for compatibility tests.
int ap_custom_track_download_start_registry(const char *tracks_dir,
	                                        const char *package_root,
	                                        int saphi_track_id, int lev_download_id, int vrm_download_id,
	                                        const char *lev_sha256, uint32_t lev_bytes,
	                                        const char *vrm_sha256, uint32_t vrm_bytes);

/* Current Seed's one-copy install into tracks/packages. The room descriptor
   supplies the revision and UUID; the downloaded bytes still have to match
   both seed pins before a package is published. */
int ap_custom_track_download_start_shared(const char *tracks_dir, const char *package_root,
    const char *download_api_url, const char *revision, const char *uuid,
    const char *title, const char *author, unsigned int laps,
    int relic_sapphire, int relic_gold, int relic_platinum,
    const char *lev_sha256, uint32_t lev_bytes, const char *vrm_sha256, uint32_t vrm_bytes,
    int saphi_track_id, int lev_download_id, int vrm_download_id);

// Thread-safe snapshot for the game thread. Returns an AP_CT_DOWNLOAD_* value.
int ap_custom_track_download_status(char *detail, int detail_bytes);

// Any active Saphi rate-limit wait, across all transfer workers. Returns the
// seconds remaining (rounded up), or 0 and empty text when transfers resume.
int ap_custom_track_download_busy(char *detail, int detail_bytes);

#ifdef __cplusplus
}
#endif

#endif
