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
//   * the page order and row order agreed on 2026-09-22, with the bespoke
//     Controls page (0.3.0) after Gameplay: shown, and holding no config rows
//   * every key in the page table exists in the entry table, except the
//     authoring-build-only box_author, so a renamed key cannot silently drop a
//     row onto a fallback page
//   * pages do not change where a row is saved: the config.ini sections of the
//     moved rows are the ones v0.2.0 wrote
//   * Connection's bespoke text editor receives only strings with real buffers

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

static void ExpectFeedConfig(void)
{
	char dir[] = "/tmp/ctr-feed-config-XXXXXX";
	const int entry = CTR_MenuFindEntry(g_configEntries, g_numConfigEntries, "item_feed");
	EXPECT_TRUE("Item Feed is a three-way enum", entry >= 0 &&
	            g_configEntries[entry].type == CFG_ENUM);
	EXPECT_TRUE("Item Feed defaults to ALL", g_config.itemFeed == AP_ITEM_FEED_ALL);
	const int partial = CTR_MenuFindEntry(g_configEntries, g_numConfigEntries, "tracker_partial_colour");
	EXPECT_TRUE("Tracker Partial Colour is a boolean", partial >= 0 &&
	            g_configEntries[partial].type == CFG_BOOL);
	EXPECT_TRUE("Tracker Partial Colour defaults to OFF", !g_config.trackerPartialColour);
	if (!mkdtemp(dir) || chdir(dir) != 0)
	{
		EXPECT_TRUE("isolated config directory", 0);
		return;
	}
	const char *oldValues[] = { "true", "false", "1", "0" };
	for (int i = 0; i < 4; ++i)
	{
		FILE *f = fopen("config.ini", "w");
		if (!f)
		{
			EXPECT_TRUE("write legacy Item Feed config", 0);
			return;
		}
		fprintf(f, "[Archipelago]\nitem_feed = %s\n", oldValues[i]);
		fclose(f);
		NativeConfig_Load();
		EXPECT_TRUE(oldValues[i], g_config.itemFeed == (i % 2 ? AP_ITEM_FEED_OFF : AP_ITEM_FEED_ALL));
	}
	g_config.itemFeed = AP_ITEM_FEED_IMPORTANT;
	g_config.trackerPartialColour = true;
	NativeConfig_Save();
	g_config.itemFeed = AP_ITEM_FEED_OFF;
	g_config.trackerPartialColour = false;
	NativeConfig_Load();
	EXPECT_TRUE("IMPORTANT survives a config round trip", g_config.itemFeed == AP_ITEM_FEED_IMPORTANT);
	EXPECT_TRUE("Tracker Partial Colour survives a config round trip", g_config.trackerPartialColour);
	unlink("config.ini");
	if (chdir("/") == 0)
		rmdir(dir);
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
		if (strcmp(s_names[p], "Connection") == 0)
			for (int r = 0; r < s_counts[p]; r++)
			{
				const ConfigEntry *e = &g_configEntries[s_rows[p][r]];
				snprintf(label, sizeof label, "Connection row %s has a valid text buffer", e->key);
				EXPECT_TRUE(label, e->type == CFG_STRING && e->valuePtr != NULL && e->max > 1);
			}
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

	EXPECT_TRUE("seven pages in the AP build", numPages == 7);
	if (numPages == 7)
	{
		static const char *const video[] = {
			"fullscreen", "aspect_ratio", "render_scale", "vsync", "smooth_scaling",
			"texture_filtering", "dithering", "increase_draw_distance",
			"disable_split_screen_lod", NULL};
		static const char *const gameplay[] = {
			"skip_intro", "skip_hints", "race_type_selector", "skip_podium", "skip_cutscenes",
			"boss_music", "ai_difficulty", "mute_when_unfocused", "device_aware_labels", "update_check", "tracker_partial_colour", NULL};
		static const char *const controls[] = {NULL};
		static const char *const connection[] = {"uri", "slot", "password", NULL};
		static const char *const archipelago[] = {
			"map_flash", "item_feed", "item_box_colours", "item_box_numbers", "sanity_numbers", "death_link", "dl_send_fall", "dl_send_hit", "dl_send_loss", "trap_duration",
			"discord_status", NULL};
		static const char *const authoring[] = {
			"nav_record", "nav_use_recorded", "nav_repeat_laps", "nav_only_mine", "nav_driver_name", NULL};

		ExpectPage(0, "Video", video);
		ExpectPage(1, "Gameplay", gameplay);
		ExpectPage(2, "Sound", controls);
		ExpectPage(3, "Controls", controls);
		ExpectPage(4, "Connection", connection);
		ExpectPage(5, "Archipelago", archipelago);
		ExpectPage(6, "Authoring", authoring);
	}

	ExpectSection("skip_intro", "Video & QoL");
	ExpectSection("mute_when_unfocused", "Video & QoL");
	ExpectSection("skip_podium", "Video & QoL");
	ExpectSection("skip_cutscenes", "Video & QoL");
	ExpectSection("skip_hints", "Archipelago");
	ExpectSection("race_type_selector", "Archipelago");
	ExpectSection("boss_music", "Archipelago");
	ExpectSection("ai_difficulty", "Archipelago");
	ExpectSection("discord_status", "Archipelago");
	ExpectSection("application_id", "Discord");
	ExpectFeedConfig();

	if (g_failures == 0)
		printf("options pages: all assertions passed (%d pages)\n", numPages);
	return g_failures == 0 ? 0 : 1;
}
