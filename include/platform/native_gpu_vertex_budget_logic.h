#ifndef NATIVE_GPU_VERTEX_BUDGET_LOGIC_H
#define NATIVE_GPU_VERTEX_BUDGET_LOGIC_H

// Freestanding vertex-buffer budget for the native GPU primitive parser
// (platform/native_gpu.c). No engine or SDL dependencies, so the harness in
// tools/test-gpu-vertex-budget.c can exercise it directly.
//
// ParsePrimitive lets one primitive write its vertices, then commits them only
// if the whole primitive fits in the uploaded buffer. A primitive that does not
// fit is dropped whole: the write index goes back to where the primitive
// started, so it is never drawn partially and never runs past the buffer. The
// buffer array carries NATIVE_GPU_MAX_PRIMITIVE_VERTICES of slack past the
// uploaded capacity to absorb the dropped primitive's writes.

// Most vertices one parsed primitive writes: LINE_F4 draws three line
// segments, each triangulated into two triangles (3 x 6).
#define NATIVE_GPU_MAX_PRIMITIVE_VERTICES 18

typedef struct
{
	int skippedPrimitives;
	int skippedVertices;
} NativeGpuVertexOverflow;

// Returns the write index to keep after a primitive wrote vertices
// [startIndex, endIndex). An end index below the start (the primitive flushed
// the buffer, e.g. DR_MOVE or a fill) always fits.
static inline int NativeGpuVertexBudget_Commit(NativeGpuVertexOverflow *overflow, int startIndex, int endIndex, int capacity)
{
	if (endIndex <= capacity)
	{
		return endIndex;
	}

	overflow->skippedPrimitives++;
	overflow->skippedVertices += endIndex - startIndex;
	return startIndex;
}

#endif
