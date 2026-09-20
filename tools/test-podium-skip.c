/*
 * Host harness for the local podium-skip policy (issue #285).
 *
 *   cc -Wall -Wextra -DCTR_AP -I ap -I . -I include \
 *      -o /tmp/test-podium-skip tools/test-podium-skip.c && \
 *      /tmp/test-podium-skip
 *
 * Behavioral assertions on production code, no source-string checks:
 *
 *   - ap/ap_podium_skip_logic.h is the policy game/222.c and
 *     game/UI/UI_RaceFlow.c consume through AP_SkipPodium: an ordinary Trophy,
 *     a CTR Challenge and an ordinary Relic skip when the option is on; a boss,
 *     a Gem Cup and a qualifying Oxide relic never skip; option off skips
 *     nothing. The decision is pure, so the caller can place it after the
 *     race's reward notification without it suppressing anything.
 *   - AP_PodiumSkipCleanGameMode2 clears the INC_* count-up bits and
 *     VEH_FREEZE_PODIUM a watched ceremony clears by its end, and leaves every
 *     other gameMode2 bit alone, so a skipped ceremony ends as a watched one.
 *   - platform/native_config.c persists skip_podium as a Video & QoL CFG_BOOL
 *     (round trip), and the built-in default is off, so a config.ini missing the
 *     key leaves the option off.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "../ap/ap_podium_skip_logic.h"
#include "../platform/native_config.c"

// Engine model ids the podium can carry (namespace_Instance.h). The trophy and
// relic ids are also pinned inside ap_hooks.c against the engine constants.
#define TEST_TROPHY 0x62
#define TEST_RELIC  0x61
#define TEST_KEY    0x63
#define TEST_GEM    0x5f
#define TEST_BIG1   0x38
#define TEST_NOFUNC 0x00

// gameMode2 bits a watched ceremony clears by its end (namespace_Main.h).
#define TEST_INC_RELIC      0x1000000
#define TEST_INC_KEY        0x2000000
#define TEST_INC_TROPHY     0x4000000
#define TEST_FREEZE_PODIUM  0x4
#define TEST_FREEZE_DOOR    0x4000
#define TEST_CUP_NEW_WIN    0x1000
#define TEST_SPAWN_RETAINED 0x2

static int g_failures;

static void expect(const char *what, int got, int want)
{
	printf("%-4s %s (got %d, want %d)\n", got == want ? "ok" : "FAIL", what,
	       got, want);
	if (got != want)
		g_failures++;
}

static const ConfigEntry *FindEntry(const char *section, const char *key)
{
	for (int i = 0; i < g_numConfigEntries; i++)
	{
		const ConfigEntry *e = &g_configEntries[i];
		if (strcmp(section, e->section) == 0 && strcmp(key, e->key) == 0)
			return e;
	}
	return NULL;
}

// Set a config value the way the generic menu writes an entry (CFG_BOOL 0/1).
static void SetEntry(const char *section, const char *key, int value)
{
	const ConfigEntry *e = FindEntry(section, key);
	if (e == NULL)
	{
		printf("FAIL SetEntry: no entry %s/%s\n", section, key);
		g_failures++;
		return;
	}
	if (e->type == CFG_BOOL)
		*(bool *)e->valuePtr = value != 0;
	else
		*(int *)e->valuePtr = value;
}

// ── Classification and decision ─────────────────────────────────────────────

static void TestDecision(void)
{
	// Option off: nothing is skipped, whatever the ceremony is.
	expect("off keeps Trophy",
	       AP_PodiumSkipDecision(0, TEST_TROPHY, 0, 0, 0, 0, 0), 0);
	expect("off keeps CTR Challenge",
	       AP_PodiumSkipDecision(0, TEST_TROPHY, 0, 0, 0, 1, 0), 0);
	expect("off keeps Relic",
	       AP_PodiumSkipDecision(0, TEST_RELIC, 0, 0, 1, 0, 0), 0);
	expect("off keeps Oxide relic",
	       AP_PodiumSkipDecision(0, TEST_RELIC, 0, 0, 1, 0, 1), 0);
	expect("off keeps boss",
	       AP_PodiumSkipDecision(0, TEST_KEY, 1, 0, 0, 0, 0), 0);
	expect("off keeps cup",
	       AP_PodiumSkipDecision(0, TEST_GEM, 0, 1, 0, 0, 0), 0);

	// Option on: ordinary Trophy, CTR Challenge and ordinary Relic skip.
	expect("on skips Trophy",
	       AP_PodiumSkipDecision(1, TEST_TROPHY, 0, 0, 0, 0, 0), 1);
	expect("on skips CTR Challenge",
	       AP_PodiumSkipDecision(1, TEST_TROPHY, 0, 0, 0, 1, 0), 1);
	expect("on skips ordinary Relic",
	       AP_PodiumSkipDecision(1, TEST_RELIC, 0, 0, 1, 0, 0), 1);

	// A qualifying Oxide relic keeps STATIC_RELIC and its ceremony.
	expect("on keeps qualifying Oxide relic",
	       AP_PodiumSkipDecision(1, TEST_RELIC, 0, 0, 1, 0, 1), 0);

	// Boss and cup never skip, even when an ordinary race flag overlaps.
	expect("on keeps boss",
	       AP_PodiumSkipDecision(1, TEST_KEY, 1, 0, 0, 0, 0), 0);
	expect("on keeps boss carrying a Trophy model",
	       AP_PodiumSkipDecision(1, TEST_TROPHY, 1, 0, 0, 0, 0), 0);
	expect("on keeps cup",
	       AP_PodiumSkipDecision(1, TEST_GEM, 0, 1, 0, 0, 0), 0);
	expect("on keeps cup carrying a Trophy model",
	       AP_PodiumSkipDecision(1, TEST_TROPHY, 0, 1, 0, 0, 0), 0);
	expect("on keeps cup carrying the token flag",
	       AP_PodiumSkipDecision(1, TEST_TROPHY, 0, 1, 0, 1, 0), 0);

	// Non-ceremony and other-reward podiums are untouched.
	expect("on keeps no-podium",
	       AP_PodiumSkipDecision(1, TEST_NOFUNC, 0, 0, 0, 0, 0), 0);
	expect("on keeps a bare Key",
	       AP_PodiumSkipDecision(1, TEST_KEY, 0, 0, 0, 0, 0), 0);
	expect("on keeps an Oxide BIG1",
	       AP_PodiumSkipDecision(1, TEST_BIG1, 0, 0, 0, 0, 0), 0);

	// The classification labels the ceremony the production wrapper logs and
	// tests against, in priority order.
	expect("classify boss first",
	       AP_PodiumSkipClassify(TEST_TROPHY, 1, 1, 1, 1),
	       AP_PODIUM_SKIP_BOSS);
	expect("classify cup next",
	       AP_PodiumSkipClassify(TEST_TROPHY, 0, 1, 1, 1), AP_PODIUM_SKIP_CUP);
	expect("classify relic",
	       AP_PodiumSkipClassify(TEST_RELIC, 0, 0, 1, 0), AP_PODIUM_SKIP_RELIC);
	expect("classify CTR Challenge",
	       AP_PodiumSkipClassify(TEST_TROPHY, 0, 0, 0, 1),
	       AP_PODIUM_SKIP_CTR_CHALLENGE);
	expect("classify Trophy",
	       AP_PodiumSkipClassify(TEST_TROPHY, 0, 0, 0, 0), AP_PODIUM_SKIP_TROPHY);
	expect("classify nothing",
	       AP_PodiumSkipClassify(TEST_NOFUNC, 0, 0, 0, 0), AP_PODIUM_SKIP_NONE);

	// The decision is a pure predicate: calling it twice returns the same
	// answer, so placing it after the reward notification cannot suppress a
	// check the notification already sent.
	{
		int first = AP_PodiumSkipDecision(1, TEST_TROPHY, 0, 0, 0, 0, 0);
		int second = AP_PodiumSkipDecision(1, TEST_TROPHY, 0, 0, 0, 0, 0);
		expect("decision is idempotent (first)", first, 1);
		expect("decision is idempotent (second)", second, first);
	}
}

// ── End-state cleanup ───────────────────────────────────────────────────────

static void TestCleanGameMode2(void)
{
	int gm2 = TEST_INC_RELIC | TEST_INC_KEY | TEST_INC_TROPHY |
	          TEST_FREEZE_PODIUM | TEST_FREEZE_DOOR | TEST_CUP_NEW_WIN |
	          TEST_SPAWN_RETAINED;
	int cleaned = AP_PodiumSkipCleanGameMode2(gm2);

	expect("clean clears INC_RELIC", (cleaned & TEST_INC_RELIC), 0);
	expect("clean clears INC_KEY", (cleaned & TEST_INC_KEY), 0);
	expect("clean clears INC_TROPHY", (cleaned & TEST_INC_TROPHY), 0);
	expect("clean clears VEH_FREEZE_PODIUM", (cleaned & TEST_FREEZE_PODIUM), 0);
	expect("clean keeps VEH_FREEZE_DOOR", (cleaned & TEST_FREEZE_DOOR),
	       TEST_FREEZE_DOOR);
	expect("clean keeps CUP_NEW_WIN", (cleaned & TEST_CUP_NEW_WIN),
	       TEST_CUP_NEW_WIN);
	expect("clean keeps the retained spawn bit", (cleaned & TEST_SPAWN_RETAINED),
	       TEST_SPAWN_RETAINED);

	// An already-clean value is unchanged (a skipped ceremony never sets the
	// bits, so this is the common case).
	expect("clean is a no-op on a clean value",
	       AP_PodiumSkipCleanGameMode2(TEST_FREEZE_DOOR), TEST_FREEZE_DOOR);
}

// ── Config persistence ──────────────────────────────────────────────────────

static void TestConfig(void)
{
	// Built-in default: off, so an unset option never changes behavior.
	expect("default skipPodium is off", g_config.skipPodium ? 1 : 0, 0);

	const ConfigEntry *e = FindEntry("Video & QoL", "skip_podium");
	expect("skip_podium entry present", e != NULL, 1);
	if (e != NULL)
	{
		expect("skip_podium is CFG_BOOL", e->type == CFG_BOOL, 1);
		expect("skip_podium points at g_config.skipPodium",
		       e->valuePtr == &g_config.skipPodium, 1);
		expect("skip_podium lives in the visible Video & QoL section",
		       strcmp(e->section, "Video & QoL") == 0, 1);
	}

	char tmpdir[] = "/tmp/test-podium-skip-XXXXXX";
	if (mkdtemp(tmpdir) == NULL)
	{
		printf("FAIL config: cannot create temp dir\n");
		g_failures++;
		return;
	}
	if (chdir(tmpdir) != 0)
	{
		printf("FAIL config: cannot chdir\n");
		g_failures++;
		return;
	}

	// Round trip: save true, reset in memory, reload, observe true.
	SetEntry("Video & QoL", "skip_podium", 1);
	NativeConfig_Save();
	g_config.skipPodium = false;
	NativeConfig_Load();
	expect("persisted skipPodium survives a load", g_config.skipPodium ? 1 : 0, 1);

	// Save writes the key as a plain bool under its section.
	{
		FILE *f = fopen("config.ini", "r");
		char buf[256];
		int inVideo = 0, sawSkip = 0;
		if (f == NULL)
		{
			printf("FAIL config.ini missing after save\n");
			g_failures++;
		}
		else
		{
			while (fgets(buf, sizeof buf, f))
			{
				if (buf[0] == '[')
					inVideo = strncmp(buf, "[Video & QoL]", 13) == 0;
				else if (inVideo && strstr(buf, "skip_podium") != NULL &&
				         strstr(buf, "= true") != NULL)
					sawSkip = 1;
			}
			fclose(f);
		}
		expect("config.ini has skip_podium = true", sawSkip, 1);
	}

	// Missing key: a config.ini without skip_podium leaves the default off.
	{
		FILE *f = fopen("config.ini", "w");
		if (f == NULL)
		{
			printf("FAIL config: cannot write minimal config.ini\n");
			g_failures++;
		}
		else
		{
			fputs("[Video & QoL]\nskip_intro = false\n", f);
			fclose(f);
		}
		g_config.skipPodium = false; // the built-in default
		NativeConfig_Load();
		expect("missing key leaves skipPodium off",
		       g_config.skipPodium ? 1 : 0, 0);
	}
}

int main(void)
{
	TestDecision();
	TestCleanGameMode2();
	TestConfig();

	if (g_failures)
	{
		printf("podium skip: FAIL (%d)\n", g_failures);
		return 1;
	}
	puts("podium skip: PASS");
	return 0;
}
