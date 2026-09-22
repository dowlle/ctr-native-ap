#ifndef NATIVE_LINK_ROUTE_H
#define NATIVE_LINK_ROUTE_H

// Startup routing for a one-click-connect launch (issue #334, slice 3).
//
// Decides, from facts main.c gathers before anything else starts, what a
// process launched with (or without) a ctr-ap request does. Pure, so the host
// harness checks every combination; main.c carries the decision out.
//
// A process that hands its request to a running client, or that finds a
// running client without carrying a request, exits before it loads config.ini,
// validates or mounts a disc, opens a window or starts networking. Only one
// game process runs per install.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	int haveRequest;  // a valid ctr-ap request was on the command line
	int storeUsable;  // the per-install state directory is usable
	int isPrimary;    // this process holds the primary lock
} NativeLinkRouteInput;

typedef enum
{
	// Start normally. Without a request that is today's startup; with one it is
	// the fallback when the store cannot be used: the request stays in memory
	// for this process only.
	NATIVE_LINK_ROUTE_RUN = 0,
	// Start as the primary and take the request at boot, before the first dial.
	NATIVE_LINK_ROUTE_RUN_WITH_REQUEST,
	// Another client owns this install: publish the request and exit.
	NATIVE_LINK_ROUTE_HAND_OFF,
	// Another client owns this install and there is no request to hand over:
	// exit successfully without starting a second game process.
	NATIVE_LINK_ROUTE_ALREADY_RUNNING
} NativeLinkRoute;

NativeLinkRoute NativeLinkRoute_Decide(const NativeLinkRouteInput *in);

#ifdef __cplusplus
}
#endif

#endif // NATIVE_LINK_ROUTE_H
