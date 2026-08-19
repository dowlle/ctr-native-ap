#ifndef AP_BLUE_FIRE_H
#define AP_BLUE_FIRE_H

#ifdef CTR_AP
struct GameTracker;

// Progressive Boost's final tier is presentation only. This swaps the retail
// exhaust palettes while the local player is actively carrying USF speed.
void AP_BlueFireTick(struct GameTracker *gGT);
#endif

#endif
