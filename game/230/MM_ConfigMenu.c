#include <common.h>
#include <stdio.h>

// In-game options menu. Ported from thecodingbob/ctr-native (branch
// modularize-improve-config): a two-level RectMenu proc -- a section selector,
// then the entries within the chosen section. Reads/writes g_config; persists to
// config.ini on exit via NativeConfig_Save. The row arrays below extend the main
// menu with an OPTIONS row (string 0x0E == LNG_OPTIONS) and are pointed at by
// MM_MenuProc_Main.

// Row arrays with CONFIG entry at bottom, used by MM_MenuProc_Main
struct MenuRow s_rowsMainMenuBasicConfig[] = {
	{0x4C, 0, 1, 0, 0},
	{0x4D, 0, 2, 1, 1},
	{0x4E, 1, 3, 2, 2},
	{0x4F, 2, 4, 3, 3},
	{0x50, 3, 5, 4, 4},
	{0x51, 4, 6, 5, 5},
	{0x0E, 5, 6, 6, 6},
	{-1},
};

struct MenuRow s_rowsMainMenuWithSBConfig[] = {
	{0x4C, 0, 1, 0, 0},
	{0x4D, 0, 2, 1, 1},
	{0x4E, 1, 3, 2, 2},
	{0x4F, 2, 4, 3, 3},
	{0x50, 3, 5, 4, 4},
	{0x51, 4, 6, 5, 5},
	{0x234, 5, 7, 6, 6},
	{0x0E, 6, 7, 7, 7},
	{-1},
};

static void MM_MenuProc_Config(struct RectMenu *menu);

// Section lookup built from g_configEntries at first use
static int s_sectionToEntry[16];
static int s_sectionCount[16];
static int s_numSections = 0;

static void BuildSectionMap(void)
{
	s_numSections = 0;
	const char *curSection = NULL;
	for (int i = 0; i < g_numConfigEntries; i++)
	{
		if (curSection == NULL || strcmp(g_configEntries[i].section, curSection) != 0)
		{
			curSection = g_configEntries[i].section;
			s_sectionToEntry[s_numSections] = i;
			s_sectionCount[s_numSections] = 0;
			s_numSections++;
		}
		s_sectionCount[s_numSections - 1]++;
	}
}

static int s_currentSection = -1; // -1 = section selector, 0+ = submenu

struct RectMenu g_configMenu = {
	.stringIndexTitle = -1,
	.state = EXECUTE_FUNCPTR | DISABLE_INPUT_ALLOW_FUNCPTRS,
	.funcPtr = MM_MenuProc_Config,
};

static void Config_UpdateSlider(const struct GamepadBuffer *pad, const int rowSelected,
                              const int localRow, int *value, const int min, const int max, const int step)
{
	if (rowSelected != localRow)
		return;
	const int held = pad->buttonsHeldCurrFrame;
	if ((held & BTN_LEFT) != 0 && (sdata->frameCounter % 3) == 0)
	{
		*value -= step;
		if (*value < min) *value = min;
	}
	if ((held & BTN_RIGHT) != 0 && (sdata->frameCounter % 3) == 0)
	{
		*value += step;
		if (*value > max) *value = max;
	}
}

static void Config_DrawValue(const ConfigEntry *e, const int valueX, int y, uint32_t *ot, char *buf)
{
	if (e->type == CFG_BOOL)
	{
		DecalFont_DrawLineOT(*(bool *)e->valuePtr ? "ON" : "OFF",
			valueX, y, FONT_SMALL, JUSTIFY_RIGHT | WHITE, ot);
	}
	else
	{
		sprintf(buf, "%d%%", *(int *)e->valuePtr);
		DecalFont_DrawLineOT(buf, valueX, y, FONT_SMALL, JUSTIFY_RIGHT | WHITE, ot);
	}
}

#ifdef CTR_AP
// ── Archipelago connection manager ──────────────────────────────────────────
// The "Connection" section is not a plain toggle/slider group: it has three
// editable text rows (Server / Slot / Password), a Connect action row, and a
// read-only status row. It gets its own submenu proc (MM_ConfigProc_Connection)
// so the generic section renderer above stays untouched. The three CFG_STRING
// entries are the section's rows in g_configEntries (before Archipelago); the
// Connect + Status rows are appended here and are not config entries.
//
// Selecting a text row starts a NativeText session (platform keyboard capture,
// see platform/native_platform.c). While it is active the platform layer owns the
// keyboard and this proc suppresses its own navigation; Enter commits + saves,
// Escape restores the pre-edit value.

static int  s_connEditing = 0;   // 1 while a text row is being edited
static int  s_connEditRow = 0;   // which string row (0..2) is being edited
static char s_connBackup[128];   // pre-edit value, restored on cancel

// Render a CFG_STRING value into out: masked (one '*' per char) for the password,
// plain otherwise, with a blinking trailing cursor while this row is being edited.
static void Conn_FormatValue(const ConfigEntry *e, int masked, int editing, char *out, int outCap)
{
	const char *src = (const char *)e->valuePtr;
	int n = 0;

	if (src[0] == '\0' && !editing)
	{
		out[0] = '-';
		out[1] = '\0';
		return;
	}

	while (src[n] != '\0' && n < outCap - 2)
	{
		out[n] = masked ? '*' : src[n];
		n++;
	}
	if (editing && (sdata->frameCounter & 0x10)) // ~2 Hz blink
		out[n++] = '_';
	out[n] = '\0';
}

static void MM_ConfigProc_Connection(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
	char buf[160];
	const int firstEntry = s_sectionToEntry[s_currentSection];
	const int numStrings = s_sectionCount[s_currentSection]; // uri / slot / password
	const int numRows = numStrings + 1;                      // + Connect action row

	// Resolve a finished edit first. The platform layer keeps the session active
	// (NativeText_Active == 1) until we call NativeText_End here, so the commit /
	// cancel frame still reads as "editing" to the parent proc's back handler --
	// avoiding a one-frame nav race (pad state is polled from the live keyboard).
	int justResolved = 0;
	if (s_connEditing && NativeText_Result() != 0)
	{
		const ConfigEntry *e = &g_configEntries[firstEntry + s_connEditRow];
		if (NativeText_Result() == 2) // Escape -> restore the pre-edit value
		{
			strncpy((char *)e->valuePtr, s_connBackup, e->max - 1);
			((char *)e->valuePtr)[e->max - 1] = '\0';
		}
		else // Enter -> persist the edited value
		{
			NativeConfig_Save();
		}
		NativeText_End();
		s_connEditing = 0;
		justResolved = 1;
	}

	// While editing (or on the frame we just resolved) all menu navigation is
	// suppressed: text mode owns input.
	if (!s_connEditing && !justResolved)
	{
		if ((pad->buttonsTapped & BTN_UP) != 0)
		{
			menu->rowSelected = (menu->rowSelected > 0) ? menu->rowSelected - 1 : numRows - 1;
			OtherFX_Play(0, 1);
		}
		if ((pad->buttonsTapped & BTN_DOWN) != 0)
		{
			menu->rowSelected = (menu->rowSelected < numRows - 1) ? menu->rowSelected + 1 : 0;
			OtherFX_Play(0, 1);
		}
		if ((pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE)) != 0)
		{
			OtherFX_Play(1, 1);
			if (menu->rowSelected < numStrings)
			{
				// Enter edit mode: back up the current value, hand the buffer to the
				// platform keyboard capture (editing continues from the current text).
				const ConfigEntry *e = &g_configEntries[firstEntry + menu->rowSelected];
				s_connEditRow = menu->rowSelected;
				strncpy(s_connBackup, (char *)e->valuePtr, sizeof s_connBackup - 1);
				s_connBackup[sizeof s_connBackup - 1] = '\0';
				NativeText_Begin((char *)e->valuePtr, e->max);
				s_connEditing = 1;
			}
			else
			{
				// Connect action: re-dial with the saved connection settings.
				AP_Net_Reconnect(g_config.uri, g_config.slot, g_config.password);
			}
		}
	}

	DecalFont_DrawLineOT((char *)g_configEntries[firstEntry].section,
		0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);

	int labelX = 0x38;
	int valueX = 0xB0;  // text values are left-justified (they grow rightward as typed)
	int startY = 0x3C;
	int rowSpacing = 0x0E;

	for (int j = 0; j < numStrings; j++)
	{
		const ConfigEntry *e = &g_configEntries[firstEntry + j];
		int y = startY + j * rowSpacing;
		int masked = (strcmp(e->key, "password") == 0);
		int editing = (s_connEditing && s_connEditRow == j);

		DecalFont_DrawLineOT((char *)e->label, labelX, y, FONT_SMALL, ORANGE, ot);
		Conn_FormatValue(e, masked, editing, buf, sizeof buf);
		DecalFont_DrawLineOT(buf, valueX, y, FONT_SMALL, WHITE, ot);

		if (j == menu->rowSelected)
		{
			RECT sel = {0x30, y - 2, 0x1B0, 0x0C};
			CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
		}
	}

	// Connect action row.
	{
		int y = startY + numStrings * rowSpacing;
		DecalFont_DrawLineOT("Connect", labelX, y, FONT_SMALL, ORANGE, ot);
		if (menu->rowSelected == numStrings)
		{
			RECT sel = {0x30, y - 2, 0x1B0, 0x0C};
			CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
		}
	}

	// Read-only status row (one line below the Connect row).
	{
		int y = startY + (numStrings + 2) * rowSpacing;
		DecalFont_DrawLineOT("Status", labelX, y, FONT_SMALL, ORANGE, ot);
		DecalFont_DrawLineOT((char *)AP_Net_StatusLine(), valueX, y, FONT_SMALL, WHITE, ot);
	}
}
#endif // CTR_AP

static void MM_MenuProc_Config(struct RectMenu *menu)
{
	struct GameTracker *gGT = sdata->gGT;
	uint32_t *ot = gGT->backBuffer->otMem.uiOT;
	struct GamepadBuffer *pad = &sdata->gGamepads->gamepad[0];
	char buf[32];

	if (s_numSections == 0)
		BuildSectionMap();

	// Back / exit -- but not while a text-entry session owns the keyboard (there
	// Escape is the cancel key, handled in the platform layer). NativeText_Active
	// is always 0 outside an edit, so this is a no-op change for the toggle sections.
	if ((pad->buttonsTapped & (BTN_TRIANGLE | BTN_START)) != 0 && !NativeText_Active())
	{
		OtherFX_Play(2, 1);
		if (s_currentSection >= 0)
		{
			menu->rowSelected = s_currentSection;
			s_currentSection = -1;
		}
		else
		{
			NativeConfig_Save();
#ifdef CTR_AP
			// Persist the AI-difficulty value to the per-slot data-storage override
			// (no-op if not connected), alongside the config.ini write above.
			AP_AiDifficultyCommit();
#endif
			sdata->ptrDesiredMenu = &D230.menuMainMenu;
		}
	}

	if (s_currentSection >= 0)
	{
		const int sec = s_currentSection;
		const int numRows = s_sectionCount[sec];
		const int firstEntry = s_sectionToEntry[sec];

#ifdef CTR_AP
		// The Connection section has bespoke text-edit / connect behaviour.
		if (strcmp(g_configEntries[firstEntry].section, "Connection") == 0)
		{
			MM_ConfigProc_Connection(menu, ot, pad);
		}
		else
		{
#endif

		if ((pad->buttonsTapped & BTN_UP) != 0)
		{
			menu->rowSelected = (menu->rowSelected > 0) ? menu->rowSelected - 1 : numRows - 1;
			OtherFX_Play(0, 1);
		}
		if ((pad->buttonsTapped & BTN_DOWN) != 0)
		{
			menu->rowSelected = (menu->rowSelected < numRows - 1) ? menu->rowSelected + 1 : 0;
			OtherFX_Play(0, 1);
		}

		if ((pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE)) != 0)
		{
			OtherFX_Play(1, 1);
			const ConfigEntry *e = &g_configEntries[firstEntry + menu->rowSelected];
			if (e->type == CFG_BOOL)
				*(bool *)e->valuePtr ^= 1;
		}

		// slider update for int entries
		for (int j = 0; j < numRows; j++)
		{
			const ConfigEntry *e = &g_configEntries[firstEntry + j];
			if (e->type == CFG_INT)
				Config_UpdateSlider(pad, menu->rowSelected, j, (int *)e->valuePtr, e->min, e->max, e->step);
		}

		DecalFont_DrawLineOT((char *)g_configEntries[firstEntry].section,
			0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);

		int labelX = 0x38;
		int valueX = 0x1DC;
		int startY = 0x3C;
		int rowSpacing = 0x0E;

		for (int j = 0; j < numRows; j++)
		{
			const ConfigEntry *e = &g_configEntries[firstEntry + j];
			int y = startY + j * rowSpacing;

			DecalFont_DrawLineOT((char *)e->label, labelX, y, FONT_SMALL, ORANGE, ot);
			Config_DrawValue(e, valueX, y, ot, buf);

			if (j == menu->rowSelected)
			{
				RECT sel = {0x30, y - 2, 0x1B0, 0x0C};
				CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
			}
		}
#ifdef CTR_AP
		} // end generic (non-Connection) section
#endif
	}
	else
	{
		if ((pad->buttonsTapped & BTN_UP) != 0)
		{
			menu->rowSelected = (menu->rowSelected > 0) ? menu->rowSelected - 1 : s_numSections - 1;
			OtherFX_Play(0, 1);
		}
		if ((pad->buttonsTapped & BTN_DOWN) != 0)
		{
			menu->rowSelected = (menu->rowSelected < s_numSections - 1) ? menu->rowSelected + 1 : 0;
			OtherFX_Play(0, 1);
		}

		if ((pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE)) != 0)
		{
			OtherFX_Play(1, 1);
			s_currentSection = menu->rowSelected;
			menu->rowSelected = 0;
		}

		DecalFont_DrawLineOT(sdata->lngStrings[LNG_OPTIONS],
			0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);

		int labelX = 0x38;
		int startY = 0x3C;
		int spacing = 0x0E;

		for (int i = 0; i < s_numSections; i++)
		{
			const ConfigEntry *e = &g_configEntries[s_sectionToEntry[i]];
			int y = startY + i * spacing;
			DecalFont_DrawLineOT((char *)e->section, labelX, y, FONT_SMALL, ORANGE, ot);
			if (i == menu->rowSelected)
			{
				RECT sel = {0x30, y - 2, 0x1B0, 0x0C};
				CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
			}
		}
	}

	{
		RECT sep = {0x20, 0x2C, 0x1C0, 2};
		Color sepColor;
		sepColor.self = sdata->battleSetup_Color_UI_1;
		RECTMENU_DrawOuterRect_Edge(&sep, sepColor, 0x20, ot);
	}

	RECT bg = {0x10, 4, 0x1E0, 0xCE};
	RECTMENU_DrawInnerRect(&bg, 4, ot);
}
