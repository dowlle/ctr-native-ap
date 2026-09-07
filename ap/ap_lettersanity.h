#ifndef AP_LETTERSANITY_H
#define AP_LETTERSANITY_H

// Letter items are frozen in the apworld's canonical token-track order, while
// every live pickup hook addresses a track by CTR's levelID. Keep the conversion
// in one pure table so receipt state and pickup state cannot silently use two
// different identity spaces (#278).
static inline int AP_LetterItemRowToLevelIDPure(int row)
{
	static const unsigned char levelID[16] = {
		3, 6, 9, 8, 14, 4, 5, 0, 2, 1, 12, 15, 7, 10, 11, 13
	};
	return (row >= 0 && row < 16) ? (int)levelID[row] : -1;
}

// Freestanding Lettersanity decisions shared by the engine hooks and the
// out-of-engine harness. Modes: 0 off, 1 locations only, 2 locations and
// items, 3 items only. A nonnegative location code marks a selected letter.

static inline int AP_LetterIsRequiredPure(int active, int mode, long code)
{
	if (!active || mode < 2) return 0;
	return mode == 3 || code >= 0;
}

static inline int AP_LetterAvailablePure(int active, int mode, long code, int received)
{
	if (!active || mode < 2) return 1;
	if (mode == 2 && code < 0) return 0;
	return received != 0;
}

static inline int AP_LettersRequiredCountPure(int active, int mode, const long codes[3])
{
	int letter, count = 0;
	if (!active || mode < 2) return 3;
	if (mode == 3) return 3;
	for (letter = 0; letter < 3; letter++)
		if (codes[letter] >= 0) count++;
	return count;
}

static inline int AP_LettersRequiredMetPure(int active, int mode,
	                                         const long codes[3],
	                                         const unsigned char received[3])
{
	int letter;
	if (!active || mode < 2) return 1;
	for (letter = 0; letter < 3; letter++)
		if (AP_LetterIsRequiredPure(active, mode, codes[letter]) && !received[letter])
			return 0;
	return 1;
}

static inline int AP_LetterTokenEarnedPure(int didWin, int collected,
	                                        int active, int mode,
	                                        const long codes[3],
	                                        const unsigned char received[3])
{
	return didWin && collected == AP_LettersRequiredCountPure(active, mode, codes) &&
	       AP_LettersRequiredMetPure(active, mode, codes, received);
}

// AP_LetterCollected's AP_EmitClassCheck toastSentItem argument (#319): a
// newly collected letter must queue the same "<ITEM> TO <PLAYER>" sent-item
// feed line as any other class when the scouted item belongs to another
// slot. Named (not a bare literal) so the single source of truth is visible
// at the call site and mutation-testable from tools/test-lettersanity-remote-feed.c.
// AP_LocationSentShouldToastPure (ap_class_check_policy.h) still suppresses
// the toast for a local recipient, since the ReceivedItems echo already
// shows those once through AP_FeedOnItemReceived.
#define AP_LETTER_TOAST_SENT_ITEM 1

#endif
