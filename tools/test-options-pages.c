// Out-of-engine assertions for the in-game Options page layout
// (include/ctr_menu_pages.h), built against the real config entry table in
// platform/native_config.c.
//
//   cc -Wall -Wextra -DCTR_AP -I include -I . -o /tmp/test-options-pages tools/test-options-pages.c && /tmp/test-options-pages
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * every menu-visible config row is on exactly one page, and Audio/State rows
//     are on none
//   * no page is longer than the rows the panel can draw (rc1 drew twelve rows on
//     Video & QoL and the last one fell off the panel)
//   * the page order and row order agreed on 2026-09-22
//   * every key in the page table exists in the entry table, except the
//     authoring-build-only box_author, so a renamed key cannot silently drop a
//     row onto a fallback page
//   * pages do not change where a row is saved: the config.ini sections of the
//     moved rows are the ones v0.2.0 wrote

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "platform/native_config.c"
#include "ctr_menu_pages.h"

static int g_failures = 0;

#define EXPECT_TRUE(name, expr)                                                        \
	do {                                                                                \
		if (!(expr)) {                                                                     \
			printf("FAIL %s\n", (name));                                                    \
			g_failures++;                                                                  \
		}                                                                                 \
	} while (0)

static const char *s_names[CTR_MENU_PAGE_CAP];
static int s_rows[CTR_MENU_PAGE_CAP][CTR_MENU_PAGE_ROW_CAP];
static int s_counts[CTR_MENU_PAGE_CAP];

static void ExpectPage(int page, const char *name, const char *const *keys)
{
	char label[128];
	int n = 0;

	snprintf(label, sizeof label, "page %d is %s", page, name);
	EXPECT_TRUE(label, strcmp(s_names[page], name) == 0);
	while (keys[n] != NULL)
		n++;
	snprintf(label, sizeof label, "%s has %d rows (got %d)", name, n, s_counts[page]);
	EXPECT_TRUE(label, s_counts[page] == n);
	for (int r = 0; r < n && r < s_counts[page]; r++)
	{
		snprintf(label, sizeof label, "%s row %d is %s", name, r, keys[r]);
		EXPECT_TRUE(label, strcmp(g_configEntries[s_rows[page][r]].key, keys[r]) == 0);
	}
}

static void ExpectSection(const char *key, const char *section)
{
	char label[128];
	const int i = CTR_MenuFindEntry(g_configEntries, g_numConfigEntries, key);

	snprintf(label, sizeof label, "%s is still saved under [%s]", key, section);
	EXPECT_TRUE(label, i >= 0 && strcmp(g_configEntries[i].section, section) == 0);
}

int main(void)
{
	const int numPages = CTR_MenuBuildPages(g_configEntries, g_numConfigEntries,
		s_names, s_rows, s_counts);

	// Exactly-once placement, hidden sections excluded.
	for (int i = 0; i < g_numConfigEntries; i++)
	{
		int seen = 0;
		char label[128];

		for (int p = 0; p < numPages; p++)
			for (int r = 0; r < s_counts[p]; r++)
				if (s_rows[p][r] == i)
					seen++;
		if (CTR_MenuSectionHidden(g_configEntries[i].section))
		{
			snprintf(label, sizeof label, "%s/%s is not on any page",
				g_configEntries[i].section, g_configEntries[i].key);
			EXPECT_TRUE(label, seen == 0);
		}
		else
		{
			snprintf(label, sizeof label, "%s/%s is on exactly one page (got %d)",
				g_configEntries[i].section, g_configEntries[i].key, seen);
			EXPECT_TRUE(label, seen == 1);
		}
	}

	for (int p = 0; p < numPages; p++)
	{
		char label[128];
		snprintf(label, sizeof label, "%s fits the panel (%d rows, limit %d)",
			s_names[p], s_counts[p], CTR_MENU_PAGE_VISIBLE_ROWS);
		EXPECT_TRUE(label, s_counts[p] <= CTR_MENU_PAGE_VISIBLE_ROWS);
	}

	for (int p = 0; p < CTR_MENU_PAGE_COUNT; p++)
		for (const char *const *k = s_ctrMenuPages[p].keys; *k != NULL; k++)
		{
			char label[128];
			if (strcmp(*k, "box_author") == 0)
				continue; // CTR_AP_AUTHORING builds only
			snprintf(label, sizeof label, "page key %s exists", *k);
			EXPECT_TRUE(label, CTR_MenuFindEntry(g_configEntries, g_numConfigEntries, *k) >= 0);
		}

	EXPECT_TRUE("five pages in the AP build", numPages == 5);
	if (numPages == 5)
	{
		static const char *const video[] = {
			"fullscreen", "aspect_ratio", "render_scale", "vsync", "smooth_scaling",
			"texture_filtering", "dithering", "increase_draw_distance",
			"disable_split_screen_lod", NULL};
		static const char *const gameplay[] = {
			"skip_intro", "skip_hints", "skip_podium", "ai_difficulty",
			"mute_when_unfocused", NULL};
		static const char *const connection[] = {"uri", "slot", "password", NULL};
		static const char *const archipelago[] = {
			"map_flash", "death_link", "trap_duration", "update_check", NULL};
		static const char *const authoring[] = {
			"nav_record", "nav_use_recorded", "nav_driver_name", NULL};

		ExpectPage(0, "Video", video);
		ExpectPage(1, "Gameplay", gameplay);
		ExpectPage(2, "Connection", connection);
		ExpectPage(3, "Archipelago", archipelago);
		ExpectPage(4, "Authoring", authoring);
	}

	ExpectSection("skip_intro", "Video & QoL");
	ExpectSection("mute_when_unfocused", "Video & QoL");
	ExpectSection("skip_podium", "Video & QoL");
	ExpectSection("skip_hints", "Archipelago");
	ExpectSection("ai_difficulty", "Archipelago");

	if (g_failures == 0)
		printf("options pages: all assertions passed (%d pages)\n", numPages);
	return g_failures == 0 ? 0 : 1;
}
