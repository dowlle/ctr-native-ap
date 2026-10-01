// Out-of-engine checks for the Options pages of the authoring client without
// Archipelago (CMake -DCTR_AUTHORING_CLIENT=ON), built against the real config
// entry table in platform/native_config.c with that build's defines.
//
//   cc -Wall -Wextra -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DCTR_BOX_AUTHORING -DCTR_AI_LAP_RECORDER -DCTR_AUTHORING_CLIENT -I include -I . -o /tmp/test-authoring-client-config tools/test-authoring-client-config.c && /tmp/test-authoring-client-config
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * the Authoring page carries Box Author Mode and the three AI lap recorder
//     rows, in page order, saved under [Authoring]
//   * every authoring option defaults to off or empty, so a fresh config.ini
//     writes nothing to disk and never opens the editor
//   * no Archipelago row (Connection, Archipelago, AP-only Gameplay rows) is in
//     the table, so the build cannot show a room setting it has no use for
//   * Video and Gameplay keep their vanilla rows

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "platform/native_config.c"
#include "ctr_menu_pages.h"

#if defined(CTR_AP)
#error "this harness is the build without CTR_AP"
#endif

static int g_failures = 0;
static int g_checks = 0;

#define EXPECT_TRUE(name, expr)                         \
	do {                                                \
		g_checks++;                                     \
		if (!(expr)) {                                  \
			printf("FAIL %s\n", (name));                \
			g_failures++;                               \
		}                                               \
	} while (0)

static const char *s_names[CTR_MENU_PAGE_CAP];
static int s_rows[CTR_MENU_PAGE_CAP][CTR_MENU_PAGE_ROW_CAP];
static int s_counts[CTR_MENU_PAGE_CAP];

static int FindPage(int numPages, const char *name)
{
	for (int p = 0; p < numPages; p++)
		if (strcmp(s_names[p], name) == 0)
			return p;
	return -1;
}

int main(void)
{
	static const char *const authoring[] = {
		"box_author", "nav_record", "nav_use_recorded", "nav_driver_name", NULL};
	static const char *const absent[] = {
		"uri", "slot", "password", "skip_hints", "skip_podium", "skip_cutscenes",
		"ai_difficulty", "map_flash", "item_box_colours", "death_link",
		"trap_duration", "update_check", "discord_status", NULL};
	const int numPages = CTR_MenuBuildPages(g_configEntries, g_numConfigEntries,
		s_names, s_rows, s_counts);
	char label[160];

	const int page = FindPage(numPages, "Authoring");
	EXPECT_TRUE("an Authoring page exists", page >= 0);
	if (page >= 0)
	{
		int n = 0;
		while (authoring[n] != NULL)
			n++;
		snprintf(label, sizeof label, "Authoring has %d rows (got %d)", n, s_counts[page]);
		EXPECT_TRUE(label, s_counts[page] == n);
		for (int r = 0; r < n && r < s_counts[page]; r++)
		{
			const ConfigEntry *e = &g_configEntries[s_rows[page][r]];
			snprintf(label, sizeof label, "Authoring row %d is %s", r, authoring[r]);
			EXPECT_TRUE(label, strcmp(e->key, authoring[r]) == 0);
			snprintf(label, sizeof label, "%s is saved under [Authoring]", authoring[r]);
			EXPECT_TRUE(label, strcmp(e->section, "Authoring") == 0);
		}
	}

	EXPECT_TRUE("Box Author Mode defaults to off", g_config.boxAuthor == false);
	EXPECT_TRUE("Save AI Lap Recordings defaults to off", g_config.navRecord == false);
	EXPECT_TRUE("Use Recorded AI Laps defaults to off", g_config.navUseRecorded == false);
	EXPECT_TRUE("Driver Name defaults to empty", g_config.navDriverName[0] == '\0');

	for (const char *const *k = absent; *k != NULL; k++)
	{
		snprintf(label, sizeof label, "AP row %s is not in this build", *k);
		EXPECT_TRUE(label, CTR_MenuFindEntry(g_configEntries, g_numConfigEntries, *k) < 0);
	}
	EXPECT_TRUE("no Connection page", FindPage(numPages, "Connection") < 0);
	EXPECT_TRUE("no Archipelago page", FindPage(numPages, "Archipelago") < 0);
	EXPECT_TRUE("Video page present", FindPage(numPages, "Video") >= 0);
	EXPECT_TRUE("Gameplay page present", FindPage(numPages, "Gameplay") >= 0);

	for (int p = 0; p < numPages; p++)
	{
		snprintf(label, sizeof label, "%s fits the panel (%d rows)", s_names[p], s_counts[p]);
		EXPECT_TRUE(label, s_counts[p] <= CTR_MENU_PAGE_VISIBLE_ROWS);
	}

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
