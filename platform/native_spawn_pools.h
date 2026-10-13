#ifndef CTR_NATIVE_SPAWN_POOLS_H
#define CTR_NATIVE_SPAWN_POOLS_H

#include <stdint.h>

// Issue #205. Every INSTANCE_BirthWithThread needs one item from three pools:
// a stack object (smallStack or mediumStack, PROC_BirthWithObject), a thread,
// and an instance. Retail sizes all of them per level from MEMPACK, and on a
// weapon-heavy race any one of them can run dry, so the birth returns NULL and
// the effect is skipped. Project ruling (2026-10-13): a missing effect is not
// an acceptable normal outcome, so native gives these four pools enough room
// that they do not fill in practice, and the NULL guards stay as a last-resort
// safety net only.
//
// The room comes from a reserve inside the MEMPACK backing store that the
// allocator never hands out (platform/native_memory.c). In the retail-pressure
// arena that reserve is the 0xba9f0 bytes below the arena start, the part of
// PS1 RAM that held the kernel and executable and that native never used. The
// pools therefore leave the 0x144e10 arena entirely, which also gives every
// level load back the bytes these pools used to take from it, and they stay
// inside the region checkpoints capture and relocate (MPAK), so quick saves
// and replay checkpoints keep working without a format change.
//
// The retail render bucket is sized for the retail instance pool: one 8-byte
// RenderBucketEntry per instance per player (renderBucketSize >> 5 instances,
// four players), plus the terminator. A larger pool needs a larger bucket, so
// the bucket moves into the reserve with the pools.
//
// Nothing indexes these pools by a fixed count: they are walked as linked
// free/taken lists, ThTick_RunBucket's 128-entry pending stack is bounded by
// thread tree depth rather than thread count, and the primitive and ordering
// table writers check their own end pointers.
//
// NativeSpawnPools_Plan is a pure function so tools/test-native-spawn-pools.c
// can check the worst cases against the real reserve size.

// Headroom on top of the retail per-level budget: 3 means four times the retail
// thread, stack and instance counts. The planner steps down toward 0 if a load
// (a large custom track) would not fit, and reports failure if even the retail
// counts do not, so the caller can fall back to the retail MEMPACK path.
#define CTR_NATIVE_SPAWN_POOL_HEADROOM 3

#define CTR_NATIVE_SPAWN_THREAD_ITEM_SIZE 0x48u
#define CTR_NATIVE_SPAWN_SMALL_ITEM_SIZE 0x48u
#define CTR_NATIVE_SPAWN_MEDIUM_ITEM_SIZE 0x88u
#define CTR_NATIVE_SPAWN_BUCKET_ENTRY_SIZE 8u

struct NativeSpawnPoolRequest
{
	uint32_t reserveBytes;
	int retailThreads;          // (renderBucketSize * 3) >> 7
	int retailInstances;        // instance capacity incl. AP and custom-track additions
	int retailInstanceBudget;   // renderBucketSize >> 5, the per-mode runtime budget
	int retailSmall;            // (poolScale * 0x19) >> 10
	int retailMedium;           // poolScale >> 7
	uint32_t instanceItemSize;  // sizeof(Instance) + sizeof(InstDrawPerPlayer) * players
	int numPlayers;
	uint32_t retailBucketBytes; // renderBucketSize
};

struct NativeSpawnPoolPlan
{
	int headroom;
	int threadItems;
	int instanceItems;
	int smallItems;
	int mediumItems;
	uint32_t threadOffset;
	uint32_t instanceOffset;
	uint32_t smallOffset;
	uint32_t mediumOffset;
	uint32_t bucketOffset;
	uint32_t bucketBytes;
	uint32_t totalBytes;
};

static inline uint32_t NativeSpawnPools_Align4(uint32_t value)
{
	return (value + 3u) & ~3u;
}

// Bytes a render bucket needs for `instances` drawn by every player, plus the
// terminator word, never less than the retail bucket.
static inline uint32_t NativeSpawnPools_BucketBytes(int instances, int numPlayers, uint32_t retailBytes)
{
	uint32_t players = (numPlayers > 0) ? (uint32_t)numPlayers : 1u;
	uint32_t bytes = ((uint32_t)instances * players + 1u) * CTR_NATIVE_SPAWN_BUCKET_ENTRY_SIZE;
	return (bytes > retailBytes) ? bytes : retailBytes;
}

// Returns 1 and fills `plan` with the largest headroom that fits the reserve,
// or 0 when not even the retail counts fit (caller keeps the retail path).
static inline int NativeSpawnPools_Plan(const struct NativeSpawnPoolRequest *req, struct NativeSpawnPoolPlan *plan)
{
	if ((req == 0) || (plan == 0) || (req->instanceItemSize == 0u) || ((req->instanceItemSize & 3u) != 0u))
		return 0;

	for (int headroom = CTR_NATIVE_SPAWN_POOL_HEADROOM; headroom >= 0; headroom--)
	{
		uint64_t offset = 0;
		struct NativeSpawnPoolPlan p;

		p.headroom = headroom;
		p.threadItems = req->retailThreads * (1 + headroom);
		p.instanceItems = req->retailInstances + req->retailInstanceBudget * headroom;
		p.smallItems = req->retailSmall * (1 + headroom);
		p.mediumItems = req->retailMedium * (1 + headroom);

		p.threadOffset = (uint32_t)offset;
		offset += NativeSpawnPools_Align4((uint32_t)p.threadItems * CTR_NATIVE_SPAWN_THREAD_ITEM_SIZE);
		p.instanceOffset = (uint32_t)offset;
		offset += (uint64_t)(uint32_t)p.instanceItems * req->instanceItemSize;
		p.smallOffset = (uint32_t)offset;
		offset += (uint64_t)(uint32_t)p.smallItems * CTR_NATIVE_SPAWN_SMALL_ITEM_SIZE;
		p.mediumOffset = (uint32_t)offset;
		offset += (uint64_t)(uint32_t)p.mediumItems * CTR_NATIVE_SPAWN_MEDIUM_ITEM_SIZE;
		p.bucketOffset = (uint32_t)offset;
		p.bucketBytes = NativeSpawnPools_BucketBytes(p.instanceItems, req->numPlayers, req->retailBucketBytes);
		offset += p.bucketBytes;

		if (offset <= (uint64_t)req->reserveBytes)
		{
			p.totalBytes = (uint32_t)offset;
			*plan = p;
			return 1;
		}
	}

	return 0;
}

#endif
