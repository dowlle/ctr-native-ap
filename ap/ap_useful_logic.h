#ifndef AP_USEFUL_LOGIC_H
#define AP_USEFUL_LOGIC_H

static unsigned AP_UsefulPendingMask(unsigned received, unsigned applied)
{
	return (received & ~applied) & 7u;
}

static unsigned AP_UsefulReceiveBit(unsigned received, int effect)
{
	if (effect < 0 || effect >= 3)
		return received & 7u;
	return (received | (1u << effect)) & 7u;
}

#endif
