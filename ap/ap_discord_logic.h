#ifndef AP_DISCORD_LOGIC_H
#define AP_DISCORD_LOGIC_H

// Discord Rich Presence (issue #366): the pure parts, freestanding so the host
// harness (tools/test-discord-presence.c) can check them without the engine.
//
//   ap/ap_discord_logic.h  this file: application ID, what the game is doing
//                          (view), the text shown, the race clock, the IPC
//                          frame header and the connect/send scheduler.
//   ap/ap_discord_ipc.cpp  the Discord IPC client: one worker thread, the
//                          named pipe (Windows) or Unix socket, JSON via the
//                          vendored nlohmann/json. Part of the ap_net library.
//   ap/ap_discord.c        the game-side glue (unity build): reads the game
//                          state about once a second and hands a copy of the
//                          activity to the worker. Never does I/O itself.
//
// The feature is local and off by default ("Discord Status", Options,
// Archipelago page). It talks only to the Discord client on the same machine.
// Nothing about the room, slot name or seed is ever sent: the activity holds a
// track and mode, the check count, a race clock, the logo and a repo link.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ── Discord application ────────────────────────────────────────────────────
// The one place the Discord application ID lives. Empty = no application yet:
// the feature then does nothing, even with the option on. A config.ini
// override ([Discord] application_id) wins when it holds a valid ID, so a
// build can be tested before this constant is filled in.
//
// Setting it up (Discord Developer Portal, discord.com/developers/applications):
// create an application named "CTR Archipelago" (Discord shows this name as
// "Playing CTR Archipelago"), copy its Application ID from General
// Information into the constant below, and under Rich Presence > Art Assets
// upload the logo with the key AP_DISCORD_LARGE_IMAGE_KEY.
#define AP_DISCORD_APPLICATION_ID ""

// Art asset key of the large image, as uploaded under the application's Rich
// Presence art assets in the Discord Developer Portal.
#define AP_DISCORD_LARGE_IMAGE_KEY  "ctr_ap_logo"
#define AP_DISCORD_LARGE_IMAGE_TEXT "CTR Archipelago"
// Button shown under the activity (label at most 32 characters).
#define AP_DISCORD_BUTTON_LABEL "CTR Archipelago on GitHub"
#define AP_DISCORD_BUTTON_URL   "https://github.com/dowlle/ctr-native-ap"

// Discord accepts details and state of 2 to 128 characters.
#define AP_DISCORD_TEXT_MAX 128

// A Discord application ID is a snowflake: decimal digits, 17 to 20 of them.
static inline int AP_Discord_ValidAppId(const char *s)
{
	size_t n;
	if (s == NULL)
		return 0;
	n = strlen(s);
	if (n < 17 || n > 20)
		return 0;
	for (size_t i = 0; i < n; i++)
		if (s[i] < '0' || s[i] > '9')
			return 0;
	return 1;
}

// The ID to use: a valid config.ini override, else a valid built-in constant,
// else NULL (feature inert).
static inline const char *AP_Discord_ResolveAppId(const char *configOverride, const char *builtIn)
{
	if (AP_Discord_ValidAppId(configOverride))
		return configOverride;
	if (AP_Discord_ValidAppId(builtIn))
		return builtIn;
	return NULL;
}

// ── What the game is doing ─────────────────────────────────────────────────
typedef enum
{
	AP_DISCORD_VIEW_NONE = 0,       // loading or unknown: keep the last activity
	AP_DISCORD_VIEW_MAIN_MENU,
	AP_DISCORD_VIEW_GARAGE,         // adventure character select
	AP_DISCORD_VIEW_CUTSCENE,
	AP_DISCORD_VIEW_HUB,            // adventure hub
	AP_DISCORD_VIEW_TROPHY_RACE,
	AP_DISCORD_VIEW_BOSS_RACE,
	AP_DISCORD_VIEW_RELIC_RACE,
	AP_DISCORD_VIEW_CTR_CHALLENGE,
	AP_DISCORD_VIEW_CRYSTAL_CHALLENGE,
	AP_DISCORD_VIEW_GEM_CUP,
	AP_DISCORD_VIEW_ARCADE,
	AP_DISCORD_VIEW_ARCADE_CUP,
	AP_DISCORD_VIEW_TIME_TRIAL,
	AP_DISCORD_VIEW_VERSUS,
	AP_DISCORD_VIEW_BATTLE,
	AP_DISCORD_VIEW__COUNT
} ApDiscordView;

// Mirrors of the engine values this file needs, so it stays freestanding.
// ap/ap_discord.c asserts each one against the engine's own definition.
#define AP_DISCORD_GM1_BATTLE_MODE       0x20
#define AP_DISCORD_GM1_MAIN_MENU         0x2000
#define AP_DISCORD_GM1_TIME_TRIAL        0x20000
#define AP_DISCORD_GM1_ADVENTURE_MODE    0x80000
#define AP_DISCORD_GM1_ADVENTURE_ARENA   0x100000
#define AP_DISCORD_GM1_ARCADE_MODE       0x400000
#define AP_DISCORD_GM1_RELIC_RACE        0x4000000
#define AP_DISCORD_GM1_CRYSTAL_CHALLENGE 0x8000000
#define AP_DISCORD_GM1_ADVENTURE_CUP     0x10000000
#define AP_DISCORD_GM1_GAME_CUTSCENE     0x20000000
#define AP_DISCORD_GM1_LOADING           0x40000000
#define AP_DISCORD_GM2_TOKEN_RACE        0x8
#define AP_DISCORD_GM2_CUP_ANY_KIND      0x10
#define AP_DISCORD_LEVEL_LAST_TRACK      17 // TURBO_TRACK
#define AP_DISCORD_LEVEL_LAST_ARENA      24 // LAB_BASEMENT
#define AP_DISCORD_LEVEL_FIRST_HUB       25 // GEM_STONE_VALLEY
#define AP_DISCORD_LEVEL_LAST_HUB        29 // CITADEL_CITY
#define AP_DISCORD_LEVEL_MAIN_MENU       39 // MAIN_MENU_LEVEL
#define AP_DISCORD_LEVEL_GARAGE          40 // ADVENTURE_GARAGE

typedef struct
{
	int gameMode1;
	int gameMode2;
	int levelID;
	int loading; // a level load is in flight
} ApDiscordGameFacts;

static inline int AP_Discord_ClassifyView(const ApDiscordGameFacts *f)
{
	const int gm1 = f->gameMode1;
	const int lev = f->levelID;

	if (f->loading || (gm1 & AP_DISCORD_GM1_LOADING))
		return AP_DISCORD_VIEW_NONE;
	if (lev == AP_DISCORD_LEVEL_GARAGE)
		return AP_DISCORD_VIEW_GARAGE;
	if (lev == AP_DISCORD_LEVEL_MAIN_MENU || (gm1 & AP_DISCORD_GM1_MAIN_MENU))
		return AP_DISCORD_VIEW_MAIN_MENU;
	if (gm1 & AP_DISCORD_GM1_GAME_CUTSCENE)
		return AP_DISCORD_VIEW_CUTSCENE;
	if (lev >= AP_DISCORD_LEVEL_FIRST_HUB && lev <= AP_DISCORD_LEVEL_LAST_HUB)
		return (gm1 & AP_DISCORD_GM1_ADVENTURE_ARENA) ? AP_DISCORD_VIEW_HUB : AP_DISCORD_VIEW_NONE;
	if (lev < 0 || lev > AP_DISCORD_LEVEL_LAST_ARENA)
		return AP_DISCORD_VIEW_CUTSCENE; // intros, endings, credits scenes
	// ADVENTURE_BOSS is the sign bit (IS_BOSS_RACE in namespace_Main.h).
	if (gm1 < 0)
		return AP_DISCORD_VIEW_BOSS_RACE;
	if (gm1 & AP_DISCORD_GM1_RELIC_RACE)
		return AP_DISCORD_VIEW_RELIC_RACE;
	if (gm1 & AP_DISCORD_GM1_CRYSTAL_CHALLENGE)
		return AP_DISCORD_VIEW_CRYSTAL_CHALLENGE;
	if (gm1 & AP_DISCORD_GM1_ADVENTURE_CUP)
		return AP_DISCORD_VIEW_GEM_CUP;
	if (gm1 & AP_DISCORD_GM1_ADVENTURE_MODE)
		return (f->gameMode2 & AP_DISCORD_GM2_TOKEN_RACE) ? AP_DISCORD_VIEW_CTR_CHALLENGE
		                                                   : AP_DISCORD_VIEW_TROPHY_RACE;
	if (gm1 & AP_DISCORD_GM1_TIME_TRIAL)
		return AP_DISCORD_VIEW_TIME_TRIAL;
	if (gm1 & AP_DISCORD_GM1_BATTLE_MODE)
		return AP_DISCORD_VIEW_BATTLE;
	if (gm1 & AP_DISCORD_GM1_ARCADE_MODE)
		return (f->gameMode2 & AP_DISCORD_GM2_CUP_ANY_KIND) ? AP_DISCORD_VIEW_ARCADE_CUP
		                                                    : AP_DISCORD_VIEW_ARCADE;
	return AP_DISCORD_VIEW_VERSUS;
}

static inline int AP_Discord_ViewIsRace(int view)
{
	return view >= AP_DISCORD_VIEW_TROPHY_RACE && view < AP_DISCORD_VIEW__COUNT;
}

static inline const char *AP_Discord_LevelName(int levelID)
{
	static const char *const names[] = {
		"Dingo Canyon", "Dragon Mines", "Blizzard Bluff", "Crash Cove",
		"Tiger Temple", "Papu's Pyramid", "Roo's Tubes", "Hot Air Skyway",
		"Sewer Speedway", "Mystery Caves", "Cortex Castle", "N. Gin Labs",
		"Polar Pass", "Oxide Station", "Coco Park", "Tiny Arena",
		"Slide Coliseum", "Turbo Track",
		"Nitro Court", "Rampage Ruins", "Parking Lot", "Skull Rock",
		"The North Bowl", "Rocky Road", "Lab Basement",
		"Gem Stone Valley", "N. Sanity Beach", "The Lost Ruins",
		"Glacier Park", "Citadel City",
	};
	if (levelID < 0 || levelID >= (int)(sizeof names / sizeof names[0]))
		return NULL;
	return names[levelID];
}

static inline const char *AP_Discord_GemCupName(int cupID)
{
	static const char *const names[] = {
		"Red Gem Cup", "Green Gem Cup", "Blue Gem Cup", "Yellow Gem Cup", "Purple Gem Cup",
	};
	if (cupID < 0 || cupID >= (int)(sizeof names / sizeof names[0]))
		return "Gem Cup";
	return names[cupID];
}

// ── The activity ───────────────────────────────────────────────────────────
typedef struct
{
	char details[AP_DISCORD_TEXT_MAX + 1]; // first line
	char state[AP_DISCORD_TEXT_MAX + 1];   // second line, "" = none
	long long startEpoch;                  // race start, unix seconds; 0 = no clock
} ApDiscordActivity;

typedef struct
{
	int view;          // ApDiscordView
	int levelID;
	int cupID;         // gem cup colour, for AP_DISCORD_VIEW_GEM_CUP
	int cortexVortex;  // the level on screen is the Cortex Vortex custom track
	int customTrack;   // another custom track is being served on this level
	int apConnected;
	int checked;       // locations checked in this slot
	int total;         // all locations in this slot
	long long startEpoch;
} ApDiscordInput;

static inline int AP_Discord_Activity_Equal(const ApDiscordActivity *a, const ApDiscordActivity *b)
{
	return strcmp(a->details, b->details) == 0 && strcmp(a->state, b->state) == 0 &&
	       a->startEpoch == b->startEpoch;
}

// Fill `out` from `in`. Returns 0 (and leaves `out` cleared) for VIEW_NONE.
static inline int AP_Discord_Compose(const ApDiscordInput *in, ApDiscordActivity *out)
{
	const char *track;
	const char *mode = NULL;

	memset(out, 0, sizeof *out);
	switch (in->view)
	{
	case AP_DISCORD_VIEW_MAIN_MENU:   snprintf(out->details, sizeof out->details, "In the main menu"); break;
	case AP_DISCORD_VIEW_GARAGE:      snprintf(out->details, sizeof out->details, "Choosing a racer"); break;
	case AP_DISCORD_VIEW_CUTSCENE:    snprintf(out->details, sizeof out->details, "Watching a cutscene"); break;
	case AP_DISCORD_VIEW_HUB:
		track = AP_Discord_LevelName(in->levelID);
		snprintf(out->details, sizeof out->details, "Exploring %s", track ? track : "the adventure");
		break;
	case AP_DISCORD_VIEW_TROPHY_RACE:       mode = "Trophy Race"; break;
	case AP_DISCORD_VIEW_BOSS_RACE:         mode = "Boss Race"; break;
	case AP_DISCORD_VIEW_RELIC_RACE:        mode = "Relic Race"; break;
	case AP_DISCORD_VIEW_CTR_CHALLENGE:     mode = "CTR Challenge"; break;
	case AP_DISCORD_VIEW_CRYSTAL_CHALLENGE: mode = "Crystal Challenge"; break;
	case AP_DISCORD_VIEW_GEM_CUP:           mode = AP_Discord_GemCupName(in->cupID); break;
	case AP_DISCORD_VIEW_ARCADE:            mode = "Arcade"; break;
	case AP_DISCORD_VIEW_ARCADE_CUP:        mode = "Arcade Cup"; break;
	case AP_DISCORD_VIEW_TIME_TRIAL:        mode = "Time Trial"; break;
	case AP_DISCORD_VIEW_VERSUS:            mode = "Versus"; break;
	case AP_DISCORD_VIEW_BATTLE:            mode = "Battle"; break;
	default:
		return 0;
	}

	if (mode != NULL)
	{
		if (in->cortexVortex)
			track = "Cortex Vortex";
		else if (in->customTrack)
			track = "Custom track";
		else
			track = AP_Discord_LevelName(in->levelID);
		if (track != NULL)
			snprintf(out->details, sizeof out->details, "%s, %s", track, mode);
		else
			snprintf(out->details, sizeof out->details, "%s", mode);
		out->startEpoch = in->startEpoch > 0 ? in->startEpoch : 0;
	}

	if (in->apConnected && in->total > 0)
	{
		int checked = in->checked < 0 ? 0 : (in->checked > in->total ? in->total : in->checked);
		snprintf(out->state, sizeof out->state, "Checks: %d/%d", checked, in->total);
	}
	return 1;
}

// Race clock: the start time stays fixed for one race (same view and level)
// and restarts when either changes. Non-race views have no clock.
typedef struct
{
	int view;
	int levelID;
	long long start;
} ApDiscordRaceClock;

static inline long long AP_Discord_RaceClockUpdate(ApDiscordRaceClock *c, int view, int levelID, long long now)
{
	if (!AP_Discord_ViewIsRace(view))
	{
		c->view = view;
		c->levelID = levelID;
		c->start = 0;
		return 0;
	}
	if (c->start == 0 || c->view != view || c->levelID != levelID)
	{
		c->view = view;
		c->levelID = levelID;
		c->start = now;
	}
	return c->start;
}

// ── IPC frames ─────────────────────────────────────────────────────────────
// Each frame is a little-endian uint32 opcode, a little-endian uint32 payload
// length, then that many bytes of JSON.
enum
{
	AP_DISCORD_OP_HANDSHAKE = 0,
	AP_DISCORD_OP_FRAME = 1,
	AP_DISCORD_OP_CLOSE = 2,
	AP_DISCORD_OP_PING = 3,
	AP_DISCORD_OP_PONG = 4,
};
#define AP_DISCORD_FRAME_HEADER 8
#define AP_DISCORD_FRAME_MAX_PAYLOAD (64u * 1024u)

static inline void AP_Discord_PutHeader(unsigned char out[AP_DISCORD_FRAME_HEADER], uint32_t op, uint32_t len)
{
	for (int i = 0; i < 4; i++)
	{
		out[i] = (unsigned char)(op >> (8 * i));
		out[4 + i] = (unsigned char)(len >> (8 * i));
	}
}

// Returns 1 for a header this client accepts (known opcode, payload within
// AP_DISCORD_FRAME_MAX_PAYLOAD), 0 otherwise.
static inline int AP_Discord_GetHeader(const unsigned char in[AP_DISCORD_FRAME_HEADER], uint32_t *op, uint32_t *len)
{
	uint32_t o = 0, l = 0;
	for (int i = 0; i < 4; i++)
	{
		o |= (uint32_t)in[i] << (8 * i);
		l |= (uint32_t)in[4 + i] << (8 * i);
	}
	*op = o;
	*len = l;
	return o <= AP_DISCORD_OP_PONG && l <= AP_DISCORD_FRAME_MAX_PAYLOAD;
}

// ── Connection status (worker -> game thread, logged on change) ────────────
enum
{
	AP_DISCORD_STATUS_OFF = 0,     // disabled, no application ID, or never started
	AP_DISCORD_STATUS_SEARCHING,   // enabled, Discord not found (retrying)
	AP_DISCORD_STATUS_CONNECTED,
	AP_DISCORD_STATUS_REJECTED,    // Discord refused the handshake (bad application ID)
};

// ── Scheduler ──────────────────────────────────────────────────────────────
// Decides when the worker connects and sends. Times are a monotonic
// millisecond clock (wrap-safe unsigned arithmetic).
//   * Enabled and not connected: connect now, then retry after firstRetryMs,
//     then every retryMs. A drop from a live connection retries after
//     firstRetryMs (a Discord restart comes back quickly).
//   * Connected: send only when the wanted activity changed, at most once per
//     minSendMs. The first activity after connecting goes out at once.
//   * Disabled while connected: close (the worker clears the activity first).
enum
{
	AP_DISCORD_DO_WAIT = 0,
	AP_DISCORD_DO_CONNECT,
	AP_DISCORD_DO_SEND,
	AP_DISCORD_DO_CLOSE,
};
#define AP_DISCORD_WAIT_FOREVER 0xFFFFFFFFu

typedef struct
{
	uint32_t firstRetryMs;
	uint32_t retryMs;
	uint32_t minSendMs;
	uint32_t pollMs;      // how often a connected worker looks at the socket

	int connected;
	int retryArmed;       // 0 = connect at the next chance
	uint32_t nextAttempt;
	int failures;
	int haveSent;         // an activity went out on this connection
	uint32_t lastSend;
	uint32_t sentGen;
} ApDiscordSched;

static inline void AP_DiscordSched_Init(ApDiscordSched *s, uint32_t firstRetryMs, uint32_t retryMs,
                                        uint32_t minSendMs, uint32_t pollMs)
{
	memset(s, 0, sizeof *s);
	s->firstRetryMs = firstRetryMs;
	s->retryMs = retryMs;
	s->minSendMs = minSendMs;
	s->pollMs = pollMs;
}

static inline uint32_t AP_DiscordSched_Remaining(uint32_t now, uint32_t due)
{
	int32_t d = (int32_t)(due - now);
	return d > 0 ? (uint32_t)d : 0;
}

// wanted: enabled with an application ID. desiredGen: generation of the
// activity the game wants shown, 0 = nothing published yet. *waitMs: how long
// the worker may sleep before asking again (when the result is DO_WAIT).
static inline int AP_DiscordSched_Next(ApDiscordSched *s, uint32_t now, int wanted,
                                       uint32_t desiredGen, uint32_t *waitMs)
{
	*waitMs = AP_DISCORD_WAIT_FOREVER;
	if (!wanted)
	{
		if (s->connected)
			return AP_DISCORD_DO_CLOSE;
		s->retryArmed = 0; // switching back on connects straight away
		s->failures = 0;
		return AP_DISCORD_DO_WAIT;
	}
	if (!s->connected)
	{
		uint32_t left = s->retryArmed ? AP_DiscordSched_Remaining(now, s->nextAttempt) : 0;
		if (left == 0)
			return AP_DISCORD_DO_CONNECT;
		*waitMs = left;
		return AP_DISCORD_DO_WAIT;
	}
	if (desiredGen != 0 && (!s->haveSent || desiredGen != s->sentGen))
	{
		uint32_t left = s->haveSent ? AP_DiscordSched_Remaining(now, s->lastSend + s->minSendMs) : 0;
		if (left == 0)
			return AP_DISCORD_DO_SEND;
		*waitMs = left < s->pollMs ? left : s->pollMs;
		return AP_DISCORD_DO_WAIT;
	}
	*waitMs = s->pollMs;
	return AP_DISCORD_DO_WAIT;
}

static inline void AP_DiscordSched_OnConnect(ApDiscordSched *s, uint32_t now, int ok)
{
	if (ok)
	{
		s->connected = 1;
		s->failures = 0;
		s->haveSent = 0;
		s->retryArmed = 0;
		return;
	}
	s->failures++;
	s->retryArmed = 1;
	s->nextAttempt = now + (s->failures == 1 ? s->firstRetryMs : s->retryMs);
}

// A send (ok = 0) or read failed, or Discord closed the connection.
static inline void AP_DiscordSched_OnDisconnect(ApDiscordSched *s, uint32_t now)
{
	s->connected = 0;
	s->haveSent = 0;
	s->failures = 1;
	s->retryArmed = 1;
	s->nextAttempt = now + s->firstRetryMs;
}

static inline void AP_DiscordSched_OnSent(ApDiscordSched *s, uint32_t now, uint32_t gen)
{
	s->haveSent = 1;
	s->lastSend = now;
	s->sentGen = gen;
}

// The worker closed on purpose (option off, application ID changed).
static inline void AP_DiscordSched_OnClosed(ApDiscordSched *s)
{
	s->connected = 0;
	s->haveSent = 0;
	s->retryArmed = 0;
	s->failures = 0;
}

#endif // AP_DISCORD_LOGIC_H
