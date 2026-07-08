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
#endif
};

const ConfigEntry g_configEntries[] = {
	{"Video & QoL", "skip_intro",               "Skip Intros",                  CFG_BOOL, &g_config.skipIntro},
	{"Video & QoL", "increase_draw_distance",   "Increase Draw Distance",       CFG_BOOL, &g_config.increaseDrawDistance},
	{"Video & QoL", "disable_split_screen_lod", "Hi-Res Models in Multiplayer", CFG_BOOL, &g_config.disableSplitScreenLod},
#ifdef CTR_AP
	{"Archipelago", "skip_hints",               "Skip Mask Hints",              CFG_BOOL, &g_config.skipHints},
	{"Archipelago", "map_flash",                "Map Flash",                    CFG_BOOL, &g_config.mapFlash},
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
				else
					*(int *)e->valuePtr = atoi(value);
				break;
			}
		}
	}

	fclose(f);
}

void NativeConfig_Save(void)
{
	FILE *f = fopen("config.ini", "w");
	if (!f)
		return;

	// Writing the file marks config.ini as authoritative from here on, so the AP
	// layer's config.ini-over-ap-config.txt precedence takes effect immediately.
	g_configIniPresent = true;

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

		if (e->type == CFG_BOOL)
			fprintf(f, "%s = %s\n", e->key, *(bool *)e->valuePtr ? "true" : "false");
		else
			fprintf(f, "%s = %d\n", e->key, *(int *)e->valuePtr);
	}

	fclose(f);
}
