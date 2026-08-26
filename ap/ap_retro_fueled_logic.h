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

// The second half of the reference module's reserve-cancelation hook. Keeping
// reserves through the U-turn is not enough: while reserves are nonzero the
// vanilla physics forces the "holding Cross" state, and the Square-held
// brake/reverse path only engages when that state is clear, so a kart that
// keeps its reserves would stay locked to the accelerator and could never
// start the U-turn. The module patches the forced-Cross immediate
// (holdingX_withReserves) to zero exactly while the U-turn input is held
// (Square with Down, or Square during the landing-boost window) without Cross.
static inline int AP_RetroFueledShouldSuppressForcedCross(int enabled,
                                                          int holdingSquare,
                                                          int holdingDown,
                                                          int landingBoost,
                                                          int holdingCross)
{
	return enabled && holdingSquare && (holdingDown || landingBoost) &&
	       !holdingCross;
}

#endif
