#ifndef NATIVE_LAUNCH_REQUEST_H
#define NATIVE_LAUNCH_REQUEST_H

// Pure parser for the credential-free one-click-connect request (issue #334,
// implementation slice 1). It understands one URI:
//
//   ctr-ap://connect?host=<encoded-host>&port=<decimal>&slot=<encoded-slot>&room=<encoded-room-id>
//
// and turns it into a temporary NativeLaunchRequest. It is deliberately
// freestanding: no engine headers, no g_config, no filesystem, no sockets, no
// Steam and no AP_Net_Reconnect. A host harness includes the .c directly and
// drives the production parser, so the protocol and the game cannot drift.
//
// The credential-bearing design in the reviewed Shape B result carried a
// `password` key. The final contract corrections removed it: no password ever
// enters the URI, the OS dispatch, the command line or a pending file. A
// `password` key is therefore an unknown key and is rejected like any other.
//
// The parser decides nothing about transport. It never infers ws:// versus
// wss://, never touches the saved connection and never mutates g_config; the
// later handoff slice owns all of that. An accepted request is a value only.
//
// Redaction is structural, not best-effort: every diagnostic is one of the
// fixed strings returned by NativeLaunchRequest_StatusText, so no diagnostic
// can carry the URI, the command line, a rejected value or a credential.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Whole-URI ceiling. A longer argument is refused before any field is read.
#define NATIVE_LAUNCH_REQUEST_URI_MAX 2048

// Field limits are BYTE counts after percent-decoding and UTF-8 validation.
//
// slot: fits g_config.slot[64] (63 bytes + NUL).
// room: 64 bytes, its own identity field, independent of the slot buffer.
// host: bounded so that "ws://" + host + ":" + five-digit port + NUL fits
//       g_config.uri[128]: 128 - 5 - 1 - 5 - 1 = 116 bytes. A longer host is
//       rejected, never truncated.
#define NATIVE_LAUNCH_REQUEST_HOST_MAX 116
#define NATIVE_LAUNCH_REQUEST_SLOT_MAX 63
#define NATIVE_LAUNCH_REQUEST_ROOM_MAX 64

// Port is ASCII decimal, 1 through 65535. Signs, whitespace and empty values
// are refused.
#define NATIVE_LAUNCH_REQUEST_PORT_MIN 1
#define NATIVE_LAUNCH_REQUEST_PORT_MAX 65535

#define NATIVE_LAUNCH_REQUEST_URI_PREFIX "ctr-ap://"
#define NATIVE_LAUNCH_REQUEST_AUTHORITY "connect"

// The parsed request identity: normalized (host, port, slot, room). Buffers
// are one byte larger than their limit for the NUL. room is required and
// nonempty; a manual request without a room ID is outside this feature.
typedef struct
{
	char host[NATIVE_LAUNCH_REQUEST_HOST_MAX + 1];
	unsigned int port;
	char slot[NATIVE_LAUNCH_REQUEST_SLOT_MAX + 1];
	char room[NATIVE_LAUNCH_REQUEST_ROOM_MAX + 1];
} NativeLaunchRequest;

typedef enum
{
	NATIVE_LAUNCH_REQUEST_OK = 0,
	NATIVE_LAUNCH_REQUEST_ERR_NULL,          // NULL uri or output
	NATIVE_LAUNCH_REQUEST_ERR_EMPTY,         // empty uri or empty required field
	NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG,      // whole URI or a field over its byte limit
	NATIVE_LAUNCH_REQUEST_ERR_SCHEME,        // not the lowercase ctr-ap:// scheme
	NATIVE_LAUNCH_REQUEST_ERR_AUTHORITY,     // authority is not exactly "connect"
	NATIVE_LAUNCH_REQUEST_ERR_FRAGMENT,      // a fragment is present
	NATIVE_LAUNCH_REQUEST_ERR_QUERY,         // malformed query: missing '=', empty key or segment
	NATIVE_LAUNCH_REQUEST_ERR_ESCAPE,        // '%' not followed by two hex digits
	NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY, // a key occurs more than once
	NATIVE_LAUNCH_REQUEST_ERR_UNKNOWN_KEY,   // a key outside host/port/slot/room (e.g. password)
	NATIVE_LAUNCH_REQUEST_ERR_MISSING_KEY,   // a required key is absent
	NATIVE_LAUNCH_REQUEST_ERR_CONTROL,       // decoded NUL, C0 control, DEL, CR, LF or tab
	NATIVE_LAUNCH_REQUEST_ERR_UTF8,          // invalid, overlong or surrogate UTF-8
	NATIVE_LAUNCH_REQUEST_ERR_HOST,          // host is not a DNS name, IPv4 or bracketed IPv6
	NATIVE_LAUNCH_REQUEST_ERR_PORT,          // port is not strict decimal in range
	NATIVE_LAUNCH_REQUEST_ERR_SLOT,          // slot empty, over its byte limit or edged with whitespace
	NATIVE_LAUNCH_REQUEST_ERR_ROOM           // room empty or carries a forbidden delimiter
} NativeLaunchRequestStatus;

// Parse one ctr-ap URI into *out. On any non-OK result *out is left untouched
// (callers can initialize a sentinel and prove it), so a rejected request can
// never half-populate a request object.
NativeLaunchRequestStatus NativeLaunchRequest_Parse(const char *uri, NativeLaunchRequest *out);

// Fixed, redacted reason for a status. Never contains a URI or a field value.
const char *NativeLaunchRequest_StatusText(NativeLaunchRequestStatus status);

// Argument-form classification for the AP-build wiring in main.c:
//   bare    ctr_native_ap <ctr-ap-uri>            (OS protocol-handler form)
//   connect ctr_native_ap connect <ctr-ap-uri>    (manual and test form)
// Anything else is NONE and stays with the existing argument behaviour. This
// only classifies; the caller extracts the URI token and calls Parse.
typedef enum
{
	NATIVE_LAUNCH_REQUEST_ARG_NONE = 0,
	NATIVE_LAUNCH_REQUEST_ARG_BARE,
	NATIVE_LAUNCH_REQUEST_ARG_CONNECT
} NativeLaunchRequestArgForm;

NativeLaunchRequestArgForm NativeLaunchRequest_ClassifyArg(const char *arg);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NATIVE_LAUNCH_REQUEST_H
