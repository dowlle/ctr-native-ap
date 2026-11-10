// Green only for checks in logic (A3-37): the colour decision of a pad whose
// open checks are partly or wholly out of logic. Runs the production headers
// ap/ap_pad_logic_look.h, ap/ap_pad_colour.h, ap/ap_pad_birth.h and the real
// win_logic evaluator on the host. The wiring in AP_PadLook, the rebuild key
// and the three surfaces is pinned by test-pad-logic-look-production.py.
//
// cc -std=gnu99 -Wall -Wextra -Wno-unused-function -DCTR_AP -I ap -I . -I include -o /tmp/test-pad-logic-look tools/test-pad-logic-look.c
// /tmp/test-pad-logic-look

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ap/ap_pad_logic_look.h"
#include "../ap/ap_pad_birth.h"

static int checks, failures;
#define EXPECT(c, why) do { checks++; if (!(c)) { printf("FAIL %s\n", why); failures++; } } while (0)

static int held_relics; // received Sapphire Relics (id 35010001)
static int item_count(void *u, long id) { (void)u; return id == 35010001L ? held_relics : 0; }

// Two relic checks: 201 needs one Sapphire Relic, 202 needs two. 301 is a
// check with no row (a box on a seed generated before A3-37b without
// pad_approach_hints). A3-37b hint rows: 401 a box and 402 a Wumpa check that
// need two Sapphire Relics, 403 a letter and 404 a podium rung that need one.
static AP_WinLogic *make_logic(void)
{
	AP_WinLogic *l = calloc(1, sizeof *l);
	l->state = AP_WL_VALID;
	l->version = 2;
	l->node_count = 4;
	l->nodes[0].tag = AP_WL_T_TRUE;
	l->nodes[1].tag = AP_WL_T_REQ; l->nodes[1].a = 4; l->nodes[1].b = 1; l->nodes[1].c = 0;
	l->nodes[2].tag = AP_WL_T_REQ; l->nodes[2].a = 4; l->nodes[2].b = 2; l->nodes[2].c = 0;
	l->region_count = 1; l->region_root[0] = 0;
	l->check_count = 6;
	l->checks[0].code = 201; l->checks[0].kind = AP_WL_KIND_SAPPHIRE; l->checks[0].region = 0; l->checks[0].rule = 1;
	l->checks[1].code = 202; l->checks[1].kind = AP_WL_KIND_GOLD; l->checks[1].region = 0; l->checks[1].rule = 2;
	l->checks[2].code = 401; l->checks[2].kind = AP_WL_KIND_HINT; l->checks[2].region = 0; l->checks[2].rule = 2;
	l->checks[3].code = 402; l->checks[3].kind = AP_WL_KIND_HINT; l->checks[3].region = 0; l->checks[3].rule = 2;
	l->checks[4].code = 403; l->checks[4].kind = AP_WL_KIND_HINT; l->checks[4].region = 0; l->checks[4].rule = 1;
	l->checks[5].code = 404; l->checks[5].kind = AP_WL_KIND_HINT; l->checks[5].region = 0; l->checks[5].rule = 1;
	return l;
}

static int look_of(AP_WinLogic *l, AP_WinLogicEnv *env, const long *codes, int n, int look)
{
	int i, in = 0, unknown = 0;
	for (i = 0; i < n; i++)
	{
		int st = AP_PadLogicStateOf(l, env, codes[i]);
		if (st == AP_PAD_LOGIC_IN) in++;
		else if (st == AP_PAD_LOGIC_UNKNOWN) unknown++;
	}
	return AP_PadLookByLogic(look, n, in, unknown);
}

int main(void)
{
	AP_WinLogic *l = make_logic();
	AP_WinLogicEnv env;
	long relics[] = {202};              // Turbo Track: only the out-of-logic relic is left
	long mixed[] = {201, 202};
	long boxOnly[] = {301};
	long boxAndRelic[] = {202, 301};
	int look;

	memset(&env, 0, sizeof env);
	env.item_count = item_count;

	// Pure decision.
	EXPECT(AP_PadLookByLogic(AP_PAD_LOOK_OPEN, 3, 1, 0) == AP_PAD_LOOK_OPEN, "an in-logic open check keeps green");
	EXPECT(AP_PadLookByLogic(AP_PAD_LOOK_OPEN, 3, 0, 0) == AP_PAD_LOOK_STAGE2_WAITING, "only out-of-logic open checks: not green");
	EXPECT(AP_PadLookByLogic(AP_PAD_LOOK_STAGE2_OPEN, 2, 0, 0) == AP_PAD_LOOK_STAGE2_WAITING, "only out-of-logic open checks: not blue-violet");
	EXPECT(AP_PadLookByLogic(AP_PAD_LOOK_OPEN, 3, 1, 2) == AP_PAD_LOOK_OPEN, "mixed keeps green");
	EXPECT(AP_PadLookByLogic(AP_PAD_LOOK_OPEN, 2, 0, 1) == AP_PAD_LOOK_OPEN, "a check with no logic row counts as in logic");
	EXPECT(AP_PadLookByLogic(AP_PAD_LOOK_OPEN, 0, 0, 0) == AP_PAD_LOOK_OPEN, "nothing listed: the state decides");
	{
		int k;
		for (k = 0; k < AP_PAD_LOOK_COUNT; k++)
			if (k != AP_PAD_LOOK_OPEN && k != AP_PAD_LOOK_STAGE2_OPEN)
				EXPECT(AP_PadLookByLogic(k, 4, 0, 0) == k, "a look that is not open is never changed");
	}

	// The real evaluator: Turbo Track after Trophy and Wumpa, relics left.
	held_relics = 0;
	look = look_of(l, &env, relics, 1, AP_PAD_LOOK_OPEN);
	EXPECT(look == AP_PAD_LOOK_STAGE2_WAITING, "out-of-logic relic only: not green");
	EXPECT(AP_PadLookHasBeamColour(look) && AP_PadLookBeamHue(look) == AP_PAD_BEAM_RED &&
	       AP_PadLookWispHue(look) == AP_PAD_BEAM_RED && AP_PadLookBeamFilter(look) == AP_PAD_BEAM_RED_FILTER,
	       "the demoted pad uses the red beam and wisps with cyan filter");
	EXPECT(AP_PadLookRGB(look) == 0xB85648u, "the demoted pad's dot and node use softer red");

	// A received item changes the logic: the look and the birth key follow.
	{
		ctr_content_pad pad;
		int counts[15], keyBefore, keyAfter;
		memset(&pad, 0, sizeof pad);
		memset(counts, 0, sizeof counts);
		keyBefore = ap_pad_birth_key(&pad, counts, 2, look, 0);
		held_relics = 2;
		look = look_of(l, &env, relics, 1, AP_PAD_LOOK_OPEN);
		keyAfter = ap_pad_birth_key(&pad, counts, 2, look, 0);
		EXPECT(look == AP_PAD_LOOK_OPEN, "the relic comes into logic: green again");
		EXPECT(keyBefore != keyAfter, "logic changing after a received item changes the rebuild key");
		held_relics = 0;
		keyAfter = ap_pad_birth_key(&pad, counts, 2, look_of(l, &env, relics, 1, AP_PAD_LOOK_OPEN), 0);
		EXPECT(keyAfter == keyBefore, "the same logic gives the same key");
	}

	held_relics = 1;
	EXPECT(look_of(l, &env, mixed, 2, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN, "mixed (one in, one out): green");
	EXPECT(look_of(l, &env, relics, 1, AP_PAD_LOOK_STAGE2_OPEN) == AP_PAD_LOOK_STAGE2_WAITING, "stage 2 pad, out of logic: not blue-violet");
	held_relics = 0;
	EXPECT(look_of(l, &env, boxOnly, 1, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN, "an open check with no row keeps green (fallback)");
	EXPECT(look_of(l, &env, boxAndRelic, 2, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN, "unknown plus out of logic keeps green (fallback)");

	// A3-37b: boxes, Wumpa, letters and rungs carry hint rows on every new
	// seed, so a pad holding only those goes orange when all are out of logic.
	{
		long boxWumpa[] = {401, 402};
		long letterRung[] = {403, 404};
		long boxAndLetter[] = {401, 403};
		held_relics = 0;
		EXPECT(AP_PadLogicStateOf(l, &env, 401) == AP_PAD_LOGIC_OUT, "a hint row is read like any row");
		EXPECT(look_of(l, &env, boxWumpa, 2, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_STAGE2_WAITING,
		       "only out-of-logic boxes and Wumpa: not green");
		EXPECT(look_of(l, &env, letterRung, 2, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_STAGE2_WAITING,
		       "only out-of-logic letters and rungs: not green");
		EXPECT(look_of(l, &env, boxWumpa, 2, AP_PAD_LOOK_STAGE2_OPEN) == AP_PAD_LOOK_STAGE2_WAITING,
		       "stage 2 pad, only out-of-logic boxes and Wumpa: not blue-violet");
		held_relics = 1;
		EXPECT(look_of(l, &env, boxAndLetter, 2, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN,
		       "a letter in logic beside an out-of-logic box keeps green");
		EXPECT(look_of(l, &env, boxWumpa, 2, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_STAGE2_WAITING,
		       "boxes and Wumpa needing more relics stay orange");
		held_relics = 2;
		EXPECT(look_of(l, &env, boxWumpa, 2, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN,
		       "the relics arrive: boxes and Wumpa in logic, green again");
		held_relics = 0;
	}

	// No usable block: nothing is guessed, the look stays.
	l->state = AP_WL_ABSENT;
	EXPECT(look_of(l, &env, relics, 1, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN, "no logic block keeps green");
	l->state = AP_WL_VALID;
	env.unreliable = 1;
	EXPECT(look_of(l, &env, relics, 1, AP_PAD_LOOK_OPEN) == AP_PAD_LOOK_OPEN, "untrusted inventory keeps green");
	env.unreliable = 0;
	EXPECT(AP_PadLogicStateOf(0, &env, 202) == AP_PAD_LOGIC_UNKNOWN, "no block pointer is unknown");

	free(l);
	printf("pad logic look: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
