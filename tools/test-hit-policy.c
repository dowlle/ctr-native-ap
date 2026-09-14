// cc -std=c99 -Wall -Wextra -DCTR_AP -I ap -I . -I include -o /tmp/test-hit-policy tools/test-hit-policy.c -lm
//
// Freestanding coverage for the Hit Character encounter decisions
// (ap/ap_hit_policy.h, block schema 2). Every ordering / exclusion /
// attribution rule the engine gather relies on is pinned here without the engine:
//
//   1. the pool draw: unhit guests first (at most 3), other non-stock ids in the
//      free extra slots, stock fill; rotation through the destination order
//   2. player exclusion (the effective player after a racer lock)
//   3. field sizes 7 (ordinary) and 4 (retail Purple cup)
//   4. guarantees G1 to G4 by exhaustive simulation (every player, every
//      unlocked-guest subset, sampled unchecked sets, every start cursor)
//   5. the pad opportunity (lowest eligible non-player unchecked id)
//   5. BOTS_ChangeState damage acceptance and negative attribution
//   6. extra-model planning for every one of the sixteen player choices
//   7. load-queue capacity

#include <stdio.h>
#include <string.h>

#include "../ap/ap_hit_policy.h"

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

static const int kIdentity[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const int kShuffled[16] = {7, 4, 11, 14, 8, 1, 10, 0, 12, 2, 3, 6, 9, 5, 13, 15};

static void elig_of(unsigned char *e, unsigned guestMask)
{
	int i;
	for (i = 0; i < 16; i++)
		e[i] = (unsigned char)(i < 8 ? 1 : ((guestMask >> (i - 8)) & 1u));
}

static void all_unchecked(unsigned char *u)
{
	memset(u, 1, CTR_CFG_HIT_CHARACTER_COUNT);
}

static int in_field(const ap_hit_field *f, int id)
{
	int i;
	for (i = 0; i < f->count; i++)
		if (f->ids[i] == id)
			return 1;
	return 0;
}

static void expect_field(const ap_hit_field *f, const int *want, int n, const char *what)
{
	int i;
	expect_eq(f->count, n, what);
	for (i = 0; i < n && i < f->count; i++)
		expect_eq(f->ids[i], want[i], what);
}

static void test_draw_basics(void)
{
	unsigned char elig[16], unc[16];
	ap_hit_cursors cur;
	ap_hit_field f;

	all_unchecked(unc);

	// No guest: stock only, player excluded, walk order from position 0.
	elig_of(elig, 0);
	AP_HitCursorsResetPure(&cur);
	AP_HitDrawFieldPure(kIdentity, elig, unc, 0, 7, &cur, &f);
	{
		static const int want[7] = {1, 2, 3, 4, 5, 6, 7};
		expect_field(&f, want, 7, "no guest: the seven other defaults");
	}
	expect_eq(f.unhit, 0, "no guest: no unhit seat");
	expect_eq(f.other, 0, "no guest: no other seat");

	// One unlocked unhit guest (Fake Crash) is seated first (G1).
	elig_of(elig, 1u << (14 - 8));
	AP_HitCursorsResetPure(&cur);
	AP_HitDrawFieldPure(kIdentity, elig, unc, 0, 7, &cur, &f);
	{
		static const int want[7] = {14, 1, 2, 3, 4, 5, 6};
		expect_field(&f, want, 7, "one unhit guest first");
	}
	expect_eq(f.unhit, 1, "one unhit seat");

	// Three unhit guests: all seated, four stock seats (G1).
	elig_of(elig, (1u << 4) | (1u << 5) | (1u << 6));
	AP_HitCursorsResetPure(&cur);
	AP_HitDrawFieldPure(kIdentity, elig, unc, 0, 7, &cur, &f);
	{
		static const int want[7] = {12, 13, 14, 1, 2, 3, 4};
		expect_field(&f, want, 7, "three unhit guests first");
	}
	// Next fresh draw: the same three guests, stock rotates on (G3).
	AP_HitDrawFieldPure(kIdentity, elig, unc, 0, 7, &cur, &f);
	{
		static const int want[7] = {12, 13, 14, 5, 6, 7, 1};
		expect_field(&f, want, 7, "stock rotates on the next draw");
	}

	// Eight unlocked unhit guests: windows of three rotate (G2).
	elig_of(elig, 0xFFu);
	AP_HitCursorsResetPure(&cur);
	AP_HitDrawFieldPure(kIdentity, elig, unc, 0, 7, &cur, &f);
	expect_eq(f.ids[0], 8, "window 1 starts at 8");
	expect_eq(f.ids[2], 10, "window 1 ends at 10");
	AP_HitDrawFieldPure(kIdentity, elig, unc, 0, 7, &cur, &f);
	expect_eq(f.ids[0], 11, "window 2 starts at 11");
	AP_HitDrawFieldPure(kIdentity, elig, unc, 0, 7, &cur, &f);
	{
		static const int want[3] = {14, 15, 8};
		int i;
		for (i = 0; i < 3; i++)
			expect_eq(f.ids[i], want[i], "window 3 wraps");
	}

	// The player is an unhit guest: never seated.
	elig_of(elig, (1u << 4) | (1u << 5) | (1u << 6));
	AP_HitCursorsResetPure(&cur);
	AP_HitDrawFieldPure(kIdentity, elig, unc, 14, 7, &cur, &f);
	expect(!in_field(&f, 14), "player guest never seated");
	expect_eq(f.unhit, 2, "the other two unhit guests seated");
	// A guest player's pack lacks Pura: she takes an extra slot (other pool).
	expect(in_field(&f, 7), "Pura seated through the free extra slot");
	expect_eq(f.other, 1, "Pura counted as other");

	// Guest player with three unhit guests: Pura is crowded out.
	elig_of(elig, (1u << 4) | (1u << 5) | (1u << 6));
	AP_HitCursorsResetPure(&cur);
	AP_HitDrawFieldPure(kIdentity, elig, unc, 15, 7, &cur, &f);
	expect(!in_field(&f, 7), "Pura crowded out by three unhit guests");
	expect_eq(f.unhit, 3, "three unhit seats");

	// Checked guests fill the free extra slots.
	elig_of(elig, 0x0Fu | (1u << 6));
	all_unchecked(unc);
	unc[8] = unc[9] = unc[10] = unc[11] = 0;
	AP_HitCursorsResetPure(&cur);
	AP_HitDrawFieldPure(kIdentity, elig, unc, 0, 7, &cur, &f);
	{
		static const int want[7] = {14, 8, 9, 1, 2, 3, 4};
		expect_field(&f, want, 7, "checked guests fill the two free extra slots");
	}
	expect_eq(f.other, 2, "two other seats");

	// Purple cup: four seats.
	all_unchecked(unc);
	elig_of(elig, (1u << 4) | (1u << 5) | (1u << 6));
	AP_HitCursorsResetPure(&cur);
	AP_HitDrawFieldPure(kIdentity, elig, unc, 0, 4, &cur, &f);
	{
		static const int want[4] = {12, 13, 14, 1};
		expect_field(&f, want, 4, "purple cup four seats");
	}

	// NULL order -> empty field.
	AP_HitCursorsResetPure(&cur);
	expect_eq(AP_HitDrawFieldPure(NULL, elig, unc, 0, 7, &cur, &f), 0, "null order -> empty");

	// Stock set matches LOAD_Robots1P.
	{
		int st[7];
		int i;
		AP_HitStockPure(3, st);
		{
			static const int want[7] = {0, 1, 2, 4, 5, 6, 7};
			for (i = 0; i < 7; i++)
				expect_eq(st[i], want[i], "stock for a default player");
		}
		AP_HitStockPure(12, st);
		for (i = 0; i < 7; i++)
			expect_eq(st[i], i, "stock for a guest player is 0..6");
		for (i = 0; i < 16; i++)
		{
			int k, want = 0;
			AP_HitStockPure(5, st);
			for (k = 0; k < 7; k++)
				if (st[k] == i)
					want = 1;
			expect_eq(AP_HitInStockPure(i, 5), want, "in-stock agrees with the stock set");
		}
	}

	// Defaults are always eligible; guests need their trigger.
	{
		unsigned char none[CTR_CFG_HIT_CHARACTER_COUNT];
		int i;
		memset(none, 0, sizeof none);
		for (i = 0; i < 8; i++)
			expect_eq(AP_HitGuestEligiblePure(i, none), 1, "default always eligible");
		for (i = 8; i < 16; i++)
			expect_eq(AP_HitGuestEligiblePure(i, none), 0, "guest needs trigger");
		none[14] = 1;
		expect_eq(AP_HitGuestEligiblePure(14, none), 1, "guest trigger met");
	}
}

// G1 to G4 by exhaustive simulation. For every order, player, unlocked-guest
// subset, unchecked set (all, plus a deterministic sample) and start cursor,
// hold the pools fixed and draw six fresh fields.
static unsigned s_lcg = 20260914u;
static unsigned lcg(void)
{
	s_lcg = s_lcg * 1103515245u + 12345u;
	return (s_lcg >> 16) & 0x7FFFu;
}

static void test_guarantees(void)
{
	const int *orders[2] = {kIdentity, kShuffled};
	int o, player, start, variant;
	unsigned mask;
	long draws = 0;
	int bad = 0;

	for (o = 0; o < 2; o++)
		for (player = 0; player < 16; player++)
			for (mask = 0; mask < 256; mask++)
				for (variant = 0; variant < 3; variant++)
				{
					unsigned char elig[16], unc[16];
					unsigned char stock[16] = {0};
					int st[7], i, k, nUnhit = 0, nOther = 0, free;
					int unhit[16], other[16];

					elig_of(elig, mask);
					for (i = 0; i < 16; i++)
						unc[i] = (unsigned char)(variant == 0 ? 1 : (lcg() % 10) < 6);
					AP_HitStockPure(player, st);
					for (i = 0; i < 7; i++)
						stock[st[i]] = 1;
					for (i = 8; i < 16; i++)
						if (i != player && elig[i] && unc[i])
							unhit[nUnhit++] = i;
					for (i = 0; i < 16; i++)
					{
						int isUnhit = 0;
						for (k = 0; k < nUnhit; k++)
							if (unhit[k] == i)
								isUnhit = 1;
						if (i != player && !stock[i] && elig[i] && !isUnhit)
							other[nOther++] = i;
					}
					free = CTR_CFG_HIT_MAX_GUESTS - (nUnhit < 3 ? nUnhit : 3);

					for (start = 0; start < 16; start++)
					{
						ap_hit_cursors cur;
						ap_hit_field seen[6];
						int d, w;
						cur.unhit = cur.other = cur.stock = start;
						for (d = 0; d < 6; d++)
						{
							AP_HitDrawFieldPure(orders[o], elig, unc, player, 7, &cur, &seen[d]);
							draws++;
							// Invariants: full, distinct, never the player, <= 3 extras.
							if (seen[d].count != 7 || in_field(&seen[d], player))
								bad++;
							{
								int extras = 0, a, b;
								for (a = 0; a < 7; a++)
								{
									if (!stock[seen[d].ids[a]])
										extras++;
									if (!elig[seen[d].ids[a]])
										bad++;
									for (b = a + 1; b < 7; b++)
										if (seen[d].ids[a] == seen[d].ids[b])
											bad++;
								}
								if (extras > 3)
									bad++;
							}
							// G1: at most three unhit -> all seated every draw.
							if (nUnhit <= 3)
								for (k = 0; k < nUnhit; k++)
									if (!in_field(&seen[d], unhit[k]))
										bad++;
						}
						// G2: any ceil(n/3) consecutive draws cover the unhit set.
						w = nUnhit ? (nUnhit + 2) / 3 : 1;
						for (d = 0; d + w <= 6; d++)
							for (k = 0; k < nUnhit; k++)
							{
								int hit = 0, e;
								for (e = d; e < d + w; e++)
									hit |= in_field(&seen[e], unhit[k]);
								if (!hit)
									bad++;
							}
						// G3: any two consecutive draws cover the stock set.
						for (d = 0; d + 2 <= 6; d++)
							for (k = 0; k < 7; k++)
								if (!in_field(&seen[d], st[k]) && !in_field(&seen[d + 1], st[k]))
									bad++;
						// G4: with a free extra slot, the other pool rotates.
						if (free > 0 && nOther > 0)
						{
							int span = (nOther + free - 1) / free;
							for (d = 0; d + span <= 6; d++)
								for (k = 0; k < nOther; k++)
								{
									int hit = 0, e;
									for (e = d; e < d + span; e++)
										hit |= in_field(&seen[e], other[k]);
									if (!hit)
										bad++;
								}
						}
					}
				}
	printf("guarantees: %ld draws simulated\n", draws);
	expect_eq(bad, 0, "G1 to G4 and draw invariants hold in every simulated state");
}

static void test_opportunity(void)
{
	unsigned char elig[16], unc[16];

	elig_of(elig, 0);
	memset(unc, 0, sizeof unc);
	expect_eq(AP_HitOpportunityPure(elig, unc, 0), -1, "everything checked -> none");
	unc[14] = 1;
	expect_eq(AP_HitOpportunityPure(elig, unc, 0), -1, "locked guest is no opportunity");
	elig[14] = 1;
	expect_eq(AP_HitOpportunityPure(elig, unc, 0), 14, "unlocked unhit guest");
	expect_eq(AP_HitOpportunityPure(elig, unc, 14), -1, "player guest excluded");
	// Defaults count (correction A still holds under the pool draw).
	unc[3] = 1;
	expect_eq(AP_HitOpportunityPure(elig, unc, 0), 3, "unchecked default counts, lowest id");
	expect_eq(AP_HitOpportunityPure(elig, unc, 3), 14, "player default excluded");
	memset(unc, 0, sizeof unc);
	unc[7] = 1;
	expect_eq(AP_HitOpportunityPure(elig, unc, 15), 7, "Pura for a guest player");
	expect_eq(AP_HitOpportunityPure(elig, unc, 7), -1, "only the player's own Hit left -> none");
}

static void test_pad_dest_eligible(void)
{
	expect_eq(AP_HitPadDestEligiblePure(3, 1), 1, "retail track eligible");
	expect_eq(AP_HitPadDestEligiblePure(16, 1), 1, "valid trial 16 eligible");
	expect_eq(AP_HitPadDestEligiblePure(17, 1), 1, "valid trial 17 eligible");
	expect_eq(AP_HitPadDestEligiblePure(16, 0), 0, "invalid trial 16 refused");
	expect_eq(AP_HitPadDestEligiblePure(17, 0), 0, "invalid trial 17 refused");
	expect_eq(AP_HitPadDestEligiblePure(18, 1), 0, "arena refused");
	expect_eq(AP_HitPadDestEligiblePure(100, 1), 0, "cup refused");
}

static void test_damage_acceptance(void)
{
	// Base accepted shape: AI victim, live, non-ghost, not player, local P1
	// attacker present and not AI.
	expect_eq(AP_HitDamageAcceptedPure(1, 14, 1, 1, 0, 0, 1, 1, 0), 1, "type 1 accepted");
	expect_eq(AP_HitDamageAcceptedPure(2, 0, 1, 1, 0, 0, 1, 1, 0), 1, "type 2 accepted");
	expect_eq(AP_HitDamageAcceptedPure(3, 7, 1, 1, 0, 0, 1, 1, 0), 1, "type 3 accepted");
	expect_eq(AP_HitDamageAcceptedPure(4, 15, 1, 1, 0, 0, 1, 1, 0), 1, "type 4 accepted");

	expect_eq(AP_HitDamageAcceptedPure(0, 14, 1, 1, 0, 0, 1, 1, 0), 0, "type 0 rejected");
	expect_eq(AP_HitDamageAcceptedPure(5, 14, 1, 1, 0, 0, 1, 1, 0), 0, "type 5 mask-grab rejected");
	expect_eq(AP_HitDamageAcceptedPure(9, 14, 1, 1, 0, 0, 1, 1, 0), 0, "unknown type rejected");
	expect_eq(AP_HitDamageAcceptedPure(-1, 14, 1, 1, 0, 0, 1, 1, 0), 0, "negative type rejected");

	expect_eq(AP_HitDamageAcceptedPure(1, 16, 1, 1, 0, 0, 1, 1, 0), 0, "engine id 16 rejected");
	expect_eq(AP_HitDamageAcceptedPure(1, -1, 1, 1, 0, 0, 1, 1, 0), 0, "engine id -1 rejected");

	expect_eq(AP_HitDamageAcceptedPure(1, 14, 0, 1, 0, 0, 1, 1, 0), 0, "human victim rejected");
	expect_eq(AP_HitDamageAcceptedPure(1, 14, 1, 0, 0, 0, 1, 1, 0), 0, "nonlive victim rejected");
	expect_eq(AP_HitDamageAcceptedPure(1, 14, 1, 1, 1, 0, 1, 1, 0), 0, "ghost victim rejected");
	expect_eq(AP_HitDamageAcceptedPure(1, 14, 1, 1, 0, 1, 1, 1, 0), 0, "player victim rejected");

	expect_eq(AP_HitDamageAcceptedPure(1, 14, 1, 1, 0, 0, 0, 0, 0), 0, "no attacker rejected");
	expect_eq(AP_HitDamageAcceptedPure(1, 14, 1, 1, 0, 0, 1, 0, 0), 0, "non-P1 attacker rejected");
	expect_eq(AP_HitDamageAcceptedPure(1, 14, 1, 1, 0, 0, 1, 1, 1), 0, "AI attacker rejected");
}

static void test_effective_player(void)
{
	expect_eq(AP_HitEffectivePlayerPure(3, 14, 1), 14, "met lock overrides chosen");
	expect_eq(AP_HitEffectivePlayerPure(3, 14, 0), 3, "unmet lock keeps chosen");
	expect_eq(AP_HitEffectivePlayerPure(3, -1, 0), 3, "no lock keeps chosen");
	expect_eq(AP_HitEffectivePlayerPure(14, 14, 1), 14, "lock equals chosen");
}

static void test_extras(void)
{
	// The pure planner: selected ids not in stock (player excluded).
	{
		int selected[3] = {7, 14, 0};
		int stock[8] = {0, 1, 2, 3, 4, 5, 6};
		int out[3] = {-1, -1, -1};
		int n = AP_HitExtrasPlanPure(selected, 3, stock, 7, 14, out, 3);
		expect_eq(n, 1, "extras: Pura needed, guest is player");
		expect_eq(out[0], 7, "extras: Pura planned");
	}
	{
		int selected[2] = {14, 7};
		int stock[8] = {0, 1, 2, 3, 4, 5, 6};
		int out[3] = {-1, -1, -1};
		int n = AP_HitExtrasPlanPure(selected, 2, stock, 7, 15, out, 3);
		expect_eq(n, 2, "extras: guest + Pura for a nondefault player");
		expect_eq(out[0], 14, "extras: guest planned first");
		expect_eq(out[1], 7, "extras: Pura planned second");
	}
	{
		// Overflow reports the full need but writes only cap.
		int selected[2] = {14, 7};
		int stock[8] = {0, 1, 2, 3, 4, 5, 6};
		int out[1] = {-1};
		int n = AP_HitExtrasPlanPure(selected, 2, stock, 7, 15, out, 1);
		expect_eq(n, 2, "extras: overflow need reported");
		expect_eq(out[0], 14, "extras: only cap written");
	}
	{
		// A default player's stock covers every default base id.
		int selected[7] = {7, 0, 1, 2, 3, 4, 5};
		int stock[8] = {0, 1, 2, 4, 5, 6, 7};
		int out[3] = {-1, -1, -1};
		int n = AP_HitExtrasPlanPure(selected, 7, stock, 7, 3, out, 3);
		expect_eq(n, 0, "extras: default player needs none");
	}
}

static void test_queue_capacity(void)
{
	expect_eq(AP_HitQueueFitsPure(0, 2, 8), 1, "queue: 0+2 fits");
	expect_eq(AP_HitQueueFitsPure(6, 2, 8), 1, "queue: 6+2 fits exactly");
	expect_eq(AP_HitQueueFitsPure(7, 2, 8), 0, "queue: 7+2 overflows");
	expect_eq(AP_HitQueueFitsPure(8, 1, 8), 0, "queue: full");
	expect_eq(AP_HitQueueFitsPure(0, 0, 8), 1, "queue: nothing needed");
	expect_eq(AP_HitQueueFitsPure(0, -1, 8), 0, "queue: negative rejected");
}

static void test_ordinary_scope(void)
{
	// Single-player ordinary Adventure Trophy: retail tracks 0..15 AND the two
	// trial Trophy tracks 16/17. Everything else is refused. The first argument
	// is ADVENTURE_MODE.
	expect_eq(AP_HIT_FIELD_MAX, 7, "field max is seven AI seats");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 3, 0, 0, 0, 0, 0, 0, 1), 1, "track 3 applies");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 0, 0, 0, 0, 0, 0, 0, 1), 1, "track 0 applies");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 15, 0, 0, 0, 0, 0, 0, 1), 1, "track 15 applies");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 16, 0, 0, 0, 0, 0, 0, 1), 1, "trial 16 applies");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 17, 0, 0, 0, 0, 0, 0, 1), 1, "trial 17 applies");
	expect_eq(AP_HitOrdinaryAppliesPure(0, 3, 0, 0, 0, 0, 0, 0, 1), 0, "arcade (no adventure) refused");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 3, 0, 0, 0, 0, 0, 0, 2), 0, "multiplayer refused");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 3, 1, 0, 0, 0, 0, 0, 1), 0, "cup refused");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 3, 0, 1, 0, 0, 0, 0, 1), 0, "boss refused");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 3, 0, 0, 1, 0, 0, 0, 1), 0, "arcade mode refused");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 3, 0, 0, 0, 1, 0, 0, 1), 0, "relic refused");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 3, 0, 0, 0, 0, 1, 0, 1), 0, "token refused");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 3, 0, 0, 0, 0, 0, 1, 1), 0, "crystal refused");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 18, 0, 0, 0, 0, 0, 0, 1), 0, "arena 18 refused");
	expect_eq(AP_HitOrdinaryAppliesPure(1, 100, 0, 0, 0, 0, 0, 0, 1), 0, "cup id refused");
}

static void test_effect_applied(void)
{
	// Type 1 while already damage-active applies nothing -> no award.
	expect_eq(AP_HitEffectAppliedPure(1, 0), 1, "type 1 fresh spin applies");
	expect_eq(AP_HitEffectAppliedPure(1, 1), 0, "type 1 already active applies nothing");
	// Type 4 while already spinning still applies burn -> award.
	expect_eq(AP_HitEffectAppliedPure(4, 0), 1, "type 4 fresh applies");
	expect_eq(AP_HitEffectAppliedPure(4, 1), 1, "type 4 burn while spinning applies");
	expect_eq(AP_HitEffectAppliedPure(2, 1), 1, "type 2 always applies");
	expect_eq(AP_HitEffectAppliedPure(3, 1), 1, "type 3 always applies");
	expect_eq(AP_HitEffectAppliedPure(0, 0), 0, "type 0 applies nothing");
	expect_eq(AP_HitEffectAppliedPure(5, 0), 0, "type 5 applies nothing");
}

static void test_race_supported(void)
{
	// Adventure ordinary race only.
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 0, 0, 0, 0, 0), 1, "adventure ordinary supported");
	expect_eq(AP_HitRaceSupportedPure(0, 0, 0, 0, 0, 0, 0, 0, 0), 0, "not adventure refused");
	expect_eq(AP_HitRaceSupportedPure(1, 1, 0, 0, 0, 0, 0, 0, 0), 1, "boss race supported (correction C)");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 1, 0, 0, 0, 0, 0, 0), 1, "cup race supported (ticket 11)");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 1, 0, 0, 0, 0, 0), 0, "time trial refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 1, 0, 0, 0, 0), 0, "arcade refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 0, 1, 0, 0, 0), 0, "battle refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 0, 0, 1, 0, 0), 0, "relic refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 0, 0, 0, 1, 0), 0, "token refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 0, 0, 0, 0, 1), 0, "crystal refused");
}

// Ticket 11: the cup snapshot lifecycle classification and field size.
static void test_cup_lifecycle(void)
{
	ap_hit_cup_snapshot s;

	expect_eq(AP_HitCupFieldSizePure(4), 4, "purple cup four seats");
	expect_eq(AP_HitCupFieldSizePure(0), 7, "red cup seven seats");
	expect_eq(AP_HitCupFieldSizePure(3), 7, "yellow cup seven seats");

	AP_HitCupSnapshotResetPure(&s);
	expect_eq(AP_HitCupLegKindPure(0, 0, s.pending, s.valid, s.cupID, s.trackIndex),
	          AP_HIT_CUP_LEG_NEW, "no snapshot -> new");

	AP_HitCupSnapshotBeginPure(&s, 0);
	expect_eq(AP_HitCupLegKindPure(0, 0, s.pending, s.valid, s.cupID, s.trackIndex),
	          AP_HIT_CUP_LEG_NEW, "pad entry (pending) -> new");

	// Simulate the resolved snapshot for leg 0.
	s.valid = 1;
	s.pending = 0;
	s.trackIndex = 0;
	expect_eq(AP_HitCupLegKindPure(0, 0, s.pending, s.valid, s.cupID, s.trackIndex),
	          AP_HIT_CUP_LEG_RETRY, "same leg -> retry");
	expect_eq(AP_HitCupLegKindPure(0, 1, s.pending, s.valid, s.cupID, s.trackIndex),
	          AP_HIT_CUP_LEG_CONTINUE, "later leg -> continue");
	expect_eq(AP_HitCupLegKindPure(1, 0, s.pending, s.valid, s.cupID, s.trackIndex),
	          AP_HIT_CUP_LEG_NEW, "different cup -> new");
}

static void test_ordinary_route(void)
{
	expect_eq(AP_HitOrdinaryRoutePure(0, 0, 0), AP_HIT_ORDINARY_NORMAL, "no hit -> normal");
	expect_eq(AP_HitOrdinaryRoutePure(0, 1, 1), AP_HIT_ORDINARY_NORMAL, "no hit ignores routes");
	expect_eq(AP_HitOrdinaryRoutePure(1, 1, 0), AP_HIT_ORDINARY_MENU, "hit + token -> menu");
	expect_eq(AP_HitOrdinaryRoutePure(1, 0, 1), AP_HIT_ORDINARY_MENU, "hit + relic -> menu");
	expect_eq(AP_HitOrdinaryRoutePure(1, 1, 1), AP_HIT_ORDINARY_MENU, "hit + both -> menu");
	expect_eq(AP_HitOrdinaryRoutePure(1, 0, 0), AP_HIT_ORDINARY_PLAIN, "hit only -> plain rerace");
}

// Model MainMain's own resolution: (gameMode | AddBits) & ~RemBits.
static unsigned resolve(unsigned gm, unsigned add, unsigned rem)
{
	return (gm | add) & ~rem;
}

static void test_chooser_bits(void)
{
	const unsigned token = 0x8u;       // gameMode2 bit (stand-in for TOKEN_RACE)
	const unsigned relic = 0x4000000u; // gameMode1 bit (stand-in for RELIC_RACE)
	unsigned gm1, gm2, a0, r0, a8, r8;

	// CTR selection survives MainMain even from a state where the pending Rem
	// bit was set (the original bug: chosen route stripped back to Trophy).
	AP_HitChooserApplyPure(1, token, relic, 0, 0, 0, 0, 0, token,
	                       &gm1, &gm2, &a0, &r0, &a8, &r8);
	expect_eq(resolve(gm1, a0, r0) & relic, 0, "CTR clears relic");
	expect_eq(resolve(gm2, a8, r8) & token, token, "CTR token survives MainMain");

	// Relic selection survives and clears the token side.
	AP_HitChooserApplyPure(2, token, relic, 0, token, 0, 0, 0, 0,
	                       &gm1, &gm2, &a0, &r0, &a8, &r8);
	expect_eq(resolve(gm1, a0, r0) & relic, relic, "relic survives MainMain");
	expect_eq(resolve(gm2, a8, r8) & token, 0, "relic clears token");

	// Trophy clears both routes.
	AP_HitChooserApplyPure(0, token, relic, relic, token, relic, 0, token, 0,
	                       &gm1, &gm2, &a0, &r0, &a8, &r8);
	expect_eq(resolve(gm1, a0, r0), 0u, "trophy clears relic");
	expect_eq(resolve(gm2, a8, r8), 0u, "trophy clears token");

	// Choosing one route from a both-set state keeps only the chosen one.
	AP_HitChooserApplyPure(1, token, relic, relic, 0, relic, 0, 0, 0,
	                       &gm1, &gm2, &a0, &r0, &a8, &r8);
	expect_eq(resolve(gm1, a0, r0), 0u, "CTR clears a set relic");
	expect_eq(resolve(gm2, a8, r8), token, "CTR keeps token from both-set");
}

static void test_extras_state(void)
{
	// The loader's per-load required-extra state: reset at EVERY load entry,
	// recorded by the slice load, validated before birth.
	ap_hit_extras_state s;
	const void *models[3];
	int ids[1] = {14};

	// A slice load records Fake Crash; its model loaded -> nothing missing.
	AP_HitExtrasResetPure(&s);
	AP_HitExtrasRecordPure(&s, ids, 1);
	models[0] = (const void *)1;
	models[1] = NULL;
	models[2] = NULL;
	expect_eq(AP_HitExtrasFirstMissingPure(&s, models), -1, "recorded extra present");

	// A following hub/boss/menu load resets first: no stale id is validated
	// against this load's (NULL) extras.
	AP_HitExtrasResetPure(&s);
	models[0] = NULL;
	expect_eq(AP_HitExtrasFirstMissingPure(&s, models), -1, "reset validates nothing stale");

	// A slice load whose required model failed is detected by index.
	AP_HitExtrasResetPure(&s);
	AP_HitExtrasRecordPure(&s, ids, 1);
	expect_eq(AP_HitExtrasFirstMissingPure(&s, models), 0, "missing required model detected");

	// Recording zero extras clears a previous count.
	AP_HitExtrasResetPure(&s);
	AP_HitExtrasRecordPure(&s, ids, 1);
	AP_HitExtrasRecordPure(&s, ids, 0);
	expect_eq(AP_HitExtrasFirstMissingPure(&s, models), -1, "zero record clears count");
}

int main(void)
{
	test_draw_basics();
	test_guarantees();
	test_opportunity();
	test_pad_dest_eligible();
	test_damage_acceptance();
	test_effective_player();
	test_extras();
	test_extras_state();
	test_queue_capacity();
	test_ordinary_scope();
	test_effect_applied();
	test_race_supported();
	test_cup_lifecycle();
	test_ordinary_route();
	test_chooser_bits();

	printf("%s: %d checks, %d failures\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
