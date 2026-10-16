#ifndef AP_RESERVE_TOP_UP_H
#define AP_RESERVE_TOP_UP_H

struct GameTracker;
#define AP_RESERVE_TOP_UP_ITEM_INDEX 203
#define AP_RESERVE_TOP_UP_AMOUNT 983

void AP_ReserveTopUpReset(void);
void AP_ReserveTopUpReceive(void);
void AP_ReserveTopUpTick(struct GameTracker *gGT);

#endif
