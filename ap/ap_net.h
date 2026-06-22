#ifndef AP_NET_H
#define AP_NET_H

// C interface to the Archipelago network client (apclientpp). Implemented in
// ap_net.cpp (C++), compiled as an isolated static lib and called from the C
// unity build (ap_hooks.c). ws:// only -- no TLS -- for local testing.

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

void ap_net_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // AP_NET_H
