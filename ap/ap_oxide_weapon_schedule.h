#ifndef AP_OXIDE_WEAPON_SCHEDULE_H
#define AP_OXIDE_WEAPON_SCHEDULE_H

// Oxide Station's retail attack schedule is expressed in checkpoint indices
// 0..127. Cortex Vortex owns a different restart-point graph, so preserve each
// attack's relative lap position while mapping it into the loaded graph.
static inline int AP_OxideWeaponCheckpoint(int retailCheckpoint,
                                           int lastTrackCheckpoint)
{
	if (lastTrackCheckpoint < 0)
		return -1;
	if (retailCheckpoint <= 0)
		return 0;
	if (retailCheckpoint >= 0x7f)
		return lastTrackCheckpoint;
	return (retailCheckpoint * lastTrackCheckpoint) / 0x7f;
}

#endif
