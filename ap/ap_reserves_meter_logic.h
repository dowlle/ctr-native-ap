#ifndef AP_RESERVES_METER_LOGIC_H
#define AP_RESERVES_METER_LOGIC_H

// Display math for the AP reserves meter (UI_DrawReservesMeter, #387).
//
// driver->reserves is a signed 16-bit field and the engine adds to it without a
// bound, exactly as retail does. Stacking enough boosts pushes it past 0x7fff,
// where it reads negative. The meter used to take that at face value: the tier
// check fell through to red and the fill length went negative, so the bar was
// drawn from the right edge outward. The engine keeps treating the stored bits as
// a large amount of fire, so the meter reads them as unsigned 16-bit and a
// wrapped value shows as the full blue bar. Only the display changes; the stored
// value is left alone.

#define AP_RESERVES_METER_WIDTH 49

enum
{
	AP_RESERVES_TIER_RED = 0,
	AP_RESERVES_TIER_YELLOW,
	AP_RESERVES_TIER_GREEN,
	AP_RESERVES_TIER_BLUE
};

// The stored field as the meter should read it: 0..65535.
static inline int AP_ReservesMeterValue(short raw)
{
	return (int)(unsigned short)raw;
}

// Fill length in pixels, always within 0..AP_RESERVES_METER_WIDTH.
static inline int AP_ReservesMeterLength(short raw)
{
	int len = (AP_ReservesMeterValue(raw) * 14) / 2400;
	return len > AP_RESERVES_METER_WIDTH ? AP_RESERVES_METER_WIDTH : len;
}

// Fill colour tier. Thresholds are the ReservesMeter module's; blue means the
// fill would overrun the bar and is pegged at full width.
static inline int AP_ReservesMeterTier(short raw)
{
	int v = AP_ReservesMeterValue(raw);
	if ((v * 14) / 2400 > AP_RESERVES_METER_WIDTH)
		return AP_RESERVES_TIER_BLUE;
	if (v <= 1600)
		return AP_RESERVES_TIER_RED;
	return v < 3840 ? AP_RESERVES_TIER_YELLOW : AP_RESERVES_TIER_GREEN;
}

#endif // AP_RESERVES_METER_LOGIC_H
