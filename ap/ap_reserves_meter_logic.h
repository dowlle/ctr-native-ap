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
// wrapped value shows as a full bar. Only the display changes; the stored value
// is left alone (ruling 2026-09-27: keep retail reserves, no cap).
//
// "Saffi fire" (purple, as in CTR Unlimited) is the wrapped state: the sign bit
// of the stored field is set (unsigned 0x8000..0xffff). The engine only counts
// reserves down while the signed value is above zero
// (VehPhysProc_Driving_DecrementTimer), while the boost speed and acceleration
// checks test reserves != 0 (VehPhysGeneral.c). So a wrapped value never
// drains: the kart keeps boosting until something zeroes the field (a hit, a
// respawn, a braking or reverse cancel) or another boost
// carries it back past 0xffff into the positive range, where it drains again.
// Blue stays the ReservesMeter module's "past the bar" tell for 8572..0x7fff,
// which still counts down.

#define AP_RESERVES_METER_WIDTH 49

enum
{
	AP_RESERVES_TIER_RED = 0,
	AP_RESERVES_TIER_YELLOW,
	AP_RESERVES_TIER_GREEN,
	AP_RESERVES_TIER_BLUE,
	AP_RESERVES_TIER_SAFFI // wrapped past 0x7fff: reserves no longer count down
};

// Past the maximum: the signed field has wrapped, so the engine no longer drains it.
static inline int AP_ReservesMeterSaffi(short raw)
{
	return raw < 0;
}

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
// fill would overrun the bar and is pegged at full width; Saffi fire means the
// value has wrapped and no longer counts down (also full width).
static inline int AP_ReservesMeterTier(short raw)
{
	int v = AP_ReservesMeterValue(raw);
	if (AP_ReservesMeterSaffi(raw))
		return AP_RESERVES_TIER_SAFFI;
	if ((v * 14) / 2400 > AP_RESERVES_METER_WIDTH)
		return AP_RESERVES_TIER_BLUE;
	if (v <= 1600)
		return AP_RESERVES_TIER_RED;
	return v < 3840 ? AP_RESERVES_TIER_YELLOW : AP_RESERVES_TIER_GREEN;
}

#endif // AP_RESERVES_METER_LOGIC_H
