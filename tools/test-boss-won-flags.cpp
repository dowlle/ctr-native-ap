// g++ -m32 -std=c++17 -Wall -Wextra -DCTR_AP -I ap -I ap/vendor/json/include -o /tmp/test-boss-won-flags tools/test-boss-won-flags.cpp
//
// Issue #458: the personally won boss races and N. Oxide's Challenge, one bit
// each in a server data storage value (ap/ap_boss_won_flags.h). Covers the
// bit layout and count, the store rules it shares with the scene flags
// (ap/ap_scene_seen_flags.h), the one-time migration for seeds without the
// key, and the two decisions that used to read checked locations: the goal
// and the Oxide garage. The production wiring is covered by
// tools/test-boss-won-production.py.
#include <cstdio>
#include "ap_boss_won_flags.h"
#include "ap_boss_door_scene_seen.h"
#include "ap_oxide_encounter.h"
#include "ap_goal_logic.h"
#include "ap_locations.h"

static int failures;

#define CHECK(label, expression) do { \
	bool passed = (expression); \
	std::printf("%s  %s\n", passed ? "ok  " : "FAIL", label); \
	failures += !passed; \
} while (0)

static long tableCode(int bit)
{
	for (int i = 0; i < AP_LOCATION_TABLE_LEN; i++)
		if (AP_LOCATION_TABLE[i].bit_index == bit)
			return AP_LOCATION_TABLE[i].location_code;
	return -1;
}

// The Oxide garage inputs exactly as AP_OxideInputs assembles them after #458:
// bosses and the first-challenge flag come from the boss-won bits, never from
// the checked locations (which are deliberately not an input here).
static AP_OxideGarageInputs garage(int goalOxide, unsigned wonBits, int goalBosses,
                                   int finalRelicMet)
{
	AP_OxideGarageInputs in = {};
	in.garageReqMet = 1;
	in.goalOxide = goalOxide;
	in.firstCleared = (wonBits & AP_BOSS_WON_OXIDE_FIRST) != 0;
	in.finalRelicMet = finalRelicMet;
	in.goalBosses = goalBosses;
	in.bossesWon = AP_BossWonCount(wonBits);
	in.goalGems = 0;
	in.gemsHeld = 0;
	in.firstOptional = 0;
	return in;
}

int main()
{
	// ── bit layout and the frozen location ids ──────────────────────────────
	CHECK("boss bits are 1, 2, 4, 8",
	      AP_BOSS_WON_BIT(0) == 1u && AP_BOSS_WON_BIT(1) == 2u &&
	      AP_BOSS_WON_BIT(2) == 4u && AP_BOSS_WON_BIT(3) == 8u);
	CHECK("out of range bosses have no bit",
	      AP_BOSS_WON_BIT(-1) == 0u && AP_BOSS_WON_BIT(4) == 0u);
	CHECK("Oxide bit and present marker outside the boss bits",
	      (AP_BOSS_WON_OXIDE_FIRST & AP_BOSS_WON_BOSS_ALL) == 0 &&
	      (AP_BOSS_WON_PRESENT & (AP_BOSS_WON_BOSS_ALL | AP_BOSS_WON_OXIDE_FIRST)) == 0 &&
	      AP_BOSS_WON_ALL == (AP_BOSS_WON_BOSS_ALL | AP_BOSS_WON_OXIDE_FIRST | AP_BOSS_WON_PRESENT));
	bool codesMatch = true;
	for (int b = 0; b < AP_BOSS_WON_BOSS_COUNT; b++)
		codesMatch = codesMatch && tableCode(94 + b) == AP_BOSS_WON_LOCATION_CODE(b);
	CHECK("boss location ids match AP_LOCATION_TABLE bits 94-97", codesMatch);
	CHECK("Oxide's Challenge id matches AP_LOCATION_TABLE",
	      tableCode(AP_GOAL_BIT_OXIDE_FIRST) == AP_BOSS_WON_OXIDE_FIRST_CODE);
	CHECK("any_percent wire value matches the encounter header",
	      AP_BOSS_WON_GOAL_OXIDE_ANY == AP_OXIDE_GOAL_ANY);

	// ── counting ───────────────────────────────────────────────────────────
	CHECK("count: none", AP_BossWonCount(0) == 0);
	CHECK("count: one", AP_BossWonCount(4) == 1);
	CHECK("count: all four", AP_BossWonCount(15) == 4);
	CHECK("count: Oxide and present bits are not bosses",
	      AP_BossWonCount(AP_BOSS_WON_OXIDE_FIRST | AP_BOSS_WON_PRESENT) == 0);
	CHECK("count: everything", AP_BossWonCount(AP_BOSS_WON_ALL) == 4);

	// ── the store ─────────────────────────────────────────────────────────
	APBossWonFlags f;
	CHECK("record before any connect is refused", !f.recordBoss(0) && f.bits == 0);
	f.connect("host", "seed:a", 0, 1);
	CHECK("key names seed, team and slot", f.key == "ctr_boss_won_v1:6:seed:a:0:1");
	CHECK("connected, Get not answered: unknown", !f.known());
	CHECK("a win before the Get reply is recorded", f.recordBoss(2) && f.bossWon(2));
	CHECK("but not sent before the Get reply", !f.wantsSend());
	CHECK("and counts", f.bossesWon() == 1);
	CHECK("existing key: no migration", !f.retrievedNeedsSeed(1));
	CHECK("server bit merged with the local win", f.bossWon(0) && f.bossWon(2) && f.bossesWon() == 2);
	CHECK("local win now sent", f.wantsSend() && f.pending == 4u);
	f.sent = true;
	f.reply(5);
	CHECK("reply confirms", f.pending == 0 && !f.wantsSend());
	CHECK("second win of the same boss records nothing", !f.recordBoss(2));
	CHECK("Oxide's Challenge recorded once", f.recordOxideFirst() && !f.recordOxideFirst() && f.oxideFirstWon());
	CHECK("a stale lower value cannot clear a win", (f.reply(0), f.bossWon(0) && f.bossWon(2)));

	// Offline win: kept for the same seed and slot, sent after the next Get.
	APBossWonFlags d;
	d.connect("host", "seed:a", 0, 1);
	d.retrievedNeedsSeed(AP_BOSS_WON_PRESENT);
	d.disconnected();
	CHECK("win while disconnected is recorded", d.recordBoss(3) && d.bossesWon() == 1);
	d.connect("host", "seed:a", 0, 1);
	CHECK("same seed reconnect keeps it, nothing sent before Get", d.bossWon(3) && !d.wantsSend());
	CHECK("no migration on a present key", !d.retrievedNeedsSeed(AP_BOSS_WON_PRESENT));
	CHECK("then the held win is sent", d.wantsSend() && d.pending == 8u);

	// A different room, seed, team or slot starts clean.
	const char *what[] = {"other endpoint clears", "other seed clears", "other team clears", "other slot clears"};
	for (int i = 0; i < 4; i++)
	{
		APBossWonFlags r;
		r.connect("host", "seed:a", 0, 1);
		r.retrievedNeedsSeed(15);
		r.recordOxideFirst();
		if (i == 0) r.connect("other", "seed:a", 0, 1);
		if (i == 1) r.connect("host", "seed:b", 0, 1);
		if (i == 2) r.connect("host", "seed:a", 1, 1);
		if (i == 3) r.connect("host", "seed:a", 0, 2);
		CHECK(what[i], r.bits == 0 && r.pending == 0 && !r.known() && !r.seeded);
	}

	// Values another tool could have written: unknown for the session, and no
	// migration over them.
	for (auto v : {nlohmann::json(-1), nlohmann::json(64), nlohmann::json(1.5),
	               nlohmann::json(true), nlohmann::json("1"), nlohmann::json::array()})
	{
		APBossWonFlags b;
		b.connect("host", "seed:a", 0, 1);
		if (b.retrievedNeedsSeed(v) || b.known() || b.bossesWon() != 0)
		{
			std::printf("FAIL  malformed value accepted: %s\n", v.dump().c_str());
			failures++;
		}
	}
	std::printf("ok    malformed values leave the flags unknown\n");

	// Separate key from the boss-door scene flag.
	APBossDoorSceneSeen door;
	door.connect("host", "seed:a", 0, 1);
	CHECK("separate key from the boss-door scene", door.key != f.key);

	// ── migration ─────────────────────────────────────────────────────────
	const unsigned roo = AP_BOSS_WON_BIT(0), papu = AP_BOSS_WON_BIT(1);
	CHECK("migration: nothing checked still makes the key present",
	      AP_BossWonMigrationBits(0, 0) == AP_BOSS_WON_PRESENT);
	CHECK("migration: checked bosses are copied",
	      AP_BossWonMigrationBits(roo | papu, 2) == (AP_BOSS_WON_PRESENT | roo | papu));
	CHECK("migration: Oxide's Challenge copied outside any_percent",
	      (AP_BossWonMigrationBits(AP_BOSS_WON_OXIDE_FIRST, 0) & AP_BOSS_WON_OXIDE_FIRST) &&
	      (AP_BossWonMigrationBits(AP_BOSS_WON_OXIDE_FIRST, 2) & AP_BOSS_WON_OXIDE_FIRST) &&
	      (AP_BossWonMigrationBits(AP_BOSS_WON_OXIDE_FIRST, 3) & AP_BOSS_WON_OXIDE_FIRST));
	CHECK("migration: Oxide's Challenge NOT copied under any_percent",
	      AP_BossWonMigrationBits(AP_BOSS_WON_OXIDE_FIRST | roo, 1) == (AP_BOSS_WON_PRESENT | roo));
	CHECK("migration: stray bits are masked",
	      AP_BossWonMigrationBits(0xFFFFFFFFu, 2) == AP_BOSS_WON_ALL);

	// An old seed in progress, first connect with this client: key absent,
	// Ripper Roo and Papu Papu checked on the server.
	APBossWonFlags m;
	m.connect("host", "seed:old", 0, 1);
	CHECK("old seed: key absent asks for migration", m.retrievedNeedsSeed(nullptr));
	m.seed(AP_BossWonMigrationBits(roo | papu, 0));
	CHECK("old seed: both bosses count after migration", m.bossesWon() == 2);
	CHECK("old seed: migration is sent with the present marker",
	      m.wantsSend() && m.pending == (AP_BOSS_WON_PRESENT | roo | papu));
	CHECK("old seed: migration does not repeat on this connection", !m.retrievedNeedsSeed(nullptr));
	m.sent = true;
	m.reply(AP_BOSS_WON_PRESENT | roo | papu);
	// Same seed later: the key is present, so a boss checked by a later Collect
	// is never copied again.
	APBossWonFlags later;
	later.connect("host", "seed:old", 0, 1);
	CHECK("later connect: present key, no migration",
	      !later.retrievedNeedsSeed(AP_BOSS_WON_PRESENT | roo | papu));
	CHECK("later connect: count stays 2", later.bossesWon() == 2);

	// A brand-new seed with nothing checked: the key is created empty.
	APBossWonFlags fresh;
	fresh.connect("host", "seed:new", 0, 1);
	CHECK("new seed: key absent asks for migration", fresh.retrievedNeedsSeed(nullptr));
	fresh.seed(AP_BossWonMigrationBits(0, 1));
	CHECK("new seed: no boss counted, key created",
	      fresh.bossesWon() == 0 && fresh.pending == AP_BOSS_WON_PRESENT && fresh.wantsSend());

	// Migration is per identity: a reconnect to the same seed after an
	// unconfirmed copy does not copy twice; another seed copies afresh.
	APBossWonFlags twice;
	twice.connect("host", "seed:x", 0, 1);
	twice.retrievedNeedsSeed(nullptr);
	twice.seed(AP_BossWonMigrationBits(roo, 0));
	twice.disconnected();
	twice.connect("host", "seed:x", 0, 1);
	CHECK("same identity: no second copy", !twice.retrievedNeedsSeed(nullptr) && twice.pending != 0);
	twice.connect("host", "seed:y", 0, 1);
	CHECK("new identity: copy runs again", twice.retrievedNeedsSeed(nullptr));

	// ── #458: a Collect-marked boss location does not count ────────────────
	// The checked set is deliberately not an input: four Collect-marked boss
	// races with no flag set must not meet goal_bosses 4; four flags must.
	unsigned noneWon = 0, allWon = AP_BOSS_WON_BOSS_ALL;
	CHECK("bosses goal: four Collect-marked bosses, none raced, not met",
	      !AP_ComposedGoalMet(0, 0, 0, 4, AP_BossWonCount(noneWon), 0, 0));
	CHECK("bosses goal: four personal wins, met",
	      AP_ComposedGoalMet(0, 0, 0, 4, AP_BossWonCount(allWon), 0, 0));
	CHECK("bosses goal: three wins of four, not met",
	      !AP_ComposedGoalMet(0, 0, 0, 4, AP_BossWonCount(7), 0, 0));

	// any_percent plus four bosses: Collect-marked bosses keep the garage shut.
	{
		AP_OxideGarageInputs in = garage(AP_OXIDE_GOAL_ANY, noneWon, 4, 0);
		AP_OxideGarageState st = AP_OxideGarageEvaluate(&in);
		CHECK("any_percent + bosses: Collect-marked bosses keep Oxide 1 shut",
		      st.encounter == AP_OXIDE_ENCOUNTER_FIRST && !st.open);
		in = garage(AP_OXIDE_GOAL_ANY, allWon, 4, 0);
		st = AP_OxideGarageEvaluate(&in);
		CHECK("any_percent + bosses: four personal wins open Oxide 1",
		      st.encounter == AP_OXIDE_ENCOUNTER_FIRST && st.open);
	}

	// ── #458 softlock: Oxide's Challenge collected by someone else ─────────
	// any_percent, Oxide's Challenge holds another world's filler and that
	// player collects: the location is checked, but the Oxide bit is unset.
	{
		AP_OxideGarageInputs in = garage(AP_OXIDE_GOAL_ANY, noneWon, 0, 0);
		AP_OxideGarageState st = AP_OxideGarageEvaluate(&in);
		CHECK("any_percent, Oxide's Challenge collected: garage still offers Oxide 1",
		      st.encounter == AP_OXIDE_ENCOUNTER_FIRST && st.open);
		// The player then wins it: the session flag and the Oxide bit are set,
		// and the goal completes.
		CHECK("any_percent: the first win completes the goal",
		      AP_ComposedGoalMet(AP_OXIDE_GOAL_ANY, 1, 0, 0, 0, 0, 0));
		// Even with every relic held the garage does not jump to the Final.
		in = garage(AP_OXIDE_GOAL_ANY, noneWon, 0, 1);
		st = AP_OxideGarageEvaluate(&in);
		CHECK("any_percent, Oxide's Challenge collected, relics held: still Oxide 1",
		      st.encounter == AP_OXIDE_ENCOUNTER_FIRST);
		// Migration under any_percent does not import the Collect mark either.
		in = garage(AP_OXIDE_GOAL_ANY, AP_BossWonMigrationBits(AP_BOSS_WON_OXIDE_FIRST, AP_OXIDE_GOAL_ANY), 0, 1);
		st = AP_OxideGarageEvaluate(&in);
		CHECK("any_percent, migrated seed with Oxide's Challenge collected: Oxide 1",
		      st.encounter == AP_OXIDE_ENCOUNTER_FIRST && st.open);
	}
	// 101_percent: a personal first win offers the Final across reconnects.
	{
		AP_OxideGarageInputs in = garage(AP_OXIDE_GOAL_FINAL, AP_BOSS_WON_OXIDE_FIRST, 0, 1);
		AP_OxideGarageState st = AP_OxideGarageEvaluate(&in);
		CHECK("101_percent: personal first win offers the Final Challenge",
		      st.encounter == AP_OXIDE_ENCOUNTER_FINAL && st.open);
		in = garage(AP_OXIDE_GOAL_FINAL, AP_BossWonMigrationBits(AP_BOSS_WON_OXIDE_FIRST, AP_OXIDE_GOAL_FINAL), 0, 1);
		st = AP_OxideGarageEvaluate(&in);
		CHECK("101_percent: migrated first win offers the Final Challenge",
		      st.encounter == AP_OXIDE_ENCOUNTER_FINAL);
	}

	std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
