#ifndef AP_FXSEEN_LOGIC_H
#define AP_FXSEEN_LOGIC_H

#include <stdio.h>
#include <string.h>

typedef struct AP_FxSeenRow
{
	char endpoint[192];
	char seed[128];
	char slot[64];
	long long max;
} AP_FxSeenRow;

// Four columns are intentional. A legacy seed+slot+max row is ambiguous across
// separately hosted instances of one generated seed and must fail open.
static inline int AP_FxSeenParseRow(const char *line, AP_FxSeenRow *row)
{
	if (line == NULL || row == NULL)
		return 0;
	return sscanf(line, "%191[^\t]\t%127[^\t]\t%63[^\t]\t%lld",
	              row->endpoint, row->seed, row->slot, &row->max) == 4;
}

static inline int AP_FxSeenRowMatches(const AP_FxSeenRow *row,
	                                  const char *endpoint, const char *seed,
	                                  const char *slot)
{
	return row != NULL && endpoint != NULL && seed != NULL && slot != NULL &&
	       !strcmp(row->endpoint, endpoint) && !strcmp(row->seed, seed) &&
	       !strcmp(row->slot, slot);
}

#endif
