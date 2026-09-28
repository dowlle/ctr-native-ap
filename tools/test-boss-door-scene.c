/*
 * Host harness for the once-per-hub boss-door scene (issue #377).
 *
 * ap/ap_boss_door_scene.c is compiled in and driven with stubbed inputs: the
 * seed's garage rule (AP_BossGarageOpen), the boss race location's checked
 * state (AP_LocationCheckedByBit) and the server-stored seen flag
 * (ap_net_boss_door_scene_*). The stub flag store keeps the same contract as
 * ap/ap_boss_door_scene_seen.h (tools/test-boss-door-scene-seen.cpp covers
 * that struct itself). The VehBirth and CS_Camera call sites, and the
 * unchanged retail rule without CTR_AP, are compiled from the production
 * source by tools/test-boss-door-scene-production.py.
 *
 * Build line (parsed by tools/ci/run-harnesses.py):
 */

// cc -m32 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I ap -I . -I include \
//    -o /tmp/test-boss-door-scene tools/test-boss-door-scene.c -lm && \
//    /tmp/test-boss-door-scene

#include <stdio.h>

#include <common.h>
#include "ap_boss_door_scene_logic.h" // AP_BOSS_DOOR_HUB_BIT for the stub store

struct sData sdata_static;

// ── stubbed inputs ──────────────────────────────────────────────────────────

static int g_cfgActive;
static int g_garageOpen[4];
static int g_bossChecked[4];  // boss race location checked (boss beaten)
static int g_flagKnown;       // Get reply for the flag arrived
static unsigned g_flagBits;   // server-stored bitmask, bit h = hub h
static int g_records;
static int g_lastCheckedBit = -1;

int ctr_cfg_active(void) { return g_cfgActive; }

int AP_BossGarageOpen(int bossIdx)
{
	return (bossIdx >= 0 && bossIdx < 4) ? g_garageOpen[bossIdx] : 1;
}

int AP_LocationCheckedByBit(int globalBit)
{
	int b = globalBit - ADV_REWARD_FIRST_BOSS_KEY;
	g_lastCheckedBit = globalBit;
	return (b >= 0 && b < 4) ? g_bossChecked[b] : 0;
}

int ap_net_boss_door_scene_known(void) { return g_flagKnown; }

int ap_net_boss_door_scene_seen(int hub)
{
	return (g_flagBits & AP_BOSS_DOOR_HUB_BIT(hub)) != 0;
}

void ap_net_boss_door_scene_record(int hub)
{
	unsigned b = AP_BOSS_DOOR_HUB_BIT(hub);
	if (b == 0 || (g_flagBits & b))
		return;
	g_flagBits |= b;
	g_records++;
}

// The production unit game/game_unity.h compiles into the game.
#include "../ap/ap_boss_door_scene.c"
#include "ap_podium_skip_logic.h" // AP_CutsceneSkipDecision (#421)

static int g_failures;

static void expect(const char *what, int got, int want)
{
	printf("%-4s %s (got %d, want %d)\n", got == want ? "ok" : "FAIL", what,
	       got, want);
	if (got != want)
		g_failures++;
}

static void Reset(void)
{
	int i;
	g_cfgActive = 1;
	for (i = 0; i < 4; i++)
	{
		g_garageOpen[i] = 1;
		g_bossChecked[i] = 0;
	}
	g_flagKnown = 1;
	g_flagBits = 0;
	g_records = 0;
	g_lastCheckedBit = -1;
}

// The podium hands over to the scene exactly when VehBirth spawned the driver
// off the podium, which is AP_BossDoorSceneReady with slot_data active. This
// is one Trophy win in `hub`: spawn decision, then the CS_Camera record rule
// (watched or skipped, both go through the same record).
static int TrophyWin(int hub)
{
	int spawnedAtDoor = AP_BossDoorSceneReady(hub);
	if (AP_BossDoorSceneTaken(1, spawnedAtDoor))
		AP_BossDoorSceneMarkPlayed(hub);
	return spawnedAtDoor;
}

static void TestPureRule(void)
{
	int hub, g, b, k, s;

	for (hub = -2; hub <= 5; hub++)
		for (g = 0; g < 2; g++)
			for (b = 0; b < 2; b++)
				for (k = 0; k < 2; k++)
					for (s = 0; s < 2; s++)
					{
						int want = hub >= 0 && hub < 4 && g && !b && k && !s;
						if (AP_BossDoorSceneWanted(hub, g, b, k, s) != want)
						{
							printf("FAIL pure rule hub=%d garage=%d beaten=%d known=%d seen=%d\n",
							       hub, g, b, k, s);
							g_failures++;
						}
					}
	printf("ok   pure rule: every input combination\n");

	expect("taken: trophy podium off the podium", AP_BossDoorSceneTaken(1, 1), 1);
	expect("taken: trophy podium on the podium", AP_BossDoorSceneTaken(1, 0), 0);
	expect("taken: key or relic podium never records", AP_BossDoorSceneTaken(0, 1), 0);
}

static void TestComposition(void)
{
	int hub;

	// Garage closed under the seed's rules: stay on the podium.
	Reset();
	g_garageOpen[0] = 0;
	expect("garage closed: no scene", TrophyWin(0), 0);
	expect("garage closed: nothing recorded", g_records, 0);

	// Boss already beaten (boss race location checked): no scene, even with
	// the hub's Trophies and no Key held.
	Reset();
	g_bossChecked[1] = 1;
	expect("boss beaten: no scene", TrophyWin(1), 0);
	expect("boss beaten reads the boss race location",
	       g_lastCheckedBit, ADV_REWARD_FIRST_BOSS_KEY + 1);

	// First time in a hub with the garage open: the scene plays. The second
	// Trophy win in the same hub stays on the podium.
	Reset();
	expect("first trophy win: scene plays", TrophyWin(2), 1);
	expect("first play recorded for the hub", (int)g_flagBits, 1 << 2);
	expect("second trophy win: no scene", TrophyWin(2), 0);
	expect("third trophy win: no scene", TrophyWin(2), 0);
	expect("recorded once", g_records, 1);

	// Per hub: another hub still plays its own scene once.
	expect("other hub: plays once", TrophyWin(3), 1);
	expect("other hub: then no more", TrophyWin(3), 0);
	expect("two hubs recorded", (int)g_flagBits, (1 << 2) | (1 << 3));

	// Seen on the server from an earlier session.
	Reset();
	g_flagBits = 1 << 0;
	expect("seen before this session: no scene", TrophyWin(0), 0);

	// Flag not read yet (offline, or the Get reply has not arrived): fail safe,
	// same as the Oxide scene: no scene, nothing recorded.
	Reset();
	g_flagKnown = 0;
	expect("flag unknown: no scene", TrophyWin(0), 0);
	expect("flag unknown: nothing recorded", g_records, 0);

	// Skip Cutscenes: the podium still hands over (BoolGotoBoss true) and the
	// skip replaces the scene; the record runs before either path, so a
	// skipped scene counts as seen.
	Reset();
	{
		int spawned = AP_BossDoorSceneReady(0);
		int skipped = AP_CutsceneSkipDecision(1, spawned);
		expect("skip: the podium would go to the scene", spawned, 1);
		expect("skip: the option skips it", skipped, 1);
		if (AP_BossDoorSceneTaken(1, spawned))
			AP_BossDoorSceneMarkPlayed(0);
		expect("skip: counts as seen", ap_net_boss_door_scene_seen(0), 1);
		expect("skip: next trophy win stays on the podium", TrophyWin(0), 0);
	}

	// Not a boss hub (Gem Stone Valley is -1, race tracks are negative too).
	Reset();
	for (hub = -26; hub < 0; hub++)
		if (AP_BossDoorSceneReady(hub))
		{
			printf("FAIL non-hub %d spawned at a boss door\n", hub);
			g_failures++;
		}
	expect("hub 4 is not a boss hub", AP_BossDoorSceneReady(4), 0);
	AP_BossDoorSceneMarkPlayed(-1);
	AP_BossDoorSceneMarkPlayed(4);
	expect("non-hub records nothing", g_records, 0);

	// Without slot_data VehBirth keeps the retail rule and never asks
	// AP_BossDoorSceneReady; the record is a no-op, so a retail session
	// behaves exactly as before.
	Reset();
	g_cfgActive = 0;
	AP_BossDoorSceneMarkPlayed(0);
	expect("no slot_data: nothing recorded", g_records, 0);
}

int main(void)
{
	TestPureRule();
	TestComposition();
	printf("\n%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
