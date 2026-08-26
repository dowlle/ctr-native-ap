#ifndef AP_USEFUL_H
#define AP_USEFUL_H
#ifdef CTR_AP

struct GameTracker;

#define AP_USEFUL_ITEM_FIRST_INDEX 117
#define AP_USEFUL_ITEM_COUNT 3

void AP_UsefulConnectReset(void);
void AP_UsefulReceive(int effect);
void AP_UsefulTick(struct GameTracker *gGT);

#endif
#endif
