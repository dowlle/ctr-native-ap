#ifndef AP_TURBOGRANT_LOGIC_H
#define AP_TURBOGRANT_LOGIC_H

// Pure, engine-independent rules for the Turbo Grant (#224, per race since
// #487). Kept in a header with no engine, no network and no config dependency so
// the runtime module and the host harness (tools/test-turbo-grant.c) exercise
// the exact same rules.
//
// THE RULE SINCE #487 (ruling of 2026-10-23). Once the `Turbo Grant` item has been
// received, a Turbo goes into the weapon slot at the start of every race. The
// item is a switch, not a stack of one-shot grants: more copies do not give more
// Turbos per race, and nothing is counted or persisted. The Itemsanity wait is
// unchanged (AP_TurboGrantDeliverable below).
//
// "Every race" is one Turbo per race ATTEMPT. The attempt boundary is the
// starting countdown: every race start and every restart runs the traffic
// lights, so observing them clears the per-race latch, and so does leaving the
// track. A Turbo that leaves the slot before it is fired (an Item Reroll, a
// Forced Use, the finish line) is not handed out again in the same attempt.
//
// The 0.2.x accounting (received, fired and in-flight counts, with the fired
// count kept in the room's DataStorage under #299) existed to make sure each
// one-shot grant was delivered exactly once. A per-race grant has no such debt,
// so that accounting is gone.

typedef struct AP_TurboGrantRace
{
	int countdownSeen; // the lights of the current attempt were observed on track
	int givenThisRace; // this attempt's Turbo has been put into the slot
} AP_TurboGrantRace;

// One frame of the attempt latch. `onTrack` is a race or battle level, `ended`
// is END_OF_RACE, `countdown` is the traffic-light countdown still running.
static void AP_TurboGrantRaceStep(AP_TurboGrantRace *r, int onTrack, int ended,
                                  int countdown)
{
	if (!onTrack || ended)
	{
		r->countdownSeen = 0;
		r->givenThisRace = 0;
		return;
	}
	if (countdown)
	{
		// A new attempt (first start or a restart): its Turbo is still owed.
		r->countdownSeen = 1;
		r->givenThisRace = 0;
	}
}

// Is this attempt's Turbo still owed? Only after the item was received, only
// once the countdown of this attempt was observed (the race-load gap, see
// ap_wumpa.c), and only once per attempt.
static int AP_TurboGrantOwedThisRace(const AP_TurboGrantRace *r, int received)
{
	return received > 0 && r->countdownSeen && !r->givenThisRace;
}

// The ruled ownership gate (ruled 2026-08-11, restated in the #224 body).
// Itemsanity off: no weapon-item gate applies. Itemsanity on: the separate
// progression `Turbo` weapon item must have been received first. This decides
// DELIVERABILITY only: the race's Turbo waits for it, and is delivered in the
// same race if the weapon item arrives before the race ends.
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
