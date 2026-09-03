// Out-of-engine assertions for the Lettersanity sent-item notification fix
// (issue #319). Compiles the REAL freestanding guards used by ap_hooks.c
// (ap_class_check_policy.h, plus the AP_LETTER_TOAST_SENT_ITEM flag in
// ap_lettersanity.h): both headers are self-contained, so this harness
// links nothing from the game.
//
//   cc -Wall -Wextra -o /tmp/test-lettersanity-remote-feed tools/test-lettersanity-remote-feed.c && /tmp/test-lettersanity-remote-feed
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Bug recap: AP_EmitClassCheck only calls AP_FeedOnLocationSent when its
// toastSentItem argument is enabled. Item Box checks enabled it; AP_LetterCollected
// passed 0, so a remote Lettersanity check never showed the "<ITEM> TO <PLAYER>"
// feed line. The fix flips that one argument to AP_LETTER_TOAST_SENT_ITEM (1);
// AP_FeedOnLocationSent's existing own-slot suppression (AP_LocationSentShouldToastPure)
// keeps the local-recipient case shown exactly once, through the received-item path.
//
// This harness does not call AP_EmitClassCheck or AP_FeedOnLocationSent directly
// (both live in ap_hooks.c, which pulls in the full decompiled engine and cannot
// be linked host-side). Instead SimulateLetterTouch mirrors their exact call
// sequence using the same pure guard predicates ap_hooks.c calls into, so a
// mutation to either predicate or to AP_LETTER_TOAST_SENT_ITEM changes this
// harness's outcome exactly as it would change the real build's.
//
// Coverage: remote recipient, local recipient, unknown scout / missing scout
// metadata, already-checked, repeated touch, reload/reconnect state,
// Lettersanity off, and exactly-once send/feed counts for each scenario.

#include <stdio.h>

#include "../ap/ap_class_check_policy.h"
#include "../ap/ap_lettersanity.h"

static int g_failures = 0;

#define CHECK(name, condition) do { \
	if (!(condition)) { \
		printf("FAIL %s\n", name); \
		g_failures++; \
	} else { \
		printf("ok   %s\n", name); \
	} \
} while (0)

typedef struct {
	int sendCalls;  // mirrors ap_net_send_location call count
	int toastCalls; // mirrors AP_FeedOnLocationSent call count
	long lastSentCode;
} Counters;

// Mirrors AP_LetterCollected -> AP_EmitClassCheck -> (conditionally)
// AP_FeedOnLocationSent for one letter touch. `alreadyChecked` models
// ap_net_location_checked(code): server-confirmed state, so it is what
// survives a reload or a reconnect, not merely in-memory session state.
// The caller marks it checked after a successful send, matching production
// (the server confirms the check and ap_net_location_checked reflects that
// from then on).
static int SimulateLetterTouch(long code, int *alreadyChecked,
                                int scoutKnown, int isOwnSlot, Counters *c)
{
	if (!AP_ClassCheckShouldSendPure(code, *alreadyChecked))
		return 0; // AP_EmitClassCheck's guard: nothing sent, nothing toasted

	c->sendCalls++;
	c->lastSentCode = code;
	*alreadyChecked = 1;

	if (AP_LETTER_TOAST_SENT_ITEM &&
	    AP_LocationSentShouldToastPure(scoutKnown, isOwnSlot))
		c->toastCalls++;

	return 1;
}

#define LETTER_CODE 35012503L // Roo's Tubes: C (tools/test-held-checks.cpp uses the same identity)

int main(void)
{
	// --- Direct guard truth tables -----------------------------------

	CHECK("send: valid code, not checked -> send",
	      AP_ClassCheckShouldSendPure(LETTER_CODE, 0) == 1);
	CHECK("send: valid code, already checked -> no send",
	      AP_ClassCheckShouldSendPure(LETTER_CODE, 1) == 0);
	CHECK("send: negative code (feature off), not checked -> no send",
	      AP_ClassCheckShouldSendPure(-1, 0) == 0);
	CHECK("send: negative code, already checked -> no send",
	      AP_ClassCheckShouldSendPure(-1, 1) == 0);

	CHECK("toast: scout known, remote recipient -> toast",
	      AP_LocationSentShouldToastPure(1, 0) == 1);
	CHECK("toast: scout known, own slot -> no toast (received path already showed it)",
	      AP_LocationSentShouldToastPure(1, 1) == 0);
	CHECK("toast: scout unknown, remote recipient -> no toast (cannot attribute)",
	      AP_LocationSentShouldToastPure(0, 0) == 0);
	CHECK("toast: scout unknown, own slot -> no toast",
	      AP_LocationSentShouldToastPure(0, 1) == 0);

	CHECK("letter toast flag is enabled (#319 fix)", AP_LETTER_TOAST_SENT_ITEM == 1);

	// --- Scenario 1: remote recipient ----------------------------------
	{
		Counters c = {0, 0, 0};
		int checked = 0;
		int newlySent = SimulateLetterTouch(LETTER_CODE, &checked, /*scoutKnown=*/1, /*isOwnSlot=*/0, &c);
		CHECK("remote recipient: touch is accepted as a new check", newlySent == 1);
		CHECK("remote recipient: location sent exactly once", c.sendCalls == 1);
		CHECK("remote recipient: sent-item feed queued exactly once", c.toastCalls == 1);
		CHECK("remote recipient: sent the right location code", c.lastSentCode == LETTER_CODE);
	}

	// --- Scenario 2: local recipient ------------------------------------
	{
		Counters c = {0, 0, 0};
		int checked = 0;
		int newlySent = SimulateLetterTouch(LETTER_CODE, &checked, /*scoutKnown=*/1, /*isOwnSlot=*/1, &c);
		CHECK("local recipient: touch is accepted as a new check", newlySent == 1);
		CHECK("local recipient: location sent exactly once", c.sendCalls == 1);
		CHECK("local recipient: NO sent-item feed line (avoid duplicating the received-item toast)",
		      c.toastCalls == 0);
	}

	// --- Scenario 3: unknown scout / missing metadata --------------------
	{
		Counters c = {0, 0, 0};
		int checked = 0;
		int newlySent = SimulateLetterTouch(LETTER_CODE, &checked, /*scoutKnown=*/0, /*isOwnSlot=*/0, &c);
		CHECK("unknown scout: the location check itself is still sent (not lost)", c.sendCalls == 1);
		CHECK("unknown scout: no feed line (cannot attribute an unscouted item)", c.toastCalls == 0);
		(void)newlySent;
	}

	// --- Scenario 4: already-checked (no re-fire) -------------------------
	{
		Counters c = {0, 0, 0};
		int checked = 1; // e.g. server already confirmed this check
		int newlySent = SimulateLetterTouch(LETTER_CODE, &checked, /*scoutKnown=*/1, /*isOwnSlot=*/0, &c);
		CHECK("already-checked: touch is rejected", newlySent == 0);
		CHECK("already-checked: no send", c.sendCalls == 0);
		CHECK("already-checked: no feed line", c.toastCalls == 0);
	}

	// --- Scenario 5: repeated touch (same session) ------------------------
	{
		Counters c = {0, 0, 0};
		int checked = 0;
		SimulateLetterTouch(LETTER_CODE, &checked, 1, 0, &c);
		SimulateLetterTouch(LETTER_CODE, &checked, 1, 0, &c); // re-touch the same letter object
		SimulateLetterTouch(LETTER_CODE, &checked, 1, 0, &c); // and again
		CHECK("repeated touch: location sent exactly once total", c.sendCalls == 1);
		CHECK("repeated touch: feed line queued exactly once total", c.toastCalls == 1);
	}

	// --- Scenario 6: reload / reconnect (server-confirmed state persists) --
	{
		Counters c = {0, 0, 0};
		// A reload or reconnect re-reads ap_net_location_checked from the
		// server's checked-locations set; this check was already settled
		// there, so it starts "checked" without this process ever having
		// sent it locally this session.
		int checkedAfterReconnect = 1;
		int newlySent = SimulateLetterTouch(LETTER_CODE, &checkedAfterReconnect, 1, 0, &c);
		CHECK("reload/reconnect: previously-settled letter does not resend", newlySent == 0);
		CHECK("reload/reconnect: no send", c.sendCalls == 0);
		CHECK("reload/reconnect: no feed line", c.toastCalls == 0);

		// A later, genuinely new letter on the same reconnected session still works.
		int freshChecked = 0;
		int freshSent = SimulateLetterTouch(LETTER_CODE + 1, &freshChecked, 1, 0, &c);
		CHECK("reload/reconnect: a genuinely new letter after reconnect still sends+toasts",
		      freshSent == 1 && c.sendCalls == 1 && c.toastCalls == 1);
	}

	// --- Scenario 7: Lettersanity off (feature flag off = no behavior change) --
	{
		// AP_LetterLocation returns -1 for every track/letter once Lettersanity
		// is inactive or the mode leaves this slot unselected; AP_EmitClassCheck's
		// code<0 guard fires before AP_LETTER_TOAST_SENT_ITEM is ever consulted,
		// so the #319 flag flip is provably inert here regardless of scout/slot.
		Counters remoteC = {0, 0, 0}, localC = {0, 0, 0};
		int checkedA = 0, checkedB = 0;
		int sentRemote = SimulateLetterTouch(-1, &checkedA, 1, 0, &remoteC);
		int sentLocal  = SimulateLetterTouch(-1, &checkedB, 1, 1, &localC);
		CHECK("Lettersanity off: no send (remote-shaped inputs)", sentRemote == 0 && remoteC.sendCalls == 0);
		CHECK("Lettersanity off: no feed line (remote-shaped inputs)", remoteC.toastCalls == 0);
		CHECK("Lettersanity off: no send (local-shaped inputs)", sentLocal == 0 && localC.sendCalls == 0);
		CHECK("Lettersanity off: no feed line (local-shaped inputs)", localC.toastCalls == 0);
	}

	printf("\n%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures != 0;
}
