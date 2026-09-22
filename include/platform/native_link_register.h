#ifndef NATIVE_LINK_REGISTER_H
#define NATIVE_LINK_REGISTER_H

// ctr-ap:// link registration (issue #334, slice 4). Windows only for 0.2.1.
//
// The handler is the per-user URL protocol key HKCU\Software\Classes\ctr-ap,
// whose shell\open\command runs this client with the link:
//
//   "C:\...\ctr_native_ap.exe" "%1"
//
// A CTR AP client also writes an ownership value (CtrApClient) next to it, so
// a client can tell its own handler, another CTR AP install's handler and some
// other program's handler apart. Rules (product ruling, 2026-09-22 22:26 CEST):
//
//   * A genuine client launch registers automatically when there is no handler
//     or the handler belongs to a CTR AP client: the most recently launched
//     client owns room links (Stable and Testing on one machine: whichever ran
//     last).
//   * A handler owned by another program is never replaced silently. Only the
//     explicit "Use this client for room links" action on the Connection page
//     replaces it.
//   * Unregister removes the handler only when it is this client's own.
//   * A write that fails or does not read back as this client's handler is
//     rolled back to exactly what was there before.
//   * Registration failure never blocks startup.
//
// The decisions are pure and run against NativeLinkRegOps, so the host harness
// drives them against a simulated registry; platform/native_link_register_win.c
// provides the real registry operations.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_LINK_REG_DESCRIPTION "URL:Crash Team Racing Archipelago room link"
#define NATIVE_LINK_REG_TEXT_MAX 2048

// What the handler key holds. Strings are UTF-8; "" means the value is absent.
typedef struct
{
	int present;      // the ctr-ap key exists
	int urlProtocol;  // the "URL Protocol" value exists
	int owned;        // the CtrApClient ownership value exists
	char description[256];
	char command[NATIVE_LINK_REG_TEXT_MAX];
} NativeLinkRegState;

typedef struct
{
	void *ctx;
	// Read the key. 1 on success (present may be 0), 0 when it cannot be read.
	int (*read)(void *ctx, NativeLinkRegState *out);
	// Make the key hold exactly these values: create it when missing, set every
	// value given, delete the flags that are 0 and the strings that are "".
	// Other values and subkeys already there are left alone.
	int (*write)(void *ctx, const NativeLinkRegState *state);
	// Delete the key and everything under it. A missing key counts as success.
	int (*remove)(void *ctx);
} NativeLinkRegOps;

typedef enum
{
	NATIVE_LINK_REG_UNKNOWN = 0,      // the key could not be read
	NATIVE_LINK_REG_NONE,             // no handler
	NATIVE_LINK_REG_THIS_CLIENT,      // this client opens room links
	NATIVE_LINK_REG_OTHER_CLIENT,     // another CTR AP install does
	NATIVE_LINK_REG_OTHER_PROGRAM     // a program that is not a CTR AP client does
} NativeLinkRegStatus;

typedef enum
{
	NATIVE_LINK_REG_OK = 0,           // done, or already this client's
	NATIVE_LINK_REG_KEPT_OTHER,       // automatic mode left another program's handler
	NATIVE_LINK_REG_NOT_OURS,         // unregister refused: not this client's handler
	NATIVE_LINK_REG_BAD_PATH,         // this client's path cannot be put in a command
	NATIVE_LINK_REG_ROLLED_BACK,      // the change failed and was undone
	NATIVE_LINK_REG_FAILED            // the change failed and could not be undone
} NativeLinkRegResult;

// "\"<exe>\" \"%1\"". Refuses an empty path, a path with a double quote, a
// control character or a percent sign (the shell would expand %1, %2 and the
// like inside the path). Returns 1 when it fits.
int NativeLinkReg_BuildCommand(const char *exe, char *out, size_t cap);

NativeLinkRegStatus NativeLinkReg_Classify(const NativeLinkRegState *state, const char *ourCommand);
NativeLinkRegStatus NativeLinkReg_Status(const NativeLinkRegOps *ops, const char *exe);

// Register this client. replaceOtherProgram = 0 is the automatic launch path;
// 1 is the explicit Connection-page action. *statusOut (may be NULL) is the
// status after the call.
NativeLinkRegResult NativeLinkReg_Register(const NativeLinkRegOps *ops, const char *exe, int replaceOtherProgram,
                                           NativeLinkRegStatus *statusOut);
NativeLinkRegResult NativeLinkReg_Unregister(const NativeLinkRegOps *ops, const char *exe,
                                             NativeLinkRegStatus *statusOut);

// Fixed texts; neither contains a path or command.
const char *NativeLinkReg_StatusText(NativeLinkRegStatus status);
const char *NativeLinkReg_ResultText(NativeLinkRegResult result);

#if defined(_WIN32)
// Real per-user registry operations (platform/native_link_register_win.c).
void NativeLinkReg_WinOps(NativeLinkRegOps *ops);
// This executable's full path as UTF-8. Returns 1 when it fits.
int NativeLinkReg_WinExePath(char *out, size_t cap);
#endif

#ifdef __cplusplus
}
#endif

#endif // NATIVE_LINK_REGISTER_H
