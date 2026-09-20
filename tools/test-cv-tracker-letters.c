// Out-of-engine assertions for the adventure-map tracker letter cells (#379).
//
// Players reported Cortex Vortex showing C, T and R as available with one letter
// received. The Cortex Vortex row had a gold/grey rule of its own that never read
// the received state, because AP_LetterAvailable only recognises that track while
// its race is the loaded level, and the tracker is drawn from the hub.
//
// Both rows now decide through AP_TrackerLetterCellPure over
// AP_LetterAvailablePure: ordinary tracks through AP_LetterAvailable, Cortex
// Vortex through AP_CortexLetterAvailableForTracker. Both read the same
// lettersanity mode; only the location codes and the received table differ. This
// harness mirrors those two feeds against separate stores and pins them to the
// same drawing decision for the same inputs.
//
//   cc -Wall -Wextra -o /tmp/test-cv-tracker-letters tools/test-cv-tracker-letters.c && /tmp/test-cv-tracker-letters

#include <stdio.h>
#include "../ap/ap_lettersanity.h"

static int failures;
static int checks;
#define EXPECT(expr, why) do { int ok = !!(expr); checks++; if (!ok) { printf("FAIL %s\n", why); failures++; } } while (0)

// Stand-ins for the two stores the engine reads.
static int seed_mode;                          // ctr_cfg.lettersanity_mode
static long retail_codes[3];                   // ctr_cfg.lettersanity_locations[track]
static unsigned char retail_received[3];       // ap_letter_received[track]
static long cortex_codes[3];                   // ctr_cfg.cortex_track.letters
static unsigned char cortex_received[3];       // ap_cortex_letter_received
static int location_exists[3], location_checked[3];

// AP_TrackerCodeState.
static int CodeState(long code, int letter)
{
	if (code < 0 || !location_exists[letter]) return 0;
	return location_checked[letter] ? 2 : 1;
}

// AP_LetterAvailable, retail branch.
static int RetailAvailable(int letter)
{
	return AP_LetterAvailablePure(1, seed_mode, retail_codes[letter], retail_received[letter]);
}

// AP_CortexLetterAvailableForTracker.
static int CortexAvailable(int letter)
{
	return AP_LetterAvailablePure(1, seed_mode, cortex_codes[letter], cortex_received[letter]);
}

static int SameCell(AP_TrackerLetterCell a, AP_TrackerLetterCell b)
{
	return a.letter == b.letter && a.gold == b.gold && a.tick == b.tick && a.locked == b.locked;
}

int main(void)
{
	static const int receipt_masks[3] = {0x0, 0x1, 0x7}; // none, C only, all three
	int state, code_missing, m, letter;
	int matrix = 0;

	for (seed_mode = 0; seed_mode <= 3; seed_mode++)
	for (state = 0; state <= 2; state++)
	for (code_missing = 0; code_missing <= 1; code_missing++)
	for (m = 0; m < 3; m++)
	{
		if (code_missing && state > 0) continue; // an absent letter has no location state
		for (letter = 0; letter < 3; letter++)
		{
			// Deliberately different wire codes per row: the seed's Cortex Vortex
			// letters are their own locations, not Oxide Station's.
			retail_codes[letter] = code_missing ? -1 : 35012500L + letter;
			cortex_codes[letter] = code_missing ? -1 : 35019900L + letter;
			retail_received[letter] = (unsigned char)((receipt_masks[m] >> letter) & 1);
			cortex_received[letter] = retail_received[letter];
			location_exists[letter] = state > 0;
			location_checked[letter] = state == 2;
		}
		for (letter = 0; letter < 3; letter++)
		{
			AP_TrackerLetterCell ordinary = AP_TrackerLetterCellPure(
				seed_mode, CodeState(retail_codes[letter], letter), RetailAvailable(letter));
			AP_TrackerLetterCell cortex = AP_TrackerLetterCellPure(
				seed_mode, CodeState(cortex_codes[letter], letter), CortexAvailable(letter));
			int received = retail_received[letter];
			EXPECT(SameCell(ordinary, cortex), "Cortex Vortex cell matches the ordinary cell");
			matrix++;

			// The shared rule, spelled out per mode.
			if (seed_mode < 2)
			{
				EXPECT(ordinary.letter == (state > 0), "modes 0 and 1 show a letter only where its location exists");
				EXPECT(ordinary.gold == (state > 0), "modes 0 and 1 have no item gate, so a shown letter is gold");
				EXPECT(ordinary.locked == 0, "modes 0 and 1 never draw the needed marker");
			}
			else if (seed_mode == 2)
			{
				EXPECT(ordinary.letter == (state > 0), "mode 2 shows a letter only where its location exists");
				EXPECT(ordinary.gold == (state > 0 && !code_missing && received),
				       "mode 2 golds a selected letter only once its item is received");
			}
			else
			{
				EXPECT(ordinary.letter == 1, "mode 3 shows every letter, location or not");
				EXPECT(ordinary.gold == (received != 0), "mode 3 golds a letter only once its item is received");
			}
			EXPECT(ordinary.tick == (state == 2), "the collected tick follows the checked location");
			EXPECT(ordinary.locked == (ordinary.letter && !ordinary.tick && !ordinary.gold),
			       "the needed marker fills in for a shown, unchecked, unavailable letter");
		}
	}
	printf("ok %d cell comparisons over mode x location state x selection x receipts x letter\n", matrix);

	// The reported regression: letter items on, only C received, each letter's
	// location present and unchecked. Only C may be gold.
	for (seed_mode = 2; seed_mode <= 3; seed_mode++)
	{
		for (letter = 0; letter < 3; letter++)
		{
			cortex_codes[letter] = 35019900L + letter;
			cortex_received[letter] = (unsigned char)(letter == 0);
			location_exists[letter] = 1;
			location_checked[letter] = 0;
		}
		for (letter = 0; letter < 3; letter++)
		{
			AP_TrackerLetterCell cell = AP_TrackerLetterCellPure(
				seed_mode, CodeState(cortex_codes[letter], letter), CortexAvailable(letter));
			EXPECT(cell.gold == (letter == 0), "Cortex Vortex golds only the received letter");
			EXPECT(cell.letter == 1, "Cortex Vortex still shows the letter it has not received");
			EXPECT(cell.locked == (letter != 0), "Cortex Vortex marks an unreceived letter as needed");
			EXPECT(cell.tick == 0, "an unchecked Cortex Vortex letter has no collected tick");
		}
	}

	printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "ok", checks, failures);
	return failures != 0;
}
