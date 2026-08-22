// Persistence harness for the character phase (issues #54 / #209).
//
// The three things this covers are the three that CANNOT be reached from a
// build machine with no display and no server:
//
//   1. THE SEAT ORDERING. On a fresh connect the client resets its capability
//      counts and then drains the server's resent ReceivedItems list only 32 a
//      frame, so a stored racer's unlock receipt can arrive several frames after
//      the racer is first considered. Answering "locked, fall back" once and
//      consuming the revision would silently throw the player's actual racer
//      away on every reconnect. ap_charseat.c is the pure state machine that
//      decides this, and every ordering below is a scripted frame sequence
//      through it: default-first, a late server revision, temporarily locked
//      then unlocked, a receipt arriving thousands of frames later, permanently
//      locked, a manual pick mid-deferral, a mid-race receipt, a reconnect and a
//      slot switch (with and without a stored key).
//
//      No test here advances a clock, because the production code no longer
//      reads one. There is no end-of-replay signal in the protocol or in
//      apclientpp to derive a deadline from, so a deferral has none: the tests
//      below pin that a delay of any length cannot lose the stored racer, and
//      that a local pick or a newer server value still cancels the deferral.
//
//   2. THE STORED-VALUE BOUND. Data storage is writable by anything holding the
//      slot and the game side narrows each delta to `short`. A stored 4294967328
//      is well-formed JSON that becomes 32 on the way in. The bound in
//      ap_editstat_bounds.h is what makes that narrowing safe, and it is
//      asserted here against extremes, boundaries and one bad element.
//
//   3. THE PACKAGE BEING ATOMIC. One bad value, a short array or a long one must
//      leave the previous package untouched rather than half-applied.
//
// Standalone, like tools/test-character-phase-seedcfg.cpp: it compiles the
// isolated ap_charseat translation unit plus a faithful re-statement of
// ap_net.cpp's validator (which cannot be linked without dragging apclientpp,
// OpenSSL and the whole websocket stack in), and nothing else. Both sides read
// the SAME ap_editstat_bounds.h, so the constant the test asserts and the one
// production enforces cannot drift.
//
// Build + run (from the repo root). Two steps, because ap_charseat.c is C and is
// compiled here exactly the way the game compiles it (C99, C linkage), then
// linked into the C++ driver:
//
//   gcc -m32 -std=c99 -DCTR_AP -Wall -Wextra -c ap/ap_charseat.c -o /tmp/ap_charseat.o
//   g++ -m32 -std=c++17 -DCTR_AP -Wall -Wextra -I ap/vendor/json/include
//       tools/test-character-persistence.cpp /tmp/ap_charseat.o -o /tmp/test-charpersist
//   /tmp/test-charpersist
#include <cstdio>
#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

extern "C" {
#include "../ap/ap_charseat.h"
}
#include "../ap/ap_charname.h"
#include "../ap/ap_editstat_bounds.h"
#define AP_STATBAR_PURE_ONLY // the draw half needs engine types; the geometry does not
#include "../ap/ap_charfreeze.h"
#include "../ap/ap_charstatrow.h"
#include "../ap/ap_statbar.h"
#include "../ap/ap_garageskip.h"
#include "../ap/ap_pauserow.h"

static int g_failures = 0;

static void check(bool ok, const char *what)
{
	if (!ok)
	{
		std::printf("FAIL: %s\n", what);
		g_failures++;
	}
}

static void check_eq(int got, int want, const char *what)
{
	if (got != want)
	{
		std::printf("FAIL: %s (got %d, want %d)\n", what, got, want);
		g_failures++;
	}
}

// ---------------------------------------------------------------------------
// PART 1 -- the seat state machine.
//
// A tiny driver that mirrors what ap_charswap.c does with the result, so a
// sequence of frames can be written the way it actually plays out.
// ---------------------------------------------------------------------------

struct Sim
{
	AP_SeatState state;
	AP_SeatInput in;
	int          seatedChar; // what data.characterIDs[0] would now hold
	int          logLines;   // how many times the caller would have logged
	int          lastAction;

	Sim(int startingChar, int saveChar)
	{
		AP_SeatReset(&state);
		std::memset(&in, 0, sizeof in);
		in.startingChar = startingChar;
		in.unlockedMask = (1u << startingChar); // the seed's starter is always yours
		in.hubReady     = 1;                    // standing in the hub unless a test says otherwise
		seatedChar      = saveChar;             // whatever the local save held
		logLines        = 0;
		lastAction      = AP_SEAT_ACT_NONE;
	}

	void unlock(int characterID) { in.unlockedMask |= (1u << characterID); }

	// The server told us a racer (a Get reply or a SetNotify). Bumps the
	// revision exactly the way ap_net.cpp does.
	void serverSays(int characterID)
	{
		in.known  = 1;
		in.stored = characterID;
		in.rev++;
	}

	// One frame of AP_CharSwap_SeatStartingCharacter.
	void frame()
	{
		AP_SeatAction act;

		if (AP_SeatIdle(&state, in.rev))
		{
			lastAction = AP_SEAT_ACT_NONE;
			return;
		}
		AP_SeatStep(&state, &in, &act);
		lastAction = act.action;
		switch (act.action)
		{
		case AP_SEAT_ACT_SEAT:
		case AP_SEAT_ACT_RESTORE:
			if (seatedChar != act.character)
				logLines++;
			seatedChar = act.character;
			break;
		case AP_SEAT_ACT_DEFER:
			logLines++; // the one deferral notice
			if (act.character != AP_SEAT_NONE)
			{
				if (seatedChar != act.character)
					logLines++;
				seatedChar = act.character;
			}
			break;
		default:
			break;
		}
	}

	void frames(int n)
	{
		for (int i = 0; i < n; i++)
			frame();
	}

	// The player confirms a racer in the picker.
	void playerPicks(int characterID)
	{
		seatedChar = characterID;
		AP_SeatLocalChoice(&state, in.rev); // ap_net_character_set does not bump rev
	}
};

// The ordinary first connect: no stored key, so subscribe seeds the cache with
// the seed's own starting racer and that is what gets seated -- over whatever
// the local save happened to hold.
static void test_default_first_connect()
{
	Sim s(/*starting*/ 3, /*save*/ 11);
	s.serverSays(3); // subscribe's seed
	s.frame();
	check_eq(s.seatedChar, 3, "first connect seats the seed's starting racer");
	check_eq(s.lastAction, AP_SEAT_ACT_SEAT, "and does it as a plain seat");
	s.frames(600);
	check_eq(s.logLines, 1, "and says nothing further for ten seconds");
}

// The `Get` reply is ASYNCHRONOUS: the seeded default is consumed on frame one
// and the stored racer lands frames later. It must still apply, exactly once.
// This is the 00:04 finding; it stays covered.
static void test_late_server_revision_still_applies()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.unlock(9);
	s.serverSays(3); // subscribe seed
	s.frames(4);
	check_eq(s.seatedChar, 3, "the seed default holds while the Get is in flight");

	s.serverSays(9); // the Get reply, four frames later
	s.frame();
	check_eq(s.seatedChar, 9, "the stored racer applies when it arrives");
	s.frames(600);
	check_eq(s.seatedChar, 9, "and is not re-applied or undone afterwards");
}

// THE FINDING THIS FILE EXISTS FOR. The stored racer's unlock receipt is still
// in the batched initial replay, so for a few frames it reads as locked. The
// choice must survive that and apply the moment the receipt lands.
static void test_stored_racer_survives_batched_item_replay()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.serverSays(7); // stored racer, unlock item not replayed yet
	s.frame();
	check_eq(s.seatedChar, 3, "the starting racer is seated meanwhile, so play is not blocked");
	check_eq(s.lastAction, AP_SEAT_ACT_DEFER, "and the stored choice is deferred, not dropped");
	check_eq(s.state.pending, 7, "the stored racer is retained as pending");

	int quiet = s.logLines;
	s.frames(50); // still draining
	check_eq(s.logLines, quiet, "retrying is silent: no repeated logging");
	check_eq(s.seatedChar, 3, "and writes nothing while it waits");

	s.unlock(7); // the receipt lands in a later 32-item batch
	s.frame();
	check_eq(s.seatedChar, 7, "the stored racer is restored once its unlock arrives");
	check_eq(s.lastAction, AP_SEAT_ACT_RESTORE, "reported as a restore");
	check_eq(s.state.pending, AP_SEAT_NONE, "and the deferral is closed");

	s.frames(600);
	check_eq(s.seatedChar, 7, "it stays put");
}

// THE HEURISTIC THIS FILE REPLACED. An earlier revision called 120 quiet frames
// proof that the initial replay was over and abandoned the stored racer there.
// A receipt after any such window is legal -- apclientpp has no end-of-replay
// event to derive one from -- so a delay far beyond it must still restore.
static void test_a_receipt_thousands_of_frames_later_still_restores()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.serverSays(7);
	s.frame();
	check_eq(s.state.pending, 7, "the stored racer is deferred");

	int quiet = s.logLines;
	s.frames(5000); // eighty seconds of silence: far past any window we might pick
	check_eq(s.state.pending, 7, "a long silence does not end the deferral");
	check_eq(s.seatedChar, 3, "and nothing is written meanwhile");
	check_eq(s.logLines, quiet, "and nothing is logged meanwhile");

	s.unlock(7); // a late ReceivedItems packet, well past the old 120-frame window
	s.frame();
	check_eq(s.seatedChar, 7, "the stored racer is still restored");
	check_eq(s.lastAction, AP_SEAT_ACT_RESTORE, "as a restore");
	check_eq(s.state.pending, AP_SEAT_NONE, "and the deferral closes");
}

// The other side of the same coin: a stored racer this seed genuinely never
// granted (a re-rolled seed on the same slot name). There is no signal that
// proves that, so the choice is never discarded. It stays inert: a pending id
// and a bitmask test a frame, no writes, no repeated logging, and never a racer
// the seed did not grant.
static void test_a_permanently_locked_stored_racer_stays_pending_and_inert()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.serverSays(7);
	s.frame();
	check_eq(s.state.pending, 7, "deferred at first, the same as the temporary case");
	check_eq(s.seatedChar, 3, "with the starting racer seated so play is not blocked");

	int deferred = s.logLines;
	s.frames(20000);
	check_eq(s.state.pending, 7, "it is still pending after five minutes");
	check_eq(s.seatedChar, 3, "the starting racer is still what is seated");
	check_eq(s.logLines, deferred, "and the wait never says anything again");
	check_eq(s.lastAction, AP_SEAT_ACT_NONE, "every one of those frames was a no-op");
}

// A deferred restore can land at any moment, including mid-race. It must not
// change the racer there: it waits for a hub frame and applies on the first one.
static void test_a_restore_waits_for_a_safe_hub_frame()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.serverSays(7);
	s.frame();
	check_eq(s.state.pending, 7, "the stored racer is deferred");

	s.in.hubReady = 0;    // the player started a race while the deferral stood
	s.in.driverAlive = 1; // and is driving
	s.unlock(7);          // and the unlock lands mid-race
	s.frames(600);
	check_eq(s.seatedChar, 3, "the racer is NOT changed during the race");
	check_eq(s.state.pending, 7, "the choice is held, not dropped");

	s.in.hubReady = 1; // back in the hub, loading idle
	s.frame();
	check_eq(s.seatedChar, 7, "and applies on the first safe hub frame");
	check_eq(s.lastAction, AP_SEAT_ACT_RESTORE, "as a restore");
}

// The connect-time resolution is NOT hub-gated. The `Get` reply lands seconds
// before the hub is up, and the hub births the player during its own load, so a
// seat that waited for hubReady would spawn the wrong racer and fix it after.
static void test_a_server_seat_does_not_wait_for_the_hub()
{
	Sim s(/*starting*/ 3, /*save*/ 11);
	s.in.hubReady = 0; // sitting in the menus, connecting
	s.unlock(9);
	s.serverSays(3); // subscribe seed
	s.frame();
	check_eq(s.seatedChar, 3, "the seeded default seats outside the hub");

	s.serverSays(9); // the asynchronous Get reply
	s.frame();
	check_eq(s.seatedChar, 9, "and so does the stored racer it brings");
	check_eq(s.lastAction, AP_SEAT_ACT_SEAT, "as a plain seat");
}

// A deferral that appears MID-session must not reseat the starting racer. The
// player is already driving something legitimate; only the first seat of a
// connection carries a stand-in.
static void test_a_mid_session_deferral_seats_no_stand_in()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.unlock(9);
	s.serverSays(9);
	s.frame();
	check_eq(s.seatedChar, 9, "the session is on 9");

	s.serverSays(7); // another device stored 7, whose unlock we do not have
	s.frame();
	check_eq(s.lastAction, AP_SEAT_ACT_DEFER, "7 is deferred");
	check_eq(s.state.pending, 7, "and retained");
	check_eq(s.seatedChar, 9, "without dragging the player back to the starter");

	s.unlock(7);
	s.frame();
	check_eq(s.seatedChar, 7, "then applies when its unlock arrives");
}

// A deferral must not survive the player making their own choice. If it did, an
// unlock arriving a moment later would yank them off the racer they just chose.
static void test_a_manual_pick_outranks_a_pending_deferral()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.serverSays(7);
	s.frame();
	check_eq(s.state.pending, 7, "7 is deferred");

	s.unlock(5);
	s.playerPicks(5);
	s.unlock(7); // 7's receipt lands right after the swap
	s.frames(600);
	check_eq(s.seatedChar, 5, "the player keeps the racer they picked");
	check_eq(s.state.pending, AP_SEAT_NONE, "the deferral was dropped at the pick");
}

// A newer SERVER value (another device swapped) supersedes a deferral outright.
static void test_a_newer_server_value_supersedes_a_deferral()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.serverSays(7); // locked, deferred
	s.frame();
	check_eq(s.state.pending, 7, "7 is deferred");

	s.unlock(4);
	s.serverSays(4); // another device picked 4
	s.frame();
	check_eq(s.seatedChar, 4, "the newer authoritative racer is seated");
	check_eq(s.state.pending, AP_SEAT_NONE, "and the stale deferral is gone");
}

// The picker is open: the player owns the choice. Nothing may be applied over
// it, and the deferral must survive them browsing and closing again.
static void test_the_picker_is_never_overwritten()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.unlock(9);
	s.in.busy = 1;
	s.serverSays(9);
	s.frames(30);
	check_eq(s.seatedChar, 3, "nothing is seated while the picker owns input");
	check_eq(s.state.seated, 0, "and no revision is consumed");

	s.in.busy = 0;
	s.frame();
	check_eq(s.seatedChar, 9, "the stored racer applies once the picker closes");
}

// A reconnect re-applies the AUTHORITATIVE racer, not whatever the save holds.
static void test_reconnect_reapplies_the_stored_racer()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.unlock(9);
	s.serverSays(9);
	s.frame();
	check_eq(s.seatedChar, 9, "session one ends on 9");

	// Reconnect: ConnectReset, then subscribe seeds the default, then the Get
	// reply brings the stored racer back.
	AP_SeatReset(&s.state);
	s.seatedChar = 0; // the local save is not the source of truth
	s.serverSays(3);  // subscribe seed
	s.frame();
	check_eq(s.seatedChar, 3, "the seed default holds while the Get is in flight");
	s.serverSays(9); // the stored value comes back
	s.frame();
	check_eq(s.seatedChar, 9, "the reconnect restores the racer, it does not undo the swap");
}

// THE 2026-08-21 RECONNECT REGRESSION (Alpha3Matrix stack5). A transient drop
// auto-reconnects while the player is mid-race. The connect reset re-arms the
// seat machine and zeroes the unlock mask (only the seed's starter reads
// unlocked until the batched replay re-delivers the rest), while the fixed
// subscribe keeps the KNOWN stored racer instead of re-seeding the starter.
// Nothing may change the racer fields while the race runs -- not the deferral,
// not a stand-in, not a plain seat -- and the held resolution must apply in
// the teardown window so the exit birth reads it.
static void test_a_mid_race_reconnect_never_switches_the_racer()
{
	Sim s(/*starting*/ 15, /*save*/ 0);
	s.unlock(0);
	s.serverSays(0); // the stored racer from an earlier session
	s.frame();
	check_eq(s.seatedChar, 0, "the session drives the stored racer");

	// Enter a race, then the connection drops and auto-reconnects mid-race.
	s.in.hubReady = 0;
	s.in.driverAlive = 1;
	AP_SeatReset(&s.state);              // AP_CharSwap_ConnectReset re-arms the seat
	s.in.unlockedMask = (1u << 15);      // capability reset: only the starter is unlocked
	// ap_net keeps known=1 / stored=0 (the subscribe no longer re-seeds), so the
	// machine re-resolves the SAME revision: stored 0, unlock not replayed yet.
	s.frame();
	check_eq(s.lastAction, AP_SEAT_ACT_DEFER, "the stored racer defers while its unlock replays");
	check_eq(s.seatedChar, 0, "and no stand-in replaces the racer being driven");

	s.frames(120); // the replay drains while the race continues
	check_eq(s.seatedChar, 0, "mid-race, nothing changes the racer fields");

	s.unlock(0); // the stored racer's unlock replays mid-race
	s.frames(120);
	check_eq(s.seatedChar, 0, "the eligible restore is still held during the race");
	check_eq(s.state.pending, 0, "and stays pending rather than being dropped");

	s.in.driverAlive = 0; // race exit: the level tears down, no local driver
	s.frame();
	check_eq(s.lastAction, AP_SEAT_ACT_RESTORE, "the restore applies in the teardown window");
	check_eq(s.seatedChar, 0, "so the exit birth reads the stored racer, not the starter");
}

// Same reconnect shape, but the resolution lands as a PLAIN SEAT: another
// device stored a different (already unlocked) racer while we raced. It must
// hold exactly like a deferred restore and apply on the teardown.
static void test_a_mid_race_server_seat_waits_for_the_teardown()
{
	Sim s(/*starting*/ 15, /*save*/ 0);
	s.unlock(0);
	s.unlock(4);
	s.serverSays(0);
	s.frame();
	check_eq(s.seatedChar, 0, "the session drives the stored racer");

	s.in.hubReady = 0;
	s.in.driverAlive = 1;
	s.serverSays(4); // another device swapped to 4, whose unlock we already hold
	s.frames(120);
	check_eq(s.seatedChar, 0, "a mid-race server resolution is held, not applied");

	s.in.driverAlive = 0; // race exit teardown
	s.frame();
	check_eq(s.lastAction, AP_SEAT_ACT_SEAT, "the held seat applies in the teardown window");
	check_eq(s.seatedChar, 4, "before the next birth reads the fields");
}

// A reconnect reset while the player stands in the HUB must not seat a
// stand-in either: the racer being driven is the legitimate current choice,
// and only its own deferral resolution may change it.
static void test_an_in_hub_reconnect_seats_no_stand_in()
{
	Sim s(/*starting*/ 15, /*save*/ 0);
	s.unlock(0);
	s.serverSays(0);
	s.frame();
	check_eq(s.seatedChar, 0, "the session drives the stored racer");

	s.in.driverAlive = 1; // standing in the hub with a live driver
	AP_SeatReset(&s.state);
	s.in.unlockedMask = (1u << 15); // replay in flight again
	s.frame();
	check_eq(s.lastAction, AP_SEAT_ACT_DEFER, "the stored racer defers");
	check_eq(s.seatedChar, 0, "without the starter stand-in replacing the driven racer");

	s.unlock(0);
	s.frame();
	check_eq(s.seatedChar, 0, "and resolves back to the same racer once the unlock replays");
	check_eq(s.state.pending, AP_SEAT_NONE, "closing the deferral");
}

// A slot switch is a reset plus a different seed. Nothing may carry over.
static void test_slot_switch_carries_nothing_over()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.serverSays(7); // slot A: 7 stored but locked -> deferred
	s.frame();
	check_eq(s.state.pending, 7, "slot A leaves a deferral outstanding");

	AP_SeatReset(&s.state);
	check_eq(s.state.pending, AP_SEAT_NONE, "the slot switch drops slot A's deferral");
	check_eq(s.state.seated, 0, "and re-arms the seat");

	// Slot B has NO ctr_current_character_<slot> key at all. That is not an empty
	// cache: ap_net_character_subscribe seeds the cache with slot B's own
	// starting racer and bumps the revision, and the Retrieved reply for a
	// missing key is a null the validator refuses, so nothing bumps it again.
	// The whole no-key case therefore looks like exactly one revision carrying
	// the seed's own starter.
	s.in.startingChar = 12; // slot B's own seed
	s.in.unlockedMask = (1u << 12);
	s.serverSays(12); // its own subscribe seed, and the only revision it gets
	s.frame();
	check_eq(s.seatedChar, 12, "slot B seats its own starting racer");
	check_eq(s.state.pending, AP_SEAT_NONE, "with nothing pending from slot A");
	check_eq(s.lastAction, AP_SEAT_ACT_SEAT, "as a plain seat, no deferral");

	s.frames(600);
	check_eq(s.seatedChar, 12, "and slot A's racer never reappears on a no-key slot");
}

// A stored id outside the roster is a key some other tool wrote. Fall back
// rather than seating a character the engine cannot name.
static void test_an_out_of_roster_stored_racer_falls_back()
{
	Sim s(/*starting*/ 3, /*save*/ 3);
	s.in.known  = 1;
	s.in.stored = 99;
	s.in.rev++;
	s.frame();
	check_eq(s.seatedChar, 3, "an out-of-roster stored racer falls back to the starter");
	check_eq(s.lastAction, AP_SEAT_ACT_SEAT, "without a deferral");
}

// ---------------------------------------------------------------------------
// PART 2 -- the stored editable-stat package bound.
//
// A faithful re-statement of ap_net.cpp's ap_edit_accept, sharing the SAME
// bounds header production uses. It cannot be linked directly: ap_net.cpp pulls
// apclientpp, websocketpp, asio and OpenSSL in, none of which this harness has
// any business building.
// ---------------------------------------------------------------------------

static int  g_edit_value[AP_NET_EDITSTAT_COUNT];
static bool g_edit_known = false;

static bool json_int64(const nlohmann::json &e, long long *out)
{
	if (e.is_number_unsigned())
	{
		unsigned long long u = e.get<unsigned long long>();
		if (u > (unsigned long long)0x7FFFFFFFFFFFFFFFull)
			return false;
		*out = (long long)u;
		return true;
	}
	if (!e.is_number_integer())
		return false;
	*out = e.get<long long>();
	return true;
}

static bool edit_accept(const nlohmann::json &v)
{
	long long staged[AP_NET_EDITSTAT_COUNT];
	int       i = 0;

	if (!v.is_array() || v.size() != (size_t)AP_NET_EDITSTAT_COUNT)
		return false;
	for (const auto &e : v)
	{
		long long n = 0;
		if (!e.is_number_integer() || !json_int64(e, &n) || !ap_editstat_value_ok(n))
			return false;
		staged[i++] = n;
	}
	for (i = 0; i < AP_NET_EDITSTAT_COUNT; i++)
		g_edit_value[i] = (int)staged[i];
	g_edit_known = true;
	return true;
}

// A well-formed package of `fill`, with element `at` replaced by `bad`.
static nlohmann::json package(long long fill, int at = -1, const nlohmann::json &bad = nullptr,
                              int count = AP_NET_EDITSTAT_COUNT)
{
	nlohmann::json arr = nlohmann::json::array();
	for (int i = 0; i < count; i++)
	{
		if (i == at)
			arr.push_back(bad);
		else
			arr.push_back(fill);
	}
	return arr;
}

static void reset_edit_state()
{
	std::memset(g_edit_value, 0, sizeof g_edit_value);
	g_edit_known = false;
}

static void test_the_bound_is_the_stat_tables_own_arithmetic()
{
	// Not a taste call: a delta is clamped_target - vanilla_base, the widest cap
	// in the table is ACCEL's 32767 and bases are non-negative, so no delta the
	// editor can produce lies outside this. Symmetric, so the narrowing target's
	// unnegatable minimum is excluded.
	check_eq(AP_NET_EDITSTAT_MAX, 32767, "upper bound is the widest stat cap");
	check_eq(AP_NET_EDITSTAT_MIN, -32767, "lower bound is its negation, not INT16_MIN");
	check(ap_editstat_value_ok(0), "zero is a delta");
	check(!ap_editstat_value_ok(-32768), "-32768 is refused: its negation does not fit");
}

static void test_a_correct_package_round_trips()
{
	reset_edit_state();
	check(edit_accept(package(0)), "an all-zero package is accepted");
	check(edit_accept(package(-1234)), "a plausible tune is accepted");
	check_eq(g_edit_value[0], -1234, "and lands");
	check_eq(g_edit_value[AP_NET_EDITSTAT_COUNT - 1], -1234, "including the last element");
}

static void test_boundary_values_are_accepted()
{
	reset_edit_state();
	check(edit_accept(package(AP_NET_EDITSTAT_MAX)), "the upper boundary is legal");
	check_eq(g_edit_value[0], AP_NET_EDITSTAT_MAX, "and is stored unchanged");
	check(edit_accept(package(AP_NET_EDITSTAT_MIN)), "the lower boundary is legal");
	check_eq(g_edit_value[0], AP_NET_EDITSTAT_MIN, "and is stored unchanged");
	check(edit_accept(package(0, 0, AP_NET_EDITSTAT_MAX)), "a single boundary element is legal");
}

static void test_values_just_past_the_boundary_are_rejected()
{
	reset_edit_state();
	edit_accept(package(5)); // a good package to protect
	check(!edit_accept(package((long long)AP_NET_EDITSTAT_MAX + 1)), "MAX+1 is rejected");
	check(!edit_accept(package((long long)AP_NET_EDITSTAT_MIN - 1)), "MIN-1 is rejected");
	check_eq(g_edit_value[0], 5, "and the previous package survives untouched");
}

static void test_extreme_integers_cannot_sneak_through_a_narrowing()
{
	reset_edit_state();
	edit_accept(package(5));

	// Each of these is a well-formed JSON integer whose low 16 bits look like a
	// perfectly ordinary delta. Before the bound, get<int>() then (short) turned
	// them into exactly that.
	check(!edit_accept(package(4294967328LL)), "2^32 + 32 does not become 32");
	check(!edit_accept(package(70000LL)), "70000 does not become 4464");
	check(!edit_accept(package(-4294967328LL)), "its negative counterpart is rejected too");
	check(!edit_accept(package(9223372036854775807LL)), "INT64_MAX is rejected");
	check(!edit_accept(package(-9223372036854775807LL - 1)), "INT64_MIN is rejected");

	// Beyond int64 entirely: nlohmann reports this as unsigned.
	nlohmann::json huge = nlohmann::json::parse("18446744073709551615");
	check(!edit_accept(package(0, 0, huge)), "a value past INT64_MAX is rejected");

	check_eq(g_edit_value[0], 5, "none of them disturbed the stored package");
}

static void test_one_bad_element_rejects_the_whole_package()
{
	reset_edit_state();
	edit_accept(package(5));

	check(!edit_accept(package(1, 0, 999999)), "a bad FIRST element rejects the package");
	check(!edit_accept(package(1, 34, 999999)), "a bad MIDDLE element rejects the package");
	check(!edit_accept(package(1, AP_NET_EDITSTAT_COUNT - 1, 999999)),
	      "a bad LAST element rejects the package");
	check_eq(g_edit_value[0], 5, "the previous package is untouched, not half-overwritten");
	check_eq(g_edit_value[40], 5, "including elements the good prefix would have reached");
}

static void test_non_integers_are_rejected()
{
	reset_edit_state();
	edit_accept(package(5));
	check(!edit_accept(package(1, 3, 2.5)), "a float element is rejected");
	check(!edit_accept(package(1, 3, "12")), "a numeric STRING is rejected");
	check(!edit_accept(package(1, 3, nullptr)), "a null element is rejected");
	check(!edit_accept(package(1, 3, true)), "a bool element is rejected");
	check_eq(g_edit_value[0], 5, "and the stored package survives each one");
}

static void test_wrong_length_is_rejected()
{
	reset_edit_state();
	edit_accept(package(5));
	check(!edit_accept(package(1, -1, nullptr, AP_NET_EDITSTAT_COUNT - 1)), "a short array is rejected");
	check(!edit_accept(package(1, -1, nullptr, AP_NET_EDITSTAT_COUNT + 1)), "a long array is rejected");
	check(!edit_accept(package(1, -1, nullptr, 0)), "an empty array is rejected");
	check(!edit_accept(nlohmann::json::object()), "an object is rejected");
	check(!edit_accept(nlohmann::json(7)), "a bare integer is rejected");
	check(!edit_accept(nlohmann::json(nullptr)), "a null is rejected");
	check_eq(g_edit_value[0], 5, "and the stored package survives each one");
}

// ---------------------------------------------------------------------------
// PART 4 -- the adventure-start garage gate (2026-08-12 runtime fix).
//
// The vanilla garage picker commits the highlighted racer to
// data.characterIDs[0] and sdata->advProgress.characterID
// (game/233/CS_Garage.c:380-381 and :466-467), AFTER the AP layer has seated the
// seed's racer into those same two fields. Observed live: a seed whose starting
// racer was Neo Cortex, with character unlocks on, loaded in as the Dingodile
// the player picked in the garage. AP_CharSwap_GarageRacer now decides what the
// garage may commit, and AP_SeatResolve is the whole of that decision.
//
// What matters here is that it agrees with AP_SeatStep. Two resolvers with the
// same job and different answers is the Lessons Learned #12 shape, so the last
// test drives both over the same inputs and pins them together rather than
// trusting the comment that says they match.
// ---------------------------------------------------------------------------

static AP_SeatInput resolveInput(int startingChar, unsigned unlockedMask,
                                 int known = 0, int stored = 0)
{
	AP_SeatInput in;
	std::memset(&in, 0, sizeof in);
	in.startingChar = startingChar;
	in.unlockedMask = unlockedMask;
	in.known        = known;
	in.stored       = stored;
	return in;
}

static void test_the_garage_gets_the_seeds_starter_when_nothing_is_stored()
{
	AP_SeatInput in = resolveInput(/*starter*/ 5, /*unlocked*/ (1u << 5));
	check_eq(AP_SeatResolve(&in), 5, "with no stored racer the garage commits the seed's starter");
}

static void test_the_garage_prefers_a_stored_racer()
{
	// The racer the player swapped to in a previous session outranks the YAML
	// starter, exactly as it does on a normal reconnect.
	AP_SeatInput in = resolveInput(5, (1u << 5) | (1u << 12), /*known*/ 1, /*stored*/ 12);
	check_eq(AP_SeatResolve(&in), 12, "a stored, unlocked racer wins over the seed's starter");
}

static void test_the_garage_falls_back_when_the_stored_racer_is_not_unlocked_yet()
{
	// Mid-replay: the stored racer's unlock item has not arrived. The garage
	// commits the starter so play is never blocked; the deferral in AP_SeatStep
	// still owns restoring the stored racer when its receipt lands.
	AP_SeatInput in = resolveInput(5, (1u << 5), /*known*/ 1, /*stored*/ 12);
	check_eq(AP_SeatResolve(&in), 5, "a not-yet-unlocked stored racer falls back to the starter");
}

static void test_the_garage_rejects_an_out_of_roster_stored_racer()
{
	AP_SeatInput hi = resolveInput(5, (1u << 5), 1, AP_SEAT_ROSTER);
	AP_SeatInput lo = resolveInput(5, (1u << 5), 1, -3);
	check_eq(AP_SeatResolve(&hi), 5, "a stored id past the roster falls back to the starter");
	check_eq(AP_SeatResolve(&lo), 5, "a negative stored id falls back to the starter");
}

static void test_the_garage_refuses_only_when_the_starter_is_out_of_roster()
{
	// The ONLY case with no answer. A negative is read by AP_CharSwap_GarageRacer
	// as "let the retail garage run", so every other case has to produce a racer
	// or the gate lapses.
	AP_SeatInput bad = resolveInput(-1, 0u);
	check_eq(AP_SeatResolve(&bad), AP_SEAT_NONE, "an out-of-roster starter yields no answer");
	check_eq(AP_SeatResolve(NULL), AP_SEAT_NONE, "a null input yields no answer");
}

static void test_the_starter_is_committed_even_before_its_unlock_is_replayed()
{
	// The window this covers happens on EVERY connect: unlockedMask is built from
	// received items that drain 32 a frame, so for the first frames of a session
	// even the seed's own starter reads locked. AP_SeatStep is conservative there
	// and defers; the garage gate must not be, because declining would drop the
	// player into the retail picker with a free choice of all eight starters.
	//
	// startingChar comes from slot_data, which is frozen and complete as soon as
	// the seed is parsed, so it is knowable in that window while the mask is not.
	AP_SeatInput nothingUnlockedYet = resolveInput(5, 0u);
	check_eq(AP_SeatResolve(&nothingUnlockedYet), 5,
	         "the seed's starter is committed even while the unlock mask is still empty");

	AP_SeatInput storedTooEarly = resolveInput(5, 0u, /*known*/ 1, /*stored*/ 12);
	check_eq(AP_SeatResolve(&storedTooEarly), 5,
	         "and an ineligible stored racer still falls back to it, not to no-answer");
}

static void test_the_two_resolvers_never_disagree()
{
	// Sweep the inputs that decide WHICH racer (the ones both functions read) and
	// require that a fresh AP_SeatStep, on a frame where it is free to act, seats
	// exactly what AP_SeatResolve says. Deferral is the one legitimate divergence
	// and is asserted as such: when the step defers with a stand-in, resolve must
	// name that same stand-in.
	for (int starter = 0; starter < 4; starter++)
	{
		for (int stored = 0; stored < 4; stored++)
		{
			for (int known = 0; known <= 1; known++)
			{
				for (unsigned mask = 0; mask < 16u; mask++)
				{
					AP_SeatInput in = resolveInput(starter, mask, known, stored);
					in.hubReady     = 1;
					in.rev          = 1;

					AP_SeatState  s;
					AP_SeatAction act;
					AP_SeatReset(&s);
					AP_SeatStep(&s, &in, &act);

					int resolved = AP_SeatResolve(&in);

					if (act.action == AP_SEAT_ACT_SEAT)
					{
						check_eq(resolved, act.character, "resolve agrees with a plain seat");
					}
					else if (act.action == AP_SEAT_ACT_DEFER && act.character != AP_SEAT_NONE)
					{
						check_eq(resolved, act.character, "resolve agrees with a deferral's stand-in");
					}
					else
					{
						// The one deliberate divergence, pinned rather than
						// tolerated: the step machine has declined to seat
						// anything because the starter is not unlocked YET, and
						// the garage gate commits it regardless. Assert both that
						// this is the only shape of disagreement and that the
						// disagreement is exactly the starter.
						check(act.action == AP_SEAT_ACT_DEFER,
						      "the only case the step machine seats nothing is a deferral");
						check(!(mask & (1u << starter)),
						      "and it happens only while the starter reads locked");
						check_eq(resolved, starter,
						         "where the garage gate commits the starter anyway");
					}
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// PART 5 -- the picker's portrait fallback.
//
// gGT->ptrIcons is rebuilt per level from the level's icon table plus the
// resident MPK (game/DecalGlobal.c:19-49), so whether every character portrait
// is resident in a given hub is a runtime data fact. When one is not, the tile
// must draw a name instead of nothing.
//
// The live 2026-08-12 session made this worth pinning. A tile DID render blank
// and the fallback was blamed; it was not the cause -- the cursor highlight was
// a filled box painted over a portrait that was resident all along -- but the
// fallback was also genuinely unable to help, because it handed
// DecalFont_DrawLine whatever sdata->lngStrings held, including NULL, and
// DecalFont_DrawLine dereferences it (game/DecalFont.c:125).
// ---------------------------------------------------------------------------

static void test_the_fallback_prefers_the_localised_name()
{
	check(std::strcmp(AP_CharName_Pick("Dingodile", "dingo"), "Dingodile") == 0,
	      "the localised name is used when it is there");
}

static void test_the_fallback_survives_a_missing_localised_name()
{
	check(std::strcmp(AP_CharName_Pick(NULL, "dingo"), "dingo") == 0,
	      "a NULL localised name falls through to the EXE's debug name");
	check(std::strcmp(AP_CharName_Pick("", "dingo"), "dingo") == 0,
	      "an EMPTY localised name falls through too, not just a NULL one");
}

static void test_the_fallback_always_returns_something_drawable()
{
	// The contract that matters: never NULL, never empty. A tile that draws
	// nothing is indistinguishable from a tile that is not there.
	const char *both_missing = AP_CharName_Pick(NULL, NULL);
	const char *both_empty   = AP_CharName_Pick("", "");
	check(both_missing != NULL && both_missing[0] != '\0',
	      "with neither name available the fallback still returns a drawable string");
	check(both_empty != NULL && both_empty[0] != '\0',
	      "two empty strings still return a drawable string");
}

// ---------------------------------------------------------------------------
// PART 6 -- the hub pause menu's SELECT CHARACTER row (#238).
//
// The row is inserted below RESUME, which renumbers everything under it. Two
// things there are pure index arithmetic that reads as obviously correct and is
// not: the up/down wiring (an off-by-one gives an unreachable row or a cursor
// trap, which no build gate notices) and the selection shift across a row-set
// swap (rowSelected persists across pause opens, so a connect or disconnect
// mid-session has to carry the highlight, and the two directions must be exact
// inverses or the highlight walks a row per swap).
//
// Both come from ap/ap_pauserow.h, which is the table MainFreeze.c builds the
// real MenuRow array from, so this sweeps the shipped wiring rather than a copy.
// ---------------------------------------------------------------------------

static void test_every_pause_row_is_reachable_both_ways()
{
	bool reachedByDown[AP_PAUSEROW_COUNT] = {false};
	bool reachedByUp[AP_PAUSEROW_COUNT] = {false};

	for (int r = 0; r < AP_PAUSEROW_COUNT; r++)
	{
		int up = AP_PAUSEROW_NAV[r][0];
		int down = AP_PAUSEROW_NAV[r][1];

		check(up >= 0 && up < AP_PAUSEROW_COUNT, "every up target is a real row");
		check(down >= 0 && down < AP_PAUSEROW_COUNT, "every down target is a real row");
		check(up != r, "no row traps the cursor by pointing up at itself");
		check(down != r, "no row traps the cursor by pointing down at itself");

		reachedByUp[up] = true;
		reachedByDown[down] = true;
	}

	for (int r = 0; r < AP_PAUSEROW_COUNT; r++)
	{
		check(reachedByUp[r], "every row is reachable by pressing up from somewhere");
		check(reachedByDown[r], "every row is reachable by pressing down from somewhere");
	}
}

static void test_pause_row_navigation_is_a_single_cycle()
{
	// Walking down from RESUME must visit all five rows and come back, which is
	// what makes the wiring one wrapping list rather than two disjoint loops.
	int at = AP_PAUSEROW_RESUME;
	for (int step = 0; step < AP_PAUSEROW_COUNT; step++)
	{
		check_eq(at, step, "walking down from RESUME visits the rows in order");
		at = AP_PAUSEROW_NAV[at][1];
	}
	check_eq(at, AP_PAUSEROW_RESUME, "and wraps back to RESUME");

	// Up is the exact reverse of down, everywhere.
	for (int r = 0; r < AP_PAUSEROW_COUNT; r++)
	{
		check_eq(AP_PAUSEROW_NAV[AP_PAUSEROW_NAV[r][1]][0], r, "up undoes down");
		check_eq(AP_PAUSEROW_NAV[AP_PAUSEROW_NAV[r][0]][1], r, "down undoes up");
	}
}

static void test_select_character_sits_directly_below_resume()
{
	check_eq(AP_PAUSEROW_NAV[AP_PAUSEROW_RESUME][1], AP_PAUSEROW_CHARACTER,
	         "down from RESUME reaches SELECT CHARACTER");
	check_eq(AP_PAUSEROW_NAV[AP_PAUSEROW_CHARACTER][0], AP_PAUSEROW_RESUME,
	         "and up from it returns to RESUME");
}

static void test_the_selection_shift_round_trips()
{
	// The retail set has four rows (0..3). Every one of them must survive a trip
	// into the AP set and back unchanged, or the highlight walks on every
	// connect and disconnect.
	for (int vanillaRow = 0; vanillaRow <= 3; vanillaRow++)
	{
		int there = AP_PauseRow_ToApIndex(vanillaRow);
		check(there >= 0 && there < AP_PAUSEROW_COUNT, "the shifted row is in range");
		check(there != AP_PAUSEROW_CHARACTER, "and never lands on the new row itself");
		check_eq(AP_PauseRow_ToVanillaIndex(there), vanillaRow, "and it round-trips back");
	}
}

static void test_losing_the_row_lands_somewhere_harmless()
{
	// SELECT CHARACTER has no retail counterpart. A player highlighting it when
	// the seed disconnects must land on RESUME, not on the row that happens to
	// take its index, and never on the terminator.
	check_eq(AP_PauseRow_ToVanillaIndex(AP_PAUSEROW_CHARACTER), 0,
	         "highlighting the row when it disappears falls back to RESUME");

	for (int apRow = 0; apRow < AP_PAUSEROW_COUNT; apRow++)
	{
		int back = AP_PauseRow_ToVanillaIndex(apRow);
		check(back >= 0 && back <= 3, "no AP row maps onto the retail terminator or past it");
	}
}

static void test_the_shift_is_saturating_not_wrapping()
{
	// Defensive: rowSelected is an s16 read back out of a struct a savestate can
	// restore, so out-of-range input must clamp rather than wrap into a valid
	// looking row.
	check_eq(AP_PauseRow_ToApIndex(-5), AP_PAUSEROW_RESUME, "a negative row clamps to RESUME");
	check_eq(AP_PauseRow_ToApIndex(99), AP_PAUSEROW_COUNT - 1, "a huge row clamps to the last row");
	check_eq(AP_PauseRow_ToVanillaIndex(-5), 0, "and the same both ways, low");
	check_eq(AP_PauseRow_ToVanillaIndex(99), AP_PAUSEROW_COUNT - 2, "and high");
}

// ---------------------------------------------------------------------------
// PART 7 -- the garage skip's session latch.
//
// THIS SECTION EXISTS BECAUSE THE FIRST CUT SOFT-LOCKED THE GAME. The latch was
// a function-local static cleared only when the seed stopped owning the racer,
// which on a character-phase seed never happens, so it was effectively a
// once-per-process flag. menuGarage's state is
// DISABLE_INPUT_ALLOW_FUNCPTRS | EXECUTE_FUNCPTR, meaning its funcPtr is the
// ONLY thing driving that screen, so the skip's early return on a second visit
// left a dead screen needing a restart. Two reachable routes:
//
//   1. Cancelling the adventure name-entry OSK, which retail sends back to the
//      garage (game/SubmitName.c:514-517).
//   2. Starting a second new adventure in the same process, since the static
//      outlives the garage level load.
//
// The re-arm signal is CS_Garage_ZoomOut, which has exactly two callers, one per
// route. The frame sequences below are those two routes, driven through the same
// header the engine calls.
// ---------------------------------------------------------------------------

// A racer id standing in for "the seed owns the choice", and the sentinel for
// "it does not" that AP_CharSwap_GarageRacer returns.
static const int kSeedOwnsRacer = 7;
static const int kNoCharacterPhase = -1;

// One garage frame. Returns 1 if this frame performed the commit and handoff.
static int garageFrame(AP_GarageSkipState *s, int apRacer)
{
	if (!AP_GarageSkip_Owns(apRacer))
	{
		AP_GarageSkip_ShouldCommit(s, apRacer); // re-arm, then run the retail garage
		return 0;
	}
	return AP_GarageSkip_ShouldCommit(s, apRacer);
}

// How many of `frames` consecutive garage frames committed.
static int garageCommits(AP_GarageSkipState *s, int apRacer, int frames)
{
	int n = 0;
	for (int i = 0; i < frames; i++)
		n += garageFrame(s, apRacer);
	return n;
}

static void test_the_skip_commits_once_per_garage_session()
{
	AP_GarageSkipState s;
	AP_GarageSkip_NewSession(&s);

	check_eq(garageFrame(&s, kSeedOwnsRacer), 1, "the first garage frame commits and hands off");
	check_eq(garageCommits(&s, kSeedOwnsRacer, 120), 0,
	         "and no later frame in the same session repeats the handoff");
}

static void test_cancelling_the_name_entry_does_not_soft_lock()
{
	// THE FIRST SOFT-LOCK. The player cancels the OSK, retail points
	// ptrDesiredMenu back at the garage and calls CS_Garage_ZoomOut(1). Without
	// the re-arm the skip returned early forever and the screen was dead.
	AP_GarageSkipState s;
	AP_GarageSkip_NewSession(&s);

	garageCommits(&s, kSeedOwnsRacer, 30); // first session, commits once
	AP_GarageSkip_NewSession(&s);          // CS_Garage_ZoomOut(1) on CANCEL

	check_eq(garageFrame(&s, kSeedOwnsRacer), 1,
	         "returning from a cancelled name entry commits again rather than dead-ending");
	check_eq(garageCommits(&s, kSeedOwnsRacer, 120), 0, "and still only once for that session");
}

static void test_a_second_adventure_does_not_soft_lock()
{
	// THE SECOND SOFT-LOCK. A new adventure reloads the garage, CS_Garage_Init
	// calls CS_Garage_ZoomOut(0), but the old latch was a process-lifetime static
	// that survived the level load.
	AP_GarageSkipState s;
	AP_GarageSkip_NewSession(&s);
	garageCommits(&s, kSeedOwnsRacer, 30);

	AP_GarageSkip_NewSession(&s); // CS_Garage_Init -> CS_Garage_ZoomOut(0)
	check_eq(garageFrame(&s, kSeedOwnsRacer), 1, "a second new adventure commits again");

	AP_GarageSkip_NewSession(&s);
	check_eq(garageFrame(&s, kSeedOwnsRacer), 1, "and a third, and so on");
}

static void test_many_sessions_each_commit_exactly_once()
{
	AP_GarageSkipState s;
	AP_GarageSkip_NewSession(&s);

	for (int session = 0; session < 25; session++)
	{
		AP_GarageSkip_NewSession(&s);
		check_eq(garageCommits(&s, kSeedOwnsRacer, 60), 1,
		         "every garage session commits exactly once, however many there are");
	}
}

static void test_a_seed_without_the_phase_never_commits_and_leaves_the_latch_armed()
{
	AP_GarageSkipState s;
	AP_GarageSkip_NewSession(&s);

	check_eq(garageCommits(&s, kNoCharacterPhase, 60), 0,
	         "with no character phase the skip never commits, so the retail garage runs");

	// And the latch must not have been consumed by those frames: a connect
	// mid-garage has to be able to take over.
	check_eq(garageFrame(&s, kSeedOwnsRacer), 1,
	         "a seed taking ownership mid-session still commits");
}

static void test_losing_the_seed_mid_session_rearms()
{
	// A disconnect while sitting in the garage. The skip stops owning the screen,
	// and the latch must be left armed rather than stale, so a reconnect commits.
	AP_GarageSkipState s;
	AP_GarageSkip_NewSession(&s);
	check_eq(garageFrame(&s, kSeedOwnsRacer), 1, "committed while the seed owned the racer");

	check_eq(garageCommits(&s, kNoCharacterPhase, 10), 0, "then the seed goes away");
	check_eq(garageFrame(&s, kSeedOwnsRacer), 1, "and a reconnect commits again");
}

static void test_the_latch_tolerates_a_null_state()
{
	// Defensive: the engine passes a file-scope address so this cannot happen
	// there, but the helpers are inline and shared, and a silent wrong answer
	// would be worse than a no-op.
	check_eq(AP_GarageSkip_ShouldCommit(NULL, kSeedOwnsRacer), 0, "a null state never commits");
	AP_GarageSkip_NewSession(NULL); // must not fault
	check(AP_GarageSkip_Owns(kSeedOwnsRacer) == 1, "ownership is a pure test of the racer id");
	check(AP_GarageSkip_Owns(kNoCharacterPhase) == 0, "and a negative racer means retail runs");
}

// ---------------------------------------------------------------------------
// PART 8 -- the picker's hold on the kart.
//
// THIS SECTION EXISTS BECAUSE A FLAG WAS MISTAKEN FOR A FREEZE. The picker took
// VEH_FREEZE_DOOR and nothing else. That flag is tested once, in
// VehPhysProc_Driving_PhysLinear (game/Vehicle/VehPhysProc.c:407-411), which
// returns before the driving code and so stops INPUT only; every stage after it
// still runs and still moves the kart from d->baseSpeed, which nothing decays
// while the flag is set. A kart that carried speed into the picker kept driving
// under it. The dev key never showed this, because you are parked when you reach
// for the keyboard; the #238 pause row is entered at speed, and PAUSE_ALL stops
// MainFrame_GameLogic wholesale (game/MAIN/MainFrame.c:137) so that speed
// survives the pause intact. Found in live play on 2026-08-12.
//
// The fix takes the engine's own modal freeze as well (the one the Aku Aku hint
// takes, VehPhysProc_FreezeEndEvent_Init at game/MAIN/MainFrame.c:836), and the
// decision about when to take, re-assert, release and drop it is the pure
// ap/ap_charfreeze.h the engine calls.
//
// The kart below models the TWO HALVES, not CTR physics: it carries a speed, the
// flag half stops input, and only the table half brings that speed to rest. That
// is exactly enough to make the shipped defect executable.
// ---------------------------------------------------------------------------

struct FakeKart
{
	int flagHeld;  // VEH_FREEZE_DOOR
	int tableHeld; // PlayerFreezeFuncTable, installed through the INIT slot
	int baseSpeed; // what the stages after PhysLinear keep moving the kart by
	int alive;     // 0 once a level load has taken this driver away
};

// One driver tick. Returns the distance the kart moves this frame.
static int kartTick(FakeKart *k)
{
	if (!k->alive)
		return 0;

	// The freeze table's PhysLinear zeroes baseSpeed and the turn states every
	// frame (game/Vehicle/VehPhysProc.c:1244-1250).
	if (k->tableHeld)
		k->baseSpeed = 0;

	// The flag gates only the driving code inside PhysLinear. It does not touch
	// the stages that carry the kart along at whatever speed it already has.
	return k->baseSpeed;
}

// The engine half, driven by whatever the pure decision returned.
static void applyFreeze(FakeKart *k, int action)
{
	if (action == AP_CHARFREEZE_HOLD)
	{
		k->flagHeld = 1;
		k->tableHeld = 1;
	}
	else if (action == AP_CHARFREEZE_RELEASE)
	{
		k->flagHeld = 0;
		k->tableHeld = 0;
	}
	// DROP touches nothing: the driver is gone.
}

// One AP_CharSwap_Tick's worth of freeze work, then the driver's own tick, in
// that order, because AP_OnFrame (game/MAIN/MainMain.c:323) runs ahead of
// MainFrame_GameLogic (:412). Returns the distance the kart moved this frame.
static int freezeFrame(AP_CharFreezeState *s, FakeKart *k, int pickerOpen, int hubReady)
{
	AP_CharFreezeInput in;
	in.pickerOpen = pickerOpen;
	in.hubReady = hubReady;

	applyFreeze(k, AP_CharFreeze_Step(s, &in));

	return kartTick(k);
}

// A kart at speed: the state the pause row hands the picker.
static FakeKart movingKart()
{
	FakeKart k;
	k.flagHeld = 0;
	k.tableHeld = 0;
	k.baseSpeed = 900; // any non-zero speed will do
	k.alive = 1;
	return k;
}

static void test_a_kart_carrying_speed_into_the_picker_comes_to_rest()
{
	// The live defect, as a test: open the picker on a moving kart, and it must
	// not travel while the picker is up.
	AP_CharFreezeState s = {0};
	FakeKart k = movingKart();
	int travelled = 0;

	for (int frame = 0; frame < 120; frame++)
		travelled += freezeFrame(&s, &k, 1, 1);

	check_eq(travelled, 0, "a kart that was moving when the picker opened does not move under it");
	check_eq(k.flagHeld, 1, "the flag half is held");
	check_eq(k.tableHeld, 1, "and the table half with it, which is what stops the kart");
}

static void test_the_flag_alone_is_not_a_freeze()
{
	// The rejected behaviour, simulated: hold only VEH_FREEZE_DOOR, as the
	// shipped picker did. The case above fails on it, which is why it is here
	// rather than described in a comment.
	FakeKart k = movingKart();
	int travelled = 0;

	for (int frame = 0; frame < 120; frame++)
	{
		k.flagHeld = 1; // and nothing else
		travelled += kartTick(&k);
	}

	check(travelled > 0, "the flag on its own leaves the kart driving, which is the defect");
}

static void test_the_hold_is_re_asserted_every_frame()
{
	// AH_Door_ThTick releases this exact flag and this exact INIT slot whenever a
	// hub door is open (game/232/AH_Door.c:184-196), and AH_MaskHint does the
	// same at its state 7. The picker now refuses that release while it is open,
	// but the hold must survive one anyway: a hold taken once at open is a hold
	// something else can take away.
	AP_CharFreezeState s = {0};
	FakeKart k = movingKart();
	int travelled = 0;

	for (int frame = 0; frame < 60; frame++)
	{
		if (frame == 20)
		{
			// something else hands the kart back, mid-picker
			k.flagHeld = 0;
			k.tableHeld = 0;
			k.baseSpeed = 900;
		}

		travelled += freezeFrame(&s, &k, 1, 1);
	}

	check_eq(travelled, 0, "a release by something else is re-taken before the driver ticks");
	check_eq(k.tableHeld, 1, "and the hold is back at the end of it");
}

static void test_closing_the_picker_releases_exactly_once()
{
	AP_CharFreezeState s = {0};
	FakeKart k = movingKart();
	AP_CharFreezeInput in;
	int releases = 0;

	in.hubReady = 1;

	for (int frame = 0; frame < 30; frame++)
	{
		in.pickerOpen = (frame < 10);

		int action = AP_CharFreeze_Step(&s, &in);
		if (action == AP_CHARFREEZE_RELEASE)
			releases++;
		applyFreeze(&k, action);
	}

	check_eq(releases, 1, "the close releases exactly once, however long the picker stays shut");
	check_eq(k.flagHeld, 0, "the flag is given back");
	check_eq(k.tableHeld, 0, "and the driving table with it");
}

static void test_losing_the_hub_drops_the_hold_instead_of_releasing_it()
{
	// The level load takes the flag and the driver struct with it. A RELEASE here
	// would write a funcPtr through a pointer that is no longer ours.
	AP_CharFreezeState s = {0};
	AP_CharFreezeInput in;

	in.pickerOpen = 1;
	in.hubReady = 1;
	check_eq(AP_CharFreeze_Step(&s, &in), AP_CHARFREEZE_HOLD, "the picker takes the hold in the hub");

	in.pickerOpen = 0;
	in.hubReady = 0;
	check_eq(AP_CharFreeze_Step(&s, &in), AP_CHARFREEZE_DROP, "losing the hub drops the hold");

	// And nothing follows it: no release arrives once the next hub is ready.
	in.hubReady = 1;
	for (int frame = 0; frame < 10; frame++)
		check_eq(AP_CharFreeze_Step(&s, &in), AP_CHARFREEZE_NONE, "a dropped hold is not released later");
}

static void test_the_hold_is_per_hub_session()
{
	// Same shape as the garage latch: what one hub takes away, the next hub must
	// be able to take again.
	AP_CharFreezeState s = {0};
	FakeKart k = movingKart();
	AP_CharFreezeInput in;
	int travelled = 0;

	in.pickerOpen = 1;
	in.hubReady = 1;
	applyFreeze(&k, AP_CharFreeze_Step(&s, &in));

	in.hubReady = 0;
	applyFreeze(&k, AP_CharFreeze_Step(&s, &in)); // DROP

	// New hub, new driver, and the player opens the picker again at speed.
	k = movingKart();

	for (int frame = 0; frame < 30; frame++)
		travelled += freezeFrame(&s, &k, 1, 1);

	check_eq(travelled, 0, "the picker takes the hold again in the next hub session");
}

static void test_a_picker_that_never_opens_never_touches_the_kart()
{
	AP_CharFreezeState s = {0};
	FakeKart k = movingKart();
	int travelled = 0;

	for (int frame = 0; frame < 60; frame++)
		travelled += freezeFrame(&s, &k, 0, 1);

	check(travelled > 0, "the kart drives normally with the picker shut");
	check_eq(k.flagHeld, 0, "and nothing ever took the flag");
	check_eq(s.held, 0, "nor the hold");
}

static void test_the_hold_tolerates_null_arguments()
{
	// Same defensive shape as the garage latch: the engine passes file-scope
	// addresses, but the helper is inline and shared.
	AP_CharFreezeState s = {0};
	AP_CharFreezeInput in;

	in.pickerOpen = 1;
	in.hubReady = 1;

	check_eq(AP_CharFreeze_Step(NULL, &in), AP_CHARFREEZE_NONE, "a null state does nothing");
	check_eq(AP_CharFreeze_Step(&s, NULL), AP_CHARFREEZE_NONE, "a null input does nothing");
	check_eq(s.held, 0, "and neither takes the hold");
}


// ---------------------------------------------------------------------------
// PART 9 -- the Garage stat bar, and the rows that are no longer slots.
//
// The picker now draws the Garage's own bars (ruling: it must look like the
// Garage, because that is the presentation players already know). Two things
// came out of that which cannot be checked on a build machine and are ugly if
// wrong:
//
//   1. THE SEGMENT ARITHMETIC. Six fixed 13-pixel cells have to render a length
//      that lands anywhere between them, as a run of full cells plus at most one
//      partial. An off-by-one here is a cell drawn one pixel wide, or a last
//      cell that never fills, and neither is visible from here.
//   2. THE ROW/SLOT SPLIT. The picker shows three stats; the edit package still
//      carries four, because it is the wire shape of the stored value and a
//      server may already hold one. So a screen row is no longer a package
//      index, and feeding one in as the other writes the WRONG STAT and
//      persists it on the same keypress. ACCSPD is the one that vanished from
//      the screen and must stay in the package.
// ---------------------------------------------------------------------------

static void test_a_full_bar_is_six_full_segments()
{
	for (int i = 0; i < AP_STATBAR_SEGMENTS; i++)
		check_eq(AP_StatBar_SegmentLen(AP_STATBAR_MAX_LEN, i), AP_STATBAR_SEGMENT_LEN,
		         "every cell of a full bar is full");
}

static void test_an_empty_bar_draws_nothing()
{
	for (int i = 0; i < AP_STATBAR_SEGMENTS; i++)
		check_eq(AP_StatBar_SegmentLen(0, i), 0, "an empty bar draws no cell");

	// Retail's own sign guard: a negative length must not wrap into a huge one.
	for (int i = 0; i < AP_STATBAR_SEGMENTS; i++)
		check_eq(AP_StatBar_SegmentLen(-40, i), 0, "a negative length draws no cell");
}

static void test_the_cells_always_add_up_to_the_bar()
{
	// The property that catches an off-by-one anywhere in the walk: whatever the
	// length, the drawn cells sum to exactly it, and no cell is over-long.
	for (int len = 0; len <= AP_STATBAR_MAX_LEN; len++)
	{
		int total = 0;
		for (int i = 0; i < AP_STATBAR_SEGMENTS; i++)
		{
			int seg = AP_StatBar_SegmentLen(len, i);
			check(seg >= 0 && seg <= AP_STATBAR_SEGMENT_LEN, "a cell is within its own width");
			total += seg;
		}
		check_eq(total, len, "the drawn cells sum to the bar length");
	}
}

static void test_a_partial_bar_is_full_cells_then_one_partial()
{
	// 30 = two full cells (26) plus 4 of the third, and nothing after it.
	check_eq(AP_StatBar_SegmentLen(30, 0), 13, "first cell full");
	check_eq(AP_StatBar_SegmentLen(30, 1), 13, "second cell full");
	check_eq(AP_StatBar_SegmentLen(30, 2), 4, "third cell partial");
	check_eq(AP_StatBar_SegmentLen(30, 3), 0, "and nothing past it");

	// A length sitting exactly on a boundary fills that cell and starts no other.
	check_eq(AP_StatBar_SegmentLen(26, 1), 13, "a boundary length fills its last cell");
	check_eq(AP_StatBar_SegmentLen(26, 2), 0, "and does not open the next one");
}

static void test_the_fill_grows_by_the_garage_rate_and_snaps_down()
{
	// Growth is three pixels a frame and stops exactly on the target.
	int len = 0;
	for (int frame = 0; frame < 4; frame++)
		len = AP_StatBar_Step(len, 10);
	check_eq(len, 10, "the fill stops exactly on its target, not past it");

	// Falling is instant, which is retail's asymmetry, not an accident.
	check_eq(AP_StatBar_Step(60, 12), 12, "a lower target is taken immediately");
	check_eq(AP_StatBar_Step(10, 10), 10, "an unchanged target is a no-op");

	// And it converges from empty to full without overshooting.
	len = 0;
	for (int frame = 0; frame < 200; frame++)
	{
		len = AP_StatBar_Step(len, AP_STATBAR_MAX_LEN);
		check(len <= AP_STATBAR_MAX_LEN, "the fill never exceeds a full bar");
	}
	check_eq(len, AP_STATBAR_MAX_LEN, "and reaches full");
}

static void test_a_value_lands_somewhere_on_the_bar()
{
	const int lo = 100, hi = 200;

	check_eq(AP_StatBar_LenForValue(lo, lo, hi), 1, "the floor of the ladder still shows a bar");
	check_eq(AP_StatBar_LenForValue(lo - 500, lo, hi), 1, "and so does anything under it");
	check_eq(AP_StatBar_LenForValue(hi, lo, hi), AP_STATBAR_MAX_LEN, "the ceiling fills the bar");
	check_eq(AP_StatBar_LenForValue(hi + 500, lo, hi), AP_STATBAR_MAX_LEN, "and so does anything over it");

	// Monotonic in between, and never outside the bar.
	int prev = 0;
	for (int v = lo; v <= hi; v++)
	{
		int len = AP_StatBar_LenForValue(v, lo, hi);
		check(len >= 1 && len <= AP_STATBAR_MAX_LEN, "a length stays on the bar");
		check(len >= prev, "a higher value never shows a shorter bar");
		prev = len;
	}

	// A stat with no spread to rank against sits mid-bar rather than empty or full.
	check_eq(AP_StatBar_LenForValue(42, 7, 7), AP_STATBAR_MAX_LEN / 2, "a flat ladder sits mid-bar");
}

static void test_the_three_rows_are_the_garages_three_stats()
{
	// ap_cs_stats[] order: 0 ACCEL, 1 SPEED, 2 ACCSPD, 3 TURN.
	check_eq(AP_CharStatRow_ToStat(0), 1, "the top row is SPEED, as in the Garage");
	check_eq(AP_CharStatRow_ToStat(1), 0, "then ACCEL");
	check_eq(AP_CharStatRow_ToStat(2), 3, "then TURN");
}

static void test_accspd_is_off_the_screen_but_still_in_the_package()
{
	// The whole point of the split: no row reaches slot 2, so no edit can write
	// it, and it keeps its place in the four-wide package regardless.
	for (int row = 0; row < AP_CS_UI_ROWS; row++)
		check(AP_CharStatRow_ToStat(row) != 2, "no row edits ACCSPD");

	check_eq(AP_CharStatRow_IsShown(2), 0, "ACCSPD is not shown");
	check_eq(AP_CharStatRow_IsShown(0), 1, "ACCEL is");
	check_eq(AP_CharStatRow_IsShown(1), 1, "SPEED is");
	check_eq(AP_CharStatRow_IsShown(3), 1, "TURN is");
}

static void test_a_junk_row_cannot_write_past_the_package()
{
	// Clamping, not wrapping: a junk row must not become a plausible-looking
	// index into someone's saved tune.
	check_eq(AP_CharStatRow_ToStat(-5), AP_CharStatRow_ToStat(0), "a negative row clamps low");
	check_eq(AP_CharStatRow_ToStat(99), AP_CharStatRow_ToStat(AP_CS_UI_ROWS - 1), "a huge row clamps high");
}

static void test_row_navigation_is_a_single_cycle()
{
	// Same property the pause row carries: every row reachable, up the exact
	// reverse of down, no cursor trap.
	int seen[AP_CS_UI_ROWS] = {0};
	int row = 0;
	for (int step = 0; step < AP_CS_UI_ROWS; step++)
	{
		seen[row] = 1;
		row = AP_CharStatRow_Next(row, 1);
	}
	check_eq(row, 0, "walking down returns to the start");
	for (int i = 0; i < AP_CS_UI_ROWS; i++)
		check(seen[i] != 0, "every row is reachable going down");

	for (int i = 0; i < AP_CS_UI_ROWS; i++)
		check_eq(AP_CharStatRow_Next(AP_CharStatRow_Next(i, 1), -1), i, "up undoes down exactly");
}

int main()
{
	test_default_first_connect();
	test_late_server_revision_still_applies();
	test_stored_racer_survives_batched_item_replay();
	test_a_receipt_thousands_of_frames_later_still_restores();
	test_a_permanently_locked_stored_racer_stays_pending_and_inert();
	test_a_restore_waits_for_a_safe_hub_frame();
	test_a_server_seat_does_not_wait_for_the_hub();
	test_a_mid_session_deferral_seats_no_stand_in();
	test_a_manual_pick_outranks_a_pending_deferral();
	test_a_newer_server_value_supersedes_a_deferral();
	test_the_picker_is_never_overwritten();
	test_reconnect_reapplies_the_stored_racer();
	test_a_mid_race_reconnect_never_switches_the_racer();
	test_a_mid_race_server_seat_waits_for_the_teardown();
	test_an_in_hub_reconnect_seats_no_stand_in();
	test_slot_switch_carries_nothing_over();
	test_an_out_of_roster_stored_racer_falls_back();

	test_the_bound_is_the_stat_tables_own_arithmetic();
	test_a_correct_package_round_trips();
	test_boundary_values_are_accepted();
	test_values_just_past_the_boundary_are_rejected();
	test_extreme_integers_cannot_sneak_through_a_narrowing();
	test_one_bad_element_rejects_the_whole_package();
	test_non_integers_are_rejected();
	test_wrong_length_is_rejected();

	test_the_garage_gets_the_seeds_starter_when_nothing_is_stored();
	test_the_garage_prefers_a_stored_racer();
	test_the_garage_falls_back_when_the_stored_racer_is_not_unlocked_yet();
	test_the_garage_rejects_an_out_of_roster_stored_racer();
	test_the_garage_refuses_only_when_the_starter_is_out_of_roster();
	test_the_starter_is_committed_even_before_its_unlock_is_replayed();
	test_the_two_resolvers_never_disagree();

	test_the_fallback_prefers_the_localised_name();
	test_the_fallback_survives_a_missing_localised_name();
	test_the_fallback_always_returns_something_drawable();

	test_every_pause_row_is_reachable_both_ways();
	test_pause_row_navigation_is_a_single_cycle();
	test_select_character_sits_directly_below_resume();
	test_the_selection_shift_round_trips();
	test_losing_the_row_lands_somewhere_harmless();
	test_the_shift_is_saturating_not_wrapping();

	test_the_skip_commits_once_per_garage_session();
	test_cancelling_the_name_entry_does_not_soft_lock();
	test_a_second_adventure_does_not_soft_lock();
	test_many_sessions_each_commit_exactly_once();
	test_a_seed_without_the_phase_never_commits_and_leaves_the_latch_armed();
	test_losing_the_seed_mid_session_rearms();
	test_the_latch_tolerates_a_null_state();

	test_a_kart_carrying_speed_into_the_picker_comes_to_rest();
	test_the_flag_alone_is_not_a_freeze();
	test_the_hold_is_re_asserted_every_frame();
	test_closing_the_picker_releases_exactly_once();
	test_losing_the_hub_drops_the_hold_instead_of_releasing_it();
	test_the_hold_is_per_hub_session();
	test_a_picker_that_never_opens_never_touches_the_kart();
	test_the_hold_tolerates_null_arguments();

	test_a_full_bar_is_six_full_segments();
	test_an_empty_bar_draws_nothing();
	test_the_cells_always_add_up_to_the_bar();
	test_a_partial_bar_is_full_cells_then_one_partial();
	test_the_fill_grows_by_the_garage_rate_and_snaps_down();
	test_a_value_lands_somewhere_on_the_bar();
	test_the_three_rows_are_the_garages_three_stats();
	test_accspd_is_off_the_screen_but_still_in_the_package();
	test_a_junk_row_cannot_write_past_the_package();
	test_row_navigation_is_a_single_cycle();

	if (g_failures == 0)
		std::printf("character persistence (seat orderings + stored-value bound + garage gate + portrait fallback + pause row + skip latch + picker freeze + stat bars): all checks passed\n");
	else
		std::printf("character persistence: %d FAILURE(S)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
