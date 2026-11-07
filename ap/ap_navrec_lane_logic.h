#ifndef AP_NAVREC_LANE_LOGIC_H
#define AP_NAVREC_LANE_LOGIC_H

// ============================================================================
// Which recordings race, and on which of the three engine nav lanes.
//
// FREESTANDING, like ap_navrec_format.h: no engine headers, no file IO, no
// logging. The loader in ap/ap_navrec.c builds the pool index from file headers
// and supplies full reads through a fetch callback; tools/test-navrec.c drives
// the same code with files held in memory, so the rule the client runs is the
// rule the harness asserts.
//
// The rule (A3-19, maintainer ruling 2026-11-07):
//
//   1. Every compatible file for the track is in the pool, found by a header
//      read, not just the newest eight. An old file by the only person who ever
//      recorded a track is as eligible as twenty newer files by someone else.
//   2. Files are grouped by contributor, the driver name compared without case
//      or repeated spaces. Unnamed files are one group. At most one file per
//      contributor races, so no name appears twice.
//   3. Contributors, then the file and the lap within it, are picked at random
//      every race ("Random every race"). The seed is logged with the files it
//      picked, so a report can be reproduced.
//   4. With k contributors (0 to 3), lanes 0 to k-1 are recorded and the other
//      lanes keep the track's own AI lines. There are no synthesised offset
//      copies any more.
// ============================================================================

#include "ap_navrec_format.h"

// How many chosen files the loader will fully READ for one race. The pool index
// itself costs one header read per file; this bounds the expensive part, so a
// pool where every pick turns out corrupt still costs a bounded race start.
#define AP_NAVREC_MAX_LOAD_ATTEMPTS 24

// What a full read tells the pick about one file. `usable` is 0 when the file was
// rejected whole or holds no eligible lap; it is then passed over like a gap.
struct AP_NavRecLaneCandidate
{
	unsigned int fileIndex;
	int          usable;
	char         driverName[AP_NAVREC_NAME_FIELD + 1];
	unsigned int lapCount; // eligible laps, numbered 0..lapCount-1
};

// 1 when the file was opened (usable or not) and `out` was filled, 0 when it is
// gone. A gone file costs nothing against the read budget.
typedef int (*AP_NavRecLaneFetch)(void *ctx, unsigned int fileIndex, struct AP_NavRecLaneCandidate *out);

// One file in the pool index, from its header.
struct AP_NavRecPoolEntry
{
	unsigned int fileIndex;
	char         key[AP_NAVREC_NAME_FIELD + 1]; // normalized contributor
};

// What raced on one recorded lane.
struct AP_NavRecPoolPick
{
	unsigned int fileIndex;
	unsigned int lapOrdinal; // index into the file's eligible laps
	char         driverName[AP_NAVREC_NAME_FIELD + 1];
};

// Contributor key: lower case, inner runs of spaces folded to one. The name has
// already been through AP_NavRecFormat_SanitizeName, so it is printable ASCII
// with no leading or trailing space. "BEXUS", "bexus" and "Bexus " are one
// person; an empty name is the anonymous group.
static void AP_NavRecPool_Key(const char *name, char out[AP_NAVREC_NAME_FIELD + 1])
{
	unsigned int n = 0;
	int          space = 0;

	memset(out, 0, AP_NAVREC_NAME_FIELD + 1);
	if (name == NULL)
		return;

	for (; (*name != '\0') && (n < (unsigned int)AP_NAVREC_NAME_FIELD); name++)
	{
		char c = *name;

		if (c == ' ')
		{
			space = 1;
			continue;
		}
		if (space && (n > 0u) && (n < (unsigned int)AP_NAVREC_NAME_FIELD))
			out[n++] = ' ';
		space = 0;
		if ((c >= 'A') && (c <= 'Z'))
			c = (char)(c - 'A' + 'a');
		if (n < (unsigned int)AP_NAVREC_NAME_FIELD)
			out[n++] = c;
	}
}

// Does a peeked header belong in this load's pool? Same identity rule the full
// read applies (AP_NavRecFormat_IdentityMatches), so the pool never offers a file
// the read would refuse for its track.
static int AP_NavRecPool_EntryCompatible(const struct AP_NavRecFileInfo *peeked, int levelId, unsigned char identityKind,
                                         const unsigned char trackUuid[AP_NAVREC_TRACK_UUID_BYTES], unsigned int navRevision)
{
	return AP_NavRecFormat_IdentityMatches(peeked, levelId, identityKind, trackUuid, navRevision);
}

// xorshift32. Never returns 0 and never sticks at 0: a zero seed is moved off it.
static unsigned int AP_NavRecPool_Next(unsigned int *state)
{
	unsigned int x = *state;

	if (x == 0u)
		x = 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static unsigned int AP_NavRecPool_Below(unsigned int *state, unsigned int bound)
{
	return (bound == 0u) ? 0u : (AP_NavRecPool_Next(state) % bound);
}

static void AP_NavRecPool_Shuffle(unsigned int *items, unsigned int count, unsigned int *state)
{
	unsigned int i;

	for (i = count; i > 1u; i--)
	{
		unsigned int j = AP_NavRecPool_Below(state, i);
		unsigned int t = items[i - 1u];

		items[i - 1u] = items[j];
		items[j] = t;
	}
}

// Pick up to AP_NAVREC_LANES contributors from the pool, one file and one lap
// each, at random from `seed`.
//
// `work` must hold 2 * count entries. Contributors are put in a random order;
// for each, that person's files are tried in a random order until one reads as
// usable, and a random eligible lap of it is taken. A contributor whose files
// all fail is skipped, so one broken file never costs a person their lane while
// they have another good one. At most `maxOpened` full reads are spent.
//
// The same pool and the same seed always give the same picks, which is what a
// savestate restore in the same session relies on.
//
// Returns the number of picks; outOpened reports the full reads spent and
// outContributors the number of distinct contributors in the pool.
static unsigned int AP_NavRecPool_Pick(const struct AP_NavRecPoolEntry *entries, unsigned int count, unsigned int seed, unsigned int maxOpened,
                                       AP_NavRecLaneFetch fetch, void *ctx, unsigned int *work, struct AP_NavRecPoolPick out[AP_NAVREC_LANES],
                                       unsigned int *outOpened, unsigned int *outContributors)
{
	unsigned int *reps;
	unsigned int *files;
	unsigned int  repCount = 0;
	unsigned int  picked = 0;
	unsigned int  opened = 0;
	unsigned int  state = seed;
	unsigned int  i;
	unsigned int  r;

	if (outOpened != NULL)
		*outOpened = 0;
	if (outContributors != NULL)
		*outContributors = 0;
	if ((entries == NULL) || (fetch == NULL) || (work == NULL) || (out == NULL) || (count == 0u))
		return 0;

	reps = work;
	files = work + count;

	// One representative entry per contributor, in index order.
	for (i = 0; i < count; i++)
	{
		unsigned int k;
		int          seen = 0;

		for (k = 0; k < repCount; k++)
		{
			if (strcmp(entries[reps[k]].key, entries[i].key) == 0)
			{
				seen = 1;
				break;
			}
		}
		if (!seen)
			reps[repCount++] = i;
	}

	if (outContributors != NULL)
		*outContributors = repCount;

	AP_NavRecPool_Shuffle(reps, repCount, &state);

	for (r = 0; (r < repCount) && (picked < (unsigned int)AP_NAVREC_LANES) && (opened < maxOpened); r++)
	{
		const char  *key = entries[reps[r]].key;
		unsigned int fileCount = 0;
		unsigned int f;

		for (i = 0; i < count; i++)
		{
			if (strcmp(entries[i].key, key) == 0)
				files[fileCount++] = i;
		}

		AP_NavRecPool_Shuffle(files, fileCount, &state);

		for (f = 0; (f < fileCount) && (opened < maxOpened); f++)
		{
			struct AP_NavRecLaneCandidate cand;

			memset(&cand, 0, sizeof cand);
			if (!fetch(ctx, entries[files[f]].fileIndex, &cand))
				continue;

			opened++;
			if (!cand.usable || (cand.lapCount == 0u))
				continue;

			memset(&out[picked], 0, sizeof out[picked]);
			out[picked].fileIndex = entries[files[f]].fileIndex;
			out[picked].lapOrdinal = AP_NavRecPool_Below(&state, cand.lapCount);
			memcpy(out[picked].driverName, cand.driverName, sizeof out[picked].driverName);
			out[picked].driverName[AP_NAVREC_NAME_FIELD] = '\0';
			picked++;
			break;
		}
	}

	if (outOpened != NULL)
		*outOpened = opened;

	return picked;
}

#endif // AP_NAVREC_LANE_LOGIC_H
