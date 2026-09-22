#ifndef NATIVE_LINK_HANDOFF_H
#define NATIVE_LINK_HANDOFF_H

// Confirmed one-click-connect handoff (issue #334, implementation slice 3).
//
// The pure decision logic the running client applies to a claimed request. It
// owns no sockets, files, config or UI; the AP glue (ap/ap_link.c) feeds it the
// claimed records, the session state and the pad edges, and carries out the
// action it returns. A host harness drives the same code.
//
// Rules (product rulings of 2026-09-20 and 2026-09-22):
//   * A request with the identity of the live session (connected, connecting or
//     retrying) is coalesced: no prompt, no reconnect, no config write. Being the
//     newest request, it also ends the wait of an older request for another room.
//   * Any other request waits for a safe main menu. Reaching the menu does not
//     approve it. While the client has a live session the player is asked
//     "Connect to <host>:<port>, slot <name>?" (the room ID is matched, not
//     shown); declining keeps the current session and writes nothing. When
//     no session is live (never dialed, retry stopped, refused) the request is
//     admitted at the safe main menu without a prompt, and at boot before the
//     first dial.
//   * The newest distinct request wins until the player accepts. A newer
//     request replaces the one on screen and re-arms the prompt, so a press
//     meant for the old request cannot accept the new one.
//   * A request expires 15 minutes after its creation. Expiry is checked when it
//     is offered, every frame it waits and on the frame of acceptance; after the
//     claim it is tracked on the monotonic clock so a wall-clock change cannot
//     extend it.

#include "platform/native_launch_request.h"
#include "platform/native_link_pending.h"

#ifdef __cplusplus
extern "C" {
#endif

// Frames a prompt must have been on screen before an accept or decline press
// counts. A button already held when the menu appears, or pressed for a request
// that was just replaced, never answers the prompt.
#define NATIVE_LINK_PROMPT_ARM_FRAMES 20

typedef struct
{
	int haveActive;               // identity of the current or last dialed session
	NativeLaunchRequest active;   // room "" = not known (dialed from saved settings)

	int haveOffer;                // a request waiting for the main menu
	NativeLinkRecord offer;
	long long offerDeadlineMonoMs;
	int promptFrames;             // frames the prompt for this offer has been shown
	int noticeDue;                // one "request waiting" notice per waiting offer
} NativeLinkHandoff;

typedef enum
{
	NATIVE_LINK_OFFER_EXPIRED = 0,         // too old; dropped
	NATIVE_LINK_OFFER_COALESCED_ACTIVE,    // same as the live session; dropped with any waiting one
	NATIVE_LINK_OFFER_COALESCED_WAITING,   // same as the waiting request; oldest kept
	NATIVE_LINK_OFFER_WAITING,             // now waiting (nothing waited before)
	NATIVE_LINK_OFFER_REPLACED             // replaced the waiting request
} NativeLinkOfferResult;

typedef struct
{
	int safe;          // at the settled top-level main menu, nothing loading
	int sessionLive;   // connected, connecting or retrying
	long long nowMonoMs;
	int acceptTapped;  // fresh press this frame
	int declineTapped; // fresh press this frame
} NativeLinkFrameInput;

typedef enum
{
	NATIVE_LINK_FRAME_IDLE = 0,  // nothing waiting
	NATIVE_LINK_FRAME_WAITING,   // a request waits for a safe main menu
	NATIVE_LINK_FRAME_PROMPT,    // show the prompt for *shown
	NATIVE_LINK_FRAME_ADMIT,     // no live session: connect to *taken now
	NATIVE_LINK_FRAME_ACCEPT,    // the player accepted: switch to *taken now
	NATIVE_LINK_FRAME_DECLINED,  // the player kept the current session
	NATIVE_LINK_FRAME_EXPIRED    // the waiting request expired; dropped
} NativeLinkFrameAction;

void NativeLinkHandoff_Init(NativeLinkHandoff *h);

// The session the client dials without a link (saved settings at boot, or the
// Connection page's Connect row). NULL clears it.
void NativeLinkHandoff_SetActive(NativeLinkHandoff *h, const NativeLaunchRequest *identity);

NativeLinkOfferResult NativeLinkHandoff_Offer(NativeLinkHandoff *h, const NativeLinkRecord *record,
                                              long long nowUnixMs, long long nowMonoMs, int sessionLive);

// One frame. On ADMIT and ACCEPT the request is copied to *taken, becomes the
// active identity and stops waiting. On PROMPT it is copied to *shown. Either
// pointer may be NULL.
NativeLinkFrameAction NativeLinkHandoff_Frame(NativeLinkHandoff *h, const NativeLinkFrameInput *in,
                                              NativeLinkRecord *taken, NativeLinkRecord *shown);

// 1 once for each waiting request while the client is not at a safe menu and
// has a live session, so the glue raises a single credential-free notice.
int NativeLinkHandoff_TakeNotice(NativeLinkHandoff *h);

// Identity of a saved connection: uri "host:port", "ws://host:port" or
// "wss://host:port" (bracketed IPv6 hosts included) plus the slot. The room is
// left empty (not known). Returns 0 when the uri has no parseable host and port.
int NativeLinkIdentity_FromConnection(const char *uri, const char *slot, NativeLaunchRequest *out);

// The saved-connection uri for an accepted request: "host:port", with no
// scheme, which is the same endpoint policy a player gets by typing the address
// into the Connection page (the client tries secure first, then plain).
// Returns 1 when it fits.
int NativeLinkIdentity_ToConnectionUri(const NativeLaunchRequest *request, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif // NATIVE_LINK_HANDOFF_H
