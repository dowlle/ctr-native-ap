#include <stdio.h>

#include "../ap/ap_glow_slots_logic.h"

static int failures;

static void expect(int condition, const char *name)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

static int groupForTestBit(int bit)
{
	return bit / 100;
}

static void testSingleOnePile(void)
{
	const int bits[] = {207};
	int out[3];
	int phase;

	for (phase = -2; phase < 1000; phase++)
	{
		AP_GlowSlots_Select(bits, 1, phase, 0, groupForTestBit, out);
		if (out[0] != 207 || out[1] != -1 || out[2] != -1)
		{
			failures++;
			printf("FAIL single one-pile phase %d\n", phase);
			return;
		}
	}
	expect(1, "single one-pile reward is phase invariant");
}

static void testSingleGrouped(void)
{
	const int bits[] = {207};
	int out[3];
	int phase;

	for (phase = -2; phase < 1000; phase++)
	{
		AP_GlowSlots_Select(bits, 1, phase, 1, groupForTestBit, out);
		if (out[0] != -1 || out[1] != -1 || out[2] != 207)
		{
			failures++;
			printf("FAIL single grouped phase %d\n", phase);
			return;
		}
	}
	expect(1, "single grouped reward is phase invariant");
}

static void testCyclesStillWork(void)
{
	const int onePile[] = {1, 2, 3, 4};
	const int grouped[] = {1, 2, 101, 201, 202};
	int out[3];

	AP_GlowSlots_Select(onePile, 4, 1, 0, groupForTestBit, out);
	expect(out[0] == 4 && out[1] == 1 && out[2] == 2,
	       "one-pile multi-reward window still cycles");

	AP_GlowSlots_Select(grouped, 5, 1, 1, groupForTestBit, out);
	expect(out[0] == 2 && out[1] == 101 && out[2] == 202,
	       "grouped multi-reward slots still cycle within type");
}

static void testDisplacedCupAdvertisesNoRetailLegRungs(void)
{
	expect(AP_GlowSlots_CupLegRungsEligible(0) == 1,
	       "ordinary cup advertises its retail leg rungs");
	expect(AP_GlowSlots_CupLegRungsEligible(1) == 0,
	       "displaced cup advertises no absent retail leg rungs");
}

int main(void)
{
	testSingleOnePile();
	testSingleGrouped();
	testCyclesStillWork();
	testDisplacedCupAdvertisesNoRetailLegRungs();
	printf("%s: 2008 phase checks plus 4 focused checks\n",
	       failures ? "FAIL" : "PASS");
	return failures != 0;
}
