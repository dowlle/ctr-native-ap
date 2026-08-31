// Host harness for the [AP CHECK DIAG] once-per-connect gate.
//
//   cc -Wall -Wextra -o /tmp/test-checkdiag-once tools/test-checkdiag-once.c && /tmp/test-checkdiag-once
//
// The acceptance question: each (branch, phase, track, rung) diagnostic fires
// exactly once between connect resets, distinct shapes never mask each other,
// a reset re-arms everything, and out-of-range inputs are never suppressed.
// This is what keeps the per-tick AP_ReconcilePodiumFromTrophies sweep from
// flooding ctr-ap.log on seeds whose slot_data omits a rung (schema <= 6).

#include <stdio.h>

#include "../ap/ap_checkdiag_once.h"

static int failures;

static void expect(const char *name, int got, int want)
{
	if (got != want)
	{
		printf("FAIL %-60s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

int main(void)
{
	ap_checkdiag_once_state s;
	int t, r, total;

	AP_CheckDiagOnceReset(&s);

	// 1. First occurrence logs, second is suppressed.
	expect("first (absent, finish, track 5, rung 2) fires",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 5, 2), 1);
	expect("repeat of the same shape is suppressed",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 5, 2), 0);

	// 2. Every axis is independent: change one coordinate, it fires again.
	expect("different track fires",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 6, 2), 1);
	expect("different rung fires",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 5, 4), 1);
	expect("different phase (held) fires",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 0, 5, 2), 1);
	expect("different branch (mismatch) fires",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_MISMATCH, 1, 5, 2), 1);
	expect("mismatch repeat is suppressed",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_MISMATCH, 1, 5, 2), 0);

	// 3. The live spam shape: nine trophy-won tracks re-swept every tick.
	//    First sweep logs nine lines, the next thousand sweeps log zero.
	{
		static const int won[9] = { 0, 2, 5, 6, 8, 9, 13, 14, 15 };
		int sweep, i;
		AP_CheckDiagOnceReset(&s);
		total = 0;
		for (sweep = 0; sweep < 1000; sweep++)
			for (i = 0; i < 9; i++)
				total += AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1,
				                          won[i], 2);
		expect("1000 reconcile sweeps over 9 tracks log exactly 9 lines",
		       total, 9);
	}

	// 4. Reset re-arms every shape exactly once.
	AP_CheckDiagOnceReset(&s);
	total = 0;
	for (t = 0; t < AP_CHECKDIAG_TRACKS; t++)
		for (r = 0; r < AP_CHECKDIAG_RUNGS; r++)
			total += AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, t, r);
	expect("after reset, full track x rung grid fires once each",
	       total, AP_CHECKDIAG_TRACKS * AP_CHECKDIAG_RUNGS);
	total = 0;
	for (t = 0; t < AP_CHECKDIAG_TRACKS; t++)
		for (r = 0; r < AP_CHECKDIAG_RUNGS; r++)
			total += AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, t, r);
	expect("second pass over the full grid is fully suppressed", total, 0);

	// 5. The first and last frozen custom podium slots are real diagnostic keys.
	AP_CheckDiagOnceReset(&s);
	expect("custom slot 1 / logical track 16 fires once",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 16, 2), 1);
	expect("custom slot 1 repeat is suppressed",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 16, 2), 0);
	expect("custom slot 32 / logical track 47 fires once",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 47, 2), 1);
	expect("custom slot 32 repeat is suppressed",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 47, 2), 0);
	// Seed one unrelated in-range cell so the corruption probe below has a
	// known suppressed value independent of the custom-slot assertions.
	expect("retail track state seeded before out-of-range probes",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 5, 2), 1);

	// 6. Truly out-of-range shapes are never suppressed (and never corrupt state).
	expect("track -1 always fires",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, -1, 2), 1);
	expect("track -1 fires again",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, -1, 2), 1);
	expect("track 48 always fires",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 48, 2), 1);
	expect("rung 5 always fires",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 5, 5), 1);
	expect("branch 2 always fires",
	       AP_CheckDiagOnce(&s, 2, 1, 5, 2), 1);
	expect("phase 2 always fires",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 2, 5, 2), 1);
	expect("in-range state survived the out-of-range probes (still suppressed)",
	       AP_CheckDiagOnce(&s, AP_CHECKDIAG_ABSENT, 1, 5, 2), 0);

	if (failures)
	{
		printf("%d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ALL PASS\n");
	return 0;
}
