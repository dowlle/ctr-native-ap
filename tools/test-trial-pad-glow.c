// Issue #343: Slide Coliseum / Turbo Track hub pads must advertise their
// Trophy and CTR Challenge checks, not only the relic tiers and podium rungs.
//
// Pins the freestanding identity bridge (ap/ap_trial_pad_glow.h) and its
// placement in the three prize slots through the production slot selector.
#include <stdio.h>

#include "../ap/ap_glow_slots_logic.h"
#include "../ap/ap_custom_pad_logic.h"
#include "../ap/ap_trial_pad_glow.h"

static int failures;

static void expect(int condition, const char *name)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

// Checked-state stub: the codes listed here count as already checked.
static long checkedCodes[4];
static int checkedCount;

static int codeChecked(long code, void *ctx)
{
	int i;
	(void)ctx;
	for (i = 0; i < checkedCount; i++)
		if (checkedCodes[i] == code)
			return 1;
	return 0;
}

// Mirrors AP_GlowBitRewardGroup for the bits this test builds: trial
// pseudo-bits first, then rungs (race slot), relics (slot 1).
#define TEST_RELIC_BIT 38
#define TEST_RUNG_BIT (AP_PODIUM_PSEUDO_BASE + 48 * CTR_CFG_PODIUM_RUNG_COUNT)

static int groupFn(int bit)
{
	int trialGroup = AP_TrialPseudoRewardGroup(bit);
	if (trialGroup >= 0)
		return trialGroup;
	if (bit >= AP_PODIUM_PSEUDO_BASE)
		return 0;
	return 1;
}

static void testIdentities(void)
{
	int trial, challenge, ok = 1;
	int highestRung = AP_PODIUM_PSEUDO_BASE +
	                  AP_PODIUM_LOGICAL_TRACK_COUNT * CTR_CFG_PODIUM_RUNG_COUNT - 1;

	for (trial = 0; trial < CTR_CFG_TRIAL_TRACK_COUNT; trial++)
		for (challenge = 0; challenge < CTR_CFG_TRIAL_CHECK_COUNT; challenge++)
		{
			int bit = AP_TrialPseudoBit(trial, challenge);
			int t = -1, c = -1;
			if (!AP_TrialPseudoDecode(bit, &t, &c) || t != trial || c != challenge)
				ok = 0;
			if (bit <= highestRung || bit == AP_CUSTOM_TROPHY_PSEUDO_BIT ||
			    bit == AP_CUSTOM_WUMPA_PSEUDO_BIT || bit == AP_CUSTOM_CTR_PSEUDO_BIT)
				ok = 0;
		}
	expect(ok, "trial pseudo-bits round-trip and sit clear of rung and custom bits");
	expect(!AP_TrialPseudoDecode(AP_TRIAL_PSEUDO_BASE - 1, 0, 0) &&
	       !AP_TrialPseudoDecode(AP_TrialPseudoBit(1, 1) + 1, 0, 0) &&
	       !AP_TrialPseudoDecode(AP_CUSTOM_CTR_PSEUDO_BIT, 0, 0) &&
	       !AP_TrialPseudoDecode(TEST_RELIC_BIT, 0, 0),
	       "non-trial bits do not decode");
	expect(AP_TrialPseudoRewardGroup(AP_TrialPseudoBit(0, CTR_CFG_TRIAL_TROPHY)) == 0 &&
	       AP_TrialPseudoRewardGroup(AP_TrialPseudoBit(1, CTR_CFG_TRIAL_CTR)) == 2 &&
	       AP_TrialPseudoRewardGroup(TEST_RELIC_BIT) == -1,
	       "Trophy groups with the race slot, CTR Challenge with the token slot");
}

static void testAppend(void)
{
	long both[2] = {35015000, 35015001};
	long trophyOnly[2] = {35015002, -1};
	long none[2] = {0, -1};
	int out[8];
	int n;

	checkedCount = 0;
	n = AP_TrialPadAppendUnchecked(both, 1, out, 8, 0, codeChecked, 0);
	expect(n == 2 && out[0] == AP_TrialPseudoBit(1, CTR_CFG_TRIAL_TROPHY) &&
	       out[1] == AP_TrialPseudoBit(1, CTR_CFG_TRIAL_CTR),
	       "trophy_and_ctr_challenge appends Trophy and CTR Challenge");

	n = AP_TrialPadAppendUnchecked(trophyOnly, 0, out, 8, 0, codeChecked, 0);
	expect(n == 1 && out[0] == AP_TrialPseudoBit(0, CTR_CFG_TRIAL_TROPHY),
	       "trophy_race mode appends only the Trophy");

	n = AP_TrialPadAppendUnchecked(none, 0, out, 8, 0, codeChecked, 0);
	expect(n == 0, "trial races off appends nothing");

	checkedCodes[0] = 35015000;
	checkedCount = 1;
	n = AP_TrialPadAppendUnchecked(both, 1, out, 8, 0, codeChecked, 0);
	expect(n == 1 && out[0] == AP_TrialPseudoBit(1, CTR_CFG_TRIAL_CTR),
	       "a checked Trophy drops out, the CTR Challenge stays");

	checkedCodes[1] = 35015001;
	checkedCount = 2;
	n = AP_TrialPadAppendUnchecked(both, 1, out, 8, 0, codeChecked, 0);
	expect(n == 0, "both checked appends nothing");

	checkedCount = 0;
	out[0] = TEST_RELIC_BIT;
	n = AP_TrialPadAppendUnchecked(both, 1, out, 2, 1, codeChecked, 0);
	expect(n == 2 && out[0] == TEST_RELIC_BIT &&
	       out[1] == AP_TrialPseudoBit(1, CTR_CFG_TRIAL_TROPHY),
	       "append keeps earlier bits and respects capacity");
}

// The reporter's seed: by_reward_type, relics + podium rungs + both trial
// checks. Before #343 the token slot stayed empty on every phase.
static void testSlots(void)
{
	int trophy = AP_TrialPseudoBit(1, CTR_CFG_TRIAL_TROPHY);
	int ctr = AP_TrialPseudoBit(1, CTR_CFG_TRIAL_CTR);
	int bits[] = {TEST_RELIC_BIT, trophy, ctr, TEST_RUNG_BIT};
	int out[3];
	int phase, ctrSeen = 0, trophySeen = 0, ctrOnlyInToken = 1;
	int onePileCtrSeen = 0;

	for (phase = 0; phase < 12; phase++)
	{
		AP_GlowSlots_Select(bits, 4, phase, 1, groupFn, out);
		if (out[2] == ctr)
			ctrSeen = 1;
		if (out[0] == trophy)
			trophySeen = 1;
		if (out[0] == ctr || out[1] == ctr)
			ctrOnlyInToken = 0;
		if (out[1] != TEST_RELIC_BIT)
			ctrOnlyInToken = 0;

		AP_GlowSlots_Select(bits, 4, phase, 0, groupFn, out);
		if (out[0] == ctr || out[1] == ctr || out[2] == ctr)
			onePileCtrSeen = 1;
	}
	expect(ctrSeen && ctrOnlyInToken, "by_reward_type shows the CTR Challenge in the token slot");
	expect(trophySeen, "by_reward_type rotates the Trophy through the race slot with the rungs");
	expect(onePileCtrSeen, "one_pile cycles the CTR Challenge in too");
}

int main(void)
{
	testIdentities();
	testAppend();
	testSlots();
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	puts("trial pad glow: all passed");
	return 0;
}
