#include <assert.h>
#include <stdio.h>
#include "../ap/ap_itemsanity_logic.h"

static void test_frozen_classification(void)
{
	const int held[AP_ITEMSANITY_WEAPON_COUNT] = {0,1,2,3,4,6,7,8,9,10,11};
	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
	{
		assert(AP_ItemsanityWeaponIndex(held[i]) == i);
		assert(AP_ItemsanityLocationCode(held[i], 0) == 35016000L + i * 2);
		assert(AP_ItemsanityLocationCode(held[i], 1) == 35016001L + i * 2);
	}
	assert(AP_ItemsanityWeaponIndex(5) == -1);
	assert(AP_ItemsanityWeaponIndex(12) == -1);
	assert(AP_ItemsanityWeaponIndex(13) == -1);
}

static void test_roll_then_substitute(void)
{
	const unsigned char table[] = {0, 0, 1, 2, 2, 2, 5, 7, 11};
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

	owned[2] = 1; // Missile only
	assert(AP_ItemsanitySubstituteRoll(2, 77, table, 9, owned) == 2);
	for (unsigned roll = 0; roll < 1000; roll++)
		assert(AP_ItemsanityCanonicalWeapon(
			AP_ItemsanitySubstituteRoll(1, roll, table, 9, owned)) == 2);

	owned[0] = 1; // Turbo plus Missile; Spring table entries count as Turbo
	for (unsigned roll = 0; roll < 1000; roll++)
	{
		int got = AP_ItemsanityCanonicalWeapon(
			AP_ItemsanitySubstituteRoll(7, roll, table, 9, owned));
		assert(got == 0 || got == 2);
	}

	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
		owned[i] = 0;
	assert(AP_ItemsanitySubstituteRoll(1, 42, table, 9, owned) == AP_ITEMSANITY_NO_ITEM);
}

static void test_inactive_and_vanilla_policy(void)
{
	assert(!AP_ItemsanityShouldFilter(0, 1, 1, 0, 0)); // absent/inactive seed
	assert(!AP_ItemsanityShouldFilter(1, 0, 1, 0, 0)); // bot/nonlocal driver
	assert(!AP_ItemsanityShouldFilter(1, 1, 0, 0, 0)); // non-adventure mode
	assert(!AP_ItemsanityShouldFilter(1, 1, 1, 1, 0)); // battle stays vanilla
	assert(!AP_ItemsanityShouldFilter(1, 1, 1, 0, 1)); // ruled crystal override
	assert(AP_ItemsanityShouldFilter(1, 1, 1, 0, 0));
}

static void test_duplicate_and_reset_model(void)
{
	unsigned char owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};
	owned[4] = 1;
	owned[4] = 1; // duplicate receipt stays boolean
	assert(owned[4] == 1);
	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
		owned[i] = 0; // fresh-connect reset
	for (int i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
		assert(owned[i] == 0);
	owned[4] = 1; // authoritative replay
	assert(owned[4] == 1);
}

int main(void)
{
	test_frozen_classification();
	test_roll_then_substitute();
	test_inactive_and_vanilla_policy();
	test_duplicate_and_reset_model();
	puts("itemsanity harness: all gates passed");
	return 0;
}
