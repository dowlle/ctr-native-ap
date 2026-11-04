// #246: one uninterrupted four-leg Cup sweep, independent of points wins.
#ifdef CTR_AP

static AP_CupSweepAttempt ap_sweep_attempt;
static int ap_sweep_legs[4];
static long ap_sweep_code;
static char ap_sweep_seed[256],ap_sweep_slot[256],ap_sweep_endpoint[512];

static int AP_CupSweepIdentity(void)
{
    char seed[256],slot[256],endpoint[512];
    int i,c=ap_sweep_attempt.cup;
    if(!ap_sweep_attempt.active || !ctr_cfg_active() ||
       AP_CupSweepCode(&ctr_cup_sweeps,c)!=ap_sweep_code || ctr_cfg_cup_displaced(c) ||
       !ap_net_seed_name(seed,sizeof seed) || !ap_net_slot_name(slot,sizeof slot) ||
       !ap_net_room_endpoint(endpoint,sizeof endpoint) || strcmp(seed,ap_sweep_seed) ||
       strcmp(slot,ap_sweep_slot) || strcmp(endpoint,ap_sweep_endpoint))return 0;
    for(i=0;i<4;++i)if(ctr_cup_sweeps.legs[c][i]!=ap_sweep_legs[i] || ctr_cfg_cup_leg(c,i)!=ap_sweep_legs[i])return 0;
    return 1;
}

void AP_CupSweepEnterFromHub(void)
{
    int c=sdata->gGT->cup.cupID,i;
    AP_CupSweepReset(&ap_sweep_attempt);
    if(!ctr_cfg_active() || AP_CupSweepCode(&ctr_cup_sweeps,c)<=0 || ctr_cfg_cup_displaced(c) ||
       !ap_net_seed_name(ap_sweep_seed,sizeof ap_sweep_seed) ||
       !ap_net_slot_name(ap_sweep_slot,sizeof ap_sweep_slot) ||
       !ap_net_room_endpoint(ap_sweep_endpoint,sizeof ap_sweep_endpoint))return;
    AP_CupSweepEnter(&ap_sweep_attempt,c);
    ap_sweep_code=AP_CupSweepCode(&ctr_cup_sweeps,c);
    for(i=0;i<4;++i)ap_sweep_legs[i]=ctr_cup_sweeps.legs[c][i];
}

static int AP_CupSweepRaceValid(struct GameTracker *g,int leg)
{
    int logical;
    if(!g || !AP_CupSweepIdentity() || leg<0 || leg>=4 || g->cup.cupID!=ap_sweep_attempt.cup ||
       (g->gameMode1 & (ADVENTURE_MODE|ADVENTURE_CUP))!=(ADVENTURE_MODE|ADVENTURE_CUP) ||
       (g->gameMode1 & (ADVENTURE_ARENA|ARCADE_MODE|MAIN_MENU|BATTLE_MODE|CRYSTAL_CHALLENGE|RELIC_RACE)) ||
       (g->gameMode2 & (CUP_ANY_KIND|TOKEN_RACE)) || !g->drivers[0] || g->drivers[0]->driverID!=0)return 0;
    logical=ap_sweep_legs[leg];
#ifdef CTR_CUSTOM_TRACKS
    if(CustomTrack_ServingLoad(g->levelID,1,g->cup.cupID) || CustomTrack_CupRaceRedirectActive(g->cup.cupID,1))return 0;
#endif
    if(logical==110)return g->levelID==13 && AP_CortexTrackActive();
    return logical==g->levelID && !AP_CortexTrackActive();
}

void AP_CupSweepOnLoadRequested(int level)
{
    struct GameTracker *g=sdata->gGT;
    int n=ap_sweep_attempt.nextLeg;
    if(ap_sweep_attempt.active && (!AP_CupSweepIdentity() || ap_sweep_attempt.started || n<0 || n>=4 ||
       g->cup.trackIndex!=n || level!=(ap_sweep_legs[n]==110?13:ap_sweep_legs[n])))AP_CupSweepReset(&ap_sweep_attempt);
}

void AP_CupSweepOnLevelStart(struct GameTracker *g)
{
    AP_CupSweepStartLeg(&ap_sweep_attempt,g->cup.cupID,g->cup.trackIndex,
                        AP_CupSweepRaceValid(g,g->cup.trackIndex));
}

void AP_CupSweepRecordLeg(struct GameTracker *g)
{
    if(!AP_CupSweepRaceValid(g,g->cup.trackIndex)) {AP_CupSweepReset(&ap_sweep_attempt);return;}
    AP_CupSweepRecord(&ap_sweep_attempt,g->cup.cupID,g->cup.trackIndex,
                      g->drivers[0]->driverRank==0,
                      AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_ADV_REWARD));
}

void AP_CupSweepComplete(struct GameTracker *g,int legs)
{
    long code=ap_sweep_code;
    int cup=g->cup.cupID;
    int won=AP_CupSweepReady(&ap_sweep_attempt,cup,legs) &&
            AP_CupSweepRaceValid(g,3) && !AP_RaceAttempt_ProducerBlocked(AP_RESULT_PRODUCER_ADV_REWARD);
    AP_CupSweepReset(&ap_sweep_attempt);
    if(!won || !ap_net_location_exists(code))return;
    AP_EmitClassCheck(code,0,-1,-1,1,"[AP CHECK] Cup sweep cup=%d location=%ld\n",cup,code);
}

#endif // CTR_AP
