#ifndef CTR_MENU_UX_H
#define CTR_MENU_UX_H

#include <stdbool.h>
#include <string.h>

// Left and right give boolean rows the same value-editing contract as enums
// and sliders. Cross/Circle remain available as the quick toggle action.
static inline void CTR_MenuBoolStep(bool *value, int dir)
{
	if (dir < 0)
		*value = false;
	else if (dir > 0)
		*value = true;
}

// The retail decal font draws both ASCII cases with the same capital glyph.
// Keep the original bytes for the server and mark only real uppercase letters.
static inline int CTR_MenuSlotCharNeedsCaseMark(unsigned char c)
{
	return c >= 'A' && c <= 'Z';
}

static inline int CTR_MenuErrorIsInvalidSlot(const char *error)
{
	return error != NULL && strstr(error, "InvalidSlot") != NULL;
}

static inline const char *CTR_MenuInvalidSlotMessage(void)
{
	return "Slot not found; case matters";
}

#endif
