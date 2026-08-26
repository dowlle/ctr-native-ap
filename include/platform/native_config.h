#ifndef NATIVE_CONFIG_H
#define NATIVE_CONFIG_H

#include <stdbool.h>

// User-facing options persisted to "config.ini" (next to the executable) and
// edited through the in-game options menu (see game/230/MM_ConfigMenu.c). Ported
// from thecodingbob/ctr-native (branch modularize-improve-config), trimmed to the
// options this fork ships -- the character/gate/portal unlocks and stat
// multipliers from that branch are intentionally omitted (they overlap the
// randomizer's gating surface).
typedef struct
{
	bool skipIntro;             // Video & QoL: skip boot intros, go straight to the main menu
	bool increaseDrawDistance;  // Video & QoL: render level geometry farther
	bool disableSplitScreenLod; // Video & QoL: hi-res character models in 3-4P split screen
	bool fullscreen;            // Video & QoL: borderless fullscreen window (default windowed)
	int  aspectRatio;           // Video & QoL: 0 = 4:3 (vanilla), 1 = 16:9, 2 = 16:10, 3 = 21:9
	bool dithering;             // Video & QoL: PSX-authentic dithering (default on)
	int  renderScale;           // Video & QoL: internal render scale. 1 = original PSX
	                            // raster + shipped VRAM presentation (default),
	                            // 2/3/4 = fixed multiples, 0 = native window.
	                            // See include/platform/native_render_scale.h.
	bool smoothScaling;         // Video & QoL: presentation filter for scaled render
	                            // modes. true = smooth (linear, default),
	                            // false = sharp (nearest). No effect at Original.
	bool textureFiltering;      // Video & QoL: bilinear PSX texture sampling in the
	                            // GTE shaders (default off: PSX-authentic point
	                            // sampling). Also flipped by the F3 debug key.
	// Audio: the vanilla audio screen's volumes (0-255) and stereo/mono mode.
	// Config-file-only -- persisted to config.ini [Audio] and edited through that
	// screen (game/MAIN/MainFreeze.c), NOT the in-game options menu (the [Audio]
	// section is gated out of it, see BuildSectionMap in game/230/MM_ConfigMenu.c).
	// -1 means "not captured yet": config.ini stays silent about audio and the
	// adventure-profile card keeps its vanilla say. Once the audio screen is
	// visited these hold real 0-255 values and config.ini is authoritative -- it
	// wins over the card, mirroring config.ini's precedence over ap-config.txt.
	int volFx;
	int volMusic;
	int volVoice;
	int stereo;
#ifdef CTR_AP
	bool skipHints;             // Archipelago: suppress Aku Aku mask hints
	bool mapFlash;              // Archipelago: hub-map "Raceable" flicker (default on)
	int  aiDifficulty;          // Archipelago: AI-difficulty preset, stored as the raw
	                            // engine difficulty VALUE (0=vanilla, 0x50, 0xa0, 0xf0,
	                            // 0x140, 0x280). Local value; synced to the per-slot
	                            // data-storage override on connect/edit. Comfort only.
	int  deathLink;             // Archipelago: DeathLink preference. -1 = follow the
	                            // seed option (default), 0 = force off, else a forced
	                            // CTR_DL_* tier (1 = mask_reset: deaths only, 2 =
	                            // any_hit: every landed hit). The menu row and the F9
	                            // toggle both edit this; ap_deathlink.c reads it
	                            // every frame.
	int  trapDuration;          // Archipelago: sustained trap lifetime in seconds.
	                            // 10/15/20/25/30/45/60/90, or 0 = full race.
	// Archipelago room, edited in the in-game connection manager (see
	// game/230/MM_ConfigMenu.c) and persisted to config.ini [Connection]. uri is
	// "ws://host:port". Slot names are at most 16 chars in AP but the buffers are
	// deliberately generous; empty uri means "no saved room" (startup skips the
	// auto-dial). Precedence over the legacy ap-config.txt uri/slot/password.
	char uri[128];
	char slot[64];
	char password[64];
	// Archipelago: pair-version update notice (issue #150). updateCheck is the
	// user's on/off switch (default on) for the whole notice; false suppresses it
	// on every surface. updateLastSeen is NOT a user setting -- it is the newest
	// seed pair version the title-screen notice has already been shown for, so the
	// same version never re-notifies there across sessions. It lives in a hidden
	// config-file-only section (the [Audio] precedent, see BuildSectionMap in
	// game/230/MM_ConfigMenu.c): a CFG_STRING row has no renderer in the generic
	// section menu, and it is state rather than an option. Empty = never shown.
	bool updateCheck;
	char updateLastSeen[32];
	// Archipelago: in-game AP box placement author mode (#182, tooling for
	// #109). Off by default; this toggle IS the gate for the author keys, so a
	// release player who never opens the menu can never reach them.
	bool boxAuthor;
	// AI lap recording. THREE options, not one, because the three are separate
	// decisions and only the first of them touches the player's disk.
	//
	// navRecord WRITES FILES. It banks the laps you drive and saves them under
	// `ap-navpaths/` when the race ends. Off by default and it must stay that
	// way: a player who never opens this menu must never find files they did not
	// ask for.
	//
	// navUseRecorded only READS. It hands recorded laps to the bots in place of
	// the ones baked into the level, so someone who wants recorded lines never
	// has to turn recording on.
	//
	// navDriverName is the name stamped into a recording. Empty falls back to the
	// Archipelago slot name above. It is CFG_STRING, and unlike updateLastSeen it
	// IS a user option, so it lives in a visible section: the generic section menu
	// draws it read-only (Config_DrawValue in game/230/MM_ConfigMenu.c) and it is
	// edited in config.ini. An in-game text-entry widget is separate UI work and
	// does not gate recording.
	bool navRecord;
	bool navUseRecorded;
	char navDriverName[32];
#endif
} NativeConfig;

typedef enum
{
	CFG_BOOL,
	CFG_INT,
	CFG_STRING,
	CFG_ENUM   // int value chosen from a fixed ladder; rendered as a name, stepped
	           // left/right (see the AI-difficulty ladder in game/230/MM_ConfigMenu.c).
	           // Persists as its raw int value, same as CFG_INT.
} ConfigType;

typedef struct
{
	const char *section;
	const char *key;
	const char *label;
	ConfigType type;
	void *valuePtr;   // points into g_config (e.g. &g_config.skipIntro; the buffer itself for CFG_STRING)
	int min, max, step; // CFG_INT: slider bounds. CFG_STRING: max = buffer capacity. Ignored for CFG_BOOL.
} ConfigEntry;

extern NativeConfig g_config;
extern const ConfigEntry g_configEntries[];
extern const int g_numConfigEntries;

void NativeConfig_Load(void);
void NativeConfig_Save(void);

// Fullscreen state-machine decisions, kept freestanding so the platform layer's
// sync logic is testable out-of-engine (tools/test-graphics-options.c).
// F11 / Alt+Enter toggle: the new config value is the negation of the window's
// current state.
bool NativeConfig_FullscreenToggledFromWindow(bool windowFullscreen);

// Per-frame sync (Platform_BeginFrame): whether the window must be re-applied
// to match g_config.fullscreen.
bool NativeConfig_FullscreenNeedsReapply(bool want, bool have);

// true if a config.ini was present at load time. Consumers (e.g. the AP layer)
// use this to decide config.ini precedence over legacy flat config files.
bool NativeConfig_HasIni(void);

#ifdef CTR_AP
// Return a valid persisted Trap Duration value in milliseconds. Unknown or
// hand-edited values fail to the recommended 15-second default; 0 means the
// scheduler's full-race policy.
int NativeConfig_TrapDurationMs(void);
#endif

#endif
