// cc -m32 -std=c99 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DBUILD=926 -ffunction-sections -fdata-sections -Wl,--gc-sections -I ap -I . -I include -o /tmp/test-wumpa-race-counter tools/test-wumpa-race-counter.c && /tmp/test-wumpa-race-counter
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "ap/ap_hooks.h"
#include "ap/ap_wumpa_shop.h"

struct sData sdata_static;
ctr_seed_config ctr_cfg;
AP_WumpaShopConfig ctr_wumpa_shop;
static int onTrack = 1, credited, forcedLoss;
int ctr_cfg_active(void) { return 1; }
int LOAD_IsOpen_RacingOrBattle(void) { return onTrack; }
int AP_RaceAttemptIsForcedLoss(void) { return forcedLoss; }
void ap_net_wumpa_credit(int n) { credited += n; }
long long ap_net_wumpa_lifetime(void) { return credited; }
void AP_LogLine(const char *s) { (void)s; }
void RB_Player_ModifyWumpa(struct Driver *d, int n)
{
	if (AP_WumpaRaceModify(d, n)) return;
	int held = d->numWumpas + n;
	d->numWumpas = held > 10 ? 10 : held < 0 ? 0 : held;
}
#include "../ap/ap_wumpa.c"

static int failures;
#define CHECK(s, x) do { if (!(x)) { printf("FAIL %s\n", s); failures++; } } while (0)

int main(void)
{
	struct GameTracker gt;
	struct Driver d;
	memset(&gt, 0, sizeof gt);
	memset(&d, 0, sizeof d);
	sdata->gGT = &gt;
	sdata->Loading.stage = LOAD_IDLE;
	gt.drivers[0] = &d;
	gt.gameMode1 = ADVENTURE_MODE;
	ctr_wumpa_shop.state = AP_WUMPA_SHOP_VALID;
	gt.trafficLightsTimer = 5;
	AP_WumpaReceive(20); // NetTick precedes the first countdown observation.
	AP_WumpaCommitReceived();
	CHECK("a first-countdown receipt never banks early", credited == 0 && g_wumpa_race_pending == 20 && g_wumpa_ap_credit == 0);
	sdata->Loading.stage = LOAD_RESTART;
	AP_WumpaTick(&gt);
	CHECK("abandoning the first countdown forfeits its receipt", g_wumpa_race_pending == 0 && credited == 0);
	sdata->Loading.stage = LOAD_IDLE;
	AP_WumpaTick(&gt);
	gt.trafficLightsTimer = -1;
	RB_Player_ModifyWumpa(&d, 15);
	CHECK("past ten stays in the race, real kart is juiced", AP_WumpaRaceCount(&d) == 15 && d.numWumpas == 10 && credited == 0);
	RB_Player_ModifyWumpa(&d, -3);
	CHECK("a hit takes from the whole total", AP_WumpaRaceCount(&d) == 12 && d.numWumpas == 10);
	RB_Player_ModifyWumpa(&d, -3);
	CHECK("juiced ends when the whole count drops below ten", AP_WumpaRaceCount(&d) == 9 && d.numWumpas == 9);
	AP_WumpaReceive(20);
	AP_WumpaTick(&gt);
	CHECK("received fruit joins this race without banking", AP_WumpaRaceCount(&d) == 29 && d.numWumpas == 10 && credited == 0 && g_wumpa_ap_credit == 0);
	RB_Player_ModifyWumpa(&d, 100);
	CHECK("three digits are represented", AP_WumpaRaceCount(&d) == 129);
	AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
	CHECK("the whole count banks once at finish", credited == 129);
	AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
	CHECK("duplicate finish pays nothing", credited == 129);
	sdata->Loading.stage = LOAD_RESTART;
	AP_WumpaTick(&gt);
	d.numWumpas = 0;
	sdata->Loading.stage = LOAD_IDLE;
	gt.trafficLightsTimer = 5;
	AP_WumpaTick(&gt);
	gt.trafficLightsTimer = -1;
	CHECK("restart resets count", AP_WumpaRaceCount(&d) == 0);
	RB_Player_ModifyWumpa(&d, 15);
	gt.gameMode1 |= PAUSE_1;
	AP_WumpaReceive(20);
	AP_WumpaTick(&gt);
	CHECK("a paused receipt waits in this attempt", g_wumpa_race_pending == 20 && AP_WumpaRaceCount(&d) == 15);
	sdata->Loading.stage = LOAD_RESTART;
	AP_WumpaTick(&gt);
	CHECK("restart forfeits unbanked fruit and paused receipts", credited == 129 && g_race_wumpa == -1 && g_wumpa_race_pending == 0);
	sdata->Loading.stage = LOAD_IDLE;
	gt.gameMode1 = ARCADE_MODE;
	d.numWumpas = 0;
	RB_Player_ModifyWumpa(&d, 15);
	CHECK("Arcade keeps the retail cap", AP_WumpaRaceCount(&d) == 10 && credited == 129);
	gt.gameMode1 = TIME_TRIAL;
	d.numWumpas = 0;
	RB_Player_ModifyWumpa(&d, 15);
	CHECK("Time Trial keeps the retail cap", AP_WumpaRaceCount(&d) == 10 && credited == 129);
	gt.gameMode1 = BATTLE_MODE;
	d.numWumpas = 0;
	RB_Player_ModifyWumpa(&d, 15);
	CHECK("Battle keeps the retail cap", AP_WumpaRaceCount(&d) == 10 && credited == 129);
	{
		const unsigned modes[] = {ADVENTURE_MODE, ADVENTURE_MODE | ADVENTURE_CUP, ADVENTURE_MODE | ADVENTURE_BOSS, RELIC_RACE};
		unsigned i;
		for (i = 0; i < sizeof modes / sizeof modes[0]; i++)
		{
			int before = credited;
			sdata->Loading.stage = LOAD_RESTART;
			AP_WumpaTick(&gt);
			sdata->Loading.stage = LOAD_IDLE;
			gt.gameMode1 = modes[i];
			gt.trafficLightsTimer = 5;
			d.numWumpas = 4;
			AP_WumpaTick(&gt);
			gt.trafficLightsTimer = -1;
			RB_Player_ModifyWumpa(&d, 11);
			CHECK("income stays exposed until any Adventure route finishes", credited == before);
			AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
			CHECK("every finished Adventure income route banks the full count", credited == before + 15);
		}
		forcedLoss = 1;
		AP_WumpaFinishArm(&g_finish_latch);
		int before = credited;
		AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
		CHECK("a forced DeathLink loss pays nothing", credited == before);
		forcedLoss = 0;
		onTrack = 0;
		AP_WumpaReceive(20);
		CHECK("outside a race the waiting allowance remains ten", g_wumpa_pending == 10 && g_wumpa_ap_credit == 10);
		AP_WumpaCommitReceived();
		CHECK("outside-race received overflow still banks after its marker", credited == before + 10);
	}
	{
		// The Adventure counter rule is independent of a shop or Wumpa goal.
		int before = credited;
		ctr_wumpa_shop.state = AP_WUMPA_SHOP_ABSENT;
		ctr_wumpa_shop.goal = 0;
		g_wumpa_pending = 0;
		onTrack = 1;
		sdata->Loading.stage = LOAD_RESTART;
		AP_WumpaTick(&gt);
		sdata->Loading.stage = LOAD_IDLE;
		gt.gameMode1 = ADVENTURE_MODE;
		gt.trafficLightsTimer = 5;
		d.numWumpas = 0;
		AP_WumpaTick(&gt);
		gt.trafficLightsTimer = -1;
		RB_Player_ModifyWumpa(&d, 14);
		CHECK("shop-off Adventure still counts past ten", AP_WumpaRaceCount(&d) == 14 && d.numWumpas == 10);
		RB_Player_ModifyWumpa(&d, -3);
		CHECK("shop-off hits use the same full count", AP_WumpaRaceCount(&d) == 11 && d.numWumpas == 10);
		AP_WumpaReceive(3);
		AP_WumpaTick(&gt);
		CHECK("shop-off receipts join the race count", AP_WumpaRaceCount(&d) == 14 && d.numWumpas == 10 && g_wumpa_ap_credit == 0);
		AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
		CHECK("shop and goal off never credits a wallet", credited == before && !AP_WumpaBanking());
	}
	printf("%s Wumpa full race counter\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
