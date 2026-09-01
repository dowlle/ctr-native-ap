#ifndef AP_NAVREC_LANE_LOGIC_H
#define AP_NAVREC_LANE_LOGIC_H

// ============================================================================
// Which recordings feed the three engine nav lanes, and which lap of each.
//
// FREESTANDING, like ap_navrec_format.h: no engine headers, no file IO, no
// logging. The loader in ap/ap_navrec.c supplies candidates through a fetch
// callback and this header decides; tools/test-navrec.c drives the same code
// with candidates held in memory, so the rule the client runs is the rule the
// harness asserts.
//
// The problem it solves: the loader used to take ONE container and fill all
// three lanes from it, so every recorded-line bot in a race drove one person's
// lines under one name, and a newest-index community file permanently shadowed
// the player's own older recordings.
// ============================================================================

#include "ap_navrec_format.h"

// How many candidate recordings the loader will OPEN while choosing the files
// for a race. A rejected file falls back to the next lower number, so one corrupt
// newest file cannot hide every good older one, but a folder full of corrupt
// files must not cost a race start hundreds of reads either. The same budget
// bounds the search for a second and third author: a folder holding fifty of one
// person's laps stops looking for someone else after eight files, not fifty.
#define AP_NAVREC_MAX_LOAD_ATTEMPTS 8

// What the loader learns about one candidate file. `usable` is 0 when the file
// was rejected whole, which is still the format's rule: a rejected file fills no
// lane, but it also does not stop the scan from reaching older good ones.
struct AP_NavRecLaneCandidate
{
	unsigned int fileIndex;
	int          usable;
	char         driverName[AP_NAVREC_NAME_FIELD + 1];
	unsigned int lapCount;
};

// 1 when a file with this index exists and `out` was filled, 0 when there is no
// such file. A gap costs nothing and does not consume the open budget.
typedef int (*AP_NavRecLaneFetch)(void *ctx, unsigned int fileIndex, struct AP_NavRecLaneCandidate *out);

static int AP_NavRecLane_AuthorSeen(const char names[][AP_NAVREC_NAME_FIELD + 1], unsigned int count, const char *candidate)
{
	unsigned int i;

	for (i = 0; i < count; i++)
	{
		if (strcmp(names[i], candidate) == 0)
			return 1;
	}

	return 0;
}

// Choose up to AP_NAVREC_LANES distinct files, newest index first.
//
// Newest-first is kept: the most recent recording is the one the player just
// made and the one they expect to see. On top of that, a file whose driver name
// is already committed is held back rather than taken, so the field mixes
// authors whenever the folder allows it. Held-back files are not discarded: once
// the scan runs out of new authors they fill the remaining lanes in the order
// they were met, which is still newest-first. Nothing here is random, so the
// same folder always produces the same field.
//
// `maxOpened` bounds how many files are actually opened, so a folder full of
// corrupt containers costs a bounded number of reads at a race start rather than
// hundreds. Gaps do not count against it.
//
// Returns how many files were chosen; outChosen and outAuthor hold that many
// entries, in lane order. outOpened, when supplied, reports how many files were
// actually opened, so a caller that found nothing can say whether the folder was
// empty or the budget ran out on rejected containers.
static unsigned int AP_NavRecLane_Select(unsigned int highestIndex, unsigned int maxOpened, AP_NavRecLaneFetch fetch, void *ctx,
                                         unsigned int *outChosen, char outAuthor[][AP_NAVREC_NAME_FIELD + 1], unsigned int *outOpened)
{
	unsigned int spare[AP_NAVREC_LANES];
	char         spareAuthor[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
	unsigned int chosenCount = 0;
	unsigned int spareCount = 0;
	unsigned int opened = 0;
	unsigned int index;
	unsigned int i;

	if (outOpened != NULL)
		*outOpened = 0;

	if ((fetch == NULL) || (outChosen == NULL) || (outAuthor == NULL))
		return 0;

	for (index = highestIndex; (index >= 1u) && (opened < maxOpened) && (chosenCount < (unsigned int)AP_NAVREC_LANES); index--)
	{
		struct AP_NavRecLaneCandidate cand;

		memset(&cand, 0, sizeof cand);
		if (!fetch(ctx, index, &cand))
			continue;

		opened++;
		if (!cand.usable)
			continue;

		if (AP_NavRecLane_AuthorSeen(outAuthor, chosenCount, cand.driverName))
		{
			// A second file by an author already on the grid. Only as many spares
			// as there are lanes left to fill can ever be used, and a spare only
			// exists once a file has been committed, so this array cannot
			// overflow. The author is carried with the index: a spare matched
			// SOME committed author, not necessarily the first one.
			if (spareCount < (unsigned int)(AP_NAVREC_LANES - 1))
			{
				memset(spareAuthor[spareCount], 0, (size_t)AP_NAVREC_NAME_FIELD + 1u);
				memcpy(spareAuthor[spareCount], cand.driverName, strlen(cand.driverName));
				spare[spareCount] = cand.fileIndex;
				spareCount++;
			}
			continue;
		}

		memset(outAuthor[chosenCount], 0, (size_t)AP_NAVREC_NAME_FIELD + 1u);
		memcpy(outAuthor[chosenCount], cand.driverName, strlen(cand.driverName));
		outChosen[chosenCount] = cand.fileIndex;
		chosenCount++;
	}

	// Out of new authors. Distinct FILES still beat repeating one file's laps, so
	// the held-back ones take the remaining lanes before the plan below starts
	// reusing a file.
	for (i = 0; (chosenCount < (unsigned int)AP_NAVREC_LANES) && (i < spareCount); i++)
	{
		memcpy(outAuthor[chosenCount], spareAuthor[i], (size_t)AP_NAVREC_NAME_FIELD + 1u);
		outChosen[chosenCount] = spare[i];
		chosenCount++;
	}

	if (outOpened != NULL)
		*outOpened = opened;

	return chosenCount;
}

// Lane to (which chosen file, which of that file's laps).
//
// Three files: one lap each, three names above the field. Fewer: the accepted
// files repeat in order and each repeat steps to the next lap, so a lane never
// duplicates a line another lane already drives. With ONE file that is exactly
// what the loader did before this existed, lane i taking lap i, which is what
// keeps the single-file case byte-for-byte unchanged.
//
// A lane whose lap index is past its file's lap count has no line of its own;
// the loader synthesises it as a lateral offset of that file's first lane, as it
// always has.
static void AP_NavRecLane_Plan(unsigned int fileCount, unsigned int *laneFile, unsigned int *laneLap)
{
	unsigned int lane;

	if ((fileCount == 0u) || (laneFile == NULL) || (laneLap == NULL))
		return;

	for (lane = 0; lane < (unsigned int)AP_NAVREC_LANES; lane++)
	{
		laneFile[lane] = lane % fileCount;
		laneLap[lane] = lane / fileCount;
	}
}

#endif // AP_NAVREC_LANE_LOGIC_H
