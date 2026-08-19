// Exercise the production birth transaction through its real entry point.
// Build as 32-bit because the native game intentionally preserves PS1 pointer
// layout and PROC_BirthWithObject uses 32-bit object-pointer arithmetic.
#include <stdio.h>
#include <string.h>

#include <common.h>

struct sData sdata_static;

// PE/COFF does not discard every unrelated included function as aggressively
// as ELF. These inert definitions satisfy references outside the transaction
// under test; none is reached by either test case.
void *MEMPACK_AllocMem(int size)
{
	(void)size;
	return NULL;
}

void COLL_SearchBSP_CallbackPARAM(struct BSP *root, struct BoundingBox *bbox,
	                              CollBspLeafCallback callback,
	                              struct ScratchpadStruct *sps)
{
	(void)root;
	(void)bbox;
	(void)callback;
	(void)sps;
}

struct MetaDataMODEL *COLL_LevModelMeta(u32 id)
{
	(void)id;
	return NULL;
}

void ConvertRotToMatrix(MATRIX *m, const SVec3 *rot)
{
	(void)m;
	(void)rot;
}

// Pull in the actual list, pool, thread-birth and instance-birth implementations.
// Function sections plus linker garbage collection discard unrelated game entry
// points while retaining the complete transaction under test.
#include "../game/LIST.c"
#include "../game/JitPool.c"
#include "../game/PROC.c"
#include "../game/INSTANCE.c"

struct StackSlot
{
	struct Item item;
	unsigned char object[0x40];
};

static struct GameTracker tracker;
static struct Model model;
static int failures;
static int oldThreadTicks;

static void count_tick(struct Thread *thread)
{
	(void)thread;
	oldThreadTicks++;
}

static void expect(int condition, const char *name)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

static void init_case(struct Thread *threadSlot, struct StackSlot *stackSlot)
{
	memset(&tracker, 0, sizeof tracker);
	memset(threadSlot, 0, sizeof *threadSlot);
	memset(stackSlot, 0, sizeof *stackSlot);
	oldThreadTicks = 0;
	sdata_static.gGT = &tracker;
	tracker.modelPtr[0] = &model;

	LIST_Clear(&tracker.JitPools.thread.free);
	LIST_AddFront(&tracker.JitPools.thread.free, (struct Item *)threadSlot);
	LIST_Clear(&tracker.JitPools.smallStack.free);
	LIST_AddFront(&tracker.JitPools.smallStack.free, &stackSlot->item);
	tracker.JitPools.smallStack.itemSize = sizeof *stackSlot;

	// This is the deterministic fault: the real INSTANCE_Birth3D reaches the
	// real JitPool_Add with no free instance and returns NULL.
	LIST_Clear(&tracker.JitPools.instance.free);
	LIST_Clear(&tracker.JitPools.instance.taken);
	tracker.JitPools.instance.maxItems = 1;
}

static void test_bucket_root(void)
{
	struct Thread threadSlot;
	struct StackSlot stackSlot;
	struct Thread oldRoot;
	struct Instance *result;

	memset(&oldRoot, 0, sizeof oldRoot);
	oldRoot.funcThTick = count_tick;
	init_case(&threadSlot, &stackSlot);
	tracker.threadBuckets[OTHER].thread = &oldRoot;

	result = INSTANCE_BirthWithThread(0, "root-fault", SMALL, OTHER,
	                                  NULL, 0, NULL);

	expect(result == NULL, "real entry reports instance-pool failure");
	expect(tracker.threadBuckets[OTHER].thread == &oldRoot,
	       "bucket root restores the pre-birth thread");
	expect(tracker.JitPools.thread.free.first == (struct Item *)&threadSlot &&
	           tracker.JitPools.thread.free.count == 1,
	       "born root thread returns to the real thread free list");
	expect(tracker.JitPools.smallStack.free.first == &stackSlot.item &&
	           tracker.JitPools.smallStack.free.count == 1,
	       "born root object returns to the real stack free list");
	ThTick_RunBucket(tracker.threadBuckets[OTHER].thread);
	expect(oldThreadTicks == 1,
	       "next real bucket tick reaches the old root exactly once");
}

static void test_parent_child(void)
{
	struct Thread threadSlot;
	struct StackSlot stackSlot;
	struct Thread parent;
	struct Thread oldChild;
	struct Instance *result;

	memset(&parent, 0, sizeof parent);
	memset(&oldChild, 0, sizeof oldChild);
	oldChild.funcThTick = count_tick;
	parent.flags = OTHER;
	parent.childThread = &oldChild;
	init_case(&threadSlot, &stackSlot);

	result = INSTANCE_BirthWithThread(0, "child-fault", SMALL, OTHER,
	                                  NULL, 0, &parent);

	expect(result == NULL, "real child entry reports instance-pool failure");
	expect(parent.childThread == &oldChild,
	       "parent restores the pre-birth child thread");
	expect(tracker.JitPools.thread.free.first == (struct Item *)&threadSlot &&
	           tracker.JitPools.thread.free.count == 1,
	       "born child thread returns to the real thread free list");
	expect(tracker.JitPools.smallStack.free.first == &stackSlot.item &&
	           tracker.JitPools.smallStack.free.count == 1,
	       "born child object returns to the real stack free list");
	ThTick_RunBucket(&parent);
	expect(oldThreadTicks == 1,
	       "next real family tick reaches the old child exactly once");
}

int main(void)
{
	test_bucket_root();
	test_parent_child();
	printf("%s production INSTANCE_BirthWithThread rollback (%d failures)\n",
	       failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
