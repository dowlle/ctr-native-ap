#ifndef AP_TIZI_MADNESS_H
#define AP_TIZI_MADNESS_H
#ifdef CTR_AP

// Tizi Madness (A4-96): while Coco Team Racing track 3 "Tizi Madness" is raced,
// the local player always holds a Mask. The rules are in
// ap_tizi_madness_logic.h; the track identity (LEV and VRM pins, never the
// title) in include/platform/native_tizi_madness.h.
//
// Scope, from the ruling: every mode the track can be raced in (a seed's pad,
// cup leg or boss venue, the custom Arcade and Time Trial pages). Player 1
// only: custom tracks race as one player (2P runs a custom race as 1P for
// player 1, ruling of 2026-10-15), and AI karts are untouched.

struct GameTracker;

// Per frame, every mode. Puts a Mask into drivers[0]'s weapon slot on any live
// race frame on Tizi Madness where the slot is free and the gate allows it.
void AP_TiziMadnessTick(struct GameTracker *gGT);

#endif // CTR_AP
#endif // AP_TIZI_MADNESS_H
