// cc -m32 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I ap -I . -I include
//    -o /tmp/test-hit-bots-gather tools/test-hit-bots-gather.c -lm
//
// Link-level coverage for the REAL BOTS-side gather (ap/ap_hit_bots.c,
// AP_HitBotsVictim). game/BOTS.c cannot be linked off-engine, so the gather was
// extracted into that unit and is compiled here with stubbed engine globals.
//
// Round-2 defect A: the Adventure test must use ADVENTURE_MODE, not the hub-only
// ADVENTURE_ARENA. This harness sets a trophy-race gameMode1 (ADVENTURE_MODE set,
// ADVENTURE_ARENA clear) and asserts the raceSupported flag is present. Against
// the pre-fix code (ADVENTURE_ARENA) this assertion fails.

#include <stdio.h>
#include <string.h>

#include <common.h>

struct sData sdata_static;
struct Data data;

// The decision sink: record the victim/damage/flags the gather produced.
static int g_calls;
static int g_victim;
static int g_damage;
static unsigned g_flags;

int AP_HitEncounterOnDamage(int victimEngineID, int damageType, unsigned flags)
{
	g_calls++;
	g_victim = victimEngineID;
	g_damage = damageType;
	g_flags = flags;
	return 1;
}

#include "../ap/ap_hit_bots.c"

static struct GameTracker gGT;
static struct Driver drivers[8];
static struct Instance instances[8];
static struct Thread threads[8];

static int g_checks;
static int g_failures;

static void expect(int ok, const char *what)
{
	g_checks++;
	if (!ok)
	{
		printf("FAIL: %s\n", what);
		g_failures++;
	}
}

static void expect_eq(int got, int want, const char *what)
{
	g_checks++;
	if (got != want)
	{
		printf("FAIL: %s (got %d, want %d)\n", what, got, want);
		g_failures++;
	}
}

static void setup(void)
{
	int i;
	memset(&gGT, 0, sizeof gGT);
	memset(drivers, 0, sizeof drivers);
	memset(instances, 0, sizeof instances);
	memset(threads, 0, sizeof threads);
	memset(&data, 0, sizeof data);
	sdata_static.gGT = &gGT;

	for (i = 0; i < 8; i++)
	{
		drivers[i].driverID = i;
		drivers[i].actionsFlagSet = ACTION_BOT;
		drivers[i].instSelf = &instances[i];
		instances[i].thread = &threads[i];
		threads[i].flags = 0;
		threads[i].modelIndex = 0;
		gGT.drivers[i] = &drivers[i];
		data.characterIDs[i] = (s16)(10 + i);
	}
	// P1 is a human attacker; victims are the AI seats.
	drivers[0].actionsFlagSet = 0;
	threads[0].modelIndex = 0;

	g_calls = 0;
	g_victim = -1;
	g_damage = -1;
	g_flags = 0;
}

static void hit(int victimSlot, int damageType, int attackerSlot, int wasDamageActive)
{
	AP_HitBotsVictim(&drivers[victimSlot], damageType,
	                 attackerSlot < 0 ? NULL : &drivers[attackerSlot], wasDamageActive);
}

static void test_trophy_race_adventure_mode(void)
{
	// A trophy race: ADVENTURE_MODE set, ADVENTURE_ARENA cleared (the pad load
	// removes it). The Hit must be accepted.
	setup();
	gGT.gameMode1 = ADVENTURE_MODE;
	gGT.gameMode2 = 0;
	hit(1, 1, 0, 0);
	expect_eq(g_calls, 1, "trophy race calls the decision");
	expect_eq(g_victim, 11, "victim engine id from characterIDs");
	expect_eq(g_damage, 1, "damage type passed through");
	expect((g_flags & 128u) != 0, "trophy race: ADVENTURE_MODE yields raceSupported");
	expect((g_flags & 1u) != 0, "victim is AI");
	expect((g_flags & 32u) != 0, "attacker is local P1");
	expect((g_flags & 256u) == 0, "fresh damage is not flagged active");
}

static void test_hub_arena_is_not_a_race(void)
{
	// The hub flag alone (no ADVENTURE_MODE) is not a supported race.
	setup();
	gGT.gameMode1 = ADVENTURE_ARENA;
	hit(1, 1, 0, 0);
	expect_eq(g_calls, 1, "hub still gathers");
	expect((g_flags & 128u) == 0, "ADVENTURE_ARENA alone is not raceSupported");
}

static void test_boss_race_supported(void)
{
	// Correction C: a boss Hit check is reachable at the boss race whether or not
	// the boss is cleared. ADVENTURE_BOSS is the sign bit (IS_BOSS_RACE).
	setup();
	gGT.gameMode1 = ADVENTURE_MODE | (int)ADVENTURE_BOSS;
	gGT.gameMode2 = 0;
	hit(1, 1, 0, 0);
	expect_eq(g_calls, 1, "boss race gathers");
	expect((g_flags & 128u) != 0, "boss race is raceSupported");
	expect((g_flags & 32u) != 0, "boss race: player attacker");
}

static void test_unsupported_modes(void)
{
	static const int modes[] = {
	    ADVENTURE_MODE | ARCADE_MODE,
	    ADVENTURE_MODE | ADVENTURE_CUP,
	    ADVENTURE_MODE | TIME_TRIAL,
	    ADVENTURE_MODE | BATTLE_MODE,
	    ADVENTURE_MODE | RELIC_RACE,
	    ADVENTURE_MODE | CRYSTAL_CHALLENGE,
	};
	for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++)
	{
		setup();
		gGT.gameMode1 = modes[i];
		hit(1, 1, 0, 0);
		expect((g_flags & 128u) == 0, "unsupported mode is not raceSupported");
	}
	// Token race lives in gameMode2.
	setup();
	gGT.gameMode1 = ADVENTURE_MODE;
	gGT.gameMode2 = TOKEN_RACE;
	hit(1, 1, 0, 0);
	expect((g_flags & 128u) == 0, "token race is not raceSupported");
}

static void test_victim_validation(void)
{
	setup();
	gGT.gameMode1 = ADVENTURE_MODE;

	// Invalid slot index: no call at all.
	drivers[1].driverID = 9;
	hit(1, 1, 0, 0);
	expect_eq(g_calls, 0, "out-of-range driverID is rejected before characterIDs");

	setup();
	gGT.gameMode1 = ADVENTURE_MODE;
	drivers[1].instSelf = NULL;
	hit(1, 1, 0, 0);
	expect((g_flags & 128u) == 0, "NULL instSelf is not a live race");

	setup();
	gGT.gameMode1 = ADVENTURE_MODE;
	threads[1].flags = THREAD_FLAG_DEAD;
	hit(1, 1, 0, 0);
	expect((g_flags & 128u) == 0, "dead thread is not a live race");
}

static void test_attribution_flags(void)
{
	setup();
	gGT.gameMode1 = ADVENTURE_MODE;
	hit(1, 1, -1, 0);
	expect((g_flags & 16u) == 0, "null attacker not present");
	expect((g_flags & 32u) == 0, "null attacker not P1");

	setup();
	gGT.gameMode1 = ADVENTURE_MODE;
	drivers[2].actionsFlagSet = ACTION_BOT;
	hit(1, 1, 2, 0);
	expect((g_flags & 64u) != 0, "AI attacker flagged");

	setup();
	gGT.gameMode1 = ADVENTURE_MODE;
	drivers[0].actionsFlagSet = ACTION_BOT;
	hit(1, 1, 0, 0);
	// The gather still marks P1 as the local attacker AND as an AI; the pure
	// decision rejects on the AI bit, so no check is sent.
	expect((g_flags & 64u) != 0, "P1 with ACTION_BOT is flagged as an AI attacker");

	// wasDamageActive propagates for the type-1/type-4 distinction.
	setup();
	gGT.gameMode1 = ADVENTURE_MODE;
	hit(1, 4, 0, 1);
	expect((g_flags & 256u) != 0, "pre-call damage-active flag propagates");
}

int main(void)
{
	test_trophy_race_adventure_mode();
	test_hub_arena_is_not_a_race();
	test_boss_race_supported();
	test_unsupported_modes();
	test_victim_validation();
	test_attribution_flags();

	printf("%s: %d checks, %d failures\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
