// Stage 2 line under the pad name (A4-59): which line a pad shows, its text and
// colour state, and the layout lift, on the production header
// ap/ap_pad_stage2_line.h. The drawing site in game/232/AH_WarpPad.c and the
// glue in ap/ap_hooks_pads.c are pinned by test-pad-stage2-line-production.py.
//
// cc -std=gnu99 -Wall -Wextra -Wno-unused-function -DCTR_AP -I ap -I . -I include -o /tmp/test-pad-stage2-line tools/test-pad-stage2-line.c
// /tmp/test-pad-stage2-line

#include <stdio.h>
#include <string.h>
#include "../ap/ap_pad_stage2_line.h"

static int checks, failures;
#define EXPECT(c, why) do { checks++; if (!(c)) { printf("FAIL %s\n", why); failures++; } } while (0)

static int64_t checked_codes[8];
static int checked_n;
static int is_checked(int64_t code)
{
	int i;
	for (i = 0; i < checked_n; ++i)
		if (checked_codes[i] == code)
			return 1;
	return 0;
}

// Tiger Temple style track pad: Trophy Race (stage 1), CTR Challenge and Relic
// Race behind stage 2, which asks for `count` items of `mask`.
static ctr_content_pad track_pad(unsigned mask, int count)
{
	ctr_content_pad p;
	memset(&p, 0, sizeof p);
	p.occupied = 1;
	p.mode_count = 3;
	p.modes[0] = AP_CONTENT_MODE_TROPHY; p.stages[0] = 1;
	p.modes[1] = AP_CONTENT_MODE_CTR;    p.stages[1] = 2;
	p.modes[2] = AP_CONTENT_MODE_RELIC;  p.stages[2] = 2;
	p.trophy_location = 100;
	p.ctr_location = 101;
	p.relic_locations[0] = 102; p.relic_locations[1] = 103; p.relic_locations[2] = 104;
	p.has_stage2 = 1;
	p.stage2_mask = mask;
	p.stage2_count = count;
	p.first_race_rule = 1;
	return p;
}

static int counts[15];

static int line_of(const ctr_content_pad *p, int state, char *out)
{
	int owned = ap_content_stage2_owned(p, counts);
	int s = ap_pad_stage2_line_state(p, state, owned, ap_pad_stage2_left(p, is_checked));
	ap_pad_stage2_line_text(p, s, owned, out, 64);
	return s;
}

int main(void)
{
	char text[64];
	ctr_content_pad p;
	int s;

	// 2 Gold Relics (item offset 2), none held: red, shown from the first
	// approach on, while the pad is still closed (state 1) or open (2).
	memset(counts, 0, sizeof counts);
	p = track_pad(1u << 2, 2);
	s = line_of(&p, 1, text);
	EXPECT(s == AP_S2LINE_UNMET, "closed pad shows its unmet stage 2");
	EXPECT(strcmp(text, "Stage 2: 0/2 Gold Relics") == 0, "unmet text");
	s = line_of(&p, 2, text);
	EXPECT(s == AP_S2LINE_UNMET, "open pad before the first race shows stage 2");

	// One held: still red, the count moves.
	counts[2] = 1;
	s = line_of(&p, 3, text);
	EXPECT(s == AP_S2LINE_UNMET, "waiting pad still red");
	EXPECT(strcmp(text, "Stage 2: 1/2 Gold Relics") == 0, "held 1 of 2");

	// Both held: green, even before the first race is won (the item part is met).
	counts[2] = 2;
	s = line_of(&p, 2, text);
	EXPECT(s == AP_S2LINE_MET, "met is green");
	EXPECT(strcmp(text, "Stage 2: 2/2 Gold Relics") == 0, "met text");
	counts[2] = 5;
	s = line_of(&p, 4, text);
	EXPECT(s == AP_S2LINE_MET && strcmp(text, "Stage 2: 2/2 Gold Relics") == 0, "held count capped at the need");

	// Every stage 2 check collected while the Trophy check still waits: grey.
	checked_n = 0;
	checked_codes[checked_n++] = 101;
	checked_codes[checked_n++] = 102;
	checked_codes[checked_n++] = 103;
	s = line_of(&p, 4, text);
	EXPECT(s == AP_S2LINE_MET, "one relic tier left keeps it coloured");
	checked_codes[checked_n++] = 104;
	s = line_of(&p, 4, text);
	EXPECT(s == AP_S2LINE_DONE, "nothing left behind stage 2 is grey");
	EXPECT(strcmp(text, "Stage 2 done: 2 Gold Relics") == 0, "done text");
	// A seed without some tier checks (code 0) does not count them as left.
	checked_n = 0;
	p.relic_locations[1] = 0; p.relic_locations[2] = 0;
	checked_codes[checked_n++] = 101;
	checked_codes[checked_n++] = 102;
	EXPECT(line_of(&p, 4, text) == AP_S2LINE_DONE, "absent tier codes are not left");
	checked_n = 0;

	// A Done pad shows nothing; its grey X already says it.
	p = track_pad(1u << 2, 2);
	EXPECT(line_of(&p, 5, text) == AP_S2LINE_NONE && text[0] == 0, "done pad has no line");

	// No second stage, a stage 2 equal to stage 1 collapsed by the seed
	// (has_stage2 0 or every mode stage 1), cups, crystals, empty pads.
	p = track_pad(1u << 2, 2);
	p.has_stage2 = 0;
	EXPECT(line_of(&p, 2, text) == AP_S2LINE_NONE, "no stage 2");
	p = track_pad(1u << 2, 2);
	p.stages[1] = p.stages[2] = 1;
	EXPECT(line_of(&p, 2, text) == AP_S2LINE_NONE, "no mode behind stage 2");
	p = track_pad(1u << 2, 2);
	p.cup = 1;
	EXPECT(line_of(&p, 2, text) == AP_S2LINE_NONE, "cup");
	p = track_pad(1u << 2, 2);
	p.crystal = 1;
	EXPECT(line_of(&p, 2, text) == AP_S2LINE_NONE, "crystal");
	p = track_pad(1u << 2, 2);
	p.occupied = 0;
	EXPECT(line_of(&p, 2, text) == AP_S2LINE_NONE, "empty pad");
	p = track_pad(1u << 2, 0);
	EXPECT(line_of(&p, 2, text) == AP_S2LINE_NONE, "zero count");
	EXPECT(ap_pad_stage2_line_state(NULL, 2, 0, 1) == AP_S2LINE_NONE, "null pad");

	// ctr_first: modes CTR (stage 1) then Relic (stage 2).
	memset(counts, 0, sizeof counts);
	p = track_pad(1u << 14, 3);
	p.mode_count = 2;
	p.modes[0] = AP_CONTENT_MODE_CTR; p.stages[0] = 1;
	p.modes[1] = AP_CONTENT_MODE_RELIC; p.stages[1] = 2;
	p.ctr_first = 1;
	counts[14] = 1;
	EXPECT(line_of(&p, 1, text) == AP_S2LINE_UNMET, "ctr_first relic stage 2");
	EXPECT(strcmp(text, "Stage 2: 1/3 Keys") == 0, "key wording");

	// Any-of families and singular wording, as the tracker's rows say them.
	memset(counts, 0, sizeof counts);
	counts[9] = 1; counts[12] = 1;
	p = track_pad(0x3e00u, 3);
	line_of(&p, 1, text);
	EXPECT(strcmp(text, "Stage 2: 2/3 Any Gems") == 0, "any gem");
	p = track_pad(1u << 0, 1);
	counts[0] = 0;
	line_of(&p, 1, text);
	EXPECT(strcmp(text, "Stage 2: 0/1 Trophy") == 0, "one trophy");
	p = track_pad(1u << 8, 4);
	line_of(&p, 1, text);
	EXPECT(strcmp(text, "Stage 2: 0/4 Purple CTR Tokens") == 0, "longest single wording");
	EXPECT((int)strlen(text) < 64, "fits the drawing buffer");
	EXPECT(strchr(text, '(') == NULL, "no parentheses: the HUD font has none");

	// A small buffer truncates safely.
	{
		char small[12];
		int owned = 0;
		memset(small, 'x', sizeof small);
		ap_pad_stage2_line_text(&p, AP_S2LINE_UNMET, owned, small, sizeof small);
		EXPECT(small[sizeof small - 1] == 0, "terminated in a short buffer");
		EXPECT(ap_pad_stage2_line_text(&p, AP_S2LINE_NONE, 0, small, sizeof small) == 0 && small[0] == 0,
		       "none clears");
	}

	// Layout: one line keeps the old block, two lift it one small line.
	EXPECT(ap_pad_title_block_lift(0, 8) == 0, "no line, no lift");
	EXPECT(ap_pad_title_block_lift(1, 8) == 0, "one line, no lift");
	EXPECT(ap_pad_title_block_lift(2, 8) == 8, "two lines lift 8");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
