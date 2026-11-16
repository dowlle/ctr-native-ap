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

// A position in, or a count of, the frame's vertex batch. A draw split stores
// its first vertex and its vertex count in this type, and both range over the
// whole uploaded buffer (MAX_VERTEX_BUFFER_SIZE, 1 << 18 since the Papu Cave
// fix) plus the slack above. They were u16: past 65,535 vertices in one batch
// every later split started at a wrapped index, so it drew earlier vertices in
// shifted groups of three. That is the "exploding polygons" seen on dense
// custom tracks with Increase Draw Distance (North Pole draws about 105,000
// vertices a frame there). native_gpu.c asserts its split fields against this.
typedef int NativeGpuVertexIndex;

// Can a split field of `fieldBytes` bytes (signed) hold every index of a batch
// of `capacity` vertices plus the slack? Usable in a static assert.
#define NATIVE_GPU_SPLIT_FIELD_HOLDS(fieldBytes, capacity) \
	((unsigned long long)(capacity) + NATIVE_GPU_MAX_PRIMITIVE_VERTICES <= ((1ull << ((fieldBytes) * 8u - 1u)) - 1u))

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
