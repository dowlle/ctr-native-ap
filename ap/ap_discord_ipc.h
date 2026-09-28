#ifndef AP_DISCORD_IPC_H
#define AP_DISCORD_IPC_H

// Discord Rich Presence IPC client (issue #366). Implemented in
// ap/ap_discord_ipc.cpp, part of the isolated C++ ap_net library so the unity C
// build stays thread-free. See ap/ap_discord_logic.h for the overall design.
//
// Threading: every call below is safe from the game thread and never waits for
// Discord. All pipe and socket I/O happens on one worker thread, started the
// first time the feature is enabled with an application ID. The calls only take
// a mutex that the worker holds while it copies state in or out, never across
// I/O. While the option has never been switched on, no thread exists and
// nothing is opened.

#include "ap_discord_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enable or disable presence. app_id NULL or "" means none (inert). Starts the
// worker the first time it is enabled with an ID. Disabling clears the activity
// and closes the connection; so does changing the ID (it reconnects with the
// new one). Cheap when nothing changed.
void ap_discord_configure(int enabled, const char *app_id);

// Hand the worker the activity to show. Copied; only a change is sent, at most
// once per 15 seconds.
void ap_discord_publish(const ApDiscordActivity *activity);

// AP_DISCORD_STATUS_* as last seen by the worker.
int ap_discord_status(void);

// Increments each time Discord refuses an activity update. The first refusal on
// a connection also stores Discord's message (truncated to fit buf).
unsigned ap_discord_error_count(char *buf, int n);

// Clean exit: clear the activity and close, waiting at most timeout_ms. Returns
// at once when the worker never started.
void ap_discord_shutdown(unsigned timeout_ms);

// JSON payloads, exposed for tools/test-discord-ipc.cpp. Return the length
// written (without the terminator), or -1 when buf is too small.
int ap_discord_build_handshake(const char *app_id, char *buf, int n);
// activity NULL = clear the activity.
int ap_discord_build_set_activity(const ApDiscordActivity *activity, long pid,
                                  unsigned nonce, char *buf, int n);

// Tests only: shorten the scheduler's timings, and (Unix) search for the
// socket in this one directory instead of the usual runtime directories.
// Call before the first ap_discord_configure.
void ap_discord_test_set_timing(unsigned first_retry_ms, unsigned retry_ms,
                                unsigned min_send_ms, unsigned poll_ms);
void ap_discord_test_set_socket_dir(const char *dir);

#ifdef __cplusplus
}
#endif

#endif // AP_DISCORD_IPC_H
