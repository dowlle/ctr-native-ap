// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_AP -DCTR_NATIVE -DBUILD=926 -I include -I . -I ap tools/test-solo-layout.c -o /tmp/test-solo-layout
/* Play Solo layout bounds (Deck menu scan, 2026-11-03). Drives the production
   pages of game/230/MM_Solo.c with stub data and the retail font widths, and
   checks every text and box:
     1. stays inside the menu frame (x 0x18 to 0x1E8, above the frame bottom),
     2. never overlaps another text, and nothing but the title sits above the
        title rule (0x2D),
     3. on the options screens: the full name and value of every overlay
        option is drawn, never cut ("PROGRESSIVE BO..." against
        "PROGRESSIVE ST..."), scrolling through every row,
     4. the inbox path is shortened from the left and keeps its end,
     5. every page draws the dimmed panel behind its text. */
#include <stdio.h>
#include <string.h>
#include <common.h>
#include <platform/native_input.h>
#include "ap_solo.h"
struct sData sdata_static;
struct Data data;
NativeConfig g_config;
#define DecalFont_DrawLineOT UnusedDraw
#include "../game/DecalFont.c"
#undef DecalFont_DrawLineOT

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
#define CHECKM(x, m) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s (%s)\n", __LINE__, #x, m); } } while (0)

/* ---- drawing stubs: record each text box ---- */
struct Box { int x, y, w, h; char text[200]; };
static struct Box boxes[64];
static int nboxes, panels, page;
static char drawn[8192];
void DecalFont_DrawLineOT(char *s, int x, int y, s16 f, int flags, uint32_t *ot)
{
	int w = DecalFont_GetLineWidth(s, f);
	(void)ot;
	CHECK(panels == 0); /* Later OT insertion puts the panel behind text. */
	if (flags & JUSTIFY_CENTER) x -= w / 2;
	else if (flags & JUSTIFY_RIGHT) x -= w;
	CHECKM(x >= 0x18 && x + w <= 0x1E8, s);
	CHECKM(y + (f == FONT_BIG ? 17 : 8) <= 0xCE, s);
	CHECKM(f == FONT_BIG || y == 0x1C || y >= 0x2F, s); /* below the title rule */
	if (nboxes < 64)
	{
		struct Box *b = &boxes[nboxes++];
		b->x = x; b->y = y; b->w = w; b->h = f == FONT_BIG ? 17 : 8;
		snprintf(b->text, sizeof b->text, "%s", s);
	}
	if (strlen(drawn) + strlen(s) + 2 < sizeof drawn) { strcat(drawn, s); strcat(drawn, "\n"); }
}
void RECTMENU_DrawInnerRect(RECT *r, int t, uint32_t *ot)
{
	(void)t; (void)ot;
	CHECK(r->x >= 0x10 && r->x + r->w <= 0x1F0 && r->y >= 0x2E && r->y + r->h <= 0xC2);
	panels++;
}
void CTR_Box_DrawClearBox(const RECT *r, const Color *c, int t, uint32_t *ot)
{
	(void)c; (void)t; (void)ot;
	CHECK(panels == 0); /* Highlights also remain in front of the panel. */
	CHECK(r->x >= 0x18 && r->x + r->w <= 0x1E8 && r->y >= 0x30 && r->y + r->h <= 0xC2);
}
int OtherFX_Play(u32 a, int b) { (void)a; (void)b; return 0; }
int Platform_OpenURL(const char *u) { (void)u; return 1; }
int Platform_InputRawGamepadButtons(void) { return 0; }
static int textResult, textCap;
int NativeText_Result(void) { return textResult; }
void NativeText_Resolve(int r) { textResult = r; }
void NativeText_End(void) { textResult = 0; }
void NativeText_Begin(char *b, int c, int x, int y, int w, int h, int p)
{ (void)b; textCap = c; (void)w; (void)h; (void)p; CHECK(x >= 0x20 && y >= 0x30); }
#define CONN_PAD_COMMIT (RAW_BTN_CROSS | RAW_BTN_START)
#define CONN_PAD_CANCEL RAW_BTN_TRIANGLE
#define CONN_ROW_X 0x30
#define CONN_ROW_W 0x1B0
#define CONN_ROW_H 0x0C

/* ---- Solo runtime stubs ---- */
/* The overlay's options (worlds/ctr/solo/options-overlay.json) with their
   display names and longest values. */
static const char *const optLabel[] = {
	"Oxide Goal", "Bosses Required Goal", "Gems Required Goal", "Oxide's Final Challenge Unlock",
	"Shuffle Keys", "Shuffle Gems", "Warp Pad Unlock Requirements", "Two-Stage Gate Density",
	"Racer-Locked Warp Pads", "Character Unlocks", "Progressive Boost", "Progressive Stats",
	"Logic Difficulty", "Itemsanity", "Lettersanity", "Item Box Locations", "Trap Fill Percentage",
	"One-Lap Cup Races"};
static const char *const optValue[] = {
	"101_percent", "4", "5", "18_sapphire_relics", "true", "false", "random_without_4_keys", "standard",
	"16", "false", "shared_global", "per_character", "medium", "false", "locations_and_items", "false",
	"100", "true"};
#define OPTS ((int)(sizeof optLabel / sizeof optLabel[0]))
static char note[AP_SOLO_TEXT];
static int rooms, tracks, needsLaps, picked, jobState = AP_SOLO_JOB_IDLE;
static int shortLetters;
static char inbox[512];
int AP_Solo_RuntimePresent(void) { return 1; }
int AP_Solo_ListStart(void) { return 0; }
int AP_Solo_DescribeStart(const char *y) { (void)y; return 0; }
int AP_Solo_ImportRoomStart(const char *p) { (void)p; return 0; }
int AP_Solo_ImportYamlStart(const char *p) { (void)p; return 0; }
int AP_Solo_TracksStart(const char *y) { (void)y; return 0; }
int AP_Solo_GenerateStart(const char *a, const char *b, const char *c, int d) { (void)a; (void)b; (void)c; (void)d; return 0; }
int AP_Solo_JobState(void) { return jobState; }
const char *AP_Solo_JobPhase(void) { return "Filling the multiworld with items, this takes a few seconds"; }
const char *AP_Solo_JobErrorText(void) { return ""; }
const char *AP_Solo_JobErrorOption(void) { return ""; }
const char *AP_Solo_JobResultRoom(void) { return ""; }
void AP_Solo_JobCancel(void) {}
void AP_Solo_JobClear(void) {}
int AP_Solo_RoomCount(void) { return rooms; }
int AP_Solo_RoomGet(int i, struct APSoloRoomInfo *o)
{
	memset(o, 0, sizeof *o);
	snprintf(o->roomId, sizeof o->roomId, "room%d", i);
	snprintf(o->slot, sizeof o->slot, "AppieCTR with a long slot name %d", i);
	snprintf(o->seedName, sizeof o->seedName, "AP_12345678901234567890123456789");
	snprintf(o->source, sizeof o->source, "full_adventure");
	snprintf(o->created, sizeof o->created, "2026-11-03 00:01");
	snprintf(o->worldVersion, sizeof o->worldVersion, "0.3.0-rc1-deck-1");
	o->checksDone = 12; o->checksTotal = 452; o->versionMatches = i != 1;
	return 1;
}
int AP_Solo_StartCount(void) { return 3; }
int AP_Solo_StartGet(int i, struct APSoloStartInfo *o)
{
	memset(o, 0, sizeof *o);
	snprintf(o->label, sizeof o->label, "%s", i == 0 ? "Full Adventure" : i == 1 ? "Standard" : "my-imported-yaml-with-a-long-name.yaml");
	o->isPreset = i < 2;
	return 1;
}
const char *AP_Solo_InboxPath(void) { return inbox; }
int AP_Solo_RoomDelete(const char *r) { (void)r; return 1; }
int AP_Solo_OptionCount(void) { return OPTS; }
int AP_Solo_OptionGet(int i, struct APSoloOptionInfo *o)
{
	memset(o, 0, sizeof *o);
	if (i < 0 || i >= OPTS) return 0;
	snprintf(o->label, sizeof o->label, "%s", optLabel[i]);
	snprintf(o->value, sizeof o->value, "%s", optValue[i]);
	if (i == 14 && shortLetters) snprintf(o->value, sizeof o->value, "false");
	snprintf(o->group, sizeof o->group, "%s", i < 4 ? "Goal" : i < 13 ? "Progression" : "Extras");
	if (i == 1) snprintf(o->note, sizeof o->note, "%s", note);
	o->visible = 1;
	return 1;
}
void AP_Solo_OptionStep(int i, int d) { (void)d; if (i == 14) shortLetters = !shortLetters; }
const char *AP_Solo_OptionHelp(int i)
{
	(void)i;
	return "Which relics open Oxide's Final Challenge. A long help text wraps over several lines and stays "
	       "inside the screen, however long it gets, because the page wraps it by width.";
}
int AP_Solo_OptionIndex(const char *n) { (void)n; return -1; }
void AP_Solo_OptionsReset(void) {}
int AP_Solo_TrackCount(void) { return tracks; }
int AP_Solo_TrackGet(int i, struct APSoloTrackInfo *o)
{
	memset(o, 0, sizeof *o);
	if (i < 0 || i >= tracks) return 0;
	snprintf(o->title, sizeof o->title, "Arabian Heights Extended Remix %d", i);
	snprintf(o->revision, sizeof o->revision, "1.0.2");
	snprintf(o->author, sizeof o->author, "A very long author display name");
	snprintf(o->modes, sizeof o->modes, "Trophy, CTR, Relic");
	o->pick = i % 3;
	o->needsLaps = needsLaps && i == 0;
	o->laps = 7;
	return 1;
}
void AP_Solo_TrackCycle(int i) { (void)i; }
int AP_Solo_TracksPicked(void) { return picked; }
void AP_Solo_TracksClear(void) {}
int AP_Solo_TracksViaOptions(int p) { return p; }
void AP_Solo_TrackStepLaps(int i, int d) { (void)i; (void)d; }
int AP_Solo_TrackKeepLaps(int i) { (void)i; return 1; }
int AP_Solo_PickStart(int r) { (void)r; return 0; }
int AP_Solo_PickTake(char *p, int n) { (void)p; (void)n; return 0; }
int AP_Solo_SessionStart(const char *r) { (void)r; return 1; }
void AP_Solo_SessionStop(void) {}
int AP_Solo_SessionActive(void) { return 0; }
static char testPassword[64];
const char *AP_Solo_Password(void) { return testPassword; }
int AP_Solo_SetPassword(const char *p)
{
	snprintf(testPassword, sizeof testPassword, "%s", p);
	return 1;
}
const char *AP_Solo_SessionRoom(void) { return ""; }
int AP_Solo_SessionState(void) { return AP_SOLO_SESSION_FAILED; }
const char *AP_Solo_SessionError(void)
{ return "The solo server stopped before it was ready. See solo/logs/server.log for the reason."; }

#include "../game/230/MM_Solo.c"

/* No two texts overlap. */
static void no_overlap(const char *where)
{
	for (int i = 0; i < nboxes; i++)
		for (int j = i + 1; j < nboxes; j++)
		{
			const struct Box *a = &boxes[i], *b = &boxes[j];
			const int hit = a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
			if (hit) printf("  %s: \"%s\" overlaps \"%s\"\n", where, a->text, b->text);
			CHECKM(!hit, where);
		}
}

static void frame(int buttons, const char *where)
{
	struct GamepadBuffer pad = {0};
	pad.buttonsTapped = buttons;
	nboxes = 0; panels = 0; drawn[0] = 0;
	MM_Solo_Frame(NULL, &pad);
	no_overlap(where);
	CHECKM(panels == 1, where);
	CHECKM(s_soloPage == SOLO_BUSY || strstr(drawn, "BACK") || strstr(drawn, "OK") || strstr(drawn, "CANCEL"), where);
}

static int full_once(const char *text, const char *all)
{
	/* `text` drawn in full as one line. */
	char needle[200];
	snprintf(needle, sizeof needle, "\n%s\n", text);
	return strstr(all, needle) != NULL;
}

int main(void)
{
	data.font_charPixWidth[FONT_SMALL] = 13; data.font_charPixWidth[FONT_BIG] = 17;
	data.font_puncPixWidth[FONT_SMALL] = 7; data.font_puncPixWidth[FONT_BIG] = 11;
	data.font_buttonPixWidth[FONT_SMALL] = 2; data.font_buttonPixWidth[FONT_BIG] = 16;
	snprintf(g_config.slot, sizeof g_config.slot, "AppieCTR");

	/* Home, no seeds: the line sits under the title rule, not on the title. */
	snprintf(inbox, sizeof inbox, "/home/deck/CTR-0.3.0-deck-1-20261102/client/solo/inbox");
	s_soloPage = SOLO_HOME;
	frame(0, "home empty");
	CHECK(strstr(drawn, "No solo seeds yet.") != NULL);
	CHECK(strstr(drawn, "Inbox: ...") != NULL && strstr(drawn, "/solo/inbox\n") != NULL);
	for (int i = 0; i < 4; i++) frame(BTN_DOWN, "home empty nav");
	rooms = 3;
	frame(0, "home rooms");
	for (int i = 0; i < 8; i++) frame(BTN_DOWN, "home rooms nav");

	s_soloRoom.checksDone = 0;
	AP_Solo_RoomGet(1, &s_soloRoom);
	MM_Solo_Go(SOLO_DETAILS);
	frame(0, "details");
	MM_Solo_Go(SOLO_NEW);
	frame(0, "new");

	/* Options: every row seen in full while scrolling down and back up. */
	{
		static char seen[16384];
		AP_Solo_StartGet(0, &s_soloStart);
		MM_Solo_Go(SOLO_OPTIONS);
		seen[0] = '\n'; seen[1] = 0;
		frame(0, "options");
		strcat(seen, drawn);
		for (int i = 0; i < OPTS + 1; i++)
		{
			frame(BTN_DOWN, "options down");
			if (strlen(seen) + strlen(drawn) < sizeof seen) strcat(seen, drawn);
			/* The selected row is always on screen. */
			if (s_soloSel < OPTS) CHECKM(full_once(optLabel[s_soloSel], drawn), optLabel[s_soloSel]);
		}
		for (int i = 0; i < OPTS + 1; i++)
		{
			frame(BTN_UP, "options up");
			if (strlen(seen) + strlen(drawn) < sizeof seen) strcat(seen, drawn);
			if (s_soloSel < OPTS) CHECKM(full_once(optLabel[s_soloSel], drawn), optLabel[s_soloSel]);
		}
		for (int i = 0; i < OPTS; i++)
		{
			CHECKM(full_once(optLabel[i], seen), optLabel[i]);
			CHECKM(full_once(optValue[i], seen), optValue[i]);
		}
		CHECK(strstr(seen, "...") == NULL); /* no name or value is cut */
		if (strstr(seen, "...")) { const char *c = strstr(seen, "..."); while (c > seen && c[-1] != '\n') c--; printf("  cut: %.60s\n", c); }
		CHECK(strstr(seen, "DeathLink") == NULL);
		/* A warning note wraps over the two help lines. */
		snprintf(note, sizeof note, "Without Oxide as the goal, set Bosses or Gems Required above 0");
		s_soloSel = 1; s_soloTop = 0;
		frame(0, "options note");
		CHECK(strstr(drawn, "above 0\n") != NULL);
		note[0] = 0;
		/* Continue: the AP-Pie line wraps, in full. */
		s_soloSel = OPTS; frame(0, "options continue");
		CHECK(strstr(drawn, "import it.") != NULL);
		/* Lettersanity grows from one line to two at the list bottom. */
		shortLetters = 1;
		s_soloSel = 14; s_soloTop = 7;
		frame(0, "short letters at bottom");
		CHECK(full_once(optLabel[14], drawn));
		frame(BTN_RIGHT, "growing selected value");
		CHECK(full_once(optLabel[14], drawn));
		CHECK(full_once(optValue[14], drawn));
	}

	/* An option's help page: a name too long for the big font is not cut. */
	s_soloHelpRow = 3; s_soloPage = SOLO_HELP;
	frame(0, "help");
	CHECK(strstr(drawn, "Oxide's Final Challenge Unlock\n") != NULL);

	/* Custom Tracks: preset, YAML, a track that needs laps. */
	tracks = 10;
	MM_Solo_Go(SOLO_TRACKS);
	for (int i = 0; i < tracks + 2; i++) frame(BTN_DOWN, "tracks");
	CHECK(strstr(drawn, "Own pad: it gets a pad.") != NULL || strstr(drawn, "it gets a pad.") != NULL);
	needsLaps = 1; s_soloSel = 0; s_soloTop = 0; frame(0, "tracks laps");
	needsLaps = 0;
	AP_Solo_StartGet(2, &s_soloStart);
	s_soloSel = tracks; frame(0, "tracks yaml continue");

	/* Review: "plus 1 custom track" and the YAML name line stay in. */
	picked = 1; s_soloFromOptions = 1;
	MM_Solo_Go(SOLO_REVIEW);
	frame(0, "review");
	CHECK(strstr(drawn, "plus 1 custom") != NULL && strstr(drawn, "track\n") != NULL && !strstr(drawn, "..."));
	AP_Solo_StartGet(0, &s_soloStart); s_soloFromOptions = 0; picked = 3;
	frame(0, "review preset");
	/* A configured name can be longer than the text editor's normal limit. */
	memset(s_soloName, 'W', sizeof s_soloName - 1);
	s_soloName[sizeof s_soloName - 1] = 0;
	frame(0, "review long configured name");
	CHECK(strstr(drawn, "...") != NULL);
	{
		struct GamepadBuffer editPad = {0};
		s_soloSel = 0; s_soloEditJustEnded = 0;
		editPad.buttonsTapped = BTN_CROSS;
		MM_Solo_PageReview(NULL, &editPad);
		CHECK(textCap == sizeof s_soloName);
		CHECK(strlen(s_soloName) == sizeof s_soloName - 1);
	}
	frame(0, "review long editing name");
	CHECK(strstr(drawn, "_\n") != NULL);
	s_soloEditing = 0;
	MM_Solo_EditPassword(SOLO_REVIEW_NAME_Y + SOLO_ROW_H);
	CHECK(textCap == sizeof s_soloPassword && s_soloEditing == 2);
	snprintf(s_soloPassword, sizeof s_soloPassword, "chosen password");
	NativeText_Resolve(1); MM_Solo_UpdateNameEdit();
	CHECK(!s_soloEditing && !strcmp(AP_Solo_Password(), "chosen password"));
	MM_Solo_EditPassword(SOLO_REVIEW_NAME_Y + SOLO_ROW_H);
	snprintf(s_soloPassword, sizeof s_soloPassword, "discarded edit");
	NativeText_Resolve(2); MM_Solo_UpdateNameEdit();
	CHECK(!strcmp(AP_Solo_Password(), "chosen password"));
	MM_Solo_EditPassword(SOLO_REVIEW_NAME_Y + SOLO_ROW_H);
	s_soloPassword[0] = 0;
	NativeText_Resolve(1); MM_Solo_UpdateNameEdit();
	CHECK(!AP_Solo_Password()[0]);

	jobState = AP_SOLO_JOB_RUNNING; s_soloBusyJob = SOLO_JOB_TRACKS; s_soloPage = SOLO_BUSY;
	frame(0, "busy");
	jobState = AP_SOLO_JOB_IDLE;
	s_soloPage = SOLO_SESSION; frame(0, "session");
	snprintf(s_soloMsg, sizeof s_soloMsg, "The path of that file is too long. Move it to a shorter folder, or use the inbox.");
	s_soloPage = SOLO_MESSAGE; frame(0, "message");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
