#ifndef AP_GRANTS_H
#define AP_GRANTS_H
#ifdef CTR_AP
struct Driver;
struct GameTracker;
#define AP_TURBO_GRANT_ITEM_INDEX 189
void AP_GrantConnectReset(void);
void AP_TurboGrantReceive(void);
void AP_TurboGrantTick(struct GameTracker *gGT);
void AP_TurboGrantOnWeaponFire(struct Driver *driver, int heldItemID);
#endif
#endif
