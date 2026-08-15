// Host harness for #224's reconnect-safe Turbo Grant accounting.
//
// Build:
//   gcc -std=c99 -Wall -Wextra -Werror tools/test-turbo-grant.c -o /tmp/test-turbo-grant
//   /tmp/test-turbo-grant

#include <assert.h>
#include <stdio.h>

#include "../ap/ap_grants_logic.h"

static void test_delivery_truth_table(void)
{
	AP_GrantLedger ledger;
	AP_GrantLedgerRestore(&ledger, 1, 0);
	assert(!AP_GrantCanDeliver(&ledger, 0, 1, 0, 0, 0));
	assert(!AP_GrantCanDeliver(&ledger, 1, 0, 0, 0, 0));
	assert(!AP_GrantCanDeliver(&ledger, 1, 1, 1, 0, 0));
	assert(!AP_GrantCanDeliver(&ledger, 1, 1, 0, 0, 1));
	assert(AP_GrantCanDeliver(&ledger, 1, 1, 0, 0, 0));
	assert(AP_GrantCanDeliver(&ledger, 1, 1, 1, 1, 0));
}

static void test_delivery_is_not_consumption(void)
{
	AP_GrantLedger ledger;
	AP_GrantLedgerRestore(&ledger, 2, 0);
	assert(AP_GrantMarkDelivered(&ledger));
	assert(ledger.fired == 0);
	assert(AP_GrantLedgerPending(&ledger) == 1);
	assert(!AP_GrantMarkDelivered(&ledger));
	assert(AP_GrantMarkFired(&ledger));
	assert(ledger.fired == 1);
	assert(AP_GrantLedgerPending(&ledger) == 1);
}

static void test_death_or_restart_requeues_inflight(void)
{
	AP_GrantLedger ledger;
	AP_GrantLedgerRestore(&ledger, 1, 0);
	assert(AP_GrantMarkDelivered(&ledger));
	assert(AP_GrantObserveSlot(&ledger, 0));
	assert(ledger.fired == 0);
	assert(AP_GrantLedgerPending(&ledger) == 1);
	assert(AP_GrantCanDeliver(&ledger, 1, 1, 0, 0, 0));
}

static void test_reconnect_rebuild_and_duplicate_counts(void)
{
	AP_GrantLedger ledger;
	AP_GrantLedgerRestore(&ledger, 3, 1);
	assert(AP_GrantLedgerPending(&ledger) == 2);
	assert(AP_GrantMarkDelivered(&ledger));
	assert(AP_GrantMarkFired(&ledger));
	assert(ledger.fired == 2);

	/* Full ReceivedItems replay reconstructs the same three receipts. */
	AP_GrantLedgerRestore(&ledger, 3, 2);
	assert(AP_GrantLedgerPending(&ledger) == 1);

	/* A genuine duplicate grant is another server-list entry and is preserved. */
	AP_GrantLedgerRestore(&ledger, 4, 2);
	assert(AP_GrantLedgerPending(&ledger) == 2);
}

static void test_batched_replay_hides_already_fired_prefix(void)
{
	AP_GrantLedger ledger;
	AP_GrantLedgerBeginReplay(&ledger);
	AP_GrantLedgerReceive(&ledger, 2);
	assert(ledger.received == 1 && ledger.fired == 1);
	assert(AP_GrantLedgerPending(&ledger) == 0);
	AP_GrantLedgerReceive(&ledger, 2);
	assert(ledger.received == 2 && ledger.fired == 2);
	assert(AP_GrantLedgerPending(&ledger) == 0);
	AP_GrantLedgerReceive(&ledger, 2);
	assert(AP_GrantLedgerPending(&ledger) == 1);
}

static void test_slot_identity_load_does_not_leak(void)
{
	AP_GrantLedger ledger;
	AP_GrantLedgerRestore(&ledger, 5, 4);
	assert(AP_GrantLedgerPending(&ledger) == 1);

	/* Switching identities loads that row's own totals and clears in-flight. */
	AP_GrantLedgerRestore(&ledger, 1, 0);
	assert(ledger.received == 1);
	assert(ledger.fired == 0);
	assert(!ledger.inFlight);
	assert(AP_GrantLedgerPending(&ledger) == 1);
}

static void test_hostile_persisted_count_is_clamped(void)
{
	AP_GrantLedger ledger;
	AP_GrantLedgerRestore(&ledger, -3, -8);
	assert(ledger.received == 0 && ledger.fired == 0);
	AP_GrantLedgerRestore(&ledger, 2, 99);
	assert(ledger.received == 2 && ledger.fired == 2);
	assert(AP_GrantLedgerPending(&ledger) == 0);
}

int main(void)
{
	test_delivery_truth_table();
	test_delivery_is_not_consumption();
	test_death_or_restart_requeues_inflight();
	test_reconnect_rebuild_and_duplicate_counts();
	test_batched_replay_hides_already_fired_prefix();
	test_slot_identity_load_does_not_leak();
	test_hostile_persisted_count_is_clamped();
	puts("Turbo Grant accounting: PASS");
	return 0;
}
