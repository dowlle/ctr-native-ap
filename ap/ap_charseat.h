#ifndef AP_CHARSEAT_H
#define AP_CHARSEAT_H

// ---------------------------------------------------------------------------
// Deterministic seat state machine for the per-slot stored racer (#54/#209).
//
// WHY THIS IS ITS OWN MODULE. The decision "which racer do we seat, and when"
// has to be right across orderings that only ever occur on a live server: a
// first-ever connect, an asynchronous `Get` reply landing frames later, a stored
// racer whose unlock item has not been replayed yet, a stored racer that this
// seed genuinely never granted, another device writing a new value mid-session,
// a reconnect and a slot switch. None of those are reachable from a build
// machine with no display, so the logic is lifted out of ap_charswap.c into a
// pure state machine with no engine, no network and no config dependencies, and
// tools/test-character-persistence.cpp drives all of them as scripted frame
// sequences.
//
// THE ORDERING THIS EXISTS TO PROTECT. On a fresh connect the client resets its
// capability counts and then drains the server's resent ReceivedItems list only
// 32 entries per frame (ap_hooks.c). A character unlock receipt can therefore
// arrive several frames AFTER the stored racer is first considered, so for a
// while a perfectly valid stored racer reads as locked. Answering "not unlocked,
// fall back" once and marking the revision consumed would throw the player's
// actual racer away every reconnect -- the same class of bug as the asynchronous
// `Get` race, one layer further in. So a stored racer that is not yet eligible is
// DEFERRED, not discarded: the seed's starting racer is seated meanwhile so play
// is never blocked, and the stored choice is retried silently until its unlock
// shows up.
//
// NO TIMER DECIDES ANYTHING. An earlier revision of this file treated a quiet
// window (no items drained for N frames) as proof that the initial replay was
// over, and abandoned the stored racer at that point. That is not sound.
// apclientpp hands each `ReceivedItems` packet's starting index to the items
// handler (ap/vendor/apclientpp/apclient.hpp, the `cmd == "ReceivedItems"`
// branch) and offers no end-of-replay event or expected total, and a slot with an
// empty inventory produces no callback at all, so there is nothing in the client
// or the protocol that says "the replay is complete". Silence is only silence: a
// packet arriving after any window we picked could still carry the valid unlock,
// which recreates exactly the bug the deferral exists to prevent. So a deferral
// now has no deadline. It ends when the racer becomes eligible, when a newer
// server revision supersedes it, when the player picks for themselves, or on a
// reset. A stored racer this seed genuinely never granted simply leaves a pending
// id and a bitmask test per frame, forever. That is the cheap end of the trade:
// never discarding a valid choice on timing is worth an inert pending bit.
//
// WHEN A DEFERRED RESTORE MAY BE APPLIED. Because a deferral has no deadline,
// its restore can land arbitrarily late -- mid-race, mid-cutscene. Changing the
// seated racer there would alter the player's selection under them, so a RESTORE
// waits for `hubReady`: the adventure hub open, loading idle, a live local driver
// (ap_cs_hubReady in ap_charswap.c), and no picker or swap in flight. A receipt
// arriving mid-race applies on the next safe hub frame instead.
//
// A plain SEAT applies "where we stand" only while NO live local driver exists.
// On a fresh boot that is the whole connect window -- the asynchronous `Get`
// reply lands before the hub's own load births the player, and waiting for
// hubReady there would spawn the player as the seed's starter and correct it
// only on the load after, which is the bug persistence exists to prevent. But a
// mid-session reconnect steps the same machine while the player is DRIVING
// (observed live 2026-08-21: the reconnect reset re-armed the seat and the
// resolution landed mid-race, switching the racer fields under a forced-racer
// race). So every application -- SEAT, RESTORE, and the DEFER stand-in -- is
// gated on `applySafe`: either no live local driver (loads, boot, race
// teardown; the write lands before the next birth reads it) or a hubReady
// frame (where the caller rebirths the driver in place). A held resolution
// keeps its revision unresolved and retries every frame, so the first teardown
// or hub frame applies it.
//
// EVERY EXIT IS QUIET OR ONE-SHOT. Retrying costs a bitmask test per frame and
// emits nothing; the caller logs only on the transitions below, each of which
// fires at most once per deferral.
// ---------------------------------------------------------------------------

// No racer. Distinct from character id 0, which is a real racer.
#define AP_SEAT_NONE   (-1)
#define AP_SEAT_ROSTER 16

// What the caller should do with the result of a step.
enum
{
	// Nothing to do this frame. The overwhelmingly common answer.
	AP_SEAT_ACT_NONE = 0,
	// Seat `character`; this revision is fully resolved.
	AP_SEAT_ACT_SEAT,
	// The stored racer is not eligible yet and stays pending, with no deadline.
	// `character` is the seed's starting racer to seat as a STAND-IN so play is
	// never blocked, or AP_SEAT_NONE when no stand-in should be seated (either
	// this connection already seated one, or even the stand-in is unavailable).
	AP_SEAT_ACT_DEFER,
	// The deferred stored racer became eligible on a safe frame: seat
	// `character` now.
	AP_SEAT_ACT_RESTORE
};

struct AP_SeatState
{
	int      seated;     // 1 once this connection has seated a racer at all
	unsigned seatedRev;  // the ap_net revision already fully resolved
	int      pending;    // AP_SEAT_NONE, or a stored racer awaiting its unlock
	unsigned pendingRev; // the revision that produced `pending`
};

struct AP_SeatInput
{
	unsigned rev;          // ap_net_character_revision()
	int      known;        // ap_net_character_known() said yes
	int      stored;       // the cached racer, meaningful only when `known`
	int      startingChar; // ctr_cfg.starting_character, the fallback
	unsigned unlockedMask; // bit i = AP_CharacterUnlocked(i)
	int      busy;         // picker open / swap requested / reload in flight
	int      hubReady;     // adventure hub idle: safe to change a seated racer
	int      driverAlive;  // a live local driver exists (gGT->drivers[0] != NULL)
};

struct AP_SeatAction
{
	int action;    // one of AP_SEAT_ACT_*
	int character; // the racer to seat, or AP_SEAT_NONE
	int fromStore; // 1 when `character` came from data storage, 0 = seed default
};

// Fresh connect / slot switch: forget everything, including any deferral.
void AP_SeatReset(struct AP_SeatState *s);

// The player chose a racer through the picker. Their choice outranks anything
// still pending from storage, so drop the deferral and treat `rev` as resolved.
// Without this, an unlock arriving after a manual swap would yank the player
// back to the racer they had just swapped away from.
void AP_SeatLocalChoice(struct AP_SeatState *s, unsigned rev);

// 1 when a step at revision `rev` provably cannot do anything. Lets the caller
// skip building the unlock mask on the ordinary frame. False for as long as a
// deferral is outstanding, which is the per-frame cost of holding one.
int AP_SeatIdle(const struct AP_SeatState *s, unsigned rev);

// Which racer is authoritative RIGHT NOW, or AP_SEAT_NONE when nothing sane is.
//
// Stateless: it reads the same inputs AP_SeatStep does and applies the same
// precedence, but keeps no bookkeeping and consumes no revision, so a caller
// that is not the per-frame seat driver can ask the question without disturbing
// a deferral. The adventure-start garage gate is that caller -- it has to know
// the answer at a moment that is not a seat frame at all.
//
// `busy` and `hubReady` are ignored: those govern WHEN a seat may be applied,
// not WHICH racer is right, and the garage is committing a value rather than
// changing one under a live driver.
int AP_SeatResolve(const struct AP_SeatInput *in);

// Advance one frame. Never touches game state itself: it returns what to do.
void AP_SeatStep(struct AP_SeatState *s, const struct AP_SeatInput *in,
                 struct AP_SeatAction *out);

#endif // AP_CHARSEAT_H
