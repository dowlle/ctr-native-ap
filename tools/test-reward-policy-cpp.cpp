// C++ translation-unit compatibility check for the shared reward-display policy
// header. Keep this tiny: the production network side is C++, while ap_hooks.c
// is C99.

#include "../ap/ap_reward_policy.h"

int main()
{
	if (AP_ItemCategory(AP_ITEM_BASE + AP_CAPABILITY_ITEM_FIRST_INDEX) !=
	    AP_CAT_CRYSTAL)
		return 1;
	if (!AP_RewardKeepsModel(AP_CAT_CRYSTAL))
		return 2;
	if (AP_RewardKeepsModel(AP_CAT_NONE))
		return 3;
	if (AP_RewardModelForCat(AP_CAT_WUMPA) != AP_MODEL_WUMPA)
		return 4;
	if (AP_RewardScaleForCat(AP_CAT_CRYSTAL) != 0x1400)
		return 5;
	return AP_RewardTintForCat(AP_CAT_CRYSTAL) == 0x0d22fff0u ? 0 : 6;
}
