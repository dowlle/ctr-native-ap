// cc -std=c99 -Wall -Wextra -DCTR_AP -I ap -I . -I include -o /tmp/test-hit-policy tools/test-hit-policy.c -lm
//
// Freestanding coverage for the Hit Character encounter decisions
// (ap/ap_hit_policy.h, ticket 06). Every ordering / exclusion / attribution rule
// the engine gather relies on is pinned here without the engine:
//
//   1. candidate ordering: one guest slot, pinned before reserve, then base fill
//   2. player exclusion (the effective player after a racer lock)
//   3. field sizes 7 (ordinary) and 4 (retail Purple cup)
//   4. the eligible unchecked opportunity, matching the field selection exactly
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

// The fixture's Crash Cove candidate lists (tracks["3"]).
static const int kBase3[8] = {7, 0, 1, 2, 3, 4, 5, 6};
static const int kRes3[8] = {15, 8, 9, 10, 11, 12, 13, 14};

static void make_cand(ctr_hit_candidates *c, int pin)
{
	int i;
	memset(c, 0, sizeof *c);
	c->base.count = 8;
	for (i = 0; i < 8; i++)
		c->base.ids[i] = kBase3[i];
	c->reserve.count = 8;
	for (i = 0; i < 8; i++)
		c->reserve.ids[i] = kRes3[i];
	if (pin >= 0)
	{
		c->pinned.count = 1;
		c->pinned.ids[0] = pin;
	}
}

static void zero_elig(unsigned char *e)
{
	memset(e, 0, CTR_CFG_HIT_CHARACTER_COUNT);
	e[0] = e[1] = e[2] = e[3] = e[4] = e[5] = e[6] = e[7] = 1; // defaults
}

static void test_select_ordering(void)
{
	ctr_hit_candidates cand;
	unsigned char elig[CTR_CFG_HIT_CHARACTER_COUNT];
	ap_hit_field f;
	int i;

	make_cand(&cand, 14);
	zero_elig(elig);

	// No eligible guest -> base fill, player excluded, order preserved.
	AP_HitSelectFieldPure(&cand, elig, 0, 7, &f);
	expect_eq(f.count, 7, "no guest count");
	expect_eq(f.guest, -1, "no guest selected");
	{
		static const int want[7] = {7, 1, 2, 3, 4, 5, 6};
		for (i = 0; i < 7; i++)
			expect_eq(f.ids[i], want[i], "no guest base order");
	}

	// Eligible pinned guest is seated first, then base fills the rest.
	elig[14] = 1;
	AP_HitSelectFieldPure(&cand, elig, 0, 7, &f);
	expect_eq(f.count, 7, "pinned guest count");
	expect_eq(f.guest, 14, "pinned guest selected");
	{
		static const int want[7] = {14, 7, 1, 2, 3, 4, 5};
		for (i = 0; i < 7; i++)
			expect_eq(f.ids[i], want[i], "pinned guest field");
	}

	// Reserve guest when the pin is not eligible.
	zero_elig(elig);
	elig[15] = 1;
	AP_HitSelectFieldPure(&cand, elig, 0, 7, &f);
	expect_eq(f.guest, 15, "reserve guest selected");
	{
		static const int want[7] = {15, 7, 1, 2, 3, 4, 5};
		for (i = 0; i < 7; i++)
			expect_eq(f.ids[i], want[i], "reserve guest field");
	}

	// Pin priority: both eligible -> the pinned guest wins.
	elig[14] = 1;
	elig[15] = 1;
	AP_HitSelectFieldPure(&cand, elig, 0, 7, &f);
	expect_eq(f.guest, 14, "pinned priority over reserve");

	// Player exclusion: a player who IS the pinned guest cannot be seated.
	zero_elig(elig);
	elig[14] = 1;
	AP_HitSelectFieldPure(&cand, elig, 14, 7, &f);
	expect_eq(f.guest, -1, "player never seats self as guest");
	for (i = 0; i < f.count; i++)
		expect(f.ids[i] != 14, "player never appears in the field");

	// Field size 4 (retail Purple cup).
	zero_elig(elig);
	AP_HitSelectFieldPure(&cand, elig, 0, 4, &f);
	expect_eq(f.count, 4, "purple cup count");
	{
		static const int want[4] = {7, 1, 2, 3};
		for (i = 0; i < 4; i++)
			expect_eq(f.ids[i], want[i], "purple cup base order");
	}
	elig[14] = 1;
	AP_HitSelectFieldPure(&cand, elig, 0, 4, &f);
	expect_eq(f.count, 4, "purple cup with guest count");
	expect_eq(f.guest, 14, "purple cup with guest selected");
	{
		static const int want[4] = {14, 7, 1, 2};
		for (i = 0; i < 4; i++)
			expect_eq(f.ids[i], want[i], "purple cup with guest field");
	}

	// NULL candidates -> empty field.
	AP_HitSelectFieldPure(NULL, elig, 0, 7, &f);
	expect_eq(f.count, 0, "null candidates -> empty");

	// Defaults are always eligible; guests need their trigger.
	{
		unsigned char none[CTR_CFG_HIT_CHARACTER_COUNT];
		memset(none, 0, sizeof none);
		for (i = 0; i < 8; i++)
			expect_eq(AP_HitGuestEligiblePure(i, none), 1, "default always eligible");
		for (i = 8; i < 16; i++)
			expect_eq(AP_HitGuestEligiblePure(i, none), 0, "guest needs trigger");
		none[14] = 1;
		expect_eq(AP_HitGuestEligiblePure(14, none), 1, "guest trigger met");
	}
}

static void test_opportunity(void)
{
	ctr_hit_candidates cand;
	unsigned char elig[CTR_CFG_HIT_CHARACTER_COUNT];
	unsigned char unc[CTR_CFG_HIT_CHARACTER_COUNT];

	make_cand(&cand, 14);
	zero_elig(elig);
	memset(unc, 0, sizeof unc);
	elig[14] = 1;

	expect_eq(AP_HitOpportunityPure(&cand, elig, unc, 0), -1,
	          "eligible but checked -> no opportunity");
	unc[14] = 1;
	expect_eq(AP_HitOpportunityPure(&cand, elig, unc, 0), 14,
	          "eligible + unchecked -> opportunity");

	// Player exclusion applies to the opportunity too.
	expect_eq(AP_HitOpportunityPure(&cand, elig, unc, 14), -1,
	          "player is the pinned guest -> no opportunity");

	// The opportunity must match the SEATED guest: pinned eligible but checked,
	// reserve eligible + unchecked -> the pin is still seated, so no opportunity.
	{
		unsigned char e2[CTR_CFG_HIT_CHARACTER_COUNT];
		unsigned char u2[CTR_CFG_HIT_CHARACTER_COUNT];
		zero_elig(e2);
		memset(u2, 0, sizeof u2);
		e2[14] = 1;
		e2[15] = 1;
		u2[15] = 1;
		expect_eq(AP_HitOpportunityPure(&cand, e2, u2, 0), -1,
		          "checked pinned guest shadows an unchecked reserve");
	}

	// No pin at all (a cup-shaped candidate): reserve guest is the opportunity.
	{
		ctr_hit_candidates cup;
		make_cand(&cup, -1);
		memset(unc, 0, sizeof unc);
		unc[15] = 1;
		expect_eq(AP_HitOpportunityPure(&cup, elig, unc, 0), -1,
		          "no pin and no eligible reserve -> none");
		elig[15] = 1;
		expect_eq(AP_HitOpportunityPure(&cup, elig, unc, 0), 15,
		          "reserve opportunity without a pin");
	}
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
	expect_eq(AP_HitRaceSupportedPure(1, 1, 0, 0, 0, 0, 0, 0, 0), 0, "boss refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 1, 0, 0, 0, 0, 0, 0), 0, "cup refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 1, 0, 0, 0, 0, 0), 0, "time trial refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 1, 0, 0, 0, 0), 0, "arcade refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 0, 1, 0, 0, 0), 0, "battle refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 0, 0, 1, 0, 0), 0, "relic refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 0, 0, 0, 1, 0), 0, "token refused");
	expect_eq(AP_HitRaceSupportedPure(1, 0, 0, 0, 0, 0, 0, 0, 1), 0, "crystal refused");
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
	test_select_ordering();
	test_opportunity();
	test_damage_acceptance();
	test_effective_player();
	test_extras();
	test_extras_state();
	test_queue_capacity();
	test_ordinary_scope();
	test_effect_applied();
	test_race_supported();
	test_ordinary_route();
	test_chooser_bits();

	printf("%s: %d checks, %d failures\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
