#pragma once

#include <string.h>
#include <platform/native_config.h>

// Pages of the in-game Options menu (game/230/MM_ConfigMenu.c). Which page a
// row appears on is a menu decision, kept apart from the config.ini [section]
// the row is saved under, so rows can move between pages without breaking the
// settings players already saved. Rows are listed by config key; a key the
// current build does not define (for example the Archipelago rows in a vanilla
// build) is simply absent from its page, and a page left empty is not shown.
//
// Grouping agreed 2026-09-22: Video holds rendering and display,
// Gameplay holds skips and race feel, Archipelago holds the multiworld rows.

// Rows that fit between the title rule and the bottom of the panel at the
// menu's 0x0E spacing from 0x3C. A twelfth row is drawn past the panel edge.
#define CTR_MENU_PAGE_VISIBLE_ROWS 11
// Storage bound per page, above the visible limit so an overfull page still
// builds (the harness is what rejects it).
#define CTR_MENU_PAGE_ROW_CAP 24
#define CTR_MENU_PAGE_CAP 20

typedef struct
{
	const char *name;
	const char *const *keys; // NULL-terminated
} CtrMenuPage;

static const char *const s_ctrMenuPageVideo[] = {
	"fullscreen", "aspect_ratio", "render_scale", "vsync", "smooth_scaling",
	"texture_filtering", "dithering", "increase_draw_distance",
	"disable_split_screen_lod", NULL,
};
static const char *const s_ctrMenuPageGameplay[] = {
	"skip_intro", "skip_hints", "skip_podium", "ai_difficulty",
	"mute_when_unfocused", NULL,
};
static const char *const s_ctrMenuPageConnection[] = {
	"uri", "slot", "password", NULL,
};
static const char *const s_ctrMenuPageArchipelago[] = {
	"map_flash", "death_link", "trap_duration", "update_check", NULL,
};
static const char *const s_ctrMenuPageAuthoring[] = {
	"box_author", "nav_record", "nav_use_recorded", "nav_driver_name", NULL,
};

static const CtrMenuPage s_ctrMenuPages[] = {
	{"Video", s_ctrMenuPageVideo},
	{"Gameplay", s_ctrMenuPageGameplay},
	{"Connection", s_ctrMenuPageConnection},
	{"Archipelago", s_ctrMenuPageArchipelago},
	{"Authoring", s_ctrMenuPageAuthoring},
};
#define CTR_MENU_PAGE_COUNT ((int)(sizeof(s_ctrMenuPages) / sizeof(s_ctrMenuPages[0])))

// Sections that are config-file-only and never get a page: Audio is edited on
// the vanilla audio screen, State is remembered state rather than an option.
static inline int CTR_MenuSectionHidden(const char *section)
{
	return strcmp(section, "Audio") == 0 || strcmp(section, "State") == 0;
}

static inline int CTR_MenuFindEntry(const ConfigEntry *entries, int numEntries, const char *key)
{
	for (int i = 0; i < numEntries; i++)
		if (strcmp(entries[i].key, key) == 0)
			return i;
	return -1;
}

// Lay the config rows out on pages. rows[p][r] receives an index into entries.
// A visible row missing from the page table is not lost: it lands on a page
// named after its config section, as the menu behaved before pages existed.
// Returns the number of pages written.
static inline int CTR_MenuBuildPages(const ConfigEntry *entries, int numEntries,
                                     const char *names[CTR_MENU_PAGE_CAP],
                                     int rows[CTR_MENU_PAGE_CAP][CTR_MENU_PAGE_ROW_CAP],
                                     int counts[CTR_MENU_PAGE_CAP])
{
	int numPages = 0;
	char placed[256] = {0};

	for (int p = 0; p < CTR_MENU_PAGE_COUNT && numPages < CTR_MENU_PAGE_CAP; p++)
	{
		int n = 0;
		for (const char *const *k = s_ctrMenuPages[p].keys; *k != NULL; k++)
		{
			const int i = CTR_MenuFindEntry(entries, numEntries, *k);
			if (i < 0 || i >= (int)sizeof placed || placed[i] || n >= CTR_MENU_PAGE_ROW_CAP)
				continue;
			if (CTR_MenuSectionHidden(entries[i].section))
				continue;
			placed[i] = 1;
			rows[numPages][n++] = i;
		}
		if (n == 0)
			continue;
		names[numPages] = s_ctrMenuPages[p].name;
		counts[numPages] = n;
		numPages++;
	}

	for (int i = 0; i < numEntries && i < (int)sizeof placed; i++)
	{
		if (placed[i] || CTR_MenuSectionHidden(entries[i].section))
			continue;
		int p = 0;
		while (p < numPages && strcmp(names[p], entries[i].section) != 0)
			p++;
		if (p == numPages)
		{
			if (numPages >= CTR_MENU_PAGE_CAP)
				continue;
			names[p] = entries[i].section;
			counts[p] = 0;
			numPages++;
		}
		if (counts[p] < CTR_MENU_PAGE_ROW_CAP)
			rows[p][counts[p]++] = i;
		placed[i] = 1;
	}

	return numPages;
}
