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
//   5. (A5-7, maintainer ruling 2026-11-15, client setting nav_repeat_laps, off by
//      default.) With the setting on, once every contributor has a lane the
//      remaining lanes are filled with other laps from the same contributors:
//      the contributor with the fewest lanes first, a file not yet raced before
//      another lap of a raced file, and never the same lap twice in one race.
//      The setting off is rule 4 exactly, including the random numbers drawn.
// ============================================================================

#include "ap_navrec_format.h"

// How many chosen files the loader will fully READ for one race. The pool index
// itself costs one header read per file; this bounds the expensive part, so a
// pool where every pick turns out corrupt still costs a bounded race start.
#define AP_NAVREC_MAX_LOAD_ATTEMPTS 24

// Full reads one contributor may spend before the FIRST pass (one lane per
// contributor) moves on. Without it, a person whose many files all fail the full
// read could use the whole budget above before anyone else is tried. The repeat
// pass (A5-7) does not use this cap: it only runs after every contributor has had
// their turn, so the cap has nothing left to protect and would otherwise stop one
// contributor with a few broken files from filling lanes; the overall budget
// still bounds it.
#define AP_NAVREC_MAX_READS_PER_CONTRIBUTOR 3

// Entries of scratch the caller provides per pool entry.
#define AP_NAVREC_POOL_WORK_PER_ENTRY 3u

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

// A null filter means all names are allowed. An empty filter means there is no
// configured identity to match, so anonymous recordings never count as ours.
static inline int AP_NavRecPool_NameMatches(const char *driverName, const char *onlyKey)
{
	char key[AP_NAVREC_NAME_FIELD + 1];

	if (onlyKey == NULL)
		return 1;
	AP_NavRecPool_Key(driverName, key);
	return onlyKey[0] != '\0' && strcmp(key, onlyKey) == 0;
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


// One lane's pick so far, kept beside the output for the repeat pass.
struct AP_NavRecPoolFillState
{
	const struct AP_NavRecPoolEntry *entries;
	unsigned int                     count;
	AP_NavRecLaneFetch               fetch;
	void                            *ctx;
	unsigned int                    *files;
	unsigned int                    *tried;
	unsigned int                     maxOpened;
	unsigned int                    *opened;
	unsigned int                    *state;
	struct AP_NavRecPoolPick        *out;
	unsigned int                    *picked;
	unsigned int                     pickEntry[AP_NAVREC_LANES]; // pool entry index behind each pick
	unsigned int                     pickLaps[AP_NAVREC_LANES];  // eligible laps in that pick's file
};

// Try to give one more lane to the contributor `key`: a file not raced yet, or
// failing that an unused lap of a file already raced. Returns 1 when a lane was
// added.
static int AP_NavRecPool_TryFill(struct AP_NavRecPoolFillState *fs, const char *key)
{
	unsigned int fileCount = 0;
	unsigned int i;
	unsigned int f;
	unsigned int p;
	unsigned int total = 0;

	for (i = 0; i < fs->count; i++)
	{
		int raced = 0;

		if (strcmp(fs->entries[i].key, key) != 0)
			continue;
		if (fs->tried[i])
			continue;
		for (p = 0; p < *fs->picked; p++)
		{
			if (fs->pickEntry[p] == i)
				raced = 1;
		}
		if (!raced)
			fs->files[fileCount++] = i;
	}

	AP_NavRecPool_Shuffle(fs->files, fileCount, fs->state);

	for (f = 0; (f < fileCount) && (*fs->opened < fs->maxOpened); f++)
	{
		struct AP_NavRecLaneCandidate cand;
		unsigned int                  at = *fs->picked;

		fs->tried[fs->files[f]] = 1u;
		memset(&cand, 0, sizeof cand);
		if (!fs->fetch(fs->ctx, fs->entries[fs->files[f]].fileIndex, &cand))
			continue;

		(*fs->opened)++;
		if (!cand.usable || (cand.lapCount == 0u))
			continue;

		memset(&fs->out[at], 0, sizeof fs->out[at]);
		fs->out[at].fileIndex = fs->entries[fs->files[f]].fileIndex;
		fs->out[at].lapOrdinal = AP_NavRecPool_Below(fs->state, cand.lapCount);
		memcpy(fs->out[at].driverName, cand.driverName, sizeof fs->out[at].driverName);
		fs->out[at].driverName[AP_NAVREC_NAME_FIELD] = '\0';
		fs->pickEntry[at] = fs->files[f];
		fs->pickLaps[at] = cand.lapCount;
		(*fs->picked)++;
		return 1;
	}

	// Another lap of a file this contributor already races. Every (file, lap)
	// pair at most once per race.
	for (p = 0; p < *fs->picked; p++)
	{
		unsigned int q;
		unsigned int used = 0;
		int          first = 1;

		if (strcmp(fs->entries[fs->pickEntry[p]].key, key) != 0)
			continue;
		for (q = 0; q < *fs->picked; q++)
		{
			if (fs->pickEntry[q] == fs->pickEntry[p])
			{
				used++;
				if (q < p)
					first = 0;
			}
		}
		if (first && (fs->pickLaps[p] > used))
			total += fs->pickLaps[p] - used;
	}

	if (total > 0u)
	{
		unsigned int want = AP_NavRecPool_Below(fs->state, total);

		for (p = 0; p < *fs->picked; p++)
		{
			unsigned int q;
			unsigned int o;
			int          first = 1;

			if (strcmp(fs->entries[fs->pickEntry[p]].key, key) != 0)
				continue;
			for (q = 0; q < p; q++)
			{
				if (fs->pickEntry[q] == fs->pickEntry[p])
					first = 0;
			}
			if (!first)
				continue;

			for (o = 0; o < fs->pickLaps[p]; o++)
			{
				int taken = 0;

				for (q = 0; q < *fs->picked; q++)
				{
					if ((fs->pickEntry[q] == fs->pickEntry[p]) && (fs->out[q].lapOrdinal == o))
						taken = 1;
				}
				if (taken)
					continue;
				if (want-- == 0u)
				{
					unsigned int at = *fs->picked;

					memset(&fs->out[at], 0, sizeof fs->out[at]);
					fs->out[at].fileIndex = fs->out[p].fileIndex;
					fs->out[at].lapOrdinal = o;
					memcpy(fs->out[at].driverName, fs->out[p].driverName, sizeof fs->out[at].driverName);
					fs->pickEntry[at] = fs->pickEntry[p];
					fs->pickLaps[at] = fs->pickLaps[p];
					(*fs->picked)++;
					return 1;
				}
			}
		}
	}

	return 0;
}

// Pick up to AP_NAVREC_LANES contributors from the pool, one file and one lap
// each, at random from `seed`.
//
// The first two thirds of `work` serve the one-lane pass. Contributors are put in a random order;
// for each, that person's files are tried in a random order until one reads as
// usable, and a random eligible lap of it is taken. A contributor whose files
// all fail is skipped, so one broken file never costs a person their lane while
// they have another good one. At most `maxOpened` full reads are spent, and at
// most AP_NAVREC_MAX_READS_PER_CONTRIBUTOR on one person.
//
// The same pool and the same seed always give the same picks, which is what a
// savestate restore in the same session relies on.
//
// `allowRepeat` (A5-7, nav_repeat_laps) adds a second pass after the one above:
// while lanes are free, the contributor holding the fewest lanes gets another
// (contributors tied on lanes in the pass-one random order), from a file not raced
// yet, else from an unused lap of a raced file. It never repeats a (file, lap)
// pair and is bounded by `maxOpened` only. With 0 the pass does not run and
// nothing extra is read or drawn, so the result is exactly the single-lane rule.
//
// `work` must hold AP_NAVREC_POOL_WORK_PER_ENTRY * count entries.
//
// Returns the number of picks; outOpened reports the full reads spent and
// outContributors the number of distinct contributors in the pool.
static unsigned int AP_NavRecPool_Pick(const struct AP_NavRecPoolEntry *entries, unsigned int count, unsigned int seed, unsigned int maxOpened,
                                       int allowRepeat, AP_NavRecLaneFetch fetch, void *ctx, unsigned int *work, struct AP_NavRecPoolPick out[AP_NAVREC_LANES],
                                       unsigned int *outOpened, unsigned int *outContributors)
{
	struct AP_NavRecPoolFillState fs;
	unsigned int *reps;
	unsigned int *files;
	unsigned int *tried;
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
	tried = work + 2u * count;
	memset(tried, 0, sizeof *tried * count);
	memset(&fs, 0, sizeof fs);

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
		unsigned int spent;
		unsigned int f;

		for (i = 0; i < count; i++)
		{
			if (strcmp(entries[i].key, key) == 0)
				files[fileCount++] = i;
		}

		AP_NavRecPool_Shuffle(files, fileCount, &state);

		for (f = 0, spent = 0; (f < fileCount) && (opened < maxOpened) && (spent < AP_NAVREC_MAX_READS_PER_CONTRIBUTOR); f++)
		{
			struct AP_NavRecLaneCandidate cand;

			memset(&cand, 0, sizeof cand);
			if (!fetch(ctx, entries[files[f]].fileIndex, &cand))
			{
				tried[files[f]] = 1u;
				continue;
			}

			opened++;
			spent++;
			tried[files[f]] = 1u;
			if (!cand.usable || (cand.lapCount == 0u))
				continue;

			memset(&out[picked], 0, sizeof out[picked]);
			out[picked].fileIndex = entries[files[f]].fileIndex;
			out[picked].lapOrdinal = AP_NavRecPool_Below(&state, cand.lapCount);
			memcpy(out[picked].driverName, cand.driverName, sizeof out[picked].driverName);
			out[picked].driverName[AP_NAVREC_NAME_FIELD] = '\0';
			fs.pickEntry[picked] = files[f];
			fs.pickLaps[picked] = cand.lapCount;
			picked++;
			break;
		}
	}

	if (allowRepeat)
	{
		fs.entries = entries;
		fs.count = count;
		fs.fetch = fetch;
		fs.ctx = ctx;
		fs.files = files;
		fs.tried = tried;
		fs.maxOpened = maxOpened;
		fs.opened = &opened;
		fs.state = &state;
		fs.out = out;
		fs.picked = &picked;

		while (picked < (unsigned int)AP_NAVREC_LANES)
		{
			unsigned int held;
			int          got = 0;

			// The contributor with the fewest lanes goes first; the pass-one
			// shuffle breaks ties.
			for (held = 0; (held < (unsigned int)AP_NAVREC_LANES) && !got; held++)
			{
				for (r = 0; (r < repCount) && !got; r++)
				{
					const char  *key = entries[reps[r]].key;
					unsigned int have = 0;
					unsigned int p;

					for (p = 0; p < picked; p++)
					{
						if (strcmp(entries[fs.pickEntry[p]].key, key) == 0)
							have++;
					}
					if (have == held)
						got = AP_NavRecPool_TryFill(&fs, key);
				}
			}

			if (!got)
				break;
		}
	}

	if (outOpened != NULL)
		*outOpened = opened;

	return picked;
}

#endif // AP_NAVREC_LANE_LOGIC_H
