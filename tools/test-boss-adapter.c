// Host harness for the #169 boss adapter (J03, 2026-10-28, decisions 6 and 8).
// It links nothing from the engine: the rules under test live in
// ap/ap_boss_adapter.h, which game/PickupBots.c and ap/ap_hooks_gates.c call.
//
// Build:
//   gcc -std=c99 -Wall -Wextra -Werror -DAP_BOSS_ADAPTER_PURE tools/test-boss-adapter.c -o /tmp/test-boss-adapter
//   /tmp/test-boss-adapter

#include <stdio.h>
#include <string.h>
#include "../ap/ap_boss_adapter.h"

static int s_checks, s_fail;
#define CHECK(c)                                                                 \
	do                                                                           \
	{                                                                            \
		s_checks++;                                                              \
		if (!(c))                                                                \
		{                                                                        \
			s_fail++;                                                            \
			printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                   \
		}                                                                        \
	} while (0)

static AP_BossAdapterFacts facts(int weapon)
{
	AP_BossAdapterFacts f;
	memset(&f, 0, sizeof f);
	f.weapon = weapon;
	f.numLaps = 3;
	f.lastSegment = 4;
	f.bossRank = 1; // the player leads by default
	f.playerRank = 0;
	return f;
}

static void test_codes(void)
{
	static const int adv[] = {0x00, 0x06, 0x07, 0x08, 0x09, 0x0c, 0x0d};
	static const int retail[] = {0x01, 0x02, 0x03, 0x04, 0x0f, 0x64, 0x66, 0x05, 0x0a, 0x0b};
	unsigned i;
	for (i = 0; i < sizeof adv / sizeof adv[0]; i++)
		CHECK(AP_BossAdapterIsAdvanced(adv[i]));
	// Retail boss tables carry only these: the adapter never sees a retail race.
	for (i = 0; i < sizeof retail / sizeof retail[0]; i++)
		CHECK(!AP_BossAdapterIsAdvanced(retail[i]));
}

static void test_once_per_segment(void)
{
	AP_BossAdapterState s;
	AP_BossAdapterFacts f = facts(AP_BA_SHIELD);
	AP_BossAdapterReset(&s);
	AP_BossAdapterEnter(&s, 0, 1);
	CHECK(!s.used);
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE);
	AP_BossAdapterCommit(&s, &f, AP_BA_FIRE);
	CHECK(s.used && s.armed);
	AP_BossAdapterEnter(&s, 0, 1); // same segment, same lap: still spent
	CHECK(s.used);
	AP_BossAdapterEnter(&s, 0, 2); // next segment: fresh use
	CHECK(!s.used);
	AP_BossAdapterCommit(&s, &f, AP_BA_SKIP);
	CHECK(s.used);
	AP_BossAdapterEnter(&s, 1, 2); // same segment next lap: fresh use
	CHECK(!s.used);
	AP_BossAdapterCommit(&s, &f, AP_BA_WAIT); // a wait keeps the use
	CHECK(!s.used);
}

static void test_families(void)
{
	AP_BossAdapterState s;
	AP_BossAdapterFacts f;
	AP_BossAdapterReset(&s);

	f = facts(AP_BA_CLOCK);
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE);
	f.playerClocked = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_WAIT);

	f = facts(AP_BA_WARP_ORB);
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE); // player leads
	f.warpOrbInPlay = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_WAIT);
	f.warpOrbInPlay = 0;
	f.bossRank = 0;
	f.playerRank = 1; // boss leads: skipped, use spent
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_SKIP);
	f.bossRank = f.playerRank = 0;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_SKIP);

	f = facts(AP_BA_SHIELD);
	f.shieldHeld = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_WAIT);

	f = facts(AP_BA_MASK);
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE);
	f.maskActive = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_WAIT);

	f = facts(AP_BA_INVISIBILITY);
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE);
	f.invisible = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_WAIT);

	// Super engine: N. Oxide's final record only.
	f = facts(AP_BA_SUPER_ENGINE);
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_SKIP);
	f.superAllowed = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE);
	AP_BossAdapterCommit(&s, &f, AP_BA_FIRE);
	CHECK(s.superMs == 0x1e00);
	f.superEngine = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_WAIT);

	// Unknown code: never fired.
	f = facts(0x01);
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_SKIP);
}

static void test_turbo(void)
{
	AP_BossAdapterState s;
	AP_BossAdapterFacts f = facts(AP_BA_TURBO);
	AP_BossAdapterReset(&s);
	f.lap = 0;
	f.segment = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE);
	AP_BossAdapterCommit(&s, &f, AP_BA_FIRE);
	CHECK(s.turboLap == 0);
	f.segment = 3; // second turbo segment, same lap: skipped
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_SKIP);
	f.lap = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE);
	// Final lap: fine before the last segment, never on the final straight.
	f.lap = 2;
	f.segment = 3;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE);
	f.segment = 4;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_SKIP);
	// The last segment on an earlier lap is not the final straight.
	f.lap = 1;
	CHECK(AP_BossAdapterDecide(&s, &f) == AP_BA_FIRE);
	CHECK(AP_BossAdapterFinalStraight(2, 3, 4, 4));
	CHECK(!AP_BossAdapterFinalStraight(1, 3, 4, 4));
	CHECK(!AP_BossAdapterFinalStraight(2, 3, 3, 4));
}

static void test_timers_and_cleanup(void)
{
	AP_BossAdapterState s;
	int t = 0x1e00, frames = 0;
	while (!AP_BossAdapterCountdown(&t, 32))
		frames++;
	CHECK(t == 0);
	CHECK(frames == 0x1e00 / 32 - 1);
	CHECK(!AP_BossAdapterCountdown(&t, 32)); // expired stays expired
	t = 10;
	CHECK(AP_BossAdapterCountdown(&t, 33) && t == 0);

	CHECK(AP_BossAdapterEndRule(1, 0, 0) == AP_BA_END_ALL);
	CHECK(AP_BossAdapterEndRule(0, 1, 0) == AP_BA_END_ALL);
	CHECK(AP_BossAdapterEndRule(0, 0, 1) == AP_BA_END_INVISIBILITY);
	CHECK(AP_BossAdapterEndRule(0, 0, 0) == AP_BA_END_NONE);

	// Race init (restart, exit, failed load) clears every use and timer.
	s.armed = 1;
	s.used = 1;
	s.turboLap = 2;
	s.maskOn = 1;
	s.superMs = 99;
	s.keyLap = 2;
	s.keySeg = 3;
	AP_BossAdapterReset(&s);
	CHECK(!s.armed && !s.used && s.turboLap == -1 && !s.maskOn && !s.superMs && s.keyLap == -1 && s.keySeg == -1);
}

static void test_remap(void)
{
	// Roo's composed segments on Roo's Tubes (78 restart points) raced on a
	// modified copy with 90 and with 40.
	const unsigned char cps[5] = {0, 16, 33, 47, 77};
	unsigned char out[5];
	int i;
	CHECK(AP_BossAdapterRemap(cps, 5, 78, 90, out));
	CHECK(out[0] == 0 && out[4] == 89);
	for (i = 1; i < 5; i++)
		CHECK(out[i] > out[i - 1]);
	CHECK(out[1] == (16 * 89) / 77);
	CHECK(AP_BossAdapterRemap(cps, 5, 78, 40, out));
	CHECK(out[4] == 39);
	// Same count is the identity.
	CHECK(AP_BossAdapterRemap(cps, 5, 78, 78, out) && memcmp(out, cps, 5) == 0);
	// Too few points to keep the segments apart: refused, nothing thrown.
	CHECK(!AP_BossAdapterRemap(cps, 5, 78, 4, out));
	// Nonsense counts.
	CHECK(!AP_BossAdapterRemap(cps, 5, 1, 90, out));
	CHECK(!AP_BossAdapterRemap(cps, 5, 78, 0, out));
	CHECK(!AP_BossAdapterRemap(cps, 0, 78, 90, out));
	CHECK(!AP_BossAdapterRemap(cps, 5, 78, 300, out));
	{
		// A schedule that does not start at 0 cannot be remapped into one.
		const unsigned char bad[3] = {5, 10, 20};
		CHECK(!AP_BossAdapterRemap(bad, 3, 78, 90, out));
	}
}

int main(void)
{
	test_codes();
	test_once_per_segment();
	test_families();
	test_turbo();
	test_timers_and_cleanup();
	test_remap();
	printf("test-boss-adapter: %d checks, %d failed\n", s_checks, s_fail);
	return s_fail ? 1 : 0;
}
