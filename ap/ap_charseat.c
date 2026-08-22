#ifdef CTR_AP

#include "ap_charseat.h"

// Pure logic only: no engine headers, no ap_net, no ctr_cfg. That is what lets
// tools/test-character-persistence.cpp compile this translation unit on its own
// and drive every connect ordering as a scripted frame sequence.

static int ap_seat_inRoster(int characterID)
{
	return characterID >= 0 && characterID < AP_SEAT_ROSTER;
}

static int ap_seat_unlocked(unsigned mask, int characterID)
{
	if (!ap_seat_inRoster(characterID))
		return 0;
	return (mask & (1u << characterID)) != 0u;
}

// May a seat change be APPLIED on this frame? Yes while no live local driver
// exists (boot, loads, race teardown: the field write lands before the next
// birth reads it), and yes on an idle hub frame (the caller rebirths the driver
// in place there). No while the player is driving anywhere else -- a mid-race
// application changes the racer under them (see the header).
static int ap_seat_applySafe(const struct AP_SeatInput *in)
{
	return !in->driverAlive || in->hubReady;
}

void AP_SeatReset(struct AP_SeatState *s)
{
	if (s == 0)
		return;
	s->seated     = 0;
	s->seatedRev  = 0;
	s->pending    = AP_SEAT_NONE;
	s->pendingRev = 0;
}

void AP_SeatLocalChoice(struct AP_SeatState *s, unsigned rev)
{
	if (s == 0)
		return;
	s->pending   = AP_SEAT_NONE;
	s->seated    = 1;
	s->seatedRev = rev;
}

int AP_SeatIdle(const struct AP_SeatState *s, unsigned rev)
{
	if (s == 0)
		return 1;
	if (s->pending != AP_SEAT_NONE)
		return 0;
	return s->seated && s->seatedRev == rev;
}

int AP_SeatResolve(const struct AP_SeatInput *in)
{
	int wanted;

	if (in == 0)
		return AP_SEAT_NONE;

	// The same precedence AP_SeatStep applies below: a STORED racer wins when
	// there is one and it is eligible, and the seed's starting racer is the
	// answer otherwise -- including while a stored racer is merely deferred,
	// which is exactly the stand-in AP_SEAT_ACT_DEFER seats. The deferral in
	// AP_SeatStep still owns restoring a stored racer later, on a hub frame,
	// once its unlock arrives.
	wanted = in->known ? in->stored : in->startingChar;
	if (!ap_seat_inRoster(wanted) || !ap_seat_unlocked(in->unlockedMask, wanted))
		wanted = in->startingChar;

	// ONE DELIBERATE DIVERGENCE FROM AP_SeatStep, and it is the direction that
	// matters. The starting racer is NOT re-tested against the unlock mask.
	//
	// AP_SeatStep tests it, and defers with no stand-in when it reads locked,
	// because it is deciding whether to CHANGE a racer the player may already be
	// driving and a wrong answer there is disruptive. This function is answering
	// a different question -- what may be committed at adventure start -- and its
	// caller reads a negative as "let the retail garage run", which hands the
	// player a free pick of the eight vanilla starters. Declining here would
	// therefore reopen the very unlock bypass the gate exists to close, and it
	// would do so in a window that occurs on every connect.
	//
	// It is also the only input here that does not depend on the item replay:
	// startingChar comes from slot_data, which is frozen and complete the moment
	// the seed is parsed, while unlockedMask is built from received items that
	// drain 32 a frame. Gating the one on the other is inferring settled state
	// from an in-flight value, which is the failure this feature has already
	// recorded twice.
	if (!ap_seat_inRoster(wanted))
		return AP_SEAT_NONE;

	return wanted;
}

void AP_SeatStep(struct AP_SeatState *s, const struct AP_SeatInput *in,
                 struct AP_SeatAction *out)
{
	int wanted;

	if (out == 0)
		return;
	out->action    = AP_SEAT_ACT_NONE;
	out->character = AP_SEAT_NONE;
	out->fromStore = 0;
	if (s == 0 || in == 0)
		return;

	// The picker owns the choice while it is open, and a reload in flight is
	// already about to apply one. Change no state at all: a deferral must
	// survive the player browsing the roster and closing it again.
	if (in->busy)
		return;

	// A newer SERVER revision supersedes a deferral outright. The racer we were
	// waiting on is no longer the authoritative answer -- another device just
	// wrote a different one -- so re-resolve from scratch below.
	if (s->pending != AP_SEAT_NONE && in->rev != s->pendingRev)
		s->pending = AP_SEAT_NONE;

	if (s->pending != AP_SEAT_NONE)
	{
		if (!ap_seat_unlocked(in->unlockedMask, s->pending))
		{
			// Still not eligible. NO DEADLINE: nothing in the client or the
			// protocol can tell "the unlock has not been replayed yet" from
			// "this seed never granted it", so we never guess with a timer and
			// never discard the choice. Silent retry: no log, no state write.
			// If the racer really was never granted, this costs one bitmask
			// test per frame and changes nothing, forever.
			return;
		}
		if (!ap_seat_applySafe(in))
		{
			// Eligible, but not on a frame where changing the seated racer is
			// safe. The receipt can land mid-race; hold the choice and apply it
			// on the next safe frame (an idle hub, or a teardown window where no
			// driver is alive) instead of switching the player's racer out from
			// under them. Applying during the teardown is what lets a race exit
			// birth the restored racer instead of the stale one.
			return;
		}
		// The unlock receipt landed. This is the ordering the whole deferral
		// exists for.
		out->action    = AP_SEAT_ACT_RESTORE;
		out->character = s->pending;
		out->fromStore = 1;
		s->seated      = 1;
		s->seatedRev   = in->rev;
		s->pending     = AP_SEAT_NONE;
		return;
	}

	if (s->seated && s->seatedRev == in->rev)
		return;

	// The STORED racer wins over the seed's starting racer: `starting_character`
	// says who you begin as, the stored value says who you have since become,
	// and a reconnect must restore the second rather than undo every swap of the
	// session. On a first-ever connect nothing is stored and the subscribe has
	// already seeded the cache with the seed's own starting racer, so the two
	// paths agree.
	wanted = in->known ? in->stored : in->startingChar;
	if (!ap_seat_inRoster(wanted))
		wanted = in->startingChar;
	if (!ap_seat_inRoster(wanted))
		return; // nothing sane to seat; leave the state untouched and retry

	if (!ap_seat_unlocked(in->unlockedMask, wanted))
	{
		s->pending    = wanted;
		s->pendingRev = in->rev;
		// seatedRev is deliberately NOT advanced here. The revision is not
		// resolved until the deferral ends one way or the other, and a pending
		// choice keeps AP_SeatIdle false regardless.
		out->action = AP_SEAT_ACT_DEFER;
		// A stand-in is only for the FIRST seat of a connection, where the
		// alternative is whatever stale racer the local save holds. Mid-session
		// the player is already driving something legitimate, so a stored value
		// that is not yet eligible must not drag them back to the starter. A
		// live driver means mid-session even right after a reconnect reset
		// (s->seated was cleared): the racer being driven IS the legitimate
		// current choice, so no stand-in may replace it.
		if (!s->seated && !in->driverAlive &&
		    ap_seat_unlocked(in->unlockedMask, in->startingChar))
		{
			out->character = in->startingChar;
			s->seated      = 1;
		}
		return;
	}

	// A plain seat applies immediately while no live driver exists (the fresh
	// -connect flow: the `Get` reply must land before the hub's load births the
	// player). With a live driver it waits for a safe frame exactly like a
	// deferred restore -- a mid-session reconnect resolves its revision while
	// the player is driving, and applying it there switches the racer under
	// them (observed live 2026-08-21, mid-race). Holding leaves the revision
	// unresolved, so this retries every frame until a teardown or hub frame.
	if (!ap_seat_applySafe(in))
		return;

	out->action    = AP_SEAT_ACT_SEAT;
	out->character = wanted;
	out->fromStore = (wanted != in->startingChar);
	s->seated      = 1;
	s->seatedRev   = in->rev;
}

#endif // CTR_AP
