#include <common.h>
#include <stdio.h>
#include <ctr_menu_ux.h>
#include <ctr_menu_pages.h>
#include <platform/native_renderer.h> // NativeRenderer_CaptureWindowGeometry, ahead of the NativeConfig_Save calls below
#ifdef CTR_AP
// Platform_InputRawGamepadButtons: physical-pad-only button mask, used by the
// connection manager's controller commit / cancel (not in common.h's platform set).
#include <platform/native_input.h>
#include "../../ap/ap_boxes.h"             // AP_Boxes_ColourRowState: the Item Box Colours row
#include "../../ap/ap_box_colour_logic.h"  // AP_BOX_COLOUR_ROW_SEED_OFF
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_assets.h>
#include <platform/native_custom_track_manager.h>
#ifdef CTR_CUSTOM_PACKAGES
// Box authoring build: the package library and Saphi Track Manager replace the
// single-package Custom Content page (CMakeLists.txt, CTR_CUSTOM_PACKAGES).
#include <stdlib.h>
#include <platform/native_custom_track_library.h>
#include <platform/native_custom_package.h>
#include <platform/native_custom_offline.h>
#include <platform/native_saphi_catalogue.h>
#include "MM_CustomText.c"
#endif
#endif
#endif

// In-game options menu. Ported from thecodingbob/ctr-native (branch
// modularize-improve-config): a two-level RectMenu proc -- a section selector,
// then the entries within the chosen section. Reads/writes g_config; persists to
// config.ini on exit via NativeConfig_Save. The row arrays below extend the main
// menu with an OPTIONS row (string 0x0E == LNG_OPTIONS) and are pointed at by
// MM_MenuProc_Main.

// Row arrays with CONFIG entry at bottom, used by MM_MenuProc_Main. QUIT (#211)
// is appended last, after OPTIONS, reusing LNG_QUIT (0x003) -- already proven as
// a MenuRow stringIndex elsewhere (e.g. game/225.c's battle pause row) rather
// than a title-only string -- so no new label is minted.
struct MenuRow s_rowsMainMenuBasicConfig[] = {
	{0x4C, 0, 1, 0, 0},
	{0x4D, 0, 2, 1, 1},
	{0x4E, 1, 3, 2, 2},
	{0x4F, 2, 4, 3, 3},
	{0x50, 3, 5, 4, 4},
	{0x51, 4, 6, 5, 5},
	{0x0E, 5, 7, 6, 6},
	{LNG_QUIT, 6, 7, 7, 7},
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
	{0x0E, 6, 8, 7, 7},
	{LNG_QUIT, 7, 8, 8, 8},
	{-1},
};

static void MM_MenuProc_Config(struct RectMenu *menu);

// Page lookup built from g_configEntries at first use. A "section" in this menu
// is a page from include/ctr_menu_pages.h, not a config.ini [section]: the page
// table decides where a row is shown, the entry's section where it is saved.
// Audio and State stay config-file-only (see CTR_MenuSectionHidden).
static int s_sectionRows[CTR_MENU_PAGE_CAP][CTR_MENU_PAGE_ROW_CAP];
static int s_sectionCount[CTR_MENU_PAGE_CAP];
static const char *s_sectionName[CTR_MENU_PAGE_CAP];
static int s_numSections = 0;
#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
static int s_customContentSection = -1;
#endif

static void BuildSectionMap(void)
{
	s_numSections = CTR_MenuBuildPages(g_configEntries, g_numConfigEntries,
		s_sectionName, s_sectionRows, s_sectionCount);
#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
	s_customContentSection = s_numSections;
	s_sectionCount[s_numSections] = 0;
	s_sectionName[s_numSections] = "Custom Content";
	s_numSections++;
#endif
}

// Row `row` of page `sec`.
static const ConfigEntry *Section_Entry(int sec, int row)
{
	return &g_configEntries[s_sectionRows[sec][row]];
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

// AI-difficulty preset ladder (CFG_ENUM). The values are the engine's internal
// difficulty scale: 0 keeps vanilla dynamic (trophy-based) scaling; the rest are
// the fixed presets from the reference difficulty menu. The raw value is what gets
// stored + synced, so an out-of-ladder value from slot_data still applies as-is;
// the menu only snaps to the nearest entry for display and left/right stepping.
static const int   s_aiDiffValues[] = {0, 0x50, 0xA0, 0xF0, 0x140, 0x280};
static const char *s_aiDiffNames[]  = {"VANILLA", "EASY", "MEDIUM", "HARD", "SUPER HARD", "ULTRA HARD"};
#define AI_DIFF_COUNT ((int)(sizeof(s_aiDiffValues) / sizeof(s_aiDiffValues[0])))

// Aspect-ratio preset ladder (CFG_ENUM), ported from thecodingbob/ctr-native
// (branch widescreen-option). 0 = 4:3 (vanilla), 1 = 16:9, 2 = 16:10,
// 3 = 21:9. The raw value is what gets stored; the menu snaps out-of-ladder
// values to the nearest preset for display and stepping, same as AI difficulty.
static const int   s_aspectValues[] = {0, 1, 2, 3};
static const char *s_aspectNames[]  = {"4:3", "16:9", "16:10", "21:9"};
#define ASPECT_COUNT ((int)(sizeof(s_aspectValues) / sizeof(s_aspectValues[0])))

// Render-scale ladder (CFG_ENUM). Values are the raw modes the renderer
// consumes (include/platform/native_render_scale.h): 1 = shipped PSX raster
// and VRAM presentation, 2/3/4 = fixed multiples, 0 = window-sized raster.
// Ladder order runs ORIGINAL -> 4X -> NATIVE so stepping right means
// "sharper"; the raw value is what gets stored, and out-of-ladder hand edits
// render and step as ORIGINAL (the renderer clamps them the same way).
static const int   s_renderScaleValues[] = {1, 2, 3, 4, 0};
static const char *s_renderScaleNames[]  = {"ORIGINAL", "2X", "3X", "4X", "NATIVE"};
#define RENDER_SCALE_COUNT ((int)(sizeof(s_renderScaleValues) / sizeof(s_renderScaleValues[0])))

// VSync ladder (CFG_ENUM), see include/platform/native_vsync.h for the raw
// values (NATIVE_VSYNC_OFF/ON/ADAPTIVE) and how they map to the SDL swap
// interval. Stored as the raw option value; an out-of-ladder hand edit
// renders and steps as Off, the safe default.
static const int   s_vsyncValues[] = {0, 1, 2};
static const char *s_vsyncNames[]  = {"OFF", "ON", "ADAPTIVE"};
#define VSYNC_COUNT ((int)(sizeof(s_vsyncValues) / sizeof(s_vsyncValues[0])))

static int Vsync_Index(int value)
{
	for (int i = 0; i < VSYNC_COUNT; i++)
		if (s_vsyncValues[i] == value)
			return i;
	return 0; // out-of-range persisted value renders and steps as OFF
}

static int RenderScale_Index(int value)
{
	for (int i = 0; i < RENDER_SCALE_COUNT; i++)
		if (s_renderScaleValues[i] == value)
			return i;
	return 0; // out-of-range persisted value renders and steps as ORIGINAL
}

static int Aspect_Index(int value)
{
	for (int i = 0; i < ASPECT_COUNT; i++)
		if (s_aspectValues[i] == value)
			return i;
	return 0; // out-of-range persisted value renders and steps as 4:3
}

// Nearest ladder index for an arbitrary value (exact match, else closest).
static int AiDiff_NearestIndex(int value)
{
	int best = 0;
	int bestDist = -1;
	for (int i = 0; i < AI_DIFF_COUNT; i++)
	{
		int d = value - s_aiDiffValues[i];
		if (d < 0)
			d = -d;
		if (bestDist < 0 || d < bestDist)
		{
			bestDist = d;
			best = i;
		}
	}
	return best;
}

static const char *AiDiff_Label(int value)
{
	return s_aiDiffNames[AiDiff_NearestIndex(value)];
}

// Step to the adjacent preset (dir -1/+1), clamped to the ladder ends.
static void AiDiff_Step(int *value, int dir)
{
	int i = AiDiff_NearestIndex(*value) + dir;
	if (i < 0)
		i = 0;
	if (i > AI_DIFF_COUNT - 1)
		i = AI_DIFF_COUNT - 1;
	*value = s_aiDiffValues[i];
}

#ifdef CTR_AP
// DeathLink ladder (CFG_ENUM): follow the seed option / force off / force a
// tier. 1/2 are the CTR_DL_MASK_RESET / CTR_DL_ANY_HIT tier values themselves
// (ap_deathlink.h), so both in-game layers are directly selectable. Edits apply
// live: ap_deathlink.c re-reads the preference every frame and syncs the
// connection tag itself, so this row needs no menu-exit hook.
static const int   s_dlinkValues[] = {-1, 0, 1, 2};
static const char *s_dlinkNames[]  = {"SEED", "OFF", "MASK RESET", "ANY HIT"};
#define DLINK_COUNT ((int)(sizeof(s_dlinkValues) / sizeof(s_dlinkValues[0])))

// Sustained-trap comfort window. Zero is stored for Full race so config.ini
// remains readable and the scheduler does not need a magic large duration.
static const int   s_trapDurationValues[] = {10, 15, 20, 25, 30, 45, 60, 90, 0};
static const char *s_trapDurationNames[]  = {"10 SEC", "15 SEC", "20 SEC", "25 SEC",
	"30 SEC", "45 SEC", "60 SEC", "90 SEC", "FULL RACE"};
#define TRAP_DURATION_COUNT ((int)(sizeof(s_trapDurationValues) / sizeof(s_trapDurationValues[0])))

static int Dlink_Index(int value)
{
	for (int i = 0; i < DLINK_COUNT; i++)
		if (s_dlinkValues[i] == value)
			return i;
	return 0; // out-of-range persisted value renders and steps as SEED
}

static int TrapDuration_Index(int value)
{
	int i;
	for (i = 0; i < TRAP_DURATION_COUNT; i++)
		if (s_trapDurationValues[i] == value)
			return i;
	return 1; // invalid hand-edited values render and step from recommended 15 s
}
#endif

// CFG_ENUM dispatch: each enum entry owns a ladder; picked by the field the
// entry edits (pointer identity, no string compare).
static const char *Enum_Label(const ConfigEntry *e)
{
	if (e->valuePtr == &g_config.aspectRatio)
		return s_aspectNames[Aspect_Index(*(int *)e->valuePtr)];
	if (e->valuePtr == &g_config.renderScale)
		return s_renderScaleNames[RenderScale_Index(*(int *)e->valuePtr)];
	if (e->valuePtr == &g_config.vsync)
		return s_vsyncNames[Vsync_Index(*(int *)e->valuePtr)];
#ifdef CTR_AP
	if (e->valuePtr == &g_config.deathLink)
		return s_dlinkNames[Dlink_Index(*(int *)e->valuePtr)];
	if (e->valuePtr == &g_config.trapDuration)
		return s_trapDurationNames[TrapDuration_Index(*(int *)e->valuePtr)];
#endif
	return AiDiff_Label(*(int *)e->valuePtr);
}

static void Enum_Step(const ConfigEntry *e, int dir)
{
	if (e->valuePtr == &g_config.aspectRatio)
	{
		int i = Aspect_Index(*(int *)e->valuePtr) + dir;
		if (i < 0)
			i = 0;
		if (i > ASPECT_COUNT - 1)
			i = ASPECT_COUNT - 1;
		*(int *)e->valuePtr = s_aspectValues[i];
		return;
	}
	if (e->valuePtr == &g_config.renderScale)
	{
		int i = RenderScale_Index(*(int *)e->valuePtr) + dir;
		if (i < 0)
			i = 0;
		if (i > RENDER_SCALE_COUNT - 1)
			i = RENDER_SCALE_COUNT - 1;
		*(int *)e->valuePtr = s_renderScaleValues[i];
		return;
	}
	if (e->valuePtr == &g_config.vsync)
	{
		int i = Vsync_Index(*(int *)e->valuePtr) + dir;
		if (i < 0)
			i = 0;
		if (i > VSYNC_COUNT - 1)
			i = VSYNC_COUNT - 1;
		*(int *)e->valuePtr = s_vsyncValues[i];
		return;
	}
#ifdef CTR_AP
	if (e->valuePtr == &g_config.deathLink)
	{
		int i = Dlink_Index(*(int *)e->valuePtr) + dir;
		if (i < 0)
			i = 0;
		if (i > DLINK_COUNT - 1)
			i = DLINK_COUNT - 1;
		*(int *)e->valuePtr = s_dlinkValues[i];
		return;
	}
	if (e->valuePtr == &g_config.trapDuration)
	{
		int i = TrapDuration_Index(*(int *)e->valuePtr) + dir;
		if (i < 0)
			i = 0;
		if (i > TRAP_DURATION_COUNT - 1)
			i = TRAP_DURATION_COUNT - 1;
		*(int *)e->valuePtr = s_trapDurationValues[i];
		return;
	}
#endif
	AiDiff_Step((int *)e->valuePtr, dir);
}

// Cross/Circle on an enum row: the next value, wrapping from the last back to
// the first, so every row on a page answers the confirm button (rc1 testing:
// A did nothing on VSync). Left/right keep their clamped stepping.
static void Enum_Advance(const ConfigEntry *e)
{
	const int before = *(int *)e->valuePtr;
	Enum_Step(e, +1);
	if (*(int *)e->valuePtr == before)
		Enum_Step(e, -64); // already at the end: every ladder clamps to its first value
}

// Characters of a CFG_STRING value that fit between a row label and the right
// edge of the value column at FONT_SMALL (13 px per character). The label column
// starts at 0x38 and values are right-justified at 0x1DC, which leaves 420 px;
// a label of a dozen characters takes about 155 of them. 20 keeps a long value
// clear of the longest label this menu has.
#define CFG_STRING_INLINE_MAX 20

#ifdef CTR_AP
// The Item Box Colours row is the player's opt-out of the seed's item box
// colours. While a loaded seed has them off, it reads "OFF (SEED)" in grey and
// takes no input: a player can turn colours off, never on.
static int Config_RowLockedBySeed(const ConfigEntry *e)
{
	return e->valuePtr == &g_config.itemBoxColours && AP_Boxes_ColourRowState() == AP_BOX_COLOUR_ROW_SEED_OFF;
}

static void Config_DrawHelpLines(const char *first, const char *second, uint32_t *ot)
{
	DecalFont_DrawLineOT((char *)first, 0x100, 0xB8, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	DecalFont_DrawLineOT((char *)second, 0x100, 0xC6, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
}

// Two help lines under the page for the selected row, in the footer style the
// Connection page uses. Only rows that need explaining have one. Each line fits
// the panel (33 characters at FONT_SMALL).
static void Config_DrawRowHelp(const ConfigEntry *e, uint32_t *ot)
{
	const char *first = 0;
	const char *second = 0;

	if (e->valuePtr == &g_config.itemBoxColours)
	{
		if (Config_RowLockedBySeed(e))
		{
			first = "This seed turned box colours off.";
			second = "All AP item boxes stay pink.";
		}
		else
		{
			first = "Purple progression, blue useful,";
			second = "cyan filler, salmon trap.";
		}
	}
	if (first == 0)
		return;
	Config_DrawHelpLines(first, second, ot);
}

// Room links (issue #334): which program opens ctr-ap:// links from a room
// page. An action row after the Archipelago page's config rows, where
// registration exists (Windows only for 0.2.1; never in the box authoring
// build). It is not a config entry: the state lives in the registry.
static int Config_LinkRow(int sec, int numRows)
{
	if (strcmp(s_sectionName[sec], "Archipelago") != 0 || !AP_LinkRegRowAvailable())
		return -1;
	return numRows;
}

static void Config_DrawLinkRow(int y, int labelX, int valueX, uint32_t *ot)
{
	DecalFont_DrawLineOT("Room links", labelX, y, FONT_SMALL, ORANGE, ot);
	DecalFont_DrawLineOT((char *)AP_LinkRegStatusText(), valueX, y, FONT_SMALL, JUSTIFY_RIGHT | WHITE, ot);
}
#endif

static void Config_DrawValue(const ConfigEntry *e, const int valueX, int y, uint32_t *ot, char *buf, int editing)
{
#ifdef CTR_AP
	if (Config_RowLockedBySeed(e))
	{
		DecalFont_DrawLineOT("OFF (SEED)", valueX, y, FONT_SMALL, JUSTIFY_RIGHT | GRAY, ot);
		return;
	}
#endif
	if (e->type == CFG_BOOL)
	{
		DecalFont_DrawLineOT(*(bool *)e->valuePtr ? "ON" : "OFF",
			valueX, y, FONT_SMALL, JUSTIFY_RIGHT | WHITE, ot);
	}
	else if (e->type == CFG_ENUM)
	{
		DecalFont_DrawLineOT((char *)Enum_Label(e),
			valueX, y, FONT_SMALL, JUSTIFY_RIGHT | WHITE, ot);
	}
	else if (e->type == CFG_STRING)
	{
		// Cross opens the row in the shared text editor (TextEdit_Begin, AP
		// builds); outside an edit the value is shown up to the inline limit.
		// While editing, the tail is shown instead so the typed end stays in
		// view, with the same blinking cursor as the Connection page. Empty
		// renders as "-", matching the connection manager.
		const char *src = (const char *)e->valuePtr;
		const int len = (int)strlen(src);
		int start = 0;
		int n = 0;

		if (editing && len > CFG_STRING_INLINE_MAX)
			start = len - CFG_STRING_INLINE_MAX;
		while ((src[start + n] != '\0') && (n < CFG_STRING_INLINE_MAX))
		{
			buf[n] = src[start + n];
			n++;
		}
		if (editing && (sdata->frameCounter & 0x10)) // ~2 Hz blink
			buf[n++] = '_';
		if (n == 0 && !editing)
			buf[n++] = '-';
		buf[n] = '\0';

		DecalFont_DrawLineOT(buf, valueX, y, FONT_SMALL, JUSTIFY_RIGHT | WHITE, ot);
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
//
// Controller escape hatch: Enter and Escape are unreachable on a pad-only device
// such as a Steam Deck, which used to leave an edit with no way out. X or START
// now commits and TRIANGLE cancels, doing exactly what the two keys do. X is the
// button that opened the row, START mirrors Enter (the keyboard maps Enter onto
// START), and TRIANGLE is this menu's own back button, so nothing new has to be
// learned. Row-to-row navigation stays suppressed for the whole edit -- only the
// two exits become reachable.
//
// The pad buffer cannot be used for this. The host keyboard is mapped onto a pad
// slot and polled live, so while typing, "z" reads as TRIANGLE, "c" as CROSS,
// space as SELECT and Enter as START -- a slot name would cancel itself halfway
// through. Platform_InputRawGamepadButtons() reads physical pads only and is
// therefore blind to typing; edges are taken here against the previous frame.

static int  s_connEditing = 0;   // 1 while a text row is being edited
static int  s_connEditRow = 0;   // which row of the current page is being edited
static const ConfigEntry *s_connEditEntry = NULL; // the entry being edited
static char s_connBackup[128];   // pre-edit value, restored on cancel
static int  s_connPadPrev = 0;   // previous frame's physical-pad mask, for edges

#define CONN_PAD_COMMIT (RAW_BTN_CROSS | RAW_BTN_START)
#define CONN_PAD_CANCEL (RAW_BTN_TRIANGLE)

// Row geometry in display-space coordinates, shared by the draw pass below and
// by the rectangle handed to the platform text-input layer when an edit starts
// (an on-screen keyboard positions itself around that rectangle). The values
// match the highlight box drawn on the selected row.
#define CONN_ROW_X       0x30
#define CONN_ROW_W       0x1B0
#define CONN_ROW_H       0x0C
#define CONN_ROW_START_Y 0x3C
#define CONN_ROW_SPACING 0x0E
// Characters that still fit between the Status label and the right edge of the
// panel at FONT_SMALL (13 px per character). Longer status text is moved to its
// own centred line below the row.
#define CONN_STATUS_INLINE_MAX 24

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

// The retail decal font maps lowercase and uppercase ASCII to the same glyph.
// For the case-sensitive AP slot only, draw a direct white line beneath each
// byte that is actually uppercase, backed by a three-pixel black rectangle.
// This mirrors the font's white fill / black outline contrast and stays visible
// across both the yellow selection bar and the menu background art. Do not use
// the font's `_` glyph: its baseline is not visible on this small-font row.
static void Conn_DrawSlotCaseMarks(const char *slot, int valueX, int y, uint32_t *ot)
{
	struct GameTracker *gGT = sdata->gGT;
	const int charWidth = data.font_charPixWidth[FONT_SMALL];

	for (int i = 0; slot[i] != '\0'; i++)
	{
		if (CTR_MenuSlotCharNeedsCaseMark((unsigned char)slot[i]))
		{
			LINE_F2 *line = gGT->backBuffer->primMem.cursor;
			POLY_F4 *stroke = (POLY_F4 *)(line + 1);
			int x = valueX + DecalFont_GetLineWidthStrlen((char *)slot, i, FONT_SMALL);

			// Link the white center first. OT insertion is LIFO, so the black backing
			// linked afterward is drawn first and the white line lands on top.
			CtrGpu_WriteColorCode(&line->r0, *data.ptrColor[WHITE]);
			line->x0 = x + 1;
			line->y0 = y + 9;
			line->x1 = x + charWidth - 2;
			line->y1 = y + 9;
			addLineF2(ot, line);

			CtrGpu_WriteColorCode(&stroke->r0, 0);
			setPolyF4(stroke);
			stroke->x0 = x;
			stroke->y0 = y + 8;
			stroke->x1 = x + charWidth - 1;
			stroke->y1 = y + 8;
			stroke->x2 = x;
			stroke->y2 = y + 11;
			stroke->x3 = x + charWidth - 1;
			stroke->y3 = y + 11;
			AddPrim(ot, stroke);

			gGT->backBuffer->primMem.cursor = (void *)(stroke + 1);
		}
	}
}

// Shared text-row editing, used by the Connection page and by a CFG_STRING row
// on any other page (Driver Name on Authoring; before this it was read-only).
// Call once every frame the page is shown: it tracks the physical-pad edges
// and resolves a finished edit. Returns 1 on the frame an edit resolved; the
// caller suppresses all navigation on that frame and while s_connEditing.
static int TextEdit_Update(void)
{
	// Pad-driven commit / cancel, folded into the same result codes the keyboard
	// produces so the resolve block below stays the single exit path. Tracked
	// every frame, editing or not, so the press that opened the row is already
	// held when the first editing frame runs and cannot re-trigger as a fresh tap.
	{
		const int padNow = Platform_InputRawGamepadButtons();
		const int padTapped = padNow & ~s_connPadPrev;
		s_connPadPrev = padNow;

		if (s_connEditing && NativeText_Result() == 0)
		{
			if ((padTapped & CONN_PAD_CANCEL) != 0)
				NativeText_Resolve(2);
			else if ((padTapped & CONN_PAD_COMMIT) != 0)
				NativeText_Resolve(1);
		}
	}

	// Resolve a finished edit first. The platform layer keeps the session active
	// (NativeText_Active == 1) until we call NativeText_End here, so the commit /
	// cancel frame still reads as "editing" to the parent proc's back handler --
	// avoiding a one-frame nav race (pad state is polled from the live keyboard).
	if (s_connEditing && NativeText_Result() != 0)
	{
		const ConfigEntry *e = s_connEditEntry;
		if (NativeText_Result() == 2) // Escape -> restore the pre-edit value
		{
			strncpy((char *)e->valuePtr, s_connBackup, e->max - 1);
			((char *)e->valuePtr)[e->max - 1] = '\0';
		}
		else // Enter -> persist the edited value
		{
			NativeRenderer_CaptureWindowGeometry();
			NativeConfig_Save();
		}
		NativeText_End();
		s_connEditing = 0;
		return 1;
	}
	return 0;
}

// Enter edit mode on a text row: back up the current value, hand the buffer to
// the platform keyboard capture (editing continues from the current text). The
// row's own rectangle goes with it, so a host on-screen keyboard (Steam Deck)
// can place itself clear of the field.
static void TextEdit_Begin(const ConfigEntry *e, int row, int rowY)
{
	s_connEditEntry = e;
	s_connEditRow = row;
	strncpy(s_connBackup, (char *)e->valuePtr, sizeof s_connBackup - 1);
	s_connBackup[sizeof s_connBackup - 1] = '\0';
	const int rowMasked = (strcmp(e->key, "password") == 0);
	NativeText_Begin((char *)e->valuePtr, e->max,
		CONN_ROW_X, rowY, CONN_ROW_W, CONN_ROW_H, rowMasked);
	s_connEditing = 1;
}

// Pad hint while a row is being edited, so the controller exits are
// discoverable. '*' and '^' are the font's own PSX face button glyphs (see
// game/DecalFont.c).
static void TextEdit_DrawHint(int y, uint32_t *ot)
{
	DecalFont_DrawLineOT("* OR START: SAVE   ^: CANCEL",
		0x100, y, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
}

static void MM_ConfigProc_Connection(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
	char buf[160];
	const int sec = s_currentSection;
	const int numStrings = s_sectionCount[sec]; // uri / slot / password
	const int numRows = numStrings + 1;         // + Connect action row

	// The Room links row lives on Options > Archipelago; this page only keeps
	// the one-time notice for when another program owns room links (#334).
	AP_LinkRegNoticeFrame();

	const int justResolved = TextEdit_Update();

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
				TextEdit_Begin(Section_Entry(sec, menu->rowSelected), menu->rowSelected,
					CONN_ROW_START_Y + menu->rowSelected * CONN_ROW_SPACING - 2);
			}
			else
			{
				// Connect action: re-dial with the saved connection settings.
				AP_Net_Reconnect(g_config.uri, g_config.slot, g_config.password);
			}
		}
	}

	DecalFont_DrawLineOT((char *)s_sectionName[sec],
		0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);

	int labelX = 0x38;
	int valueX = 0xB0;  // text values are left-justified (they grow rightward as typed)
	int startY = CONN_ROW_START_Y;
	int rowSpacing = CONN_ROW_SPACING;

	for (int j = 0; j < numStrings; j++)
	{
		const ConfigEntry *e = Section_Entry(sec, j);
		int y = startY + j * rowSpacing;
		int masked = (strcmp(e->key, "password") == 0);
		int editing = (s_connEditing && s_connEditRow == j);

		DecalFont_DrawLineOT((char *)e->label, labelX, y, FONT_SMALL, ORANGE, ot);
		Conn_FormatValue(e, masked, editing, buf, sizeof buf);
		DecalFont_DrawLineOT(buf, valueX, y, FONT_SMALL, WHITE, ot);
		if (strcmp(e->key, "slot") == 0)
			Conn_DrawSlotCaseMarks((const char *)e->valuePtr, valueX, y, ot);

		if (j == menu->rowSelected)
		{
			RECT sel = {CONN_ROW_X, y - 2, CONN_ROW_W, CONN_ROW_H};
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

	// Read-only status row (one line below the Connect row). Short states sit
	// beside the label; anything longer than fits there (the unreachable-host
	// line, a wordy slot refusal) is centred on the next line instead, where the
	// full panel width is available, rather than running off the right edge.
	{
		int y = startY + (numStrings + 2) * rowSpacing;
		const char *status = AP_Net_StatusLine();
		DecalFont_DrawLineOT("Status", labelX, y, FONT_SMALL, ORANGE, ot);
		if ((int)strlen(status) > CONN_STATUS_INLINE_MAX)
			DecalFont_DrawLineOT((char *)status, 0x100, y + rowSpacing,
				FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
		else
			DecalFont_DrawLineOT((char *)status, valueX, y, FONT_SMALL, WHITE, ot);
	}

	// Pair-version update notice (issue #150) -- the persistent surface, below the
	// status block. Rows 8-10 of this section's grid: clear of the status overflow
	// line (row 6) and of the edit hint below (row 7), and the last line lands on
	// 0xC8, still inside the panel. Self-gates: nothing is drawn and no space is
	// taken unless the connected seed was built by a newer pair.
	AP_DrawConnUpdateNotice(ot, 0x100, startY + (numStrings + 5) * rowSpacing, rowSpacing);

	// Use the same bottom footer position as the Options version to explain the
	// case markers at their point of use. A newer-version notice owns this area
	// when armed and takes priority over the static hint.
	if (!AP_ConnUpdateNoticeActive())
	{
		const char *noticeFirst;
		const char *noticeSecond;

		// One-time notice when another program owns ctr-ap:// links: they are
		// never taken over silently, and the notice points at the Room links row
		// on the Archipelago page. Same rows the update notice would use, and
		// like it, it owns the footer while shown.
		if (!s_connEditing && AP_LinkRegNotice(&noticeFirst, &noticeSecond))
		{
			DecalFont_DrawLineOT((char *)noticeFirst, 0x100, startY + (numStrings + 5) * rowSpacing,
				FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
			DecalFont_DrawLineOT((char *)noticeSecond, 0x100, startY + (numStrings + 6) * rowSpacing,
				FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
		}
		else
			DecalFont_DrawLineOT((char *)CTR_MenuSlotCaseHint(),
				0x100, 0xC0, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	}

	// Drawn as a footer rather than on the row itself: the text value is
	// left-justified and grows rightward as it is typed, so there is no space
	// left on the row to put it.
	if (s_connEditing)
		TextEdit_DrawHint(startY + (numStrings + 4) * rowSpacing, ot);
}

#if defined(CTR_CUSTOM_TRACKS) && !defined(CTR_CUSTOM_PACKAGES)
#define CUSTOM_CONTENT_ACTION_COUNT 6
static const char *const s_customContentActions[CUSTOM_CONTENT_ACTION_COUNT] = {
	"Download Saphi",
	"Rescan",
	"Verify / Setup",
	"Open Source",
	"Copy YAML",
	"Save YAML",
};
static char s_customContentMessage[128] =
	"Download from Saphi, or add the original LEV and VRM files, then Rescan.";
static int s_customContentDownloadActive = 0;

static void CustomContent_SetMessage(const char *message)
{
	size_t len = strlen(message);
	if (len >= sizeof s_customContentMessage)
		len = sizeof s_customContentMessage - 1;
	memcpy(s_customContentMessage, message, len);
	s_customContentMessage[len] = '\0';
}

static void CustomContent_DrawMessage(uint32_t *ot)
{
	char first[35];
	char second[35];
	const char *cursor = s_customContentMessage;
	int line;

	for (line = 0; line < 2 && *cursor != '\0'; line++)
	{
		char *out = line == 0 ? first : second;
		int count = 0;
		int lastSpace = -1;

		while (cursor[count] != '\0' && count < 34)
		{
			out[count] = cursor[count];
			if (cursor[count] == ' ')
				lastSpace = count;
			count++;
		}
		if (cursor[count] != '\0' && lastSpace > 0)
			count = lastSpace;
		out[count] = '\0';
		cursor += count;
		while (*cursor == ' ')
			cursor++;
	}
	if (line == 2 && *cursor != '\0')
	{
		int len = (int)strlen(second);
		if (len > 30)
			len = 30;
		memcpy(second + len, "...", 4);
	}

	DecalFont_DrawLineOT(first, 0x100, 0xB8, FONT_SMALL,
	                   JUSTIFY_CENTER | (AP_CustomContentRequired() ? RED : WHITE), ot);
	if (line > 1)
		DecalFont_DrawLineOT(second, 0x100, 0xC6, FONT_SMALL,
		                   JUSTIFY_CENTER | (AP_CustomContentRequired() ? RED : WHITE), ot);
}

static void MM_ConfigProc_CustomContent(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
	const struct CustomTrackManagerPackage *package = CustomTrackManager_BabyTPark();
	const struct CustomTrackManagerStatus *status = AP_CustomContentStatus();
	struct CustomTrackManagerStatus exported;
	char line[160];
	char yaml[4096];
	char downloadMessage[128];
	int downloadState = AP_CustomContentDownloadStatus(downloadMessage, sizeof downloadMessage);
	int i;

	if (s_customContentDownloadActive)
	{
		CustomContent_SetMessage(downloadMessage);
		if (downloadState == AP_CT_DOWNLOAD_SUCCEEDED)
		{
			s_customContentDownloadActive = 0;
			AP_CustomContentVerify();
			status = AP_CustomContentStatus();
			CustomContent_SetMessage(status->detail);
		}
		else if (downloadState == AP_CT_DOWNLOAD_FAILED)
			s_customContentDownloadActive = 0;
	}

	if ((pad->buttonsTapped & BTN_UP) != 0)
	{
		int row = menu->rowSelected % 3;
		menu->rowSelected += row > 0 ? -1 : 2;
		OtherFX_Play(0, 1);
	}
	if ((pad->buttonsTapped & BTN_DOWN) != 0)
	{
		int row = menu->rowSelected % 3;
		menu->rowSelected += row < 2 ? 1 : -2;
		OtherFX_Play(0, 1);
	}
	if ((pad->buttonsTapped & (BTN_LEFT | BTN_RIGHT)) != 0)
	{
		menu->rowSelected = (menu->rowSelected + 3) % CUSTOM_CONTENT_ACTION_COUNT;
		OtherFX_Play(0, 1);
	}

	if ((pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE)) != 0)
	{
		OtherFX_Play(1, 1);
		switch (menu->rowSelected)
		{
		case 0:
			if (AP_CustomContentDownloadStart())
			{
				s_customContentDownloadActive = 1;
				CustomContent_SetMessage("Starting verified download from Project Saphi...");
			}
			else
			{
				AP_CustomContentDownloadStatus(downloadMessage, sizeof downloadMessage);
				CustomContent_SetMessage(downloadMessage);
			}
			break;
		case 1:
			AP_CustomContentRescan();
			status = AP_CustomContentStatus();
			CustomContent_SetMessage(status->detail);
			break;
		case 2:
			AP_CustomContentVerify();
			status = AP_CustomContentStatus();
			CustomContent_SetMessage(status->detail);
			break;
		case 3:
			CustomContent_SetMessage(Platform_OpenURL(package->sourceUrl)
			                        ? "Opened the official Project Saphi page."
			                        : "Could not open the official source page.");
			break;
		case 4:
			if (CustomTrackManager_RenderYaml(package, status, yaml, sizeof yaml) &&
			    Platform_SetClipboardText(yaml))
				CustomContent_SetMessage("Verified custom_tracks YAML copied.");
			else
				CustomContent_SetMessage("Copy YAML requires a Ready package.");
			break;
		case 5:
			if (CustomTrackManager_SaveYaml(NativeAssets_GetAssetDir(), package, status,
			                                &exported))
			{
				char savedMessage[96];
				CustomTrackManager_FormatSavedPathForDisplay(exported.yamlPath, savedMessage,
				                                              sizeof savedMessage);
				CustomContent_SetMessage(savedMessage);
			}
			else
				CustomContent_SetMessage("Save YAML requires a Ready package.");
			break;
		}
	}

	status = AP_CustomContentStatus();
	DecalFont_DrawLineOT("Custom Content", 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
	DecalFont_DrawLineOT((char *)package->title, 0x100, 0x36,
	                   FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	snprintf(line, sizeof line, "v%s  |  %s  |  Project Saphi", package->version, package->author);
	DecalFont_DrawLineOT(line, 0x100, 0x48, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	snprintf(line, sizeof line, "Status: %s  |  Seed: %s",
	         downloadState == AP_CT_DOWNLOAD_RUNNING ? "Downloading" : CustomTrackManager_StateText(status->state),
	         AP_CustomContentSeedSelected()
	             ? (AP_CustomContentRequired() ? "REQUIRED" : "READY")
	             : "OPTIONAL");
	DecalFont_DrawLineOT(line, 0x100, 0x5A, FONT_SMALL, JUSTIFY_CENTER |
	                   (AP_CustomContentRequired() ? RED : ORANGE), ot);

	for (i = 0; i < CUSTOM_CONTENT_ACTION_COUNT; i++)
	{
		int column = i / 3;
		int row = i % 3;
		int x = column == 0 ? 0x38 : 0x118;
		int y = 0x78 + row * 0x18;
		DecalFont_DrawLineOT((char *)s_customContentActions[i], x, y, FONT_SMALL, ORANGE, ot);
		if (i == menu->rowSelected)
		{
			RECT sel = {column == 0 ? 0x30 : 0x110, y - 2, 0xC0, 0x0C};
			CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
		}
	}

	CustomContent_DrawMessage(ot);
}
#endif

#ifdef CTR_CUSTOM_PACKAGES
static struct CustomTrackLibrary s_contentLibrary;
static int s_contentLibraryLoaded;
static int s_contentLibraryTab;
static int s_contentSearchEditing;
static int s_contentSearchPadPrev;
static char s_contentSearchBackup[CTR_LIBRARY_QUERY_MAX + 1];
static int s_contentInstallBusy;
static char s_contentInstallPin[65];
static char s_contentInstallMessage[512];
static int s_contentStoreBusy, s_contentStoreWanted;
static char s_contentStoreMessage[512];
static struct CustomSaphiCatalogue *s_saphiCatalogue;
static int s_saphiRefreshBusy, s_saphiLoaded, s_saphiDownloading;
static char s_saphiMessage[512], s_saphiQuery[CTR_LIBRARY_QUERY_MAX + 1];
static size_t s_saphiSelection[2];

/* Shared metadata/store projection for Options and Arcade. The UI that starts
   a worker drains it before leaving; neither screen takes ownership of the
   other screen's search or selection. */
static void MM_CustomLibrary_Tick(int reload)
{
    char path[CTR_CT_MANAGER_PATH_MAX];
    if (reload) s_contentLibraryLoaded = 0;
    if (!s_contentLibraryLoaded)
    {
        int length = snprintf(path, sizeof path, "%s/tracks/library.catalog", NativeAssets_GetAssetDir());
        if (length > 0 && (size_t)length < sizeof path)
            CustomTrackLibrary_ReadCatalogue(&s_contentLibrary, path);
        s_contentLibraryLoaded = 1;
        s_contentStoreWanted = 1;
    }
    if (s_contentInstallBusy)
    {
        int result;
        if (CustomPackage_PollInboxInstall(&result, s_contentInstallPin,
            s_contentInstallMessage, sizeof s_contentInstallMessage) == 1)
        {
            s_contentInstallBusy = 0;
            if (result)
            {
                CustomTrackLibrary_MarkInstalled(&s_contentLibrary, s_contentInstallPin);
                s_contentStoreWanted = 1;
                snprintf(s_contentInstallMessage, sizeof s_contentInstallMessage,
                         "Installed. Choose custom tracks from Arcade.");
            }
        }
    }
    if (s_contentStoreBusy)
    {
        int result = CustomPackage_PollStoreScan(&s_contentLibrary, s_contentStoreMessage, sizeof s_contentStoreMessage);
        if (result == 1 || result == -2) s_contentStoreBusy = 0;
        if (result == 1) s_contentInstallPin[0] = 0; /* Fresh store evidence supersedes an old action message. */
    }
    if (s_contentStoreWanted && !s_contentStoreBusy && !s_contentInstallBusy)
    {
        int length = snprintf(path, sizeof path, "%s/tracks/packages", NativeAssets_GetAssetDir());
        s_contentStoreMessage[0] = 0;
        if (length > 0 && (size_t)length < sizeof path)
            s_contentStoreBusy = CustomPackage_StartStoreScan(path);
        s_contentStoreWanted = 0;
        if (!s_contentStoreBusy)
            snprintf(s_contentStoreMessage, sizeof s_contentStoreMessage, "Could not start package store scan");
    }
}

static int MM_CustomLibrary_Busy(void)
{
    return s_contentInstallBusy || s_contentStoreBusy || s_contentStoreWanted || s_saphiDownloading;
}

#include "MM_CustomManager.c"
#endif // CTR_CUSTOM_PACKAGES
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
	if ((pad->buttonsTapped & (BTN_TRIANGLE | BTN_START)) != 0 && !NativeText_Active()
#ifdef CTR_CUSTOM_PACKAGES
	    // Drain a package or Saphi worker before leaving; Back first leaves
	    // the Versions list of one track.
	    && !MM_CustomLibrary_Busy() && !s_saphiVersionTrack
#endif
	)
	{
		OtherFX_Play(2, 1);
		if (s_currentSection >= 0)
		{
			menu->rowSelected = s_currentSection;
			s_currentSection = -1;
		}
		else
		{
			NativeRenderer_CaptureWindowGeometry();
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

#ifdef CTR_AP
		// Connection and Custom Content have bespoke action/state surfaces.
#ifdef CTR_CUSTOM_TRACKS
		if (sec == s_customContentSection)
		{
			MM_ConfigProc_CustomContent(menu, ot, pad);
		}
		else
#endif
		if (strcmp(s_sectionName[sec], "Connection") == 0)
		{
			MM_ConfigProc_Connection(menu, ot, pad);
		}
		else
		{
		// A text row on this page (Driver Name) uses the shared editor. While it
		// owns input, and on the frame it resolves, the page ignores the pad: the
		// host keyboard is mapped onto a pad slot, so typing would navigate.
		const int textBusy = TextEdit_Update() || s_connEditing;
		const int linkRow = Config_LinkRow(sec, numRows);
		if (linkRow >= 0)
			AP_LinkRegPageFrame();
#else
		const int textBusy = 0;
		const int linkRow = -1;
#endif
		const int pageRows = numRows + (linkRow >= 0 ? 1 : 0);

		if (!textBusy)
		{
		if ((pad->buttonsTapped & BTN_UP) != 0)
		{
			menu->rowSelected = (menu->rowSelected > 0) ? menu->rowSelected - 1 : pageRows - 1;
			OtherFX_Play(0, 1);
		}
		if ((pad->buttonsTapped & BTN_DOWN) != 0)
		{
			menu->rowSelected = (menu->rowSelected < pageRows - 1) ? menu->rowSelected + 1 : 0;
			OtherFX_Play(0, 1);
		}

		// The selected config entry; NULL on the Room links action row.
		const ConfigEntry *selEntry = (menu->rowSelected < numRows) ? Section_Entry(sec, menu->rowSelected) : NULL;

		// Cross/Circle acts on every row type: a boolean toggles, an enum moves to
		// its next value (wrapping), a text row opens the editor.
		if ((pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE)) != 0)
		{
			OtherFX_Play(1, 1);
			const ConfigEntry *e = selEntry;
#ifdef CTR_AP
			if (e == NULL)
				AP_LinkRegAction(); // Room links: use this client for ctr-ap:// links, or stop
			else if (Config_RowLockedBySeed(e))
				; // the seed has item box colours off: nothing to toggle
			else
#endif
			if (e->type == CFG_BOOL)
				*(bool *)e->valuePtr ^= 1;
			else if (e->type == CFG_ENUM)
				Enum_Advance(e);
#ifdef CTR_AP
			else if (e->type == CFG_STRING)
				TextEdit_Begin(e, menu->rowSelected,
					CONN_ROW_START_Y + menu->rowSelected * CONN_ROW_SPACING - 2);
#endif
		}

		// Boolean rows now follow the same left/right value-editing convention as
		// enums and sliders: left is OFF, right is ON. Cross/Circle still toggles.
		{
			const ConfigEntry *e = selEntry;
#ifdef CTR_AP
			if (e != NULL && e->type == CFG_BOOL && !Config_RowLockedBySeed(e))
#else
			if (e != NULL && e->type == CFG_BOOL)
#endif
			{
				if ((pad->buttonsTapped & BTN_LEFT) != 0)
				{
					CTR_MenuBoolStep((bool *)e->valuePtr, -1);
					OtherFX_Play(0, 1);
				}
				if ((pad->buttonsTapped & BTN_RIGHT) != 0)
				{
					CTR_MenuBoolStep((bool *)e->valuePtr, +1);
					OtherFX_Play(0, 1);
				}
			}
		}

		// enum entries: tap left/right to step through the preset ladder
		{
			const ConfigEntry *e = selEntry;
			if (e != NULL && e->type == CFG_ENUM)
			{
				if ((pad->buttonsTapped & BTN_LEFT) != 0)
				{
					Enum_Step(e, -1);
					OtherFX_Play(0, 1);
				}
				if ((pad->buttonsTapped & BTN_RIGHT) != 0)
				{
					Enum_Step(e, +1);
					OtherFX_Play(0, 1);
				}
			}
		}

		// slider update for int entries
		for (int j = 0; j < numRows; j++)
		{
			const ConfigEntry *e = Section_Entry(sec, j);
			if (e->type == CFG_INT)
				Config_UpdateSlider(pad, menu->rowSelected, j, (int *)e->valuePtr, e->min, e->max, e->step);
		}
		} // !textBusy

		DecalFont_DrawLineOT((char *)s_sectionName[sec],
			0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);

		int labelX = 0x38;
		int valueX = 0x1DC;
		int startY = 0x3C;
		int rowSpacing = 0x0E;

		for (int j = 0; j < numRows; j++)
		{
			const ConfigEntry *e = Section_Entry(sec, j);
			int y = startY + j * rowSpacing;
#ifdef CTR_AP
			const int editing = s_connEditing && s_connEditEntry == e;
#else
			const int editing = 0;
#endif

			DecalFont_DrawLineOT((char *)e->label, labelX, y, FONT_SMALL, ORANGE, ot);
			Config_DrawValue(e, valueX, y, ot, buf, editing);

			if (j == menu->rowSelected)
			{
				RECT sel = {0x30, y - 2, 0x1B0, 0x0C};
				CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
			}
		}
#ifdef CTR_AP
		if (linkRow >= 0)
		{
			int y = startY + linkRow * rowSpacing;
			Config_DrawLinkRow(y, labelX, valueX, ot);
			if (menu->rowSelected == linkRow)
			{
				RECT sel = {0x30, y - 2, 0x1B0, 0x0C};
				CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
			}
		}

		if (s_connEditing)
			TextEdit_DrawHint(0xC0, ot);
		else if (menu->rowSelected == linkRow)
			Config_DrawHelpLines("Which program opens room links.", AP_LinkRegActionHint(), ot);
		else if (numRows > 0 && menu->rowSelected < numRows)
			Config_DrawRowHelp(Section_Entry(sec, menu->rowSelected), ot);
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
			int y = startY + i * spacing;
			int color = ORANGE;
#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
			if (i == s_customContentSection && AP_CustomContentRequired())
				color = RED;
#endif
			DecalFont_DrawLineOT((char *)s_sectionName[i], labelX, y, FONT_SMALL, color, ot);
			if (i == menu->rowSelected)
			{
				RECT sel = {0x30, y - 2, 0x1B0, 0x0C};
				CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
			}
		}

#ifdef CTR_AP
		// Keep the tester-visible pair identity available in-game, without using a
		// main-menu row or colliding with the denser section submenus.
		DecalFont_DrawLineOT("CTR-AP " CTR_AP_VERSION,
			0x100, 0xC0, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
#endif
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
