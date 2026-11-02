// cc -std=c11 -Wall -Wextra -Werror -o /tmp/test-race-stats tools/test-race-stats.c
//
// Only Adventure drives the room's kart stats (ruling of 2026-11-02). Arcade
// (retail in a room, the custom pages, Arcade cups), Time Trial, Versus and
// Battle drive vanilla stats. Two parts:
//
// 1. ap/ap_race_stats_logic.h, the one rule, against every game mode the
//    player can race in.
// 2. The two writers of the room's stats ask that rule: AP_CapabilityStats
//    (progressive stats, every frame) treats the pack as off outside Adventure,
//    and AP_CharSwap_ApplyStatPackage (editable stats, Penta's PAL table, the
//    starting class) writes nothing there. Read from the source, run from the
//    repository root as CI does.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ap/ap_race_stats_logic.h"

// gameMode1 bits as in include/namespace_Main.h (ap_capability.c pins the one
// the rule reads).
#define MAIN_MENU 0x2000u
#define TIME_TRIAL 0x20000u
#define ADVENTURE_MODE 0x80000u
#define ADVENTURE_ARENA 0x100000u
#define ARCADE_MODE 0x400000u
#define RELIC_RACE 0x4000000u
#define CRYSTAL_CHALLENGE 0x8000000u
#define ADVENTURE_CUP 0x10000000u
#define ADVENTURE_BOSS 0x80000000u
#define BATTLE_MODE 0x20u
#define POINT_LIMIT 0x4000u
#define LIFE_LIMIT 0x8000u
#define TIME_LIMIT 0x10000u

static int failures;

static void expect(int actual, int wanted, const char *name)
{
	printf("%s  %-62s %d\n", actual == wanted ? "ok  " : "FAIL", name, actual);
	if (actual != wanted)
	{
		printf("      wanted %d\n", wanted);
		failures++;
	}
}

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n;
	char *buf;
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc((size_t)n + 1);
	if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n)
	{
		free(buf);
		buf = NULL;
	}
	if (buf)
		buf[n] = 0;
	fclose(f);
	return buf;
}

// The body of the function whose definition starts with `head`, up to the
// first line that is a lone closing brace.
static char *body_of(const char *src, const char *head)
{
	const char *start = src ? strstr(src, head) : NULL;
	const char *end;
	char *out;
	if (!start)
		return NULL;
	end = strstr(start, "\n}\n");
	if (!end)
		return NULL;
	out = malloc((size_t)(end - start) + 1);
	memcpy(out, start, (size_t)(end - start));
	out[end - start] = 0;
	return out;
}

// `a` appears in `text` before `b`.
static int before(const char *text, const char *a, const char *b)
{
	const char *pa = text ? strstr(text, a) : NULL;
	const char *pb = text ? strstr(text, b) : NULL;
	return pa && pb && pa < pb;
}

int main(void)
{
	char *cap, *swap, *stats, *package;

	// ── The rule ──
	expect(AP_RaceStats_RoomApplies(ARCADE_MODE), 0, "Arcade single race: vanilla");
	expect(AP_RaceStats_RoomApplies(ARCADE_MODE | MAIN_MENU), 0, "Arcade track select: vanilla");
	expect(AP_RaceStats_RoomApplies(TIME_TRIAL), 0, "Time Trial: vanilla");
	expect(AP_RaceStats_RoomApplies(ARCADE_MODE | RELIC_RACE), 0, "Arcade with a stray relic bit: still vanilla");
	expect(AP_RaceStats_RoomApplies(ADVENTURE_MODE), 1, "Adventure race: room stats");
	expect(AP_RaceStats_RoomApplies(ADVENTURE_MODE | ADVENTURE_CUP), 1, "Adventure cup: room stats");
	expect(AP_RaceStats_RoomApplies(ADVENTURE_MODE | ADVENTURE_BOSS), 1, "Boss race: room stats");
	expect(AP_RaceStats_RoomApplies(ADVENTURE_MODE | RELIC_RACE), 1, "Relic Race: room stats");
	expect(AP_RaceStats_RoomApplies(ADVENTURE_MODE | CRYSTAL_CHALLENGE | ADVENTURE_ARENA), 1,
	       "Crystal Challenge: room stats");
	expect(AP_RaceStats_RoomApplies(ADVENTURE_MODE | ADVENTURE_ARENA), 1, "Adventure hub: room stats");
	expect(AP_RaceStats_RoomApplies(0), 0, "Versus race (no mode bit): vanilla");
	expect(AP_RaceStats_RoomApplies(MAIN_MENU), 0, "Versus track select: vanilla");
	expect(AP_RaceStats_RoomApplies(BATTLE_MODE), 0, "Battle: vanilla");
	expect(AP_RaceStats_RoomApplies(BATTLE_MODE | POINT_LIMIT), 0, "Battle, point limit: vanilla");
	expect(AP_RaceStats_RoomApplies(BATTLE_MODE | LIFE_LIMIT), 0, "Battle, life limit: vanilla");
	expect(AP_RaceStats_RoomApplies(BATTLE_MODE | TIME_LIMIT), 0, "Battle, time limit: vanilla");
	expect(AP_RaceStats_RoomApplies(ADVENTURE_MODE | ADVENTURE_BOSS | RELIC_RACE), 1,
	       "Adventure with any extra bit: room stats");
	expect(AP_RaceStats_RoomApplies(RELIC_RACE), 1, "Relic Race without the Adventure bit: room stats");
	expect(AP_RaceStats_RoomApplies(CRYSTAL_CHALLENGE), 1, "Crystal Challenge bit alone: room stats");
	expect(AP_RaceStats_RoomApplies(ADVENTURE_CUP), 1, "Adventure cup bit alone: room stats");
	expect(AP_RaceStats_RoomApplies(ADVENTURE_BOSS), 1, "Boss bit alone: room stats");
	expect(AP_RaceStats_RoomApplies(TIME_TRIAL | RELIC_RACE), 0, "Time Trial with a stale relic bit: vanilla");
	expect(AP_RaceStats_RoomApplies(BATTLE_MODE | ADVENTURE_ARENA), 0, "Battle with a stale hub bit: vanilla");

	// ── The writers ask it ──
	cap = slurp("ap/ap_capability.c");
	swap = slurp("ap/ap_charswap.c");
	expect(cap != NULL && swap != NULL, 1, "sources readable from the repository root");
	expect(cap && strstr(cap, "AP_RS_ADVENTURE_MODE == ADVENTURE_MODE") != NULL &&
	           strstr(cap, "AP_RS_RELIC_RACE == RELIC_RACE") != NULL && strstr(cap, "AP_RS_BATTLE_MODE == BATTLE_MODE") != NULL,
	       1, "ap_capability.c pins the mode bits");
	expect(cap && strstr(cap, "return AP_RaceStats_RoomApplies(sdata->gGT->gameMode1);") != NULL, 1,
	       "AP_CapabilityRoomStatsRace reads the rule on the live mode");

	stats = body_of(cap, "void AP_CapabilityStats(struct Driver *driver)\n{");
	expect(stats && strstr(stats, "statsLive = AP_CapabilityStatsActive() && AP_CapabilityRoomStatsRace();") != NULL,
	       1, "progressive stats: pack live only in Adventure");
	expect(stats && strstr(stats, "rank = statsLive ? AP_CapabilityAppliedStatRankFor(") != NULL, 1,
	       "progressive stats: a non-Adventure race takes the vanilla anchor");
	expect(stats && strstr(stats, "AP_CapabilityStatsActive() && !weakened") == NULL, 1,
	       "progressive stats: no early test that skips the mode rule");

	package = body_of(swap, "void AP_CharSwap_ApplyStatPackage(struct Driver *driver)\n{");
	expect(package && before(package, "if (!AP_CapabilityRoomStatsRace())", "ap_cs_effectiveValue("), 1,
	       "stat package: a non-Adventure race returns before any write");

	free(stats);
	free(package);
	free(cap);
	free(swap);
	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
