// c++ -std=c++17 -Wall -Wextra -o /tmp/test-held-checks tools/test-held-checks.cpp
#include <cstdio>
#include <set>
#include <vector>

#include "../ap/ap_held_checks.h"

static int failures;

static void expect(bool condition, const char *name)
{
	std::printf("%s  %s\n", condition ? "ok  " : "FAIL", name);
	if (!condition)
		failures++;
}

int main()
{
	APHeldChecks held;
	std::set<int64_t> settled;
	std::vector<int64_t> sent;
	auto isSettled = [&](int64_t code) { return settled.count(code) != 0; };
	auto send = [&](int64_t code) { sent.push_back(code); return true; };

	held.onConnected("seed-a", "slot-a", isSettled, send);
	held.hold(101);
	held.hold(101);
	expect(held.size() == 1, "offline duplicate is retained once");

	auto same = held.onConnected("seed-a", "slot-a", isSettled, send);
	expect(same.sent == 1 && sent == std::vector<int64_t>{101},
	       "same identity flushes the retained check once");
	expect(held.empty(), "successful flush drains retained state");

	held.hold(102);
	auto failed = held.onConnected("seed-a", "slot-a", isSettled,
	                               [](int64_t) { return false; });
	expect(failed.rearmed == 1 && held.size() == 1,
	       "failed reconnect send re-arms the check");
	auto retry = held.onConnected("seed-a", "slot-a", isSettled, send);
	expect(retry.sent == 1 && held.empty(), "re-armed check sends on the next reconnect");

	held.hold(103);
	auto wrongSlot = held.onConnected("seed-a", "slot-b", isSettled, send);
	expect(wrongSlot.discarded == 1 && held.empty(),
	       "slot mismatch discards instead of cross-delivering");

	held.hold(104);
	auto wrongSeed = held.onConnected("seed-b", "slot-b", isSettled, send);
	expect(wrongSeed.discarded == 1 && held.empty(),
	       "seed mismatch discards instead of cross-delivering");

	held.hold(105);
	settled.insert(105);
	const size_t sendsBeforeSettled = sent.size();
	auto already = held.onConnected("seed-b", "slot-b", isSettled, send);
	expect(already.settled == 1 && already.attempted == 0,
	       "server-settled check is suppressed on reconnect");
	expect(sent.size() == sendsBeforeSettled && held.empty(),
	       "settled suppression neither sends nor re-arms");

	held.hold(106);
	held.hold(107);
	auto mixed = held.onConnected("seed-b", "slot-b", isSettled,
	                              [](int64_t code) { return code == 106; });
	expect(mixed.sent == 1 && mixed.rearmed == 1 && held.size() == 1,
	       "mixed flush preserves only the failed send");

	// Representative Lettersanity locations across different tracks. All 48
	// identities use this same queue, so sample the shared path rather than
	// duplicating the same assertion for every C, T and R.
	APHeldChecks letters;
	std::vector<int64_t> letterSent;
	letters.onConnected("letter-seed", "letter-slot", isSettled,
	                    [&](int64_t code) { letterSent.push_back(code); return true; });
	letters.hold(35012503); // Roo's Tubes: C
	letters.hold(35012503); // repeated collision/send path while offline
	letters.hold(35012525); // Blizzard Bluff: T
	letters.hold(35012544); // N. Gin Labs: R
	auto letterFlush = letters.onConnected(
	    "letter-seed", "letter-slot", isSettled,
	    [&](int64_t code) { letterSent.push_back(code); return true; });
	expect(letterFlush.sent == 3 && letters.empty(),
	       "representative letter checks survive same-room reconnect");
	expect(letterSent == std::vector<int64_t>({35012503, 35012525, 35012544}),
	       "representative letter checks flush once each in code order");

	// Bounded-reconnect suspension (2026-08-30): a check earned while offline
	// must SURVIVE the pre-connect retry-budget stop. The suspension tears down
	// the network client but deliberately does NOT touch g_held_checks (see
	// ap_net_apply_retry_stop in ap_net.cpp); the checks stay held and flush on
	// the later manual recovery, exactly once.
	{
		APHeldChecks suspended;
		std::vector<int64_t> suspendedSent;
		suspended.onConnected("susp-seed", "susp-slot", isSettled,
		                      [](int64_t) { return false; });
		suspended.hold(5001);
		suspended.hold(5002);
		// The retry budget stops; this is a no-op for the held queue (the
		// production teardown path never calls into it).
		auto afterStop = suspended.onConnected("susp-seed", "susp-slot",
		                                       [](int64_t) { return false; },
		                                       [](int64_t) { return false; });
		expect(afterStop.rearmed == 2 && suspended.size() == 2,
		       "held checks survive a suspension that leaves them unsent");
		auto recovered = suspended.onConnected(
		    "susp-seed", "susp-slot", isSettled,
		    [&](int64_t code) { suspendedSent.push_back(code); return true; });
		expect(recovered.sent == 2 && suspended.empty(),
		       "manual recovery after suspension flushes the held checks once");
		expect(suspendedSent == std::vector<int64_t>({5001, 5002}),
		       "post-suspension flush sends each held check exactly once");
		// Fail-closed stays true after the suspension: a check held offline and
		// then recovered into a DIFFERENT slot is discarded, never delivered.
		suspended.hold(5003);
		auto wrongSlot = suspended.onConnected("susp-seed", "other-slot", isSettled,
		                                       [&](int64_t code) {
			                                       suspendedSent.push_back(code);
			                                       return true;
		                                       });
		expect(wrongSlot.discarded == 1 && suspended.empty(),
		       "post-suspension flush to a different slot is fail-closed (discarded)");
		expect(suspendedSent == std::vector<int64_t>({5001, 5002}),
		       "fail-closed discard never sends the mismatched check");
	}

	std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
