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
     5. every page draws the dimmed panel behind its text,
     6. a set option's checklist (A2-41): every row name drawn in full with
        its "default" mark and On / Off, Cross toggles, help and back return
        to the right level, and a guard holds Continue and opens its row. */
#include <stdio.h>
#include "menu-label-stub.h"
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
#define CONN_PAD_COMMIT (RAW_BTN_CROSS | RAW_BTN_CIRCLE)
#define CONN_PAD_CANCEL (RAW_BTN_TRIANGLE | RAW_BTN_START)
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
	"One-Lap Cup Races",
	/* Alpha2: every option is a row; this group has more rows than the list holds. */
	"Final Challenge Relic Count", "Wumpa Shop Prices", "Boss Attack Intensity", "Cup Custom Legs",
	"Goal Wumpa", "Starting Stat Class", "Warp Pad Item Display", "Pad Approach Hints", "Race Type Selector",
	"Progressive Starting Wumpa", "Shortcut Knowledge", "Garage Requirement Level", "Wumpa Check",
	"Editable Stats",
	/* A2-41: a set option, a checklist row. A3-22: a weights option, a number
	   page with one row per trap. */
	"Crystal Arenas", "Trap Weights"};
static const char *const optValue[] = {
	"101_percent", "4", "5", "18_sapphire_relics", "true", "false", "random_without_4_keys", "standard",
	"16", "false", "shared_global", "per_character", "medium", "false", "locations_and_items", "false",
	"100", "true",
	"54", "400", "very_strong_attacks", "4", "5000", "per_character_ranking", "one_pile", "false", "always",
	"10", "easy", "very_high", "per_track", "per_character", "4 of 7", "2 custom"};
#define OPTS ((int)(sizeof optLabel / sizeof optLabel[0]))
#define SET_OPT (OPTS - 2)
#define WEIGHTS_OPT (OPTS - 1)
/* The longest checklist labels of the overlay, and more rows than the list
   holds (Boss Track Pool has 19). The trap rows reuse them. */
static const char *const itemLabel[] = {
	"Rocky Road", "Rampage Ruins", "Skull Rock", "Nitro Court", "Parking Lot", "The North Bowl", "Lab Basement",
	"Finish on Podium", "Finish Any Place", "Crash Bandicoot", "Penta Penguin", "Hot Air Skyway",
	"Papu's Pyramid", "Sewer Speedway", "Slide Coliseum", "Invisibility", "Super Engine", "Nitros Oxide",
	"Cortex Vortex"};
#define ITEMS ((int)(sizeof itemLabel / sizeof itemLabel[0]))
static int itemOn[ITEMS] = {1, 1, 1, 1};
static int itemWeight[ITEMS] = {5, 0, 3, 4, 2, 6, 5, 3, 2, 0, 4, 3, 5, 4, 3, 2, 3, 1, 2};
static int itemDefaultWeight[ITEMS] = {5, 5, 2, 4, 3, 6, 4, 5, 4, 3, 3, 3, 2, 5, 4, 3, 0, 3, 3};
static int blockedOpt = -1;
static const char *const guardReason =
	"Signature profiles need TNT, Potion and Bomb checked."; /* the longest guard reason */
static char note[AP_SOLO_TEXT];
static int rooms, tracks, needsLaps, picked, jobState = AP_SOLO_JOB_IDLE, cancels;
static int shortLetters;
static char inbox[512];
int AP_Solo_RuntimePresent(void) { return 1; }
int ap_net_dial_blocked(void) { return 0; } // Box Author Mode off (ap/ap_author_gate.h)
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
void AP_Solo_JobCancel(void) { cancels++; }
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
	if (i == 12 && shortLetters) snprintf(o->value, sizeof o->value, "low");
	o->changed = i == 12 && shortLetters;
	snprintf(o->group, sizeof o->group, "%s", i < 4 ? "Goal" : i < 13 ? "Progression" : i < 18 ? "Extras" : "More Options");
	if (i == 1) snprintf(o->note, sizeof o->note, "%s", note);
	if (i == SET_OPT) o->kind = AP_SOLO_OPT_SET;
	if (i == WEIGHTS_OPT) o->kind = AP_SOLO_OPT_WEIGHTS;
	if (i == blockedOpt)
	{
		o->blocked = 1;
		snprintf(o->note, sizeof o->note, "%s", guardReason);
	}
	o->visible = 1;
	return 1;
}
int AP_Solo_OptionItemCount(int i) { return i == SET_OPT || i == WEIGHTS_OPT ? ITEMS : 0; }
int AP_Solo_OptionItemGet(int i, int k, struct APSoloItemInfo *o)
{
	memset(o, 0, sizeof *o);
	if ((i != SET_OPT && i != WEIGHTS_OPT) || k < 0 || k >= ITEMS) return 0;
	snprintf(o->label, sizeof o->label, "%s", itemLabel[k]);
	if (i == WEIGHTS_OPT)
	{
		o->weight = itemWeight[k];
		o->defaultWeight = itemDefaultWeight[k];
		o->on = itemWeight[k] > 0;
		o->isDefault = itemWeight[k] == itemDefaultWeight[k];
		return 1;
	}
	o->on = itemOn[k];
	o->isDefault = k < 4;
	return 1;
}
void AP_Solo_OptionItemToggle(int i, int k) { if (i == SET_OPT && k >= 0 && k < ITEMS) itemOn[k] = !itemOn[k]; }
void AP_Solo_OptionItemStep(int i, int k, int d)
{
	if (i != WEIGHTS_OPT || k < 0 || k >= ITEMS) return;
	itemWeight[k] += d > 0 ? 1 : -1;
	if (itemWeight[k] < 0) itemWeight[k] = 100;
	if (itemWeight[k] > 100) itemWeight[k] = 0;
}
void AP_Solo_OptionItemReset(int i, int k)
{
	if (i == WEIGHTS_OPT && k >= 0 && k < ITEMS) itemWeight[k] = itemDefaultWeight[k];
}
int AP_Solo_OptionsBlocked(void) { return blockedOpt; }
void AP_Solo_OptionStep(int i, int d) { (void)d; if (i == 12) shortLetters = !shortLetters; }
const char *AP_Solo_OptionHelp(int i)
{
	if (i == 5)
		return "One. Two sentences that wrap. Three has more words to fill the page. Four goes on and on. "
		       "Five keeps writing until the help text is longer than the page holds. Six. Seven is a long "
		       "line again with plenty of words. Eight and nine and ten. Eleven goes on for a while as well, "
		       "with more and more words. Then the next sentence adds yet more words so that it needs many lines. "
		       "Twelve is the end of it.";
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
	nboxes = 0; panels = 0; drawn[0] = '\n'; drawn[1] = 0;
	MM_Solo_Frame(NULL, &pad);
	no_overlap(where);
	CHECKM(panels == 1, where);
	CHECKM(s_soloPage == SOLO_BUSY || strstr(drawn, "BACK") || strstr(drawn, "OK") || strstr(drawn, "CANCEL"), where);
}

/* A key press that changes the page: the frame draws nothing (Back and page
   changes own their frame), so nothing is checked. */
static void tap(int buttons)
{
	struct GamepadBuffer pad = {0};
	pad.buttonsTapped = buttons;
	MM_Solo_Frame(NULL, &pad);
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

	/* Options, first level: the groups and Continue. */
	{
		static char seen[32768];
		static const char *const groupName[] = {"Goal", "Progression", "Extras", "More Options"};
		static const int groupFirst[] = {0, 4, 13, 18}, groupEnd[] = {4, 13, 18, OPTS};
		AP_Solo_StartGet(0, &s_soloStart);
		MM_Solo_Go(SOLO_OPTIONS);
		frame(0, "option groups");
		CHECK(full_once("Goal", drawn) && full_once("More Options", drawn) && full_once("Continue", drawn));
		CHECK(full_once("4 options", drawn) && full_once("16 options", drawn));
		CHECK(strstr(drawn, "DeathLink") == NULL && strstr(drawn, "...") == NULL);
		for (int g = 0; g < 4; g++)
		{
			const int rowsInGroup = groupEnd[g] - groupFirst[g];
			s_soloPage = SOLO_OPTIONS; s_soloGroup = -1; s_soloSel = g; s_soloTop = 0;
			tap(BTN_CROSS);
			CHECK(s_soloGroup == g);
			seen[0] = '\n'; seen[1] = 0;
			frame(0, "group page");
			CHECK(full_once(groupName[g], drawn)); /* the title */
			strcat(seen, drawn);
			/* Down through the group and past its end (wraps), then up. */
			for (int i = 0; i < rowsInGroup + 1; i++)
			{
				frame(BTN_DOWN, "group down");
				strcat(seen, drawn);
				CHECKM(full_once(optLabel[groupFirst[g] + s_soloSel], drawn), optLabel[groupFirst[g] + s_soloSel]);
			}
			for (int i = 0; i < rowsInGroup + 1; i++)
			{
				frame(BTN_UP, "group up");
				strcat(seen, drawn);
				CHECKM(full_once(optLabel[groupFirst[g] + s_soloSel], drawn), optLabel[groupFirst[g] + s_soloSel]);
			}
			for (int i = groupFirst[g]; i < groupEnd[g]; i++)
			{
				CHECKM(full_once(optLabel[i], seen), optLabel[i]);
				CHECKM(full_once(optValue[i], seen), optValue[i]);
			}
			CHECK(strstr(seen, "...") == NULL); /* no name or value is cut */
			if (strstr(seen, "...")) { const char *c = strstr(seen, "..."); while (c > seen && c[-1] != '\n') c--; printf("  cut: %.60s\n", c); }
			/* Triangle returns to the group list with the group selected. */
			tap(BTN_TRIANGLE);
			CHECK(s_soloGroup == -1 && s_soloSel == g && s_soloPage == SOLO_OPTIONS);
		}
		/* A warning note wraps over the two help lines. */
		snprintf(note, sizeof note, "Without Oxide as the goal, set Bosses or Gems Required above 0");
		s_soloPage = SOLO_OPTIONS; s_soloGroup = 0; s_soloSel = 1; s_soloTop = 0;
		frame(0, "options note");
		CHECK(strstr(drawn, "above 0\n") != NULL);
		note[0] = 0;
		/* Continue (last row of the group list) goes on to the review. */
		s_soloPage = SOLO_OPTIONS; s_soloGroup = -1; s_soloSel = 4; s_soloTop = 0;
		frame(0, "options continue");
		CHECK(strstr(drawn, "Continue to the custom tracks") != NULL);
		tap(BTN_CROSS);
		CHECK(s_soloPage == SOLO_REVIEW && s_soloFromOptions == 1);
		/* Back from the review lands on Continue in the group list. */
		tap(BTN_TRIANGLE);
		CHECK(s_soloPage == SOLO_OPTIONS && s_soloGroup == -1 && s_soloSel == 4);
		/* A changed value shows as "1 changed" in its group. */
		shortLetters = 1;
		frame(0, "changed group");
		CHECK(full_once("1 changed", drawn));
		/* A value that grows from one line to two at the list bottom of
		   Progression (its ninth row). */
		s_soloGroup = 1; s_soloSel = 8; s_soloTop = 3;
		frame(0, "short value at bottom");
		CHECK(full_once(optLabel[12], drawn));
		frame(BTN_RIGHT, "growing selected value");
		CHECK(full_once(optLabel[12], drawn));
		CHECK(full_once(optValue[12], drawn));
		shortLetters = 0;
		/* An option error opens the group of the refused option. */
		s_soloGroup = -1; s_soloSel = 4; s_soloHighlight = 14;
		frame(0, "option error");
		CHECK(s_soloGroup == 2 && full_once(optLabel[14], drawn));

		/* A2-41: the set row opens its checklist. */
		{
			static char seenItems[16384];
			const int setRow = SET_OPT - groupFirst[3];
			s_soloPage = SOLO_OPTIONS; s_soloGroup = 3; s_soloList = -1; s_soloSel = setRow; s_soloTop = 0;
			frame(0, "set row");
			CHECK(full_once("Crystal Arenas", drawn) && full_once("4 of 7", drawn));
			CHECK(strstr(drawn, "*: OPEN LIST") != NULL);
			tap(BTN_CROSS);
			CHECK(s_soloList == SET_OPT && s_soloSel == 0 && s_soloTop == 0);
			frame(0, "checklist");
			CHECK(full_once("Crystal Arenas", drawn)); /* the title */
			CHECK(full_once("default", drawn) && full_once("On", drawn) && full_once("Off", drawn));
			CHECK(strstr(drawn, "4 of 19 on.") != NULL && strstr(drawn, "*: ON/OFF") != NULL);
			seenItems[0] = '\n'; seenItems[1] = 0;
			strcat(seenItems, drawn);
			for (int i = 0; i < ITEMS + 1; i++)
			{
				frame(BTN_DOWN, "checklist down");
				strcat(seenItems, drawn);
				CHECKM(full_once(itemLabel[s_soloSel], drawn), itemLabel[s_soloSel]);
			}
			for (int i = 0; i < ITEMS; i++)
				CHECKM(full_once(itemLabel[i], seenItems), itemLabel[i]);
			CHECK(strstr(seenItems, "...") == NULL);
			/* Cross toggles the selected row (Down past the end wrapped to row 1). */
			CHECK(s_soloSel == 1 && itemOn[1]);
			frame(BTN_CROSS, "checklist toggle");
			CHECK(!itemOn[1] && strstr(drawn, "3 of 19 on.") != NULL);
			frame(BTN_RIGHT, "checklist toggle right");
			CHECK(itemOn[1]);
			/* Help and back: the checklist again, then the group with the row. */
			tap(BTN_SQUARE);
			CHECK(s_soloPage == SOLO_HELP && s_soloHelpRow == SET_OPT);
			tap(BTN_TRIANGLE);
			CHECK(s_soloPage == SOLO_OPTIONS && s_soloList == SET_OPT);
			frame(0, "checklist after help");
			tap(BTN_TRIANGLE);
			CHECK(s_soloPage == SOLO_OPTIONS && s_soloList == -1 && s_soloGroup == 3 && s_soloSel == setRow);
			/* A guard holds Continue: the group shows "1 to fix" and Continue
			   opens the refused row with its reason in red. */
			blockedOpt = SET_OPT;
			tap(BTN_TRIANGLE);
			s_soloSel = 4; s_soloTop = 0;
			frame(0, "blocked groups");
			CHECK(full_once("1 to fix", drawn) && strstr(drawn, "Continue opens it.") != NULL);
			tap(BTN_CROSS);
			CHECK(s_soloPage == SOLO_OPTIONS && s_soloHighlight == SET_OPT);
			frame(0, "blocked row");
			CHECK(s_soloGroup == 3 && s_soloSel == setRow && s_soloList == -1);
			CHECK(strstr(drawn, "Bomb checked.\n") != NULL);
			tap(BTN_CROSS);
			frame(0, "blocked checklist");
			CHECK(s_soloList == SET_OPT && strstr(drawn, "Bomb checked.\n") != NULL);
			blockedOpt = -1;
			tap(BTN_TRIANGLE);
			tap(BTN_TRIANGLE);
			s_soloSel = 4; s_soloTop = 0;
			tap(BTN_CROSS);
			CHECK(s_soloPage == SOLO_REVIEW);
		}

		/* A3-22: the weights row opens its number page, one row per trap. */
		{
			static char seenWeights[16384];
			const int weightsRow = WEIGHTS_OPT - groupFirst[3];
			s_soloPage = SOLO_OPTIONS; s_soloGroup = 3; s_soloList = -1; s_soloSel = weightsRow; s_soloTop = 0;
			frame(0, "weights row");
			CHECK(full_once("Trap Weights", drawn) && full_once("2 custom", drawn));
			CHECK(strstr(drawn, "*: OPEN WEIGHTS") != NULL);
			tap(BTN_CROSS);
			CHECK(s_soloList == WEIGHTS_OPT && s_soloSel == 0 && s_soloTop == 0);
			frame(0, "weights page");
			CHECK(full_once("Trap Weights", drawn)); /* the title */
			CHECK(strstr(drawn, "of 19 above 0. SELECT: help.") != NULL);
			CHECK(strstr(drawn, "LEFT/RIGHT: CHANGE") != NULL && strstr(drawn, "[: RESET") != NULL);
			/* The grey default weight and the row's own weight are drawn. */
			CHECK(full_once("5", drawn) && full_once("0", drawn));
			seenWeights[0] = '\n'; seenWeights[1] = 0;
			strcat(seenWeights, drawn);
			for (int i = 0; i < ITEMS + 1; i++)
			{
				frame(BTN_DOWN, "weights down");
				strcat(seenWeights, drawn);
				CHECKM(full_once(itemLabel[s_soloSel], drawn), itemLabel[s_soloSel]);
			}
			for (int i = 0; i < ITEMS; i++)
				CHECKM(full_once(itemLabel[i], seenWeights), itemLabel[i]);
			CHECK(strstr(seenWeights, "...") == NULL);
			/* Left and Right step the selected weight (Down past the end wrapped to row 1). */
			CHECK(s_soloSel == 1 && itemWeight[1] == 0);
			frame(BTN_LEFT, "weight wraps to the maximum");
			CHECK(itemWeight[1] == 100);
			frame(BTN_RIGHT, "weight wraps to zero");
			CHECK(itemWeight[1] == 0);
			/* Confirm never resets. Square needs two presses on the same row. */
			itemWeight[1] = 77;
			frame(BTN_CROSS, "weight confirm"); CHECK(itemWeight[1] == 77);
			frame(BTN_CIRCLE, "weight confirm alias"); CHECK(itemWeight[1] == 77);
			frame(BTN_SQUARE, "weight reset armed"); CHECK(itemWeight[1] == 77);
			CHECK(strstr(drawn, "AGAIN TO RESET") != NULL);
			frame(0, "weight reset release");
			frame(BTN_SQUARE, "weight reset");
			CHECK(itemWeight[1] == itemDefaultWeight[1]);
			frame(BTN_SQUARE, "arm before change");
			frame(BTN_RIGHT, "disarm on value change");
			int changedWeight = itemWeight[1];
			frame(BTN_SQUARE, "arm again"); CHECK(itemWeight[1] == changedWeight);
			frame(BTN_DOWN, "disarm on row change");
			frame(BTN_UP, "return to row");
			frame(BTN_SQUARE, "arm after navigation"); CHECK(itemWeight[1] == changedWeight);
			/* Help and back: the page again, then the group with the row. */
			tap(BTN_SELECT);
			CHECK(s_soloPage == SOLO_HELP && s_soloHelpRow == WEIGHTS_OPT);
			tap(BTN_TRIANGLE);
			CHECK(s_soloPage == SOLO_OPTIONS && s_soloList == WEIGHTS_OPT);
			frame(0, "weights after help");
			tap(BTN_TRIANGLE);
			CHECK(s_soloPage == SOLO_OPTIONS && s_soloList == -1 && s_soloGroup == 3 && s_soloSel == weightsRow);
		}
	}

	/* An option's help page: a name too long for the big font is not cut. */
	s_soloHelpRow = 3; s_soloPage = SOLO_HELP; s_soloHelpTop = 0;
	frame(0, "help");
	CHECK(strstr(drawn, "Oxide's Final Challenge Unlock\n") != NULL);
	/* A long help text scrolls: the first line goes, the last one arrives. */
	s_soloHelpRow = 5; s_soloHelpTop = 0;
	frame(0, "long help");
	CHECK(strstr(drawn, "UP/DOWN: SCROLL") != NULL && strstr(drawn, "One. Two") != NULL && strstr(drawn, "Twelve") == NULL);
	for (int i = 0; i < 30; i++) frame(BTN_DOWN, "long help down");
	CHECK(strstr(drawn, "of it.") != NULL && strstr(drawn, "One. Two") == NULL);
	CHECK(strstr(drawn, "...") == NULL);
	for (int i = 0; i < 30; i++) frame(BTN_UP, "long help up");
	CHECK(strstr(drawn, "One. Two") != NULL);

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
	for (int i = 0; i < 2; ++i) {
		const int backs[] = {BTN_TRIANGLE, BTN_START};
		jobState = AP_SOLO_JOB_RUNNING; s_soloBusyJob = SOLO_JOB_GENERATE; s_soloPage = SOLO_BUSY;
		int before = cancels;
		frame(BTN_CIRCLE, "busy confirm does not cancel"); CHECK(cancels == before && s_soloPage == SOLO_BUSY);
		frame(backs[i], "busy cancel"); CHECK(cancels == before + 1 && s_soloPage == SOLO_REVIEW);
	}
	jobState = AP_SOLO_JOB_IDLE;
	s_soloPage = SOLO_SESSION; frame(0, "session");
	snprintf(s_soloMsg, sizeof s_soloMsg, "The path of that file is too long. Move it to a shorter folder, or use the inbox.");
	s_soloPage = SOLO_MESSAGE; frame(0, "message");

	menuTestLetters = true;
	MM_Solo_Go(SOLO_HOME); frame(0, "Xbox home hints");
	CHECK(strstr(drawn, "A: PLAY") && strstr(drawn, "X: DETAILS") && strstr(drawn, "Y: BACK"));
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
