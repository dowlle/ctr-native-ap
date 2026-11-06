#ifndef AP_FASTTRAVEL_H
#define AP_FASTTRAVEL_H

/* Hub fast travel (A3-16, 0.3.0-alpha3): the hub pause menu's TRAVEL row, its
 * five-hub selector and the trip itself. Logic lives in ap_fasttravel_logic.h;
 * the save-screen spawn lands in VehBirth_TeleportSelf through
 * AP_FastTravelSpawnAt. */

struct GameTracker;
struct Level;
struct RectMenu;
union SVec3;

/* The pause menu asks whether the TRAVEL row exists (an AP seed in a hub). */
int AP_FastTravelAvailable(void);
/* The TRAVEL row opens the hub selector (sdata->ptrDesiredMenu). */
void AP_FastTravelShowMenu(void);
int AP_FastTravelPending(void);
void AP_FastTravelFrame(struct GameTracker *gGT);

/* VehBirth: the armed trip claims this hub's spawn. Returns 1 and fills
 * posBottom/rotY when the kart lands at the save screen, 0 for the normal
 * spawn rules. Always disarms the trip's spawn for this levelID. */
int AP_FastTravelSpawnAt(struct Level *level1, int levelID, union SVec3 *posBottom, s16 *rotY);

#endif
