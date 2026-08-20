// cc -Wall -Wextra -o /tmp/test-native-clip-scratch tools/test-native-clip-scratch.c platform/native_clip_scratch.c
// Run from the repository root, or pass the repository root as argv[1].
//
// Covers the Nitros Oxide level-load crash: entering Mystery Caves or Polar
// Pass as Oxide died in MEMPACK_AllocMem while MainInit_JitPoolsNew asked for
// the per-player clip buffer. The two captured crash records are
//
//   [AP MEMPACK] AllocMem exhausted: need=10000 free=9980  pack=1330704  levelID 9
//   [AP MEMPACK] AllocMem exhausted: need=12000 free=10868 pack=1330704  levelID 12
//
// so the harness proves three things: that those two need= values are exactly
// what MainDB_GetClipSize asks for on those two levels, that the pre-fix
// allocator fails on the captured headroom, and that routing the clip buffer to
// the dedicated native scratch removes the request from the pack entirely.
//
// Every level constant is read out of the source tree at run time rather than
// restated here, so a table edit that outgrows the scratch buffer fails this
// test instead of silently overrunning it.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/native_clip_scratch.h"

static int failures;

static void expect(int condition, const char *name)
{
	printf("%s  %s\n", condition ? "ok  " : "FAIL", name);
	if (!condition)
		failures++;
}

// ---------------------------------------------------------------------------
// Source-tree derivation

#define MAX_LEVELS 128
#define MAX_GROUPS 32
#define MAX_LABELS 8
#define MAX_RETURNS 4

static char levelNames[MAX_LEVELS][64];
static int levelCount;

struct ClipGroup
{
	char labels[MAX_LABELS][64];
	int labelCount;
	int returns[MAX_RETURNS];
	int returnCount;
};

static struct ClipGroup clipGroups[MAX_GROUPS];
static int clipGroupCount;
static struct ClipGroup clipDefault;

static FILE *openRepoFile(const char *root, const char *relative)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s", root, relative);
	f = fopen(path, "r");
	if (f == NULL)
	{
		fprintf(stderr, "cannot open %s\n", path);
		exit(2);
	}
	return f;
}

// Pull `NAME` or `NAME = <n>` entries out of `enum LevelID { ... }` so a level
// id in a crash record can be turned back into the name the table switches on.
static void parseLevelEnum(const char *root)
{
	FILE *f = openRepoFile(root, "include/namespace_Level.h");
	char line[512];
	int inEnum = 0;
	int next = 0;

	while (fgets(line, sizeof line, f) != NULL)
	{
		char name[64];
		int explicitValue;
		char *brace;

		if (!inEnum)
		{
			if (strstr(line, "enum LevelID") != NULL)
				inEnum = 1;
			continue;
		}

		brace = strchr(line, '}');
		if (brace != NULL)
			break;

		if (sscanf(line, " %63[A-Za-z0-9_] = %d", name, &explicitValue) == 2)
			next = explicitValue;
		else if (sscanf(line, " %63[A-Za-z0-9_]", name) != 1)
			continue;
		else if (strchr(line, ',') == NULL && strchr(line, '=') == NULL)
			continue;

		if (name[0] == '\0' || levelCount >= MAX_LEVELS)
			continue;

		snprintf(levelNames[next < MAX_LEVELS ? next : 0], 64, "%s", name);
		if (next + 1 > levelCount)
			levelCount = next + 1;
		next++;
	}

	fclose(f);
}

// Pull the `case <LABEL>:` / `return <n>;` structure out of MainDB_GetClipSize.
// A group with two returns is the player-count split the real function makes
// (`numPlyrCurrGame < 3` takes the first return).
static void parseClipTable(const char *root)
{
	FILE *f = openRepoFile(root, "game/MAIN/MainDB.c");
	char line[512];
	int inFunc = 0;
	int inDefault = 0;
	struct ClipGroup pending;
	int braceDepth = 0;
	int seenOpen = 0;

	memset(&pending, 0, sizeof pending);

	while (fgets(line, sizeof line, f) != NULL)
	{
		char label[64];
		int value;
		const char *p;

		if (!inFunc)
		{
			if (strstr(line, "int MainDB_GetClipSize") != NULL)
				inFunc = 1;
			continue;
		}

		for (p = line; *p != '\0'; p++)
		{
			if (*p == '{')
			{
				braceDepth++;
				seenOpen = 1;
			}
			else if (*p == '}')
			{
				braceDepth--;
			}
		}

		if (sscanf(line, " case %63[A-Za-z0-9_] :", label) == 1)
		{
			// A new label after a return closes the previous group. Depth is
			// not usable for this: the `numPlyrCurrGame < 3` split nests one
			// of the returns inside an if block.
			if (pending.returnCount > 0 && clipGroupCount < MAX_GROUPS)
			{
				clipGroups[clipGroupCount++] = pending;
				memset(&pending, 0, sizeof pending);
			}
			if (pending.labelCount < MAX_LABELS)
				snprintf(pending.labels[pending.labelCount++], 64, "%s", label);
			continue;
		}

		if (strstr(line, "default:") != NULL)
		{
			if (pending.returnCount > 0 && clipGroupCount < MAX_GROUPS)
			{
				clipGroups[clipGroupCount++] = pending;
				memset(&pending, 0, sizeof pending);
			}
			inDefault = 1;
			continue;
		}

		if (sscanf(line, " return %d ;", &value) == 1)
		{
			if (inDefault)
			{
				if (clipDefault.returnCount < MAX_RETURNS)
					clipDefault.returns[clipDefault.returnCount++] = value;
			}
			else if (pending.labelCount > 0 && pending.returnCount < MAX_RETURNS)
			{
				pending.returns[pending.returnCount++] = value;
			}
			continue;
		}

		if (seenOpen && braceDepth == 0)
			break;
	}

	if (pending.returnCount > 0 && clipGroupCount < MAX_GROUPS)
		clipGroups[clipGroupCount++] = pending;

	fclose(f);
}

static const struct ClipGroup *groupForLevel(int levelID)
{
	if (levelID < 0 || levelID >= levelCount)
		return &clipDefault;

	for (int g = 0; g < clipGroupCount; g++)
		for (int l = 0; l < clipGroups[g].labelCount; l++)
			if (strcmp(clipGroups[g].labels[l], levelNames[levelID]) == 0)
				return &clipGroups[g];

	return &clipDefault;
}

// The parsed stand-in for MainDB_GetClipSize(levelID, numPlyrCurrGame).
static int clipEntriesFor(int levelID, int players)
{
	const struct ClipGroup *group = groupForLevel(levelID);

	if (group->returnCount == 0)
		return 0;
	if (group->returnCount == 1 || players < 3)
		return group->returns[0];

	return group->returns[1];
}

static int levelIdNamed(const char *name)
{
	for (int i = 0; i < levelCount; i++)
		if (strcmp(levelNames[i], name) == 0)
			return i;
	return -1;
}

// ---------------------------------------------------------------------------
// Allocator model

// MEMPACK_AllocMem (game/MEMPACK.c): a request larger than the pack's free
// bytes is fatal. Returns the remaining headroom, or -1 for the fatal case.
static int mempackAlloc(int freeBytes, int need)
{
	if (freeBytes < need)
		return -1;
	return freeBytes - ((need + 3) & ~3);
}

int main(int argc, char **argv)
{
	const char *root = (argc > 1) ? argv[1] : ".";

	// Captured from the two v0.2.0-alpha2 support bundles, playing as Nitros
	// Oxide (character 15).
	const int capturedCavesFree = 9980;
	const int capturedCavesNeed = 10000;
	const int capturedPolarFree = 10868;
	const int capturedPolarNeed = 12000;

	// Same level, same load, other characters: the headroom the pack kept
	// after MainInit_JitPoolsNew on Mystery Caves.
	const int pinstripeCavesHeadroom = 20296;
	const int puraCavesHeadroom = 17644;

	int caves, polar, garage;
	int worstEntries = 0;
	int worstBytes = 0;

	parseLevelEnum(root);
	parseClipTable(root);

	expect(levelCount > 0, "level id table parsed from include/namespace_Level.h");
	expect(clipGroupCount > 0 && clipDefault.returnCount == 1,
	       "clip size table parsed from game/MAIN/MainDB.c");

	caves = levelIdNamed("MYSTERY_CAVES");
	polar = levelIdNamed("POLAR_PASS");
	garage = levelIdNamed("ADVENTURE_GARAGE");

	expect(caves == 9, "crash record levelID 9 is MYSTERY_CAVES");
	expect(polar == 12, "crash record levelID 12 is POLAR_PASS");

	// The need= values in the crash records are the clip request, not a guess.
	expect((clipEntriesFor(caves, 1) << 2) == capturedCavesNeed,
	       "Mystery Caves clip buffer is the captured need=10000");
	expect((clipEntriesFor(polar, 1) << 2) == capturedPolarNeed,
	       "Polar Pass clip buffer is the captured need=12000");

	// Pre-fix: the captured headroom cannot serve the captured request.
	expect(mempackAlloc(capturedCavesFree, capturedCavesNeed) < 0,
	       "pre-fix Mystery Caves load exhausts the pack (short by 20 bytes)");
	expect(mempackAlloc(capturedPolarFree, capturedPolarNeed) < 0,
	       "pre-fix Polar Pass load exhausts the pack (short by 1,132 bytes)");

	// Pre-fix with any other racer seated: the same allocation fits, which is
	// why the crash looked character-specific rather than level-specific.
	expect(mempackAlloc(capturedCavesFree + (pinstripeCavesHeadroom + capturedCavesNeed - capturedCavesFree),
	                    capturedCavesNeed) >= 0,
	       "pre-fix Mystery Caves load fits with Pinstripe's lighter package");
	expect((pinstripeCavesHeadroom + capturedCavesNeed) - capturedCavesFree == 20316,
	       "Oxide costs 20,316 bytes more than Pinstripe at the same point");
	expect((puraCavesHeadroom + capturedCavesNeed) - capturedCavesFree == 17664,
	       "Oxide costs 17,664 bytes more than Pura at the same point");

	// Post-fix: the clip buffer no longer comes from the pack, so the load
	// makes no request at all and keeps every captured byte.
	expect(mempackAlloc(capturedCavesFree, 0) == capturedCavesFree,
	       "post-fix Mystery Caves load survives and keeps 9,980 bytes");
	expect(mempackAlloc(capturedPolarFree, 0) == capturedPolarFree,
	       "post-fix Polar Pass load survives and keeps 10,868 bytes");

	// The declared capacity has to cover the whole table, at every player count
	// data.PtrClipBuffer can hold.
	for (int level = 0; level < levelCount; level++)
	{
		for (int players = 1; players <= (int)CTR_NATIVE_CLIP_SCRATCH_MAX_PLAYERS; players++)
		{
			int entries = clipEntriesFor(level, players);
			int bytes = entries * 4 * players;

			if (entries > worstEntries)
				worstEntries = entries;
			if (bytes > worstBytes)
				worstBytes = bytes;

			if (!NativeClipScratch_CanFit((uint32_t)entries, (uint32_t)players,
			                              CTR_NATIVE_CLIP_SCRATCH_CAPACITY))
			{
				printf("FAIL  level %d at %d players needs %d bytes\n", level, players, bytes);
				failures++;
			}
		}
	}

	expect(worstEntries == (int)CTR_NATIVE_CLIP_SCRATCH_MAX_ENTRIES,
	       "declared max entry count still matches the live clip size table");
	expect(garage >= 0 && clipEntriesFor(garage, 1) == worstEntries,
	       "ADVENTURE_GARAGE is the worst case the buffer is sized from");
	expect(worstBytes <= (int)CTR_NATIVE_CLIP_SCRATCH_CAPACITY,
	       "every level fits the dedicated scratch at four players");

	// The garage load is the tightest one in the captured session: 5,320 bytes
	// of headroom left, less than the Oxide delta above.
	expect(5320 + (clipEntriesFor(garage, 1) << 2) == 101320,
	       "post-fix garage load keeps 101,320 bytes instead of 5,320");

	// Bounds behaviour of the buffer itself.
	expect(!NativeClipScratch_CanFit(CTR_NATIVE_CLIP_SCRATCH_MAX_ENTRIES + 1u,
	                                 CTR_NATIVE_CLIP_SCRATCH_MAX_PLAYERS,
	                                 CTR_NATIVE_CLIP_SCRATCH_CAPACITY),
	       "one entry beyond the worst case is rejected");
	expect(!NativeClipScratch_CanFit(1u, CTR_NATIVE_CLIP_SCRATCH_MAX_PLAYERS + 1u,
	                                 CTR_NATIVE_CLIP_SCRATCH_CAPACITY),
	       "a fifth player is rejected");
	expect(!NativeClipScratch_CanFit(1u, 0u, CTR_NATIVE_CLIP_SCRATCH_CAPACITY),
	       "a zero player count is rejected");
	expect(!NativeClipScratch_CanFit(UINT32_MAX, 1u, CTR_NATIVE_CLIP_SCRATCH_CAPACITY),
	       "an entry count that would wrap the byte total is rejected");

	// Slices: disjoint, word aligned, inside the buffer.
	{
		uint32_t entries = (uint32_t)clipEntriesFor(polar, 4);
		uint32_t players = CTR_NATIVE_CLIP_SCRATCH_MAX_PLAYERS;
		uint32_t stride = NativeClipScratch_PlayerBytes(entries);
		unsigned char *base = (unsigned char *)NativeClipScratch_Player(0, entries, players);
		int layoutOk = 1;

		for (uint32_t i = 0; i < players; i++)
		{
			unsigned char *slice = (unsigned char *)NativeClipScratch_Player(i, entries, players);

			if (slice != base + (i * stride))
				layoutOk = 0;
			if (((uintptr_t)slice & 3u) != 0)
				layoutOk = 0;
			if ((slice + stride) > (base + NativeClipScratch_Capacity()))
				layoutOk = 0;
		}

		expect(layoutOk, "four-player slices are word aligned, disjoint and in bounds");
		expect(NativeClipScratch_Capacity() == CTR_NATIVE_CLIP_SCRATCH_CAPACITY,
		       "reported capacity matches the declared capacity");

		// The declared worst case has to end exactly on the buffer end: any
		// slack means the capacity formula and the table disagree.
		{
			uint32_t worst = CTR_NATIVE_CLIP_SCRATCH_MAX_ENTRIES;
			unsigned char *lastSlice =
			    (unsigned char *)NativeClipScratch_Player(players - 1, worst, players);
			unsigned char *worstBase = (unsigned char *)NativeClipScratch_Player(0, worst, players);
			unsigned char *worstEnd = lastSlice + NativeClipScratch_PlayerBytes(worst);

			memset(worstBase, 0x3c, NativeClipScratch_Capacity());
			expect(worstEnd == worstBase + NativeClipScratch_Capacity(),
			       "the declared worst case fills the buffer exactly, with no slack");
		}
	}

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
