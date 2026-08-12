#ifndef AP_EDITSTAT_BOUNDS_H
#define AP_EDITSTAT_BOUNDS_H

// ---------------------------------------------------------------------------
// Bounded shape of the stored editable-stat package (issues #54/#209).
//
// The package that rides per-slot AP data storage under "ctr_editstats_<slot>"
// is a flat array of AP_NET_EDITSTAT_COUNT signed DELTAS over the vanilla stat
// table. Data storage is writable by anything holding the slot, so what comes
// back is untrusted input, and the game side narrows each element to `short`
// before it ever reaches a driver. Length and JSON integer-ness alone do not
// make that narrowing safe: a stored 4294967328 is a perfectly well-formed JSON
// integer that becomes 32 on the way in, and a stored 70000 becomes 4464.
//
// WHY THIS RANGE, derived rather than picked. Every delta is produced by
// ap_cs_adjust as `clamped_target - vanilla_base`, where the target is run
// through ap_cs_clamp into [0, cap] and the base is a non-negative vanilla
// value. The widest cap in the stat table is ACCEL's 32767, so no delta the
// editor can ever generate lies outside [-32767, +32767]. The bound is the
// range's own arithmetic, not a guess, and it is deliberately symmetric: it
// excludes -32768, whose negation is not representable in the type the values
// are narrowed to.
//
// Anything outside the range means the package was not written by this client.
// It is rejected WHOLESALE by the caller rather than clamped: a kart tuned from
// half of someone else's array, or from a silently squashed one, is worse than
// a kart that ignored the package and kept vanilla numbers.
//
// Shared by ap_net.cpp (the validator) and tools/test-character-persistence.cpp
// (the harness that proves it), so the constant the test asserts and the one
// production enforces cannot drift apart.
// ---------------------------------------------------------------------------

#define AP_NET_EDITSTAT_COUNT 68 // 4 global + 16 racers x 4

#define AP_NET_EDITSTAT_MIN (-32767)
#define AP_NET_EDITSTAT_MAX (32767)

// 1 if `v` is a delta this client could have written. Takes a long long so the
// caller can range-check BEFORE any narrowing, which is the whole point.
static inline int ap_editstat_value_ok(long long v)
{
	return v >= (long long)AP_NET_EDITSTAT_MIN && v <= (long long)AP_NET_EDITSTAT_MAX;
}

#endif // AP_EDITSTAT_BOUNDS_H
