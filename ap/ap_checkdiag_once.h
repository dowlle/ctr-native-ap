// Once-per-connect gate for the [AP CHECK DIAG] podium diagnostic lines.
//
// Why this exists: AP_ReconcilePodiumFromTrophies runs every AP tick by design
// (its sends dedup against the checked set, so re-running is a behavioral
// no-op). The Alpha 4 progression-integrity diagnostics in AP_EmitRung log
// BEFORE that dedup, so on a seed whose slot_data omits a rung (schema <= 6
// carries held_5th = -1 on all 16 tracks) every tick logged one refusal per
// trophy-won track, forever: ~15k lines per session against the August async.
// The refusal is correct; repeating it is pure log spam that drowns evidence
// and grows ctr-ap.log without bound on the game thread.
//
// The gate: each (branch, phase, track, rung) diagnostic fires exactly once
// between resets. Reset at the fresh-connect session reset, the same place the
// other per-session state re-arms, so every new connect (a new seed, or a
// reconnect after a config change) gets one fresh set of lines.
//
// Pure and freestanding so tools/test-checkdiag-once.c can drive it host-side.

#ifndef AP_CHECKDIAG_ONCE_H
#define AP_CHECKDIAG_ONCE_H

#define AP_CHECKDIAG_TRACKS 48 // 16 retail trophy tracks + 32 frozen custom slots
#define AP_CHECKDIAG_RUNGS  5  // AP_RUNG_HELD_1ST .. AP_RUNG_FINISH_ANY

// Diagnostic branches inside AP_EmitRung.
#define AP_CHECKDIAG_ABSENT   0 // code < 0: rung absent from seed config
#define AP_CHECKDIAG_MISMATCH 1 // configured code the server does not know

typedef struct
{
	// [branch][phase][track] -> bitmask of rung tags already logged.
	unsigned char seen[2][2][AP_CHECKDIAG_TRACKS];
} ap_checkdiag_once_state;

static void AP_CheckDiagOnceReset(ap_checkdiag_once_state *s)
{
	int b, p, t;
	for (b = 0; b < 2; b++)
		for (p = 0; p < 2; p++)
			for (t = 0; t < AP_CHECKDIAG_TRACKS; t++)
				s->seen[b][p][t] = 0;
}

// Returns 1 exactly once per (branch, phase, track, rung) between resets, 0 on
// every repeat. Any out-of-range input returns 1 unconditionally: an unexpected
// shape must always reach the log, never be silently suppressed.
static int AP_CheckDiagOnce(ap_checkdiag_once_state *s, int branch, int phase,
                            int track, int rung)
{
	unsigned char bit;
	if (branch < 0 || branch > 1 || phase < 0 || phase > 1 ||
	    track < 0 || track >= AP_CHECKDIAG_TRACKS ||
	    rung < 0 || rung >= AP_CHECKDIAG_RUNGS)
		return 1;
	bit = (unsigned char)(1u << rung);
	if (s->seen[branch][phase][track] & bit)
		return 0;
	s->seen[branch][phase][track] |= bit;
	return 1;
}

#endif // AP_CHECKDIAG_ONCE_H
