#ifndef AP_LETTERSANITY_H
#define AP_LETTERSANITY_H

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

#endif
