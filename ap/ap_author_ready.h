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

#endif
