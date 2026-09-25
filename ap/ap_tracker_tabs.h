#ifndef AP_TRACKER_TABS_H
#define AP_TRACKER_TABS_H

// Pure tab and racer-row model for the enlarged Adventure tracker. The five hub
// maps come first, then the RACERS and ITEMS tabs, all on the one L1/R1 cycle.
// A tab with nothing to show for the seed is skipped rather than drawn empty.
// Kept free of engine types so tools/test-tracker-tabs.c exercises the same
// rules the tracker draws from.

#define AP_TRACKER_TAB_HUBS 5
enum
{
	AP_TRACKER_TAB_RACERS = AP_TRACKER_TAB_HUBS,
	AP_TRACKER_TAB_ITEMS,
	AP_TRACKER_TAB_COUNT
};

// Dense physical pads 0..27 and the five Gem Cup pads 100..104, the same two
// ranges ctr_cfg.racer_lock / gem_cup_racer_lock cover.
#define AP_TRACKER_TAB_DENSE_PADS 28
#define AP_TRACKER_TAB_CUP_PADS 5
#define AP_TRACKER_TAB_RACERS_COUNT 16

// RACERS has something to say when the seed's character phase makes racers
// items or locks pads to them, or when Hit Character is on. A seed with every
// racer unlocked, no locks and no Hit shows nothing new, so the tab is skipped.
static inline int AP_TrackerRacersTabPure(int phasePresent, int characterUnlocks, int racerLocks, int hitOn)
{
	return (phasePresent && (characterUnlocks || racerLocks)) || hitOn ? 1 : 0;
}

static inline int AP_TrackerTabShownPure(int tab, int racersOn, int itemsOn)
{
	if (tab >= 0 && tab < AP_TRACKER_TAB_HUBS)
		return 1;
	if (tab == AP_TRACKER_TAB_RACERS)
		return racersOn ? 1 : 0;
	if (tab == AP_TRACKER_TAB_ITEMS)
		return itemsOn ? 1 : 0;
	return 0;
}

// Next shown tab in direction dir (<0 left, otherwise right), wrapping. The hub
// tabs are always shown, so this always lands on a real tab.
static inline int AP_TrackerTabStepPure(int tab, int dir, int racersOn, int itemsOn)
{
	int i;
	if (tab < 0 || tab >= AP_TRACKER_TAB_COUNT)
		tab = 0;
	for (i = 0; i < AP_TRACKER_TAB_COUNT; i++)
	{
		tab = (tab + (dir < 0 ? AP_TRACKER_TAB_COUNT - 1 : 1)) % AP_TRACKER_TAB_COUNT;
		if (AP_TrackerTabShownPure(tab, racersOn, itemsOn))
			return tab;
	}
	return 0;
}

// A tab that stops being shown while the tracker is open (a seed swap) falls
// back to the hub map the player was last on.
static inline int AP_TrackerTabSettlePure(int tab, int hub, int racersOn, int itemsOn)
{
	if (AP_TrackerTabShownPure(tab, racersOn, itemsOn))
		return tab;
	return hub >= 0 && hub < AP_TRACKER_TAB_HUBS ? hub : 0;
}

typedef struct
{
	int unlocked;       // AP_CharacterUnlocked for this engine id
	int hit;            // 0 absent, 1 pending, 2 done (Hit Character check)
	unsigned densePads; // bit p: dense physical pad p is locked to this racer
	unsigned cupPads;   // bit c: Gem Cup pad 100 + c is locked to this racer
} AP_TrackerRacerRow;

// One row per engine character id 0..15. Lock values outside 0..15 (the -1
// "no lock" and anything malformed) name no racer.
static inline void AP_TrackerRacerRowsPure(
	const int denseLock[AP_TRACKER_TAB_DENSE_PADS],
	const int cupLock[AP_TRACKER_TAB_CUP_PADS],
	const unsigned char unlocked[AP_TRACKER_TAB_RACERS_COUNT],
	const unsigned char hitState[AP_TRACKER_TAB_RACERS_COUNT],
	AP_TrackerRacerRow rows[AP_TRACKER_TAB_RACERS_COUNT])
{
	int i;
	for (i = 0; i < AP_TRACKER_TAB_RACERS_COUNT; i++)
	{
		rows[i].unlocked = unlocked[i] ? 1 : 0;
		rows[i].hit = hitState[i] <= 2 ? hitState[i] : 0;
		rows[i].densePads = 0;
		rows[i].cupPads = 0;
	}
	for (i = 0; i < AP_TRACKER_TAB_DENSE_PADS; i++)
		if (denseLock[i] >= 0 && denseLock[i] < AP_TRACKER_TAB_RACERS_COUNT)
			rows[denseLock[i]].densePads |= 1u << i;
	for (i = 0; i < AP_TRACKER_TAB_CUP_PADS; i++)
		if (cupLock[i] >= 0 && cupLock[i] < AP_TRACKER_TAB_RACERS_COUNT)
			rows[cupLock[i]].cupPads |= 1u << i;
}

#endif
