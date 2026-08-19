#ifndef AP_RETRO_FUELED_LOGIC_H
#define AP_RETRO_FUELED_LOGIC_H

#define AP_RETRO_FUELED_PAD_RESERVES 960
#define AP_RETRO_FUELED_FIRE_LEVEL 0x800

static inline int AP_RetroFueledShouldRewritePad(int enabled,
                                                unsigned type,
                                                unsigned turboPadMask,
                                                unsigned superEngineMask)
{
	return enabled && (type & turboPadMask) != 0 &&
	       (type & superEngineMask) == 0;
}

static inline int AP_RetroFueledShouldKeepFireCap(int enabled,
                                                  int currentCap,
                                                  int sacredCap)
{
	return enabled && currentCap > sacredCap;
}

static inline int AP_RetroFueledShouldKeepReserves(int enabled,
                                                   int holdingDown,
                                                   int landingBoost)
{
	return enabled && (holdingDown || landingBoost);
}

#endif
