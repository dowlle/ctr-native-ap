#include "platform/native_link_route.h"

#include <stddef.h>

// Startup routing for a one-click-connect launch (issue #334, slice 3). See the
// header.

NativeLinkRoute NativeLinkRoute_Decide(const NativeLinkRouteInput *in)
{
	if (in == NULL || !in->storeUsable)
		return NATIVE_LINK_ROUTE_RUN;
	if (!in->isPrimary)
		return in->haveRequest ? NATIVE_LINK_ROUTE_HAND_OFF : NATIVE_LINK_ROUTE_ALREADY_RUNNING;
	if (!in->haveRequest)
		return NATIVE_LINK_ROUTE_RUN;
	return NATIVE_LINK_ROUTE_RUN_WITH_REQUEST;
}
