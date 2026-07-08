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
#ifdef CTR_AP
	bool skipHints;             // Archipelago: suppress Aku Aku mask hints
	bool mapFlash;              // Archipelago: hub-map "Raceable" flicker (default on)
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
	CFG_STRING
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
