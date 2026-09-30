// Warp pad glow shows every open location (ruling of 2026-09-30): the display
// identities and gathers for item boxes, CTR letters and per-track Wumpa.
//
//   cc -Wall -Wextra -Werror -DCTR_AP -I ap -I . -I include
//     -o /tmp/test-pad-glow-items tools/test-pad-glow-items.c
//
// Exit 0 = every assertion held.
#include <stdio.h>
#include <string.h>

#define AP_PAD_DISPLAY_BITS_MAX 96 /* mirrors ap_hooks.h */
#include "ap_pad_glow_items.h"
#include "ap_glow_slots_logic.h"
#include "ap_relic_perfect.h"
#include "ap_trial_pad_glow.h"
#include "ap_cortex_track.h"

static int checks;
static int failures;

static void expect(long got, long want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		printf("FAIL %s: got %ld want %ld\n", name, got, want);
	}
}

// Fake server: a set of live codes and a set of checked codes.
static char live[64000], done[64000];
#define OFF 35000000L
static int isLive(long c, void *x) { (void)x; return c >= OFF && c < OFF + 64000 && live[c - OFF]; }
static int isDone(long c, void *x) { (void)x; return c >= OFF && c < OFF + 64000 && done[c - OFF]; }
static void setLive(long c) { live[c - OFF] = 1; }
static void setDone(long c) { done[c - OFF] = 1; }
static void reset(void) { memset(live, 0, sizeof live); memset(done, 0, sizeof done); }

static int unique(const int *b, int n)
{
	int i, j;
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			if (b[i] == b[j])
				return 0;
	return 1;
}

static void test_ranges_and_codes(void)
{
	static ctr_seed_config cfg;
	int code, level, letter, bit;

	// The three ranges sit above every existing pseudo-bit family and do not touch.
	expect(AP_PADGLOW_BOX_BASE >= AP_RELIC_PERFECT_PSEUDO_BASE + AP_RELIC_PERFECT_TRACK_COUNT, 1,
	       "boxes above perfect range");
	expect(AP_PADGLOW_BOX_BASE > AP_CV_PSEUDO_BASE + AP_CV_SLOT_COUNT, 1, "boxes above cortex range");
	expect(AP_PADGLOW_BOX_BASE > AP_TRIAL_PSEUDO_BASE + 4, 1, "boxes above trial range");
	expect(AP_PADGLOW_BOX_BASE + AP_BOX_LOCATION_COUNT <= AP_PADGLOW_LETTER_BASE, 1, "boxes end before letters");
	expect(AP_PADGLOW_LETTER_BASE + CTR_CFG_LETTER_TRACK_COUNT * CTR_CFG_LETTER_COUNT <= AP_PADGLOW_WUMPA_BASE, 1,
	       "letters end before Wumpa");

	for (code = 0; code < AP_BOX_LOCATION_COUNT; code++)
	{
		long wire = AP_BOX_CODE_BASE + code;
		bit = AP_PadGlowBoxBit(wire);
		expect(AP_PadGlowBoxCode(bit), wire, "box bit round trip");
		expect(AP_PadGlowItemRewardGroup(bit), 0, "box in race slot");
		expect(AP_PadGlowIsItemBit(bit), 1, "box is item bit");
	}
	expect(AP_PadGlowBoxBit(AP_BOX_CODE_BASE - 1), -1, "box below range");
	expect(AP_PadGlowBoxBit(AP_BOX_CODE_BASE + AP_BOX_LOCATION_COUNT), -1, "box above range");
	expect(AP_PadGlowBoxCode(AP_PADGLOW_BOX_BASE - 1), -1, "no code below box range");

	for (level = 0; level < CTR_CFG_LETTER_TRACK_COUNT; level++)
		for (letter = 0; letter < CTR_CFG_LETTER_COUNT; letter++)
		{
			int l2, c2;
			cfg.lettersanity_locations[level][letter] = 35013000L + level * 3 + letter;
			bit = AP_PadGlowLetterBit(level, letter);
			expect(AP_PadGlowLetterDecode(bit, &l2, &c2), 1, "letter decodes");
			expect(l2 * 10 + c2, level * 10 + letter, "letter round trip");
			expect(AP_PadGlowItemCode(&cfg, bit), 35013000L + level * 3 + letter, "letter code");
			expect(AP_PadGlowItemRewardGroup(bit), 2, "letter in token slot");
		}
	for (level = 0; level < CTR_CFG_WUMPA_TRACK_COUNT; level++)
	{
		cfg.wumpa.tracks[level] = 35016100L + level;
		bit = AP_PadGlowWumpaBit(level);
		expect(AP_PadGlowWumpaLevel(bit), level, "wumpa round trip");
		expect(AP_PadGlowItemCode(&cfg, bit), 35016100L + level, "wumpa code");
		expect(AP_PadGlowItemRewardGroup(bit), 0, "wumpa in race slot");
	}
	cfg.wumpa.tracks[3] = -1;
	expect(AP_PadGlowItemCode(&cfg, AP_PadGlowWumpaBit(3)), -1, "absent wumpa row is no code");
	expect(AP_PadGlowItemCode(0, AP_PadGlowWumpaBit(3)), -1, "no cfg, no wumpa code");
	// Not item bits: podium, trial, cortex, perfect and real AdvProgress bits.
	expect(AP_PadGlowIsItemBit(0x100), 0, "podium is not an item bit");
	expect(AP_PadGlowIsItemBit(0x1ef), 0, "last podium is not an item bit");
	expect(AP_PadGlowIsItemBit(AP_TrialPseudoBit(1, 1)), 0, "trial is not an item bit");
	expect(AP_PadGlowIsItemBit(AP_CV_PSEUDO_BASE), 0, "cortex is not an item bit");
	expect(AP_PadGlowIsItemBit(AP_RelicPerfectPseudoBit(17)), 0, "perfect is not an item bit");
	expect(AP_PadGlowIsItemBit(0x4c + 5), 0, "adv bit is not an item bit");
	expect(AP_PadGlowItemRewardGroup(0x4c), -1, "adv bit has no item group");
	// Cortex Vortex Wumpa follows the race slot; letters keep the token slot.
	expect(AP_CortexPseudoRewardGroup(AP_CortexPseudoBit(AP_CV_SLOT_WUMPA)), 0, "cortex wumpa race slot");
	expect(AP_CortexPseudoRewardGroup(AP_CortexPseudoBit(AP_CV_SLOT_LETTER0 + 1)), 2, "cortex letter token slot");
}

// Same numbers as AP_BoxMap_CountStanding: the gather lists exactly the boxes the count sees.
static void test_boxes_match_count(void)
{
	int level, slots, slot, out[32], n;

	for (level = 0; level < AP_BOX_TRACK_COUNT; level++)
		for (slots = 0; slots <= AP_BOX_SLOTS_PER_TRACK + 3; slots += 3)
		{
			reset();
			for (slot = 0; slot < AP_BOX_SLOTS_PER_TRACK; slot++)
			{
				long code = AP_BoxMap_Code(level, slot);
				if (slot % 4 != 3)
					setLive(code);
				if (slot % 5 == 0)
					setDone(code);
			}
			n = AP_PadGlowAppendBoxes(level, slots, out, 32, 0, isLive, isDone, 0);
			expect(n, AP_BoxMap_CountStanding(level, slots, isLive, isDone, 0), "box gather == count");
			expect(unique(out, n), 1, "box gather unique");
		}
	// Levels that are not box tracks list nothing.
	reset();
	n = AP_PadGlowAppendBoxes(40, 15, out, 32, 0, isLive, isDone, 0);
	expect(n, 0, "non-box level lists nothing");
}

static void test_letters_and_wumpa(void)
{
	long row[3] = {35013000L, -1, 35013002L};
	int out[8], n;

	reset();
	setLive(35013000L);
	setLive(35013002L);
	n = AP_PadGlowAppendLetters(row, 4, out, 8, 0, isLive, isDone, 0);
	expect(n, 2, "two live letters listed");
	expect(out[0], AP_PadGlowLetterBit(4, 0), "first letter bit");
	expect(out[1], AP_PadGlowLetterBit(4, 2), "third letter bit, absent one skipped");
	setDone(35013000L);
	n = AP_PadGlowAppendLetters(row, 4, out, 8, 0, isLive, isDone, 0);
	expect(n, 1, "checked letter drops out");
	n = AP_PadGlowAppendLetters(row, 18, out, 8, 0, isLive, isDone, 0);
	expect(n, 0, "no letters past the letter tracks");

	setLive(35016105L);
	n = AP_PadGlowAppendWumpa(35016105L, 5, out, 8, 0, isLive, isDone, 0);
	expect(n, 1, "open wumpa listed");
	expect(out[0], AP_PadGlowWumpaBit(5), "wumpa bit");
	setDone(35016105L);
	expect(AP_PadGlowAppendWumpa(35016105L, 5, out, 8, 0, isLive, isDone, 0), 0, "checked wumpa drops out");
	expect(AP_PadGlowAppendWumpa(-1, 5, out, 8, 0, isLive, isDone, 0), 0, "absent wumpa row");
}

// Worst cases: everything live and open. Proves the buffer size.
static void test_worst_case_fits(void)
{
	int buf[AP_PAD_DISPLAY_BITS_MAX];
	long row[3] = {35013000L, 35013001L, 35013002L};
	int n, leg, slot, level;

	// Busiest single pad: 5 tiers + perfect + 2 trial checks + 5 rungs, then the
	// item families. (Tiers, perfect, trial and rungs are appended by the
	// production enumerator; here they are represented by their count.)
	reset();
	for (slot = 0; slot < AP_BOX_SLOTS_PER_TRACK; slot++)
		setLive(AP_BoxMap_Code(16, slot));
	for (slot = 0; slot < 3; slot++)
		setLive(row[slot]);
	setLive(35016116L);
	n = 5 + 1 + 2 + CTR_CFG_PODIUM_RUNG_COUNT;
	n = AP_PadGlowAppendBoxes(16, AP_BOX_SLOTS_PER_TRACK, buf, AP_PAD_DISPLAY_BITS_MAX, n, isLive, isDone, 0);
	n = AP_PadGlowAppendLetters(row, 16, buf, AP_PAD_DISPLAY_BITS_MAX, n, isLive, isDone, 0);
	n = AP_PadGlowAppendWumpa(35016116L, 16, buf, AP_PAD_DISPLAY_BITS_MAX, n, isLive, isDone, 0);
	expect(n, 5 + 1 + 2 + 5 + 15 + 3 + 1, "busiest single pad is 32 bits");
	expect(n <= AP_PAD_DISPLAY_BITS_MAX, 1, "single pad fits the display buffer");

	// Gem cup with four distinct legs: gem + per leg 5 rungs + 15 boxes + Wumpa.
	reset();
	n = 1;
	for (leg = 0; leg < 4; leg++)
	{
		level = leg * 3; // four distinct box tracks
		for (slot = 0; slot < AP_BOX_SLOTS_PER_TRACK; slot++)
			setLive(AP_BoxMap_Code(level, slot));
		setLive(35016100L + level);
		n += CTR_CFG_PODIUM_RUNG_COUNT;
		n = AP_PadGlowAppendBoxes(level, AP_BOX_SLOTS_PER_TRACK, buf, AP_PAD_DISPLAY_BITS_MAX, n, isLive, isDone, 0);
		n = AP_PadGlowAppendWumpa(35016100L + level, level, buf, AP_PAD_DISPLAY_BITS_MAX, n, isLive, isDone, 0);
	}
	expect(n, 1 + 4 * (5 + 15 + 1), "four-leg cup is 85 bits");
	expect(n, AP_PADGLOW_WORST_CASE_BITS, "cup worst case equals the documented bound");
	expect(n <= AP_PAD_DISPLAY_BITS_MAX, 1, "cup fits the display buffer");
}

// A long list still shows every entry: one pile reaches every bit, by_reward_type
// puts boxes and Wumpa in the race slot and letters in the token slot.
static int groupFn(int bit)
{
	int g = AP_PadGlowItemRewardGroup(bit);
	return g >= 0 ? g : 1;
}

static void test_cycles(void)
{
	int bits[AP_PAD_DISPLAY_BITS_MAX];
	int n = 0, slot, i, phase, seen[AP_PAD_DISPLAY_BITS_MAX], out[3];
	long row[3] = {35013000L, 35013001L, 35013002L};
	int relicBit = 0x16 + 3;

	reset();
	for (slot = 0; slot < 15; slot++)
		setLive(AP_BoxMap_Code(3, slot));
	for (slot = 0; slot < 3; slot++)
		setLive(row[slot]);
	setLive(35016103L);
	bits[n++] = relicBit;
	n = AP_PadGlowAppendBoxes(3, 15, bits, AP_PAD_DISPLAY_BITS_MAX, n, isLive, isDone, 0);
	n = AP_PadGlowAppendLetters(row, 3, bits, AP_PAD_DISPLAY_BITS_MAX, n, isLive, isDone, 0);
	n = AP_PadGlowAppendWumpa(35016103L, 3, bits, AP_PAD_DISPLAY_BITS_MAX, n, isLive, isDone, 0);
	expect(n, 20, "relic + 15 boxes + 3 letters + wumpa");

	// One pile: every entry appears within n phases, three per window.
	memset(seen, 0, sizeof seen);
	for (phase = 0; phase < n; phase++)
	{
		AP_GlowSlots_Select(bits, n, phase, 0, groupFn, out);
		for (i = 0; i < 3; i++)
			for (slot = 0; slot < n; slot++)
				if (out[i] == bits[slot])
					seen[slot] = 1;
	}
	for (slot = 0; slot < n; slot++)
		expect(seen[slot], 1, "one pile reaches every entry");

	// by_reward_type: race slot cycles boxes + wumpa only, relic slot the relic,
	// token slot only letters, and every entry of each group shows up.
	memset(seen, 0, sizeof seen);
	for (phase = 0; phase < 40; phase++)
	{
		AP_GlowSlots_Select(bits, n, phase, 1, groupFn, out);
		expect(out[1], relicBit, "relic slot keeps the relic");
		expect(AP_PadGlowBoxCode(out[0]) >= 0 || AP_PadGlowWumpaLevel(out[0]) >= 0, 1,
		       "race slot shows a box or wumpa");
		expect(AP_PadGlowLetterDecode(out[2], 0, 0), 1, "token slot shows a letter");
		for (slot = 0; slot < n; slot++)
			if (out[0] == bits[slot] || out[2] == bits[slot])
				seen[slot] = 1;
	}
	for (slot = 1; slot < n; slot++)
		expect(seen[slot], 1, "grouped slots reach every box, wumpa and letter");

	// No letters: the token slot hides; boxes still fill the race slot.
	n = 0;
	bits[n++] = AP_BoxMap_Code(3, 0) >= 0 ? AP_PadGlowBoxBit(AP_BoxMap_Code(3, 0)) : -1;
	AP_GlowSlots_Select(bits, n, 0, 1, groupFn, out);
	expect(out[0] == bits[0] && out[1] == -1 && out[2] == -1, 1, "lone box in the race slot");
}

int main(void)
{
	test_ranges_and_codes();
	test_boxes_match_count();
	test_letters_and_wumpa();
	test_worst_case_fits();
	test_cycles();
	printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures != 0;
}
