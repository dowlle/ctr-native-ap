#ifndef AP_GRANTS_LOGIC_H
#define AP_GRANTS_LOGIC_H

/* Pure accounting rules for queued, on-demand AP weapon grants.
 *
 * ReceivedItems is authoritative for how many grants exist. Only firing spends
 * one. Delivery into the weapon slot is therefore an in-flight reservation,
 * not consumption: if death, restart or another effect clears that slot before
 * the normal fire hook runs, the reservation is released and can be delivered
 * again.
 */
typedef struct AP_GrantLedger
{
	long long received;
	long long fired;
	int inFlight;
} AP_GrantLedger;

static void AP_GrantLedgerRestore(AP_GrantLedger *ledger,
	long long received, long long fired)
{
	if (received < 0)
		received = 0;
	if (fired < 0)
		fired = 0;
	if (fired > received)
		fired = received;
	ledger->received = received;
	ledger->fired = fired;
	ledger->inFlight = 0;
}

static void AP_GrantLedgerBeginReplay(AP_GrantLedger *ledger)
{
	ledger->received = 0;
	ledger->fired = 0;
	ledger->inFlight = 0;
}

static void AP_GrantLedgerReceive(AP_GrantLedger *ledger,
	long long persistedFired)
{
	if (persistedFired < 0)
		persistedFired = 0;
	ledger->received++;
	/* Reconcile the replay prefix before exposing later, genuinely pending
	 * receipts. This is safe while the full list arrives in several batches. */
	if (ledger->fired < persistedFired)
		ledger->fired++;
}

static long long AP_GrantLedgerPending(const AP_GrantLedger *ledger)
{
	long long pending = ledger->received - ledger->fired -
	                    (ledger->inFlight != 0);
	return pending > 0 ? pending : 0;
}

static int AP_GrantCanDeliver(const AP_GrantLedger *ledger,
	int raceActive, int slotEmpty, int itemsanityActive, int turboOwned,
	int conflictingTransition)
{
	return AP_GrantLedgerPending(ledger) > 0 && raceActive && slotEmpty &&
	       !conflictingTransition && (!itemsanityActive || turboOwned);
}

static int AP_GrantMarkDelivered(AP_GrantLedger *ledger)
{
	if (ledger->inFlight || AP_GrantLedgerPending(ledger) <= 0)
		return 0;
	ledger->inFlight = 1;
	return 1;
}

static int AP_GrantMarkFired(AP_GrantLedger *ledger)
{
	if (!ledger->inFlight)
		return 0;
	ledger->inFlight = 0;
	if (ledger->fired < ledger->received)
		ledger->fired++;
	return 1;
}

static int AP_GrantObserveSlot(AP_GrantLedger *ledger, int stillHoldsGrant)
{
	if (!ledger->inFlight || stillHoldsGrant)
		return 0;
	ledger->inFlight = 0;
	return 1;
}

#endif
