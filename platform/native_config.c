#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <platform/native_config.h>

// Options live in "config.ini" in the working directory -- the same place the AP
// layer reads "ap-config.txt" from (see ap/ap_hooks.c AP_ReadConfig). Ported from
// thecodingbob/ctr-native (branch modularize-improve-config); his code wrote
// build/config.ini, changed here to a cwd-relative path so the menu and the AP
// config sit side by side.

NativeConfig g_config = {
	false, // skipIntro
	false, // increaseDrawDistance
	false, // disableSplitScreenLod
#ifdef CTR_AP
	false, // skipHints
	true,  // mapFlash (default on: vanilla-style Raceable flicker)
	0,     // aiDifficulty (0 = vanilla)
	"",    // uri      (empty = no saved room; startup skips the auto-dial)
	"",    // slot
	"",    // password
#endif
};

const ConfigEntry g_configEntries[] = {
	{"Video & QoL", "skip_intro",               "Skip Intros",                  CFG_BOOL, &g_config.skipIntro},
	{"Video & QoL", "increase_draw_distance",   "Increase Draw Distance",       CFG_BOOL, &g_config.increaseDrawDistance},
	{"Video & QoL", "disable_split_screen_lod", "Hi-Res Models in Multiplayer", CFG_BOOL, &g_config.disableSplitScreenLod},
#ifdef CTR_AP
	// Connection section (before Archipelago): CFG_STRING rows edited in the
	// connection manager. max = buffer capacity (see MM_ConfigMenu.c).
	{"Connection",  "uri",                      "Server",                       CFG_STRING, g_config.uri,      0, (int)sizeof(g_config.uri),      0},
	{"Connection",  "slot",                     "Slot",                         CFG_STRING, g_config.slot,     0, (int)sizeof(g_config.slot),     0},
	{"Connection",  "password",                 "Password",                     CFG_STRING, g_config.password, 0, (int)sizeof(g_config.password), 0},
	{"Archipelago", "skip_hints",               "Skip Mask Hints",              CFG_BOOL, &g_config.skipHints},
	{"Archipelago", "map_flash",                "Map Flash",                    CFG_BOOL, &g_config.mapFlash},
	// CFG_INT slider: AI-difficulty comfort setting, 0..100% in 5% steps.
	{"Archipelago", "ai_difficulty",            "AI Difficulty",                CFG_INT,  &g_config.aiDifficulty, 0, 100, 5},
#endif
};

const int g_numConfigEntries = sizeof(g_configEntries) / sizeof(g_configEntries[0]);

static bool g_configIniPresent = false;

bool NativeConfig_HasIni(void)
{
	return g_configIniPresent;
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

	char line[256];
	char section[64] = "";

	while (fgets(line, sizeof(line), f))
	{
		char *p = trimWhitespace(line);

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
					// value is already trimmed; plain key=value, no quoting.
					strncpy((char *)e->valuePtr, value, e->max - 1);
					((char *)e->valuePtr)[e->max - 1] = '\0';
				}
				else
					*(int *)e->valuePtr = atoi(value);
				break;
			}
		}
	}

	fclose(f);
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

static void WriteEntryLine(FILE *f, const ConfigEntry *e)
{
	if (e->type == CFG_BOOL)
		fprintf(f, "%s = %s\n", e->key, *(bool *)e->valuePtr ? "true" : "false");
	else if (e->type == CFG_STRING)
		fprintf(f, "%s = %s\n", e->key, (const char *)e->valuePtr);
	else
		fprintf(f, "%s = %d\n", e->key, *(int *)e->valuePtr);
}

void NativeConfig_Save(void)
{
	// Read the existing file first (before truncating it): NativeConfig_Save must
	// carry through any section or key this build's entry table does not own.
	// Otherwise saving from a build with a smaller table -- e.g. the vanilla build,
	// which has no [Connection]/[Archipelago] rows -- would silently drop the AP
	// build's sections. Owned keys are rewritten in place with their current value;
	// everything else (unknown sections/keys, comments, blank lines) is preserved.
	char *existing = NULL;
	FILE *rf = fopen("config.ini", "r");
	if (rf)
	{
		fseek(rf, 0, SEEK_END);
		long len = ftell(rf);
		fseek(rf, 0, SEEK_SET);
		if (len > 0)
		{
			existing = (char *)malloc((size_t)len + 1);
			if (existing)
			{
				size_t got = fread(existing, 1, (size_t)len, rf);
				existing[got] = '\0';
			}
		}
		fclose(rf);
	}

	FILE *f = fopen("config.ini", "w");
	if (!f)
	{
		free(existing);
		return;
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
		return;
	}

	// Generously sized flag per owned entry; the entry table is small.
	bool written[64] = {false};

	char section[64] = "";
	char *cursor = existing;
	while (*cursor)
	{
		char *nl = strchr(cursor, '\n');
		size_t lineLen = nl ? (size_t)(nl - cursor) : strlen(cursor);

		char raw[256];
		size_t copyLen = lineLen < sizeof(raw) - 1 ? lineLen : sizeof(raw) - 1;
		memcpy(raw, cursor, copyLen);
		raw[copyLen] = '\0';
		if (copyLen > 0 && raw[copyLen - 1] == '\r') // tolerate CRLF
			raw[copyLen - 1] = '\0';

		char parse[256];
		strncpy(parse, raw, sizeof(parse) - 1);
		parse[sizeof(parse) - 1] = '\0';
		char *p = trimWhitespace(parse);

		if (*p == '[')
		{
			char *end = strchr(p + 1, ']');
			if (end)
			{
				*end = '\0';
				strncpy(section, p + 1, sizeof(section) - 1);
				section[sizeof(section) - 1] = '\0';
			}
			fprintf(f, "%s\n", raw); // section header, preserved verbatim
		}
		else if (*p == '\0' || *p == ';' || *p == '#')
		{
			fprintf(f, "%s\n", raw); // blank line or comment, preserved
		}
		else
		{
			char *eq = strchr(p, '=');
			if (eq)
			{
				*eq = '\0';
				char *key = trimWhitespace(p);
				int idx = -1;
				if (FindConfigEntry(section, key, &idx))
				{
					WriteEntryLine(f, &g_configEntries[idx]); // owned: current value
					if (idx < (int)(sizeof(written) / sizeof(written[0])))
						written[idx] = true;
				}
				else
				{
					fprintf(f, "%s\n", raw); // unknown key, preserved
				}
			}
			else
			{
				fprintf(f, "%s\n", raw); // not a key=value line, preserved
			}
		}

		if (!nl)
			break;
		cursor = nl + 1;
	}

	free(existing);

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
		if (lastSection == NULL || strcmp(e->section, lastSection) != 0)
		{
			fprintf(f, "\n[%s]\n", e->section);
			lastSection = e->section;
		}
		WriteEntryLine(f, e);
	}

	fclose(f);
}
