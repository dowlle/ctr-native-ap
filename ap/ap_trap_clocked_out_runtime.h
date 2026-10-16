#ifndef AP_TRAP_CLOCKED_OUT_RUNTIME_H
#define AP_TRAP_CLOCKED_OUT_RUNTIME_H

#include "ap_trap_clocked_out_logic.h"
static AP_TrapClockStage g_trap_clocked;

static int AP_TrapClockedEligible(struct GameTracker *g,struct Driver *d)
{
    return g && d && AP_TrapIsLocal(d) && sdata && sdata->Loading.stage==LOAD_IDLE &&
           (AP_TrapContextOf(g,LOAD_IDLE)&AP_TRAP_CTX_ALL_NO_HUB) &&
           AP_TrapControlUnlocked(g,d) && !(g->gameMode1&PAUSE_ALL) &&
           !AP_TrapScripted(g,d) && !AP_TrapFinishOrPodium(g,d);
}

static int AP_TrapApplyClockedOut(struct GameTracker *g,struct Driver *d)
{
    int landed;
    if(!AP_TrapClockedEligible(g,d) || d->clockReceive!=0 ||
       !AP_TrapFlattenReady(d->kartState,d->squishTimer,d->invincibleTimer,
          (d->actionsFlagSet&ACTION_MASK_WEAPON)!=0,d->instBubbleHold!=0,d->pendingDamageType))return 0;
    // The ordinary local hazard path, with no attacker and no self-inflicted
    // DeathLink. The retail weapon's loop deliberately is not used.
    AP_DeathLinkSuppressSelfInflicted(1);
    landed=RB_Hazard_HurtDriver(d,1,0,0);
    AP_DeathLinkSuppressSelfInflicted(0);
    if(!landed)return 0;
    d->clockReceive=AP_TRAP_CLOCKED_OUT_MS;
    d->clockFlash=4;
    OtherFX_Play(0x44,1);
    return 1;
}

static void AP_TrapStartClockedOut(struct GameTracker *g,struct Driver *d)
{
    AP_TrapClockFired(&g_trap_clocked);
    if(AP_TrapApplyClockedOut(g,d))AP_TrapClockLanded(&g_trap_clocked);
}

static void AP_TrapPollClockedOut(struct GameTracker *g,struct Driver *d)
{
    int gate=AP_TrapSchedPollGate(&g_sched,AP_TRAP_CLOCKED_OUT);
    switch(AP_TrapClockPoll(&g_trap_clocked,gate,AP_TrapClockedEligible(g,d),d!=0,d?d->clockReceive:0)) {
    case AP_TRAP_CLOCK_TRY:
        if(AP_TrapApplyClockedOut(g,d))AP_TrapClockLanded(&g_trap_clocked);
        break;
    case AP_TRAP_CLOCK_DONE:
        AP_TrapSchedEffectDone(&g_sched,AP_TRAP_CLOCKED_OUT);
        break;
    default: break;
    }
}

static void AP_TrapClockedOutReset(struct Driver *d)
{
    // Only an applied trap owns these fields; never clear a retail clock
    // just because an armed or still-protected trap crossed a boundary.
    if(g_trap_clocked.applied && d) { d->clockReceive=0;d->clockFlash=0; }
    AP_TrapClockClear(&g_trap_clocked);
}
#endif
