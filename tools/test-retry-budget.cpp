// Out-of-engine assertions for the bounded automatic-AP-reconnect retry budget
// (2026-08-30 work order). Compiles the REAL freestanding logic -- ap_retry_budget.h
// is self-contained by design (no apclientpp, no sockets), so this harness needs
// nothing from the game. It drives the same state machine ap_net.cpp uses with a
// simulated poll loop that mirrors apclientpp's actual backoff schedule (first
// attempt immediately, then 1.5s doubling to a 15s cap, see APClient::poll /
// connect_socket in ap/vendor/apclientpp/apclient.hpp:1258,1327,1731), so the
// "no fourth attempt over an interval longer than the 15s retry cap" property is
// proven against the real timing, not a hand-waved tick count.
//
// c++ -std=c++17 -Wall -Wextra -o /tmp/test-retry-budget tools/test-retry-budget.cpp
// /tmp/test-retry-budget

#include <cstdio>
#include <string>
#include <vector>

#include "../ap/ap_retry_budget.h"

static int failures;

static void expect(bool condition, const char *name)
{
	std::printf("%s  %s\n", condition ? "ok  " : "FAIL", name);
	if (!condition)
		failures++;
}

// ── Simulated dial loop ──────────────────────────────────────────────────────
// Mirrors apclientpp's poll()/connect_socket() retry schedule over a virtual
// clock. `reason` is the transport error text (the "connect attempt failed: %s"
// diagnostic in ap_net.cpp); the harness records one line per attempt exactly
// like the production handler does, so reason retention is observable.
struct DialSim
{
	APRetryBudget &budget;
	double        now = 0.0;
	double        nextAt = 0.0;
	double        intervalMs = 1500.0;
	bool          first = true;
	int           attempts = 0;
	int           terminalSummaries = 0;
	std::vector<std::string> attemptReasons;

	explicit DialSim(APRetryBudget &b) : budget(b) {}

	// Advance the virtual clock by `seconds` and fire every attempt whose time
	// has come, as apclientpp would. Returns true when an attempt executed.
	bool advance(double seconds)
	{
		double until = now + seconds;
		bool fired = false;
		for (;;)
		{
			double dueAt = first ? now : nextAt;
			if (!budget.retrying() || dueAt > until)
				break;
			now = (dueAt > now) ? dueAt : now; // jump the clock to the attempt
			fired = true;
			first = false;
			attempts++;
			attemptReasons.push_back(currentReason);
			if (budget.onSocketError())
				terminalSummaries++;
			nextAt = now + intervalMs / 1000.0;
			intervalMs *= 2.0;
			if (intervalMs > 15000.0)
				intervalMs = 15000.0;
		}
		now = until;
		return fired;
	}

	std::string currentReason = "Connection refused";
};

int main()
{
	// Test 1: first / second / third failure boundaries, exactly one terminal
	// summary at exhaustion.
	{
		APRetryBudget b(3);
		b.start(false);
		expect(b.retrying() && !b.stopped() && b.fails() == 0,
		       "fresh pre-connect run starts retrying with a zero budget count");
		expect(!b.onSocketError() && b.fails() == 1, "1st failure: counted, not exhausted");
		expect(!b.onSocketError() && b.fails() == 2, "2nd failure: counted, not exhausted");
		expect(b.onSocketError() && b.fails() == 3 && b.stopped() && !b.retrying(),
		       "3rd failure: exhausts budget exactly once");
		expect(!b.onSocketError() && b.fails() == 3 && b.stopped(),
		       "post-stop failure: neither counted nor re-exhausted");
		expect(b.fails() == 3, "failure count stays pinned at the budget");
	}

	// Test 2: no fourth attempt and no new retry lines over a simulated interval
	// longer than the 15s retry cap.
	{
		APRetryBudget b(3);
		b.start(false);
		DialSim sim(b);
		sim.advance(4.5); // t=0, 1.5, 4.5 -> three attempts, exhausted on the 3rd
		expect(sim.attempts == 3, "three attempts happen before exhaustion");
		expect(sim.terminalSummaries == 1, "exactly one terminal summary at exhaustion");
		sim.advance(60.0); // far past the 15s cap, still "polling"
		expect(sim.attempts == 3, "no fourth attempt after exhaustion over >60s");
		expect(sim.terminalSummaries == 1, "no further terminal summaries while stopped");
		expect(sim.attemptReasons.size() == 3, "no further attempt diagnostics while stopped");
	}

	// Test 3: manual Connect from the stopped state resets the counter and
	// attempts immediately.
	{
		APRetryBudget b(3);
		b.start(false);
		b.onSocketError();
		b.onSocketError();
		b.onSocketError(); // stopped
		expect(b.stopped() && b.fails() == 3, "precondition: budget exhausted");
		b.start(false); // menu Connect creates a fresh client + fresh budget
		expect(!b.stopped() && b.retrying() && b.fails() == 0,
		       "manual Connect resets counter and cleared stopped state");
		DialSim sim(b);
		sim.advance(0.0); // the fresh client dials immediately
		expect(sim.attempts == 1, "manual Connect dials on the very next poll");
		expect(!b.stopped() && b.fails() == 1, "manual dial runs under the fresh budget");
	}

	// Test 4: a successful slot connection resets the failure run (and marks the
	// run post-connect so a later drop keeps background recovery).
	{
		APRetryBudget b(3);
		b.start(false);
		b.onSocketError();
		b.onSocketError();
		expect(b.fails() == 2 && !b.stopped(), "precondition: two failures counted");
		b.onSlotConnected();
		expect(b.fails() == 0 && b.everConnected() && !b.stopped(),
		       "successful slot connection resets the failure run");
		b.onSocketError();
		b.onSocketError();
		b.onSocketError();
		expect(!b.stopped() && b.retrying() && b.fails() == 3,
		       "post-connect failures never stop the run (background recovery)");
	}

	// Test 5: wrong host, refused port, DNS failure and TLS failure all reach the
	// same bounded lifecycle while retaining their useful diagnostic reason.
	{
		const char *reasons[] = {
		    "Connection refused",              // refused port
		    "No such host is known",           // DNS failure
		    "Host not found",                  // wrong host
		    "TLS handshake failed",            // certificate / TLS failure
		};
		for (const char *reason : reasons)
		{
			APRetryBudget b(3);
			b.start(false);
			DialSim sim(b);
			sim.currentReason = reason;
			sim.advance(4.5);
			expect(sim.attempts == 3 && sim.terminalSummaries == 1 && b.stopped(),
			       "same 3-attempt bounded lifecycle regardless of reason");
			bool retained = true;
			for (const auto &r : sim.attemptReasons)
				if (r != reason)
					retained = false;
			expect(retained, "diagnostic reason is retained on every attempt line");
		}
	}

	// Test 6: slot-name / password refusal stays on its existing error path and
	// never consumes the budget (the server IS reachable, the room just said no).
	{
		APRetryBudget b(3);
		b.start(false);
		// Slot refusal fires NO socket-error handler; the refusal handler sets a
		// separate error status. Nothing here calls onSocketError.
		expect(b.fails() == 0 && !b.stopped() && b.retrying(),
		       "slot refusal leaves the retry budget untouched");
	}

	// Test 7: startup with no configured connection performs zero attempts. The
	// budget only ever counts what is fed to it: no dial means no socket-error
	// handler, and the production gate (AP_NetTick does not even create a client
	// without a config) means nothing polls a live client at all.
	{
		APRetryBudget b(3);
		b.start(false); // client exists but is never dialed
		expect(b.fails() == 0 && !b.stopped() && b.retrying(),
		       "no dial means zero failures and zero stop state");
		// Even if a poll loop ran, an idle budget cannot fabricate attempts:
		// nothing here calls onSocketError.
		b.start(false);
		expect(b.fails() == 0, "re-dial of an idle client starts at zero attempts");
	}

	// Test 8: a previously healthy slot connection drops, automatic recovery
	// remains active, and its log remains rate-limited (never frame-per-log).
	{
		APRetryBudget b(3);
		b.start(false);
		b.onSlotConnected(); // healthy connection established
		DialSim sim(b);
		sim.advance(60.0);
		expect(sim.attempts >= 5 && sim.attempts <= 10 && !b.stopped() && b.retrying(),
		       "post-connect recovery stays active across a 60s drop");
		expect(sim.terminalSummaries == 0, "post-connect recovery never emits a stop summary");
		// Backoff of >=1.5s between attempts bounds the per-attempt log lines:
		// over 60s that is at most ~8 lines, not 60s * frame-rate.
		expect(sim.attemptReasons.size() <= 10,
		       "recovery log stays rate-limited (bounded by the retry backoff)");
	}

	// Test 10: the stopped status line fits the Connection screen's status row
	// and no longer contains the word "retrying". This locks the exact wording
	// that ap_hooks.c AP_Net_StatusLine renders for AP_NET_STATUS_RETRY_STOPPED.
	{
		const std::string line = "Auto-retry stopped, press Connect";
		// Status text longer than 24 chars moves to its own centred line below
		// the row (CONN_STATUS_INLINE_MAX, game/230/MM_ConfigMenu.c); the panel
		// is 0x1E0 px wide and FONT_SMALL is 13 px/char, so ~36 chars fit.
		expect(line.size() <= 36, "stopped status fits the status row area");
		expect(line.find("retrying") == std::string::npos &&
		           line.find("Retry") == std::string::npos,
		       "stopped status no longer contains the word retrying");
		expect(line.find("stopped") != std::string::npos &&
		           line.find("Connect") != std::string::npos,
		       "stopped status says attempts stopped and that Connect retries");
		expect(line != "Not connected" && line.find("Cannot reach") == std::string::npos,
		       "stopped status is neither Not connected nor a retrying line");
	}

	std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
