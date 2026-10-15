#ifndef AP_UPDATE_H
#define AP_UPDATE_H

// Signed in-game updater (issue #301): the C surface of the network worker in
// ap_update_net.cpp. The trust decisions live in ap_update_core.cpp; this
// header only moves work on and off the background thread. Every function is
// safe to call from the game thread; none blocks on the network.
//
// Not built into the box authoring download (CTR_AP_AUTHORING): its archive is
// not covered by the signed manifest and it must never replace itself with the
// player client.

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	AP_UPD_IDLE = 0,        // nothing started
	AP_UPD_CHECKING,        // releases/latest request in flight
	AP_UPD_NONE,            // no offer: up to date, declined, offline or any check failure (silent)
	AP_UPD_OFFER,           // a newer signed-capable release can be offered
	AP_UPD_DOWNLOADING,     // player confirmed; progress in permille
	AP_UPD_VERIFYING,
	AP_UPD_READY,           // verified and staged beside the executable
	AP_UPD_FAILED_VERIFY,   // signature, hash or archive check failed; nothing kept
	AP_UPD_FAILED_DOWNLOAD, // network failure after confirmation; nothing kept
	AP_UPD_FAILED_INSTALL,  // staging or swap failed; the installed build is unchanged
	AP_UPD_INSTALLED        // swap done; the next start runs the new build
};

// Startup, once: clean stale staged files and report whether this launch is
// the first one after an update (1), a manual rollback (2) or neither (0).
// `running_tag` is CTR_AP_COMPAT_VERSION.
int ap_update_startup(const char *running_tag);
// The new build reached the main menu: drop the rollback copy and marker.
void ap_update_confirm_started(void);

// Start the asynchronous check. `declined` is the remembered declined tag or "".
void ap_update_check_start(const char *running_tag, const char *declined);

// Snapshot: returns AP_UPD_*; copies the offered tag; progress in 0..1000 or -1.
int ap_update_status(char *tag, int tag_bytes, int *progress_permille);
// One short reason for the last failure, for the log only.
void ap_update_detail(char *out, int out_bytes);

// Player confirmed the offer.
void ap_update_download_start(void);
// Player cancelled the download: the UI returns at once, the worker's result
// is discarded and nothing is written.
void ap_update_cancel(void);
// Player dismissed a failure or the offer was declined: back to AP_UPD_NONE.
void ap_update_dismiss(void);

// Install the staged build (renames only). 1 on success (AP_UPD_INSTALLED).
int ap_update_apply(void);
// Start the installed executable as a new process (direct launches only).
int ap_update_relaunch(void);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
// Game-thread glue (ap/ap_update.c, AP unity build).
struct GameTracker;
struct RectMenu;
// Every frame from AP_OnFrame: startup cleanup and the check on the first call.
void AP_UpdateTick(struct GameTracker *gGT);
// The main-menu proc reports each frame the top-level menu is settled.
void AP_UpdateMainMenuSettled(struct RectMenu *menu);
// From RECTMENU_ProcessState: 1 when the update prompt owns this frame.
int AP_UpdateMenuFrame(void);
#endif

#endif
