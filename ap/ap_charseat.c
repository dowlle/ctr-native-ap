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
		if (!in->hubReady)
		{
			// Eligible, but not on a frame where changing the seated racer is
			// safe. The receipt can land mid-race; hold the choice and apply it
			// on the next hub frame instead of switching the player's racer out
			// from under them.
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
		// that is not yet eligible must not drag them back to the starter.
		if (!s->seated && ap_seat_unlocked(in->unlockedMask, in->startingChar))
		{
			out->character = in->startingChar;
			s->seated      = 1;
		}
		return;
	}

	// A plain seat is NOT hub-gated. It resolves a revision the server just
	// handed us, which on a fresh connect means the asynchronous `Get` reply
	// landing a few frames behind the subscribe's seeded default -- before the
	// hub is up, and before its load births the player. Waiting for hubReady
	// there would spawn the player as the seed's starter and only correct it on
	// the load after, which is the bug persistence exists to prevent. Only a
	// DEFERRED restore waits, because only a deferral has no deadline and can
	// therefore land at an arbitrary moment.
	out->action    = AP_SEAT_ACT_SEAT;
	out->character = wanted;
	out->fromStore = (wanted != in->startingChar);
	s->seated      = 1;
	s->seatedRev   = in->rev;
}

#endif // CTR_AP
