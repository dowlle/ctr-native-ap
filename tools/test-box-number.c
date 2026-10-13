/*
 * Host harness for the native AP box number label and its local option.
 * Build line (parsed by tools/ci/run-harnesses.py):
 */
// cc -m32 -Wall -Wextra -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I ap -I . -I include -o /tmp/test-box-number tools/test-box-number.c -lm && /tmp/test-box-number

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <common.h>
#include <platform/native_config.h>
#include <ctr_menu_pages.h>
#include "../ap/ap_box_number_logic.h"

struct sData sdata_static;
#include "../platform/native_config.c"

static int failures;

static void check(int condition, const char *name)
{
	if (!condition)
	{
		printf("FAIL %s\n", name);
		failures++;
	}
}

int main(void)
{
	const ConfigEntry *entry = NULL;
	char number[3];
	char dir[] = "/tmp/test-box-number-XXXXXX";
	int i;

	check(g_config.itemBoxNumbers, "numbers default on");
	for (i = 0; i < g_numConfigEntries; i++)
		if (strcmp(g_configEntries[i].key, "item_box_numbers") == 0)
			entry = &g_configEntries[i];
	check(entry != NULL, "option is registered");
	if (entry != NULL)
	{
		const char *names[CTR_MENU_PAGE_CAP];
		int rows[CTR_MENU_PAGE_CAP][CTR_MENU_PAGE_ROW_CAP];
		int counts[CTR_MENU_PAGE_CAP];
		int pages = CTR_MenuBuildPages(g_configEntries, g_numConfigEntries,
		                               names, rows, counts);
		int onApPage = 0;
		int page, row;
		for (page = 0; page < pages; page++)
			for (row = 0; row < counts[page]; row++)
				if (&g_configEntries[rows[page][row]] == entry &&
				    strcmp(names[page], "Archipelago") == 0)
					onApPage = 1;
		check(onApPage, "option appears in the AP menu");
		check(strcmp(entry->section, "Archipelago") == 0, "option is on AP page");
		check(strcmp(entry->label, "Item Box Numbers") == 0, "option label");
		check(entry->type == CFG_BOOL && entry->valuePtr == &g_config.itemBoxNumbers,
		      "option controls local bool");
	}

	check(AP_BoxNumber_Text(0, number) && strcmp(number, "1") == 0, "first slot is 1");
	check(AP_BoxNumber_Text(8, number) && strcmp(number, "9") == 0, "ninth slot is 9");
	check(AP_BoxNumber_Text(9, number) && strcmp(number, "10") == 0, "tenth slot is 10");
	check(AP_BoxNumber_Text(14, number) && strcmp(number, "15") == 0, "last slot is 15");
	check(!AP_BoxNumber_Text(-1, number) && !AP_BoxNumber_Text(15, number), "invalid slots hidden");
	check(AP_BoxNumber_Standing(1, 1, 0), "open spawned box is labelled");
	check(!AP_BoxNumber_Standing(1, 1, 1), "checked box is hidden");
	check(!AP_BoxNumber_Standing(1, 0, 0), "box without an instance is hidden");
	check(!AP_BoxNumber_Standing(0, 1, 0), "removed box is hidden");
	check(AP_BoxNumber_LabelWidthForDepth(99) == 0, "near clip");
	check(AP_BoxNumber_LabelWidthForDepth(100) == 13, "near width");
	check(AP_BoxNumber_LabelWidthForDepth(450) == 11, "middle width");
	check(AP_BoxNumber_LabelWidthForDepth(900) == 9, "far width");
	check(AP_BoxNumber_LabelWidthForDepth(1500) == 0, "distance cap");

	if (mkdtemp(dir) == NULL || chdir(dir) != 0)
		return 1;
	g_config.itemBoxNumbers = false;
	NativeConfig_Save();
	g_config.itemBoxNumbers = true;
	NativeConfig_Load();
	check(!g_config.itemBoxNumbers, "off persists");
	g_config.itemBoxNumbers = true;
	NativeConfig_Save();
	g_config.itemBoxNumbers = false;
	NativeConfig_Load();
	check(g_config.itemBoxNumbers, "on persists");

	printf("%s box numbers (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
