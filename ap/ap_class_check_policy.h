#ifndef AP_CLASS_CHECK_POLICY_H
#define AP_CLASS_CHECK_POLICY_H

// Freestanding guard decisions shared by AP_EmitClassCheck (the common
// location-check gate used by podium, item-box, itemsanity, relic-perfect,
// custom-track-trophy and Lettersanity checks) and AP_FeedOnLocationSent (the
// nonlocal sent-item toast). Pulled out of ap_hooks.c so a host harness can
// pin these guards without linking the engine (#319). Extraction only: both
// predicates reproduce the exact conditions the two functions already
// enforced inline; no location class's behavior changes by this move.

// A location check should be sent exactly once: only when this seed actually
// carries a code for it (a negative code means the location is absent this
// seed, e.g. a feature-off Lettersanity slot) and it has not already been
// checked (a fresh touch, a reload, and a reconnect all read the same
// server-confirmed checked-locations state, so all three dedupe here).
static inline int AP_ClassCheckShouldSendPure(long code, int alreadyChecked)
{
	if (code < 0) return 0; // location absent this seed
	if (alreadyChecked) return 0; // already checked (re-touch / reload / reconnect)
	return 1;
}

// A location we just sent should toast "<ITEM> TO <PLAYER>" to the hub feed
// only when the scouted destination is actually known (otherwise the item
// cannot be attributed, so stay silent rather than guess) and the recipient
// is NOT our own slot: an own-slot send is echoed back through ReceivedItems,
// and AP_FeedOnItemReceived already toasts that receipt, so toasting here too
// would double the line.
static inline int AP_LocationSentShouldToastPure(int scoutKnown, int isOwnSlot)
{
	if (!scoutKnown) return 0; // cannot attribute an unscouted destination
	if (isOwnSlot) return 0;   // own-slot receipt echo already toasts this
	return 1;
}

#endif
