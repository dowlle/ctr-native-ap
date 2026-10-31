#ifndef AP_PAD_ACTIVITY_LOGIC_H
#define AP_PAD_ACTIVITY_LOGIC_H

// What a plan pad or boss garage offers right now, and why a race is locked
// (#375, one generator row 6). Freestanding so tools/test-tracker-pad-launch.py
// pins it out of engine.
//
// One decision for the in-game tracker's activity list. Its facts come from
// the same functions the physical pad and garage ask (AP_PadState,
// AP_PadReplayable, AP_ContentPadRuleOwned, AP_ContentSelectorBuild,
// AP_ContentPadReady, AP_BossGarageOpen, AP_OxideGarageOpen), gathered by
// AP_PadActivities in ap_tracker_launch.c. A tracker start never loads anything
// itself: it moves the player to the pad's hub and lets that pad or garage run
// its own entry, so the pad keeps the last word. The tracker adds one gate the
// pad cannot see from where the player stands: the hub's door Keys.
//
// Rows, in order: a plan pad's own modes in the pad's order, or one row for a
// cup, Crystal Challenge or boss garage. A locked row stays in the list, grey.
// It carries every requirement it is missing (decision 34), not only the
// first: `reason` is the first one the player meets, `mask` has a bit for
// each, and AP_PadActivityReasonLines wraps them onto two lines. A room without
// a plan has no list: its tracker stays inspection only.

#include <string.h>


enum
{
	AP_PADACT_TROPHY = 0, // same values as AP_CONTENT_MODE_*
	AP_PADACT_CTR = 1,
	AP_PADACT_RELIC = 2,
	AP_PADACT_CUP = 3,
	AP_PADACT_CRYSTAL = 4,
	AP_PADACT_BOSS = 5,
	AP_PADACT_KINDS = 6
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
	AP_PADACT_LOCK_CLOSED = 8,  // no entry on this pad
	AP_PADACT_LOCK_BOSS = 9,    // the garage requirement
	AP_PADACT_LOCK_CTRCHAL = 10, // ctr_first: finish the CTR Challenge first
	AP_PADACT_REASONS = 11
};

#define AP_PADACT_BIT(reason) (1 << (reason))

enum
{
	AP_PADACT_NODE_NONE = 0,
	AP_PADACT_NODE_CONTENT = 2,  // track pad with its own modes
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
	// Pads (not garages)
	int padState;   // AP_PadState: 1 locked, 2 raceable, 3 closed again, 4 stage 2 open, 5 done
	int occupied;   // the pad has an entry
	int stage1Met;  // the pad's own rule and its racer lock; the Keys are keysNeed
	int padReqMet;  // the pad's own rule, without the hub Keys or the racer lock
	int racerMet;   // the racer lock (AP_ContentPadRacerMet)
	int replayable; // AP_PadReplayable
	int contentReady; // AP_ContentPadReady
	int firstRaceWon; // ap_content_first_race_won
	int ctrFirst;   // ap_content_pad_ctr_first
	// CONTENT
	int modeCount;
	int contentMode[AP_PADACT_MAX_ROWS];
	int contentOpen[AP_PADACT_MAX_ROWS];
	int contentLeft[AP_PADACT_MAX_ROWS];
	int contentStage2[AP_PADACT_MAX_ROWS]; // the race waits for the pad's stage 2
	int stage2Open;  // the pad's stage 2 is met
	// GARAGE
	int garageOpen;
} ap_padact_facts;

typedef struct
{
	int count;
	int activity[AP_PADACT_MAX_ROWS];
	int reason[AP_PADACT_MAX_ROWS];     // AP_PADACT_OPEN or the first lock reason
	int mask[AP_PADACT_MAX_ROWS];       // every lock reason, AP_PADACT_BIT(reason); 0 when open
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

// `reason` becomes every row's first reason and `bits` joins its mask.
static inline void AP_PadActivityAll(ap_padact_list *out, int reason, int bits)
{
	int i;
	for (i = 0; i < out->count; i++)
	{
		out->reason[i] = reason;
		out->mask[i] |= bits | AP_PADACT_BIT(reason);
	}
}

static inline void AP_PadActivityRow(ap_padact_list *out, int activity, int reason, int left, int mode)
{
	if (out->count >= AP_PADACT_MAX_ROWS)
		return;
	out->activity[out->count] = activity;
	out->reason[out->count] = reason;
	out->mask[out->count] = reason == AP_PADACT_OPEN ? 0 : AP_PADACT_BIT(reason);
	out->left[out->count] = left;
	out->launchMode[out->count] = mode;
	out->count++;
}

// The node-level reason a pad refuses entry, before any mode is asked, in
// the order AH_WarpPad_ThTick meets them. 0 when open.
static inline int AP_PadActivityEntryReason(const ap_padact_facts *f)
{
	if (!f->occupied)
		return AP_PADACT_LOCK_CLOSED;
	if (!f->contentReady)
		return AP_PADACT_LOCK_CONTENT;
	if (f->padState == 5 && !f->replayable)
		return AP_PADACT_LOCK_DONE;
	if (!f->stage1Met)
		return f->padReqMet ? AP_PADACT_LOCK_RACER : AP_PADACT_LOCK_PAD;
	if (f->padState == 3)
		return AP_PADACT_LOCK_STAGE2;
	return AP_PADACT_OPEN;
}

// Every requirement the pad's entry is missing, in a mask. An empty, finished
// or uninstalled pad stands alone (nothing else matters there). The first
// reason (AP_PadActivityEntryReason) is always one of the bits.
static inline int AP_PadActivityEntryMask(const ap_padact_facts *f)
{
	int m = 0, first = AP_PadActivityEntryReason(f);
	if (first == AP_PADACT_OPEN)
		return 0;
	if (first == AP_PADACT_LOCK_CLOSED || first == AP_PADACT_LOCK_CONTENT || first == AP_PADACT_LOCK_DONE)
		return AP_PADACT_BIT(first);
	if (!f->padReqMet)
		m |= AP_PADACT_BIT(AP_PADACT_LOCK_PAD);
	if (!f->racerMet)
		m |= AP_PADACT_BIT(AP_PADACT_LOCK_RACER);
	return m ? m : AP_PADACT_BIT(first);
}

static inline void AP_PadActivityDecideRows(const ap_padact_facts *f, ap_padact_list *out)
{
	int m, entry, entryMask, hubShort;
	out->count = 0;
	switch (f->node)
	{
	case AP_PADACT_NODE_GARAGE:
		AP_PadActivityRow(out, AP_PADACT_BOSS, f->garageOpen ? AP_PADACT_OPEN : AP_PADACT_LOCK_BOSS, 1, -1);
		break;
	case AP_PADACT_NODE_CONTENT:
		for (m = 0; m < f->modeCount && m < AP_PADACT_MAX_ROWS; m++)
		{
			// What this race waits for on top of the pad's own entry: a later
			// race for the first race, a stage-2 race for the second requirement.
			int first = f->ctrFirst ? AP_PADACT_LOCK_CTRCHAL : AP_PADACT_LOCK_TROPHY;
			int bits = 0, reason = AP_PADACT_OPEN;
			if (m > 0 && !f->firstRaceWon)
				bits |= AP_PADACT_BIT(first);
			if (f->contentStage2[m] && !f->stage2Open)
				bits |= AP_PADACT_BIT(AP_PADACT_LOCK_STAGE2);
			if (!f->contentOpen[m] && (bits || f->padReqMet))
				reason = bits & AP_PADACT_BIT(first) ? first : AP_PADACT_LOCK_STAGE2;
			AP_PadActivityRow(out, f->contentMode[m], reason, f->contentLeft[m] ? 1 : 0, f->contentMode[m]);
			out->mask[out->count - 1] |= bits;
		}
		break;
	case AP_PADACT_NODE_SINGLE:
		AP_PadActivityRow(out, f->single, AP_PADACT_OPEN, f->padState != 5, -1);
		break;
	default:
		return;
	}
	// Pad-wide gates join a row's own answer: the hub Keys first, as the
	// player meets the doors before the pad, then the pad's entry. A row is
	// open only when none of them holds it back.
	hubShort = f->keysHave < f->keysNeed;
	if (f->node == AP_PADACT_NODE_GARAGE)
	{
		if (hubShort)
			AP_PadActivityAll(out, AP_PADACT_LOCK_HUB, 0);
		return;
	}
	entry = AP_PadActivityEntryReason(f);
	entryMask = AP_PadActivityEntryMask(f);
	// Nothing else matters on an empty, finished or uninstalled pad.
	if (entryMask & (AP_PADACT_BIT(AP_PADACT_LOCK_DONE) | AP_PADACT_BIT(AP_PADACT_LOCK_CLOSED) |
	                 AP_PADACT_BIT(AP_PADACT_LOCK_CONTENT)))
		for (m = 0; m < out->count; m++)
			out->mask[m] = 0;
	if (hubShort)
	{
		AP_PadActivityAll(out, AP_PADACT_LOCK_HUB, entryMask);
		return;
	}
	if (entry != AP_PADACT_OPEN)
	{
		// Every row shows the pad's reasons, so the list never shows a mode
		// open on a pad that will not take the kart.
		AP_PadActivityAll(out, entry, entryMask);
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
		"TROPHY RACE", "CTR CHALLENGE", "RELIC RACE", "CUP",
		"CRYSTAL CHALLENGE", "BOSS RACE"};
	return activity >= 0 && activity < AP_PADACT_KINDS ? names[activity] : "?";
}

static inline const char *AP_PadActivityReasonName(int reason)
{
	static const char *const names[AP_PADACT_REASONS] = {
		"", "NEEDS HUB KEYS", "PAD LOCKED", "NEEDS RACER", "WIN TROPHY RACE FIRST",
		"SECOND REQUIREMENT", "INSTALL CONTENT IN OPTIONS", "COMPLETE", "CLOSED",
		"BOSS LOCKED", "FINISH CTR CHALLENGE FIRST"};
	return reason >= 0 && reason < AP_PADACT_REASONS ? names[reason] : "LOCKED";
}

static inline void AP_PadActivityDecide(const ap_padact_facts *f, ap_padact_list *out)
{
	int i;
	AP_PadActivityDecideRows(f, out);
	// A row that is not open always names at least its first reason.
	for (i = 0; i < out->count; i++)
		if (out->reason[i] != AP_PADACT_OPEN && !out->mask[i])
			out->mask[i] = AP_PADACT_BIT(out->reason[i]);
}

// The reasons of a mask, in the order a player meets them, joined with ", "
// and split onto at most two lines of about `maxChars` characters (only
// between reasons; the second line takes the rest). Returns the line count,
// 0 for an empty mask.
static inline int AP_PadActivityReasonLines(int mask, int maxChars, char lines[2][112])
{
	static const int order[AP_PADACT_REASONS - 1] = {
		AP_PADACT_LOCK_HUB, AP_PADACT_LOCK_CLOSED, AP_PADACT_LOCK_DONE, AP_PADACT_LOCK_CONTENT,
		AP_PADACT_LOCK_PAD, AP_PADACT_LOCK_RACER, AP_PADACT_LOCK_TROPHY, AP_PADACT_LOCK_CTRCHAL,
		AP_PADACT_LOCK_STAGE2, AP_PADACT_LOCK_BOSS};
	int i, line = 0, n = 0;
	lines[0][0] = lines[1][0] = 0;
	for (i = 0; i < AP_PADACT_REASONS - 1; i++)
	{
		const char *name;
		int len, cur;
		if (!(mask & AP_PADACT_BIT(order[i])))
			continue;
		name = AP_PadActivityReasonName(order[i]);
		len = (int)strlen(name);
		cur = (int)strlen(lines[line]);
		if (cur > 0 && line == 0 && cur + 2 + len > maxChars)
			line = 1, cur = 0;
		if (cur + (cur ? 2 : 0) + len >= 110)
			continue;
		if (cur)
		{
			strcat(lines[line], ", ");
		}
		strcat(lines[line], name);
		n++;
	}
	return n ? (lines[1][0] ? 2 : 1) : 0;
}

#endif
