// cc -Wall -Wextra -DCTR_AP -I ap -I . -I include -o /tmp/test-roster-swap tools/test-roster-swap.c -lm
//
// The roster swap (ap/ap_roster_swap.h) with the real field draw
// (ap/ap_hit_policy.h): guest racers with random bosses, the gauntlet's drawn
// racer and the chaos Final Challenge's field and attack turns. The engine side,
// the model load and how a race with these fields plays are Steam rows.
#include <stdio.h>
#include <string.h>

#include "ap_seedcfg.h"
#include "ap_hit_policy.h"
#include "ap_roster_swap.h"

static int s_fail, s_pass;
#define CHECK(cond, what)                                 \
	do                                                    \
	{                                                     \
		if (cond)                                         \
			s_pass++;                                     \
		else                                              \
		{                                                 \
			s_fail++;                                     \
			printf("FAIL %s: %s\n", what, #cond);         \
		}                                                 \
	} while (0)

enum { CRASH = 0, CORTEX = 2, PURA = 7, PINSTRIPE = 8, PAPU = 9, ROO = 10, JOE = 11, TROPY = 12, PENTA = 13, FAKE = 14, OXIDE = 15 };

static const int kRetail[6] = {ROO, PAPU, JOE, PINSTRIPE, OXIDE, OXIDE};

static int in_stock(int id, int player) { return AP_HitInStockPure(id, player); }

static void draw(const int ident[6], unsigned checked, int player, int seats, ap_hit_field *f)
{
	static const int order[16] = {3, 14, 1, 9, 5, 0, 12, 7, 15, 2, 10, 4, 8, 13, 6, 11};
	unsigned char eligible[16], unchecked[16];
	ap_hit_cursors cur;
	int i;
	memset(unchecked, 0, sizeof unchecked);
	for (i = 0; i < 16; i++)
		eligible[i] = (unsigned char)AP_RosterEligibleNoHit(ident, i, checked);
	AP_HitCursorsResetPure(&cur);
	AP_HitDrawFieldPure(order, eligible, unchecked, player, seats, &cur, f);
}

static int seated(const ap_hit_field *f, int id)
{
	int i;
	for (i = 0; i < f->count; i++)
		if (f->ids[i] == id)
			return 1;
	return 0;
}

static void test_eligibility(void)
{
	const int crash[6] = {CRASH, PAPU, JOE, PINSTRIPE, OXIDE, OXIDE};
	const int final[6] = {ROO, PAPU, JOE, PINSTRIPE, CORTEX, CORTEX};
	CHECK(AP_RosterDefaultEligible(kRetail, CRASH, 0), "a default not drawn races");
	CHECK(!AP_RosterDefaultEligible(crash, CRASH, 0), "Crash on hub 1 is out");
	CHECK(AP_RosterDefaultEligible(crash, CRASH, 1u << 0), "beaten or collected, Crash is back");
	CHECK(!AP_RosterDefaultEligible(crash, CRASH, 1u << 1), "another garage does not free him");
	CHECK(!AP_RosterDefaultEligible(final, CORTEX, 0), "the final racer waits");
	CHECK(AP_RosterDefaultEligible(final, CORTEX, 1u << 4), "either N. Oxide race frees the final racer");
	CHECK(AP_RosterDefaultEligible(final, CORTEX, 1u << 5), "either N. Oxide race frees the final racer");

	CHECK(AP_RosterEligibleNoHit(crash, ROO, 0), "the ruling's example: Roo in the starting pool");
	CHECK(!AP_RosterEligibleNoHit(crash, PAPU, 0), "a boss in its own garage waits");
	CHECK(AP_RosterEligibleNoHit(crash, PAPU, 1u << 1), "beaten, it joins");
	CHECK(!AP_RosterEligibleNoHit(crash, TROPY, 0), "no Hit block, no track guests");
	{
		const int tropy[6] = {TROPY, PAPU, JOE, PINSTRIPE, OXIDE, OXIDE};
		CHECK(!AP_RosterEligibleNoHit(tropy, TROPY, 0), "a drawn guest waits");
		CHECK(AP_RosterEligibleNoHit(tropy, TROPY, 1u << 0), "a drawn guest joins when beaten");
	}
	CHECK(AP_RosterEligibleNoHit(final, OXIDE, 0), "N. Oxide displaced races from the start");
	CHECK(!AP_RosterEligibleNoHit(crash, -1, 0) && !AP_RosterEligibleNoHit(crash, 16, 0), "bounds");
}

static void test_triggers(void)
{
	const int swapped[6] = {TROPY, ROO, CRASH, PINSTRIPE, OXIDE, OXIDE};
	long any[AP_HIT_TRIGGER_WINS_MAX];
	int kind, n;

	n = AP_HitBossGuestTrigger(swapped, TROPY, any, &kind);
	CHECK(n == 1 && any[0] == 35011100L && kind == 1, "a drawn guest waits for its garage only");
	n = AP_HitBossGuestTrigger(swapped, ROO, any, &kind);
	CHECK(n == 1 && any[0] == 35011101L && kind == 1, "Roo on another garage");
	n = AP_HitBossGuestTrigger(swapped, PAPU, any, &kind);
	CHECK(n == 0 && kind == 1, "Papu raced nowhere has no win");
	n = AP_HitBossGuestTrigger(swapped, PENTA, any, &kind);
	CHECK(n == 2 && kind == 0, "a guest drawn nowhere keeps its track wins");
	n = AP_HitBossGuestTrigger(kRetail, TROPY, any, &kind);
	CHECK(n == 2 && kind == 0, "the retail table is untouched");
	n = AP_HitBossGuestTrigger(kRetail, OXIDE, any, &kind);
	CHECK(n == 2 && any[0] == 35011104L && kind == 1, "both N. Oxide races");

	// N. Oxide with his races disabled on a random-boss seed: his codes seat
	// nobody, so he has no win and races from the start (fixes 2026-11-02).
	{
		AP_BossEncounters e;
		int seats[6];
		const int disabled[6] = {CRASH, PAPU, JOE, PINSTRIPE, -1, -1};
		memset(&e, 0, sizeof e);
		e.state = AP_BOSS_ENC_VALID;
		e.version = AP_BOSS_ENCOUNTERS_VERSION_2;
		e.opponent[0] = CRASH;
		e.opponent[1] = PAPU;
		e.opponent[2] = JOE;
		e.opponent[3] = PINSTRIPE;
		AP_HitBossSeats(&e, seats);
		CHECK(seats[4] == OXIDE && seats[5] == OXIDE, "his races in the seed: he waits for them");
		e.oxideDisabled = 1;
		AP_HitBossSeats(&e, seats);
		CHECK(seats[0] == CRASH && seats[4] == -1 && seats[5] == -1, "his races disabled: his codes seat nobody");
		n = AP_HitBossGuestTrigger(seats, OXIDE, any, &kind);
		CHECK(n == 0 && kind == 1, "N. Oxide disabled has no win on a random-boss seed");
		CHECK(AP_RosterEligibleNoHit(disabled, OXIDE, 0), "N. Oxide disabled races from the start without Hit");
		CHECK(!AP_RosterEligibleNoHit(disabled, CRASH, 0) && AP_RosterEligibleNoHit(disabled, ROO, 0),
		      "the garages swap as before");
		e.opponent[0] = ROO;
		AP_HitBossSeats(&e, seats);
		CHECK(seats[4] == OXIDE && seats[5] == OXIDE, "retail bosses keep the retail table and its Keys");
	}
}

static void test_draw(void)
{
	const int one[6] = {CRASH, PAPU, JOE, PINSTRIPE, OXIDE, OXIDE};
	const int four[6] = {0, 1, 2, 3, OXIDE, OXIDE};
	const int five[6] = {0, 1, 2, 3, 4, 4};
	ap_hit_field f;

	draw(one, 0, 5, 7, &f);
	CHECK(f.count == 7, "full field");
	CHECK(!seated(&f, CRASH), "Crash sits out while the field fills");
	CHECK(seated(&f, ROO), "Roo races in his place");

	draw(one, 1u << 0, 5, 7, &f);
	CHECK(f.count == 7, "full field after the win");

	// Four and five defaults held: three extras cannot cover them. The race
	// runs with fewer karts; a held racer never takes a seat early (fixes
	// 2026-11-02). Never above three extras, never a duplicate, never the player.
	draw(four, 0, 5, 7, &f);
	CHECK(f.count == 6 && f.unhit + f.other == 3, "four held: six seats");
	CHECK(!seated(&f, 0) && !seated(&f, 1) && !seated(&f, 2) && !seated(&f, 3), "four held: none seated");
	draw(five, 0, 7, 7, &f);
	CHECK(f.count == 5 && f.unhit + f.other == 3 && !seated(&f, 7), "five held: five seats");
	CHECK(!seated(&f, 0) && !seated(&f, 4), "five held: none seated");
	draw(four, 0, 5, 4, &f);
	CHECK(f.count == 4, "a four seat cup still fills");
	{
		int i, j, seen = 0;
		for (i = 0; i < f.count; i++)
			for (j = i + 1; j < f.count; j++)
				seen += f.ids[i] == f.ids[j];
		CHECK(seen == 0, "no duplicate seat");
	}

	draw(one, 0, ROO, 4, &f);
	CHECK(f.count == 4 && !seated(&f, ROO), "the player is never seated, four seat cup");

	// No swap: the stock draw of every older seed, whatever the order.
	draw(kRetail, 0xf, 3, 7, &f);
	CHECK(f.count == 7, "retail field fills");
}

static void test_seat(void)
{
	int ids[7] = {ROO, PAPU, JOE, 0, 1, 2, 3};
	int base[7];

	memcpy(base, ids, sizeof ids);
	CHECK(AP_RosterSeatAttacker(ids, 7, PAPU, 5, 3, in_stock) == -1 && !memcmp(ids, base, sizeof ids), "already races");
	CHECK(AP_RosterSeatAttacker(ids, 7, 5, 5, 3, in_stock) == -1, "the player is not seated");
	CHECK(AP_RosterSeatAttacker(ids, 7, -1, 5, 3, in_stock) == -1, "an unarmed leg changes nothing");
	CHECK(AP_RosterSeatAttacker(ids, 0, ROO, 5, 3, in_stock) == -1, "no field");

	// Extras full: a fourth model cannot load, so it takes an extra's seat.
	CHECK(AP_RosterSeatAttacker(ids, 7, OXIDE, 5, 3, in_stock) == 2 && ids[2] == OXIDE, "full extras swap an extra");
	CHECK(ids[3] == 0 && ids[0] == ROO, "the rest stay");

	{
		int stock[7] = {ROO, 0, 1, 2, 3, 4, 6};
		CHECK(AP_RosterSeatAttacker(stock, 7, OXIDE, 5, 3, in_stock) == 6 && stock[6] == OXIDE, "room for a model: the last stock seat");
		CHECK(stock[0] == ROO, "a waiting Hit target keeps its seat");
	}
	{
		int stock[7] = {ROO, PAPU, JOE, 0, 1, 2, 3};
		CHECK(AP_RosterSeatAttacker(stock, 7, 6, 5, 3, in_stock) == 6 && stock[6] == 6, "a stock attacker costs no model");
	}
	{
		// Every leg of a cup: at most three extras, whatever the four attackers.
		const int attackers[4] = {ROO, PAPU, JOE, PINSTRIPE};
		int leg, i;
		for (leg = 0; leg < 4; leg++)
		{
			int field[7] = {TROPY, PENTA, FAKE, 0, 1, 2, 3};
			int extras = 0;
			AP_RosterSeatAttacker(field, 7, attackers[leg], 5, 3, in_stock);
			for (i = 0; i < 7; i++)
				extras += !in_stock(field[i], 5);
			CHECK(extras <= 3, "a leg stays within the slots");
			CHECK(field[2] == attackers[leg], "the drawn racer races that leg");
		}
	}
}

static void test_chaos(void)
{
	const int retail[6] = {ROO, PAPU, JOE, PINSTRIPE, OXIDE, OXIDE};
	const int mixed[6] = {CRASH, ROO, TROPY, PENTA, FAKE, FAKE};
	int lineup[5], extras[5], n;

	AP_RosterChaosLineup(retail, lineup);
	CHECK(lineup[0] == OXIDE && lineup[1] == ROO && lineup[4] == PINSTRIPE, "final racer first");
	n = AP_RosterChaosExtras(lineup, 5, in_stock, extras, 5);
	CHECK(n == 5, "the retail five need five models: more than the three slots");
	n = AP_RosterChaosExtras(lineup, ROO, in_stock, extras, 5);
	CHECK(n == 4, "the player's own racer is in the pack");
	AP_RosterChaosLineup(mixed, lineup);
	n = AP_RosterChaosExtras(lineup, 5, in_stock, extras, 5);
	CHECK(n == 4, "a stock rival loads from the pack");
	CHECK(lineup[0] == FAKE && lineup[1] == CRASH, "drawn five");
	n = AP_RosterChaosExtras(lineup, 5, in_stock, extras, 2);
	CHECK(n == 2, "never above the cap");

	{
		unsigned char all[5] = {1, 1, 1, 1, 1}, none[5] = {0, 0, 0, 0, 0}, some[5] = {0, 0, 1, 0, 0};
		int cursor = 0, shots[5] = {0}, i;
		for (i = 0; i < 50; i++)
		{
			int owner = AP_RosterChaosOwner(cursor, all, 5);
			shots[owner]++;
			cursor = (owner + 1) % 5;
		}
		CHECK(shots[0] == 10 && shots[4] == 10, "five rivals take even turns");
		CHECK(AP_RosterChaosOwner(0, none, 5) == -1, "nobody can fire");
		CHECK(AP_RosterChaosOwner(3, some, 5) == 2, "a stuck rival loses the turn");
		CHECK(AP_RosterChaosOwner(9, all, 5) == 0, "a bad cursor is safe");
		CHECK(AP_RosterChaosOwner(0, all, 0) == -1, "no field");
	}

	// Never a mirror kart of the player (fixes 2026-11-02): the player's racer
	// among the five gives the seat to the first free id of the player's pack.
	{
		int seated5[5], seat;
		AP_RosterChaosLineup(retail, lineup);
		seat = AP_RosterChaosSeat(lineup, 5, in_stock, seated5);
		CHECK(seat == -1 && !memcmp(seated5, lineup, sizeof lineup), "no duplicate: the five as drawn");
		seat = AP_RosterChaosSeat(lineup, ROO, in_stock, seated5);
		CHECK(seat == 1 && seated5[1] == 0 && in_stock(seated5[1], ROO), "Roo driving: a stand-in from his pack");
		CHECK(AP_RosterChaosStandIn(lineup, 1, seated5[1]) && !AP_RosterChaosStandIn(lineup, 0, seated5[0]),
		      "the stand-in takes no turn");
		n = AP_RosterChaosExtras(seated5, ROO, in_stock, extras, 5);
		CHECK(n == 4, "a stand-in costs no model");
		AP_RosterChaosLineup(mixed, lineup);
		seat = AP_RosterChaosSeat(lineup, CRASH, in_stock, seated5);
		CHECK(seat == 1 && seated5[1] == 1, "Crash driving: Coco stands in");
		{
			const int defaults[6] = {0, 1, 2, 3, 4, 4};
			int i, j, dup = 0;
			AP_RosterChaosLineup(defaults, lineup);
			seat = AP_RosterChaosSeat(lineup, 4, in_stock, seated5);
			CHECK(seat == 0 && seated5[0] == 5, "the final racer's seat: the next free default");
			for (i = 0; i < 5; i++)
				for (j = i + 1; j < 5; j++)
					dup += seated5[i] == seated5[j];
			CHECK(dup == 0, "no duplicate seat");
		}
	}
}

int main(void)
{
	test_eligibility();
	test_triggers();
	test_draw();
	test_seat();
	test_chaos();
	printf("roster swap: %d passed, %d failed\n", s_pass, s_fail);
	return s_fail != 0;
}
