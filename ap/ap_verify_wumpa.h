#ifndef AP_VERIFY_WUMPA_H
#define AP_VERIFY_WUMPA_H

#include "ap_seedcfg.h"

typedef struct
{
	long code;
	int destination;
} AP_VerifyWumpaLocation;

typedef struct
{
	unsigned char destination_open[105];
	int cup_legs[5][4];
	unsigned char cup_displaced[5];
} AP_VerifyWumpaRoutes;

static inline int AP_VerifyWumpaWorklist(const ctr_wumpa_checks *wumpa,
	AP_VerifyWumpaLocation *out, int capacity)
{
	int i, n = 0;

	if (wumpa == 0 || out == 0 || capacity <= 0)
		return 0;
	if (wumpa->mode == CTR_CFG_WUMPA_GLOBAL)
	{
		if (wumpa->global_code >= 0 && n < capacity)
		{
			out[n].code = wumpa->global_code;
			out[n].destination = -1;
			n++;
		}
		return n;
	}
	if (wumpa->mode != CTR_CFG_WUMPA_PER_TRACK)
		return 0;

	for (i = 0; i < CTR_CFG_WUMPA_TRACK_COUNT && n < capacity; i++)
		if (wumpa->tracks[i] >= 0)
		{
			out[n].code = wumpa->tracks[i];
			out[n].destination = i;
			n++;
		}
	for (i = 0; i < wumpa->custom_count &&
	    i < CTR_CFG_WUMPA_CUSTOM_MAX && n < capacity; i++)
		if (wumpa->custom[i].code >= 0)
		{
			out[n].code = wumpa->custom[i].code;
			out[n].destination = wumpa->custom[i].cup_level_id;
			n++;
		}
	return n;
}

static inline int AP_VerifyWumpaReachable(int destination,
	const AP_VerifyWumpaRoutes *routes)
{
	int cup, leg;

	if (destination < 0)
		return 1;
	if (routes == 0 || destination >= 105)
		return 0;
	if (routes->destination_open[destination])
		return 1;
	if (destination >= CTR_CFG_WUMPA_TRACK_COUNT)
		return 0;

	for (cup = 0; cup < 5; cup++)
	{
		if (routes->cup_displaced[cup] ||
		    !routes->destination_open[100 + cup])
			continue;
		for (leg = 0; leg < 4; leg++)
			if (routes->cup_legs[cup][leg] == destination)
				return 1;
	}
	return 0;
}

#endif
