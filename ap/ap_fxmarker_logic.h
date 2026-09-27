#ifndef AP_FXMARKER_LOGIC_H
#define AP_FXMARKER_LOGIC_H

// Room-scoped one-shot effect markers (#299). Pure rules, no engine, no network,
// no file access, so the runtime and tools/test-fxmarker.c run the same code.
//
// WHY THE MARKERS LIVE IN THE ROOM. The server resends every received item on
// each connect, so the client has to remember which one-shot effects already
// ran. Up to 0.2.1 that memory was a local file keyed by endpoint, seed and slot
// (ctr-ap-fxseen.txt) plus a seed+slot keyed Turbo Grant file. A fresh room made
// from the same seed on the same host and port matched the old row and silently
// skipped its traps, Wumpa and Turbo Grants. Server DataStorage is part of the
// room's own save, so a fresh room starts with the key absent and a reconnect to
// the same room reads back what it wrote.
//
// Two markers use this one shape:
//   * the effect marker: the highest server item index whose one-shot effect
//     (trap, Wumpa bundle, Wumpa filler) has been handled. Nothing = -1.
//   * the Turbo Grant fired count. Nothing = 0.
// Both only ever move up, so every write is a DataStorage `max` operation and a
// reply from the server can only raise the local copy.
//
// LIFECYCLE PER CONNECT.
//   1. Reset -> PENDING. The client sends Get for the key with the connect.
//   2. While PENDING, one-shot effects are HELD: not applied and not counted as
//      handled. The effect marker tracks the highest index drained meanwhile.
//   3. The Get reply resolves the marker:
//        key present and valid       -> source SERVER, use it;
//        key absent (or not a number) and a legacy local row for this room
//                                    -> source MIGRATED, seed from the row once;
//        otherwise                   -> source FRESH, start from nothing.
//      Held effects above the resolved marker then run, and the marker rises to
//      cover everything drained while pending.
//   4. After that, drained batches raise it and each rise is written with `max`.
//
// NO TIMEOUT. If the reply never arrives the effects stay held for the whole
// connection. That loses nothing: the next connect replays the full item list
// and offers every effect again. The alternatives are worse: treating silence as
// a fresh room double-applies traps on a reconnect, and falling back to the
// local file brings back the bug this fixes. The caller logs once when a reply
// is overdue so a stuck hold is visible.
//
// MIGRATION AMBIGUITY. A room played on 0.2.1 has no key yet, only the local
// row. Seeding from that row keeps an ongoing run from replaying every trap,
// but it is exactly the old identity, so a fresh room of the same seed on the
// same host and port that is FIRST joined with this build also inherits it.
// That can happen at most once per row: the caller deletes the row as soon as
// the server confirms the migrated value, and from then on only the room's own
// key is read.

#define AP_FXM_DECIDE_APPLY 0 // run the effect now
#define AP_FXM_DECIDE_SKIP  1 // already handled in this room
#define AP_FXM_DECIDE_HOLD  2 // marker unknown yet; keep it until resolved

#define AP_FXM_SRC_NONE     0
#define AP_FXM_SRC_SERVER   1
#define AP_FXM_SRC_MIGRATED 2
#define AP_FXM_SRC_FRESH    3

// What the Get reply said about the key.
#define AP_FXM_STORE_PENDING 0 // no reply yet
#define AP_FXM_STORE_ABSENT  1 // key not in this room's storage
#define AP_FXM_STORE_VALID   2 // an integer
#define AP_FXM_STORE_INVALID 3 // something else wrote a non-integer

typedef struct AP_FxMarker
{
	int       known;         // 0 = pending, 1 = resolved
	int       source;        // AP_FXM_SRC_*
	long long empty;         // the "nothing handled" value (-1 or 0)
	long long value;         // the marker, valid once known
	long long held;          // effect marker: highest index drained while pending
	long long server;        // highest value the server is known to hold
	int       server_valid;  // 0 until the server has shown an integer
	int       migrate_row;   // 1 while a migrated local row awaits confirmation
	long long migrated;      // the value taken from that row
} AP_FxMarker;

static inline long long AP_FxMarkerMax(long long a, long long b)
{
	return a > b ? a : b;
}

static inline void AP_FxMarkerReset(AP_FxMarker *m, long long empty)
{
	m->known = 0;
	m->source = AP_FXM_SRC_NONE;
	m->empty = empty;
	m->value = empty;
	m->held = empty;
	m->server = empty;
	m->server_valid = 0;
	m->migrate_row = 0;
	m->migrated = empty;
}

// Effect marker: what to do with one effect item at server index srvIdx.
// An unknown index (-1) always applies, as before: it cannot be deduped and a
// live trap must never be swallowed.
static inline int AP_FxMarkerDecide(const AP_FxMarker *m, long long srvIdx)
{
	if (srvIdx < 0)
		return AP_FXM_DECIDE_APPLY;
	if (!m->known)
		return AP_FXM_DECIDE_HOLD;
	return srvIdx > m->value ? AP_FXM_DECIDE_APPLY : AP_FXM_DECIDE_SKIP;
}

// Resolve from the Get reply and, only when the key is absent or unusable, the
// legacy local row. Returns 1 on the call that resolves it. The caller then
// releases held effects against `value` and calls AP_FxMarkerFinishResolve.
static inline int AP_FxMarkerResolve(AP_FxMarker *m, int storeState, long long storeValue,
                                     int haveLocal, long long localValue)
{
	long long base;

	if (m->known || storeState == AP_FXM_STORE_PENDING)
		return 0;
	if (storeState == AP_FXM_STORE_VALID)
	{
		m->source = AP_FXM_SRC_SERVER;
		m->server = storeValue;
		m->server_valid = 1;
		base = AP_FxMarkerMax(storeValue, m->empty);
	}
	else if (haveLocal)
	{
		m->source = AP_FXM_SRC_MIGRATED;
		base = AP_FxMarkerMax(localValue, m->empty);
		m->migrated = base;
		m->migrate_row = 1;
	}
	else
	{
		m->source = AP_FXM_SRC_FRESH;
		base = m->empty;
	}
	m->known = 1;
	m->value = base;
	return 1;
}

// After the held effects have been released: fold the pending-time high water
// in. Returns 1 when the server needs a write.
static inline int AP_FxMarkerFinishResolve(AP_FxMarker *m)
{
	if (!m->known)
		return 0;
	m->value = AP_FxMarkerMax(m->value, m->held);
	if (m->migrate_row && m->value <= m->empty)
		m->migrate_row = 0; // nothing worth writing; the row can go at once
	return m->server_valid ? m->value > m->server : m->value > m->empty;
}

// A drained batch whose highest server index is batchMax. Returns 1 when the
// marker rose and the server needs a write.
static inline int AP_FxMarkerNoteDrained(AP_FxMarker *m, long long batchMax)
{
	if (!m->known)
	{
		m->held = AP_FxMarkerMax(m->held, batchMax);
		return 0;
	}
	if (batchMax <= m->value)
		return 0;
	m->value = batchMax;
	return 1;
}

// Turbo Grant fired count: one more grant fired. Returns 1 when a write is due.
// Never reached while pending in practice: a grant is only delivered once the
// count is known, so there is nothing to fire before then.
static inline int AP_FxMarkerIncrement(AP_FxMarker *m)
{
	m->value++;
	return m->known;
}

// A value the server reported for the key (SetReply after our own `max` write,
// or another client of the same slot). Never lowers the marker. Returns 1 when a
// migrated local row is now safe to delete, which happens exactly once.
static inline int AP_FxMarkerServerValue(AP_FxMarker *m, long long v)
{
	if (!m->server_valid || v > m->server)
		m->server = v;
	m->server_valid = 1;
	if (m->known)
		m->value = AP_FxMarkerMax(m->value, v);
	if (m->migrate_row && v >= m->migrated)
	{
		m->migrate_row = 0;
		return 1;
	}
	return 0;
}

// The DataStorage operation for a write. `max` keeps two clients or a reconnect
// race from moving the key backwards. A key some other tool filled with a
// non-integer would make the server's max comparison fail, so that one case
// replaces it.
static inline const char *AP_FxMarkerWriteOp(const AP_FxMarker *m, int storeState)
{
	return (storeState == AP_FXM_STORE_INVALID && !m->server_valid) ? "replace" : "max";
}

static inline const char *AP_FxMarkerSourceName(int source)
{
	switch (source)
	{
	case AP_FXM_SRC_SERVER:   return "server";
	case AP_FXM_SRC_MIGRATED: return "migrated";
	case AP_FXM_SRC_FRESH:    return "fresh";
	default:                  return "pending";
	}
}

#endif
