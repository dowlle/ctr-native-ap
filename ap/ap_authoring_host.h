#ifndef AP_AUTHORING_HOST_H
#define AP_AUTHORING_HOST_H

// The authoring client without Archipelago (CTR_AP off, and any of
// CTR_CUSTOM_PACKAGES, CTR_BOX_AUTHORING or CTR_AI_LAP_RECORDER on).
//
// The authoring modules (ap_author.c, ap_spawn.c, ap_marker_model.c,
// ap_navrec.c, HOWL_CustomMusic.c and the Arcade custom pages) were written
// inside the AP layer and call a handful of its services. In an AP build
// ap_hooks.c provides them. Without CTR_AP, ap_authoring_host.c provides the
// same few names, so those modules compile unchanged in both builds:
//
//   AP_LogLine                     the log sink (stdout plus ctr-authoring.log)
//   AP_CustomOfflineLaunchAllowed  Arcade custom pages may always launch: there
//                                  is no seed that could own the session
//   Authoring_OnFrame              the per-frame order AP_OnFrame uses for the
//                                  same modules (marker model, author, spawn
//                                  loader, lap recorder)
//
// Nothing here connects to a room, reads slot_data or sends a check.

#if !defined(CTR_AP) && (defined(CTR_CUSTOM_PACKAGES) || defined(CTR_BOX_AUTHORING) || defined(CTR_AI_LAP_RECORDER))

struct GameTracker;

void AP_LogLine(const char *msg);

#ifdef CTR_CUSTOM_PACKAGES
int AP_CustomOfflineLaunchAllowed(void);
#endif

#if defined(CTR_BOX_AUTHORING) || defined(CTR_AI_LAP_RECORDER)
// Called once per frame from MainMain.c, where an AP build calls AP_OnFrame.
void Authoring_OnFrame(struct GameTracker *gGT);
#endif

#endif

#endif // AP_AUTHORING_HOST_H
