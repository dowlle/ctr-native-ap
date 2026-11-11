// Host harness for the "Boss Music" Options row (A4-61): the config.ini
// [Archipelago] boss_music entry exists, points at g_config.bossMusic,
// defaults to BOSS THEME, sits on the Gameplay page, and survives a save and
// load; and the load-time decision in ap/ap_boss_music_logic.h holds: only a
// boss race with the row on BOSS THEME takes the boss banks, TRACK takes the
// venue's own music, and an out-of-range hand edit is the default again.
// platform/native_config.c is compiled in.
//
// Build line (parsed by tools/ci/run-harnesses.py):

// cc -m32 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I ap -I . -I include \
//    -o /tmp/test-boss-music tools/test-boss-music.c -lm && \
//    /tmp/test-boss-music

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <common.h>
#include <platform/native_config.h>
#include <ctr_menu_pages.h>

struct sData sdata_static;

#include "../platform/native_config.c"

#include "../ap/ap_boss_music_logic.h"

static int g_failures;

static void expect(const char *what, int got, int want)
{
	if (got != want)
	{
		printf("FAIL %s: got %d, want %d\n", what, got, want);
		g_failures++;
	}
	else
		printf("ok   %s\n", what);
}

static const ConfigEntry *FindEntry(const char *key)
{
	for (int i = 0; i < g_numConfigEntries; i++)
		if (strcmp(g_configEntries[i].key, key) == 0)
			return &g_configEntries[i];
	return NULL;
}

static int ConfigHas(const char *line)
{
	FILE *f = fopen("config.ini", "r");
	char buf[256];
	int inAp = 0, saw = 0;
	if (f == NULL)
		return 0;
	while (fgets(buf, sizeof buf, f))
	{
		if (buf[0] == '[')
			inAp = strncmp(buf, "[Archipelago]", 13) == 0;
		else if (inAp && strstr(buf, line) != NULL)
			saw = 1;
	}
	fclose(f);
	return saw;
}

int main(void)
{
	expect("default bossMusic is BOSS THEME", g_config.bossMusic, AP_BOSS_MUSIC_BOSS_THEME);

	const ConfigEntry *e = FindEntry("boss_music");
	expect("boss_music entry present", e != NULL, 1);
	if (e == NULL)
		return 1;
	expect("boss_music is CFG_ENUM", e->type == CFG_ENUM, 1);
	expect("boss_music points at g_config.bossMusic", e->valuePtr == &g_config.bossMusic, 1);
	expect("boss_music saved under [Archipelago]", strcmp(e->section, "Archipelago") == 0, 1);
	expect("label is Boss Music", strcmp(e->label, "Boss Music") == 0, 1);

	{
		const char *names[CTR_MENU_PAGE_CAP];
		int rows[CTR_MENU_PAGE_CAP][CTR_MENU_PAGE_ROW_CAP];
		int counts[CTR_MENU_PAGE_CAP];
		int pages = CTR_MenuBuildPages(g_configEntries, g_numConfigEntries, names, rows, counts);
		int onGameplay = 0;
		for (int p = 0; p < pages; p++)
			for (int r = 0; r < counts[p]; r++)
				if (&g_configEntries[rows[p][r]] == e && strcmp(names[p], "Gameplay") == 0)
					onGameplay = 1;
		expect("row is on the Gameplay page", onGameplay, 1);
	}

	// The load-time decision: boss banks only for a boss race on BOSS THEME.
	expect("boss race, BOSS THEME: boss banks", AP_BossMusic_BossBanks(1, AP_BOSS_MUSIC_BOSS_THEME), 1);
	expect("boss race, TRACK: venue music", AP_BossMusic_BossBanks(1, AP_BOSS_MUSIC_TRACK), 0);
	expect("boss race, out-of-range value: boss banks", AP_BossMusic_BossBanks(1, 7), 1);
	expect("ordinary race, BOSS THEME: no boss banks", AP_BossMusic_BossBanks(0, AP_BOSS_MUSIC_BOSS_THEME), 0);
	expect("ordinary race, TRACK: no boss banks", AP_BossMusic_BossBanks(0, AP_BOSS_MUSIC_TRACK), 0);

	char tmpdir[] = "/tmp/test-boss-music-XXXXXX";
	if (mkdtemp(tmpdir) == NULL || chdir(tmpdir) != 0)
	{
		printf("FAIL cannot use a temp dir\n");
		return 1;
	}

	g_config.bossMusic = AP_BOSS_MUSIC_TRACK;
	NativeConfig_Save();
	expect("config.ini has boss_music = 1", ConfigHas("boss_music = 1"), 1);
	g_config.bossMusic = AP_BOSS_MUSIC_BOSS_THEME;
	NativeConfig_Load();
	expect("TRACK survives a load", g_config.bossMusic, AP_BOSS_MUSIC_TRACK);

	g_config.bossMusic = AP_BOSS_MUSIC_BOSS_THEME;
	NativeConfig_Save();
	g_config.bossMusic = AP_BOSS_MUSIC_TRACK;
	NativeConfig_Load();
	expect("BOSS THEME survives a load", g_config.bossMusic, AP_BOSS_MUSIC_BOSS_THEME);

	{
		FILE *f = fopen("config.ini", "w");
		if (f != NULL)
		{
			fputs("[Archipelago]\nmap_flash = true\n", f);
			fclose(f);
		}
		g_config.bossMusic = AP_BOSS_MUSIC_BOSS_THEME; // the startup default
		NativeConfig_Load();
		expect("an older config.ini without the key keeps BOSS THEME", g_config.bossMusic, AP_BOSS_MUSIC_BOSS_THEME);
	}

	printf("%s boss music (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
