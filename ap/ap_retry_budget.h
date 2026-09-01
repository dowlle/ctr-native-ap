// Pure automatic-reconnect retry budget, shared between ap_net.cpp (production)
// and tools/test-retry-budget.cpp (the harness). No socket, no clock, no I/O:
// everything here is a decision function over the event history, so the bounded
// reconnect lifecycle is provable without spinning up a network stack. Socket
// timing stays apclientpp's job (1.5s / 3s / 6s / ... capped at 15s, see
// APClient::poll()); this owns the "how many failures before we stop" ruling.
//
// Lifecycle ruling (2026-08-30, work order):
//   - The budget applies only while the current client run has never reached
//     SLOT_CONNECTED: startup and manual dials get `budget` failed socket
//     attempts, then automatic retries stop entirely.
//   - Once a healthy slot connection has been established, a later disconnect
//     keeps the existing background recovery for as long as the client lives, so
//     a player mid-seed is not stranded away from the main-menu Connection
//     screen. That recovery is not bounded by this class.
//   - A manual Connect after a stop is a brand-new run: fresh budget, fresh
//     attempt, terminal status cleared (the caller does the re-dial).
//   - Slot-name / password refusal is a separate error path and never consumes
//     the budget: the server IS reachable, the room just said no.

#ifndef AP_RETRY_BUDGET_H
#define AP_RETRY_BUDGET_H

class APRetryBudget
{
public:
	// budget: failed socket attempts allowed before automatic retries stop.
	explicit APRetryBudget(int budget) : m_budget(budget) {}

	// Reset for a fresh client run or a manual dial. Every caller that re-arms a
	// client (startup init, menu Connect, a later process launch) starts here.
	void start(bool everConnected = false)
	{
		m_fails = 0;
		m_everConnected = everConnected;
		m_stopped = false;
	}

	// The room answered a websocket handshake, so it is reachable: any
	// unreachable verdict is stale and the failure run starts over (#146).
	void onSocketUp()
	{
		m_fails = 0;
	}

	// The slot handshake completed: this run has proven connectivity, so any
	// later drop is post-connect background recovery, not a bounded startup
	// dial. Also ends the current failure run.
	void onSlotConnected()
	{
		m_everConnected = true;
		m_fails = 0;
	}

	// One failed socket attempt. Returns true on exactly the call that exhausts
	// the budget (the "emit the terminal summary" edge); every earlier and later
	// call returns false. While stopped the call is a no-op: no further
	// counting, so no fourth-attempt or post-stop retry activity can originate
	// here. Post-connect runs count for the "cannot reach host after N attempts"
	// message but never stop.
	bool onSocketError()
	{
		if (m_stopped)
			return false;
		if (m_fails < m_budget)
			m_fails++;
		if (!m_everConnected && m_fails >= m_budget)
		{
			m_stopped = true;
			return true;
		}
		return false;
	}

	// Whether automatic attempts may still run. False only for a pre-connect run
	// whose budget is exhausted (until the next start()).
	bool retrying() const { return !m_stopped; }

	// 1 if this run ever reached SLOT_CONNECTED (post-connect recovery is live).
	bool everConnected() const { return m_everConnected; }

	// 1 once the pre-connect budget is exhausted (terminal until the next start).
	bool stopped() const { return m_stopped; }

	int  fails() const { return m_fails; }
	int  budget() const { return m_budget; }

private:
	int  m_budget;
	int  m_fails = 0;
	bool m_everConnected = false;
	bool m_stopped = false;
};

#endif // AP_RETRY_BUDGET_H
