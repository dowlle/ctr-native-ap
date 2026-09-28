#ifndef NATIVE_GPU_SPLIT_BUDGET_LOGIC_H
#define NATIVE_GPU_SPLIT_BUDGET_LOGIC_H

// Freestanding draw-split table budget for the native GPU primitive parser
// (platform/native_gpu.c). No engine or SDL dependencies, so the harness in
// tools/test-gpu-split-budget.c can exercise it directly.
//
// The parser records one draw split per run of primitives that share blend
// mode, texture, clip rect and the other per-draw state. Split 0 of the table is
// a sentinel, so a table of `capacity` entries holds capacity - 1 splits. When a
// primitive needs a new split and the table is full, native_gpu.c draws the
// pending splits and empties the table (the same flush a DR_MOVE or fill packet
// does), then records the primitive in the fresh table. Every primitive keeps
// its own state and the draw order is unchanged; the batch is just drawn in
// more than one part.
//
// The early flush is correct output, so it is reported rather than logged per
// primitive: one line for the first batch that needed it, then at most one line
// per NATIVE_GPU_SPLIT_REPORT_INTERVAL such batches, with the counts in between.

#define NATIVE_GPU_SPLIT_REPORT_INTERVAL 600

typedef struct
{
	int earlyFlushes;   // table-full flushes in the batch being parsed
	int earlySplits;    // splits those early flushes drew
	int reported;       // a line has been logged since start-up
	int batchesSince;   // batches that flushed early since the last line
	int peakSplits;     // most splits one such batch needed since the last line
	int peakParts;      // most parts one such batch was drawn in since the last line
} NativeGpuSplitOverflow;

typedef struct
{
	int batches;     // batches that flushed early, including the one reported
	int peakSplits;  // most splits one of them needed
	int peakParts;   // most parts one of them was drawn in
} NativeGpuSplitReport;

// True when the table cannot take another split.
static inline int NativeGpuSplitBudget_IsFull(int splitIndex, int capacity)
{
	return splitIndex + 1 >= capacity;
}

// A full table was flushed mid-batch with `pendingSplits` splits in it.
static inline void NativeGpuSplitBudget_NoteEarlyFlush(NativeGpuSplitOverflow *overflow, int pendingSplits)
{
	overflow->earlyFlushes++;
	overflow->earlySplits += pendingSplits;
}

// A batch ended with a regular flush of `pendingSplits` splits. Returns 1 and
// fills `report` when a line should be logged for the batches seen so far.
static inline int NativeGpuSplitBudget_EndBatch(NativeGpuSplitOverflow *overflow, int pendingSplits, NativeGpuSplitReport *report)
{
	if (overflow->earlyFlushes == 0)
	{
		return 0;
	}

	const int splits = overflow->earlySplits + pendingSplits;
	const int parts = overflow->earlyFlushes + 1;
	overflow->earlyFlushes = 0;
	overflow->earlySplits = 0;

	overflow->batchesSince++;
	if (splits > overflow->peakSplits)
	{
		overflow->peakSplits = splits;
	}
	if (parts > overflow->peakParts)
	{
		overflow->peakParts = parts;
	}

	if (overflow->reported && overflow->batchesSince < NATIVE_GPU_SPLIT_REPORT_INTERVAL)
	{
		return 0;
	}

	report->batches = overflow->batchesSince;
	report->peakSplits = overflow->peakSplits;
	report->peakParts = overflow->peakParts;
	overflow->reported = 1;
	overflow->batchesSince = 0;
	overflow->peakSplits = 0;
	overflow->peakParts = 0;
	return 1;
}

#endif
