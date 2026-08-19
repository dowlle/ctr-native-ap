#include <stdio.h>
#include <string.h>

#include "../ap/ap_verify_logic.h"

static int failures;
#define OK(label, expr) do { int got = !!(expr); \
	printf("%s  %s\n", got ? "ok  " : "FAIL", label); failures += !got; } while (0)

int main(void)
{
	AP_VerifyOptions o;
	int items[AP_VF_ITEM_COUNT];
	memset(&o, 0, sizeof o);
	memset(items, 0, sizeof items);
	o.character_unlocks = 1;
	o.starting_character = 0;
	o.logic_difficulty = 2;

	OK("starting racer is usable", AP_VerifyCharacterUnlocked(&o, items, 0));
	OK("locked Cortex starts unavailable", !AP_VerifyCharacterUnlocked(&o, items, 1));
	items[AP_VF_CHARACTER_FIRST + 4] = 1;
	OK("Cortex unlock item opens its racer lock", AP_VerifyCharacterUnlocked(&o, items, 1));

	o.boost_mode = 1;
	OK("shared USF gate blocks at rank zero", !AP_VerifyCapabilityGate(&o, items, -1, 2, 0));
	items[AP_VF_BOOST_SHARED] = 2;
	OK("shared USF gate opens at two copies", AP_VerifyCapabilityGate(&o, items, -1, 2, 0));

	memset(items, 0, sizeof items);
	o.boost_mode = 2;
	o.stats_mode = 2;
	items[AP_VF_PC_FIRST + 0 * 4] = 2; // Crash boost
	items[AP_VF_PC_FIRST + 4 * 4 + 1] = 1; // Cortex stats, split away
	items[AP_VF_PC_FIRST + 4 * 4 + 2] = 1;
	items[AP_VF_PC_FIRST + 4 * 4 + 3] = 1;
	items[AP_VF_CHARACTER_FIRST + 4] = 1;
	OK("per-racer gate rejects split boost/stats", !AP_VerifyCapabilityGate(&o, items, -1, 2, 1));
	items[AP_VF_PC_FIRST + 0 * 4 + 1] = 1;
	items[AP_VF_PC_FIRST + 0 * 4 + 2] = 1;
	items[AP_VF_PC_FIRST + 0 * 4 + 3] = 1;
	OK("one racer satisfying every term opens gate", AP_VerifyCapabilityGate(&o, items, -1, 2, 1));
	OK("Cortex-locked gate does not borrow Crash ranks", !AP_VerifyCapabilityGate(&o, items, 1, 2, 1));

	memset(items, 0, sizeof items);
	o.boost_mode = 1;
	o.stats_mode = 1;
	o.itemsanity = 1;
	OK("Tiger door box needs an opener", !AP_VerifyBoxGate(&o, items, 4, 5, -1));
	items[AP_VF_WEAPON_FIRST + 5] = 1; // Shield Bubble
	OK("Tiger door box accepts a ruled opener", AP_VerifyBoxGate(&o, items, 4, 5, -1));
	OK("HAS hard box blocks below USF/stats", !AP_VerifyBoxGate(&o, items, 7, 8, -1));
	items[AP_VF_BOOST_SHARED] = 2;
	items[AP_VF_BOOST_SHARED + 1] = 1;
	items[AP_VF_BOOST_SHARED + 2] = 1;
	items[AP_VF_BOOST_SHARED + 3] = 1;
	OK("HAS hard box opens at exact capability minima", AP_VerifyBoxGate(&o, items, 7, 8, -1));

	o.shortcut_knowledge = 1;
	items[AP_VF_BOOST_SHARED] = 0;
	OK("Oxide finish needs USF below hard knowledge", !AP_VerifyTrophyCapabilityGate(&o, items, 13, -1));
	o.shortcut_knowledge = 2;
	OK("hard knowledge activates Oxide bare escape", AP_VerifyTrophyCapabilityGate(&o, items, 13, -1));

	memset(items, 0, sizeof items);
	o.logic_difficulty = 1;
	o.shortcut_knowledge = 0;
	items[AP_VF_WEAPON_FIRST + 6] = 1; // Mask family
	OK("difficulty gate rejects only one weapon family", !AP_VerifyTrophyCapabilityGate(&o, items, 3, -1));
	items[AP_VF_WEAPON_FIRST + 2] = 1; // Missile family
	OK("difficulty gate accepts two distinct families", AP_VerifyTrophyCapabilityGate(&o, items, 3, -1));

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
