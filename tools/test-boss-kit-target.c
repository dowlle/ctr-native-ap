// Host harness for the boss-kit missile target fallback (A4-48a2, ruled
// 2026-11-10 22:43 CET: "Only for kit missiles"). The rule lives in
// ap/ap_boss_kit_target_logic.h, which ap/ap_hooks_gates.c calls from the
// boss weapon fire path, so the engine and this harness run the same code.
// tools/test-boss-kit-target-production.py checks the wiring.
//
// The retail chain is mirrored below with the file each step comes from, since
// the defect (a lead boss never locks) is in that chain and stays as it is.
//
// Build:
//   gcc -std=c99 -Wall -Wextra -Werror tools/test-boss-kit-target.c -o /tmp/test-boss-kit-target
//   /tmp/test-boss-kit-target

#include <stdio.h>
#include "../ap/ap_boss_kit_target_logic.h"

static int s_checks, s_fail;
#define CHECK(c)                                                                 \
	do                                                                           \
	{                                                                            \
		s_checks++;                                                              \
		if (!(c))                                                                \
		{                                                                        \
			s_fail++;                                                            \
			printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                   \
		}                                                                        \
	} while (0)

// Retail weaponType column, game/zGlobal_DATA.c data.BossWeapon* (rows up to
// the zero throwFlag).
static const unsigned char OXIDE[] = {0x66, 0x64, 0x03, 0x01, 0x0f, 0x01, 0x04, 0x0f, 0x04, 0x0f, 0x04, 0x0f, 0x04};
static const unsigned char ROO[] = {0x03, 0x03, 0x03, 0x03, 0x03};
static const unsigned char PAPU[] = {0x66, 0x0f, 0x66, 0x0f, 0x66};
static const unsigned char JOE[] = {0x64, 0x64, 0x0f, 0x64, 0x64};
static const unsigned char PINSTRIPE[] = {0x01, 0x01, 0x0f, 0x01, 0x01};
#define N(a) ((int)sizeof(a))

// Mirror of VehPickupItem_ShootNow, game/Vehicle/VehPickupItem.c, case 2, for a
// racing boss (not battle mode, not the player's model): the screen test found
// nobody (retail: a boss's view matrix is not inverted, so it rarely does),
// then the fallback fires only on an odd frame and only for rank != 0.
// Returns the index of the victim in race order, or -1.
static int retail_victim(int rank, int frameOdd)
{
	if (frameOdd && rank != 0)
		return rank - 1;
	return -1;
}

// The target with the A4-48a2 hook: -1 is nobody, 100 is the player.
static int victim_new(int rank, int frameOdd, int kitRace, int weaponType, const unsigned char *t, int n,
                      int playerInvisible)
{
	int v = retail_victim(rank, frameOdd);
	int q = AP_BossKitTarget_Qualifies(kitRace, weaponType, t, n);

	if (AP_BossKitTarget_UsePlayer(v >= 0, q, 1, 0, playerInvisible))
		return 100;
	return v;
}

int main(void)
{
	int rank, odd;

	// Item mapping, as PickupBots_GetBossWeaponID.
	CHECK(AP_BossKitTarget_ItemOfType(0x64) == AP_BKT_ITEM_TNT);
	CHECK(AP_BossKitTarget_ItemOfType(0x65) == AP_BKT_ITEM_BOMB);
	CHECK(AP_BossKitTarget_ItemOfType(0x66) == AP_BKT_ITEM_POTION);
	CHECK(AP_BossKitTarget_ItemOfType(2) == AP_BKT_ITEM_MISSILE);

	// Homing: missile and bomb only (the shared tracker branch).
	CHECK(AP_BossKitTarget_Homing(2) && AP_BossKitTarget_Homing(1));
	CHECK(!AP_BossKitTarget_Homing(3) && !AP_BossKitTarget_Homing(4) && !AP_BossKitTarget_Homing(0) &&
	      !AP_BossKitTarget_Homing(6) && !AP_BossKitTarget_Homing(0xf));

	// No retail table has a missile, so a kit missile always qualifies.
	CHECK(!AP_BossKitTarget_InTable(2, OXIDE, N(OXIDE)));
	CHECK(!AP_BossKitTarget_InTable(2, ROO, N(ROO)));
	CHECK(!AP_BossKitTarget_InTable(2, PAPU, N(PAPU)));
	CHECK(!AP_BossKitTarget_InTable(2, JOE, N(JOE)));
	CHECK(!AP_BossKitTarget_InTable(2, PINSTRIPE, N(PINSTRIPE)));

	// THE BUG (old code): a boss in first place gets no target for a kit
	// missile, on odd and even frames alike.
	CHECK(retail_victim(0, 0) == -1 && retail_victim(0, 1) == -1);

	// FIXED: Joe (retail: TNT only) with a kit missile, in first place: player.
	CHECK(victim_new(0, 1, 1, 2, JOE, N(JOE), 0) == 100);
	CHECK(victim_new(0, 0, 1, 2, JOE, N(JOE), 0) == 100);
	// Every garage, final included, takes it.
	CHECK(victim_new(0, 0, 1, 2, ROO, N(ROO), 0) == 100);
	CHECK(victim_new(0, 0, 1, 2, PAPU, N(PAPU), 0) == 100);
	CHECK(victim_new(0, 0, 1, 2, PINSTRIPE, N(PINSTRIPE), 0) == 100);
	CHECK(victim_new(0, 0, 1, 2, OXIDE, N(OXIDE), 0) == 100);
	// A trailing boss on an even frame also takes the player ...
	CHECK(victim_new(2, 0, 1, 2, JOE, N(JOE), 0) == 100);
	// ... and on an odd frame keeps the retail pick (the kart ahead).
	CHECK(victim_new(2, 1, 1, 2, JOE, N(JOE), 0) == 1);

	// A kit bomb the retail table lacks (Joe, Papu, Roo) also qualifies;
	// Pinstripe's and Oxide's retail bombs do not.
	CHECK(victim_new(0, 0, 1, 0x65, JOE, N(JOE), 0) == 100);
	CHECK(victim_new(0, 0, 1, 1, PAPU, N(PAPU), 0) == 100);
	CHECK(victim_new(0, 0, 1, 0x65, PINSTRIPE, N(PINSTRIPE), 0) == -1);
	CHECK(victim_new(0, 0, 1, 0x65, OXIDE, N(OXIDE), 0) == -1);

	// Retail weapons of the table keep retail behaviour (no target in first).
	CHECK(victim_new(0, 0, 1, 0x64, JOE, N(JOE), 0) == -1);
	CHECK(victim_new(0, 0, 1, 0x66, PAPU, N(PAPU), 0) == -1);
	CHECK(victim_new(0, 0, 1, 3, ROO, N(ROO), 0) == -1);

	// Non-homing kit weapons (potion, TNT the table lacks) are not targeted.
	CHECK(victim_new(0, 0, 1, 0x66, JOE, N(JOE), 0) == -1);
	CHECK(victim_new(0, 0, 1, 3, PAPU, N(PAPU), 0) == -1);

	// Not a kit race (retail attack, or a non-AP build never calls the hook):
	// even a missile gets the retail answer.
	for (rank = 0; rank < 4; rank++)
		for (odd = 0; odd < 2; odd++)
			CHECK(victim_new(rank, odd, 0, 2, JOE, N(JOE), 0) == retail_victim(rank, odd));

	// An invisible player cannot be locked, as in retail.
	CHECK(victim_new(0, 0, 1, 2, JOE, N(JOE), 1) == -1);
	CHECK(!AP_BossKitTarget_UsePlayer(0, 1, 0, 0, 0)); // no player
	CHECK(!AP_BossKitTarget_UsePlayer(0, 1, 1, 1, 0)); // player is the shooter

	printf("%d checks, %d failed\n", s_checks, s_fail);
	return s_fail ? 1 : 0;
}
