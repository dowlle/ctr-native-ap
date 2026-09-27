// Host harness for the #299 room-scoped one-shot effect markers
// (ap/ap_fxmarker_logic.h). Drives the same decision code the runtime uses for
// traps, Wumpa bundles, Wumpa filler and the Turbo Grant fired count.
//
// Covered: fresh room (key absent, no legacy row), same-room reconnect (key
// present), one-time migration from a 0.2.1 local row, effects held while the
// Get reply is pending and released after it, max-only merging of server
// replies, the non-integer key case, and the Turbo Grant count on top of the
// pending arithmetic.
//
// Not covered here: the DataStorage round trip itself and the in-game effects.
// Those are runtime checks on a real server.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_fxmarker_logic.h"
#include "../ap/ap_turbogrant_logic.h"

static int checks;
static int failures;

static void expect(const char *name, long long got, long long want)
{
	checks++;
	if (got != want)
	{
		printf("FAIL %s: got %lld, want %lld\n", name, got, want);
		failures++;
	}
}

// Replays a connect: items at server indexes 0..n-1, of which `effects` are
// one-shot effect items. Returns how many effects ran, counting held ones that
// are released after the reply. `replyAfter` = how many batches drain before
// the Get reply arrives.
typedef struct Room
{
	int       storeState;
	long long storeValue;
	int       haveLocal;
	long long localValue;
} Room;

static int run_connect(AP_FxMarker *m, const Room *r, const long long *effects, int ne,
                       long long lastIndex, int replyAfter, int *writes)
{
	long long held[64];
	int nheld = 0, ran = 0, i, batch = 0;

	AP_FxMarkerReset(m, -1);
	*writes = 0;
	// Drain one effect per batch, the reply lands after `replyAfter` batches.
	for (i = 0; i < ne; i++, batch++)
	{
		int d;
		if (batch == replyAfter)
		{
			int k;
			AP_FxMarkerResolve(m, r->storeState, r->storeValue, r->haveLocal, r->localValue);
			for (k = 0; k < nheld; k++)
				if (AP_FxMarkerDecide(m, held[k]) == AP_FXM_DECIDE_APPLY)
					ran++;
			nheld = 0;
			*writes += AP_FxMarkerFinishResolve(m);
		}
		d = AP_FxMarkerDecide(m, effects[i]);
		if (d == AP_FXM_DECIDE_APPLY)
			ran++;
		else if (d == AP_FXM_DECIDE_HOLD)
			held[nheld++] = effects[i];
		*writes += AP_FxMarkerNoteDrained(m, effects[i]);
	}
	*writes += AP_FxMarkerNoteDrained(m, lastIndex);
	if (!m->known)
	{
		int k;
		AP_FxMarkerResolve(m, r->storeState, r->storeValue, r->haveLocal, r->localValue);
		for (k = 0; k < nheld; k++)
			if (AP_FxMarkerDecide(m, held[k]) == AP_FXM_DECIDE_APPLY)
				ran++;
		*writes += AP_FxMarkerFinishResolve(m);
	}
	return ran;
}

static void test_fresh_room(void)
{
	// Same seed, same host and port as an old room with a high marker. The new
	// room's storage has no key and no local row matches any more: every effect
	// runs.
	const long long fx[] = {3, 7, 12};
	Room r = {AP_FXM_STORE_ABSENT, 0, 0, 0};
	AP_FxMarker m;
	int writes;
	expect("fresh room: all effects run", run_connect(&m, &r, fx, 3, 20, 1, &writes), 3);
	expect("fresh room: source fresh", m.source, AP_FXM_SRC_FRESH);
	expect("fresh room: marker covers the replay", m.value, 20);
	expect("fresh room: marker written", writes > 0, 1);
}

static void test_same_room_reconnect(void)
{
	// The room remembers index 12. The resend offers 3, 7, 12 again (skip) and a
	// new live trap at 15 (run).
	const long long fx[] = {3, 7, 12, 15};
	Room r = {AP_FXM_STORE_VALID, 12, 0, 0};
	AP_FxMarker m;
	int writes;
	expect("reconnect: only the new effect runs", run_connect(&m, &r, fx, 4, 15, 0, &writes), 1);
	expect("reconnect: source server", m.source, AP_FXM_SRC_SERVER);
	expect("reconnect: marker 15", m.value, 15);
}

static void test_reconnect_with_late_reply(void)
{
	// The reply arrives after three batches. Nothing may run early, and after the
	// reply only index 15 runs.
	const long long fx[] = {3, 7, 12, 15};
	Room r = {AP_FXM_STORE_VALID, 12, 0, 0};
	AP_FxMarker m;
	int writes;
	expect("late reply: no double-apply", run_connect(&m, &r, fx, 4, 15, 3, &writes), 1);
	expect("late reply: marker 15", m.value, 15);
	expect("late reply: rise written once resolved", writes >= 1, 1);
}

static void test_pending_holds(void)
{
	AP_FxMarker m;
	AP_FxMarkerReset(&m, -1);
	expect("pending: effect held", AP_FxMarkerDecide(&m, 4), AP_FXM_DECIDE_HOLD);
	expect("pending: unknown index still applies", AP_FxMarkerDecide(&m, -1), AP_FXM_DECIDE_APPLY);
	expect("pending: drain does not write", AP_FxMarkerNoteDrained(&m, 9), 0);
	expect("pending: marker not advanced", m.value, -1);
	expect("pending: no reply, no resolve",
	       AP_FxMarkerResolve(&m, AP_FXM_STORE_PENDING, 0, 1, 50), 0);
	expect("pending: still held after a non-reply", AP_FxMarkerDecide(&m, 4), AP_FXM_DECIDE_HOLD);
	// A fresh room: the held index 4 runs, and the marker covers 9.
	expect("pending: resolves", AP_FxMarkerResolve(&m, AP_FXM_STORE_ABSENT, 0, 0, 0), 1);
	expect("pending: held effect released", AP_FxMarkerDecide(&m, 4), AP_FXM_DECIDE_APPLY);
	expect("pending: finish writes", AP_FxMarkerFinishResolve(&m), 1);
	expect("pending: marker covers drained", m.value, 9);
	expect("pending: replayed index now skipped", AP_FxMarkerDecide(&m, 4), AP_FXM_DECIDE_SKIP);
}

static void test_migration(void)
{
	// A room played on 0.2.1: no key, local row says 12. The ongoing run must
	// not replay its traps, the value is written, and the row is deleted once
	// the server confirms it.
	const long long fx[] = {3, 7, 12, 15};
	Room r = {AP_FXM_STORE_ABSENT, 0, 1, 12};
	AP_FxMarker m;
	int writes;
	expect("migration: only the new effect runs", run_connect(&m, &r, fx, 4, 15, 0, &writes), 1);
	expect("migration: source migrated", m.source, AP_FXM_SRC_MIGRATED);
	expect("migration: written to the room", writes > 0, 1);
	expect("migration: row kept until confirmed", m.migrate_row, 1);
	expect("migration: a lower reply does not confirm", AP_FxMarkerServerValue(&m, 5), 0);
	expect("migration: confirmation deletes the row", AP_FxMarkerServerValue(&m, 15), 1);
	expect("migration: deletes only once", AP_FxMarkerServerValue(&m, 16), 0);

	// A room key wins over a local row: the row is never consulted.
	AP_FxMarkerReset(&m, -1);
	AP_FxMarkerResolve(&m, AP_FXM_STORE_VALID, 4, 1, 99);
	expect("migration: server key wins", m.value, 4);
	expect("migration: no row involvement", m.migrate_row, 0);

	// An empty legacy row (-1) needs no write and can go at once.
	AP_FxMarkerReset(&m, -1);
	AP_FxMarkerResolve(&m, AP_FXM_STORE_ABSENT, 0, 1, -1);
	expect("migration: empty row needs no write", AP_FxMarkerFinishResolve(&m), 0);
	expect("migration: empty row removable now", m.migrate_row, 0);
}

static void test_max_merge(void)
{
	AP_FxMarker m;
	AP_FxMarkerReset(&m, -1);
	AP_FxMarkerResolve(&m, AP_FXM_STORE_VALID, 10, 0, 0);
	AP_FxMarkerFinishResolve(&m);
	AP_FxMarkerServerValue(&m, 6);
	expect("max: a lower reply never lowers", m.value, 10);
	AP_FxMarkerServerValue(&m, 30);
	expect("max: a higher reply raises", m.value, 30);
	expect("max: a drained batch below does not write", AP_FxMarkerNoteDrained(&m, 25), 0);
	expect("max: a drained batch above writes", AP_FxMarkerNoteDrained(&m, 31), 1);
	expect("max: writes use max", !strcmp(AP_FxMarkerWriteOp(&m, AP_FXM_STORE_VALID), "max"), 1);
}

static void test_invalid_key(void)
{
	// Something else put a string under the key. Treat as absent and replace it
	// on the first write; once the server shows an integer, back to max.
	AP_FxMarker m;
	AP_FxMarkerReset(&m, -1);
	AP_FxMarkerResolve(&m, AP_FXM_STORE_INVALID, 0, 0, 0);
	expect("invalid: treated as fresh", m.source, AP_FXM_SRC_FRESH);
	expect("invalid: first write replaces",
	       !strcmp(AP_FxMarkerWriteOp(&m, AP_FXM_STORE_INVALID), "replace"), 1);
	AP_FxMarkerServerValue(&m, 3);
	expect("invalid: max after a valid reply",
	       !strcmp(AP_FxMarkerWriteOp(&m, AP_FXM_STORE_INVALID), "max"), 1);
}

static void test_turbo_fired_count(void)
{
	AP_FxMarker t;

	// Fresh room: fired 0, every received grant is owed.
	AP_FxMarkerReset(&t, 0);
	AP_FxMarkerResolve(&t, AP_FXM_STORE_ABSENT, 0, 0, 0);
	expect("turbo fresh: fresh", t.source, AP_FXM_SRC_FRESH);
	expect("turbo fresh: nothing to write", AP_FxMarkerFinishResolve(&t), 0);
	expect("turbo fresh: 3 owed", AP_TurboGrantPending(3, (int)t.value, 0), 3);
	expect("turbo fire: write due", AP_FxMarkerIncrement(&t), 1);
	expect("turbo fire: 2 owed", AP_TurboGrantPending(3, (int)t.value, 0), 2);

	// Same room reconnect: the room says 2 fired.
	AP_FxMarkerReset(&t, 0);
	AP_FxMarkerResolve(&t, AP_FXM_STORE_VALID, 2, 0, 0);
	expect("turbo reconnect: 1 owed", AP_TurboGrantPending(3, (int)t.value, 0), 1);

	// Migration from the seed+slot row.
	AP_FxMarkerReset(&t, 0);
	AP_FxMarkerResolve(&t, AP_FXM_STORE_ABSENT, 0, 1, 2);
	expect("turbo migration: source", t.source, AP_FXM_SRC_MIGRATED);
	expect("turbo migration: written", AP_FxMarkerFinishResolve(&t), 1);
	expect("turbo migration: confirm deletes row", AP_FxMarkerServerValue(&t, 2), 1);

	// Pending: the marker is not known, so the runtime delivers nothing.
	AP_FxMarkerReset(&t, 0);
	expect("turbo pending: unknown", t.known, 0);

	// Two clients of one slot: the other one fired more. Never back.
	AP_FxMarkerReset(&t, 0);
	AP_FxMarkerResolve(&t, AP_FXM_STORE_VALID, 1, 0, 0);
	AP_FxMarkerServerValue(&t, 4);
	AP_FxMarkerServerValue(&t, 2);
	expect("turbo max: stays at 4", t.value, 4);
}

int main(void)
{
	(void)AP_TurboGrantDeliverable; // shared header; only the pending rule is used here
	(void)AP_TurboGrantClampReserves;
	test_fresh_room();
	test_same_room_reconnect();
	test_reconnect_with_late_reply();
	test_pending_holds();
	test_migration();
	test_max_merge();
	test_invalid_key();
	test_turbo_fired_count();

	printf("%s: %d checks\n", failures ? "FAIL" : "PASS", checks);
	return failures != 0;
}
