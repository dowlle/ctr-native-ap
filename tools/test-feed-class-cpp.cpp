// C++ translation-unit compatibility check for the shared classification header.
// Keep this tiny: the production network side is C++, while ap_hooks.c is C99.

#include "../ap/ap_item_flags.h"

int main()
{
	if (AP_ItemFlagsClass(0) != AP_ITEM_CLASS_FILLER)
		return 1;
	if (AP_ItemFlagsClass(AP_ITEM_FLAG_PROGRESSION | AP_ITEM_FLAG_USEFUL |
	                      AP_ITEM_FLAG_TRAP) != AP_ITEM_CLASS_PROGRESSION)
		return 2;
	return AP_ItemFlagsClass(AP_ITEM_FLAG_USEFUL | AP_ITEM_FLAG_TRAP) ==
	               AP_ITEM_CLASS_USEFUL ? 0 : 3;
}
