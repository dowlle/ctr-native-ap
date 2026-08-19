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

	std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
