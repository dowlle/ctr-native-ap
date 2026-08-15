#ifndef AP_WUMPA_LOGIC_H
#define AP_WUMPA_LOGIC_H

static int AP_WumpaStartingIncrement(int current)
{
	if (current < 0)
		current = 0;
	return current < 10 ? current + 1 : 10;
}

static int AP_WumpaStartingValue(int received, int cheatActive,
	int vanillaValue)
{
	if (cheatActive)
		return vanillaValue;
	if (received < 0)
		return 0;
	return received > 10 ? 10 : received;
}

#endif
