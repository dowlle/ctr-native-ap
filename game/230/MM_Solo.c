/* Play Solo (#89): Options > Connection > Play Solo, and the title screen's
   PLAY SOLO row (decision 30), which opens the same pages.
   Included by MM_ConfigMenu.c in the AP player client (CTR_AP, not
   CTR_AP_AUTHORING), ahead of MM_ConfigProc_Connection.

   Pages:
     Home     your solo seeds (Cross resumes, Square opens Details), then
              New solo seed, Import room file, Import YAML file, and the
              AP-Pie YAML builder. Triangle leaves Play Solo.
     New      bundled presets, saved YAMLs, then YAMLs in solo/inbox.
     Options  every option of the world (alpha2), in groups. The first level
              lists the groups with how many options each has changed from
              the preset; Cross opens one and Triangle returns. Inside a
              group Left / Right change a value, Square shows the help text. A row is one line when the full name and
              value fit, otherwise the value takes a second line, right
              aligned: names and values are never cut (Deck menu scan,
              2026-11-03). DeathLink is off in solo and has no row.
     Tracks   installed custom tracks for a preset (decision 32) or an
              imported or inbox YAML (round 2 decision 5), shown only
              when there are any: Cross cycles off, in the pool, own pad.
              An imported track without laps asks for them first: Left /
              Right pick 1 to 7, Cross keeps them (ruling of 2026-11-02).
     Review   slot name (Cross edits it) and Generate.
     Busy     a runtime job with its phase; Circle cancels generation.
     Session  starting, connecting, connected or the reason it failed.
   The runtime, jobs and session live in ap/ap_solo.cpp; the option rules in
   ap/ap_solo_logic.hpp.

   Layout (512 x 216, FONT_SMALL advances 13 pixels): every text stays between
   SOLO_EDGE_L and SOLO_EDGE_R, inside the menu frame. The list holds nine lines
   from SOLO_LIST_Y, two help lines follow at SOLO_HELP_Y and the footer at
   SOLO_FOOTER_Y. A dimmed panel sits behind the list and help, as on the Track
   Manager, so the logo does not show through. tools/test-solo-layout.c checks
   the bounds. */

#define SOLO_ROWS 9
#define SOLO_LIST_Y 0x38
#define SOLO_ROW_H 0x0C
#define SOLO_EDGE_L 0x18
#define SOLO_EDGE_R 0x1E8
#define SOLO_TEXT_W (SOLO_EDGE_R - SOLO_EDGE_L) /* centred lines */
#define SOLO_ROW_X 0x38
#define SOLO_ROW_R 0x1C8                         /* right edge of a row's value */
#define SOLO_ROW_W (SOLO_ROW_R - SOLO_ROW_X)
#define SOLO_HELP_Y 0xA8
#define SOLO_FOOTER_Y 0xC4
#define SOLO_AP_PIE_URL "https://ap-pie.com/yaml-builder/ctr"

enum SoloPage
{
	SOLO_OFF = 0,
	SOLO_HOME,
	SOLO_DETAILS,
	SOLO_NEW,
	SOLO_OPTIONS,
	SOLO_TRACKS,
	SOLO_HELP,
	SOLO_REVIEW,
	SOLO_BUSY,
	SOLO_SESSION,
	SOLO_MESSAGE,
};

enum SoloBusyJob
{
	SOLO_JOB_LIST = 0,
	SOLO_JOB_DESCRIBE,
	SOLO_JOB_GENERATE,
	SOLO_JOB_IMPORT_ROOM,
	SOLO_JOB_IMPORT_YAML,
	SOLO_JOB_TRACKS,
};

static int s_soloPage;
static int s_soloSel;
static int s_soloTop;
static int s_soloBusyJob;
static int s_soloDeleteArmed;
static int s_soloFromOptions;   /* the Review page came from the Options page */
static int s_soloFromTracks;    /* ... by way of the Tracks page */
static int s_soloHighlight = -1; /* option row to highlight after an option error */
static int s_soloHelpRow;
static int s_soloHelpTop;       /* first help line shown (Up / Down scroll) */
static int s_soloGroup = -1;    /* open option group, -1 the group list */
static int s_soloGroupSel;      /* the group list's selection, kept while a group is open */
static int s_soloGroupTop;
static int s_soloEditing;
static int s_soloEditJustEnded;  /* the frame an edit resolved: no navigation */
static int s_soloPadPrev;
static struct APSoloRoomInfo s_soloRoom;
static struct APSoloStartInfo s_soloStart;
static char s_soloName[64];
static char s_soloNameBackup[64];
static char s_soloMsg[256];

static int s_soloPickKind;       /* SOLO_JOB_IMPORT_ROOM or SOLO_JOB_IMPORT_YAML */
static int s_soloLapsFailed;     /* the Tracks page could not keep a track's laps */

/* Word-wrapped FONT_SMALL text: lines `skip` to `skip + lines - 1` are drawn
   (none when `lines` is 0, which only counts); with `ellipsis`, text that does not fit ends the
   last drawn line with "...". `flags` is the colour, optionally
   with JUSTIFY_CENTER (x is then the centre). Returns how many lines the whole
   text wraps to. */
static int MM_Solo_WrapLines(const char *text, int x, int y, int width, int lines, int skip, int ellipsis, int flags,
                              uint32_t *ot)
{
	char line[200];
	const char *cursor = text;
	int i;
	for (i = 0; *cursor; i++)
	{
		size_t n = 0, cut = 0;
		while (cursor[n] && cursor[n] != '\n' && n + 4 < sizeof line)
		{
			line[n] = cursor[n];
			line[n + 1] = '\0';
			if (DecalFont_GetLineWidth(line, FONT_SMALL) > width) break;
			if (cursor[n] == ' ') cut = n;
			n++;
		}
		/* Break at the last space, unless that leaves most of the line empty
		   (a long file name): then break inside the word. */
		if (cursor[n] && cursor[n] != '\n' && cut > 0 && cut >= n / 2) n = cut;
		if (n == 0 && *cursor != '\n' && *cursor != ' ') n = 1; /* a glyph wider than the line still advances */
		line[n] = '\0';
		cursor += n;
		while (*cursor == ' ' || *cursor == '\n') cursor++;
		if (lines > 0 && i >= skip && i < skip + lines)
		{
			if (ellipsis && *cursor && i + 1 == skip + lines)
			{
				for (;;)
				{
					memcpy(line + n, "...", 4);
					if (DecalFont_GetLineWidth(line, FONT_SMALL) <= width || n == 0) break;
					n--;
				}
			}
			DecalFont_DrawLineOT(line, x, y + (i - skip) * SOLO_ROW_H, FONT_SMALL, flags, ot);
		}
	}
	return i;
}

static void MM_Solo_DrawWrapped(const char *text, int x, int y, int width, int lines, int flags, uint32_t *ot)
{
	MM_Solo_WrapLines(text, x, y, width, lines, 0, 1, flags, ot);
}

static void MM_Solo_FitFont(const char *text, char *out, size_t size, int width, int font)
{
	size_t n;
	snprintf(out, size, "%s", text);
	n = strlen(out);
	if (DecalFont_GetLineWidth(out, font) <= width) return;
	while (n > 0)
	{
		n--;
		if (n + 4 > size) continue;
		memcpy(out + n, "...", 4);
		if (DecalFont_GetLineWidth(out, font) <= width) return;
	}
	out[0] = '\0';
}

static void MM_Solo_Fit(const char *text, char *out, size_t size, int width)
{
	MM_Solo_FitFont(text, out, size, width, FONT_SMALL);
}

/* `prefix` then as much of the end of `text` as fits, "..." in front of a
   shortened end: a path keeps its last folders (the inbox line). */
static void MM_Solo_FitLeft(const char *prefix, const char *text, char *out, size_t size, int width)
{
	const char *tail = text;
	snprintf(out, size, "%s%s", prefix, text);
	while (*tail && DecalFont_GetLineWidth(out, FONT_SMALL) > width)
	{
		tail++;
		snprintf(out, size, "%s...%s", prefix, tail);
	}
}

/* The page title in the big font; a name too long for it (an option's help
   page) is drawn in the small font instead of being cut. */
static void MM_Solo_Title(const char *title, uint32_t *ot)
{
	char fit[160];
	if (DecalFont_GetLineWidth((char *)title, FONT_BIG) <= SOLO_TEXT_W)
	{
		DecalFont_DrawLineOT((char *)title, 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
		return;
	}
	MM_Solo_Fit(title, fit, sizeof fit, SOLO_TEXT_W);
	DecalFont_DrawLineOT(fit, 0x100, 0x1C, FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
}

static void MM_Solo_Footer(const char *hint, uint32_t *ot)
{
	char fit[160];
	MM_Solo_Fit(hint, fit, sizeof fit, SOLO_TEXT_W);
	DecalFont_DrawLineOT(fit, 0x100, SOLO_FOOTER_Y, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
}

/* Up to two centred help lines above the footer. */
static void MM_Solo_Help(const char *text, int flags, uint32_t *ot)
{
	MM_Solo_DrawWrapped(text, 0x100, SOLO_HELP_Y, SOLO_TEXT_W, 2, JUSTIFY_CENTER | flags, ot);
}

static void MM_Solo_HighlightLines(int y, int lines, uint32_t *ot)
{
	RECT sel = {0x30, y - 2, 0x1A0, SOLO_ROW_H * lines};
	CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
}

static void MM_Solo_Highlight(int y, uint32_t *ot)
{
	MM_Solo_HighlightLines(y, 1, ot);
}

/* The dimmed panel behind the list and help lines, drawn after the text so it
   sits behind it (Track Manager does the same). */
static void MM_Solo_Panel(uint32_t *ot)
{
	RECT panel = {0x14, 0x32, 0x1D8, 0x8E};
	RECTMENU_DrawInnerRect(&panel, 0, ot);
}

static int s_soloRowsShown = SOLO_ROWS; /* list rows of the current page */

/* Up / Down over `count` rows, keeping the selection in view. */
static void MM_Solo_Navigate(struct GamepadBuffer *pad, int count)
{
	if (count <= 0)
	{
		s_soloSel = 0;
		return;
	}
	if ((pad->buttonsTapped & BTN_UP) != 0)
	{
		s_soloSel = s_soloSel > 0 ? s_soloSel - 1 : count - 1;
		s_soloDeleteArmed = 0;
		OtherFX_Play(0, 1);
	}
	if ((pad->buttonsTapped & BTN_DOWN) != 0)
	{
		s_soloSel = s_soloSel < count - 1 ? s_soloSel + 1 : 0;
		s_soloDeleteArmed = 0;
		OtherFX_Play(0, 1);
	}
	if (s_soloSel >= count) s_soloSel = count - 1;
	if (s_soloSel < s_soloTop) s_soloTop = s_soloSel;
	if (s_soloSel >= s_soloTop + s_soloRowsShown) s_soloTop = s_soloSel - s_soloRowsShown + 1;
}

static void MM_Solo_Go(int page)
{
	/* A message that was to return to Options or Custom Tracks is gone once
	   any page is chosen another way (for example Back to the seed list). */
	if (s_soloFromOptions > 1)
		s_soloFromOptions = 1;
	s_soloPage = page;
	s_soloSel = 0;
	s_soloTop = 0;
	s_soloDeleteArmed = 0;
	if (page == SOLO_OPTIONS)
		s_soloGroup = -1;
}

static void MM_Solo_Message(const char *text)
{
	snprintf(s_soloMsg, sizeof s_soloMsg, "%s", text);
	MM_Solo_Go(SOLO_MESSAGE);
}

static void MM_Solo_Busy(int job)
{
	s_soloBusyJob = job;
	s_soloPage = SOLO_BUSY;
}

static void MM_Solo_Refresh(void)
{
	if (AP_Solo_ListStart())
		MM_Solo_Busy(SOLO_JOB_LIST);
	else
		MM_Solo_Go(SOLO_HOME);
}

/* Back closes Play Solo's own pages before leaving the Connection page. */
static int MM_Solo_ConsumesBack(void)
{
	return s_soloPage != SOLO_OFF;
}

static void MM_Solo_Open(void)
{
	if (!AP_Solo_RuntimePresent())
	{
		MM_Solo_Message("Solo runtime is missing. Reinstall or update the client.");
		return;
	}
	if (s_soloName[0] == '\0')
		snprintf(s_soloName, sizeof s_soloName, "%s", g_config.slot[0] ? g_config.slot : "Player");
	MM_Solo_Refresh();
}

static void MM_Solo_Pick(int kind)
{
	if (AP_Solo_PickStart(kind == SOLO_JOB_IMPORT_ROOM))
		s_soloPickKind = kind;
}

static void MM_Solo_PollPick(void)
{
	char path[1024];
	const int took = AP_Solo_PickTake(path, sizeof path);
	if (took == -2)
		MM_Solo_Message("The path of that file is too long. Move it to a shorter folder, or use the inbox.");
	if (took != 1)
		return;
	if (s_soloPickKind == SOLO_JOB_IMPORT_ROOM ? AP_Solo_ImportRoomStart(path) : AP_Solo_ImportYamlStart(path))
		MM_Solo_Busy(s_soloPickKind);
}

static void MM_Solo_StartSession(const char *roomId)
{
	AP_Solo_SessionStart(roomId);
	MM_Solo_Go(SOLO_SESSION);
}

/* ---- pages ---- */

#define SOLO_HOME_ACTIONS 4
static const char *const s_soloHomeActions[SOLO_HOME_ACTIONS] = {
	"New solo seed",
	"Import room file",
	"Import YAML file",
	"Build a YAML on AP-Pie",
};

static void MM_Solo_PageHome(uint32_t *ot, struct GamepadBuffer *pad)
{
	const int rooms = AP_Solo_RoomCount();
	const int stopRow = AP_Solo_SessionActive() ? 1 : 0;
	const int count = stopRow + rooms + SOLO_HOME_ACTIONS;
	char line[160], fit[160];
	int i;

	const int listY = SOLO_LIST_Y + (rooms == 0 ? SOLO_ROW_H : 0); /* below "No solo seeds yet." */

	s_soloRowsShown = rooms == 0 ? SOLO_ROWS - 1 : SOLO_ROWS;
	MM_Solo_Navigate(pad, count);
	if ((pad->buttonsTapped & BTN_CROSS) != 0)
	{
		const int row = s_soloSel - stopRow;
		OtherFX_Play(1, 1);
		if (stopRow && s_soloSel == 0)
			AP_Solo_SessionStop();
		else if (row < rooms)
		{
			if (AP_Solo_RoomGet(row, &s_soloRoom))
				MM_Solo_StartSession(s_soloRoom.roomId);
		}
		else if (row == rooms)
			MM_Solo_Go(SOLO_NEW);
		else if (row == rooms + 1)
			MM_Solo_Pick(SOLO_JOB_IMPORT_ROOM);
		else if (row == rooms + 2)
			MM_Solo_Pick(SOLO_JOB_IMPORT_YAML);
		else
			Platform_OpenURL(SOLO_AP_PIE_URL);
		return;
	}
	if ((pad->buttonsTapped & BTN_SQUARE) != 0 && s_soloSel >= stopRow && s_soloSel - stopRow < rooms)
	{
		if (AP_Solo_RoomGet(s_soloSel - stopRow, &s_soloRoom))
		{
			OtherFX_Play(1, 1);
			MM_Solo_Go(SOLO_DETAILS);
		}
		return;
	}

	MM_Solo_Title("Play Solo", ot);
	if (rooms == 0)
		DecalFont_DrawLineOT("No solo seeds yet.", SOLO_ROW_X, SOLO_LIST_Y, FONT_SMALL, GRAY, ot);
	for (i = s_soloTop; i < count && i < s_soloTop + s_soloRowsShown; i++)
	{
		const int y = listY + (i - s_soloTop) * SOLO_ROW_H;
		const int row = i - stopRow;
		int color = ORANGE;
		if (stopRow && i == 0)
		{
			snprintf(line, sizeof line, "Stop the solo session");
			color = WHITE;
		}
		else if (row < rooms)
		{
			struct APSoloRoomInfo r;
			AP_Solo_RoomGet(row, &r);
			if (r.checksDone >= 0)
				snprintf(line, sizeof line, "%s  %d/%d  %s%s", r.slot, r.checksDone, r.checksTotal, r.created,
				         r.versionMatches ? "" : "  OLD VERSION");
			else
				snprintf(line, sizeof line, "%s  %s", r.slot, r.created);
			color = r.versionMatches ? WHITE : RED;
			if (AP_Solo_SessionActive() && strcmp(AP_Solo_SessionRoom(), r.roomId) == 0)
				color = ORANGE;
		}
		else
			snprintf(line, sizeof line, "%s", s_soloHomeActions[row - rooms]);
		MM_Solo_Fit(line, fit, sizeof fit, SOLO_ROW_W);
		DecalFont_DrawLineOT(fit, SOLO_ROW_X, y, FONT_SMALL, color, ot);
		if (i == s_soloSel)
			MM_Solo_Highlight(y, ot);
	}
	if (AP_Solo_InboxPath()[0])
	{
		/* A long path keeps its end: "Inbox: ...folder/solo/inbox". */
		MM_Solo_FitLeft("Inbox: ", AP_Solo_InboxPath(), fit, sizeof fit, SOLO_TEXT_W);
		DecalFont_DrawLineOT(fit, 0x100, SOLO_HELP_Y + SOLO_ROW_H, FONT_SMALL, JUSTIFY_CENTER | GRAY, ot);
	}
	MM_Solo_Footer("*: PLAY   [: DETAILS   ^: BACK", ot);
}

static void MM_Solo_PageDetails(uint32_t *ot, struct GamepadBuffer *pad)
{
	char line[160], fit[160];
	MM_Solo_Navigate(pad, 2);
	if ((pad->buttonsTapped & BTN_CROSS) != 0)
	{
		OtherFX_Play(1, 1);
		if (s_soloSel == 0)
			MM_Solo_StartSession(s_soloRoom.roomId);
		else if (!s_soloDeleteArmed)
			s_soloDeleteArmed = 1; /* Delete asks twice */
		else
		{
			if (AP_Solo_RoomDelete(s_soloRoom.roomId))
				MM_Solo_Refresh();
			else
				MM_Solo_Message("This room is in use or could not be deleted.");
		}
		return;
	}
	MM_Solo_Title("Solo Seed", ot);
	snprintf(line, sizeof line, "Slot: %s", s_soloRoom.slot);
	MM_Solo_Fit(line, fit, sizeof fit, SOLO_ROW_W);
	DecalFont_DrawLineOT(fit, SOLO_ROW_X, 0x38, FONT_SMALL, WHITE, ot);
	snprintf(line, sizeof line, "Seed: %s", s_soloRoom.seedName);
	MM_Solo_Fit(line, fit, sizeof fit, SOLO_ROW_W);
	DecalFont_DrawLineOT(fit, SOLO_ROW_X, 0x44, FONT_SMALL, WHITE, ot);
	snprintf(line, sizeof line, "From: %s   Made: %s", s_soloRoom.source, s_soloRoom.created);
	MM_Solo_Fit(line, fit, sizeof fit, SOLO_ROW_W);
	DecalFont_DrawLineOT(fit, SOLO_ROW_X, 0x50, FONT_SMALL, WHITE, ot);
	if (s_soloRoom.checksDone >= 0)
		snprintf(line, sizeof line, "Checks: %d of %d", s_soloRoom.checksDone, s_soloRoom.checksTotal);
	else
		snprintf(line, sizeof line, "Checks: unknown of %d", s_soloRoom.checksTotal);
	DecalFont_DrawLineOT(line, SOLO_ROW_X, 0x5C, FONT_SMALL, WHITE, ot);
	snprintf(line, sizeof line, "World version: %s%s", s_soloRoom.worldVersion,
	         s_soloRoom.versionMatches ? "" : " (not this client's)");
	MM_Solo_DrawWrapped(line, SOLO_ROW_X, 0x68, SOLO_ROW_W, 2, s_soloRoom.versionMatches ? WHITE : RED, ot);
	DecalFont_DrawLineOT("Resume", SOLO_ROW_X, 0x84, FONT_SMALL, ORANGE, ot);
	DecalFont_DrawLineOT(s_soloDeleteArmed ? "Press Cross again to delete" : "Delete", SOLO_ROW_X, 0x90, FONT_SMALL,
	                     s_soloDeleteArmed ? RED : ORANGE, ot);
	MM_Solo_Highlight(s_soloSel == 0 ? 0x84 : 0x90, ot);
	MM_Solo_Footer("*: SELECT   ^: BACK", ot);
}

static void MM_Solo_PageNew(uint32_t *ot, struct GamepadBuffer *pad)
{
	const int count = AP_Solo_StartCount();
	char fit[160];
	int i;
	MM_Solo_Navigate(pad, count);
	if ((pad->buttonsTapped & BTN_CROSS) != 0 && count > 0)
	{
		if (!AP_Solo_StartGet(s_soloSel, &s_soloStart))
			return;
		if (s_soloStart.error[0])
		{
			OtherFX_Play(5, 1);
			MM_Solo_Message(s_soloStart.error);
			return;
		}
		OtherFX_Play(1, 1);
		if (s_soloStart.isPreset)
		{
			s_soloHighlight = -1;
			AP_Solo_TracksClear(); /* a new start: no custom tracks picked */
			if (AP_Solo_DescribeStart(s_soloStart.path))
				MM_Solo_Busy(SOLO_JOB_DESCRIBE);
		}
		else
		{
			/* An imported or inbox YAML generates as it is, but may still
			   take custom tracks (round 2 decision 5): list them first. */
			s_soloFromOptions = 0;
			s_soloFromTracks = 0;
			AP_Solo_TracksClear();
			if (AP_Solo_TracksStart(s_soloStart.path))
				MM_Solo_Busy(SOLO_JOB_TRACKS);
			else
				MM_Solo_Go(SOLO_REVIEW);
		}
		return;
	}
	MM_Solo_Title("New Solo Seed", ot);
	if (count == 0)
		DecalFont_DrawLineOT("No presets found.", 0x100, SOLO_LIST_Y, FONT_SMALL, JUSTIFY_CENTER | RED, ot);
	for (i = s_soloTop; i < count && i < s_soloTop + SOLO_ROWS; i++)
	{
		struct APSoloStartInfo st;
		const int y = SOLO_LIST_Y + (i - s_soloTop) * SOLO_ROW_H;
		AP_Solo_StartGet(i, &st);
		MM_Solo_Fit(st.label, fit, sizeof fit, SOLO_ROW_W);
		DecalFont_DrawLineOT(fit, SOLO_ROW_X, y, FONT_SMALL, st.error[0] ? GRAY : (st.isPreset ? ORANGE : WHITE), ot);
		if (i == s_soloSel)
			MM_Solo_Highlight(y, ot);
	}
	MM_Solo_Footer("*: CHOOSE   ^: BACK", ot);
}

#define SOLO_OPTION_CAP 160
#define SOLO_GROUP_CAP 24

/* The option groups in the order the overlay lists them (a group is the label
   its options carry). Returns how many. */
static int MM_Solo_Groups(char labels[][32], int cap)
{
	int n = 0, i, k;
	struct APSoloOptionInfo info;
	for (i = 0; i < AP_Solo_OptionCount() && n < cap; i++)
	{
		if (!AP_Solo_OptionGet(i, &info))
			continue;
		for (k = 0; k < n; k++)
			if (strcmp(labels[k], info.group) == 0)
				break;
		if (k == n)
			snprintf(labels[n++], 32, "%s", info.group);
	}
	return n;
}

/* Visible option rows of `group` (-1: all groups). */
static int MM_Solo_VisibleOptions(int *map, int cap, const char *group)
{
	int n = 0, i;
	struct APSoloOptionInfo info;
	for (i = 0; i < AP_Solo_OptionCount() && n < cap; i++)
		if (AP_Solo_OptionGet(i, &info) && info.visible && (!group || strcmp(info.group, group) == 0))
			map[n++] = i;
	return n;
}

/* Back on the options screen from Tracks, Review or an error: the group list
   with Continue selected. */
static void MM_Solo_ToOptions(void)
{
	char labels[SOLO_GROUP_CAP][32];
	s_soloPage = SOLO_OPTIONS;
	s_soloGroup = -1;
	s_soloSel = MM_Solo_Groups(labels, SOLO_GROUP_CAP);
	s_soloTop = 0;
}

/* Lines an option row takes: one when the full name and value fit side by
   side, two when the value goes under the name. */
#define SOLO_OPTION_GAP 0x10
static int MM_Solo_OptionLines(const struct APSoloOptionInfo *info)
{
	return DecalFont_GetLineWidth((char *)info->label, FONT_SMALL) + SOLO_OPTION_GAP +
	               DecalFont_GetLineWidth((char *)info->value, FONT_SMALL) <= SOLO_ROW_W
	           ? 1
	           : 2;
}

static int MM_Solo_OptionRowLines(const int *map, int rows, int i)
{
	struct APSoloOptionInfo info;
	if (i >= rows || !AP_Solo_OptionGet(map[i], &info))
		return 1;
	return MM_Solo_OptionLines(&info);
}

/* Groups changed from the preset: "3 changed", else "9 options". */
static void MM_Solo_GroupCounts(const char *group, int *shown, int *changed)
{
	int i;
	struct APSoloOptionInfo info;
	*shown = 0;
	*changed = 0;
	for (i = 0; i < AP_Solo_OptionCount(); i++)
		if (AP_Solo_OptionGet(i, &info) && info.visible && strcmp(info.group, group) == 0)
		{
			(*shown)++;
			if (info.changed)
				(*changed)++;
		}
}

/* First level: the groups, then Continue. */
static void MM_Solo_PageOptionGroups(uint32_t *ot, struct GamepadBuffer *pad)
{
	char labels[SOLO_GROUP_CAP][32];
	const int groups = MM_Solo_Groups(labels, SOLO_GROUP_CAP);
	const int count = groups + 1;
	char line[160], fit[160];
	int i;

	MM_Solo_Navigate(pad, count);
	if (s_soloSel < groups && (pad->buttonsTapped & BTN_CROSS) != 0)
	{
		OtherFX_Play(1, 1);
		s_soloGroupSel = s_soloSel;
		s_soloGroupTop = s_soloTop;
		s_soloGroup = s_soloSel;
		s_soloSel = 0;
		s_soloTop = 0;
		return;
	}
	if (s_soloSel == groups && (pad->buttonsTapped & BTN_CROSS) != 0)
	{
		OtherFX_Play(1, 1);
		s_soloFromOptions = 1;
		s_soloFromTracks = 0;
		/* The custom-track step: list the installed tracks first. */
		if (AP_Solo_TracksStart(s_soloStart.path))
			MM_Solo_Busy(SOLO_JOB_TRACKS);
		else
			MM_Solo_Go(SOLO_REVIEW);
		return;
	}

	snprintf(line, sizeof line, "%s Options", s_soloStart.label);
	MM_Solo_FitFont(line, fit, sizeof fit, SOLO_TEXT_W, FONT_BIG);
	DecalFont_DrawLineOT(fit, 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
	for (i = s_soloTop; i < count && i - s_soloTop < SOLO_ROWS; i++)
	{
		const int y = SOLO_LIST_Y + (i - s_soloTop) * SOLO_ROW_H;
		if (i == groups)
			DecalFont_DrawLineOT("Continue", SOLO_ROW_X, y, FONT_SMALL, ORANGE, ot);
		else
		{
			int shown, changed;
			MM_Solo_GroupCounts(labels[i], &shown, &changed);
			MM_Solo_Fit(labels[i], fit, sizeof fit, SOLO_ROW_W - 0x90);
			DecalFont_DrawLineOT(fit, SOLO_ROW_X, y, FONT_SMALL, ORANGE, ot);
			if (changed)
				snprintf(line, sizeof line, "%d changed", changed);
			else
				snprintf(line, sizeof line, "%d option%s", shown, shown == 1 ? "" : "s");
			DecalFont_DrawLineOT(line, SOLO_ROW_R, y, FONT_SMALL, JUSTIFY_RIGHT | (changed ? WHITE : GRAY), ot);
		}
		if (i == s_soloSel)
			MM_Solo_Highlight(y, ot);
	}
	if (s_soloSel < groups)
		MM_Solo_Help("Open a group to change its options.", GRAY, ot);
	else
		MM_Solo_Help("Continue to the custom tracks and the review.", GRAY, ot);
	MM_Solo_Footer(s_soloSel < groups ? "*: OPEN  ^: BACK" : "*: CONTINUE  ^: BACK", ot);
}

/* Second level: the options of one group. */
static void MM_Solo_PageOptions(uint32_t *ot, struct GamepadBuffer *pad)
{
	char labels[SOLO_GROUP_CAP][32];
	int map[SOLO_OPTION_CAP];
	int rows, count;
	struct APSoloOptionInfo info;
	char fit[160];
	int i;

	if (s_soloHighlight >= 0 && AP_Solo_OptionGet(s_soloHighlight, &info))
	{
		/* An option error: open the group of the refused option. */
		const int groups = MM_Solo_Groups(labels, SOLO_GROUP_CAP);
		for (i = 0; i < groups; i++)
			if (strcmp(labels[i], info.group) == 0)
			{
				s_soloGroup = i;
				s_soloGroupSel = i;
				s_soloGroupTop = i >= SOLO_ROWS ? i - SOLO_ROWS + 1 : 0;
			}
	}
	else if (s_soloHighlight >= 0)
		s_soloHighlight = -1;
	if (s_soloGroup < 0)
	{
		MM_Solo_PageOptionGroups(ot, pad);
		return;
	}
	if (s_soloGroup >= MM_Solo_Groups(labels, SOLO_GROUP_CAP))
	{
		s_soloGroup = -1;
		return;
	}
	rows = MM_Solo_VisibleOptions(map, SOLO_OPTION_CAP, labels[s_soloGroup]);
	count = rows;
	if (s_soloHighlight >= 0)
	{
		s_soloSel = 0;
		for (i = 0; i < rows; i++)
			if (map[i] == s_soloHighlight)
				s_soloSel = i;
		s_soloTop = 0;
		s_soloHighlight = -1;
	}
	if (count == 0)
	{
		/* Every row of the group is hidden by another option. */
		MM_Solo_FitFont(labels[s_soloGroup], fit, sizeof fit, SOLO_TEXT_W, FONT_BIG);
		DecalFont_DrawLineOT(fit, 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
		MM_Solo_Help("Nothing to set here with the current choices.", GRAY, ot);
		MM_Solo_Footer("^: BACK", ot);
		return;
	}
	MM_Solo_Navigate(pad, count);
	if ((pad->buttonsTapped & (BTN_LEFT | BTN_RIGHT)) != 0)
	{
		AP_Solo_OptionGet(map[s_soloSel], &info);
		if (info.forced)
			OtherFX_Play(5, 1);
		else
		{
			AP_Solo_OptionStep(map[s_soloSel], (pad->buttonsTapped & BTN_RIGHT) ? 1 : -1);
			OtherFX_Play(0, 1);
		}
		/* A changed value can show or hide other rows of this group. */
		rows = MM_Solo_VisibleOptions(map, SOLO_OPTION_CAP, labels[s_soloGroup]);
		count = rows;
		if (s_soloSel >= count)
			s_soloSel = count > 0 ? count - 1 : 0;
	}
	{
		/* A changed value can grow its row: measure after stepping so the
		   selection stays visible in the very same frame. */
		int lines;
		do
		{
			lines = 0;
			for (i = s_soloTop; i <= s_soloSel && i < count; i++)
				lines += MM_Solo_OptionRowLines(map, rows, i);
			if (lines > SOLO_ROWS && s_soloTop < s_soloSel)
				s_soloTop++;
		} while (lines > SOLO_ROWS && s_soloTop < s_soloSel);
	}
	if ((pad->buttonsTapped & BTN_SQUARE) != 0)
	{
		s_soloHelpRow = map[s_soloSel];
		s_soloHelpTop = 0;
		OtherFX_Play(1, 1);
		s_soloPage = SOLO_HELP;
		return;
	}

	MM_Solo_FitFont(labels[s_soloGroup], fit, sizeof fit, SOLO_TEXT_W, FONT_BIG);
	DecalFont_DrawLineOT(fit, 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
	{
		int used = 0; /* list lines drawn */
		for (i = s_soloTop; i < count; i++)
		{
			const int y = SOLO_LIST_Y + used * SOLO_ROW_H;
			const int lines = MM_Solo_OptionRowLines(map, rows, i);
			if (used + lines > SOLO_ROWS)
				break;
			AP_Solo_OptionGet(map[i], &info);
			/* Names fit the row or take a second line for the value; the fit
			   only guards a name longer than the row. */
			MM_Solo_Fit(info.label, fit, sizeof fit, SOLO_ROW_W);
			DecalFont_DrawLineOT(fit, SOLO_ROW_X, y, FONT_SMALL, info.forced ? GRAY : ORANGE, ot);
			MM_Solo_Fit(info.value, fit, sizeof fit, SOLO_ROW_W - SOLO_OPTION_GAP);
			DecalFont_DrawLineOT(fit, SOLO_ROW_R, y + (lines - 1) * SOLO_ROW_H, FONT_SMALL,
			                     JUSTIFY_RIGHT | (info.forced ? GRAY : (info.note[0] ? RED : WHITE)), ot);
			if (i == s_soloSel)
				MM_Solo_HighlightLines(y, lines, ot);
			used += lines;
		}
	}
	if (AP_Solo_OptionGet(map[s_soloSel], &info))
		/* A forced reason or warning needs both lines; otherwise the hint. */
		MM_Solo_Help(info.note[0] ? info.note : "Square: what this option does.", info.note[0] ? RED : GRAY, ot);
	MM_Solo_Footer("LEFT/RIGHT: CHANGE  [: HELP  ^: BACK", ot);
}

#define SOLO_TRACK_PICK_X 0x150 /* the pick column: "-", "In pool", "Own pad" or laps */

static void MM_Solo_PageTracks(uint32_t *ot, struct GamepadBuffer *pad)
{
	static const char *const picks[] = {"-", "In pool", "Own pad"};
	const int rows = AP_Solo_TrackCount();
	const int count = rows + 1;
	struct APSoloTrackInfo info;
	char line[200], fit[200];
	int i;

	/* Eight rows: three help lines sit under them. */
	s_soloRowsShown = SOLO_ROWS - 1;
	MM_Solo_Navigate(pad, count);
	if ((pad->buttonsTapped & (BTN_UP | BTN_DOWN)) != 0)
		s_soloLapsFailed = 0;
	if (s_soloSel < rows && AP_Solo_TrackGet(s_soloSel, &info) && info.needsLaps)
	{
		/* An imported track without laps: Left / Right pick them, Cross keeps
		   them with its Play Solo entry and lists the tracks again. */
		if ((pad->buttonsTapped & (BTN_LEFT | BTN_RIGHT)) != 0)
		{
			AP_Solo_TrackStepLaps(s_soloSel, (pad->buttonsTapped & BTN_LEFT) != 0 ? -1 : 1);
			OtherFX_Play(0, 1);
		}
		else if ((pad->buttonsTapped & BTN_CROSS) != 0)
		{
			if (AP_Solo_TrackKeepLaps(s_soloSel) && AP_Solo_TracksStart(s_soloStart.path))
			{
				OtherFX_Play(1, 1);
				s_soloLapsFailed = 0;
				MM_Solo_Busy(SOLO_JOB_TRACKS);
				return;
			}
			OtherFX_Play(5, 1);
			s_soloLapsFailed = 1;
		}
	}
	else if (s_soloSel < rows && (pad->buttonsTapped & (BTN_CROSS | BTN_LEFT | BTN_RIGHT)) != 0)
	{
		/* Left steps back through the cycle: two steps forward of three. */
		AP_Solo_TrackCycle(s_soloSel);
		if ((pad->buttonsTapped & BTN_LEFT) != 0)
			AP_Solo_TrackCycle(s_soloSel);
		OtherFX_Play(0, 1);
	}
	else if (s_soloSel == rows && (pad->buttonsTapped & BTN_CROSS) != 0)
	{
		OtherFX_Play(1, 1);
		s_soloFromTracks = 1;
		MM_Solo_Go(SOLO_REVIEW);
		return;
	}

	MM_Solo_Title("Custom Tracks", ot);
	for (i = s_soloTop; i < count && i < s_soloTop + s_soloRowsShown; i++)
	{
		const int y = SOLO_LIST_Y + (i - s_soloTop) * SOLO_ROW_H;
		if (i == rows)
			DecalFont_DrawLineOT("Continue", SOLO_ROW_X, y, FONT_SMALL, ORANGE, ot);
		else
		{
			AP_Solo_TrackGet(i, &info);
			snprintf(line, sizeof line, "%s %s", info.title, info.revision);
			MM_Solo_Fit(line, fit, sizeof fit, SOLO_TRACK_PICK_X - SOLO_ROW_X - 0x08);
			DecalFont_DrawLineOT(fit, SOLO_ROW_X, y, FONT_SMALL, ORANGE, ot);
			if (info.needsLaps)
			{
				snprintf(line, sizeof line, "< %d lap%s >", info.laps, info.laps == 1 ? "" : "s");
				DecalFont_DrawLineOT(line, SOLO_TRACK_PICK_X, y, FONT_SMALL, RED, ot);
			}
			else if (!info.modes[0])
				DecalFont_DrawLineOT(info.timeTrial ? "Time Trial only" : "No pad races", 0x158, y, FONT_SMALL, GRAY, ot);
			else
				DecalFont_DrawLineOT((char *)picks[info.pick >= 0 && info.pick <= 2 ? info.pick : 0],
				                     SOLO_TRACK_PICK_X, y, FONT_SMALL, info.pick ? WHITE : GRAY, ot);
		}
		if (i == s_soloSel)
			MM_Solo_Highlight(y, ot);
	}
	/* Three help lines under the eight rows: what the selected track is, then
	   what the picks mean (that line used to sit on the title rule). */
	{
		const int helpY = SOLO_HELP_Y - SOLO_ROW_H;
		int legendLines = 2;
		if (s_soloSel < rows && AP_Solo_TrackGet(s_soloSel, &info) && info.needsLaps)
		{
			if (s_soloLapsFailed)
				snprintf(line, sizeof line, "Could not keep the laps. See the client log.");
			else
				snprintf(line, sizeof line, "By %s. Pick its laps, then press * to use it.", info.author);
			MM_Solo_DrawWrapped(line, 0x100, helpY, SOLO_TEXT_W, 3, JUSTIFY_CENTER | (s_soloLapsFailed ? RED : GRAY),
			                    ot);
			legendLines = 0;
		}
		else if (s_soloSel < rows && AP_Solo_TrackGet(s_soloSel, &info))
		{
			if (!info.modes[0] && info.timeTrial && info.pick)
				snprintf(line, sizeof line, "Time Trial only; press * to remove it from the YAML.");
			else if (!info.modes[0] && info.timeTrial)
				snprintf(line, sizeof line, "By %s. Time Trial only; no Adventure pad races.", info.author);
			else if (!info.modes[0])
				snprintf(line, sizeof line, "By %s. No supported races for Adventure pads.", info.author);
			else if (info.author[0])
				snprintf(line, sizeof line, "By %s. Modes: %s%s", info.author, info.modes,
				         info.timeTrial ? ", Time Trial" : "");
			else
				snprintf(line, sizeof line, "Modes: %s%s", info.modes, info.timeTrial ? ", Time Trial" : "");
			MM_Solo_Fit(line, fit, sizeof fit, SOLO_TEXT_W);
			DecalFont_DrawLineOT(fit, 0x100, helpY, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
		}
		else if (!s_soloStart.isPreset)
			MM_Solo_DrawWrapped("The YAML's own settings stay; the tracks are added.", 0x100, helpY, SOLO_TEXT_W, 1,
			                    JUSTIFY_CENTER | WHITE, ot);
		if (legendLines)
			MM_Solo_DrawWrapped("In pool: the seed may use it. Own pad: it gets a pad.", 0x100, SOLO_HELP_Y,
			                    SOLO_TEXT_W, legendLines, JUSTIFY_CENTER | GRAY, ot);
	}
	if (s_soloSel < rows && AP_Solo_TrackGet(s_soloSel, &info) && info.needsLaps)
		MM_Solo_Footer("LEFT/RIGHT: LAPS  *: KEEP  ^: BACK", ot);
	else
		MM_Solo_Footer("*: CHANGE   ^: BACK", ot);
}

#define SOLO_HELP_LINES 11

static void MM_Solo_PageHelp(uint32_t *ot, struct GamepadBuffer *pad)
{
	struct APSoloOptionInfo info;
	const char *text = AP_Solo_OptionHelp(s_soloHelpRow);
	/* Long help scrolls: Up / Down move by a line. */
	const int total = MM_Solo_WrapLines(text, 0x30, SOLO_LIST_Y, 0x1A0, 0, 0, 0, WHITE, NULL);
	const int last = total > SOLO_HELP_LINES ? total - SOLO_HELP_LINES : 0;
	if ((pad->buttonsTapped & BTN_DOWN) != 0 && s_soloHelpTop < last)
		s_soloHelpTop++;
	if ((pad->buttonsTapped & BTN_UP) != 0 && s_soloHelpTop > 0)
		s_soloHelpTop--;
	if (s_soloHelpTop > last)
		s_soloHelpTop = last;
	AP_Solo_OptionGet(s_soloHelpRow, &info);
	MM_Solo_Title(info.label, ot);
	MM_Solo_WrapLines(text, 0x30, SOLO_LIST_Y, 0x1A0, SOLO_HELP_LINES, s_soloHelpTop, 0, WHITE, ot);
	MM_Solo_Footer(last ? "UP/DOWN: SCROLL  ^: BACK" : "^: BACK", ot);
}

/* The slot name text entry, with pad commit / cancel like the Connection page. */
static void MM_Solo_UpdateNameEdit(void)
{
	const int padNow = Platform_InputRawGamepadButtons();
	const int padTapped = padNow & ~s_soloPadPrev;
	s_soloPadPrev = padNow;
	if (!s_soloEditing)
		return;
	if (NativeText_Result() == 0)
	{
		if ((padTapped & CONN_PAD_CANCEL) != 0)
			NativeText_Resolve(2);
		else if ((padTapped & CONN_PAD_COMMIT) != 0)
			NativeText_Resolve(1);
	}
	if (NativeText_Result() != 0)
	{
		if (NativeText_Result() == 2 || s_soloName[0] == '\0')
			snprintf(s_soloName, sizeof s_soloName, "%s", s_soloNameBackup);
		NativeText_End();
		s_soloEditing = 0;
	}
}

#define SOLO_REVIEW_NAME_Y 0x6C /* under the start and options lines, two each */

static void MM_Solo_PageReview(uint32_t *ot, struct GamepadBuffer *pad)
{
	char line[160], fit[160];
	if (!s_soloEditing && !s_soloEditJustEnded)
	{
		MM_Solo_Navigate(pad, 2);
		if ((pad->buttonsTapped & BTN_CROSS) != 0)
		{
			OtherFX_Play(1, 1);
			if (s_soloSel == 0)
			{
				snprintf(s_soloNameBackup, sizeof s_soloNameBackup, "%s", s_soloName);
				NativeText_Begin(s_soloName, sizeof s_soloName, CONN_ROW_X, SOLO_REVIEW_NAME_Y - 2, CONN_ROW_W, CONN_ROW_H, 0);
				s_soloEditing = 1;
			}
			else if (AP_Solo_GenerateStart(s_soloStart.path, s_soloName,
			                               s_soloStart.isPreset ? s_soloStart.presetId : "", s_soloFromOptions))
				MM_Solo_Busy(SOLO_JOB_GENERATE);
			return;
		}
	}
	MM_Solo_Title("Review", ot);
	snprintf(line, sizeof line, "Start: %s", s_soloStart.label);
	MM_Solo_DrawWrapped(line, SOLO_ROW_X, 0x38, SOLO_ROW_W, 2, WHITE, ot);
	if (AP_Solo_TracksPicked() > 0)
		snprintf(line, sizeof line, "Options: %s, plus %d custom track%s",
		         s_soloFromOptions ? "as set" : "as in the YAML", AP_Solo_TracksPicked(),
		         AP_Solo_TracksPicked() == 1 ? "" : "s");
	else
		snprintf(line, sizeof line, "%s",
		         s_soloFromOptions ? "Options: as set on the previous page" : "Options: as in the YAML");
	MM_Solo_DrawWrapped(line, SOLO_ROW_X, 0x50, SOLO_ROW_W, 2, WHITE, ot);
	DecalFont_DrawLineOT("Slot name", SOLO_ROW_X, SOLO_REVIEW_NAME_Y, FONT_SMALL, ORANGE, ot);
	MM_Solo_Fit(s_soloName, line, sizeof line,
	            SOLO_ROW_R - 0xB0 - (s_soloEditing ? DecalFont_GetLineWidth("_", FONT_SMALL) : 0));
	snprintf(fit, sizeof fit, "%s%s", line, s_soloEditing ? "_" : "");
	DecalFont_DrawLineOT(fit, 0xB0, SOLO_REVIEW_NAME_Y, FONT_SMALL, WHITE, ot);
	DecalFont_DrawLineOT("Generate", SOLO_ROW_X, SOLO_REVIEW_NAME_Y + SOLO_ROW_H, FONT_SMALL, ORANGE, ot);
	if (!s_soloEditing)
		MM_Solo_Highlight(s_soloSel == 0 ? SOLO_REVIEW_NAME_Y : SOLO_REVIEW_NAME_Y + SOLO_ROW_H, ot);
	if (!s_soloStart.isPreset)
		MM_Solo_Help("The YAML's own name is used unless you change it here.", GRAY, ot);
	MM_Solo_Footer(s_soloEditing ? "* OR START: SAVE   ^: CANCEL" : "*: SELECT   ^: BACK", ot);
}

static void MM_Solo_PageBusy(uint32_t *ot, struct GamepadBuffer *pad)
{
	const int state = AP_Solo_JobState();
	static const char *const titles[] = {"Loading", "Loading Options", "Generating", "Importing Room",
	                                     "Importing YAML", "Finding Custom Tracks"};
	if (state == AP_SOLO_JOB_RUNNING)
	{
		if (s_soloBusyJob == SOLO_JOB_GENERATE && (pad->buttonsTapped & BTN_CIRCLE) != 0)
		{
			OtherFX_Play(2, 1);
			AP_Solo_JobCancel();
			MM_Solo_Go(SOLO_REVIEW);
			return;
		}
		MM_Solo_Title(titles[s_soloBusyJob], ot);
		MM_Solo_DrawWrapped(AP_Solo_JobPhase(), 0x100, 0x60, SOLO_TEXT_W, 2, JUSTIFY_CENTER | WHITE, ot);
		if (s_soloBusyJob == SOLO_JOB_GENERATE)
			MM_Solo_Footer("CIRCLE: CANCEL", ot);
		return;
	}
	if (state == AP_SOLO_JOB_FAILED)
	{
		const char *option = AP_Solo_JobErrorOption();
		snprintf(s_soloMsg, sizeof s_soloMsg, "%s", AP_Solo_JobErrorText());
		AP_Solo_JobClear();
		if (s_soloBusyJob == SOLO_JOB_GENERATE && s_soloFromOptions && option[0] && AP_Solo_OptionIndex(option) >= 0)
		{
			/* Back to the options screen with the row highlighted. */
			s_soloHighlight = AP_Solo_OptionIndex(option);
			s_soloPage = SOLO_MESSAGE;
			s_soloFromOptions = 2; /* the message returns to Options */
			return;
		}
		if (s_soloBusyJob == SOLO_JOB_TRACKS)
		{
			s_soloPage = SOLO_MESSAGE;
			s_soloFromOptions = 2; /* the message returns to Options, or to the YAML list */
			return;
		}
		if (s_soloBusyJob == SOLO_JOB_GENERATE && s_soloFromTracks)
		{
			/* For example a picked track was removed: list the tracks again. */
			s_soloPage = SOLO_MESSAGE;
			s_soloFromOptions = 3; /* the message returns to Custom Tracks */
			return;
		}
		MM_Solo_Go(SOLO_MESSAGE);
		return;
	}
	AP_Solo_JobClear();
	switch (s_soloBusyJob)
	{
	case SOLO_JOB_LIST:
		MM_Solo_Go(SOLO_HOME);
		break;
	case SOLO_JOB_DESCRIBE:
		MM_Solo_Go(SOLO_OPTIONS);
		break;
	case SOLO_JOB_TRACKS:
		/* The step is only shown when there is a track to pick. */
		s_soloFromTracks = AP_Solo_TrackCount() > 0;
		MM_Solo_Go(s_soloFromTracks ? SOLO_TRACKS : SOLO_REVIEW);
		break;
	case SOLO_JOB_GENERATE:
	{
		char room[24];
		snprintf(room, sizeof room, "%s", AP_Solo_JobResultRoom());
		MM_Solo_StartSession(room);
		break;
	}
	case SOLO_JOB_IMPORT_YAML:
	case SOLO_JOB_IMPORT_ROOM:
	default:
		MM_Solo_Refresh();
		break;
	}
}

static void MM_Solo_PageSession(uint32_t *ot, struct GamepadBuffer *pad)
{
	const char *text = "";
	int color = WHITE;
	(void)pad;
	switch (AP_Solo_SessionState())
	{
	case AP_SOLO_SESSION_STARTING: text = "Starting the solo server..."; break;
	case AP_SOLO_SESSION_CONNECTING: text = "Connecting..."; break;
	case AP_SOLO_SESSION_LIVE: text = "Connected. Back out and start Adventure to play."; color = ORANGE; break;
	case AP_SOLO_SESSION_STOPPING: text = "Saving and stopping..."; break;
	case AP_SOLO_SESSION_FAILED: text = AP_Solo_SessionError(); color = RED; break;
	default: text = "The solo session is not running."; break;
	}
	MM_Solo_Title("Play Solo", ot);
	MM_Solo_DrawWrapped(text, 0x40, 0x58, 0x180, 4, color, ot);
	MM_Solo_Footer("^: BACK", ot);
}

static void MM_Solo_PageMessage(uint32_t *ot, struct GamepadBuffer *pad)
{
	if ((pad->buttonsTapped & BTN_CROSS) != 0)
	{
		OtherFX_Play(2, 1);
		if (s_soloFromOptions == 2 && !AP_Solo_TracksViaOptions(s_soloStart.isPreset))
			MM_Solo_Go(SOLO_NEW);
		else if (s_soloFromOptions == 2)
		{
			s_soloFromOptions = 1;
			MM_Solo_ToOptions();
		}
		else if (s_soloFromOptions == 3)
		{
			s_soloFromOptions = AP_Solo_TracksViaOptions(s_soloStart.isPreset);
			if (AP_Solo_TracksStart(s_soloStart.path))
				MM_Solo_Busy(SOLO_JOB_TRACKS);
			else
				MM_Solo_Go(SOLO_TRACKS);
		}
		else
			MM_Solo_Go(AP_Solo_RuntimePresent() ? SOLO_HOME : SOLO_OFF);
		return;
	}
	MM_Solo_Title("Play Solo", ot);
	MM_Solo_DrawWrapped(s_soloMsg, 0x40, 0x48, 0x180, 7, WHITE, ot);
	MM_Solo_Footer("*: OK", ot);
}

/* Triangle or Start inside Play Solo: one page back. */
static void MM_Solo_Back(void)
{
	OtherFX_Play(2, 1);
	switch (s_soloPage)
	{
	case SOLO_HOME: s_soloPage = SOLO_OFF; break;
	case SOLO_DETAILS: case SOLO_NEW: case SOLO_SESSION: MM_Solo_Refresh(); break;
	case SOLO_OPTIONS:
		if (s_soloGroup >= 0)
		{
			s_soloGroup = -1;
			s_soloSel = s_soloGroupSel;
			s_soloTop = s_soloGroupTop;
		}
		else
			MM_Solo_Go(SOLO_NEW);
		break;
	case SOLO_TRACKS:
		if (AP_Solo_TracksViaOptions(s_soloStart.isPreset))
		{
			MM_Solo_ToOptions();
		}
		else
			MM_Solo_Go(SOLO_NEW);
		break;
	case SOLO_HELP: s_soloPage = SOLO_OPTIONS; break;
	case SOLO_REVIEW:
		if (s_soloFromTracks)
			MM_Solo_Go(SOLO_TRACKS);
		else if (s_soloFromOptions)
		{
			MM_Solo_ToOptions();
		}
		else
			MM_Solo_Go(SOLO_NEW);
		break;
	case SOLO_MESSAGE: MM_Solo_Go(AP_Solo_RuntimePresent() ? SOLO_HOME : SOLO_OFF); break;
	default: break; /* Busy: Circle cancels generation; other jobs finish quickly */
	}
}

/* The Connection page calls this first every frame. Returns 1 when Play Solo
   owns the page this frame, 2 when it closed this frame (draw the Connection
   page, but take no input), 0 otherwise. */
static int MM_Solo_Frame(uint32_t *ot, struct GamepadBuffer *pad)
{
	if (s_soloPage == SOLO_OFF)
		return 0;
	/* Track pad edges every frame, editing or not, so the press that opens
	   the slot name edit cannot also commit it. */
	{
		const int wasEditing = s_soloEditing;
		MM_Solo_UpdateNameEdit();
		s_soloEditJustEnded = wasEditing && !s_soloEditing;
	}
	MM_Solo_PollPick();
	if (!s_soloEditing && !s_soloEditJustEnded && (pad->buttonsTapped & (BTN_TRIANGLE | BTN_START)) != 0 &&
	    s_soloPage != SOLO_BUSY)
	{
		MM_Solo_Back();
		/* Back owns this frame: the new page reads input from the next one.
		   On leaving Play Solo the Connection page draws but takes no input
		   this frame (2). */
		if (s_soloPage == SOLO_OFF)
			return 2;
		return 1;
	}
	s_soloRowsShown = SOLO_ROWS;
	switch (s_soloPage)
	{
	case SOLO_HOME: MM_Solo_PageHome(ot, pad); break;
	case SOLO_DETAILS: MM_Solo_PageDetails(ot, pad); break;
	case SOLO_NEW: MM_Solo_PageNew(ot, pad); break;
	case SOLO_OPTIONS: MM_Solo_PageOptions(ot, pad); break;
	case SOLO_TRACKS: MM_Solo_PageTracks(ot, pad); break;
	case SOLO_HELP: MM_Solo_PageHelp(ot, pad); break;
	case SOLO_REVIEW: MM_Solo_PageReview(ot, pad); break;
	case SOLO_BUSY: MM_Solo_PageBusy(ot, pad); break;
	case SOLO_SESSION: MM_Solo_PageSession(ot, pad); break;
	case SOLO_MESSAGE: MM_Solo_PageMessage(ot, pad); break;
	default: break;
	}
	MM_Solo_Panel(ot);
	return 1;
}
