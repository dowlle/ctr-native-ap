#ifndef AP_TRAP_CLOCKED_OUT_RUNTIME_H
#define AP_TRAP_CLOCKED_OUT_RUNTIME_H

#include "ap_trap_clocked_out_logic.h"
static AP_TrapClockStage g_trap_clocked[2];

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

static void AP_TrapTryClockedOut(struct GameTracker *gt, struct Driver *driver,
                                AP_TrapClockStage *stage)
{
    if (AP_TrapApplyClockedOut(gt, driver))
        AP_TrapClockLanded(stage);
}

static void AP_TrapStartClockedOut(struct GameTracker *gt, struct Driver *local, unsigned seats)
{
    struct Driver *drivers[] = { local, AP_TrapCrystalPartner(gt) };
    for (int player = 0; player < 2; player++)
    {
        AP_TrapClockStage *stage = &g_trap_clocked[player];
        AP_TrapClockClear(stage);
        if (!(seats & (1u << player)))
            continue;
        AP_TrapClockFired(stage);
        AP_TrapTryClockedOut(gt, drivers[player], stage);
    }
}

static void AP_TrapPollClockedOut(struct GameTracker *gt, struct Driver *local)
{
    struct Driver *drivers[] = { local, AP_TrapCrystalPartner(gt) };
    int gate = AP_TrapSchedPollGate(&g_sched, AP_TRAP_CLOCKED_OUT);
    for (int player = 0; player < 2; player++)
    {
        struct Driver *driver = drivers[player];
        AP_TrapClockStage *stage = &g_trap_clocked[player];
        if (AP_TrapClockPoll(stage, gate, AP_TrapClockedEligible(gt, driver),
                             driver != NULL, driver ? driver->clockReceive : 0) == AP_TRAP_CLOCK_TRY)
            AP_TrapTryClockedOut(gt, driver, stage);
    }
    if (gate == AP_TRAP_POLL_RUN && !g_trap_clocked[0].pending && !g_trap_clocked[0].applied &&
        !g_trap_clocked[1].pending && !g_trap_clocked[1].applied)
        AP_TrapSchedEffectDone(&g_sched,AP_TRAP_CLOCKED_OUT);
}

static void AP_TrapClockedOutReset(struct Driver *local, struct Driver *partner)
{
    struct Driver *drivers[] = { local, partner };
    for (int player = 0; player < 2; player++)
    {
        struct Driver *driver = drivers[player];
        // Only an applied trap owns these fields.
        if (g_trap_clocked[player].applied && driver)
        {
            driver->clockReceive = 0;
            driver->clockFlash = 0;
        }
        AP_TrapClockClear(&g_trap_clocked[player]);
    }
}
#endif
