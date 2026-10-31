#ifndef AP_RACE_STRUCTURE_LOGIC_H
#define AP_RACE_STRUCTURE_LOGIC_H

// Which races earn a track's podium rungs (#208, 0.3.0). Freestanding so the
// ctr_first harness pins it out of engine. A Trophy Race does; so does a CTR
// Challenge on a ctr_first pad, by finishing place, letters or not (decision 14
// of 2026-10-28).
static inline int AP_RaceStructureRaceEarnsRungs(int trophyRace, int tokenRace, int destCtrFirst)
{
	return trophyRace || (tokenRace && destCtrFirst);
}

#endif
