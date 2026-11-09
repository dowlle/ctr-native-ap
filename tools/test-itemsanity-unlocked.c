// A4-12: exercise the production Itemsanity hooks with a minimal engine shell.
// gcc -std=c99 -Wall -Wextra -Werror tools/test-itemsanity-unlocked.c -o /tmp/test-itemsanity-unlocked
#define CTR_AP
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ap/ap_itemsanity_logic.h"

#define ADVENTURE_MODE 1
#define BATTLE_MODE 2
#define CRYSTAL_CHALLENGE 4
#define ADVENTURE_BOSS 8
struct Driver { int numWumpas; };
struct GameTracker { struct Driver *drivers[2]; int gameMode1; };
static struct { struct GameTracker *gGT; } state, *sdata = &state;
static unsigned char ap_itemsanity_owned[AP_ITEMSANITY_WEAPON_COUNT];
static int active = 1, exists = 1, fruit, rerollExcluded = -1;
static int ctr_cfg_active(void) { return active; }
static int ap_net_location_exists(long code) { (void)code; return exists; }
static int ap_net_location_checked(long code) { (void)code; return 0; }
static void AP_AppendLog(const char *line) { (void)line; }
static void RB_Player_ModifyWumpa(struct Driver *d, int n) { (void)d; fruit += n; }
static void AP_EmitClassCheck(long code, ...) { (void)code; }
static int AP_TrapRerollExcludedItem(void) { return rerollExcluded; }
#include "../ap/ap_hooks_itemsanity.c"

int main(void)
{
	static const int ids[] = {0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11};
	struct Driver local = {0}, bot = {0};
	struct GameTracker gt = {{&local, &bot}, 0};
	sdata->gGT = &gt;
	for (unsigned mask = 0; mask < (1u << AP_ITEMSANITY_WEAPON_COUNT); mask++)
	{
		for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
			ap_itemsanity_owned[i] = (mask >> i) & 1;
		for (int mode = 0; mode < 2; mode++)
		{
			unsigned seen = 0;
			gt.gameMode1 = ADVENTURE_MODE | (mode ? ADVENTURE_BOSS : CRYSTAL_CHALLENGE);
			assert(AP_ItemsanityUnlockedPool(&local));
			assert(!AP_ItemsanityUnlockedPool(&bot));
			fruit = 0;
			for (unsigned roll = 0; roll < 200; roll++)
			{
				// Even an already-owned retail draw must not starve the other unlocks.
				int got = mode ? AP_ItemsanityFilterRoll(&local, 0, roll, 0, 0)
				               : AP_ItemsanityFilterCrystalGrant(&local, 1, roll);
				if (!mask) assert(got == AP_ITEMSANITY_NO_ITEM);
				else
				{
					int index = AP_ItemsanityWeaponIndex(got);
					assert(index >= 0 && ap_itemsanity_owned[index]);
					seen |= 1u << index;
				}
				assert(AP_ItemsanityFilterRoll(&bot, 8, roll, 0, 0) == 8);
				assert(AP_ItemsanityFilterCrystalGrant(&bot, 1, roll) == 1);
			}
			assert(seen == mask);
			assert(fruit == ((!mask && mode) ? 200 : 0));
		}
	}
	// Outside the local active Adventure path, preserve retail draws and grants.
	for (int variant = 0; variant < 5; variant++)
	{
		gt.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE;
		active = variant != 0;
		exists = variant != 1;
		if (variant == 2) gt.gameMode1 &= ~ADVENTURE_MODE;
		if (variant == 3) gt.gameMode1 |= BATTLE_MODE;
		if (variant == 4) sdata->gGT = 0;
		assert(!AP_ItemsanityUnlockedPool(&local));
		assert(AP_ItemsanityFilterCrystalGrant(&local, 1, 23) == 1);
		assert(AP_ItemsanityFilterRoll(&local, 8, 23, 0, 0) == 8);
	}
	sdata->gGT = &gt;
	active = exists = 1;
	gt.gameMode1 = ADVENTURE_MODE;
	assert(!AP_ItemsanityUnlockedPool(&local));
	assert(AP_ItemsanityFilterRoll(&local, 8, 0, 0, 0) == 8);
	// Engine-wide caps remain ownership-safe after the special pool draw.
	memset(ap_itemsanity_owned, 0, sizeof ap_itemsanity_owned);
	ap_itemsanity_owned[AP_ItemsanityWeaponIndex(9)] = 1;
	ap_itemsanity_owned[AP_ItemsanityWeaponIndex(11)] = 1;
	ap_itemsanity_owned[AP_ItemsanityWeaponIndex(3)] = 1;
	gt.gameMode1 |= CRYSTAL_CHALLENGE;
	assert(AP_ItemsanitySubstituteOwned(&local, 2, 0, 0, 0) == 3);
	// Keep all frozen IDs represented in the harness as well.
	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
		assert(AP_ItemsanityWeaponIndex(ids[i]) == i);
	puts("unlocked weapons: 2048 ownership sets, 200 rolls, both modes passed");
	return 0;
}
