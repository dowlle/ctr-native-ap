#include <stdio.h>
#include <string.h>

#include <common.h>

struct sData sdata_static;

static int onTrack;
static int modifyCalls;
static int modifiedBy;
static char lastLog[160];

int LOAD_IsOpen_RacingOrBattle(void)
{
	return onTrack;
}

void RB_Player_ModifyWumpa(struct Driver *driver, int amount)
{
	int next;
	modifyCalls++;
	modifiedBy += amount;
	next = (int)driver->numWumpas + amount;
	if (next > 10)
		next = 10;
	if (next < 0)
		next = 0;
	driver->numWumpas = next;
}

void AP_LogLine(const char *line)
{
	snprintf(lastLog, sizeof lastLog, "%s", line ? line : "");
}

#include "../ap/ap_wumpa.c"

static struct GameTracker tracker;
static struct Driver driver;
static int checks;
static int failures;

static void expect(int condition, const char *name)
{
	checks++;
	if (!condition)
	{
		failures++;
		printf("FAIL %s\n", name);
	}
}

static void reset_case(void)
{
	memset(&tracker, 0, sizeof tracker);
	memset(&driver, 0, sizeof driver);
	memset(lastLog, 0, sizeof lastLog);
	tracker.drivers[0] = &driver;
	onTrack = 0;
	modifyCalls = 0;
	modifiedBy = 0;
	// The engine's not-loading value is LOAD_IDLE (-1), not a zeroed field; a
	// zeroed Loading.stage is LOAD_TEN_STAGES_0, which no idle frame ever sees.
	sdata->Loading.stage = LOAD_IDLE;
	g_wumpa_pending = 0;
	g_wumpa_starting = 0;
	g_starting_applied = 1;
	g_countdown_seen = 0;
}

static void observe_countdown_then_lights_out(void)
{
	onTrack = 1;
	tracker.gameMode1 = START_OF_RACE;
	tracker.trafficLightsTimer = 30;
	AP_WumpaTick(&tracker);
	tracker.gameMode1 = 0;
	tracker.trafficLightsTimer = 0;
	AP_WumpaTick(&tracker);
}

static void leave_track(void)
{
	onTrack = 0;
	AP_WumpaTick(&tracker);
}

// The pause-menu RESTART, frame for frame as MainFreeze.c and MainMain.c run it:
// PAUSE_1 is cleared and Loading.stage becomes LOAD_RESTART, the live scene keeps
// ticking (AP_OnFrame included) while the checkered flag closes over it, then the
// world is reinitialized in place. Overlay 1 is never unloaded, so onTrack stays
// true, and END_OF_RACE is never set. VehBirth zeroes the kart on the way in.
static void restart_from_pause(void)
{
	int frame;

	tracker.gameMode1 = PAUSE_1;
	AP_WumpaTick(&tracker);

	tracker.gameMode1 = 0;
	tracker.trafficLightsTimer = -960; // the mid-race floor MainMain clamps to
	sdata->Loading.stage = LOAD_RESTART;
	for (frame = 0; frame < 3; frame++)
		AP_WumpaTick(&tracker);

	sdata->Loading.stage = LOAD_IDLE;
	driver.numWumpas = 0;
}

static void test_bundle_quantities_and_window(void)
{
	reset_case();
	AP_WumpaReceive(3);
	expect(g_wumpa_pending == 3, "small bundle banks three fruit");
	expect(strstr(lastLog, "banked +3") != NULL, "small bundle log reports three");
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 0, "hub does not consume small bundle");

	onTrack = 1;
	tracker.gameMode1 = 0;
	tracker.trafficLightsTimer = 0;
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 0, "race-load gap does not consume bundle");

	observe_countdown_then_lights_out();
	expect(modifyCalls == 1 && modifiedBy == 3 && driver.numWumpas == 3,
	       "small bundle grants exactly three after lights-out");
	expect(g_wumpa_pending == 0, "small bundle bank drains once");
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 1, "small bundle does not repeat in the same race");

	AP_WumpaReceive(10);
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 2 && modifiedBy == 13 && driver.numWumpas == 10,
	       "big bundle grants ten live and retail path caps total at ten");
	expect(g_wumpa_pending == 0, "big bundle bank drains once");
}

static void test_bank_clamp(void)
{
	reset_case();
	AP_WumpaReceive(3);
	AP_WumpaReceive(10);
	expect(g_wumpa_pending == 10, "combined bundle bank clamps at ten");
	expect(strstr(lastLog, "banked +7, discarded +3") != NULL,
	       "overflow log reports banked and discarded quantities");
	AP_WumpaReceive(3);
	expect(g_wumpa_pending == 10, "full bank remains capped");
	expect(strstr(lastLog, "discarded +3") != NULL,
	       "full-bank receipt reports complete discard");
}

static void test_progressive_per_race(void)
{
	reset_case();
	AP_WumpaReceiveStarting();
	AP_WumpaReceiveStarting();
	expect(g_wumpa_starting == 2, "two progressive receipts rebuild count two");
	leave_track();
	observe_countdown_then_lights_out();
	expect(modifyCalls == 1 && modifiedBy == 2 && driver.numWumpas == 2,
	       "starting fruit applies after first lights-out");
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 1, "starting fruit applies once per race");

	leave_track();
	driver.numWumpas = 0;
	observe_countdown_then_lights_out();
	expect(modifyCalls == 2 && modifiedBy == 4 && driver.numWumpas == 2,
	       "starting fruit reapplies with the same count next race");
}

static void test_reconnect_replay(void)
{
	reset_case();
	AP_WumpaReceiveStarting();
	leave_track();
	observe_countdown_then_lights_out();
	expect(driver.numWumpas == 1, "pre-reconnect race received starting fruit");

	AP_WumpaConnectReset();
	AP_WumpaReceiveStarting();
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 1 && driver.numWumpas == 1,
	       "mid-race reconnect replay does not duplicate starting fruit");

	leave_track();
	driver.numWumpas = 0;
	observe_countdown_then_lights_out();
	expect(modifyCalls == 2 && modifiedBy == 2 && driver.numWumpas == 1,
	       "replayed progressive count applies on the next race");

	AP_WumpaReceive(10);
	expect(g_wumpa_pending == 10, "new post-reconnect big bundle is pending");
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 3 && modifiedBy == 12 && driver.numWumpas == 10,
	       "new post-reconnect bundle grants exactly once");
}

static void test_pause_restart_starting_fruit(void)
{
	reset_case();
	AP_WumpaReceiveStarting();
	AP_WumpaReceiveStarting();
	leave_track();
	observe_countdown_then_lights_out();
	expect(modifyCalls == 1 && driver.numWumpas == 2,
	       "race before a restart applies two starting fruit");

	restart_from_pause();
	expect(modifyCalls == 1, "restart transition frames grant nothing");
	observe_countdown_then_lights_out();
	expect(modifyCalls == 2 && modifiedBy == 4 && driver.numWumpas == 2,
	       "pause-menu restart reapplies exactly the granted count");
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 2,
	       "restarted race does not apply starting fruit twice");

	// A retry of a retry, not only the first one.
	restart_from_pause();
	observe_countdown_then_lights_out();
	expect(modifyCalls == 3 && modifiedBy == 6 && driver.numWumpas == 2,
	       "a second consecutive restart reapplies the count again");

	// A real race load after a run of restarts is unchanged.
	leave_track();
	driver.numWumpas = 0;
	observe_countdown_then_lights_out();
	expect(modifyCalls == 4 && modifiedBy == 8 && driver.numWumpas == 2,
	       "a fresh race load after restarts still applies the same count");
}

static void test_pause_restart_before_first_grant(void)
{
	reset_case();
	AP_WumpaReceiveStarting();
	leave_track();

	// Countdown observed, lights still up: the grant has not happened yet.
	onTrack = 1;
	tracker.gameMode1 = START_OF_RACE;
	tracker.trafficLightsTimer = 30;
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 0, "no starting fruit before lights-out");

	restart_from_pause();
	observe_countdown_then_lights_out();
	expect(modifyCalls == 1 && modifiedBy == 1 && driver.numWumpas == 1,
	       "a restart before the first grant applies the count exactly once");
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 1, "and does not apply it a second time");
}

static void test_pause_restart_bundle_bank(void)
{
	reset_case();
	leave_track();
	observe_countdown_then_lights_out();

	// A bundle that arrives with the pause menu open is banked, and the player
	// picks RESTART before the race can hand it over.
	AP_WumpaReceive(3);
	restart_from_pause();
	expect(modifyCalls == 0 && g_wumpa_pending == 3,
	       "a bundle banked before a restart is not spent on the doomed race");

	observe_countdown_then_lights_out();
	expect(modifyCalls == 1 && modifiedBy == 3 && driver.numWumpas == 3,
	       "the banked bundle grants exactly three in the restarted race");
	expect(g_wumpa_pending == 0, "the bundle bank drains once across a restart");
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 1, "the bundle does not repeat in the restarted race");
}

static void test_reconnect_across_restart(void)
{
	reset_case();
	AP_WumpaReceiveStarting();
	AP_WumpaReceiveStarting();
	leave_track();
	observe_countdown_then_lights_out();
	expect(driver.numWumpas == 2, "race before the reconnect received two fruit");

	// Reconnect on a restart transition frame. The replay rebuilds the ladder,
	// and the restarted race is a genuine race start, so it applies once there.
	tracker.gameMode1 = PAUSE_1;
	AP_WumpaTick(&tracker);
	tracker.gameMode1 = 0;
	tracker.trafficLightsTimer = -960;
	sdata->Loading.stage = LOAD_RESTART;
	AP_WumpaConnectReset();
	AP_WumpaReceiveStarting();
	AP_WumpaReceiveStarting();
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 1 && driver.numWumpas == 2,
	       "a reconnect during a restart transition grants nothing there");

	sdata->Loading.stage = LOAD_IDLE;
	driver.numWumpas = 0;
	observe_countdown_then_lights_out();
	expect(modifyCalls == 2 && modifiedBy == 4 && driver.numWumpas == 2,
	       "the restarted race applies the replayed count exactly once");
}

static void test_level_load_exit_does_not_spend_bank(void)
{
	reset_case();
	leave_track();
	observe_countdown_then_lights_out();

	// Leaving a race for the menu or the next cup leg is the same shape as a
	// restart on the way out: the pause is already lifted and the live scene
	// keeps ticking while the checkered flag closes over it.
	AP_WumpaReceive(3);
	tracker.gameMode1 = 0;
	tracker.trafficLightsTimer = -960;
	sdata->Loading.stage = LOAD_REQUESTED;
	AP_WumpaTick(&tracker);
	AP_WumpaTick(&tracker);
	expect(modifyCalls == 0 && g_wumpa_pending == 3,
	       "a level-load exit does not spend the bank on the race it leaves");

	onTrack = 0;
	AP_WumpaTick(&tracker);
	sdata->Loading.stage = LOAD_IDLE;
	driver.numWumpas = 0;
	observe_countdown_then_lights_out();
	expect(modifyCalls == 1 && modifiedBy == 3 && driver.numWumpas == 3,
	       "the bank survives the load and grants in the next race");
}

int main(void)
{
	test_bundle_quantities_and_window();
	test_bank_clamp();
	test_progressive_per_race();
	test_reconnect_replay();
	test_pause_restart_starting_fruit();
	test_pause_restart_before_first_grant();
	test_pause_restart_bundle_bank();
	test_reconnect_across_restart();
	test_level_load_exit_does_not_spend_bank();
	printf("%s Wumpa production lifecycle (%d checks, %d failures)\n",
	       failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
