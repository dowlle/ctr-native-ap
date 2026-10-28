#ifndef AP_PAD_ACTIVITY_LOGIC_H
#define AP_PAD_ACTIVITY_LOGIC_H

// What a pad or boss garage offers right now, and why a mode is locked
// (#375, 0.3.0). Freestanding so tools/test-pad-activity.py pins it out of
// engine.
//
// One decision for the in-game tracker's activity list. Its facts come from
// the same functions the physical pad and garage ask (AP_PadState,
// AP_PadReplayable, AP_PadStage1Met, ctr_cfg_racer_lock_met,
// AP_PadSelectorFacts, AP_ContentSelectorBuild, AP_CustomContentRequired,
// AP_BossGarageOpen, AP_OxideGarageOpen), gathered by AP_PadActivities in
// ap_hooks_pads.c. A tracker start never loads anything itself: it moves the
// player to the pad's hub and lets that pad or garage run its own entry, so
// the pad keeps the last word. The tracker adds one gate the pad cannot see
// from where the player stands: the hub's door Keys.
//
// Rows, in order: the race selector's Trophy Race, CTR Challenge and Relic
// Race; a content-plan pad's own modes; or one row for a cup, arena, trial,
// legacy custom race or boss garage. A locked row stays in the list, grey,
// with the first reason that applies.

#include "ap_race_structure_logic.h"

enum
{
	AP_PADACT_TROPHY = 0, // same values as AP_RS_TROPHY / CTR / RELIC
	AP_PADACT_CTR = 1,
	AP_PADACT_RELIC = 2,
	AP_PADACT_CUP = 3,
	AP_PADACT_CRYSTAL = 4,
	AP_PADACT_BOSS = 5,
	AP_PADACT_RACE = 6, // a pad that asks for its race type itself
	AP_PADACT_KINDS = 7
};

enum
{
	AP_PADACT_OPEN = 0,
	AP_PADACT_LOCK_HUB = 1,     // the hub's door Keys are missing
	AP_PADACT_LOCK_PAD = 2,     // the pad's own requirement
	AP_PADACT_LOCK_RACER = 3,   // the pad's racer lock
	AP_PADACT_LOCK_TROPHY = 4,  // classic: win the Trophy Race first
	AP_PADACT_LOCK_STAGE2 = 5,  // the pad's second requirement
	AP_PADACT_LOCK_CONTENT = 6, // custom content is not installed or Ready
	AP_PADACT_LOCK_DONE = 7,    // every check here is done and it cannot replay
	AP_PADACT_LOCK_CLOSED = 8,  // no entry at all (arena off, refused room)
	AP_PADACT_LOCK_BOSS = 9,    // the garage requirement
	AP_PADACT_REASONS = 10
};

enum
{
	AP_PADACT_NODE_NONE = 0,
	AP_PADACT_NODE_SELECTOR = 1, // #208 race type selector pad
	AP_PADACT_NODE_CONTENT = 2,  // content-plan track pad with its own modes
	AP_PADACT_NODE_SINGLE = 3,   // one activity, the pad's own route
	AP_PADACT_NODE_GARAGE = 4
};

#define AP_PADACT_MAX_ROWS 3

typedef struct
{
	int node;       // AP_PADACT_NODE_*
	int single;     // the activity of a SINGLE node
	int keysHave;   // received Keys
	int keysNeed;   // the hub doors on the way to this pad
	int content;    // a content-plan room: the pad's gate is AP_ContentPadOpen
	// Pads (not garages)
	int padState;   // AP_PadState: 1 locked, 2 raceable, 3 re-locked, 4 tier 2, 5 done, 0 unknown
	int stage1Met;  // AP_PadStage1Met (content plan: AP_ContentPadOpen)
	int racerMet;   // ctr_cfg_racer_lock_met
	int replayable; // AP_PadReplayable
	int contentReady; // not AP_CustomContentRequired; content plan: occupied and Ready
	// SELECTOR
	int structure;  // ctr_cfg.adventure_structure
	int trophyChecked;
	int stage2Met;
	ap_rs_facts rs;
	// CONTENT
	int modeCount;
	int contentMode[AP_PADACT_MAX_ROWS];
	int contentOpen[AP_PADACT_MAX_ROWS];
	int contentLeft[AP_PADACT_MAX_ROWS];
	// GARAGE
	int garageOpen;
} ap_padact_facts;

typedef struct
{
	int count;
	int activity[AP_PADACT_MAX_ROWS];
	int reason[AP_PADACT_MAX_ROWS];     // AP_PADACT_OPEN or a lock reason
	int left[AP_PADACT_MAX_ROWS];       // an open check is left (else a replay)
	int launchMode[AP_PADACT_MAX_ROWS]; // the mode the pad's own menu takes, -1 none
} ap_padact_list;

// The Keys the hub doors need before a physical pad or garage in hub `hub`
// (levelID - GEM_STONE_VALLEY: 0 Gem Stone Valley, 1 N. Sanity Beach, 2 The
// Lost Ruins, 3 Glacier Park, 4 Citadel City) is reachable. These are the
// apworld's region entrances in world.json (N. Sanity Beach is free, Gem
// Stone Valley and The Lost Ruins Key 1, Glacier Park Key 2, Citadel City
// Key 3) and the Cups Room door (Key 2, D232.arrKeysNeeded[0]) in front of
// the five Gem Cup pads (physical 100 to 104). Every pad exit keeps its hub's
// Key rule in logic (Regions.py), so this is the same gate in both places.
static inline int AP_PadActivityHubKeys(int hub, int physicalPad)
{
	static const int need[5] = {1, 0, 1, 2, 3};
	if (physicalPad >= 100 && physicalPad < 105)
		return 2;
	return hub >= 0 && hub < 5 ? need[hub] : 99;
}

static inline void AP_PadActivityAll(ap_padact_list *out, int reason)
{
	int i;
	for (i = 0; i < out->count; i++)
		out->reason[i] = reason;
}

static inline void AP_PadActivityRow(ap_padact_list *out, int activity, int reason, int left, int mode)
{
	if (out->count >= AP_PADACT_MAX_ROWS)
		return;
	out->activity[out->count] = activity;
	out->reason[out->count] = reason;
	out->left[out->count] = left;
	out->launchMode[out->count] = mode;
	out->count++;
}

// The node-level reason a pad refuses entry, before any mode is asked, in
// the order AH_WarpPad_ThTick meets them: Done without a replay, a closed
// (locked or re-locked) pad, the racer lock, custom content. 0 when open.
static inline int AP_PadActivityEntryReason(const ap_padact_facts *f)
{
	if (f->content)
	{
		// AH_WarpPad_ThTick's Done gate runs before its content-plan branch
		// and never replays a content pad: AP_PadState 5 is an empty slot, a
		// pad whose content is not Ready, or a finished Crystal Challenge.
		if (!f->contentReady)
			return AP_PADACT_LOCK_CONTENT;
		if (f->padState == 5)
			return AP_PADACT_LOCK_DONE;
		if (!f->stage1Met)
			return f->racerMet ? AP_PADACT_LOCK_PAD : AP_PADACT_LOCK_RACER;
		return AP_PADACT_OPEN;
	}
	if (f->padState == 0)
		return AP_PADACT_LOCK_CLOSED;
	if (f->padState == 5 && !f->replayable)
		return AP_PADACT_LOCK_DONE;
	if (f->padState == 1)
		return !f->stage1Met ? AP_PADACT_LOCK_PAD : AP_PADACT_LOCK_RACER;
	if (f->padState == 3)
		return AP_PADACT_LOCK_STAGE2;
	if (!f->racerMet)
		return AP_PADACT_LOCK_RACER;
	if (!f->contentReady)
		return AP_PADACT_LOCK_CONTENT;
	if (f->node == AP_PADACT_NODE_SELECTOR && f->structure == AP_RS_STRUCTURE_REFUSED)
		return AP_PADACT_LOCK_CLOSED;
	return AP_PADACT_OPEN;
}

static inline void AP_PadActivityDecide(const ap_padact_facts *f, ap_padact_list *out)
{
	int m, entry;
	out->count = 0;
	switch (f->node)
	{
	case AP_PADACT_NODE_GARAGE:
		AP_PadActivityRow(out, AP_PADACT_BOSS, f->garageOpen ? AP_PADACT_OPEN : AP_PADACT_LOCK_BOSS, 1, -1);
		break;
	case AP_PADACT_NODE_SELECTOR:
	{
		// AP_RaceStructureAdmit, the selector's own admission: the Trophy
		// Race always; the CTR Challenge at stage 1 under ctr_first; the rest
		// after the checked Trophy and the pad's second requirement.
		int admitted[AP_RS_COUNT];
		int structure = f->structure == AP_RS_STRUCTURE_CTR_FIRST ? AP_RS_STRUCTURE_CTR_FIRST : AP_RS_STRUCTURE_CLASSIC;
		AP_RaceStructureAdmit(structure, f->trophyChecked, f->stage2Met, admitted);
		for (m = 0; m < AP_RS_COUNT; m++)
		{
			if (!f->rs.present[m])
				continue;
			AP_PadActivityRow(out, m,
			                  admitted[m] ? AP_PADACT_OPEN
			                  : !f->trophyChecked ? AP_PADACT_LOCK_TROPHY : AP_PADACT_LOCK_STAGE2,
			                  f->rs.productive[m] ? 1 : 0, m);
		}
		break;
	}
	case AP_PADACT_NODE_CONTENT:
		for (m = 0; m < f->modeCount && m < AP_PADACT_MAX_ROWS; m++)
			AP_PadActivityRow(out, f->contentMode[m],
			                  f->contentOpen[m] ? AP_PADACT_OPEN : AP_PADACT_LOCK_STAGE2,
			                  f->contentLeft[m] ? 1 : 0,
			                  f->modeCount > 1 ? f->contentMode[m] : -1);
		break;
	case AP_PADACT_NODE_SINGLE:
		AP_PadActivityRow(out, f->single, AP_PADACT_OPEN, f->padState != 5 && f->padState != 4, -1);
		break;
	default:
		return;
	}
	// Pad-wide gates override a row's own answer: the hub Keys first, as the
	// player meets the doors before the pad, then the pad's entry.
	if (f->keysHave < f->keysNeed)
	{
		AP_PadActivityAll(out, AP_PADACT_LOCK_HUB);
		return;
	}
	if (f->node == AP_PADACT_NODE_GARAGE)
		return;
	entry = AP_PadActivityEntryReason(f);
	if (entry != AP_PADACT_OPEN)
	{
		// One reason for every row, so the list never shows a mode open on
		// a pad that will not take the kart.
		AP_PadActivityAll(out, entry);
	}
}

// The row to select when the list opens: the first open row with a check
// left, else the first open row, else the first row.
static inline int AP_PadActivityCursor(const ap_padact_list *l)
{
	int i, open = -1;
	for (i = 0; i < l->count; i++)
		if (l->reason[i] == AP_PADACT_OPEN)
		{
			if (l->left[i])
				return i;
			if (open < 0)
				open = i;
		}
	return open >= 0 ? open : 0;
}

// Does the list still offer `activity` with `mode` open? The launch re-asks
// this after a hub load, so a state change on the way cancels the start.
static inline int AP_PadActivityStillOpen(const ap_padact_list *l, int activity, int mode)
{
	int i;
	for (i = 0; i < l->count; i++)
		if (l->activity[i] == activity && l->launchMode[i] == mode)
			return l->reason[i] == AP_PADACT_OPEN;
	return 0;
}

static inline const char *AP_PadActivityName(int activity)
{
	static const char *const names[AP_PADACT_KINDS] = {
		"TROPHY RACE", "CTR CHALLENGE", "RELIC RACE", "GEM CUP",
		"CRYSTAL CHALLENGE", "BOSS RACE", "RACE"};
	return activity >= 0 && activity < AP_PADACT_KINDS ? names[activity] : "?";
}

static inline const char *AP_PadActivityReasonName(int reason)
{
	static const char *const names[AP_PADACT_REASONS] = {
		"", "NEEDS HUB KEYS", "PAD LOCKED", "NEEDS RACER", "WIN TROPHY RACE FIRST",
		"SECOND REQUIREMENT", "INSTALL CONTENT IN OPTIONS", "COMPLETE", "CLOSED",
		"BOSS LOCKED"};
	return reason >= 0 && reason < AP_PADACT_REASONS ? names[reason] : "LOCKED";
}

#endif
