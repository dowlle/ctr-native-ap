// Behavioral harness for Steam routing and ctr-ap:// registration (issue #334,
// implementation slice 4). It includes the production units directly, so these
// assertions run against the code main.c compiles:
//
//   * platform/native_steam_route.c    SteamGameId validation, the rungameid
//                                      URL and the recorded-route file
//   * platform/native_link_route.c     the Steam branch of startup routing
//   * platform/native_link_register.c  registration decisions, driven against a
//                                      simulated per-user registry: clean
//                                      install, another CTR-AP client, another
//                                      program, explicit replacement,
//                                      ownership-safe unregister, command
//                                      quoting and rollback on failure
//
//   cc -Wall -Wextra -I . -I include -o /tmp/test-link-windows tools/test-link-windows.c
//
// Exit 0 = every assertion held; failures are printed otherwise.
//
// The real registry operations (platform/native_link_register_win.c), the
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
// Simulated per-user registry.

typedef struct
{
	NativeLinkRegState key;
	int extraSubkey;     // a subkey another program added (DefaultIcon and so on)
	int failRead;
	int failWrite;       // write fails after setting some values
	int corruptWrite;    // write reports success but stores a different command
	int failRemove;
	int failWritesAfter; // fail every write after this many (-1 = never)
	int writes;
	int removes;
} SimReg;

static int SimRegRead(void *ctx, NativeLinkRegState *out)
{
	SimReg *r = (SimReg *)ctx;
	if (r->failRead)
		return 0;
	*out = r->key;
	return 1;
}

static int SimRegWrite(void *ctx, const NativeLinkRegState *s)
{
	SimReg *r = (SimReg *)ctx;
	r->writes++;
	if (r->failWritesAfter >= 0 && r->writes > r->failWritesAfter)
		return 0;
	if (r->failWrite)
	{
		// Partial: the key and marker land, the command does not.
		r->key.present = 1;
		r->key.owned = s->owned;
		return 0;
	}
	r->key = *s;
	r->key.present = 1;
	if (r->corruptWrite)
		snprintf(r->key.command, sizeof r->key.command, "\"C:\\\\elsewhere.exe\" \"%%1\"");
	return 1;
}

static int SimRegRemove(void *ctx)
{
	SimReg *r = (SimReg *)ctx;
	r->removes++;
	if (r->failRemove)
		return 0;
	memset(&r->key, 0, sizeof r->key);
	r->extraSubkey = 0;
	return 1;
}

static NativeLinkRegOps SimRegOps(SimReg *r)
{
	NativeLinkRegOps ops;
	ops.ctx = r;
	ops.read = SimRegRead;
	ops.write = SimRegWrite;
	ops.remove = SimRegRemove;
	return ops;
}

static void SimRegInit(SimReg *r)
{
	memset(r, 0, sizeof *r);
	r->failWritesAfter = -1;
}

static void SimRegForeign(SimReg *r)
{
	r->key.present = 1;
	r->key.urlProtocol = 1;
	r->key.owned = 0;
	snprintf(r->key.description, sizeof r->key.description, "URL:Some Other Tool");
	snprintf(r->key.command, sizeof r->key.command, "\"C:\\Tools\\other.exe\" --open \"%%1\"");
	r->extraSubkey = 1;
}

#define EXE_STABLE "C:\\Games\\CTR-AP Stable\\ctr_native_ap.exe"
#define EXE_TESTING "C:\\Games\\CTR-AP Testing\\ctr_native_ap.exe"

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
	NativeLinkRegState saved;

	// Clean machine: the launch registers.
	SimRegInit(&reg);
	ops = SimRegOps(&reg);
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_NONE, "clean machine: not set up");
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_OK &&
	           status == NATIVE_LINK_REG_THIS_CLIENT,
	       "clean machine: launch registers");
	expect(reg.key.urlProtocol && reg.key.owned &&
	           strcmp(reg.key.description, NATIVE_LINK_REG_DESCRIPTION) == 0 &&
	           strcmp(reg.key.command, "\"" EXE_STABLE "\" \"%1\"") == 0,
	       "handler key holds the protocol marker, ownership and command");
	reg.writes = 0;
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_OK && reg.writes == 0,
	       "already this client's: nothing rewritten");
	expect(NativeLinkReg_Status(&ops, "c:\\games\\ctr-ap stable\\CTR_NATIVE_AP.EXE") == NATIVE_LINK_REG_THIS_CLIENT,
	       "path compares case-insensitively");

	// Stable and Testing: whichever launched last owns room links.
	expect(NativeLinkReg_Status(&ops, EXE_TESTING) == NATIVE_LINK_REG_OTHER_CLIENT, "Testing sees Stable's handler");
	expect(NativeLinkReg_Register(&ops, EXE_TESTING, 0, &status) == NATIVE_LINK_REG_OK &&
	           status == NATIVE_LINK_REG_THIS_CLIENT,
	       "Testing launch takes over from Stable");
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_OTHER_CLIENT, "Stable now sees Testing's");
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_NOT_OURS && reg.key.present &&
	           reg.removes == 0,
	       "Stable cannot unregister Testing's handler");
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_OK, "Stable launch takes it back");

	// Unregister removes only this client's handler.
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_OK && status == NATIVE_LINK_REG_NONE &&
	           !reg.key.present,
	       "unregister removes this client's handler");
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_NOT_OURS,
	       "unregister with nothing registered does nothing");

	// Another program: never replaced silently.
	SimRegInit(&reg);
	SimRegForeign(&reg);
	saved = reg.key;
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_OTHER_PROGRAM, "foreign handler detected");
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_KEPT_OTHER &&
	           status == NATIVE_LINK_REG_OTHER_PROGRAM && reg.writes == 0,
	       "launch leaves another program's handler alone");
	expect(memcmp(&reg.key, &saved, sizeof saved) == 0, "foreign handler untouched");
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_NOT_OURS && reg.removes == 0,
	       "unregister never removes another program's handler");

	// The explicit action replaces it.
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 1, &status) == NATIVE_LINK_REG_OK &&
	           status == NATIVE_LINK_REG_THIS_CLIENT,
	       "explicit action replaces another program's handler");
	expect(reg.extraSubkey, "replacement leaves the other program's extra subkeys alone");
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_OK && !reg.key.present,
	       "unregister after an explicit replacement removes the handler");

	// A bare key without a command or marker opens nothing.
	SimRegInit(&reg);
	reg.key.present = 1;
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_NONE, "empty key counts as not set up");

	// Unreadable registry.
	SimRegInit(&reg);
	reg.failRead = 1;
	expect(NativeLinkReg_Status(&ops, EXE_STABLE) == NATIVE_LINK_REG_UNKNOWN, "unreadable: unknown");
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_FAILED && reg.writes == 0,
	       "unreadable: nothing written");

	// A path that cannot be quoted safely is never written.
	SimRegInit(&reg);
	expect(NativeLinkReg_Register(&ops, "C:\\100%1\\ctr_native_ap.exe", 0, &status) == NATIVE_LINK_REG_BAD_PATH &&
	           reg.writes == 0 && !reg.key.present,
	       "unsafe path refused before any write");
}

static void TestRollback(void)
{
	SimReg reg;
	NativeLinkRegOps ops;
	NativeLinkRegStatus status;
	NativeLinkRegState saved;

	// Clean machine, partial write: rolled back to no key at all.
	SimRegInit(&reg);
	ops = SimRegOps(&reg);
	reg.failWrite = 1;
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_ROLLED_BACK &&
	           status == NATIVE_LINK_REG_NONE,
	       "partial write on a clean machine rolled back");
	expect(!reg.key.present, "no half-written key left");

	// Write that reads back wrong: rolled back.
	SimRegInit(&reg);
	reg.corruptWrite = 1;
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_ROLLED_BACK && !reg.key.present,
	       "write that does not read back as this client is rolled back");

	// Explicit replacement of another program where no write lands and the
	// restore cannot be written either: reported as failed, and the foreign
	// handler is still exactly as it was.
	SimRegInit(&reg);
	SimRegForeign(&reg);
	saved = reg.key;
	reg.failWritesAfter = 0;
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 1, &status) == NATIVE_LINK_REG_FAILED,
	       "failed replacement with a failed restore is reported as failed");
	expect(memcmp(&reg.key, &saved, sizeof saved) == 0, "foreign handler as it was");

	// Unregister whose delete fails: this client's handler stays whole.
	SimRegInit(&reg);
	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 0, &status) == NATIVE_LINK_REG_OK, "registered for delete failure");
	saved = reg.key;
	reg.failRemove = 1;
	expect(NativeLinkReg_Unregister(&ops, EXE_STABLE, &status) == NATIVE_LINK_REG_ROLLED_BACK &&
	           status == NATIVE_LINK_REG_THIS_CLIENT,
	       "failed unregister reported and handler restored");
	expect(memcmp(&reg.key, &saved, sizeof saved) == 0, "handler unchanged after failed unregister");
}

// A registry whose first write only half-lands and whose later writes work:
// the explicit replacement of another program's handler is undone exactly.
typedef struct
{
	SimReg base;
	int calls;
} FlakyReg;

static int FlakyWrite(void *ctx, const NativeLinkRegState *s)
{
	FlakyReg *r = (FlakyReg *)ctx;
	if (r->calls++ == 0)
	{
		r->base.key.owned = s->owned; // partial: marker only
		snprintf(r->base.key.command, sizeof r->base.key.command, "%s", s->command);
		r->base.key.command[5] = '\0'; // truncated command
		return 0;
	}
	r->base.key = *s;
	r->base.key.present = 1;
	return 1;
}

static int FlakyRead(void *ctx, NativeLinkRegState *out)
{
	*out = ((FlakyReg *)ctx)->base.key;
	return 1;
}

static int FlakyRemove(void *ctx)
{
	memset(&((FlakyReg *)ctx)->base.key, 0, sizeof(NativeLinkRegState));
	return 1;
}

static void TestExactRollback(void)
{
	FlakyReg reg;
	NativeLinkRegOps ops;
	NativeLinkRegStatus status;
	NativeLinkRegState saved;

	memset(&reg, 0, sizeof reg);
	SimRegForeign(&reg.base);
	saved = reg.base.key;
	ops.ctx = &reg;
	ops.read = FlakyRead;
	ops.write = FlakyWrite;
	ops.remove = FlakyRemove;

	expect(NativeLinkReg_Register(&ops, EXE_STABLE, 1, &status) == NATIVE_LINK_REG_ROLLED_BACK &&
	           status == NATIVE_LINK_REG_OTHER_PROGRAM,
	       "half-landed replacement of another program is rolled back");
	expect(memcmp(&reg.base.key, &saved, sizeof saved) == 0,
	       "the other program's description, protocol marker and command are exactly restored");
	expect(!reg.base.key.owned, "no ownership marker left on another program's handler");
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
	TestRollback();
	TestExactRollback();
	TestTexts();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
