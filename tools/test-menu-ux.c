// Focused out-of-engine checks for the options/connection UX helpers.
//
//   cc -Wall -Wextra -Werror -I include -o /tmp/test-menu-ux tools/test-menu-ux.c && /tmp/test-menu-ux

#include <stdio.h>
#include <string.h>
#include <ctr_menu_ux.h>

static int failures;

#define CHECK(name, expr) do { if (!(expr)) { printf("FAIL %s\n", name); failures++; } } while (0)

int main(void)
{
	bool value = true;
	CTR_MenuBoolStep(&value, -1);
	CHECK("left sets bool off", value == false);
	CTR_MenuBoolStep(&value, +1);
	CHECK("right sets bool on", value == true);
	CTR_MenuBoolStep(&value, 0);
	CHECK("zero direction leaves bool unchanged", value == true);

	CHECK("uppercase A marked", CTR_MenuSlotCharNeedsCaseMark('A'));
	CHECK("uppercase Z marked", CTR_MenuSlotCharNeedsCaseMark('Z'));
	CHECK("lowercase not marked", !CTR_MenuSlotCharNeedsCaseMark('a'));
	CHECK("digit not marked", !CTR_MenuSlotCharNeedsCaseMark('7'));
	CHECK("underscore not marked", !CTR_MenuSlotCharNeedsCaseMark('_'));

	CHECK("exact InvalidSlot recognized", CTR_MenuErrorIsInvalidSlot("InvalidSlot"));
	CHECK("decorated InvalidSlot recognized", CTR_MenuErrorIsInvalidSlot("InvalidSlot: Appie"));
	CHECK("other error preserved", !CTR_MenuErrorIsInvalidSlot("InvalidPassword"));
	CHECK("null error safe", !CTR_MenuErrorIsInvalidSlot(NULL));
	CHECK("InvalidSlot guidance stable",
		strcmp(CTR_MenuInvalidSlotMessage(), "Slot not found; case matters") == 0);

	if (failures != 0)
		return 1;
	puts("PASS menu UX helpers (13 checks)");
	return 0;
}
