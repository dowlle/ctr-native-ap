#ifndef AP_TRAP_CLOCKED_OUT_LOGIC_H
#define AP_TRAP_CLOCKED_OUT_LOGIC_H

#include "ap_trap_sched_logic.h"

// #455: retail non-juiced clock duration, irrespective of Wumpa count.
#define AP_TRAP_CLOCKED_OUT_MS 0x1e00
typedef struct { int pending, applied; } AP_TrapClockStage;
enum { AP_TRAP_CLOCK_WAIT, AP_TRAP_CLOCK_TRY, AP_TRAP_CLOCK_DONE };
static inline void AP_TrapClockClear(AP_TrapClockStage *s) { s->pending=s->applied=0; }
static inline void AP_TrapClockFired(AP_TrapClockStage *s) { s->pending=1;s->applied=0; }
static inline void AP_TrapClockLanded(AP_TrapClockStage *s) { s->pending=0;s->applied=1; }
static inline int AP_TrapClockPoll(AP_TrapClockStage *s,int gate,int eligible,int haveDriver,int timer) {
    if(gate==AP_TRAP_POLL_GONE) { AP_TrapClockClear(s);return AP_TRAP_CLOCK_WAIT; }
    if(gate!=AP_TRAP_POLL_RUN || !eligible || !haveDriver)return AP_TRAP_CLOCK_WAIT;
    if(s->pending)return AP_TRAP_CLOCK_TRY;
    if(s->applied && timer==0) { AP_TrapClockClear(s);return AP_TRAP_CLOCK_DONE; }
    return AP_TRAP_CLOCK_WAIT;
}
static inline int AP_TrapClockOwed(const AP_TrapClockStage *s,int gate) {
    return s->pending && gate!=AP_TRAP_POLL_GONE;
}
#endif
