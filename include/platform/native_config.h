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
	// Archipelago room, edited in the in-game connection manager (see
	// game/230/MM_ConfigMenu.c) and persisted to config.ini [Connection]. uri is
	// "ws://host:port". Slot names are at most 16 chars in AP but the buffers are
	// deliberately generous; empty uri means "no saved room" (startup skips the
	// auto-dial). Precedence over the legacy ap-config.txt uri/slot/password.
	char uri[128];
	char slot[64];
	char password[64];
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

// true if a config.ini was present at load time. Consumers (e.g. the AP layer)
// use this to decide config.ini precedence over legacy flat config files.
bool NativeConfig_HasIni(void);

#endif
