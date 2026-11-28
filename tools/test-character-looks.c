// Character looks: exercise the retained selection rules without model assets.
//
// cc -std=c99 -Wall -Wextra -DCTR_AP -I ap -I include -o /tmp/test-character-looks tools/test-character-looks.c
// && /tmp/test-character-looks
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_look_logic.h"

static int fails;
#define EXPECT(name, cond)                                                                                             \
	do                                                                                                                 \
	{                                                                                                                  \
		if (cond)                                                                                                      \
			printf("ok   %s\n", name);                                                                                 \
		else                                                                                                           \
		{                                                                                                              \
			printf("FAIL %s\n", name);                                                                                 \
			fails++;                                                                                                   \
		}                                                                                                              \
	} while (0)

int main(void)
{
	const AP_LookDef *moe = &AP_LOOKS[0];
	const int joe = AP_LOOK_RACER_JOE;
	const unsigned bit = 1u;

	// --- the table ---------------------------------------------------------
	EXPECT("one look so far", AP_LOOK_COUNT == 1);
	EXPECT("Joe Mama is item index 215 (35010215)", moe->itemIndex == 215 && strcmp(moe->itemName, "Joe Mama") == 0);
	EXPECT("Komodo Moe is a look for Komodo Joe (id 11)", moe->racer == 11);
	EXPECT("owned bits fit the table", AP_LOOK_COUNT <= 32);
	EXPECT("ORIGINAL never collides with a look value", AP_LookForValue(AP_LOOK_ORIGINAL) == -1);
	EXPECT("item lookup", AP_LookForItem(215) == 0 && AP_LookForItem(214) == -1 && AP_LookForItem(203) == -1);
	for (int i = 0; i < AP_LOOK_COUNT; i++)
	{
		char name[96];
		snprintf(name, sizeof name, "look %d label fits the value column", i);
		EXPECT(name, strlen(AP_LOOKS[i].label) <= 12);
		for (int k = i + 1; k < AP_LOOK_COUNT; k++)
			EXPECT("item indexes are unique", AP_LOOKS[i].itemIndex != AP_LOOKS[k].itemIndex);
	}

	// --- the newest-wins choice read through the owned bits ---------------------
	EXPECT("nothing owned: original even with Moe saved", AP_LookEffective(0, joe, 215) == -1);
	EXPECT("owned and saved: Moe", AP_LookEffective(bit, joe, 215) == 0);
	EXPECT("owned, ORIGINAL saved: original", AP_LookEffective(bit, joe, AP_LOOK_ORIGINAL) == -1);
	EXPECT("a look never applies to another racer", AP_LookEffective(bit, 0, 215) == -1);
	EXPECT("an unknown saved value drives as original", AP_LookEffective(bit, joe, 999) == -1 &&
	                                                        AP_LookEffective(bit, joe, -5) == -1);
	EXPECT("row shows ORIGINAL when this seed has not delivered the saved look",
	       strcmp(AP_LookRowLabel(0, joe, 215), "ORIGINAL") == 0 && AP_LookRowValue(0, joe, 215) == 0);
	EXPECT("row shows the look when owned", strcmp(AP_LookRowLabel(bit, joe, 215), "KOMODO MOE") == 0);

	// --- the OPTIONS row ladder ------------------------------------------------
	EXPECT("nothing owned: right stays ORIGINAL", AP_LookRowStep(0, joe, 0, +1) == 0);
	EXPECT("nothing owned: left stays ORIGINAL", AP_LookRowStep(0, joe, 0, -1) == 0);
	EXPECT("owned: right goes to Moe", AP_LookRowStep(bit, joe, 0, +1) == 215);
	EXPECT("owned: right clamps at the last look", AP_LookRowStep(bit, joe, 215, +1) == 215);
	EXPECT("owned: left goes back to ORIGINAL", AP_LookRowStep(bit, joe, 215, -1) == 0);
	EXPECT("unowned saved look steps from ORIGINAL", AP_LookRowStep(0, joe, 215, -1) == 0);
	EXPECT("other racers have no looks to step", AP_LookRowStep(bit, 0, 0, +1) == 0);
	EXPECT("has-choice follows the owned bits", AP_LookRacerHasOwned(bit, joe) && !AP_LookRacerHasOwned(0, joe) &&
	                                                !AP_LookRacerHasOwned(bit, 0));

	const char *why;
	unsigned char shortFile[16] = {0};
	EXPECT("tiny file refused", !AP_LookValidateModel(shortFile, sizeof shortFile, NULL, &why));
	EXPECT("NULL refused", !AP_LookValidateModel(NULL, 0, NULL, &why));
	return fails != 0;
}
