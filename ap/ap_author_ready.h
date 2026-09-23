#ifndef AP_AUTHOR_READY_H
#define AP_AUTHOR_READY_H

// Author markers share the runtime spawn pool with boxes. They may queue and
// accept input only after loading is idle and the local driver's instance has
// been born; otherwise the level transition can reset the pool underneath them.
static inline int AP_AuthorRuntimeReady(int hasState, int loadIsIdle,
                                        int hasDriver, int hasDriverInstance)
{
	return hasState && loadIsIdle && hasDriver && hasDriverInstance;
}

// #194: both field crashes happened with author mode on in a boss race
// (Ripper Roo, Papu Papu). A boss race loads the same levelID as the normal
// race on that track, so author mode used to rebuild the track's markers and
// accept drops there. Boss races add nothing to authoring (the placement is the
// same spot on the same track), so author mode stands down in them: no
// markers, no keys. The ordinary race on the same track stays authorable.
// Author-only: runtime AP boxes in boss races are a separate path.
static inline int AP_AuthorRaceAllowsAuthoring(int isBossRace)
{
	return !isBossRace;
}

#endif
