/* Play Solo (#89): Options > Connection > Play Solo, and the title screen's
   PLAY SOLO row (decision 30), which opens the same pages.
   Included by MM_ConfigMenu.c in the AP player client (CTR_AP, not
   CTR_AP_AUTHORING), ahead of MM_ConfigProc_Connection.

   Pages:
     Home     your solo seeds (Cross resumes, Square opens Details), then
              New solo seed, Import room file, Import YAML file, and the
              AP-Pie YAML builder. Triangle leaves Play Solo.
     New      bundled presets, saved YAMLs, then YAMLs in solo/inbox.
     Options  the curated options of a preset: Left / Right change, Square
              shows the help text. DeathLink is fixed off in solo.
     Tracks   installed custom tracks for a preset (decision 32), shown only
              when there are any: Cross cycles off, in the pool, own pad.
     Review   slot name (Cross edits it) and Generate.
     Busy     a runtime job with its phase; Circle cancels generation.
     Session  starting, connecting, connected or the reason it failed.
   The runtime, jobs and session live in ap/ap_solo.cpp; the option rules in
   ap/ap_solo_logic.hpp. */

#define SOLO_ROWS 9
#define SOLO_LIST_Y 0x38
#define SOLO_ROW_H 0x0C
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
static int s_soloEditing;
static int s_soloEditJustEnded;  /* the frame an edit resolved: no navigation */
static int s_soloPadPrev;
static struct APSoloRoomInfo s_soloRoom;
static struct APSoloStartInfo s_soloStart;
static char s_soloName[64];
static char s_soloNameBackup[64];
static char s_soloMsg[256];

static int s_soloPickKind;       /* SOLO_JOB_IMPORT_ROOM or SOLO_JOB_IMPORT_YAML */

static void MM_Solo_DrawWrapped(const char *text, int x, int y, int width, int lines, int color, uint32_t *ot)
{
	char line[160];
	const char *cursor = text;
	int i;
	for (i = 0; i < lines && *cursor; i++)
	{
		size_t n = 0, cut = 0;
		while (cursor[n] && cursor[n] != '\n' && n + 1 < sizeof line)
		{
			line[n] = cursor[n];
			line[n + 1] = '\0';
			if (DecalFont_GetLineWidth(line, FONT_SMALL) > width) break;
			if (cursor[n] == ' ') cut = n;
			n++;
		}
		if (cursor[n] && cursor[n] != '\n' && cut > 0) n = cut;
		line[n] = '\0';
		DecalFont_DrawLineOT(line, x, y + i * SOLO_ROW_H, FONT_SMALL, color, ot);
		cursor += n;
		while (*cursor == ' ' || *cursor == '\n') cursor++;
	}
}

static void MM_Solo_Fit(const char *text, char *out, size_t size, int width)
{
	size_t n;
	snprintf(out, size, "%s", text);
	n = strlen(out);
	if (DecalFont_GetLineWidth(out, FONT_SMALL) <= width) return;
	while (n > 0)
	{
		n--;
		if (n + 4 > size) continue;
		memcpy(out + n, "...", 4);
		if (DecalFont_GetLineWidth(out, FONT_SMALL) <= width) return;
	}
	out[0] = '\0';
}

static void MM_Solo_Title(const char *title, uint32_t *ot)
{
	DecalFont_DrawLineOT((char *)title, 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
}

static void MM_Solo_Footer(const char *hint, uint32_t *ot)
{
	DecalFont_DrawLineOT((char *)hint, 0x100, 0xC4, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
}

static void MM_Solo_Highlight(int y, uint32_t *ot)
{
	RECT sel = {0x30, y - 2, 0x1A0, 0x0C};
	CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
}

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
	if (s_soloSel >= s_soloTop + SOLO_ROWS) s_soloTop = s_soloSel - SOLO_ROWS + 1;
}

static void MM_Solo_Go(int page)
{
	s_soloPage = page;
	s_soloSel = 0;
	s_soloTop = 0;
	s_soloDeleteArmed = 0;
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
		DecalFont_DrawLineOT("No solo seeds yet.", 0x100, 0x28, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	for (i = s_soloTop; i < count && i < s_soloTop + SOLO_ROWS; i++)
	{
		const int y = SOLO_LIST_Y + (i - s_soloTop) * SOLO_ROW_H;
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
		MM_Solo_Fit(line, fit, sizeof fit, 0x190);
		DecalFont_DrawLineOT(fit, 0x38, y, FONT_SMALL, color, ot);
		if (i == s_soloSel)
			MM_Solo_Highlight(y, ot);
	}
	if (AP_Solo_InboxPath()[0])
	{
		snprintf(line, sizeof line, "Inbox: %s", AP_Solo_InboxPath());
		MM_Solo_Fit(line, fit, sizeof fit, 0x1A0);
		DecalFont_DrawLineOT(fit, 0x100, 0xB4, FONT_SMALL, JUSTIFY_CENTER | GRAY, ot);
	}
	MM_Solo_Footer("*: PLAY   SQUARE: DETAILS   ^: BACK", ot);
}

static void MM_Solo_PageDetails(uint32_t *ot, struct GamepadBuffer *pad)
{
	char line[160];
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
	DecalFont_DrawLineOT(line, 0x38, 0x38, FONT_SMALL, WHITE, ot);
	snprintf(line, sizeof line, "Seed: %s", s_soloRoom.seedName);
	DecalFont_DrawLineOT(line, 0x38, 0x44, FONT_SMALL, WHITE, ot);
	snprintf(line, sizeof line, "From: %s   Made: %s", s_soloRoom.source, s_soloRoom.created);
	DecalFont_DrawLineOT(line, 0x38, 0x50, FONT_SMALL, WHITE, ot);
	if (s_soloRoom.checksDone >= 0)
		snprintf(line, sizeof line, "Checks: %d of %d", s_soloRoom.checksDone, s_soloRoom.checksTotal);
	else
		snprintf(line, sizeof line, "Checks: unknown of %d", s_soloRoom.checksTotal);
	DecalFont_DrawLineOT(line, 0x38, 0x5C, FONT_SMALL, WHITE, ot);
	snprintf(line, sizeof line, "World version: %s%s", s_soloRoom.worldVersion,
	         s_soloRoom.versionMatches ? "" : " (not this client's)");
	DecalFont_DrawLineOT(line, 0x38, 0x68, FONT_SMALL, s_soloRoom.versionMatches ? WHITE : RED, ot);
	DecalFont_DrawLineOT("Resume", 0x38, 0x84, FONT_SMALL, ORANGE, ot);
	DecalFont_DrawLineOT(s_soloDeleteArmed ? "Press Cross again to delete" : "Delete", 0x38, 0x90, FONT_SMALL,
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
			s_soloFromOptions = 0;
			s_soloFromTracks = 0;
			MM_Solo_Go(SOLO_REVIEW);
		}
		return;
	}
	MM_Solo_Title("New Solo Seed", ot);
	if (count == 0)
		DecalFont_DrawLineOT("No presets found.", 0x100, 0x48, FONT_SMALL, JUSTIFY_CENTER | RED, ot);
	for (i = s_soloTop; i < count && i < s_soloTop + SOLO_ROWS; i++)
	{
		struct APSoloStartInfo st;
		const int y = SOLO_LIST_Y + (i - s_soloTop) * SOLO_ROW_H;
		AP_Solo_StartGet(i, &st);
		MM_Solo_Fit(st.label, fit, sizeof fit, 0x190);
		DecalFont_DrawLineOT(fit, 0x38, y, FONT_SMALL, st.error[0] ? GRAY : (st.isPreset ? ORANGE : WHITE), ot);
		if (i == s_soloSel)
			MM_Solo_Highlight(y, ot);
	}
	MM_Solo_Footer("*: CHOOSE   ^: BACK", ot);
}

/* Visible option rows plus the Continue row. */
static int MM_Solo_VisibleOptions(int *map, int cap)
{
	int n = 0, i;
	struct APSoloOptionInfo info;
	for (i = 0; i < AP_Solo_OptionCount() && n < cap; i++)
		if (AP_Solo_OptionGet(i, &info) && info.visible)
			map[n++] = i;
	return n;
}

static void MM_Solo_PageOptions(uint32_t *ot, struct GamepadBuffer *pad)
{
	int map[64];
	const int rows = MM_Solo_VisibleOptions(map, 64);
	const int count = rows + 1;
	struct APSoloOptionInfo info;
	char line[160], fit[160];
	int i;

	if (s_soloHighlight >= 0)
	{
		for (i = 0; i < rows; i++)
			if (map[i] == s_soloHighlight)
				s_soloSel = i;
		s_soloHighlight = -1;
		if (s_soloSel >= SOLO_ROWS) s_soloTop = s_soloSel - SOLO_ROWS + 1;
	}
	MM_Solo_Navigate(pad, count);
	if (s_soloSel < rows && (pad->buttonsTapped & (BTN_LEFT | BTN_RIGHT)) != 0)
	{
		AP_Solo_OptionGet(map[s_soloSel], &info);
		if (info.forced)
			OtherFX_Play(5, 1);
		else
		{
			AP_Solo_OptionStep(map[s_soloSel], (pad->buttonsTapped & BTN_RIGHT) ? 1 : -1);
			OtherFX_Play(0, 1);
		}
	}
	if (s_soloSel < rows && (pad->buttonsTapped & BTN_SQUARE) != 0)
	{
		s_soloHelpRow = map[s_soloSel];
		OtherFX_Play(1, 1);
		s_soloPage = SOLO_HELP;
		return;
	}
	if (s_soloSel == rows && (pad->buttonsTapped & BTN_CROSS) != 0)
	{
		OtherFX_Play(1, 1);
		s_soloFromOptions = 1;
		s_soloFromTracks = 0;
		/* The custom-track step: list the installed tracks first. */
		if (AP_Solo_TracksStart())
			MM_Solo_Busy(SOLO_JOB_TRACKS);
		else
			MM_Solo_Go(SOLO_REVIEW);
		return;
	}

	snprintf(line, sizeof line, "%s Options", s_soloStart.label);
	MM_Solo_Fit(line, fit, sizeof fit, 0x1C0);
	DecalFont_DrawLineOT(fit, 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
	for (i = s_soloTop; i < count && i < s_soloTop + SOLO_ROWS; i++)
	{
		const int y = SOLO_LIST_Y + (i - s_soloTop) * SOLO_ROW_H;
		if (i == rows)
			DecalFont_DrawLineOT("Continue", 0x38, y, FONT_SMALL, ORANGE, ot);
		else
		{
			AP_Solo_OptionGet(map[i], &info);
			MM_Solo_Fit(info.label, fit, sizeof fit, 0xD0);
			DecalFont_DrawLineOT(fit, 0x38, y, FONT_SMALL, info.forced ? GRAY : ORANGE, ot);
			MM_Solo_Fit(info.value, fit, sizeof fit, 0xB8);
			DecalFont_DrawLineOT(fit, 0x118, y, FONT_SMALL, info.forced ? GRAY : (info.note[0] ? RED : WHITE), ot);
		}
		if (i == s_soloSel)
			MM_Solo_Highlight(y, ot);
	}
	if (s_soloSel < rows && AP_Solo_OptionGet(map[s_soloSel], &info))
	{
		snprintf(line, sizeof line, "%s%s%s", info.group, info.note[0] ? ": " : "", info.note);
		MM_Solo_Fit(line, fit, sizeof fit, 0x1A0);
		DecalFont_DrawLineOT(fit, 0x100, 0xB4, FONT_SMALL, JUSTIFY_CENTER | (info.note[0] ? RED : GRAY), ot);
	}
	else
		DecalFont_DrawLineOT("More options: build a YAML on AP-Pie and import it", 0x100, 0xB4, FONT_SMALL,
		                     JUSTIFY_CENTER | GRAY, ot);
	MM_Solo_Footer("LEFT/RIGHT: CHANGE   SQUARE: HELP   ^: BACK", ot);
}

static void MM_Solo_PageTracks(uint32_t *ot, struct GamepadBuffer *pad)
{
	static const char *const picks[] = {"-", "In pool", "Own pad"};
	const int rows = AP_Solo_TrackCount();
	const int count = rows + 1;
	struct APSoloTrackInfo info;
	char line[200], fit[200];
	int i;

	MM_Solo_Navigate(pad, count);
	if (s_soloSel < rows && (pad->buttonsTapped & (BTN_CROSS | BTN_LEFT | BTN_RIGHT)) != 0)
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
	DecalFont_DrawLineOT("In pool: the seed may use it. Own pad: it gets a pad.", 0x100, 0x28, FONT_SMALL,
	                     JUSTIFY_CENTER | GRAY, ot);
	for (i = s_soloTop; i < count && i < s_soloTop + SOLO_ROWS; i++)
	{
		const int y = SOLO_LIST_Y + (i - s_soloTop) * SOLO_ROW_H;
		if (i == rows)
			DecalFont_DrawLineOT("Continue", 0x38, y, FONT_SMALL, ORANGE, ot);
		else
		{
			AP_Solo_TrackGet(i, &info);
			snprintf(line, sizeof line, "%s %s", info.title, info.revision);
			MM_Solo_Fit(line, fit, sizeof fit, 0x110);
			DecalFont_DrawLineOT(fit, 0x38, y, FONT_SMALL, ORANGE, ot);
			DecalFont_DrawLineOT((char *)picks[info.pick >= 0 && info.pick <= 2 ? info.pick : 0], 0x158, y, FONT_SMALL,
			                     info.pick ? WHITE : GRAY, ot);
		}
		if (i == s_soloSel)
			MM_Solo_Highlight(y, ot);
	}
	if (s_soloSel < rows && AP_Solo_TrackGet(s_soloSel, &info))
	{
		snprintf(line, sizeof line, "By %s. Modes: %s", info.author, info.modes[0] ? info.modes : "none");
		MM_Solo_Fit(line, fit, sizeof fit, 0x1A0);
		DecalFont_DrawLineOT(fit, 0x100, 0xB4, FONT_SMALL, JUSTIFY_CENTER | GRAY, ot);
	}
	MM_Solo_Footer("*: CHANGE   ^: BACK", ot);
}

static void MM_Solo_PageHelp(uint32_t *ot, struct GamepadBuffer *pad)
{
	struct APSoloOptionInfo info;
	(void)pad;
	AP_Solo_OptionGet(s_soloHelpRow, &info);
	MM_Solo_Title(info.label, ot);
	MM_Solo_DrawWrapped(AP_Solo_OptionHelp(s_soloHelpRow), 0x30, 0x34, 0x1A0, 11, WHITE, ot);
	MM_Solo_Footer("^: BACK", ot);
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
				NativeText_Begin(s_soloName, 16, CONN_ROW_X, 0x58 - 2, CONN_ROW_W, CONN_ROW_H, 0);
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
	MM_Solo_Fit(line, fit, sizeof fit, 0x1A0);
	DecalFont_DrawLineOT(fit, 0x38, 0x38, FONT_SMALL, WHITE, ot);
	if (s_soloFromOptions && AP_Solo_TracksPicked() > 0)
	{
		snprintf(line, sizeof line, "Options: as set, plus %d custom track%s", AP_Solo_TracksPicked(),
		         AP_Solo_TracksPicked() == 1 ? "" : "s");
		DecalFont_DrawLineOT(line, 0x38, 0x44, FONT_SMALL, WHITE, ot);
	}
	else
		DecalFont_DrawLineOT(s_soloFromOptions ? "Options: as set on the previous page" : "Options: as in the YAML",
		                     0x38, 0x44, FONT_SMALL, WHITE, ot);
	DecalFont_DrawLineOT("Slot name", 0x38, 0x58, FONT_SMALL, ORANGE, ot);
	snprintf(line, sizeof line, "%s%s", s_soloName, s_soloEditing ? "_" : "");
	DecalFont_DrawLineOT(line, 0xB0, 0x58, FONT_SMALL, WHITE, ot);
	DecalFont_DrawLineOT("Generate", 0x38, 0x64, FONT_SMALL, ORANGE, ot);
	if (!s_soloEditing)
		MM_Solo_Highlight(s_soloSel == 0 ? 0x58 : 0x64, ot);
	if (!s_soloStart.isPreset)
		DecalFont_DrawLineOT("The YAML's own name is used unless you change it here.", 0x100, 0x80, FONT_SMALL,
		                     JUSTIFY_CENTER | GRAY, ot);
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
		DecalFont_DrawLineOT((char *)AP_Solo_JobPhase(), 0x100, 0x60, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
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
			s_soloFromOptions = 2; /* the message returns to Options */
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
		if (s_soloFromOptions == 2)
		{
			s_soloFromOptions = 1;
			s_soloPage = SOLO_OPTIONS;
			s_soloSel = 0;
			s_soloTop = 0;
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
	case SOLO_OPTIONS: MM_Solo_Go(SOLO_NEW); break;
	case SOLO_TRACKS:
		s_soloPage = SOLO_OPTIONS;
		s_soloSel = 0;
		s_soloTop = 0;
		break;
	case SOLO_HELP: s_soloPage = SOLO_OPTIONS; break;
	case SOLO_REVIEW:
		if (s_soloFromTracks)
			MM_Solo_Go(SOLO_TRACKS);
		else if (s_soloFromOptions)
		{
			s_soloPage = SOLO_OPTIONS;
			s_soloSel = 0;
			s_soloTop = 0;
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
	return 1;
}
