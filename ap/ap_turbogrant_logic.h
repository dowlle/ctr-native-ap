#ifndef AP_TURBOGRANT_LOGIC_H
#define AP_TURBOGRANT_LOGIC_H

// Pure, engine-independent rules for the #224 Turbo Grant. Kept in a header with
// no engine, no network and no config dependency so the runtime module and the
// host harness (tools/test-turbo-grant.c) exercise the exact same arithmetic.
//
// WHY THE ACCOUNTING IS SHAPED LIKE THIS. Every other one-shot AP effect on this
// client (traps, the Wumpa filler) dedups replays against ap_fx_seen_max, the
// highest server item index whose BATCH was drained. That mechanism is wrong for
// this item and using it would lose grants: it advances when the batch drains,
// so a grant that arrived while the player was in the hub -- and therefore could
// not enter a weapon slot yet -- would be counted as consumed the moment the
// batch was seen, and a reconnect would never hand it back. The issue's rule 4
// is explicit that no received grant is silently discarded.
//
// So this item tracks three numbers instead of a high-water mark:
//
//   received   -- rebuilt from ZERO on every fresh connect out of the server's
//                 authoritative full ReceivedItems replay. Idempotent by
//                 construction: replaying the same list twice yields the same
//                 number, and a slot switch replays a different list.
//   fired      -- persisted per seed AND slot. Incremented ONLY when a delivered
//                 grant is actually fired at the weapon choke point, which is the
//                 only moment the player has provably had the thing.
//   inFlight   -- session-only, 0 or 1: a delivered Turbo sitting in the weapon
//                 slot, not yet fired. Never persisted, because a process that
//                 exits with one in the slot did not deliver it in any sense the
//                 player can use.
//
// pending = received - fired - inFlight, floored at zero. That single expression
// is what makes every case in the issue's rule 7 fall out:
//
//   * RECONNECT / duplicate replay: received rebuilds to the same value, fired is
//     read back from disk, pending is unchanged.
//   * SLOT or SEED CHANGE: a different persistence row is read, so one identity's
//     fired count can never suppress another's grants.
//   * DEATH, RACE RESTART, or anything else that removes the Turbo from the slot
//     before it is fired: the caller drops inFlight without touching fired, and
//     pending goes back up by one on its own. Nothing has to remember that a
//     requeue is owed.
//   * A HOSTILE OR CORRUPT persisted fired count cannot manufacture grants,
//     because the floor is zero, and cannot be made to overflow, because it is
//     clamped to received before it is used.

// Clamp a persisted fired count into the range the current session can justify.
// A file that has been hand-edited, truncated, or written by a future build must
// never be able to drive the arithmetic negative or past what the server has
// actually sent this slot.
static int AP_TurboGrantClampFired(int fired, int received)
{
	if (fired < 0)
		return 0;
	if (fired > received)
		return received;
	return fired;
}

// Grants this slot is still owed. `inFlight` is 0 or 1; any other value is
// treated as 1, so a corrupt caller cannot double-count a delivery.
static int AP_TurboGrantPending(int received, int fired, int inFlight)
{
	int pending;

	if (received < 0)
		received = 0;
	fired = AP_TurboGrantClampFired(fired, received);
	if (inFlight != 0)
		inFlight = 1;

	pending = received - fired - inFlight;
	return pending < 0 ? 0 : pending;
}

// The ruled ownership gate (ruled 2026-08-11, restated in the #224 body).
// Itemsanity off: no weapon-item gate applies. Itemsanity on: the separate
// progression `Turbo` weapon item must have been received first. This decides
// DELIVERABILITY only -- a grant that fails it stays queued, it is never lost.
static int AP_TurboGrantDeliverable(int itemsanityOn, int turboWeaponOwned)
{
	return itemsanityOn ? (turboWeaponOwned != 0) : 1;
}

// ── Reserve overflow rail (#224 item 6) ─────────────────────────────────────
//
// `driver->reserves` is an s16 and VehFire_Increment accumulates into it with
// wrapping 16-bit arithmetic; nothing in the engine bounds the sum. Vanilla never
// gets near the boundary (a normal pad banks 0x3c0, the Turbo weapon 0x960), but
// a repeatable on-demand Turbo source is exactly the kind of thing that makes
// "never" an assumption rather than a fact, so the issue requires the sum to be
// clamped below the boundary on any reserve change that passes through an enabled
// tier. This is a rail, not a balance change: below the boundary it returns its
// input unchanged, so a seed with the boost pack on behaves identically until the
// point where vanilla would have wrapped to a negative reserve.
//
// The three branches mirror VehFire_Increment's own three, because what reaches
// `driver->reserves` is NOT the same as the `reserves` argument in two of them:
//
//   type & 1        turbo pad / boost powerup: adds (reserves - outsideTimer),
//                   and only when that is positive. The Turbo weapon itself comes
//                   through here (VehPickupItem_ShootNow case 0 passes type 9).
//   type & 0x10 == 0  start line, hang time, powerslide: adds `reserves` whole.
//   else            super engine: ASSIGNS rather than adds, so only the s16
//                   ceiling applies.
#define AP_TURBOGRANT_RESERVE_MAX 0x7fff

static int AP_TurboGrantClampReserves(int currentReserves, int outsideTimer,
                                      int reserves, unsigned type)
{
	int headroom;

	if (currentReserves < 0)
		currentReserves = 0;
	headroom = AP_TURBOGRANT_RESERVE_MAX - currentReserves;
	if (headroom < 0)
		headroom = 0;

	// VehFire_Increment's pad branch reads the outside timer through a u16 cast
	// before subtracting, so match that rather than the signed value.
	outsideTimer = (int)(unsigned short)outsideTimer;

	if ((type & 1u) != 0)
	{
		if (reserves > outsideTimer && reserves - outsideTimer > headroom)
			reserves = outsideTimer + headroom;
	}
	else if ((type & 0x10u) == 0)
	{
		if (reserves > headroom)
			reserves = headroom;
	}
	else
	{
		if (reserves > AP_TURBOGRANT_RESERVE_MAX)
			reserves = AP_TURBOGRANT_RESERVE_MAX;
	}
	return reserves;
}

#endif // AP_TURBOGRANT_LOGIC_H
