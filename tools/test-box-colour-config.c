/*
 * Host harness for the "Item Box Colours" Options row (config.ini
 * [Archipelago] item_box_colours): the entry exists, points at
 * g_config.itemBoxColours, defaults on, sits on the Archipelago page, and
 * survives a save and load. platform/native_config.c is compiled in.
 *
 * Build line (parsed by tools/ci/run-harnesses.py):
 */

// cc -m32 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I ap -I . -I include \
//    -o /tmp/test-box-colour-config tools/test-box-colour-config.c -lm && \
//    /tmp/test-box-colour-config

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <common.h>
#include <platform/native_config.h>
#include <ctr_menu_pages.h>

struct sData sdata_static;

#include "../platform/native_config.c"

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
	expect("default itemBoxColours is on", g_config.itemBoxColours ? 1 : 0, 1);

	const ConfigEntry *e = FindEntry("item_box_colours");
	expect("item_box_colours entry present", e != NULL, 1);
	if (e == NULL)
		return 1;
	expect("item_box_colours is CFG_BOOL", e->type == CFG_BOOL, 1);
	expect("item_box_colours points at g_config.itemBoxColours", e->valuePtr == &g_config.itemBoxColours, 1);
	expect("item_box_colours saved under [Archipelago]", strcmp(e->section, "Archipelago") == 0, 1);
	expect("label is Item Box Colours", strcmp(e->label, "Item Box Colours") == 0, 1);

	{
		const char *names[CTR_MENU_PAGE_CAP];
		int rows[CTR_MENU_PAGE_CAP][CTR_MENU_PAGE_ROW_CAP];
		int counts[CTR_MENU_PAGE_CAP];
		int pages = CTR_MenuBuildPages(g_configEntries, g_numConfigEntries, names, rows, counts);
		int onAp = 0;
		for (int p = 0; p < pages; p++)
			for (int r = 0; r < counts[p]; r++)
				if (&g_configEntries[rows[p][r]] == e && strcmp(names[p], "Archipelago") == 0)
					onAp = 1;
		expect("row is on the Archipelago page", onAp, 1);
	}

	char tmpdir[] = "/tmp/test-box-colour-config-XXXXXX";
	if (mkdtemp(tmpdir) == NULL || chdir(tmpdir) != 0)
	{
		printf("FAIL cannot use a temp dir\n");
		return 1;
	}

	g_config.itemBoxColours = false;
	NativeConfig_Save();
	expect("config.ini has item_box_colours = false", ConfigHas("item_box_colours = false"), 1);
	g_config.itemBoxColours = true;
	NativeConfig_Load();
	expect("off survives a load", g_config.itemBoxColours ? 1 : 0, 0);

	g_config.itemBoxColours = true;
	NativeConfig_Save();
	g_config.itemBoxColours = false;
	NativeConfig_Load();
	expect("on survives a load", g_config.itemBoxColours ? 1 : 0, 1);

	{
		FILE *f = fopen("config.ini", "w");
		if (f != NULL)
		{
			fputs("[Archipelago]\nmap_flash = true\n", f);
			fclose(f);
		}
		g_config.itemBoxColours = true;
		NativeConfig_Load();
		expect("an older config.ini without the key keeps colours on", g_config.itemBoxColours ? 1 : 0, 1);
	}

	printf("%s box colour config (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
