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

static struct GameTracker rc_gt;
static struct Driver rc_d;

// One tick on a load stage: the pause menu's RESTART and EXIT TO MAP both set
// one (MainFreeze.c), and the forced reload keeps it set for several frames.
static void rc_boundary(int stage)
{
	sdata->Loading.stage = stage;
	AP_WumpaTick(&rc_gt);
}

// A fresh attempt: load boundary, countdown observed (arms the latch), lights
// out (applies the starting ladder). kartStart is the kart's own start.
static void rc_attempt(unsigned mode, int kartStart)
{
	onTrack = 1;
	rc_boundary(LOAD_RESTART);
	rc_boundary(LOAD_RESTART);
	sdata->Loading.stage = LOAD_IDLE;
	rc_gt.gameMode1 = mode;
	rc_gt.trafficLightsTimer = 5;
	rc_d.actionsFlagSet = 0;
	rc_d.numWumpas = kartStart;
	AP_WumpaTick(&rc_gt);
	rc_gt.trafficLightsTimer = -1;
	AP_WumpaTick(&rc_gt);
}

// Pause, then leave by RESTART or EXIT TO MAP. The menu clears PAUSE_1 and sets
// the load stage; an exit's reload then leaves the race overlay.
static void rc_leave(int exitToMap)
{
	rc_gt.gameMode1 &= ~PAUSE_1;
	rc_boundary(exitToMap ? LOAD_REQUESTED : LOAD_RESTART);
	rc_boundary(exitToMap ? LOAD_REQUESTED : LOAD_RESTART);
	if (exitToMap)
	{
		onTrack = 0;
		rc_boundary(LOAD_IDLE);
		rc_boundary(LOAD_IDLE);
	}
}

// A2-42 (ruled 2026-11-04): exit to map, restart and a DeathLink loss bank the
// fruit picked up minus hits, never the starting fruit; Arcade banks nothing.
static void test_exit_and_restart_bank(void)
{
	struct GameTracker *gt = &rc_gt;
	struct Driver *d = &rc_d;
	int before, i;

	sdata->gGT = gt;
	gt->drivers[0] = d;
	ctr_wumpa_shop.state = AP_WUMPA_SHOP_VALID;
	forcedLoss = 0;
	g_wumpa_pending = 0;
	for (i = 0; i < 3; i++)
		AP_WumpaReceiveStarting();

	before = credited;
	rc_attempt(ADVENTURE_MODE, 0);
	CHECK("A2-42 starting fruit joins the race count", AP_WumpaRaceCount(d) == 3 && g_race_start == 3);
	RB_Player_ModifyWumpa(d, 8);
	gt->gameMode1 |= PAUSE_1;
	rc_leave(1);
	CHECK("A2-42 exit to map banks the fruit picked up, not the starting fruit", credited == before + 8);
	rc_boundary(LOAD_IDLE);
	rc_boundary(LOAD_REQUESTED);
	CHECK("A2-42 the reload after an exit never banks twice", credited == before + 8);

	before = credited;
	rc_attempt(ADVENTURE_MODE, 0);
	RB_Player_ModifyWumpa(d, 12);
	gt->gameMode1 |= PAUSE_1;
	rc_leave(0);
	CHECK("A2-42 restart banks the fruit picked up past ten", credited == before + 12);

	before = credited;
	rc_attempt(ADVENTURE_MODE, 0);
	RB_Player_ModifyWumpa(d, 10);
	RB_Player_ModifyWumpa(d, -3);
	rc_leave(1);
	CHECK("A2-42 hits before an exit come off the payout", credited == before + 7);

	before = credited;
	rc_attempt(ADVENTURE_MODE, 0);
	RB_Player_ModifyWumpa(d, 2);
	RB_Player_ModifyWumpa(d, -4);
	rc_leave(0);
	CHECK("A2-42 hits that eat into the starting fruit pay nothing", credited == before);

	before = credited;
	for (i = 0; i < 6; i++)
	{
		rc_attempt(ADVENTURE_MODE, 2);
		CHECK("A2-42 each restarted attempt starts from the same fruit", AP_WumpaRaceCount(d) == 5 && g_race_start == 5);
		rc_leave(0);
	}
	CHECK("A2-42 a restart loop never banks starting fruit", credited == before);

	before = credited;
	rc_attempt(ADVENTURE_MODE, 0);
	RB_Player_ModifyWumpa(d, 9);
	AP_WumpaOnRaceFinish(d, 1, d->numWumpas);
	CHECK("A2-42 a finish still banks the whole count", credited == before + 12);
	rc_leave(0);
	CHECK("A2-42 a finish then RETRY never banks twice", credited == before + 12);

	before = credited;
	rc_attempt(ADVENTURE_MODE, 0);
	RB_Player_ModifyWumpa(d, 6);
	AP_WumpaReceive(4);
	AP_WumpaTick(gt);
	CHECK("A2-42 received fruit joins the race count", AP_WumpaRaceCount(d) == 13);
	gt->gameMode1 |= PAUSE_1;
	AP_WumpaReceive(5);
	AP_WumpaTick(gt);
	CHECK("A2-42 a paused receipt waits for the race", g_wumpa_race_pending == 5 && credited == before);
	rc_leave(1);
	AP_WumpaCommitReceived();
	CHECK("A2-42 a paused exit banks picked-up and received fruit, paused receipt in full",
	      credited == before + 10 + 5 && g_wumpa_race_pending == 0);

	// Ranked race: the received race_loss finishes the player last.
	before = credited;
	rc_attempt(ADVENTURE_MODE, 0);
	RB_Player_ModifyWumpa(d, 11);
	RB_Player_ModifyWumpa(d, -3);
	forcedLoss = 1;
	d->actionsFlagSet |= ACTION_RACE_FINISHED;
	AP_WumpaTick(gt);
	CHECK("A2-42 a DeathLink loss banks picked up minus hits", credited == before + 8);
	gt->gameMode1 |= END_OF_RACE;
	AP_WumpaTick(gt);
	gt->gameMode1 &= ~END_OF_RACE;
	rc_leave(1);
	CHECK("A2-42 a DeathLink loss then the reload banks once", credited == before + 8);
	forcedLoss = 0;

	// Relic Race: the received race_loss leaves through the EXIT TO MAP route.
	before = credited;
	rc_attempt(RELIC_RACE, 0);
	RB_Player_ModifyWumpa(d, 7);
	forcedLoss = 1;
	rc_leave(1);
	CHECK("A2-42 a Relic Race DeathLink exit banks picked up fruit", credited == before + 7);
	forcedLoss = 0;

	{
		const unsigned modes[] = {ARCADE_MODE, TIME_TRIAL, BATTLE_MODE};
		for (i = 0; i < 3; i++)
		{
			before = credited;
			rc_attempt(modes[i], 0);
			RB_Player_ModifyWumpa(d, 15);
			gt->gameMode1 |= PAUSE_1;
			rc_leave(i != 0);
			CHECK("A2-42 Arcade, Time Trial and Battle exits bank nothing", credited == before);
		}
	}

	g_wumpa_starting = 0;
	onTrack = 1;
	rc_boundary(LOAD_RESTART);
	sdata->Loading.stage = LOAD_IDLE;
	gt->gameMode1 = ADVENTURE_MODE;
	d->actionsFlagSet = 0;
}

int main(void)
{
	struct GameTracker gt;
	struct Driver d;
	int b0;
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
	CHECK("A2-42 abandoning the first countdown banks its receipt in full", g_wumpa_race_pending == 0 && credited == 20);
	b0 = credited;
	sdata->Loading.stage = LOAD_IDLE;
	AP_WumpaTick(&gt);
	gt.trafficLightsTimer = -1;
	RB_Player_ModifyWumpa(&d, 15);
	CHECK("past ten stays in the race, real kart is juiced", AP_WumpaRaceCount(&d) == 15 && d.numWumpas == 10 && credited == b0);
	RB_Player_ModifyWumpa(&d, -3);
	CHECK("a hit takes from the whole total", AP_WumpaRaceCount(&d) == 12 && d.numWumpas == 10);
	RB_Player_ModifyWumpa(&d, -3);
	CHECK("juiced ends when the whole count drops below ten", AP_WumpaRaceCount(&d) == 9 && d.numWumpas == 9);
	AP_WumpaReceive(20);
	AP_WumpaTick(&gt);
	CHECK("received fruit joins this race without banking", AP_WumpaRaceCount(&d) == 29 && d.numWumpas == 10 && credited == b0 && g_wumpa_ap_credit == 0);
	RB_Player_ModifyWumpa(&d, 100);
	CHECK("three digits are represented", AP_WumpaRaceCount(&d) == 129);
	AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
	CHECK("the whole count banks once at finish", credited == b0 + 129);
	AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
	CHECK("duplicate finish pays nothing", credited == b0 + 129);
	sdata->Loading.stage = LOAD_RESTART;
	AP_WumpaTick(&gt);
	CHECK("a RETRY after the finish pays nothing more", credited == b0 + 129);
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
	CHECK("A2-42 restart banks picked-up fruit and the paused receipt", credited == b0 + 129 + 15 + 20 && g_race_wumpa == -1 && g_wumpa_race_pending == 0);
	b0 = credited;
	sdata->Loading.stage = LOAD_IDLE;
	gt.gameMode1 = ARCADE_MODE;
	d.numWumpas = 0;
	RB_Player_ModifyWumpa(&d, 15);
	CHECK("Arcade keeps the retail cap", AP_WumpaRaceCount(&d) == 10 && credited == b0);
	gt.gameMode1 = TIME_TRIAL;
	d.numWumpas = 0;
	RB_Player_ModifyWumpa(&d, 15);
	CHECK("Time Trial keeps the retail cap", AP_WumpaRaceCount(&d) == 10 && credited == b0);
	gt.gameMode1 = BATTLE_MODE;
	d.numWumpas = 0;
	RB_Player_ModifyWumpa(&d, 15);
	CHECK("Battle keeps the retail cap", AP_WumpaRaceCount(&d) == 10 && credited == b0);
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
		sdata->Loading.stage = LOAD_RESTART;
		AP_WumpaTick(&gt);
		sdata->Loading.stage = LOAD_IDLE;
		gt.gameMode1 = ADVENTURE_MODE;
		gt.trafficLightsTimer = 5;
		d.numWumpas = 4;
		AP_WumpaTick(&gt);
		gt.trafficLightsTimer = -1;
		RB_Player_ModifyWumpa(&d, 11);
		forcedLoss = 1;
		int before = credited;
		AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
		CHECK("A2-42 a forced DeathLink loss banks the fruit picked up, never the kart's start", credited == before + 11);
		forcedLoss = 0;
		before = credited;
		onTrack = 0;
		AP_WumpaTick(&gt);
		CHECK("A2-42 leaving after a forced loss pays nothing more", credited == before);
		AP_WumpaReceive(20);
		CHECK("outside-race receipts wait only for their effect marker", g_wumpa_pending == 0 && g_wumpa_ap_credit == 20 && credited == before);
		AP_WumpaCommitReceived();
		CHECK("the whole outside-race receipt banks after its marker", credited == before + 20 && g_wumpa_ap_credit == 0);
		AP_WumpaCommitReceived();
		CHECK("a repeated marker commit never credits twice", credited == before + 20);
		AP_WumpaReceive(1);
		AP_WumpaReceive(3);
		CHECK("small shop and other receipts accumulate without a kart grant", g_wumpa_pending == 0 && g_wumpa_ap_credit == 4);
		AP_WumpaCommitReceived();
		CHECK("small outside-race receipts bank in full", credited == before + 24);
		{
			int i, b2 = credited;
			for (i = 0; i < 12; i++)
				AP_WumpaReceive(1);
			AP_WumpaCommitReceived();
			CHECK("A2-17 twelve single fruit outside a race all bank (no clamp at ten)", credited == b2 + 12 && g_wumpa_pending == 0);
		}
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
		{
			int i;
			for (i = 0; i < 12; i++)
				AP_WumpaReceive(1);
			AP_WumpaTick(&gt);
			CHECK("A2-17 twelve single fruit in a race raise the race count past ten", AP_WumpaRaceCount(&d) == 26 && d.numWumpas == 10);
		}
		gt.gameMode1 |= PAUSE_1;
		AP_WumpaReceive(2);
		gt.gameMode1 &= ~PAUSE_1;
		sdata->Loading.stage = LOAD_RESTART;
		AP_WumpaTick(&gt);
		CHECK("A2-42 shop and goal off: a restart never credits a wallet", credited == before);
		sdata->Loading.stage = LOAD_IDLE;
		gt.trafficLightsTimer = 5;
		AP_WumpaTick(&gt);
		gt.trafficLightsTimer = -1;
		RB_Player_ModifyWumpa(&d, 14);
		AP_WumpaOnRaceFinish(&d, 1, d.numWumpas);
		CHECK("shop and goal off never credits a wallet", credited == before && !AP_WumpaBanking());
		onTrack = 0;
		AP_WumpaReceive(20);
		CHECK("without banking outside-race receipts still wait for the kart", g_wumpa_pending == 10 && g_wumpa_ap_credit == 0);
		AP_WumpaCommitReceived();
		CHECK("without banking a marker never creates wallet credit", credited == before);
	}
	test_exit_and_restart_bank();
	printf("%s Wumpa full race counter\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
