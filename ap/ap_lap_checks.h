#ifndef AP_LAP_CHECKS_H
#define AP_LAP_CHECKS_H
/* #344 lap completion checks: `<Track>: Complete One Lap`, one per track,
 * sent at the first accepted lap of any lap race on it (docs/LAP_CHECKS.md).
 * The plan carries the codes on its pads, cup legs and boss encounters
 * (ap_content_plan.h). Pure C, so tools/test-lap-checks.cpp runs it without
 * the engine. */

/* Does this lap crossing earn the check? The local player's own kart (driver
 * slot 0, not a bot) completing a lap: lapIndex has just advanced to 1 or
 * more. `adventure` is 1 in an Adventure race with laps (Trophy Race, CTR
 * Challenge, Relic Race, cup leg, boss race); Crystal Challenges, the hub,
 * Arcade and demo mode are 0. */
static inline int AP_LapCrossingCounts(int driverSlot, int isBot, int lapIndex, int adventure)
{
    return driverSlot == 0 && !isBot && lapIndex >= 1 && adventure;
}
#endif
