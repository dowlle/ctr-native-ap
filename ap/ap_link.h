#ifndef AP_LINK_H
#define AP_LINK_H

// One-click-connect glue for the running client (issue #334, slice 3).
//
// Carries out what the pure handoff (platform/native_link_handoff.h) decides:
// claims requests from the per-install store, raises the "request waiting"
// notice, draws the "Connect to <host>:<port>, slot <name>?" prompt on the
// settled main menu, and on acceptance saves the connection and re-dials
// through AP_Net_Reconnect. Nothing is written and no socket is touched for a request
// that is declined, expires or matches the session already running.

#include <stdint.h>

#include "platform/native_link_handoff.h"
#include "platform/native_link_host.h"

struct RectMenu;
struct GameTracker;

// Startup (main.c). The state directory this process uses; primary != 0 when it
// holds the primary lock and so claims requests other launches publish.
NativeLinkHost *AP_LinkHost(void);
void AP_LinkAttach(int primary);

// A request from this process's own command line, taken at boot before the
// first dial.
void AP_LinkSetBootRequest(const NativeLaunchRequest *request);

// Boot dial (AP_NetTick, first frame). 1 when a request was admitted and the
// client is already dialing it, so the saved connection must not be dialed.
int AP_LinkBootAdmit(void);

// Every frame from AP_OnFrame: poll the store, expire, notices.
void AP_LinkTick(struct GameTracker *gGT);

// The main-menu proc reports each frame the top-level menu is settled on screen.
void AP_LinkMainMenuSettled(struct RectMenu *menu);

// From RECTMENU_ProcessState, after input was collected. 1 when the prompt owns
// this frame: the menu underneath neither runs nor sees the buttons.
int AP_LinkMenuFrame(void);

// A dial the player made without a link (saved settings at boot, Connection
// page's Connect row), so a later link for the same session is recognised.
void AP_LinkNoteDial(const char *uri, const char *slot);

// Title-screen hint after a linked room refused the (empty) password.
void AP_LinkDrawTitleHint(uint32_t *ot);

#endif // AP_LINK_H
