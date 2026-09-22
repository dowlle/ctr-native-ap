// Behavioral harness for Steam routing and ctr-ap:// registration (issue #334,
// implementation slice 4). It includes the production units directly, so these
// assertions run against the code main.c compiles:
//
//   * platform/native_steam_route.c    SteamGameId validation, the rungameid
//                                      URL and the recorded-route file
//   * platform/native_link_route.c     the Steam branch of startup routing
//   * platform/native_link_register.c  registration decisions, driven against a
//                                      simulated per-user registry tree: clean
//                                      install, another CTR-AP client, another
//                                      program with foreign values, types and
//                                      subkeys, explicit replacement, owned-only
//                                      unregister with foreign additions,
//                                      command quoting, and a failure injected
//                                      at every write step with the full tree
//                                      compared after the rollback
//
//   cc -Wall -Wextra -I . -I include -o /tmp/test-link-windows tools/test-link-windows.c
//
// Exit 0 = every assertion held; failures are printed otherwise.
//
// The real registry operations (platform/native_link_register_win.c: a
// recursive read and single create, set and delete calls, no decisions), the
// Explorer and browser dispatch of ctr-ap:// and the steam:// hand-off are
// Windows acceptance checks and are not exercised here.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform/native_fs_utf8.c"
#include "platform/native_steam_route.c"
#include "platform/native_link_route.c"
#include "platform/native_link_register.c"

static int g_checks;
static int g_failures;

static void expect(int condition, const char *name)
{
	g_checks++;
	if (!condition)
	{
		g_failures++;
		printf("FAIL: %s\n", name);
	}
}

// ---------------------------------------------------------------------------

static void TestSteamGameId(void)
{
	unsigned long long id = 77;

	expect(NativeSteamRoute_ParseGameId("16748481221419335680", &id) && id == 16748481221419335680ULL,
	       "measured Windows shortcut id parses");
	expect(NativeSteamRoute_ParseGameId("13273274172630368256", &id) && id == 13273274172630368256ULL,
	       "measured Deck shortcut id parses");
	expect(NativeSteamRoute_ParseGameId("18446744073709551615", &id) && id == 18446744073709551615ULL,
	       "largest 64-bit value parses");
	expect(NativeSteamRoute_ParseGameId("1", &id) && id == 1, "one digit parses");

	id = 77;
	expect(!NativeSteamRoute_ParseGameId(NULL, &id), "missing variable refused");
	expect(!NativeSteamRoute_ParseGameId("", &id), "empty refused");
	expect(!NativeSteamRoute_ParseGameId("0", &id), "zero refused");
	expect(!NativeSteamRoute_ParseGameId("00000000000000000000", &id), "all zeros refused");
	expect(!NativeSteamRoute_ParseGameId("18446744073709551616", &id), "one past 64 bits refused");
	expect(!NativeSteamRoute_ParseGameId("99999999999999999999", &id), "twenty nines overflow refused");
	expect(!NativeSteamRoute_ParseGameId("123456789012345678901", &id), "21 digits refused");
	expect(!NativeSteamRoute_ParseGameId("-1", &id), "sign refused");
	expect(!NativeSteamRoute_ParseGameId("+1", &id), "plus sign refused");
	expect(!NativeSteamRoute_ParseGameId(" 1", &id), "leading space refused");
	expect(!NativeSteamRoute_ParseGameId("1 ", &id), "trailing space refused");
	expect(!NativeSteamRoute_ParseGameId("0x10", &id), "hex refused");
	expect(!NativeSteamRoute_ParseGameId("12a", &id), "letters refused");
	expect(id == 77, "refused value leaves the output untouched");

	{
		char url[NATIVE_STEAM_ROUTE_URL_MAX];
		expect(NativeSteamRoute_FormatUrl(16748481221419335680ULL, url, sizeof url) &&
		           strcmp(url, "steam://rungameid/16748481221419335680") == 0,
		       "rungameid URL");
		expect(NativeSteamRoute_FormatUrl(18446744073709551615ULL, url, sizeof url), "largest id fits the URL buffer");
		expect(!NativeSteamRoute_FormatUrl(0, url, sizeof url), "zero id has no URL");
		expect(!NativeSteamRoute_FormatUrl(1, url, 10), "URL never truncated");
	}
}

static void WriteFile(const char *dir, const char *name, const char *text)
{
	char path[512];
	FILE *f;
	snprintf(path, sizeof path, "%s/%s", dir, name);
	f = fopen(path, "wb");
	if (f != NULL)
	{
		fputs(text, f);
		fclose(f);
	}
}

static void TestSteamRouteStore(void)
{
	char dir[256];
	char cmd[320];
	unsigned long long id = 5;

	snprintf(dir, sizeof dir, "/tmp/ctr-steam-route-%d", (int)getpid());
	expect(NativeFs_MakeDirectory(dir), "state directory");

	expect(!NativeSteamRoute_Load(dir, &id), "no file: no route");
	expect(NativeSteamRoute_Save(dir, 16748481221419335680ULL), "route saved");
	expect(NativeSteamRoute_Load(dir, &id) && id == 16748481221419335680ULL, "route loads back");
	expect(NativeSteamRoute_Save(dir, 42), "route replaced");
	expect(NativeSteamRoute_Load(dir, &id) && id == 42, "replacement loads back");
	expect(!NativeSteamRoute_Save(dir, 0), "zero is never recorded");

	WriteFile(dir, NATIVE_STEAM_ROUTE_FILE ".tmp", "junk");
	expect(NativeSteamRoute_Save(dir, 43), "save clears a crashed temp file");
	expect(NativeSteamRoute_Load(dir, &id) && id == 43, "and saves");

	id = 5;
	WriteFile(dir, NATIVE_STEAM_ROUTE_FILE, "0\n");
	expect(!NativeSteamRoute_Load(dir, &id), "stored zero refused");
	WriteFile(dir, NATIVE_STEAM_ROUTE_FILE, "123");
	expect(!NativeSteamRoute_Load(dir, &id), "missing newline refused");
	WriteFile(dir, NATIVE_STEAM_ROUTE_FILE, "123\n456\n");
	expect(!NativeSteamRoute_Load(dir, &id), "extra line refused");
	WriteFile(dir, NATIVE_STEAM_ROUTE_FILE, "18446744073709551616\n");
	expect(!NativeSteamRoute_Load(dir, &id), "overflow refused");
	WriteFile(dir, NATIVE_STEAM_ROUTE_FILE, "123456789012345678901234567890123456789\n");
	expect(!NativeSteamRoute_Load(dir, &id), "overlong file refused");
	expect(id == 5, "damaged file leaves the output untouched");

	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	if (system(cmd) != 0)
		printf("note: could not remove %s\n", dir);
}

static void TestSteamRouting(void)
{
	NativeLinkRouteInput in;

	memset(&in, 0, sizeof in);
	in.haveRequest = 1;
	in.storeUsable = 1;
	in.isPrimary = 1;
	in.steamRoutingEnabled = 1;
	in.steamRouteKnown = 1;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_VIA_STEAM,
	       "link, no client running, Steam route known: go through Steam");
	in.launchedBySteam = 1;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_RUN_WITH_REQUEST,
	       "already started by Steam: run, never loop through Steam");
	in.launchedBySteam = 0;
	in.steamRouteKnown = 0;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_RUN_WITH_REQUEST, "never run under Steam: start directly");
	in.steamRouteKnown = 1;
	in.steamRoutingEnabled = 0;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_RUN_WITH_REQUEST,
	       "Steam routing off (not Windows): start directly");
	in.steamRoutingEnabled = 1;
	in.isPrimary = 0;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_HAND_OFF,
	       "client already running: hand off, Steam not invoked again");
	in.haveRequest = 0;
	in.isPrimary = 1;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_RUN, "no link: plain start even with a Steam route");
	in.isPrimary = 0;
	in.launchedBySteam = 1;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_ALREADY_RUNNING,
	       "second Steam launch, client already running: exit, no second game process");
	in.launchedBySteam = 0;
	in.isPrimary = 1;
	in.haveRequest = 1;
	in.storeUsable = 0;
	expect(NativeLinkRoute_Decide(&in) == NATIVE_LINK_ROUTE_RUN,
	       "unusable store: no Steam hop (nothing could carry the request)");
}

// ---------------------------------------------------------------------------
// Simulated per-user registry: the handler tree held in a NativeLinkRegTree,
// with the same single-change operations as the real layer (setting a value
// on a missing key fails; deleting a key that still holds anything fails).

typedef struct
{
	NativeLinkRegTree *tree;
	int failSnapshot;
	int failAt;         // the mutation with this index fails (-1 = none)
	int failFrom;       // every mutation from this index on fails (-1 = none)
	int corruptCommand; // the next set of the open command stores other text, reports success
	int wrongType;      // the next value set is stored as REG_EXPAND_SZ, reports success
	int mutations;
} SimReg;

static int SimMutation(SimReg *r)
{
	const int n = r->mutations++;
	return !(n == r->failAt || (r->failFrom >= 0 && n >= r->failFrom));
}

static int SimSnapshot(void *ctx, NativeLinkRegTree *out)
{
	SimReg *r = (SimReg *)ctx;
	if (r->failSnapshot)
		return 0;
	*out = *r->tree;
	return 1;
}

static int SimCreateKey(void *ctx, const char *path)
{
	SimReg *r = (SimReg *)ctx;
	return SimMutation(r) && NativeLinkRegTree_AddKey(r->tree, path) >= 0;
}

static int SimSetValue(void *ctx, const char *path, const char *name, unsigned long type, const void *data,
                       size_t size)
{
	SimReg *r = (SimReg *)ctx;
	unsigned char other[64];
	size_t otherSize;

	if (!SimMutation(r))
		return 0;
	if (r->wrongType)
	{
		r->wrongType = 0;
		type = NATIVE_LINK_REG_TYPE_EXPAND_SZ;
	}
	if (r->corruptCommand && strcmp(path, NATIVE_LINK_REG_COMMAND_KEY) == 0 && name[0] == '\0')
	{
		r->corruptCommand = 0;
		otherSize = linkRegUtf16("\"C:\\elsewhere.exe\" \"%1\"", other, sizeof other);
		return NativeLinkRegTree_SetValue(r->tree, path, name, type, other, otherSize);
	}
	return NativeLinkRegTree_SetValue(r->tree, path, name, type, data, size);
}

static int SimDeleteValue(void *ctx, const char *path, const char *name)
{
	SimReg *r = (SimReg *)ctx;
	return SimMutation(r) && NativeLinkRegTree_DeleteValue(r->tree, path, name);
}

static int SimDeleteEmptyKey(void *ctx, const char *path)
{
	SimReg *r = (SimReg *)ctx;
	return SimMutation(r) && NativeLinkRegTree_DeleteEmptyKey(r->tree, path);
}

static NativeLinkRegOps SimRegOps(SimReg *r)
{
	NativeLinkRegOps ops;
	ops.ctx = r;
	ops.snapshot = SimSnapshot;
	ops.createKey = SimCreateKey;
	ops.setValue = SimSetValue;
	ops.deleteValue = SimDeleteValue;
	ops.deleteEmptyKey = SimDeleteEmptyKey;
	return ops;
}

// Trees are large; the harness keeps them static.
static NativeLinkRegTree g_simTree;
static NativeLinkRegTree g_saved;
static NativeLinkRegTree g_want;

static void SimRegInit(SimReg *r)
{
	memset(r, 0, sizeof *r);
	r->tree = &g_simTree;
	NativeLinkRegTree_Clear(r->tree);
	r->failAt = -1;
	r->failFrom = -1;
}

// Clear the fault switches and the mutation count, keep the tree.
static void SimRegArm(SimReg *r)
{
	r->failSnapshot = 0;
	r->failAt = -1;
	r->failFrom = -1;
	r->corruptCommand = 0;
	r->wrongType = 0;
	r->mutations = 0;
}

static void PutText(NativeLinkRegTree *t, const char *path, const char *name, unsigned long type, const char *text)
{
	unsigned char buf[NATIVE_LINK_REG_TEXT_MAX * 2 + 2];
	const size_t n = linkRegUtf16(text, buf, sizeof buf);
	NativeLinkRegTree_AddKey(t, path);
	NativeLinkRegTree_SetValue(t, path, name, type, buf, n);
}

static void PutBytes(NativeLinkRegTree *t, const char *path, const char *name, unsigned long type, const void *data,
                     size_t size)
{
	NativeLinkRegTree_AddKey(t, path);
	NativeLinkRegTree_SetValue(t, path, name, type, data, size);
}

#define REG_BINARY_T 3u
#define REG_DWORD_T 4u

// Another program's handler with everything a real one might carry: a
// description stored as REG_EXPAND_SZ, an empty REG_BINARY "URL Protocol", a
// DWORD, an icon subkey, a value next to the open command and a binary value
// on shell\open.
static void SimRegForeign(SimReg *r)
{
	static const unsigned char flags[4] = {0x00, 0x00, 0x21, 0x00};
	static const unsigned char blob[5] = {1, 2, 3, 0, 255};

	PutText(r->tree, "", "", NATIVE_LINK_REG_TYPE_EXPAND_SZ, "URL:Some Other Tool");
	PutBytes(r->tree, "", NATIVE_LINK_REG_URL_PROTOCOL, REG_BINARY_T, NULL, 0);
	PutBytes(r->tree, "", "EditFlags", REG_DWORD_T, flags, sizeof flags);
	PutText(r->tree, "DefaultIcon", "", NATIVE_LINK_REG_TYPE_EXPAND_SZ, "%SystemRoot%\\other.ico,0");
	PutBytes(r->tree, "shell\\open", "FriendlyAppName", REG_BINARY_T, blob, sizeof blob);
	PutText(r->tree, NATIVE_LINK_REG_COMMAND_KEY, "", NATIVE_LINK_REG_TYPE_SZ,
	        "\"C:\\Tools\\other.exe\" --open \"%1\"");
	PutText(r->tree, NATIVE_LINK_REG_COMMAND_KEY, "DelegateExecute", NATIVE_LINK_REG_TYPE_SZ,
	        "{00000000-0000-0000-0000-000000000000}");
}

#define EXE_STABLE "C:\\Games\\CTR-AP Stable\\ctr_native_ap.exe"
#define EXE_TESTING "C:\\Games\\CTR-AP Testing\\ctr_native_ap.exe"
#define CMD_STABLE "\"" EXE_STABLE "\" \"%1\""

// This client's registration applied on top of a tree, the way the unit
// writes it.
static void ApplyOwned(NativeLinkRegTree *t, const char *command)
{
	PutText(t, "", "", NATIVE_LINK_REG_TYPE_SZ, NATIVE_LINK_REG_DESCRIPTION);
	PutText(t, "", NATIVE_LINK_REG_URL_PROTOCOL, NATIVE_LINK_REG_TYPE_SZ, "");
	PutText(t, "", NATIVE_LINK_REG_OWNER, NATIVE_LINK_REG_TYPE_SZ, "1");
	PutText(t, NATIVE_LINK_REG_COMMAND_KEY, "", NATIVE_LINK_REG_TYPE_SZ, command);
}

static void TestQuoting(void)
{
	char cmd[NATIVE_LINK_REG_TEXT_MAX];

	expect(NativeLinkReg_BuildCommand(EXE_STABLE, cmd, sizeof cmd) &&
	           strcmp(cmd, "\"C:\\Games\\CTR-AP Stable\\ctr_native_ap.exe\" \"%1\"") == 0,
	       "path with spaces quoted, link passed as one quoted argument");
	expect(NativeLinkReg_BuildCommand("D:\\Spiele\\Cr\xC3\xA4sh\\ctr_native_ap.exe", cmd, sizeof cmd),
	       "non-ASCII UTF-8 path accepted");
	expect(NativeLinkReg_BuildCommand("C:\\a&b^c(1)\\x.exe", cmd, sizeof cmd) &&
	           strcmp(cmd, "\"C:\\a&b^c(1)\\x.exe\" \"%1\"") == 0,
	       "shell metacharacters stay inside the quotes");
	expect(!NativeLinkReg_BuildCommand("C:\\100%1\\x.exe", cmd, sizeof cmd), "percent in path refused");
	expect(!NativeLinkReg_BuildCommand("C:\\a\"b\\x.exe", cmd, sizeof cmd), "double quote in path refused");
	expect(!NativeLinkReg_BuildCommand("C:\\a\nb\\x.exe", cmd, sizeof cmd), "control character refused");
	expect(!NativeLinkReg_BuildCommand("", cmd, sizeof cmd), "empty path refused");
	expect(!NativeLinkReg_BuildCommand(NULL, cmd, sizeof cmd), "missing path refused");
	expect(!NativeLinkReg_BuildCommand(EXE_STABLE, cmd, 20), "command never truncated");
}

static void TestRegistration(void)
{
	SimReg reg;
	NativeLinkRegOps ops;
	NativeLinkRegStatus status;

	// Clean machine: the launch registers exactly the four values, all REG_SZ.
	SimRegInit(&reg);
	ops = SimRegOps(&reg);
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_NONE, "clean machine: not set up");
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_OK &&
	           status == NATIVE_LINK_REG_THIS_CLIENT,
	       "clean machine: launch registers");
	NativeLinkRegTree_Clear(&g_want);
	ApplyOwned(&g_want, CMD_STABLE);
	expect(NativeLinkRegTree_Equal(reg.tree, &g_want) && reg.tree->keyCount == 4 && reg.tree->valueCount == 4,
	       "handler tree holds exactly the protocol marker, ownership, description and command");
	SimRegArm(&reg);
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_OK && reg.mutations == 0,
	       "already this client's: nothing rewritten");
	expect(NativeLinkReg_Status(&ops, "c:\\games\\ctr-ap stable\\CTR_NATIVE_AP.EXE") == NATIVE_LINK_REG_THIS_CLIENT,
	       "path compares case-insensitively");

	// Stable and Testing: whichever launched last owns room links.
	expect(NativeLinkReg_Status(&ops, EXE_TESTING) == NATIVE_LINK_REG_OTHER_CLIENT, "Testing sees Stable's handler");
	expect(NativeLinkReg_Register(&ops, EXE_TESTING, 0, &status) == NATIVE_LINK_REG_OK &&
	           status == NATIVE_LINK_REG_THIS_CLIENT,
	       "Testing launch takes over from Stable");
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_OTHER_CLIENT, "Stable now sees Testing's");
	SimRegArm(&reg);
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_NOT_OURS &&
	           reg.tree->keyCount == 4 && reg.mutations == 0,
	       "Stable cannot unregister Testing's handler");
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_OK, "Stable launch takes it back");

	// Unregister removes this client's registration completely when nothing
	// else is there.
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_OK && status == NATIVE_LINK_REG_NONE &&
	           reg.tree->keyCount == 0,
	       "unregister removes this client's handler and its empty keys");
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_NOT_OURS,
	       "unregister with nothing registered does nothing");

	// Another program: never replaced silently.
	SimRegInit(&reg);
	SimRegForeign(&reg);
	g_saved = *reg.tree;
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_OTHER_PROGRAM, "foreign handler detected");
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_KEPT_OTHER &&
	           status == NATIVE_LINK_REG_OTHER_PROGRAM && reg.mutations == 0,
	       "launch leaves another program's handler alone");
	expect(NativeLinkRegTree_Equal(reg.tree, &g_saved), "foreign handler untouched");
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_NOT_OURS && reg.mutations == 0,
	       "unregister never touches another program's handler");

	// The explicit action replaces it: this client's four values, everything
	// else exactly as it was (types included).
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 1, &status) == NATIVE_LINK_REG_OK &&
	           status == NATIVE_LINK_REG_THIS_CLIENT,
	       "explicit action replaces another program's handler");
	g_want = g_saved;
	ApplyOwned(&g_want, CMD_STABLE);
	expect(NativeLinkRegTree_Equal(reg.tree, &g_want),
	       "replacement changes only this client's values; foreign values, types and subkeys stay");

	// Unregister after the replacement: only this client's values go; keys
	// still holding the other program's values stay.
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_OK && status == NATIVE_LINK_REG_NONE,
	       "unregister after an explicit replacement");
	g_want = g_saved;
	NativeLinkRegTree_DeleteValue(&g_want, "", "");
	NativeLinkRegTree_DeleteValue(&g_want, "", NATIVE_LINK_REG_URL_PROTOCOL);
	NativeLinkRegTree_DeleteValue(&g_want, NATIVE_LINK_REG_COMMAND_KEY, "");
	expect(NativeLinkRegTree_Equal(reg.tree, &g_want),
	       "unregister removed only this client's values; EditFlags, DefaultIcon, DelegateExecute and "
	       "FriendlyAppName kept");

	// Foreign additions to this client's own registration survive unregister.
	SimRegInit(&reg);
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_OK, "registered for additions");
	{
		static const unsigned char dword[4] = {7, 0, 0, 0};
		PutBytes(reg.tree, "", "AddedByOther", REG_DWORD_T, dword, sizeof dword);
		PutText(reg.tree, "DefaultIcon", "", NATIVE_LINK_REG_TYPE_EXPAND_SZ, "%SystemRoot%\\icon.ico");
		PutText(reg.tree, "shell\\edit\\command", "", NATIVE_LINK_REG_TYPE_SZ, "notepad.exe");
	}
	g_want = *reg.tree;
	NativeLinkRegTree_DeleteValue(&g_want, "", "");
	NativeLinkRegTree_DeleteValue(&g_want, "", NATIVE_LINK_REG_URL_PROTOCOL);
	NativeLinkRegTree_DeleteValue(&g_want, "", NATIVE_LINK_REG_OWNER);
	NativeLinkRegTree_DeleteValue(&g_want, NATIVE_LINK_REG_COMMAND_KEY, "");
	NativeLinkRegTree_DeleteEmptyKey(&g_want, NATIVE_LINK_REG_COMMAND_KEY);
	NativeLinkRegTree_DeleteEmptyKey(&g_want, "shell\\open");
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_OK &&
	           NativeLinkRegTree_Equal(reg.tree, &g_want),
	       "unregister with foreign additions: this client's values and emptied keys go, the rest stays");
	expect(NativeLinkRegTree_FindKey(reg.tree, "shell") >= 0 && NativeLinkRegTree_FindKey(reg.tree, "shell\\open") < 0 &&
	           NativeLinkRegTree_FindValue(reg.tree, "", "AddedByOther") >= 0,
	       "a key that still holds a foreign subkey is kept");

	// This client's registration changed by something else: stop, touch nothing.
	SimRegInit(&reg);
	NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status);
	PutText(reg.tree, "", "", NATIVE_LINK_REG_TYPE_SZ, "URL:Edited by someone");
	g_saved = *reg.tree;
	SimRegArm(&reg);
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_CHANGED && reg.mutations == 0 &&
	           NativeLinkRegTree_Equal(reg.tree, &g_saved),
	       "unregister stops when the description was changed");
	SimRegInit(&reg);
	NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status);
	PutText(reg.tree, "", NATIVE_LINK_REG_URL_PROTOCOL, NATIVE_LINK_REG_TYPE_EXPAND_SZ, "");
	g_saved = *reg.tree;
	SimRegArm(&reg);
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_CHANGED && reg.mutations == 0 &&
	           NativeLinkRegTree_Equal(reg.tree, &g_saved),
	       "unregister stops when a value's type was changed");
	// A launch repairs its own registration with a wrong type.
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_OK &&
	           linkRegOwnedIntact(reg.tree, CMD_STABLE),
	       "launch rewrites its own value that has the wrong type");

	// A bare key without a command or marker opens nothing.
	SimRegInit(&reg);
	NativeLinkRegTree_AddKey(reg.tree, "");
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_NONE, "empty key counts as not set up");

	// Unreadable (or too large to snapshot) registry: nothing is written.
	SimRegInit(&reg);
	reg.failSnapshot = 1;
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_UNKNOWN, "unreadable: unknown");
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 1, &status) == NATIVE_LINK_REG_FAILED && reg.mutations == 0,
	       "unreadable: nothing written");
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_FAILED && reg.mutations == 0,
	       "unreadable: nothing removed");

	// A path that cannot be quoted or encoded safely is never written.
	SimRegInit(&reg);
	expect(NativeLinkReg_Register(&ops, "C:\\100%1\\ctr_native_ap.exe", 0, &status) == NATIVE_LINK_REG_BAD_PATH &&
	           reg.mutations == 0 && reg.tree->keyCount == 0,
	       "unsafe path refused before any write");
	expect(NativeLinkReg_Register(&ops, "C:\\bad\xFF\\ctr_native_ap.exe", 0, &status) == NATIVE_LINK_REG_BAD_PATH &&
	           reg.mutations == 0,
	       "invalid UTF-8 path refused before any write");
}

static void TestTree(void)
{
	static NativeLinkRegTree t;
	static const unsigned char one[1] = {1};

	NativeLinkRegTree_Clear(&t);
	expect(NativeLinkRegTree_AddKey(&t, "a\\b\\c") >= 0 && t.keyCount == 4, "adding a key adds its parents");
	expect(NativeLinkRegTree_FindKey(&t, "A\\B") >= 0, "key names compare case-insensitively");
	expect(NativeLinkRegTree_SetValue(&t, "a", "X", REG_BINARY_T, one, 1) &&
	           NativeLinkRegTree_FindValue(&t, "a", "x") >= 0,
	       "value names compare case-insensitively");
	expect(!NativeLinkRegTree_SetValue(&t, "missing", "x", REG_BINARY_T, one, 1), "value on a missing key refused");
	expect(!NativeLinkRegTree_DeleteEmptyKey(&t, "a\\b"), "key with a subkey is not deleted");
	expect(!NativeLinkRegTree_DeleteEmptyKey(&t, "a"), "key with a value is not deleted");
	expect(NativeLinkRegTree_DeleteEmptyKey(&t, "a\\b\\c") && NativeLinkRegTree_DeleteEmptyKey(&t, "a\\b") &&
	           t.keyCount == 2 && NativeLinkRegTree_FindValue(&t, "a", "x") >= 0,
	       "empty keys delete and values keep their key");
	{
		static NativeLinkRegTree u;
		static const unsigned char two[1] = {2};
		u = t;
		expect(NativeLinkRegTree_Equal(&t, &u), "copy is equal");
		NativeLinkRegTree_SetValue(&u, "a", "x", REG_DWORD_T, one, 1);
		expect(!NativeLinkRegTree_Equal(&t, &u), "type difference detected");
		NativeLinkRegTree_SetValue(&u, "a", "x", REG_BINARY_T, two, 1);
		expect(!NativeLinkRegTree_Equal(&t, &u), "data difference detected");
		NativeLinkRegTree_SetValue(&u, "a", "x", REG_BINARY_T, one, 1);
		NativeLinkRegTree_AddKey(&u, "a\\extra");
		expect(!NativeLinkRegTree_Equal(&t, &u), "extra subkey detected");
	}
	expect(NativeLinkRegTree_AddKey(&t, "\\bad") < 0 && NativeLinkRegTree_AddKey(&t, "bad\\") < 0,
	       "malformed paths refused");
}

// Fail the registration at every single step in turn: the tree must come back
// exactly (keys, values, types and data) each time.
static void RegisterFailEachStep(void (*setup)(SimReg *), int explicitAction, const char *what)
{
	SimReg reg;
	NativeLinkRegOps ops;
	NativeLinkRegStatus status;
	NativeLinkRegStatus statusBefore;
	int step;
	int allRestored = 1;
	int allReported = 1;

	SimRegInit(&reg);
	ops = SimRegOps(&reg);
	setup(&reg);
	NativeLinkReg_Register(&ops, EXE_STABLE, explicitAction, &status);
	expect(reg.mutations == 5, "registration takes one key create and four value sets");
	for (step = 0; step < 5; step++)
	{
		SimRegInit(&reg);
		ops = SimRegOps(&reg);
		setup(&reg);
		g_saved = *reg.tree;
		statusBefore = NativeLinkReg_Status(&ops, EXE_STABLE);
		SimRegArm(&reg);
		reg.failAt = step;
		if (NativeLinkReg_Register(&ops, EXE_STABLE, explicitAction, &status) != NATIVE_LINK_REG_ROLLED_BACK ||
		    status != statusBefore)
			allReported = 0;
		if (!NativeLinkRegTree_Equal(reg.tree, &g_saved))
			allRestored = 0;
	}
	printf("  register, fail each of 5 steps: %s\n", what);
	expect(allReported, "each failed step is reported as rolled back with the earlier status");
	expect(allRestored, "each failed step leaves the tree exactly as it was");
}

static void SetupClean(SimReg *r)
{
	(void)r;
}

static void SetupOtherClient(SimReg *r)
{
	static const unsigned char dword[4] = {9, 0, 0, 0};
	ApplyOwned(r->tree, "\"" EXE_TESTING "\" \"%1\"");
	PutBytes(r->tree, "", "AddedByOther", REG_DWORD_T, dword, sizeof dword);
	PutText(r->tree, "DefaultIcon", "", NATIVE_LINK_REG_TYPE_EXPAND_SZ, "%SystemRoot%\\icon.ico");
}

static void TestRollback(void)
{
	SimReg reg;
	NativeLinkRegOps ops;
	NativeLinkRegStatus status;

	RegisterFailEachStep(SetupClean, 0, "clean machine");
	RegisterFailEachStep(SimRegForeign, 1, "explicit replacement of another program");
	RegisterFailEachStep(SetupOtherClient, 0, "another CTR-AP client with foreign additions");

	// Writes that report success but read back wrong are undone exactly.
	SimRegInit(&reg);
	ops = SimRegOps(&reg);
	SimRegForeign(&reg);
	g_saved = *reg.tree;
	reg.corruptCommand = 1;
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 1, &status) == NATIVE_LINK_REG_ROLLED_BACK &&
	           status == NATIVE_LINK_REG_OTHER_PROGRAM && NativeLinkRegTree_Equal(reg.tree, &g_saved),
	       "command that reads back as another path: foreign handler exactly restored");
	SimRegInit(&reg);
	SimRegForeign(&reg);
	g_saved = *reg.tree;
	reg.wrongType = 1;
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 1, &status) == NATIVE_LINK_REG_ROLLED_BACK &&
	           NativeLinkRegTree_Equal(reg.tree, &g_saved),
	       "a value that reads back with another type: foreign handler exactly restored");

	// A restore that cannot complete is reported as failed, never as undone.
	SimRegInit(&reg);
	SimRegForeign(&reg);
	g_saved = *reg.tree;
	reg.failFrom = 2;
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 1, &status) == NATIVE_LINK_REG_FAILED,
	       "partial replacement with a failing restore is reported as failed");
	SimRegArm(&reg);
	expect(!NativeLinkRegTree_Equal(reg.tree, &g_saved), "(the partial change is still there, as reported)");

	// Nothing landed at all: restoring is trivially exact.
	SimRegInit(&reg);
	SimRegForeign(&reg);
	g_saved = *reg.tree;
	reg.failFrom = 0;
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 1, &status) == NATIVE_LINK_REG_ROLLED_BACK &&
	           NativeLinkRegTree_Equal(reg.tree, &g_saved),
	       "no write landed: the tree is unchanged and reported as undone");

	// Unregister failing at each step, with foreign additions present.
	{
		int step;
		int steps;
		int allRestored = 1;
		int allReported = 1;

		SimRegInit(&reg);
		ops = SimRegOps(&reg);
		ApplyOwned(reg.tree, CMD_STABLE);
		PutText(reg.tree, "DefaultIcon", "", NATIVE_LINK_REG_TYPE_EXPAND_SZ, "%SystemRoot%\\icon.ico");
		expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_OK, "unregister step count");
		steps = reg.mutations;
		expect(steps == 7, "unregister takes 4 value deletes and 3 key deletes (the ctr-ap key keeps DefaultIcon)");
		for (step = 0; step < steps; step++)
		{
			SimRegInit(&reg);
			ops = SimRegOps(&reg);
			ApplyOwned(reg.tree, CMD_STABLE);
			PutText(reg.tree, "DefaultIcon", "", NATIVE_LINK_REG_TYPE_EXPAND_SZ, "%SystemRoot%\\icon.ico");
			g_saved = *reg.tree;
			SimRegArm(&reg);
			reg.failAt = step;
			if (NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) != NATIVE_LINK_REG_ROLLED_BACK ||
			    status != NATIVE_LINK_REG_THIS_CLIENT)
				allReported = 0;
			if (!NativeLinkRegTree_Equal(reg.tree, &g_saved))
				allRestored = 0;
		}
		expect(allReported, "unregister failing at each step is reported as rolled back");
		expect(allRestored, "unregister failing at each step leaves the registration exactly as it was");
	}
}

static void TestTexts(void)
{
	int i;
	for (i = NATIVE_LINK_REG_UNKNOWN; i <= NATIVE_LINK_REG_OTHER_PROGRAM + 1; i++)
	{
		const char *t = NativeLinkReg_StatusText((NativeLinkRegStatus)i);
		expect(t != NULL && strchr(t, '\\') == NULL && strchr(t, '%') == NULL, "status text has no path");
	}
	for (i = NATIVE_LINK_REG_OK; i <= NATIVE_LINK_REG_FAILED + 1; i++)
	{
		const char *t = NativeLinkReg_ResultText((NativeLinkRegResult)i);
		expect(t != NULL && strchr(t, '\\') == NULL && strchr(t, '%') == NULL, "result text has no path");
	}
}

int main(void)
{
	TestSteamGameId();
	TestSteamRouteStore();
	TestSteamRouting();
	TestQuoting();
	TestRegistration();
	TestTree();
	TestRollback();
	TestTexts();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
