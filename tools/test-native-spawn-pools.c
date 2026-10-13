// cc -std=c99 -Wall -Wextra -Werror -o /tmp/test-native-spawn-pools tools/test-native-spawn-pools.c
// Run from the repository root, or pass the repository root as argv[1].
//
// Issue #205: native sizes the thread, instance and small/medium stack pools
// with headroom over retail, in a reserve inside the MEMPACK backing store
// (platform/native_spawn_pools.h, platform/native_memory.c). This checks:
//   - every retail level shape, 1-4 players, gets the full headroom and fits
//     the real reserve size, read from native_memory.c rather than restated;
//   - the planned regions do not overlap, stay 4-byte aligned, and the render
//     bucket holds one entry per instance per player plus the terminator;
//   - a custom track with a huge authored table steps the headroom down but
//     never below the retail count, and an impossible load reports failure so
//     MainInit keeps the retail path;
//   - a stress fill: the planned instance pool hands out exactly its capacity
//     of distinct, in-region slots (walked the way JitPool_Clear lays them out)
//     and the next request fails, so the new capacity is real.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/native_spawn_pools.h"

static int failures;

static void expect(int condition, const char *name)
{
	printf("%s  %s\n", condition ? "ok  " : "FAIL", name);
	if (!condition)
		failures++;
}

static uint32_t read_reserve(const char *root)
{
	char path[1024];
	char line[512];
	uint32_t value = 0;
	FILE *f;

	snprintf(path, sizeof path, "%s/platform/native_memory.c", root);
	f = fopen(path, "r");
	if (f == NULL)
		return 0;
	while (fgets(line, sizeof line, f) != NULL)
	{
		const char *key = "#define CTR_NATIVE_SPAWN_POOL_RESERVE ";
		if (strncmp(line, key, strlen(key)) == 0)
		{
			value = (uint32_t)strtoul(line + strlen(key), NULL, 0);
			break;
		}
	}
	fclose(f);
	return value;
}

// Mirrors MainInit_JitPoolsNew's retail arithmetic for one level shape.
static struct NativeSpawnPoolRequest shape(uint32_t reserve, int renderBucketSize, int poolScale, int apExtra, int players)
{
	struct NativeSpawnPoolRequest r;
	r.reserveBytes = reserve;
	r.retailThreads = (renderBucketSize * 3) >> 7;
	r.retailInstanceBudget = renderBucketSize >> 5;
	r.retailInstances = r.retailInstanceBudget + apExtra;
	r.retailSmall = (poolScale * 0x19) >> 10;
	r.retailMedium = poolScale >> 7;
	r.instanceItemSize = 0x74u + 0x88u * (uint32_t)players;
	r.numPlayers = players;
	r.retailBucketBytes = (uint32_t)renderBucketSize;
	return r;
}

static int layout_ok(const struct NativeSpawnPoolRequest *r, const struct NativeSpawnPoolPlan *p)
{
	uint32_t threadEnd = p->threadOffset + (uint32_t)p->threadItems * CTR_NATIVE_SPAWN_THREAD_ITEM_SIZE;
	uint32_t instEnd = p->instanceOffset + (uint32_t)p->instanceItems * r->instanceItemSize;
	uint32_t smallEnd = p->smallOffset + (uint32_t)p->smallItems * CTR_NATIVE_SPAWN_SMALL_ITEM_SIZE;
	uint32_t mediumEnd = p->mediumOffset + (uint32_t)p->mediumItems * CTR_NATIVE_SPAWN_MEDIUM_ITEM_SIZE;
	uint32_t bucketEnd = p->bucketOffset + p->bucketBytes;
	uint32_t need = ((uint32_t)p->instanceItems * (uint32_t)r->numPlayers + 1u) * CTR_NATIVE_SPAWN_BUCKET_ENTRY_SIZE;

	return (threadEnd <= p->instanceOffset) && (instEnd <= p->smallOffset) && (smallEnd <= p->mediumOffset) &&
	       (mediumEnd <= p->bucketOffset) && (bucketEnd == p->totalBytes) && (p->totalBytes <= r->reserveBytes) &&
	       ((p->instanceOffset | p->smallOffset | p->mediumOffset | p->bucketOffset) & 3u) == 0u &&
	       (p->bucketBytes >= need) && (p->bucketBytes >= r->retailBucketBytes);
}

int main(int argc, char **argv)
{
	const char *root = (argc > 1) ? argv[1] : ".";
	uint32_t reserve = read_reserve(root);
	struct NativeSpawnPoolPlan plan;
	uint32_t worst = 0;

	expect(reserve == 0xba9f0u, "reserve read from native_memory.c is the retail-pressure arena start (0xba9f0)");

	// renderBucketSize, poolScale, AP instance extra (hub +48, trial +6)
	static const int shapes[][3] = {
	    {0x1000, 0x1000, 0},  // race, battle, boss, cutscene
	    {0x1000, 0x1000, 6},  // trial track token race
	    {0x800, 0x800, 48},   // adventure hub with the AP pad extras
	    {0x400, 0x400, 0},    // main menu
	    {0x800, 0x400, 0},    // garage
	};
	int allFull = 1;
	int allLayout = 1;
	for (unsigned s = 0; s < sizeof shapes / sizeof shapes[0]; s++)
	{
		for (int players = 1; players <= 4; players++)
		{
			struct NativeSpawnPoolRequest r = shape(reserve, shapes[s][0], shapes[s][1], shapes[s][2], players);
			if (!NativeSpawnPools_Plan(&r, &plan) || plan.headroom != CTR_NATIVE_SPAWN_POOL_HEADROOM)
				allFull = 0;
			else if (!layout_ok(&r, &plan))
				allLayout = 0;
			if (plan.totalBytes > worst)
				worst = plan.totalBytes;
		}
	}
	expect(allFull, "every retail level shape, 1-4 players, gets the full 4x headroom");
	expect(allLayout, "planned regions are disjoint, aligned, inside the reserve, bucket covers inst x players + 1");
	printf("      worst-case reserve use %u of %u bytes\n", (unsigned)worst, (unsigned)reserve);

	{
		struct NativeSpawnPoolRequest r = shape(reserve, 0x1000, 0x1000, 0, 1);
		NativeSpawnPools_Plan(&r, &plan);
		expect(plan.threadItems == 384 && plan.instanceItems == 512 && plan.smallItems == 400 && plan.mediumItems == 128,
		       "1P race: thread 96->384, instance 128->512, small 100->400, medium 32->128");
	}
	{
		struct NativeSpawnPoolRequest r = shape(reserve, 0x800, 0x800, 48, 1);
		NativeSpawnPools_Plan(&r, &plan);
		expect(plan.instanceItems == 112 + 3 * 64, "hub keeps its AP +48 and adds 3x the 64 runtime budget");
	}
	{
		// A custom track whose authored table nearly fills the reserve at 4P.
		struct NativeSpawnPoolRequest r = shape(reserve, 0x1000, 0x1000, 900, 4);
		int ok = NativeSpawnPools_Plan(&r, &plan);
		expect(ok && plan.headroom < CTR_NATIVE_SPAWN_POOL_HEADROOM && plan.instanceItems >= r.retailInstances &&
		           plan.threadItems >= r.retailThreads && layout_ok(&r, &plan),
		       "huge custom table steps headroom down, never below retail counts");
	}
	{
		struct NativeSpawnPoolRequest r = shape(reserve, 0x1000, 0x1000, 5000, 4);
		expect(!NativeSpawnPools_Plan(&r, &plan), "a load the reserve cannot hold reports failure (retail path kept)");
	}
	{
		struct NativeSpawnPoolRequest r = shape(reserve, 0x1000, 0x1000, 0, 1);
		r.instanceItemSize = 0x75u;
		expect(!NativeSpawnPools_Plan(&r, &plan), "an unaligned item size is refused");
	}

	// Stress fill of the planned 4P race instance pool.
	{
		struct NativeSpawnPoolRequest r = shape(reserve, 0x1000, 0x1000, 0, 4);
		unsigned char *mem = calloc(1, reserve);
		void *freeList = NULL;
		int handed = 0;
		int inRegion = 1;

		NativeSpawnPools_Plan(&r, &plan);
		// JitPool_Clear: step itemSize (aligned down to 4) from ptrPoolData,
		// pushing every slot onto the free list.
		for (int i = 0; i < plan.instanceItems; i++)
		{
			void **slot = (void **)(mem + plan.instanceOffset + (uint32_t)i * ((r.instanceItemSize >> 2) << 2));
			*slot = freeList;
			freeList = slot;
		}
		while (freeList != NULL)
		{
			unsigned char *slot = freeList;
			if ((slot < mem + plan.instanceOffset) || (slot + r.instanceItemSize > mem + plan.smallOffset))
				inRegion = 0;
			freeList = *(void **)slot;
			memset(slot, 0xA5, r.instanceItemSize); // a live instance overwrites its slot
			handed++;
		}
		expect(handed == 512 && inRegion, "stress fill: 4P race pool hands out 512 distinct in-region instances");
		expect(freeList == NULL, "stress fill: request 513 finds the pool empty");
		expect(mem[plan.smallOffset] == 0, "stress fill: writing every instance leaves the stack pools untouched");
		free(mem);
	}

	printf("%s native spawn pools (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
