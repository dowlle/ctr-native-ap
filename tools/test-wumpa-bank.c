// gcc -m32 -std=c99 -Wall -Wextra -DCTR_AP -I ap -o /tmp/test-wumpa-bank tools/test-wumpa-bank.c && /tmp/test-wumpa-bank
//
// #249 banking decisions (ap/ap_wumpa_shop.h): fruit past 10 banks, the held
// fruit banks once per finished attempt and never for a forced DeathLink loss,
// quit and restart bank nothing held, the goal arm counts the lifetime total,
// the shelf states, and the hub ids and codes the apworld mints. The engine
// wiring is checked by tools/test-wumpa-shop-production.py.
#include <stdio.h>
#include <string.h>
#include "ap_wumpa_shop.h"
#include "ap_oxide_encounter.h"
#include "ap_goal_line.h"

static int failures;
#define CHECK(label, expr) do { int ok_ = (expr); printf("%s  %s\n", ok_ ? "ok  " : "FAIL", label); failures += !ok_; } while (0)

int main(void)
{
	AP_WumpaFinishLatch l = {0};
	AP_WumpaShopSlot s = {35028600LL, 20, 50};
	AP_WumpaShopConfig c = {0};

	CHECK("overflow: 8 + 5 banks 3", AP_WumpaBankOverflow(8, 5) == 3);
	CHECK("overflow: 10 + 1 banks 1", AP_WumpaBankOverflow(10, 1) == 1);
	CHECK("overflow: 4 + 5 banks 0", AP_WumpaBankOverflow(4, 5) == 0);
	CHECK("overflow: a loss banks 0", AP_WumpaBankOverflow(10, -10) == 0);
	CHECK("overflow: AP bundle of 10 at 7 banks 7", AP_WumpaBankOverflow(7, 10) == 7);

	CHECK("finish: no attempt armed pays nothing", AP_WumpaFinishTake(&l, 7, 0) == 0);
	AP_WumpaFinishArm(&l);
	CHECK("finish: armed attempt pays the held count", AP_WumpaFinishTake(&l, 7, 0) == 7);
	CHECK("finish: a second finish edge in the same attempt pays nothing", AP_WumpaFinishTake(&l, 7, 0) == 0);
	AP_WumpaFinishArm(&l);
	CHECK("finish: a forced DeathLink loss pays nothing", AP_WumpaFinishTake(&l, 9, 1) == 0);
	CHECK("finish: and disarms the attempt", AP_WumpaFinishTake(&l, 9, 0) == 0);
	AP_WumpaFinishArm(&l);
	AP_WumpaFinishArm(&l); // a pause-menu restart: new countdown, no finish in between
	CHECK("finish: a restart then a finish pays once", AP_WumpaFinishTake(&l, 10, 0) == 10 &&
	                                                    AP_WumpaFinishTake(&l, 10, 0) == 0);
	AP_WumpaFinishArm(&l);
	CHECK("finish: zero held pays nothing", AP_WumpaFinishTake(&l, 0, 0) == 0);

	CHECK("goal: off is met", AP_WumpaGoalMet(0, 0));
	CHECK("goal: short", !AP_WumpaGoalMet(1000, 999));
	CHECK("goal: reached", AP_WumpaGoalMet(1000, 1000));

	CHECK("slot: sold beats everything", AP_WumpaSlotState(&s, 0, 0, 1, 0, 0) == AP_WUMPA_SLOT_SOLD);
	CHECK("slot: a Collected slot is sold without a purchase", AP_WumpaSlotState(&s, 0, 0, 0, 1, 0) == AP_WUMPA_SLOT_SOLD);
	CHECK("slot: pending", AP_WumpaSlotState(&s, 100, 100, 0, 0, 1) == AP_WUMPA_SLOT_PENDING);
	CHECK("slot: locked below its unlock", AP_WumpaSlotState(&s, 49, 49, 0, 0, 0) == AP_WUMPA_SLOT_LOCKED);
	CHECK("slot: unlock counts lifetime, not balance", AP_WumpaSlotState(&s, 60, 25, 0, 0, 0) == AP_WUMPA_SLOT_FOR_SALE);
	CHECK("slot: too poor", AP_WumpaSlotState(&s, 60, 19, 0, 0, 0) == AP_WUMPA_SLOT_TOO_POOR);

	CHECK("hubs: level ids in apworld order", AP_WumpaShopHubLevel(0) == 26 && AP_WumpaShopHubLevel(1) == 25 &&
	                                           AP_WumpaShopHubLevel(2) == 27 && AP_WumpaShopHubLevel(3) == 28 &&
	                                           AP_WumpaShopHubLevel(4) == 29 && AP_WumpaShopHubLevel(5) == -1);
	CHECK("hubs: reverse map", AP_WumpaShopHubOfLevel(25) == 1 && AP_WumpaShopHubOfLevel(13) == -1);
	CHECK("codes: 35028600 + 40 * hub + (n - 1)", AP_WumpaShopCode(0, 1) == 35028600LL &&
	                                              AP_WumpaShopCode(4, 25) == 35028784LL &&
	                                              AP_WumpaShopCode(2, 10) == 35028689LL);

	CHECK("banking: off without shop or goal", !AP_WumpaBankingActive(&c));
	c.goal = 300;
	CHECK("banking: a goal alone banks", AP_WumpaBankingActive(&c) && !AP_WumpaShopUsable(&c));
	c.goal = 0; c.state = AP_WUMPA_SHOP_REFUSED;
	CHECK("banking: a refused block alone does not bank", !AP_WumpaBankingActive(&c));
	c.state = AP_WUMPA_SHOP_VALID; c.slots = 4;
	CHECK("banking: a valid shop banks", AP_WumpaBankingActive(&c) && AP_WumpaShopUsable(&c));

	{
		char line[64];
		int w = AP_GoalLineFormat(line, (int)sizeof line, AP_OXIDE_GOAL_ANY, 2, 1, 0, 0);
		CHECK("goal line: Wumpa segment appended",
		      AP_GoalLineAppendWumpa(line, (int)sizeof line, w, 412, 1000) &&
		      strcmp(line, "GOAL: OXIDE - BOSS 1/2 - WUMPA 412/1000") == 0);
		w = AP_GoalLineFormat(line, (int)sizeof line, AP_OXIDE_GOAL_OPTIONAL, 0, 0, 0, 0);
		CHECK("goal line: Wumpa alone", AP_GoalLineAppendWumpa(line, (int)sizeof line, w, 1500, 1000) &&
		                                strcmp(line, "GOAL: WUMPA 1000/1000") == 0);
		w = AP_GoalLineFormat(line, (int)sizeof line, AP_OXIDE_GOAL_ANY, 0, 0, 0, 0);
		CHECK("goal line: no Wumpa arm leaves the line", AP_GoalLineAppendWumpa(line, (int)sizeof line, w, 5, 0) == w &&
		                                                 strcmp(line, "GOAL: OXIDE") == 0);
	}
	{
		// The Wumpa arm gates the selected Oxide finale like the Boss and Gem
		// arms, so beating it still completes the goal at once.
		AP_OxideGarageInputs in;
		AP_OxideGarageState st;
		memset(&in, 0, sizeof in);
		in.garageReqMet = 1;
		in.goalOxide = AP_OXIDE_GOAL_ANY;
		in.wumpaShort = 1;
		st = AP_OxideGarageEvaluate(&in);
		CHECK("oxide: any_percent first challenge shut while Wumpa is short", !st.open);
		in.wumpaShort = 0;
		st = AP_OxideGarageEvaluate(&in);
		CHECK("oxide: opens once the Wumpa goal is met", st.open && st.encounter == AP_OXIDE_ENCOUNTER_FIRST);
		in.goalOxide = AP_OXIDE_GOAL_OPTIONAL;
		in.wumpaShort = 1;
		st = AP_OxideGarageEvaluate(&in);
		CHECK("oxide: optional Oxide is never gated by the Wumpa arm", st.open);
	}

	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures != 0;
}
