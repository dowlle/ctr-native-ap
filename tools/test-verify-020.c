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

	// The composed goal's Oxide condition carries Oxide Station's finish term.
	memset(items, 0, sizeof items);
	memset(&o, 0, sizeof o);
	o.character_unlocks = 1;
	o.starting_character = 0;
	o.logic_difficulty = 2;
	o.boost_mode = 1;
	o.shortcut_knowledge = 1;
	OK("Oxide goal blocked with no boost", !AP_VerifyOxideGoalFinish(&o, items, -1));
	items[AP_VF_BOOST_SHARED] = 1;
	OK("Oxide goal blocked one rank below USF", !AP_VerifyOxideGoalFinish(&o, items, -1));
	items[AP_VF_BOOST_SHARED] = 2;
	OK("Oxide goal opens at USF", AP_VerifyOxideGoalFinish(&o, items, -1));
	items[AP_VF_BOOST_SHARED] = 0;
	o.shortcut_knowledge = 2;
	OK("hard knowledge clears the Oxide goal bare", AP_VerifyOxideGoalFinish(&o, items, -1));
	o.shortcut_knowledge = 1;
	o.boost_mode = 0;
	OK("Oxide goal term vacuous on an unrandomized boost chain",
		AP_VerifyOxideGoalFinish(&o, items, -1));

	memset(items, 0, sizeof items);
	o.boost_mode = 2;
	items[AP_VF_PC_FIRST + 0 * 4] = 2;    // Crash boost
	items[AP_VF_CHARACTER_FIRST + 4] = 1; // Cortex unlocked, chain still empty
	OK("Oxide goal takes any driveable racer on a free pad",
		AP_VerifyOxideGoalFinish(&o, items, -1));
	OK("Cortex-locked Oxide goal does not borrow Crash ranks",
		!AP_VerifyOxideGoalFinish(&o, items, 1));

	// Relic tier boost gates (2026-08-21 ruling): first-boost floor on every
	// Gold and Platinum, USF on Hot Air Skyway (row 12) and Oxide Station
	// (row 15) both tiers and N. Gin Labs (row 14) Platinum. Sapphire and
	// out-of-range codes stay ungated. No hard-knowledge escape.
	memset(items, 0, sizeof items);
	memset(&o, 0, sizeof o);
	o.character_unlocks = 1;
	o.starting_character = 0;
	o.logic_difficulty = 2;
	o.boost_mode = 1;
	OK("Labs Platinum blocked with no boost",
		!AP_VerifyLocationCapabilityGate(&o, items, AP_VF_LOC_LABS_PLATINUM, -1));
	OK("Labs Gold blocked with no boost",
		!AP_VerifyLocationCapabilityGate(&o, items, 35012114L, -1));
	OK("ordinary Gold and Platinum blocked with no boost",
		!AP_VerifyLocationCapabilityGate(&o, items, 35012100L, -1) &&
		!AP_VerifyLocationCapabilityGate(&o, items, 35012200L, -1));
	OK("Labs Sapphire and out-of-range codes stay ungated",
		AP_VerifyLocationCapabilityGate(&o, items, 35012014L, -1) &&
		AP_VerifyLocationCapabilityGate(&o, items, 35012314L, -1) &&
		AP_VerifyLocationCapabilityGate(&o, items, 35012099L, -1));
	items[AP_VF_BOOST_SHARED] = 1;
	OK("first boost opens ordinary Gold, Platinum and Labs Gold",
		AP_VerifyLocationCapabilityGate(&o, items, 35012100L, -1) &&
		AP_VerifyLocationCapabilityGate(&o, items, 35012200L, -1) &&
		AP_VerifyLocationCapabilityGate(&o, items, 35012114L, -1));
	OK("Labs Platinum blocked one rank below USF",
		!AP_VerifyLocationCapabilityGate(&o, items, AP_VF_LOC_LABS_PLATINUM, -1));
	OK("HAS and Oxide blocked one rank below USF on both tiers",
		!AP_VerifyLocationCapabilityGate(&o, items, 35012112L, -1) &&
		!AP_VerifyLocationCapabilityGate(&o, items, 35012212L, -1) &&
		!AP_VerifyLocationCapabilityGate(&o, items, 35012115L, -1) &&
		!AP_VerifyLocationCapabilityGate(&o, items, 35012215L, -1));
	items[AP_VF_BOOST_SHARED] = 2;
	OK("Labs Platinum opens at USF",
		AP_VerifyLocationCapabilityGate(&o, items, AP_VF_LOC_LABS_PLATINUM, -1));
	OK("HAS and Oxide open at USF on both tiers",
		AP_VerifyLocationCapabilityGate(&o, items, 35012112L, -1) &&
		AP_VerifyLocationCapabilityGate(&o, items, 35012212L, -1) &&
		AP_VerifyLocationCapabilityGate(&o, items, 35012115L, -1) &&
		AP_VerifyLocationCapabilityGate(&o, items, 35012215L, -1));

	items[AP_VF_BOOST_SHARED] = 0;
	o.shortcut_knowledge = 2;
	OK("hard knowledge does not escape Labs Platinum",
		!AP_VerifyLocationCapabilityGate(&o, items, AP_VF_LOC_LABS_PLATINUM, -1));
	items[AP_VF_BOOST_SHARED] = 2;
	OK("Labs Platinum still opens at USF on hard knowledge",
		AP_VerifyLocationCapabilityGate(&o, items, AP_VF_LOC_LABS_PLATINUM, -1));
	items[AP_VF_BOOST_SHARED] = 0;
	o.boost_mode = 0;
	OK("Labs Platinum vacuous on an unrandomized boost chain",
		AP_VerifyLocationCapabilityGate(&o, items, AP_VF_LOC_LABS_PLATINUM, -1));

	memset(items, 0, sizeof items);
	o.boost_mode = 2;
	o.shortcut_knowledge = 0;
	items[AP_VF_PC_FIRST + 0 * 4] = 2;
	items[AP_VF_CHARACTER_FIRST + 4] = 1;
	OK("Labs Platinum takes any driveable racer on a free pad",
		AP_VerifyLocationCapabilityGate(&o, items, AP_VF_LOC_LABS_PLATINUM, -1));
	OK("Cortex-locked Labs Platinum does not borrow Crash ranks",
		!AP_VerifyLocationCapabilityGate(&o, items, AP_VF_LOC_LABS_PLATINUM, 1));

	// Cup-leg term parity: an unrandomized boost chain never evaluates the
	// leg pad's racer, matching the apworld's unconditional usf_term.
	memset(&o, 0, sizeof o);
	memset(items, 0, sizeof items);
	o.character_unlocks = 1;
	o.starting_character = 0;
	o.boost_mode = 0;
	OK("USF cup leg vacuous on an unrandomized boost chain even racer-locked",
		AP_VerifyCupLegCapability(&o, items, 13, 1));
	o.boost_mode = 1;
	OK("USF cup leg blocks below USF once the chain is randomized",
		!AP_VerifyCupLegCapability(&o, items, 13, -1));
	items[AP_VF_BOOST_SHARED] = 2;
	OK("USF cup leg opens at USF",
		AP_VerifyCupLegCapability(&o, items, 13, -1));
	OK("non-USF cup leg never gated",
		AP_VerifyCupLegCapability(&o, items, 0, -1));
	o.shortcut_knowledge = 2;
	items[AP_VF_BOOST_SHARED] = 0;
	OK("hard knowledge escapes the Oxide cup leg",
		AP_VerifyCupLegCapability(&o, items, 13, -1));

	printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
