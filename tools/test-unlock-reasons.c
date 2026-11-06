#include <stdio.h>
#include <string.h>

#include "../ap/ap_unlock_reasons_logic.h"

static int failures;

static void expect(const char *name, int got, int want)
{
	if (got != want)
	{
		printf("FAIL %-46s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

static void expect_str(const char *name, const char *got, const char *want)
{
	if (strcmp(got, want) != 0)
	{
		printf("FAIL %-46s got=\"%s\" want=\"%s\"\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

int main(void)
{
	AP_UnlockWatch a, b;
	AP_UnlockEvent ev[64];
	char line[96];
	int n;

	// ── pad slot mapping ──
	expect("slot of pad 0", AP_UnlockPadSlot(0), 0);
	expect("slot of pad 15", AP_UnlockPadSlot(15), 15);
	expect("slot of pad 19", AP_UnlockPadSlot(19), 19);
	expect("slot of pad 21", AP_UnlockPadSlot(21), 20);
	expect("slot of pad 23", AP_UnlockPadSlot(23), 21);
	expect("slot of cup 100", AP_UnlockPadSlot(100), 22);
	expect("slot of cup 104", AP_UnlockPadSlot(104), 26);
	expect("hub level 25 is no pad", AP_UnlockPadSlot(25), -1);
	expect("level 20 is no pad", AP_UnlockPadSlot(20), -1);
	expect("level 22 is no pad", AP_UnlockPadSlot(22), -1);
	expect("negative is no pad", AP_UnlockPadSlot(-1), -1);
	expect("garage cup pad 1000 is no pad", AP_UnlockPadSlot(1000), -1);
	expect("slot 0 back to pad", AP_UnlockSlotPad(0), 0);
	expect("slot 20 back to pad 21", AP_UnlockSlotPad(20), 21);
	expect("slot 21 back to pad 23", AP_UnlockSlotPad(21), 23);
	expect("slot 22 back to cup 100", AP_UnlockSlotPad(22), 100);
	expect("slot 26 back to cup 104", AP_UnlockSlotPad(26), 104);
	expect("slot 27 out of range", AP_UnlockSlotPad(27), -1);
	expect("slot -1 out of range", AP_UnlockSlotPad(-1), -1);

	// ── diff: nothing opened ──
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	a.shopKnown = 1;
	b.shopKnown = 1;
	expect("no change, no events", AP_UnlockDiff(&a, &b, ev, 64), 0);

	// closing something is not an opening (defensive; counts never fall live)
	b.padOpen = 0;
	a.padOpen = 1u << 3;
	expect("a closed pad is no event", AP_UnlockDiff(&a, &b, ev, 64), 0);

	// ── diff: one event per kind ──
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	a.shopKnown = 1;
	b.shopKnown = 1;
	b.padOpen = 1u << 3; // Crash Cove slot
	n = AP_UnlockDiff(&a, &b, ev, 64);
	expect("one pad event", n, 1);
	expect("pad event kind", ev[0].kind, AP_UNLOCK_PAD);
	expect("pad event index", ev[0].index, 3);

	memset(&b, 0, sizeof b);
	b.shopKnown = 1;
	b.padStage2 = 1u << 0;
	n = AP_UnlockDiff(&a, &b, ev, 64);
	expect("one stage 2 event", n, 1);
	expect("stage 2 event kind", ev[0].kind, AP_UNLOCK_STAGE2);
	expect("stage 2 event index", ev[0].index, 0);

	memset(&b, 0, sizeof b);
	b.shopKnown = 1;
	b.doorsReady = 1u << 1;
	n = AP_UnlockDiff(&a, &b, ev, 64);
	expect("one door event", n, 1);
	expect("door event kind", ev[0].kind, AP_UNLOCK_DOOR);
	expect("door event index", ev[0].index, 1);

	memset(&b, 0, sizeof b);
	b.shopKnown = 1;
	b.garagesOpen = 1u << 4;
	n = AP_UnlockDiff(&a, &b, ev, 64);
	expect("one garage event", n, 1);
	expect("garage event kind", ev[0].kind, AP_UNLOCK_GARAGE);
	expect("garage event index is Oxide", ev[0].index, 4);

	memset(&b, 0, sizeof b);
	b.shopKnown = 1;
	b.shopOpen[2] = 3;
	n = AP_UnlockDiff(&a, &b, ev, 64);
	expect("one shop event", n, 1);
	expect("shop event kind", ev[0].kind, AP_UNLOCK_SHOP);
	expect("shop event hub", ev[0].index, 2);

	// ── diff: priority order, one item opening many things ──
	memset(&b, 0, sizeof b);
	b.shopKnown = 1;
	b.garagesOpen = 1u << 0;
	b.doorsReady = 1u << 0;
	b.padOpen = (1u << 2) | (1u << 5);
	b.padStage2 = 1u << 7;
	b.shopOpen[4] = 1;
	n = AP_UnlockDiff(&a, &b, ev, 64);
	expect("six openings from one item", n, 6);
	expect("garage first", ev[0].kind, AP_UNLOCK_GARAGE);
	expect("door second", ev[1].kind, AP_UNLOCK_DOOR);
	expect("pads third", ev[2].kind, AP_UNLOCK_PAD);
	expect("pads in slot order", ev[2].index, 2);
	expect("second pad slot", ev[3].index, 5);
	expect("stage 2 fourth", ev[4].kind, AP_UNLOCK_STAGE2);
	expect("shop last", ev[5].kind, AP_UNLOCK_SHOP);

	// already-open stays quiet on the next diff
	n = AP_UnlockDiff(&b, &b, ev, 64);
	expect("same snapshot, no events", n, 0);

	// ── diff: shop stays silent until the lifetime storage is known ──
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	b.shopOpen[0] = 4;
	expect("unknown -> known baseline is silent", AP_UnlockDiff(&a, &b, ev, 64), 0);
	a.shopKnown = 1;
	a.shopOpen[0] = 4;
	expect("known -> unknown is silent", AP_UnlockDiff(&a, &b, ev, 64), 0);
	b.shopKnown = 1;
	expect("known counts diff", AP_UnlockDiff(&a, &b, ev, 64), 0);

	// ── diff: event cap ──
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	b.shopKnown = 1;
	a.shopKnown = 1;
	b.garagesOpen = 0x1F;
	b.doorsReady = 0xF;
	n = AP_UnlockDiff(&a, &b, ev, 3);
	expect("cap respected", n, 3);
	expect("cap keeps priority order", ev[2].kind, AP_UNLOCK_GARAGE);

	// ── wording: one line per kind ──
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_PAD, "Crash Cove", 0);
	expect_str("pad line", line, "Crash Cove OPEN");
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_STAGE2, "Papu's Pyramid", 0);
	expect_str("stage 2 line", line, "Papu's Pyramid STAGE 2 OPEN");
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_DOOR, "Glacier Park", 0);
	expect_str("door line", line, "Glacier Park DOOR OPEN");
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_GARAGE, "Ripper Roo", 0);
	expect_str("garage line", line, "Ripper Roo GARAGE OPEN");
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_GARAGE, "Oxide", 0);
	expect_str("oxide garage line", line, "Oxide GARAGE OPEN");
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_SHOP, "The Lost Ruins", 0);
	expect_str("shop line", line, "The Lost Ruins SHOP STOCK OPEN");

	// the one-line-per-item summary of further openings
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_PAD, "Crash Cove", 1);
	expect_str("one more opening", line, "Crash Cove OPEN (+1 MORE)");
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_PAD, "Crash Cove", 3);
	expect_str("three more openings", line, "Crash Cove OPEN (+3 MORE)");

	// long custom-track names are cut so the suffix always fits
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_PAD,
	                    "30thAnniversaryAdventureSlot7: CNK Challenge", 0);
	expect_str("long name cut at 24", line, "30thAnniversaryAdventure OPEN");
	AP_UnlockReasonLine(line, (int)sizeof line, AP_UNLOCK_PAD, 0, 0);
	expect_str("missing name", line, "? OPEN");
	AP_UnlockReasonLine(line, (int)sizeof line, 99, "Odd Kind", 0);
	expect_str("unknown kind falls back", line, "Odd Kind OPEN");

	printf("%s: 62 checks\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
