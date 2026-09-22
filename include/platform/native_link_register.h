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
//   * Unregister removes only what this client writes, and only while all of
//     it is still exactly as this client wrote it. Values and subkeys another
//     program added stay; a key is removed only once it is empty.
//   * Before any change the whole handler tree is snapshotted (every key, and
//     every value with its type and data). A change that fails or does not
//     read back exactly as intended is undone to that snapshot, and the undo is
//     verified against the full snapshot.
//   * Registration failure never blocks startup.
//
// The decisions are pure and run against NativeLinkRegOps, so the host harness
// drives them against a simulated registry; platform/native_link_register_win.c
// provides the real registry operations and nothing else: a recursive read,
// create key, set value, delete value and delete an empty key. There is no
// recursive delete.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_LINK_REG_DESCRIPTION "URL:Crash Team Racing Archipelago room link"
#define NATIVE_LINK_REG_TEXT_MAX 2048

// Key paths are relative to HKCU\Software\Classes\ctr-ap and use a
// backslash separator; "" is the ctr-ap key itself. Names are UTF-8; the value
// name "" is the key's default value.
#define NATIVE_LINK_REG_COMMAND_KEY "shell\\open\\command"
#define NATIVE_LINK_REG_URL_PROTOCOL "URL Protocol"
#define NATIVE_LINK_REG_OWNER "CtrApClient"

// Registry value types used by this unit (the Win32 numbers).
#define NATIVE_LINK_REG_TYPE_SZ 1u
#define NATIVE_LINK_REG_TYPE_EXPAND_SZ 2u

// Snapshot limits. A handler tree beyond them cannot be snapshotted, so it is
// never changed.
#define NATIVE_LINK_REG_MAX_KEYS 32
#define NATIVE_LINK_REG_MAX_VALUES 96
#define NATIVE_LINK_REG_NAME_MAX 512
#define NATIVE_LINK_REG_PATH_MAX 512
#define NATIVE_LINK_REG_DATA_MAX 65536
#define NATIVE_LINK_REG_DEPTH_MAX 8

typedef struct
{
	int key;              // index into keys
	char name[NATIVE_LINK_REG_NAME_MAX];
	unsigned long type;
	size_t offset;        // into data
	size_t size;
} NativeLinkRegValue;

// The complete handler tree. keyCount == 0 means the ctr-ap key is absent.
// keys[0] is then the ctr-ap key itself (path "") and every key's parent comes
// before it. Key and value names compare case-insensitively (ASCII), as in the
// registry.
typedef struct
{
	int keyCount;
	char keys[NATIVE_LINK_REG_MAX_KEYS][NATIVE_LINK_REG_PATH_MAX];
	int valueCount;
	NativeLinkRegValue values[NATIVE_LINK_REG_MAX_VALUES];
	size_t dataUsed;
	unsigned char data[NATIVE_LINK_REG_DATA_MAX];
} NativeLinkRegTree;

// Tree helpers, shared by the decisions, the real snapshot and the harness.
void NativeLinkRegTree_Clear(NativeLinkRegTree *t);
int NativeLinkRegTree_FindKey(const NativeLinkRegTree *t, const char *path);
// Adds the key and any missing parents (like RegCreateKeyEx). Returns its
// index, or -1 when a limit is hit.
int NativeLinkRegTree_AddKey(NativeLinkRegTree *t, const char *path);
int NativeLinkRegTree_FindValue(const NativeLinkRegTree *t, const char *path, const char *name);
// The key must exist. Returns 1 on success.
int NativeLinkRegTree_SetValue(NativeLinkRegTree *t, const char *path, const char *name, unsigned long type,
                               const void *data, size_t size);
// A missing value counts as success.
int NativeLinkRegTree_DeleteValue(NativeLinkRegTree *t, const char *path, const char *name);
// Refuses (returns 0) a key that still has values or subkeys. A missing key
// counts as success.
int NativeLinkRegTree_DeleteEmptyKey(NativeLinkRegTree *t, const char *path);
// Same keys and, per key, the same values with the same type and data bytes.
int NativeLinkRegTree_Equal(const NativeLinkRegTree *a, const NativeLinkRegTree *b);

typedef struct
{
	void *ctx;
	// Read the whole tree. 1 on success (an absent key gives an empty tree), 0
	// when it cannot be read completely or exceeds the snapshot limits.
	int (*snapshot)(void *ctx, NativeLinkRegTree *out);
	// Create the key and any missing parents. Existing keys are left as they are.
	int (*createKey)(void *ctx, const char *path);
	// Set one value on an existing key.
	int (*setValue)(void *ctx, const char *path, const char *name, unsigned long type, const void *data,
	                size_t size);
	// Delete one value. A missing value or key counts as success.
	int (*deleteValue)(void *ctx, const char *path, const char *name);
	// Delete a key only when it has no values and no subkeys. A missing key
	// counts as success.
	int (*deleteEmptyKey)(void *ctx, const char *path);
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
	NATIVE_LINK_REG_CHANGED,          // unregister refused: this client's handler was changed elsewhere
	NATIVE_LINK_REG_FAILED            // the change failed and could not be undone
} NativeLinkRegResult;

// "\"<exe>\" \"%1\"". Refuses an empty path, a path with a double quote, a
// control character or a percent sign (the shell would expand %1, %2 and the
// like inside the path). Returns 1 when it fits.
int NativeLinkReg_BuildCommand(const char *exe, char *out, size_t cap);

NativeLinkRegStatus NativeLinkReg_Classify(const NativeLinkRegTree *tree, const char *ourCommand);
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
