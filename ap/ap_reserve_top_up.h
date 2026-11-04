#ifndef AP_RESERVE_TOP_UP_H
#define AP_RESERVE_TOP_UP_H

struct GameTracker;
#define AP_RESERVE_TOP_UP_ITEM_INDEX 203
// Use the fixed lower endpoint of 25-50% per copy,
// rounded from one quarter of the positive signed 16-bit reserve range.
#define AP_RESERVE_TOP_UP_AMOUNT 8192

void AP_ReserveTopUpReset(void);
void AP_ReserveTopUpReceive(void);
void AP_ReserveTopUpTick(struct GameTracker *gGT);

#endif
