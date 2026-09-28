// Out-of-engine assertions for the native GPU draw-split table budget
// (include/platform/native_gpu_split_budget_logic.h): when the split table is
// full the pending splits are drawn and the table starts over, so every
// primitive is still drawn with its own state, and the report is rate-limited.
//
//   cc -Wall -Wextra -I include -o /tmp/test-gpu-split-budget tools/test-gpu-split-budget.c && /tmp/test-gpu-split-budget
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * the full test at the table edge (split 0 is the sentinel)
//   * a replay of the AddSplit flow with a small table: a batch far larger
//     than the table is drawn in parts, in order, with every primitive under
//     its own state and no primitive lost
//   * a batch that fits is drawn in one part and never reported
//   * the report: first batch at once, then one line per interval with the
//     batch count and the peaks in between

#include <stdio.h>

#include "platform/native_gpu_split_budget_logic.h"

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

// Minimal model of native_gpu.c: one split per change of state, a table of
// TABLE entries with entry 0 as the sentinel, a flush that "draws" the pending
// splits into drawnState[] (one entry per primitive) and resets the table.
#define TABLE 8
#define MAX_PRIMS 256

typedef struct
{
	int state[TABLE];
	int prims[TABLE];
	int splitIndex;
	int drawnState[MAX_PRIMS];
	int drawnCount;
	int parts;
	NativeGpuSplitOverflow overflow;
	int reports;
} Model;

static void ModelClear(Model *m)
{
	m->splitIndex = 0;
	m->state[0] = -1;
	m->prims[0] = 0;
}

static void ModelFlush(Model *m, int early)
{
	NativeGpuSplitReport report;
	if (!early && NativeGpuSplitBudget_EndBatch(&m->overflow, m->splitIndex, &report))
	{
		m->reports++;
	}
	for (int i = 1; i <= m->splitIndex; i++)
	{
		for (int p = 0; p < m->prims[i]; p++)
		{
			m->drawnState[m->drawnCount++] = m->state[i];
		}
	}
	if (m->splitIndex > 0)
	{
		m->parts++;
	}
	ModelClear(m);
}

static void ModelAddPrim(Model *m, int state)
{
	if (m->state[m->splitIndex] != state)
	{
		if (NativeGpuSplitBudget_IsFull(m->splitIndex, TABLE))
		{
			NativeGpuSplitBudget_NoteEarlyFlush(&m->overflow, m->splitIndex);
			ModelFlush(m, 1);
		}
		m->splitIndex++;
		m->state[m->splitIndex] = state;
		m->prims[m->splitIndex] = 0;
	}
	m->prims[m->splitIndex]++;
}

int main(void)
{
	expect(!NativeGpuSplitBudget_IsFull(0, TABLE), "empty table: not full");
	expect(!NativeGpuSplitBudget_IsFull(TABLE - 2, TABLE), "one slot left: not full");
	expect(NativeGpuSplitBudget_IsFull(TABLE - 1, TABLE), "last slot used: full");

	{
		// A batch that alternates state every primitive needs one split per
		// primitive: 50 splits through a table that holds 7.
		Model m = {0};
		ModelClear(&m);
		int want[MAX_PRIMS];
		int n = 0;
		for (int i = 0; i < 50; i++)
		{
			want[n] = i % 3;
			ModelAddPrim(&m, want[n]);
			n++;
			if (i % 5 == 0)
			{
				// runs of the same state stay in one split
				want[n] = i % 3;
				ModelAddPrim(&m, want[n]);
				n++;
			}
		}
		ModelFlush(&m, 0);

		int same = (m.drawnCount == n);
		for (int i = 0; same && i < n; i++)
		{
			same = (m.drawnState[i] == want[i]);
		}
		expect(m.drawnCount == n, "large batch: no primitive lost");
		expect(same, "large batch: drawn in order, each with its own state");
		expect(m.parts == (50 + TABLE - 2) / (TABLE - 1), "large batch: drawn in ceil(50 / 7) parts");
		expect(m.reports == 1, "large batch: reported once");
	}

	{
		Model m = {0};
		ModelClear(&m);
		for (int i = 0; i < TABLE - 1; i++)
		{
			ModelAddPrim(&m, i);
		}
		ModelFlush(&m, 0);
		expect(m.parts == 1 && m.drawnCount == TABLE - 1, "batch that fills the table exactly: one part");
		expect(m.reports == 0 && m.overflow.batchesSince == 0, "batch that fits: not reported");
	}

	{
		NativeGpuSplitOverflow o = {0};
		NativeGpuSplitReport r = {0};

		NativeGpuSplitBudget_NoteEarlyFlush(&o, 4095);
		expect(NativeGpuSplitBudget_EndBatch(&o, 100, &r), "first overflowing batch: reported");
		expect(r.batches == 1 && r.peakSplits == 4195 && r.peakParts == 2, "first report: counts");

		int lines = 0;
		for (int i = 0; i < 3 * NATIVE_GPU_SPLIT_REPORT_INTERVAL; i++)
		{
			expect(!NativeGpuSplitBudget_EndBatch(&o, 10, &r), "batch without early flush: never reported");
			NativeGpuSplitBudget_NoteEarlyFlush(&o, 4095);
			if (i == 7)
			{
				NativeGpuSplitBudget_NoteEarlyFlush(&o, 4095);
			}
			if (NativeGpuSplitBudget_EndBatch(&o, 5, &r))
			{
				lines++;
				expect(r.batches == NATIVE_GPU_SPLIT_REPORT_INTERVAL, "later report: covers one interval of batches");
				if (lines == 1)
				{
					expect(r.peakSplits == 2 * 4095 + 5 && r.peakParts == 3, "later report: peaks since the last line");
				}
				else
				{
					expect(r.peakSplits == 4100 && r.peakParts == 2, "later report: peaks reset after a line");
				}
			}
		}
		expect(lines == 3, "later reports: one per interval");
	}

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
