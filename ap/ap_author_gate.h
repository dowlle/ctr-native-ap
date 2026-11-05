#ifndef AP_AUTHOR_GATE_H
#define AP_AUTHOR_GATE_H

// Box Author Mode in the player client: when it may be on, and which file
// each side reads and writes.
//
// The player client edits boxes only while no Archipelago room is connected.
// Turning the mode on is refused while a room is connecting, connected or
// being retried, and while a Play Solo session runs; every dial (Connect, a
// room link, Play Solo) is refused while it is on. The mode is a session
// switch, not a saved option: every launch starts with it off, so a saved
// room still dials at startup and nothing left on blocks it.
//
// FILES. The runtime boxes of a seed read AP_PLACEMENT_FILE (the player's
// "ap-box-placements.json", a wholesale override of the built-in set) and
// author mode reads and writes AP_AUTHOR_FILE ("ap-box-placements-authoring.json",
// the same name the box authoring download uses). Author mode never writes the
// player's file, and the runtime never reads the authoring file: the table
// is reloaded from the player's file when the mode goes off, and no seed can
// connect while it is on. In the box authoring builds the two names are the
// same file, as before.
//
// Freestanding (C99, no engine headers), so tools/test-author-gate.c checks
// exactly what the client runs.

#define AP_AUTHOR_FILE "ap-box-placements-authoring.json"
#ifdef CTR_BOX_AUTHORING
#define AP_PLACEMENT_FILE AP_AUTHOR_FILE
#else
#define AP_PLACEMENT_FILE "ap-box-placements.json"
#endif

// The file the placement table is read from: the authoring file while author
// mode holds the table, else the player's.
static inline const char *AP_AuthorGate_ReadFile(int authorTable)
{
	return authorTable ? AP_AUTHOR_FILE : AP_PLACEMENT_FILE;
}

// The only file author mode writes, in every build.
static inline const char *AP_AuthorGate_WriteFile(void)
{
	return AP_AUTHOR_FILE;
}

// ap_net.h's AP_NET_STATUS_* values, repeated so this header needs no other.
// tools/test-author-gate.c checks they match.
#define AP_AUTHOR_GATE_NET_IDLE          0
#define AP_AUTHOR_GATE_NET_CONNECTING    1
#define AP_AUTHOR_GATE_NET_CONNECTED     2
#define AP_AUTHOR_GATE_NET_ERROR         3
#define AP_AUTHOR_GATE_NET_UNREACHABLE   4
#define AP_AUTHOR_GATE_NET_RETRY_STOPPED 5

enum
{
	AP_AUTHOR_GATE_OK = 0,
	AP_AUTHOR_GATE_ROOM = 1, // a room is connecting, connected or retrying
	AP_AUTHOR_GATE_SOLO = 2, // a Play Solo session
};

// May the mode go on now? A refused or stopped dial (ERROR, RETRY_STOPPED)
// is not a room: nothing retries on its own from there, and the next dial is
// refused while the mode is on.
static inline int AP_AuthorGate_Enter(int netStatus, int soloSession)
{
	if (soloSession)
		return AP_AUTHOR_GATE_SOLO;
	if (netStatus == AP_AUTHOR_GATE_NET_CONNECTING || netStatus == AP_AUTHOR_GATE_NET_CONNECTED ||
	    netStatus == AP_AUTHOR_GATE_NET_UNREACHABLE)
		return AP_AUTHOR_GATE_ROOM;
	return AP_AUTHOR_GATE_OK;
}

// Whether a refused or stopped client is left behind that the mode should
// close on the way in, so nothing of the old room survives under it.
static inline int AP_AuthorGate_CloseOnEnter(int netStatus)
{
	return netStatus == AP_AUTHOR_GATE_NET_ERROR || netStatus == AP_AUTHOR_GATE_NET_RETRY_STOPPED;
}

// The reason every dial shows while the mode is on (the Connection page's
// status row prints it after "Error: "), or NULL to let the dial through.
#define AP_AUTHOR_GATE_DIAL_REASON "Box Author Mode is on. Turn it off in Options > Authoring"
static inline const char *AP_AuthorGate_DialRefusal(int modeOn)
{
	return modeOn ? AP_AUTHOR_GATE_DIAL_REASON : 0;
}

// The two help lines under the Box Author Mode row (33 characters each at
// FONT_SMALL). refusal is the last AP_AuthorGate_Enter answer on this row
// visit, AP_AUTHOR_GATE_OK when there was none.
static inline void AP_AuthorGate_Help(int modeOn, int refusal, const char **first, const char **second)
{
	if (refusal == AP_AUTHOR_GATE_ROOM)
	{
		*first = "Connected to a room. Authoring";
		*second = "needs none: press again to leave.";
	}
	else if (refusal == AP_AUTHOR_GATE_SOLO)
	{
		*first = "A Play Solo game is running.";
		*second = "Press again to end it and author.";
	}
	else if (modeOn)
	{
		*first = "Race a track: Select drops a box.";
		*second = "Rooms cannot connect while on.";
	}
	else
	{
		*first = "Place AP boxes to share. Off:";
		*second = "every room plays as before.";
	}
}

#endif
