#include <platform/native_menu.h>
#ifdef CTR_AP

// ---------------------------------------------------------------------------
// In-race Kart Setup (ruling of 2026-11-18: "The kart setup options
// should essentially follow the character selector screen and have the same
// rules as there.")
//
// A KART SETUP row in the Adventure race and cup pause menus (and the Arcade
// ones a Relic Race without ADVENTURE_MODE pauses into) opens this page
// inside the same pause, the way the hub pause TRACKER row opens the tracker:
// PAUSE_1 stays set for the whole visit, so the race timer, lap progress and
// every kart stand still, and Back returns to the pause menu with its RESUME
// row. Nothing here resumes the race.
//
// The page is the hub picker's stat panel for the racer being driven. Rows,
// values, labels, editability, rank limits, global or per-racer scope and
// persistence all come from ap_charswap.c (AP_CharSwap_KartSetup*), so an edit
// made here is the hub picker's state and the reverse. Character switching
// stays in the hub; this page has no roster.
//
// Applying the change: a Progressive Stats or Boost rank is read by the
// per-frame writers (AP_CapabilityStats, AP_CapabilityFireGrant), so it takes
// effect on the first unpaused frame. An editable stat is re-applied to the
// live driver at once through the picker's own constants rewrite.
// ---------------------------------------------------------------------------

#include "ap_kartsetup.h" // includes ap_kartsetup_logic.h
#include "ap_stat_select_logic.h"
#include "ap_charswap.h"
#include "ap_capability.h"
#include "ap_hooks.h"

CTR_STATIC_ASSERT(AP_KS_ADVENTURE_ARENA == ADVENTURE_ARENA);

static struct
{
	int open;
	int request;
	int row; // focused AP_SS_PANEL_* row
} ap_ks;

int AP_KartSetupRowLive(void)
{
	struct GameTracker *gt = sdata->gGT;
	if (gt == NULL)
		return 0;
	return AP_KartSetup_RowLive(gt->gameMode1, (int)(u8)gt->numPlyrCurrGame, AP_CharSwap_PauseRowLive(),
	                            AP_CapabilityRoomStatsRace());
}

void AP_KartSetupRequestOpen(void)
{
	if (AP_KartSetupRowLive())
		ap_ks.request = 1;
}

int AP_KartSetupOpen(void)
{
	return ap_ks.open;
}

// The page lives only inside a race pause menu that is on screen and settled.
static int ap_ks_context(void)
{
	struct GameTracker *gt = sdata->gGT;
	struct RectMenu *m = sdata->ptrActiveMenu;

	if (gt == NULL || (gt->gameMode1 & PAUSE_1) == 0 || (gt->gameMode1 & END_OF_RACE) != 0)
		return 0;
	if (m == NULL || sdata->ptrDesiredMenu != NULL || m->funcPtr != MainFreeze_MenuPtrDefault)
		return 0;
	if (m != &data.menuAdvRace && m != &data.menuAdvCup && m != &data.menuArcadeRace && m != &data.menuArcadeCup)
		return 0;
	if ((m->state & NEEDS_TO_CLOSE) != 0)
		return 0;
	return AP_KartSetupRowLive();
}

static void ap_ks_draw(unsigned avail)
{
	struct GameTracker *gt = sdata->gGT;
	RECT box;
	const int panelY = 0x3a;

	// Text first, backing box last: the first primitive added to the UI ordering
	// table is drawn on top (the picker's own plate comment explains the order).
	DecalFont_DrawLine("KART SETUP", 0x100, 0x1c, FONT_BIG, (JUSTIFY_CENTER | ORANGE));
	AP_CharSwap_KartSetupDrawPanel(panelY, avail ? ap_ks.row : -1);
	DecalFont_DrawLine(avail ? NativeMenu_Labels("LEFT/RIGHT: CHANGE   ^: BACK") : NativeMenu_Labels("^: BACK"), 0x100,
	                   panelY + 76, FONT_SMALL, (JUSTIFY_CENTER | ORANGE));

	box.x = 0x38;
	box.y = 0x16;
	box.w = 0x190;
	box.h = 0x8a;
	RECTMENU_DrawInnerRect(&box, 4, gt->backBuffer->otMem.uiOT);
}

// Called first from RECTMENU_ProcessState. Returns 1 when the page owned this
// frame, so the pause menu underneath neither draws nor sees the press.
int AP_KartSetupMenuFrame(void)
{
	int tap;
	unsigned avail;

	// A connect, disconnect or slot switch while paused adds or removes the row
	// on the menu already on screen, carrying the highlight by row.
	MainFreeze_APKartSetupSync();

	if (!ap_ks_context())
	{
		int wasOpen = ap_ks.open;
		ap_ks.request = 0;
		ap_ks.open = 0;
		// The page closed under the player (a disconnect or slot switch took the
		// row away, or the menu changed). The press of this frame was meant for
		// the page: swallow it so it cannot also select RESUME, or reach
		// MainFrame's Start unpause, on the menu underneath.
		if (wasOpen && sdata->ptrActiveMenu != NULL)
		{
			RECTMENU_ClearInput();
			AP_LogLine("[AP KARTSETUP] closed: the row is no longer available here\n");
			return 1;
		}
		return 0;
	}

	tap = sdata->buttonTapPerPlayer[0];
	avail = AP_CharSwap_KartSetupRows();

	if (!ap_ks.open)
	{
		if (!ap_ks.request)
			return 0;
		ap_ks.request = 0;
		ap_ks.open = 1;
		// The picker enters its panel on the first available row; so does this.
		ap_ks.row = AP_StatSel_ValidRow(ap_ks.row, avail);
		AP_CharSwap_KartSetupResetBars();
		// The confirming press must not also reach MainFrame's Start unpause.
		sdata->AnyPlayerTap &= ~BTN_START;
		AP_LogLine("[AP KARTSETUP] opened from the race pause menu\n");
		ap_ks_draw(avail);
		return 1;
	}

	// MainFrame reads this aggregate after the menu pass and unpauses on Start.
	// Start belongs to this page while it is open: it goes back one level, as it
	// does in the hub picker's panel, and the race stays paused.
	sdata->AnyPlayerTap &= ~BTN_START;

	if ((tap & (BTN_TRIANGLE | BTN_START)) != 0)
	{
		ap_ks.open = 0;
		OtherFX_Play(2, 1);
		RECTMENU_ClearInput();
		AP_LogLine("[AP KARTSETUP] closed, back to the pause menu\n");
		return 1;
	}

	if (avail != 0)
	{
		ap_ks.row = AP_StatSel_ValidRow(ap_ks.row, avail);
		if ((tap & BTN_UP) != 0)
			ap_ks.row = AP_StatSel_NextRow(ap_ks.row, -1, avail);
		else if ((tap & BTN_DOWN) != 0)
			ap_ks.row = AP_StatSel_NextRow(ap_ks.row, 1, avail);
		else if ((tap & (BTN_LEFT | BTN_RIGHT)) != 0)
		{
			int used = AP_CharSwap_KartSetupAdjust(ap_ks.row, (tap & BTN_LEFT) ? -1 : 1);
			if (used >= 0)
				ap_ks.row = used;
		}
	}

	ap_ks_draw(avail);
	return 1;
}

#endif // CTR_AP
