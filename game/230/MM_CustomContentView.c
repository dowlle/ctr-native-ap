/* The Custom Content package page's layout (Options > Custom Content, a room
   with one custom-track package). Included by MM_ConfigMenu.c, which gathers
   the strings; tools/test-custom-content-layout.c checks the bounds.

   Minimum fix from the Deck menu scan (2026-11-03), not the restructure being
   designed: one line each for the package, its source, the status, the seed
   requirement, the relic times and the importer hint, so nothing overlaps
   ("RELIC TIMES: NOT SET" ran into "SELECT: IMPORT TRACK FILES"); two action
   columns that end inside the frame; the message wraps over three lines; and
   a dimmed panel behind the text, as on the Track Manager. */

#define CC_VIEW_EDGE_L 0x18
#define CC_VIEW_EDGE_R 0x1E8
#define CC_VIEW_TEXT_W (CC_VIEW_EDGE_R - CC_VIEW_EDGE_L)
#define CC_VIEW_LINE_H 0x0C
#define CC_VIEW_ACTION_Y 0x7C       /* first action row; the relic editor starts here too */
#define CC_VIEW_ACTION_X0 0x38
#define CC_VIEW_ACTION_X1 0x100
#define CC_VIEW_MESSAGE_Y 0xAC

struct CustomContentView
{
	const char *title;
	const char *version;
	const char *author;
	const char *state;   /* "Not installed", "Ready", "Downloading" ... */
	const char *seed;    /* "OPTIONAL", "READY" or "REQUIRED" */
	int required;        /* the room needs this package: red status and message */
	const char *relic;   /* "Relic times: ..." */
	const char *message;
	const char *const *actions;
	int actionCount;
	int actionRows;
	int selected;
	int editing;         /* the relic times editor replaces the actions */
};

/* One FONT_SMALL line centred on the page, shortened with "..." to fit. */
static void CustomContentView_Line(const char *text, int y, int flags, uint32_t *ot)
{
	char line[160];
	size_t n;
	snprintf(line, sizeof line, "%s", text);
	n = strlen(line);
	while (n > 0 && DecalFont_GetLineWidth(line, FONT_SMALL) > CC_VIEW_TEXT_W)
	{
		n--;
		if (n + 4 > sizeof line) continue;
		memcpy(line + n, "...", 4);
	}
	DecalFont_DrawLineOT(line, 0x100, y, FONT_SMALL, JUSTIFY_CENTER | flags, ot);
}

/* Up to `lines` centred lines, word-wrapped by width; "..." if text remains. */
static void CustomContentView_Wrapped(const char *text, int y, int lines, int flags, uint32_t *ot)
{
	char line[200];
	int i;
	for (i = 0; i < lines && *text; i++)
	{
		size_t n = 0, cut = 0;
		while (text[n] && text[n] != '\n' && n + 4 < sizeof line)
		{
			line[n] = text[n];
			line[n + 1] = '\0';
			if (DecalFont_GetLineWidth(line, FONT_SMALL) > CC_VIEW_TEXT_W) break;
			if (text[n] == ' ') cut = n;
			n++;
		}
		if (text[n] && text[n] != '\n' && cut > 0 && cut >= n / 2) n = cut;
		line[n] = '\0';
		text += n;
		while (*text == ' ' || *text == '\n') text++;
		if (*text && i + 1 == lines)
		{
			for (;;)
			{
				memcpy(line + n, "...", 4);
				if (DecalFont_GetLineWidth(line, FONT_SMALL) <= CC_VIEW_TEXT_W || n == 0) break;
				n--;
			}
		}
		DecalFont_DrawLineOT(line, 0x100, y + i * CC_VIEW_LINE_H, FONT_SMALL, JUSTIFY_CENTER | flags, ot);
	}
}

static void CustomContentView_Draw(const struct CustomContentView *v, uint32_t *ot)
{
	char line[200];
	int i;

	DecalFont_DrawLineOT("Custom Content", 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
	CustomContentView_Line(v->title, 0x34, WHITE, ot);
	snprintf(line, sizeof line, "v%s   %s   Project Saphi", v->version, v->author);
	CustomContentView_Line(line, 0x40, WHITE, ot);
	snprintf(line, sizeof line, "Status: %s", v->state);
	CustomContentView_Line(line, 0x4C, v->required ? RED : ORANGE, ot);
	snprintf(line, sizeof line, "Seed: %s", v->seed);
	CustomContentView_Line(line, 0x58, v->required ? RED : ORANGE, ot);
	CustomContentView_Line(v->relic, 0x64, WHITE, ot);
	if (!v->editing)
	{
		CustomContentView_Line("Select: Import track files", 0x70, ORANGE, ot);
		for (i = 0; i < v->actionCount; i++)
		{
			const int column = i / v->actionRows;
			const int y = CC_VIEW_ACTION_Y + (i % v->actionRows) * CC_VIEW_LINE_H;
			const int x = column == 0 ? CC_VIEW_ACTION_X0 : CC_VIEW_ACTION_X1;
			DecalFont_DrawLineOT((char *)v->actions[i], x, y, FONT_SMALL, ORANGE, ot);
			if (i == v->selected)
			{
				RECT sel = {x - 8, y - 2, column == 0 ? CC_VIEW_ACTION_X1 - 8 - (x - 8) : CC_VIEW_EDGE_R - (x - 8),
				            CC_VIEW_LINE_H};
				CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
			}
		}
	}
	CustomContentView_Wrapped(v->message, CC_VIEW_MESSAGE_Y, 3, v->required ? RED : WHITE, ot);

	/* Behind everything above: drawn last, like the Track Manager's panels. */
	{
		RECT panel = {0x14, 0x31, 0x1D8, 0x9E};
		RECTMENU_DrawInnerRect(&panel, 0, ot);
	}
}
