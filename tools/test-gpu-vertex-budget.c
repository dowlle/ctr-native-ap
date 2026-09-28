// Out-of-engine assertions for the native GPU vertex-buffer budget
// (include/platform/native_gpu_vertex_budget_logic.h): a primitive that does
// not fit the uploaded vertex buffer is dropped whole and counted, instead of
// being written past the buffer.
//
//   cc -Wall -Wextra -I include -o /tmp/test-gpu-vertex-budget tools/test-gpu-vertex-budget.c && /tmp/test-gpu-vertex-budget
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * a primitive that fits, including one that ends exactly at capacity
//   * a primitive that overflows by one vertex is dropped whole and counted
//   * after a drop, a smaller primitive that still fits is kept
//   * a flush inside the primitive (index back below the start) is kept
//   * a replay of a full buffer of LINE_F4-sized primitives never keeps an
//     index past capacity and never writes past capacity plus the slack

#include <stdio.h>

#include "platform/native_gpu_vertex_budget_logic.h"

static int g_checks;
static int g_failures;

static void expect(int condition, const char *name)
{
	g_checks++;
	if (!condition)
	{
		g_failures++;
		printf("FAIL: %s\n", name);
	}
}

int main(void)
{
	const int capacity = 64;

	{
		NativeGpuVertexOverflow o = {0, 0};
		expect(NativeGpuVertexBudget_Commit(&o, 0, 6, capacity) == 6, "fits: keeps end");
		expect(NativeGpuVertexBudget_Commit(&o, 58, 64, capacity) == 64, "ends at capacity: keeps end");
		expect(o.skippedPrimitives == 0 && o.skippedVertices == 0, "fits: nothing counted");
	}

	{
		NativeGpuVertexOverflow o = {0, 0};
		expect(NativeGpuVertexBudget_Commit(&o, 59, 65, capacity) == 59, "one over: rolled back to start");
		expect(o.skippedPrimitives == 1 && o.skippedVertices == 6, "one over: counted whole");
		expect(NativeGpuVertexBudget_Commit(&o, 59, 62, capacity) == 62, "smaller primitive after a drop: kept");
		expect(o.skippedPrimitives == 1, "smaller primitive after a drop: not counted");
	}

	{
		NativeGpuVertexOverflow o = {0, 0};
		expect(NativeGpuVertexBudget_Commit(&o, 60, 0, capacity) == 0, "flush inside the primitive: kept");
		expect(o.skippedPrimitives == 0, "flush inside the primitive: not counted");
	}

	{
		// Replay: LINE_F4 (the largest primitive) until well past capacity.
		NativeGpuVertexOverflow o = {0, 0};
		int index = 0;
		int maxWritten = 0;
		for (int i = 0; i < 20; i++)
		{
			const int end = index + NATIVE_GPU_MAX_PRIMITIVE_VERTICES;
			if (end > maxWritten)
			{
				maxWritten = end;
			}
			index = NativeGpuVertexBudget_Commit(&o, index, end, capacity);
			if (index > capacity)
			{
				break;
			}
		}
		expect(index <= capacity, "replay: kept index never past capacity");
		expect(maxWritten <= capacity + NATIVE_GPU_MAX_PRIMITIVE_VERTICES, "replay: writes stay inside the slack");
		expect(index == (capacity / NATIVE_GPU_MAX_PRIMITIVE_VERTICES) * NATIVE_GPU_MAX_PRIMITIVE_VERTICES, "replay: whole primitives kept");
		expect(o.skippedPrimitives == 20 - capacity / NATIVE_GPU_MAX_PRIMITIVE_VERTICES, "replay: every later primitive counted");
	}

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
