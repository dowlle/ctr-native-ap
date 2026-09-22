#include "platform/native_link_handoff.h"

#include <stdio.h>
#include <string.h>

// Confirmed one-click-connect handoff (issue #334, slice 3). Pure decision
// logic; see the header for the rules. Freestanding so the host harness
// includes this unit directly.

void NativeLinkHandoff_Init(NativeLinkHandoff *h)
{
	if (h != NULL)
		memset(h, 0, sizeof *h);
}

void NativeLinkHandoff_SetActive(NativeLinkHandoff *h, const NativeLaunchRequest *identity)
{
	if (h == NULL)
		return;
	if (identity == NULL)
	{
		h->haveActive = 0;
		memset(&h->active, 0, sizeof h->active);
		return;
	}
	h->active = *identity;
	h->haveActive = 1;
}

static void linkDropOffer(NativeLinkHandoff *h)
{
	h->haveOffer = 0;
	h->promptFrames = 0;
	h->noticeDue = 0;
	memset(&h->offer, 0, sizeof h->offer);
	h->offerDeadlineMonoMs = 0;
}

NativeLinkOfferResult NativeLinkHandoff_Offer(NativeLinkHandoff *h, const NativeLinkRecord *record,
                                              long long nowUnixMs, long long nowMonoMs, int sessionLive)
{
	long long age;
	long long deadline;

	if (h == NULL || record == NULL)
		return NATIVE_LINK_OFFER_EXPIRED;
	if (NativeLinkRecord_Expired(record->createdUnixMs, nowUnixMs))
		return NATIVE_LINK_OFFER_EXPIRED;

	// From here on the remaining lifetime runs on the monotonic clock.
	age = nowUnixMs - record->createdUnixMs;
	if (age < 0)
		age = 0;
	deadline = nowMonoMs + (NATIVE_LINK_PENDING_TTL_MS - age);

	if (sessionLive && h->haveActive && NativeLinkRequest_SameIdentity(&h->active, &record->request))
	{
		// The newest request is the session the player already has. Latest wins,
		// so an older request for another room stops waiting as well.
		linkDropOffer(h);
		return NATIVE_LINK_OFFER_COALESCED_ACTIVE;
	}

	if (h->haveOffer && NativeLinkRequest_SameIdentity(&h->offer.request, &record->request))
	{
		// Repeated clicks keep one copy and never extend its expiry.
		if (record->createdUnixMs < h->offer.createdUnixMs)
		{
			h->offer.createdUnixMs = record->createdUnixMs;
			h->offer.token = record->token;
		}
		if (deadline < h->offerDeadlineMonoMs)
			h->offerDeadlineMonoMs = deadline;
		return NATIVE_LINK_OFFER_COALESCED_WAITING;
	}

	{
		const int replaced = h->haveOffer;
		linkDropOffer(h);
		h->haveOffer = 1;
		h->offer = *record;
		h->offerDeadlineMonoMs = deadline;
		h->noticeDue = 1;
		return replaced ? NATIVE_LINK_OFFER_REPLACED : NATIVE_LINK_OFFER_WAITING;
	}
}

static NativeLinkFrameAction linkTake(NativeLinkHandoff *h, NativeLinkRecord *taken, NativeLinkFrameAction action)
{
	if (taken != NULL)
		*taken = h->offer;
	NativeLinkHandoff_SetActive(h, &h->offer.request);
	linkDropOffer(h);
	return action;
}

NativeLinkFrameAction NativeLinkHandoff_Frame(NativeLinkHandoff *h, const NativeLinkFrameInput *in,
                                              NativeLinkRecord *taken, NativeLinkRecord *shown)
{
	if (h == NULL || in == NULL || !h->haveOffer)
		return NATIVE_LINK_FRAME_IDLE;

	// Expiry first, so a press on the frame the request runs out never accepts it.
	if (in->nowMonoMs >= h->offerDeadlineMonoMs)
	{
		linkDropOffer(h);
		return NATIVE_LINK_FRAME_EXPIRED;
	}

	if (!in->safe)
	{
		// Leaving the menu hides the prompt; it re-arms when the menu is back.
		h->promptFrames = 0;
		return NATIVE_LINK_FRAME_WAITING;
	}

	// Safe main menu. With no live session there is nothing to switch away from,
	// so the request is admitted, unless a prompt is already on screen: an answer
	// the player is being asked for is never taken away from them.
	if (!in->sessionLive && h->promptFrames == 0)
		return linkTake(h, taken, NATIVE_LINK_FRAME_ADMIT);

	h->noticeDue = 0;
	if (h->promptFrames >= NATIVE_LINK_PROMPT_ARM_FRAMES)
	{
		if (in->acceptTapped)
			return linkTake(h, taken, NATIVE_LINK_FRAME_ACCEPT);
		if (in->declineTapped)
		{
			linkDropOffer(h);
			return NATIVE_LINK_FRAME_DECLINED;
		}
	}
	else
	{
		h->promptFrames++;
	}

	if (shown != NULL)
		*shown = h->offer;
	return NATIVE_LINK_FRAME_PROMPT;
}

int NativeLinkHandoff_TakeNotice(NativeLinkHandoff *h)
{
	if (h == NULL || !h->haveOffer || !h->noticeDue)
		return 0;
	h->noticeDue = 0;
	return 1;
}

int NativeLinkIdentity_FromConnection(const char *uri, const char *slot, NativeLaunchRequest *out)
{
	const char *hostStart;
	const char *hostEnd;
	const char *portStart;
	const char *p;
	unsigned long port = 0;
	size_t hostLen;
	NativeLaunchRequest id;

	if (uri == NULL || slot == NULL || out == NULL)
		return 0;

	hostStart = uri;
	if (strncmp(hostStart, "ws://", 5) == 0)
		hostStart += 5;
	else if (strncmp(hostStart, "wss://", 6) == 0)
		hostStart += 6;
	else if (strstr(hostStart, "://") != NULL)
		return 0;

	if (*hostStart == '[')
	{
		hostEnd = strchr(hostStart, ']');
		if (hostEnd == NULL)
			return 0;
		hostEnd++;
		if (*hostEnd != ':')
			return 0;
	}
	else
	{
		hostEnd = strchr(hostStart, ':');
		if (hostEnd == NULL)
			return 0;
	}
	portStart = hostEnd + 1;

	hostLen = (size_t)(hostEnd - hostStart);
	if (hostLen == 0 || hostLen > NATIVE_LAUNCH_REQUEST_HOST_MAX)
		return 0;

	for (p = portStart; *p != '\0' && *p != '/'; p++)
	{
		if (*p < '0' || *p > '9')
			return 0;
		port = port * 10u + (unsigned long)(*p - '0');
		if (port > NATIVE_LAUNCH_REQUEST_PORT_MAX)
			return 0;
	}
	if (p == portStart || port < NATIVE_LAUNCH_REQUEST_PORT_MIN)
		return 0;
	if (*p == '/' && p[1] != '\0')
		return 0;

	if (strlen(slot) > NATIVE_LAUNCH_REQUEST_SLOT_MAX)
		return 0;

	memset(&id, 0, sizeof id);
	memcpy(id.host, hostStart, hostLen);
	id.host[hostLen] = '\0';
	id.port = (unsigned int)port;
	memcpy(id.slot, slot, strlen(slot) + 1);
	id.room[0] = '\0';
	*out = id;
	return 1;
}

int NativeLinkIdentity_ToConnectionUri(const NativeLaunchRequest *request, char *out, size_t cap)
{
	int n;

	if (request == NULL || out == NULL || cap == 0)
		return 0;
	n = snprintf(out, cap, "%s:%u", request->host, request->port);
	return n > 0 && (size_t)n < cap;
}
