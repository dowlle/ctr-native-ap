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
#include "../ap/ap_editstat_bounds.h"

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

	s.in.hubReady = 0; // the player started a race while the deferral stood
	s.unlock(7);       // and the unlock lands mid-race
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

	if (g_failures == 0)
		std::printf("character persistence (seat orderings + stored-value bound): all checks passed\n");
	else
		std::printf("character persistence: %d FAILURE(S)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
