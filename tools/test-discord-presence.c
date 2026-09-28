// Host harness for the pure Discord Rich Presence rules (issue #366,
// ap/ap_discord_logic.h).
//
//   cc -Wall -Wextra -I ap -o /tmp/test-discord-presence tools/test-discord-presence.c && /tmp/test-discord-presence
//
// Covers:
//   * application ID: validation, config override over the built-in constant,
//     nothing set = NULL (feature inert)
//   * view classification for every mode, loading keeps the last activity
//   * activity text for every view, check count only when connected, clamping,
//     custom and Cortex Vortex track names, no clock outside races
//   * race clock: fixed for one race, restarts on a new race, none in menus
//   * IPC frame header: little-endian encode/decode, unknown opcode and
//     oversize payload rejected
//   * scheduler: disabled means no connection attempt, retry backoff 15 s then
//     30 s, send only on change and at most once per 15 s, first activity after
//     a (re)connect goes out at once, close on disable, clock wrap

#include <stdio.h>
#include <string.h>

#include "ap_discord_logic.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(name, expr)                                  \
	do {                                                   \
		g_checks++;                                        \
		if (!(expr)) {                                     \
			printf("FAIL %s (line %d)\n", (name), __LINE__); \
			g_failures++;                                  \
		}                                                  \
	} while (0)

#define CHECK_STR(name, got, want)                                                  \
	do {                                                                            \
		g_checks++;                                                                 \
		if (strcmp((got), (want)) != 0) {                                           \
			printf("FAIL %s: got \"%s\", want \"%s\" (line %d)\n", (name), (got), (want), __LINE__); \
			g_failures++;                                                           \
		}                                                                           \
	} while (0)

#define VALID_ID "1234567890123456789"

static void TestAppId(void)
{
	CHECK("empty id invalid", !AP_Discord_ValidAppId(""));
	CHECK("NULL id invalid", !AP_Discord_ValidAppId(NULL));
	CHECK("16 digits invalid", !AP_Discord_ValidAppId("1234567890123456"));
	CHECK("17 digits valid", AP_Discord_ValidAppId("12345678901234567"));
	CHECK("20 digits valid", AP_Discord_ValidAppId("12345678901234567890"));
	CHECK("21 digits invalid", !AP_Discord_ValidAppId("123456789012345678901"));
	CHECK("letters invalid", !AP_Discord_ValidAppId("12345678901234567a"));
	CHECK("spaces invalid", !AP_Discord_ValidAppId(" 1234567890123456789"));

	CHECK("placeholder constant is inert", AP_Discord_ResolveAppId("", AP_DISCORD_APPLICATION_ID) == NULL ||
	                                        AP_Discord_ValidAppId(AP_DISCORD_APPLICATION_ID));
	CHECK("nothing set = NULL", AP_Discord_ResolveAppId("", "") == NULL);
	CHECK("override used", strcmp(AP_Discord_ResolveAppId(VALID_ID, ""), VALID_ID) == 0);
	CHECK("override beats built-in",
	      strcmp(AP_Discord_ResolveAppId(VALID_ID, "11111111111111111"), VALID_ID) == 0);
	CHECK("bad override falls back to built-in",
	      strcmp(AP_Discord_ResolveAppId("abc", "11111111111111111"), "11111111111111111") == 0);
	CHECK("bad override, no built-in = NULL", AP_Discord_ResolveAppId("abc", "") == NULL);
}

static int View(int gm1, int gm2, int level, int loading)
{
	ApDiscordGameFacts f;
	f.gameMode1 = gm1;
	f.gameMode2 = gm2;
	f.levelID = level;
	f.loading = loading;
	return AP_Discord_ClassifyView(&f);
}

#define BOSS ((int)0x80000000u)

static void TestClassify(void)
{
	const int adv = AP_DISCORD_GM1_ADVENTURE_MODE;
	CHECK("loading keeps last", View(adv, 0, 3, 1) == AP_DISCORD_VIEW_NONE);
	CHECK("LOADING flag keeps last", View(adv | AP_DISCORD_GM1_LOADING, 0, 3, 0) == AP_DISCORD_VIEW_NONE);
	CHECK("main menu flag", View(AP_DISCORD_GM1_MAIN_MENU, 0, 3, 0) == AP_DISCORD_VIEW_MAIN_MENU);
	CHECK("main menu level", View(0, 0, AP_DISCORD_LEVEL_MAIN_MENU, 0) == AP_DISCORD_VIEW_MAIN_MENU);
	CHECK("garage", View(AP_DISCORD_GM1_MAIN_MENU, 0, AP_DISCORD_LEVEL_GARAGE, 0) == AP_DISCORD_VIEW_GARAGE);
	CHECK("cutscene flag", View(adv | AP_DISCORD_GM1_GAME_CUTSCENE, 0, 26, 0) == AP_DISCORD_VIEW_CUTSCENE);
	CHECK("intro level is a cutscene", View(0, 0, 33, 0) == AP_DISCORD_VIEW_CUTSCENE);
	CHECK("hub", View(adv | AP_DISCORD_GM1_ADVENTURE_ARENA, 0, 26, 0) == AP_DISCORD_VIEW_HUB);
	CHECK("hub level without arena flag keeps last", View(adv, 0, 26, 0) == AP_DISCORD_VIEW_NONE);
	CHECK("trophy race", View(adv, 0, 3, 0) == AP_DISCORD_VIEW_TROPHY_RACE);
	CHECK("ctr challenge", View(adv, AP_DISCORD_GM2_TOKEN_RACE, 3, 0) == AP_DISCORD_VIEW_CTR_CHALLENGE);
	CHECK("relic race", View(adv | AP_DISCORD_GM1_RELIC_RACE, 0, 3, 0) == AP_DISCORD_VIEW_RELIC_RACE);
	CHECK("crystal challenge", View(adv | AP_DISCORD_GM1_CRYSTAL_CHALLENGE, 0, 21, 0) == AP_DISCORD_VIEW_CRYSTAL_CHALLENGE);
	CHECK("gem cup", View(adv | AP_DISCORD_GM1_ADVENTURE_CUP, AP_DISCORD_GM2_CUP_ANY_KIND, 3, 0) == AP_DISCORD_VIEW_GEM_CUP);
	CHECK("boss race", View(adv | BOSS, 0, 3, 0) == AP_DISCORD_VIEW_BOSS_RACE);
	CHECK("arcade", View(AP_DISCORD_GM1_ARCADE_MODE, 0, 3, 0) == AP_DISCORD_VIEW_ARCADE);
	CHECK("arcade cup", View(AP_DISCORD_GM1_ARCADE_MODE, AP_DISCORD_GM2_CUP_ANY_KIND, 3, 0) == AP_DISCORD_VIEW_ARCADE_CUP);
	CHECK("time trial", View(AP_DISCORD_GM1_TIME_TRIAL, 0, 3, 0) == AP_DISCORD_VIEW_TIME_TRIAL);
	CHECK("battle", View(AP_DISCORD_GM1_BATTLE_MODE, 0, 21, 0) == AP_DISCORD_VIEW_BATTLE);
	CHECK("versus", View(0, 0, 3, 0) == AP_DISCORD_VIEW_VERSUS);
	CHECK("negative level", View(0, 0, -1, 0) == AP_DISCORD_VIEW_CUTSCENE);
}

static ApDiscordActivity Compose(int view, int level, int cup, int connected, int checked, int total)
{
	ApDiscordInput in;
	ApDiscordActivity out;
	memset(&in, 0, sizeof in);
	in.view = view;
	in.levelID = level;
	in.cupID = cup;
	in.apConnected = connected;
	in.checked = checked;
	in.total = total;
	in.startEpoch = 1700000000;
	AP_Discord_Compose(&in, &out);
	return out;
}

static void TestCompose(void)
{
	ApDiscordActivity a;
	ApDiscordInput in;

	a = Compose(AP_DISCORD_VIEW_MAIN_MENU, 39, 0, 1, 12, 300);
	CHECK_STR("main menu", a.details, "In the main menu");
	CHECK_STR("main menu checks", a.state, "Checks: 12/300");
	CHECK("main menu has no clock", a.startEpoch == 0);

	a = Compose(AP_DISCORD_VIEW_GARAGE, 40, 0, 0, 0, 0);
	CHECK_STR("garage", a.details, "Choosing a racer");
	CHECK_STR("not connected has no state", a.state, "");

	a = Compose(AP_DISCORD_VIEW_CUTSCENE, 33, 0, 0, 0, 0);
	CHECK_STR("cutscene", a.details, "Watching a cutscene");

	a = Compose(AP_DISCORD_VIEW_HUB, 25, 0, 1, 1, 2);
	CHECK_STR("hub", a.details, "Exploring Gem Stone Valley");
	CHECK("hub has no clock", a.startEpoch == 0);
	a = Compose(AP_DISCORD_VIEW_HUB, 29, 0, 1, 1, 2);
	CHECK_STR("hub citadel", a.details, "Exploring Citadel City");

	a = Compose(AP_DISCORD_VIEW_TROPHY_RACE, 3, 0, 1, 42, 312);
	CHECK_STR("trophy", a.details, "Crash Cove, Trophy Race");
	CHECK_STR("trophy checks", a.state, "Checks: 42/312");
	CHECK("race has clock", a.startEpoch == 1700000000);

	a = Compose(AP_DISCORD_VIEW_BOSS_RACE, 13, 0, 1, 1, 2);
	CHECK_STR("boss", a.details, "Oxide Station, Boss Race");
	a = Compose(AP_DISCORD_VIEW_RELIC_RACE, 5, 0, 1, 1, 2);
	CHECK_STR("relic", a.details, "Papu's Pyramid, Relic Race");
	a = Compose(AP_DISCORD_VIEW_CTR_CHALLENGE, 11, 0, 1, 1, 2);
	CHECK_STR("ctr challenge", a.details, "N. Gin Labs, CTR Challenge");
	a = Compose(AP_DISCORD_VIEW_CRYSTAL_CHALLENGE, 21, 0, 1, 1, 2);
	CHECK_STR("crystal", a.details, "Skull Rock, Crystal Challenge");
	a = Compose(AP_DISCORD_VIEW_GEM_CUP, 0, 0, 1, 1, 2);
	CHECK_STR("red gem cup", a.details, "Dingo Canyon, Red Gem Cup");
	a = Compose(AP_DISCORD_VIEW_GEM_CUP, 17, 4, 1, 1, 2);
	CHECK_STR("purple gem cup", a.details, "Turbo Track, Purple Gem Cup");
	a = Compose(AP_DISCORD_VIEW_GEM_CUP, 17, 9, 1, 1, 2);
	CHECK_STR("unknown cup", a.details, "Turbo Track, Gem Cup");
	a = Compose(AP_DISCORD_VIEW_ARCADE, 14, 0, 0, 0, 0);
	CHECK_STR("arcade", a.details, "Coco Park, Arcade");
	a = Compose(AP_DISCORD_VIEW_ARCADE_CUP, 14, 0, 0, 0, 0);
	CHECK_STR("arcade cup", a.details, "Coco Park, Arcade Cup");
	a = Compose(AP_DISCORD_VIEW_TIME_TRIAL, 16, 0, 0, 0, 0);
	CHECK_STR("time trial", a.details, "Slide Coliseum, Time Trial");
	a = Compose(AP_DISCORD_VIEW_VERSUS, 15, 0, 0, 0, 0);
	CHECK_STR("versus", a.details, "Tiny Arena, Versus");
	a = Compose(AP_DISCORD_VIEW_BATTLE, 24, 0, 0, 0, 0);
	CHECK_STR("battle", a.details, "Lab Basement, Battle");

	a = Compose(AP_DISCORD_VIEW_TROPHY_RACE, 3, 0, 1, 400, 312);
	CHECK_STR("checked clamped to total", a.state, "Checks: 312/312");
	a = Compose(AP_DISCORD_VIEW_TROPHY_RACE, 3, 0, 1, -3, 312);
	CHECK_STR("negative checked clamped", a.state, "Checks: 0/312");
	a = Compose(AP_DISCORD_VIEW_TROPHY_RACE, 3, 0, 1, 0, 0);
	CHECK_STR("no total, no state", a.state, "");

	memset(&in, 0, sizeof in);
	in.view = AP_DISCORD_VIEW_TROPHY_RACE;
	in.levelID = 13;
	in.cortexVortex = 1;
	AP_Discord_Compose(&in, &a);
	CHECK_STR("cortex vortex", a.details, "Cortex Vortex, Trophy Race");
	in.cortexVortex = 0;
	in.customTrack = 1;
	in.view = AP_DISCORD_VIEW_GEM_CUP;
	in.cupID = 2;
	AP_Discord_Compose(&in, &a);
	CHECK_STR("custom track", a.details, "Custom track, Blue Gem Cup");

	memset(&in, 0, sizeof in);
	in.view = AP_DISCORD_VIEW_NONE;
	CHECK("none composes nothing", AP_Discord_Compose(&in, &a) == 0);
	in.view = AP_DISCORD_VIEW_VERSUS;
	in.levelID = 99;
	AP_Discord_Compose(&in, &a);
	CHECK_STR("unknown level falls back to mode", a.details, "Versus");

	{
		ApDiscordActivity b = Compose(AP_DISCORD_VIEW_TROPHY_RACE, 3, 0, 1, 42, 312);
		ApDiscordActivity c = b;
		CHECK("equal", AP_Discord_Activity_Equal(&b, &c));
		c.startEpoch++;
		CHECK("clock differs", !AP_Discord_Activity_Equal(&b, &c));
		c = b;
		snprintf(c.state, sizeof c.state, "Checks: 43/312");
		CHECK("state differs", !AP_Discord_Activity_Equal(&b, &c));
	}

	// No privacy leak: nothing but the fixed phrases, track names and numbers.
	a = Compose(AP_DISCORD_VIEW_HUB, 26, 0, 1, 7, 9);
	CHECK("details length within Discord limit", strlen(a.details) >= 2 && strlen(a.details) <= AP_DISCORD_TEXT_MAX);
}

static void TestRaceClock(void)
{
	ApDiscordRaceClock c;
	memset(&c, 0, sizeof c);
	CHECK("menu has no clock", AP_Discord_RaceClockUpdate(&c, AP_DISCORD_VIEW_MAIN_MENU, 39, 100) == 0);
	CHECK("race starts", AP_Discord_RaceClockUpdate(&c, AP_DISCORD_VIEW_TROPHY_RACE, 3, 200) == 200);
	CHECK("same race keeps start", AP_Discord_RaceClockUpdate(&c, AP_DISCORD_VIEW_TROPHY_RACE, 3, 260) == 200);
	CHECK("new track restarts", AP_Discord_RaceClockUpdate(&c, AP_DISCORD_VIEW_TROPHY_RACE, 4, 300) == 300);
	CHECK("new mode restarts", AP_Discord_RaceClockUpdate(&c, AP_DISCORD_VIEW_RELIC_RACE, 4, 400) == 400);
	CHECK("hub clears", AP_Discord_RaceClockUpdate(&c, AP_DISCORD_VIEW_HUB, 26, 500) == 0);
	CHECK("same race again restarts after hub", AP_Discord_RaceClockUpdate(&c, AP_DISCORD_VIEW_RELIC_RACE, 4, 600) == 600);
}

static void TestFrames(void)
{
	unsigned char h[AP_DISCORD_FRAME_HEADER];
	uint32_t op = 99, len = 99;

	AP_Discord_PutHeader(h, AP_DISCORD_OP_FRAME, 0x01020304u);
	CHECK("op little-endian", h[0] == 1 && h[1] == 0 && h[2] == 0 && h[3] == 0);
	CHECK("len little-endian", h[4] == 4 && h[5] == 3 && h[6] == 2 && h[7] == 1);

	AP_Discord_PutHeader(h, AP_DISCORD_OP_HANDSHAKE, 42);
	CHECK("round trip accepted", AP_Discord_GetHeader(h, &op, &len) == 1);
	CHECK("round trip values", op == AP_DISCORD_OP_HANDSHAKE && len == 42);

	AP_Discord_PutHeader(h, AP_DISCORD_OP_PONG, AP_DISCORD_FRAME_MAX_PAYLOAD);
	CHECK("max payload accepted", AP_Discord_GetHeader(h, &op, &len) == 1);
	AP_Discord_PutHeader(h, AP_DISCORD_OP_FRAME, AP_DISCORD_FRAME_MAX_PAYLOAD + 1);
	CHECK("oversize rejected", AP_Discord_GetHeader(h, &op, &len) == 0);
	AP_Discord_PutHeader(h, 5, 10);
	CHECK("unknown opcode rejected", AP_Discord_GetHeader(h, &op, &len) == 0);
	{
		const unsigned char raw[8] = {2, 0, 0, 0, 0x10, 0, 0, 0};
		CHECK("decode close", AP_Discord_GetHeader(raw, &op, &len) == 1 && op == AP_DISCORD_OP_CLOSE && len == 16);
	}
}

static void TestScheduler(void)
{
	ApDiscordSched s;
	uint32_t wait = 0;
	uint32_t t = 1000;

	AP_DiscordSched_Init(&s, 15000, 30000, 15000, 1000);

	// Disabled (or no application ID): never a connection attempt.
	for (int i = 0; i < 100; i++)
		CHECK("disabled never connects", AP_DiscordSched_Next(&s, t + (uint32_t)i * 60000u, 0, 5, &wait) == AP_DISCORD_DO_WAIT);
	CHECK("disabled sleeps until woken", wait == AP_DISCORD_WAIT_FOREVER);

	// Enabled: connect at once.
	CHECK("enabled connects at once", AP_DiscordSched_Next(&s, t, 1, 0, &wait) == AP_DISCORD_DO_CONNECT);
	AP_DiscordSched_OnConnect(&s, t, 0);
	CHECK("first retry waits", AP_DiscordSched_Next(&s, t + 1, 1, 0, &wait) == AP_DISCORD_DO_WAIT);
	CHECK("first retry after 15 s", wait == 14999);
	CHECK("not before 15 s", AP_DiscordSched_Next(&s, t + 14999, 1, 0, &wait) == AP_DISCORD_DO_WAIT);
	CHECK("retry at 15 s", AP_DiscordSched_Next(&s, t + 15000, 1, 0, &wait) == AP_DISCORD_DO_CONNECT);
	t += 15000;
	AP_DiscordSched_OnConnect(&s, t, 0);
	AP_DiscordSched_Next(&s, t, 1, 0, &wait);
	CHECK("later retries every 30 s", wait == 30000);
	t += 30000;
	CHECK("retry at 30 s", AP_DiscordSched_Next(&s, t, 1, 0, &wait) == AP_DISCORD_DO_CONNECT);
	AP_DiscordSched_OnConnect(&s, t, 0);
	AP_DiscordSched_Next(&s, t, 1, 0, &wait);
	CHECK("backoff capped at 30 s", wait == 30000);
	t += 30000;
	CHECK("connect", AP_DiscordSched_Next(&s, t, 1, 0, &wait) == AP_DISCORD_DO_CONNECT);
	AP_DiscordSched_OnConnect(&s, t, 1);

	// Connected, nothing published yet: nothing to send.
	CHECK("nothing published, nothing sent", AP_DiscordSched_Next(&s, t, 1, 0, &wait) == AP_DISCORD_DO_WAIT);
	CHECK("connected polls", wait == 1000);
	// First activity goes out at once.
	CHECK("first activity at once", AP_DiscordSched_Next(&s, t, 1, 1, &wait) == AP_DISCORD_DO_SEND);
	AP_DiscordSched_OnSent(&s, t, 1);
	CHECK("unchanged, no send", AP_DiscordSched_Next(&s, t + 20000, 1, 1, &wait) == AP_DISCORD_DO_WAIT);
	// A change 1 s later waits for the 15 s window.
	CHECK("change inside window waits", AP_DiscordSched_Next(&s, t + 1000, 1, 2, &wait) == AP_DISCORD_DO_WAIT);
	CHECK("wait no longer than poll", wait == 1000);
	CHECK("change near window end", AP_DiscordSched_Next(&s, t + 14500, 1, 3, &wait) == AP_DISCORD_DO_WAIT && wait == 500);
	CHECK("change sent at 15 s", AP_DiscordSched_Next(&s, t + 15000, 1, 3, &wait) == AP_DISCORD_DO_SEND);
	AP_DiscordSched_OnSent(&s, t + 15000, 3);
	t += 15000;

	// Discord went away: retry after 15 s, and resend the activity at once.
	AP_DiscordSched_OnDisconnect(&s, t);
	CHECK("after drop waits", AP_DiscordSched_Next(&s, t + 100, 1, 3, &wait) == AP_DISCORD_DO_WAIT && wait == 14900);
	CHECK("after drop reconnects at 15 s", AP_DiscordSched_Next(&s, t + 15000, 1, 3, &wait) == AP_DISCORD_DO_CONNECT);
	t += 15000;
	AP_DiscordSched_OnConnect(&s, t, 1);
	CHECK("resend after reconnect", AP_DiscordSched_Next(&s, t, 1, 3, &wait) == AP_DISCORD_DO_SEND);
	AP_DiscordSched_OnSent(&s, t, 3);

	// Disabled while connected: close; enabling again connects at once.
	CHECK("disable closes", AP_DiscordSched_Next(&s, t + 10, 0, 3, &wait) == AP_DISCORD_DO_CLOSE);
	AP_DiscordSched_OnClosed(&s);
	CHECK("closed stays quiet", AP_DiscordSched_Next(&s, t + 20, 0, 3, &wait) == AP_DISCORD_DO_WAIT && wait == AP_DISCORD_WAIT_FOREVER);
	CHECK("re-enable connects at once", AP_DiscordSched_Next(&s, t + 30, 1, 3, &wait) == AP_DISCORD_DO_CONNECT);

	// A failed attempt, then the option off and on: no leftover backoff.
	AP_DiscordSched_OnConnect(&s, t + 30, 0);
	AP_DiscordSched_Next(&s, t + 40, 0, 3, &wait);
	CHECK("toggle clears backoff", AP_DiscordSched_Next(&s, t + 50, 1, 3, &wait) == AP_DISCORD_DO_CONNECT);

	// Millisecond clock wrap.
	AP_DiscordSched_Init(&s, 15000, 30000, 15000, 1000);
	t = 0xFFFFF000u;
	AP_DiscordSched_Next(&s, t, 1, 0, &wait);
	AP_DiscordSched_OnConnect(&s, t, 0);
	CHECK("wrap: still waiting", AP_DiscordSched_Next(&s, t + 10000, 1, 0, &wait) == AP_DISCORD_DO_WAIT && wait == 5000);
	CHECK("wrap: retry due", AP_DiscordSched_Next(&s, t + 15000, 1, 0, &wait) == AP_DISCORD_DO_CONNECT);
}

int main(void)
{
	TestAppId();
	TestClassify();
	TestCompose();
	TestRaceClock();
	TestFrames();
	TestScheduler();
	if (g_failures == 0)
		printf("discord presence: all %d checks passed\n", g_checks);
	return g_failures == 0 ? 0 : 1;
}
