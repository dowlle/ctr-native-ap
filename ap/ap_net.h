#ifndef AP_NET_H
#define AP_NET_H

// C interface to the Archipelago network client (apclientpp). Implemented in
// ap_net.cpp (C++), compiled as an isolated static lib and called from the C
// unity build (ap_hooks.c). Handles ws:// and wss:// (TLS) with compression;
// the transport is selected from the uri scheme passed to ap_net_init().

#ifdef __cplusplus
extern "C" {
#endif

// Construct the client. uri is "ws://host:port". Returns 0 on success.
int  ap_net_init(const char *uuid, const char *game, const char *uri);

// Set the slot (player) name + password; the actual Connect is sent on RoomInfo.
void ap_net_connect_slot(const char *slot, const char *password);

// Pump the socket; call once per (throttled) frame. Fires handlers inline.
void ap_net_poll(void);

// 1 once the slot handshake has completed.
int  ap_net_is_connected(void);

// Game -> server.
void ap_net_send_location(long long location_code); // LocationChecks([code])
void ap_net_send_goal(void);                         // StatusUpdate(GOAL)

// Drain newly-received item ids into out (capacity max). Returns count copied.
int  ap_net_drain_items(long long *out, int max);

// 1 once after each fresh slot-connect (new seed / reconnect / server switch),
// then self-clears. Caller zeroes per-session received-item tallies on a 1 so
// counts rebuild from the server's authoritative ReceivedItems list.
int  ap_net_take_recv_reset(void);

// ── Reward-glow / pad-state support (scouted on slot-connect) ──
// 1 (and fills any non-null out params) if a scout result is known for
// location_code: the item placed there, the player who receives it, and AP
// item flags (bit0 = progression). A scouted item with player == ap_net_self_slot
// is an own CTR reward (render its model); otherwise a foreign AP item (marker).
int  ap_net_scout_known(long long location_code, long long *out_item,
                        int *out_player, unsigned *out_flags);

// Resolve the scouted item at location_code into display strings: the item name
// (item_buf) and the receiving player's alias (player_buf), each truncated to fit
// its buffer and null-terminated. Returns 1 if the location is scouted, 0
// otherwise (both buffers are set to "" on a 0). A name not yet present in the
// synced DataPackage comes back as "Unknown"; the caller substitutes a generic.
int  ap_net_scout_text(long long location_code, char *item_buf, int item_n,
                       char *player_buf, int player_n);

// 1 if location_code has already been checked on the server (own slot). Drives
// the "done / chill" warp-pad colouring vs the "new / available" glow.
int  ap_net_location_checked(long long location_code);

// Connected slot number, or -1 before connect.
int  ap_net_self_slot(void);

// Coarse connection status for the in-game connection manager. Tracked by the
// socket / slot handlers in ap_net.cpp; ap_hooks.c's AP_Net_StatusLine maps these
// to a one-line string for the menu.
enum
{
	AP_NET_STATUS_IDLE = 0,   // no client / not dialed
	AP_NET_STATUS_CONNECTING, // socket up or slot handshake pending
	AP_NET_STATUS_CONNECTED,  // slot connected
	AP_NET_STATUS_ERROR       // slot refused (see ap_net_last_error)
};
int  ap_net_status(void);
const char *ap_net_last_error(void); // last slot-refused reason, "" if none

void ap_net_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // AP_NET_H
