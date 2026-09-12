#ifndef AP_HIT_CHOOSER_H
#define AP_HIT_CHOOSER_H

// The ordinary Hit Character chooser's frame-to-frame state machine (ticket 06).
//
// AH_WarpPad.c is a large overlay that cannot be linked off-engine, so the
// decisions that must survive across frames live here as pure C. The engine glue
// only shows/hides the RectMenu, applies the mode bits via AP_HitChooserApplyPure
// and restores the saved values on cancel.
//
// The defects this state machine closes (round-2 review):
//   B. a chosen CTR/Relic route must not reopen the chooser this warp;
//   C. an unseen Aku hint must not reopen it either, and cancel must restore the
//      pre-chooser mode/pending/menu state;
//   D. if the Hit opportunity vanishes while the menu is open, the menu is
//      closed and the normal routing resumes.

#ifdef CTR_AP

#include "ap_hit_policy.h" // AP_HitOrdinaryRoutePure + AP_HIT_ORDINARY_*

// One frame's action for the engine.
enum
{
	AP_HIT_CHOOSER_NONE = 0, // no chooser action; use the normal tier-2 routing
	AP_HIT_CHOOSER_OPEN,     // show the chooser menu (rows already built)
	AP_HIT_CHOOSER_WAIT,     // menu open, waiting for input
	AP_HIT_CHOOSER_PLAIN,    // Hit opportunity is the only check -> plain rerace
	AP_HIT_CHOOSER_APPLY,    // a route was chosen; outRoute holds it
	AP_HIT_CHOOSER_CANCEL,   // cancelled; restore the saved state
	AP_HIT_CHOOSER_VANISH    // opportunity vanished while open; hide the menu
};

typedef struct
{
	int menuOpen;     // chooser menu currently shown
	int warpResolved; // a route was applied this warp -> never reopen
	int choice;       // -2 waiting, -1 cancel, 0 Trophy, 1 CTR, 2 Relic
	int savedValid;
	int savedGm1, savedGm2;
	unsigned savedAdd0, savedRem0, savedAdd8, savedRem8;
	int savedMenuFlag;
} ap_hit_chooser;

// Called once when a warp begins (framesWarping == 0).
static inline void AP_HitChooserWarpStart(ap_hit_chooser *c)
{
	c->menuOpen = 0;
	c->warpResolved = 0;
	c->choice = -2;
	c->savedValid = 0;
}

// The menu proc writes the selected route here: -1 cancel, 0 Trophy, 1 CTR,
// 2 Relic.
static inline void AP_HitChooserSetChoice(ap_hit_chooser *c, int route)
{
	c->choice = route;
}

// One frame. `gm*`/`add*`/`rem*`/`menuFlag` are the engine's CURRENT values;
// they are snapshotted the first time the menu opens so CANCEL can restore them.
// Returns an AP_HIT_CHOOSER_* action; APPLY also writes *outRoute.
static inline int AP_HitChooserFrame(ap_hit_chooser *c, int hasOpportunity,
                                     int tokenLeft, int relicLeft,
                                     int gm1, int gm2,
                                     unsigned add0, unsigned rem0,
                                     unsigned add8, unsigned rem8,
                                     int menuFlag, int *outRoute)
{
	// The opportunity vanished while the menu was open: close it, no action.
	if (c->menuOpen && !hasOpportunity)
	{
		c->menuOpen = 0;
		c->choice = -2;
		return AP_HIT_CHOOSER_VANISH;
	}
	if (!hasOpportunity || c->warpResolved)
		return AP_HIT_CHOOSER_NONE;

	if (AP_HitOrdinaryRoutePure(hasOpportunity, tokenLeft, relicLeft) ==
	    AP_HIT_ORDINARY_PLAIN)
		return AP_HIT_CHOOSER_PLAIN;

	// A CTR/Relic route exists -> the chooser menu.
	if (!c->menuOpen)
	{
		// Snapshot the pre-chooser state before the engine shows the menu.
		c->savedValid = 1;
		c->savedGm1 = gm1;
		c->savedGm2 = gm2;
		c->savedAdd0 = add0;
		c->savedRem0 = rem0;
		c->savedAdd8 = add8;
		c->savedRem8 = rem8;
		c->savedMenuFlag = menuFlag;
		c->choice = -2;
		c->menuOpen = 1;
		return AP_HIT_CHOOSER_OPEN;
	}
	if (c->choice == -2)
		return AP_HIT_CHOOSER_WAIT;

	c->menuOpen = 0;
	if (c->choice < 0)
	{
		// Cancel re-arms a fresh choice for a later entry.
		c->warpResolved = 0;
		return AP_HIT_CHOOSER_CANCEL;
	}
	// A route was applied: this warp is resolved, so the hint wait that follows
	// cannot reopen the chooser.
	c->warpResolved = 1;
	*outRoute = c->choice;
	return AP_HIT_CHOOSER_APPLY;
}

#endif // CTR_AP
#endif // AP_HIT_CHOOSER_H
