#ifndef AP_RACE_STRUCTURE_LOGIC_H
#define AP_RACE_STRUCTURE_LOGIC_H

// The pad race type menu (#208, one generator row 6), freestanding so its
// harness pins it out of engine.
//
// race_type_selector  "productive" | "always", in the plan's `pad_client`
//                     record. When the menu opens. The seed value is the
//                     starting value; the in-game Options row (SEED / ALWAYS /
//                     PRODUCTIVE) overrides it live (decision 15). Presentation
//                     only.
//
// The menu serves content-plan track pads. Rows follow the pad's own race order
// and exist for the races the pad has open now. A row with no open check is
// grey but can still be chosen (a replay). The cursor starts on the first row
// with an open check. A room without a plan never reaches it.

enum
{
	AP_RS_TROPHY = 0, // the same values as AP_CONTENT_MODE_*
	AP_RS_CTR = 1,
	AP_RS_RELIC = 2,
	AP_RS_COUNT = 3
};

enum
{
	AP_RS_SELECTOR_PRODUCTIVE = 0,
	AP_RS_SELECTOR_ALWAYS = 1
};

// AP_RaceSelectorDecide results besides a mode.
#define AP_RS_OPEN_MENU (-1)
#define AP_RS_NO_ROUTE  (-2)

typedef struct
{
	int count;
	int mode[AP_RS_COUNT];
	int productive[AP_RS_COUNT];
	int productiveCount;
	int cursor;
} ap_rs_rows;

// The selector setting in force: the in-game Options row (-1 SEED, 0
// PRODUCTIVE, 1 ALWAYS) over the seed's value (decision 15).
static inline int AP_RaceSelectorEffective(int menuValue, int seedValue)
{
	if (menuValue == AP_RS_SELECTOR_PRODUCTIVE || menuValue == AP_RS_SELECTOR_ALWAYS)
		return menuValue;
	return seedValue;
}

// The entry decision. `always`: the menu opens whenever a row exists.
// `productive`: exactly one productive row loads directly; several, or none
// with rows to replay, open the menu. No row at all: AP_RS_NO_ROUTE, the
// caller releases the kart.
static inline int AP_RaceSelectorDecide(const ap_rs_rows *rows, int selector)
{
	int i;
	if (rows->count == 0)
		return AP_RS_NO_ROUTE;
	if (selector == AP_RS_SELECTOR_ALWAYS || rows->productiveCount != 1)
		return AP_RS_OPEN_MENU;
	for (i = 0; i < rows->count; i++)
		if (rows->productive[i])
			return rows->mode[i];
	return AP_RS_OPEN_MENU;
}

// Grey-but-selectable rows as a bit mask over row indices (RECTMENU replay).
static inline unsigned AP_RaceSelectorReplayMask(const ap_rs_rows *rows)
{
	unsigned mask = 0;
	int i;
	for (i = 0; i < rows->count; i++)
		if (!rows->productive[i])
			mask |= 1u << i;
	return mask;
}

// Which races earn a track's podium rungs (#208, 0.3.0). A Trophy Race does; so
// does a CTR Challenge on a ctr_first pad, by finishing place, letters or not
// (decision 14 of 2026-10-28).
static inline int AP_RaceStructureRaceEarnsRungs(int trophyRace, int tokenRace, int destCtrFirst)
{
	return trophyRace || (tokenRace && destCtrFirst);
}

#endif
