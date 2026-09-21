#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <platform/native_config.h>
#include <platform/native_disc_resolution.h>
#include <platform/native_render_scale.h>
#include <platform/native_window_geometry.h>

// Options live in "config.ini" in the working directory -- the same place the AP
// layer reads "ap-config.txt" from (see ap/ap_hooks.c AP_ReadConfig). Ported from
// thecodingbob/ctr-native (branch modularize-improve-config); his code wrote
// build/config.ini, changed here to a cwd-relative path so the menu and the AP
// config sit side by side.

NativeConfig g_config = {
	false, // skipIntro
	false, // increaseDrawDistance
	false, // disableSplitScreenLod
	false, // fullscreen (default windowed)
	0,     // aspectRatio (0 = 4:3, vanilla)
	true,  // dithering (default on: PSX-authentic)
	1,     // renderScale (1 = original PSX raster, the shipped default)
	true,  // smoothScaling (default on: linear presentation at scaled modes)
	false, // textureFiltering (default off: PSX-authentic point sampling)
	0,     // vsync (0 = Off, default: avoid double-throttling the retail draw-sync path)
	false, // muteWhenUnfocused (default off: a single-game setup keeps today's behaviour)
	NATIVE_WINDOW_GEOMETRY_POS_UNSET, // windowX  (unset = never saved)
	NATIVE_WINDOW_GEOMETRY_POS_UNSET, // windowY
	0,     // windowWidth  (0 = never saved)
	0,     // windowHeight
	false, // windowMaximized
	-1,    // volFx    (-1 = audio not captured; card / boot defaults stand)
	-1,    // volMusic
	-1,    // volVoice
	-1,    // stereo
	"",    // discPath (empty = no remembered external disc image)
#ifdef CTR_AP
	false, // skipHints
	true,  // mapFlash (default on: vanilla-style Raceable flicker)
	0,     // aiDifficulty (0 = vanilla)
	-1,    // deathLink (-1 = follow the seed option)
	15,    // trapDuration (recommended default, seconds; 0 = full race)
	"",    // uri      (empty = no saved room; startup skips the auto-dial)
	"",    // slot
	"",    // password
	true,  // updateCheck (default on: the pair-version notice is informational)
	"",    // updateLastSeen (empty = the title notice has never been shown)
	false, // boxAuthor (off: an authoring tool, not a player option)
	false, // navRecord (off: this one writes files to the player's disk)
	false, // navUseRecorded (off: changes how the AI drives)
	"",    // navDriverName (empty = fall back to the Archipelago slot name)
#endif
};

const ConfigEntry g_configEntries[] = {
	{"Video & QoL", "skip_intro",               "Skip Intros",                  CFG_BOOL, &g_config.skipIntro},
	{"Video & QoL", "increase_draw_distance",   "Increase Draw Distance",       CFG_BOOL, &g_config.increaseDrawDistance},
	{"Video & QoL", "disable_split_screen_lod", "Hi-Res Models in Multiplayer", CFG_BOOL, &g_config.disableSplitScreenLod},
	// The three graphics options ported from thecodingbob/ctr-native
	// (widescreen-option / fullscreen-option / dithering-option branches). The
	// aspect_ratio row is CFG_ENUM: 0 = 4:3 (vanilla default), 1 = 16:9,
	// 2 = 16:10, 3 = 21:9, stepped through a fixed ladder and rendered as a name
	// (see the s_aspectValues/s_aspectNames ladders in game/230/MM_ConfigMenu.c).
	// Persists as its raw int value, same as CFG_INT.
	{"Video & QoL", "dithering",                "Dithering",                    CFG_BOOL, &g_config.dithering},
	{"Video & QoL", "fullscreen",               "Fullscreen",                   CFG_BOOL, &g_config.fullscreen},
	{"Video & QoL", "aspect_ratio",             "Aspect Ratio",                 CFG_ENUM, &g_config.aspectRatio},
	// Render-scale ladder (CFG_ENUM): 1 = ORIGINAL (shipped raster + VRAM
	// present, the default), 2/3/4 = fixed multiples, 0 = NATIVE (window-sized
	// raster). Stored as the raw mode value; the renderer clamps out-of-ladder
	// hand edits (NativeRenderScale_ClampMode) and applies edits on the next
	// frame boundary, so none of these rows needs a menu-exit hook.
	{"Video & QoL", "render_scale",             "Render Scale",                 CFG_ENUM, &g_config.renderScale},
	{"Video & QoL", "smooth_scaling",           "Smooth Scaling",               CFG_BOOL, &g_config.smoothScaling},
	{"Video & QoL", "texture_filtering",        "Texture Filtering",            CFG_BOOL, &g_config.textureFiltering},
	// VSync ladder (CFG_ENUM): 0 = Off (default), 1 = On, 2 = Adaptive. See
	// include/platform/native_vsync.h for the option -> SDL swap-interval
	// mapping and NativeRenderer_UpdateSwapIntervalState (platform/
	// native_renderer.c) for where it is applied, cached, and re-applied.
	{"Video & QoL", "vsync",                    "VSync",                        CFG_ENUM, &g_config.vsync},
	// Mute-when-unfocused (CFG_BOOL): off by default. On, the audio output is
	// silenced whenever the game window does not have input focus, for players
	// running several randomizer clients side by side. Decided in
	// include/platform/native_focus_mute.h, applied as an SDL output-stream gain
	// by NativeAudio_UpdateFocusMuteState (platform/native_audio.c) from
	// Platform_BeginScene, so it is picked up on the next frame and needs no
	// menu-exit hook.
	{"Video & QoL", "mute_when_unfocused",      "Mute When Unfocused",          CFG_BOOL, &g_config.muteWhenUnfocused},
	// Remembered window geometry: config-file-only, exactly like update_last_seen
	// below (State section, gated out of the in-game menu in BuildSectionMap,
	// game/230/MM_ConfigMenu.c). Gathered/applied in platform/native_renderer.c;
	// decision rules in include/platform/native_window_geometry.h.
	{"State",       "window_x",                 "Window X",                     CFG_INT,  &g_config.windowX},
	{"State",       "window_y",                 "Window Y",                     CFG_INT,  &g_config.windowY},
	{"State",       "window_w",                 "Window Width",                 CFG_INT,  &g_config.windowWidth},
	{"State",       "window_h",                 "Window Height",                CFG_INT,  &g_config.windowHeight},
	{"State",       "window_maximized",         "Window Maximized",             CFG_BOOL, &g_config.windowMaximized},
	// Remembered external disc image (issue #334, slice 2). CFG_DISC_PATH:
	// validated on load, written back verbatim. Hidden from the in-game menu
	// with the rest of [State].
	{"State",       "disc_path",                "Disc Image",                   CFG_DISC_PATH, g_config.discPath, 0, (int)sizeof(g_config.discPath), 0},
	// Audio section: config-file-only. Hidden from the in-game options menu (gated
	// out of BuildSectionMap in game/230/MM_ConfigMenu.c) because it is edited
	// through the vanilla audio screen and a CFG_INT would render there as a bare
	// "%d%%". Persisted here so the audio choice survives a launch. min/max/step
	// are unused (the menu never renders these rows), kept for CFG_INT table shape.
	{"Audio",       "vol_fx",                   "FX Volume",                    CFG_INT,  &g_config.volFx,    0, 255, 4},
	{"Audio",       "vol_music",                "Music Volume",                 CFG_INT,  &g_config.volMusic, 0, 255, 4},
	{"Audio",       "vol_voice",                "Voice Volume",                 CFG_INT,  &g_config.volVoice, 0, 255, 4},
	{"Audio",       "stereo",                   "Stereo",                       CFG_INT,  &g_config.stereo,   0, 1,   1},
#ifdef CTR_AP
	// Connection section (before Archipelago): CFG_STRING rows edited in the
	// connection manager. max = buffer capacity (see MM_ConfigMenu.c).
	{"Connection",  "uri",                      "Server",                       CFG_STRING, g_config.uri,      0, (int)sizeof(g_config.uri),      0},
	{"Connection",  "slot",                     "Slot",                         CFG_STRING, g_config.slot,     0, (int)sizeof(g_config.slot),     0},
	{"Connection",  "password",                 "Password",                     CFG_STRING, g_config.password, 0, (int)sizeof(g_config.password), 0},
	{"Archipelago", "skip_hints",               "Skip Mask Hints",              CFG_BOOL, &g_config.skipHints},
	{"Archipelago", "map_flash",                "Map Flash",                    CFG_BOOL, &g_config.mapFlash},
	// CFG_ENUM: AI-difficulty preset, stepped through a fixed value ladder and
	// rendered as a preset name (see MM_ConfigMenu.c). Stored as its raw value.
	{"Archipelago", "ai_difficulty",            "AI Difficulty",                CFG_ENUM, &g_config.aiDifficulty},
	{"Archipelago", "death_link",               "DeathLink",                    CFG_ENUM, &g_config.deathLink},
	{"Archipelago", "trap_duration",            "Trap Duration",                CFG_ENUM, &g_config.trapDuration},
	// Pair-version update notice (issue #150). A plain CFG_BOOL alongside
	// skip_hints/map_flash, so it renders and toggles with no menu changes.
	{"Archipelago", "update_check",             "Update Check",                 CFG_BOOL, &g_config.updateCheck},
	// State section: config-file-only, exactly like [Audio] above -- gated out of
	// BuildSectionMap (game/230/MM_ConfigMenu.c) so it is never a menu section.
	// This is remembered state, not an option, which is the reason it is hidden.
	// (The generic renderer does draw CFG_STRING rows read-only now, so hiding
	// this one is a decision about what it IS, no longer a missing renderer.)
	// Written by the title-screen notice itself, never by a menu row.
	{"State",       "update_last_seen",         "Update Last Seen",             CFG_STRING, g_config.updateLastSeen, 0, (int)sizeof(g_config.updateLastSeen), 0},
	// Box placement editing is absent from ordinary public builds. Keeping the
	// row out of g_configEntries means NativeConfig_Load cannot revive it from a
	// stale box_author=true, while ap_author.c independently forces the runtime
	// gate off as defence in depth. Dedicated authoring builds opt in explicitly.
#ifdef CTR_AP_AUTHORING
	{"Authoring",   "box_author",               "Box Author Mode",              CFG_BOOL, &g_config.boxAuthor},
#endif
	// "Save" is in the label deliberately. The one option in this build that
	// writes to the player's disk should say so on the row itself, not only in a
	// manual nobody reads.
	{"Authoring",   "nav_record",               "Save AI Lap Recordings",       CFG_BOOL, &g_config.navRecord},
	{"Authoring",   "nav_use_recorded",         "Use Recorded AI Laps",         CFG_BOOL, &g_config.navUseRecorded},
	// Read-only on the menu, edited in config.ini. Empty renders as "-" and means
	// the Archipelago slot name is used instead.
	{"Authoring",   "nav_driver_name",          "Driver Name",                  CFG_STRING, g_config.navDriverName, 0, (int)sizeof(g_config.navDriverName), 0},
#endif
};

const int g_numConfigEntries = sizeof(g_configEntries) / sizeof(g_configEntries[0]);

static bool g_configIniPresent = false;

bool NativeConfig_HasIni(void)
{
	return g_configIniPresent;
}

#ifdef CTR_AP
int NativeConfig_TrapDurationMs(void)
{
	static const int allowed[] = {10, 15, 20, 25, 30, 45, 60, 90};
	int i;
	if (g_config.trapDuration == 0)
		return 0;
	for (i = 0; i < (int)(sizeof(allowed) / sizeof(allowed[0])); i++)
		if (g_config.trapDuration == allowed[i])
			return allowed[i] * 1000;
	return 15000;
}
#endif

bool NativeConfig_FullscreenToggledFromWindow(bool windowFullscreen)
{
	return !windowFullscreen;
}

bool NativeConfig_FullscreenNeedsReapply(bool want, bool have)
{
	return want != have;
}

static bool ParseBool(const char *s)
{
	return strcmp(s, "true") == 0 || strcmp(s, "1") == 0;
}

static char *trimWhitespace(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	if (*s == '\0')
		return s;
	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end))
		end--;
	*(end + 1) = '\0';
	return s;
}

// Longest config.ini line accepted. A longer line is rejected whole instead of
// being split by the old fixed fgets buffer, which silently turned one long
// value (e.g. a disc path) into several bogus keys (issue #334, slice 2).
#define NATIVE_CONFIG_LINE_MAX 4096

// Bounded full-line read. Returns 1 for a line that fits, 0 at end of file, and
// -1 for a line longer than dstSize-1. The excess is always consumed so the
// next read starts on the next line, and dst then holds an unusable prefix the
// caller must ignore.
static int NativeConfig_ReadLine(FILE *f, char *dst, size_t dstSize)
{
	size_t len = 0;
	int c;
	int overlong = 0;
	int any = 0;

	if (dstSize == 0)
		return 0;

	while ((c = fgetc(f)) != EOF)
	{
		any = 1;
		if (c == '\n')
			break;
		if (len + 1 < dstSize)
			dst[len++] = (char)c;
		else
			overlong = 1;
	}

	dst[len] = '\0';
	if (!any)
		return 0;
	return overlong ? -1 : 1;
}

void NativeConfig_Load(void)
{
	FILE *f = fopen("config.ini", "r");
	if (!f)
	{
		g_configIniPresent = false;
		printf("[Config] config.ini not found, using defaults\n");
		return;
	}

	g_configIniPresent = true;
	printf("[Config] loading config.ini\n");

	char line[NATIVE_CONFIG_LINE_MAX];
	char section[64] = "";
	int lineStatus;
	int firstLine = 1;

	while ((lineStatus = NativeConfig_ReadLine(f, line, sizeof(line))) != 0)
	{
		char *p;

		if (lineStatus < 0)
		{
			fprintf(stderr, "[Config] line longer than %d bytes ignored\n", (int)(sizeof(line) - 1));
			firstLine = 0;
			continue;
		}

		// A UTF-8 BOM before the first section header must not hide that header
		// from the parser, or every key in the first section would be skipped.
		p = line;
		if (firstLine && ((unsigned char)p[0] == 0xEF) && ((unsigned char)p[1] == 0xBB) && ((unsigned char)p[2] == 0xBF))
			p += 3;
		firstLine = 0;

		p = trimWhitespace(p);

		if (*p == '\0' || *p == ';' || *p == '#')
			continue;

		if (*p == '[')
		{
			char *end = strchr(p + 1, ']');
			if (end)
			{
				*end = '\0';
				strncpy(section, p + 1, sizeof(section) - 1);
				section[sizeof(section) - 1] = '\0';
			}
			continue;
		}

		char *eq = strchr(p, '=');
		if (!eq)
			continue;

		*eq = '\0';
		char *key = trimWhitespace(p);
		char *value = trimWhitespace(eq + 1);

		for (int i = 0; i < g_numConfigEntries; i++)
		{
			const ConfigEntry *e = &g_configEntries[i];
			if (strcmp(section, e->section) == 0 &&
			    strcmp(key, e->key) == 0)
			{
				if (e->type == CFG_BOOL)
					*(bool *)e->valuePtr = ParseBool(value);
				else if (e->type == CFG_STRING)
				{
					// Existing keys keep exactly the pre-issue-334 behavior: an
					// over-capacity value is truncated to the buffer. The
					// stricter, reject-as-absent handling is deliberately limited
					// to CFG_DISC_PATH below so it cannot change how uri, slot,
					// password, update_last_seen or nav_driver_name load.
					strncpy((char *)e->valuePtr, value, e->max - 1);
					((char *)e->valuePtr)[e->max - 1] = '\0';
				}
				else if (e->type == CFG_DISC_PATH)
				{
					// Stored verbatim or treated as absent, never truncated. A
					// bad value gets one status line and leaves the field empty.
					NativeDiscPathStatus pathStatus = NativeDiscPath_Validate(value, strlen(value));
					if (pathStatus == NATIVE_DISC_PATH_OK)
						memcpy((char *)e->valuePtr, value, strlen(value) + 1);
					else
					{
						((char *)e->valuePtr)[0] = '\0';
						fprintf(stderr, "[Config] %s.%s ignored: %s\n", section, key, NativeDiscPath_StatusText(pathStatus));
					}
				}
				else
					*(int *)e->valuePtr = atoi(value);
				break;
			}
		}
	}

	fclose(f);

	// Snap a hand-edited render_scale onto the supported ladder at load, the
	// same clamp the renderer applies every frame. This keeps the menu row and
	// the running renderer in agreement for out-of-ladder values (a persisted
	// 9 would otherwise render at 4x while the row reads ORIGINAL).
	g_config.renderScale = NativeRenderScale_ClampMode(g_config.renderScale);
#ifdef CTR_AP
	// Keep the in-memory/menu value inside the public ladder even when a player
	// hand-edited an unsupported value. Zero is the intentional Full race token.
	if (g_config.trapDuration != 0 &&
	    NativeConfig_TrapDurationMs() != g_config.trapDuration * 1000)
		g_config.trapDuration = 15;
#endif
}

// Look up the entry table row that owns a section/key pair (i.e. one this build
// knows about). Returns false for sections/keys outside the current build's
// table -- those are carried through verbatim on save.
static bool FindConfigEntry(const char *section, const char *key, int *outIndex)
{
	for (int i = 0; i < g_numConfigEntries; i++)
	{
		if (strcmp(section, g_configEntries[i].section) == 0 &&
		    strcmp(key, g_configEntries[i].key) == 0)
		{
			if (outIndex)
				*outIndex = i;
			return true;
		}
	}
	return false;
}

// Whether config.ini exists in the working directory. Used to tell a missing
// file (safe to create) from an existing file that could not be read, which
// must never be overwritten with defaults (issue #334, slice 2).
static int configFileExists(void)
{
#if defined(_WIN32)
	return _access("config.ini", 0) == 0;
#else
	return access("config.ini", F_OK) == 0;
#endif
}

static void WriteEntryLine(FILE *f, const ConfigEntry *e)
{
	if (e->type == CFG_BOOL)
		fprintf(f, "%s = %s\n", e->key, *(bool *)e->valuePtr ? "true" : "false");
	else if (e->type == CFG_DISC_PATH)
	{
		// Written only when it has a value (issue #334, slice 2). An empty disc
		// path means "no remembered external disc" and must not add a key to a
		// config that did not have one, so a config written by main comes out
		// byte-for-byte identical through a load-and-save with nothing changed.
		const char *value = (const char *)e->valuePtr;
		if (value[0] != '\0')
			fprintf(f, "%s = %s\n", e->key, value);
	}
	else if (e->type == CFG_STRING)
		fprintf(f, "%s = %s\n", e->key, (const char *)e->valuePtr);
	else
		fprintf(f, "%s = %d\n", e->key, *(int *)e->valuePtr);
}

int NativeConfig_Save(void)
{
	// Read the existing file first (before truncating it): NativeConfig_Save must
	// carry through any section or key this build's entry table does not own.
	// Otherwise saving from a build with a smaller table -- e.g. the vanilla build,
	// which has no [Connection]/[Archipelago] rows -- would silently drop the AP
	// build's sections. Owned keys are rewritten in place with their current value;
	// everything else (unknown sections/keys, comments, blank lines) is preserved.
	//
	// This save is deliberately not atomic in this slice: main truncates the file
	// in place, and this keeps that behavior. A crash during the write can leave a
	// partial file. Making the save atomic (write a temporary sibling, flush it,
	// then rename it over config.ini) is left for its own change.
	char *existing = NULL;
	char *scratch = NULL;
	const char *refusal = NULL;
	FILE *rf = fopen("config.ini", "r");
	if (rf == NULL)
	{
		// A missing config.ini is the normal first run: writing the entry table
		// straight out is correct. A file that exists but cannot be opened is
		// different: never fall back to writing defaults over it.
		if (configFileExists())
			refusal = "the existing file could not be opened for reading";
	}
	else
	{
		long len = 0;
		int readFailed = 0;

		if (fseek(rf, 0, SEEK_END) != 0)
			readFailed = 1;
		else
		{
			len = ftell(rf);
			if (len < 0)
				readFailed = 1;
			else if (fseek(rf, 0, SEEK_SET) != 0)
				readFailed = 1;
		}

		if (!readFailed && (len > 0))
		{
			existing = (char *)malloc((size_t)len + 1);
			if (existing == NULL)
				refusal = "not enough memory to preserve the existing file";
			else
			{
				size_t got = fread(existing, 1, (size_t)len, rf);

				if (got != (size_t)len)
					refusal = "the existing file could not be read";
				else
				{
					existing[got] = '\0';
					// An embedded NUL would terminate the line traversal below
					// and silently lose everything after it. Refuse instead.
					if (memchr(existing, '\0', got) != NULL)
						refusal = "the existing file contains an embedded NUL byte";
				}
			}
		}
		else if (readFailed)
			refusal = "the existing file could not be read";

		fclose(rf);
	}

	// Every allocation the save needs is made BEFORE config.ini is opened for
	// writing (issue #334, slice 2). The existing content is parsed from a
	// scratch copy, so the untouched original can be written back byte-for-byte
	// for anything this build does not own. If any allocation fails the save
	// aborts with the file untouched instead of falling back to defaults.
	if ((refusal == NULL) && (existing != NULL))
	{
		size_t existingLen = strlen(existing);

		scratch = (char *)malloc(existingLen + 1u);
		if (scratch == NULL)
			refusal = "not enough memory to preserve the existing file";
		else
			memcpy(scratch, existing, existingLen + 1u);
	}

	if (refusal != NULL)
	{
		free(existing);
		free(scratch);
		fprintf(stderr, "[Config] config.ini not saved: %s\n", refusal);
		return 0;
	}

	FILE *f = fopen("config.ini", "w");
	if (!f)
	{
		free(existing);
		free(scratch);
		fprintf(stderr, "[Config] config.ini not saved: cannot open for writing\n");
		return 0;
	}

	// Writing the file marks config.ini as authoritative from here on, so the AP
	// layer's config.ini-over-ap-config.txt precedence takes effect immediately.
	g_configIniPresent = true;

	if (!existing)
	{
		// No prior file (or empty): write the entry table straight out.
		const char *lastSection = NULL;
		for (int i = 0; i < g_numConfigEntries; i++)
		{
			const ConfigEntry *e = &g_configEntries[i];
			if (lastSection == NULL || strcmp(e->section, lastSection) != 0)
			{
				if (lastSection != NULL)
					fprintf(f, "\n");
				fprintf(f, "[%s]\n", e->section);
				lastSection = e->section;
			}
			WriteEntryLine(f, e);
		}
		fclose(f);
		free(scratch);
		return 1;
	}

	// Generously sized flag per owned entry; the entry table is small.
	bool written[64] = {false};

	char section[64] = "";
	size_t total = strlen(existing);
	size_t offset = 0;
	int firstLine = 1;
	while (offset < total)
	{
		const char *cursor = existing + offset;
		char *parse = scratch + offset;
		char *nl = strchr(cursor, '\n');
		size_t lineLen = nl ? (size_t)(nl - cursor) : strlen(cursor);
		size_t writeLen = nl ? lineLen + 1u : lineLen; // include the terminator, or none at EOF
		int owned = 0;
		char *p;

		// Parse the line from the scratch copy; the original stays untouched so
		// unknown lines can be written back byte-for-byte. No per-line
		// allocation happens here, so a full write cannot fail partway on
		// memory (issue #334, slice 2).
		parse[lineLen] = '\0';

		// A UTF-8 BOM on the first line is part of the file's bytes (it is
		// written back verbatim) but must not hide the first section header.
		p = parse;
		if (firstLine && ((unsigned char)p[0] == 0xEF) && ((unsigned char)p[1] == 0xBB) && ((unsigned char)p[2] == 0xBF))
			p += 3;
		firstLine = 0;

		p = trimWhitespace(p);

		if (*p == '[')
		{
			char *end = strchr(p + 1, ']');
			if (end)
			{
				*end = '\0';
				strncpy(section, p + 1, sizeof(section) - 1);
				section[sizeof(section) - 1] = '\0';
			}
		}
		else if ((*p != '\0') && (*p != ';') && (*p != '#'))
		{
			char *eq = strchr(p, '=');
			int idx = -1;

			if (eq)
			{
				*eq = '\0';
				if (FindConfigEntry(section, trimWhitespace(p), &idx))
				{
					WriteEntryLine(f, &g_configEntries[idx]); // owned: current value
					if (idx < (int)(sizeof(written) / sizeof(written[0])))
						written[idx] = true;
					owned = 1;
				}
			}
		}

		// Section headers, comments, blank lines, unknown keys and lines that are
		// not key=value are all written back byte-for-byte, including a line
		// longer than the old 255-byte scratch buffer and a missing final
		// newline. Only owned keys are normalized in place.
		if (!owned)
			fwrite(cursor, 1, writeLen, f);

		offset += writeLen;
	}

	free(existing);
	free(scratch);

	// Append any owned entries that were not already present in the file (e.g. an
	// option added to the table since the config was last written). A section that
	// already appeared but is missing a key gets a repeated header here; the loader
	// resolves keys against the most recent header, so this stays correct.
	const char *lastSection = NULL;
	for (int i = 0; i < g_numConfigEntries; i++)
	{
		if (i < (int)(sizeof(written) / sizeof(written[0])) && written[i])
			continue;
		const ConfigEntry *e = &g_configEntries[i];

		// An empty disc path writes no line, so it must not cause an empty
		// section header to be appended either.
		if ((e->type == CFG_DISC_PATH) && (((const char *)e->valuePtr)[0] == '\0'))
			continue;

		if (lastSection == NULL || strcmp(e->section, lastSection) != 0)
		{
			fprintf(f, "\n[%s]\n", e->section);
			lastSection = e->section;
		}
		WriteEntryLine(f, e);
	}

	fclose(f);
	return 1;
}
