#ifndef AP_CUP_SWEEPS_H
#define AP_CUP_SWEEPS_H

#define AP_CUP_SWEEP_PSEUDO_BASE 53000
typedef struct AP_CupSweepConfig {
    int enabled, present[5], legs[5][4];
    long locations[5];
} AP_CupSweepConfig;
typedef struct AP_CupSweepAttempt {
    int active, cup, nextLeg, started;
    unsigned mask;
} AP_CupSweepAttempt;

static inline void AP_CupSweepReset(AP_CupSweepAttempt *s) {
    s->active=s->started=s->nextLeg=0;s->cup=-1;s->mask=0;
}
static inline void AP_CupSweepEnter(AP_CupSweepAttempt *s,int cup) {
    AP_CupSweepReset(s);
    if(cup>=0 && cup<5) {s->active=1;s->cup=cup;}
}
static inline void AP_CupSweepStartLeg(AP_CupSweepAttempt *s,int cup,int leg,int valid) {
    if(!valid || !s->active || cup!=s->cup || leg!=s->nextLeg || s->started || leg<0 || leg>=4) {
        AP_CupSweepReset(s);return;
    }
    s->started=1;
}
static inline void AP_CupSweepRecord(AP_CupSweepAttempt *s,int cup,int leg,int first,int forced) {
    if(!s->active)return;
    if(cup!=s->cup || !first || forced) {AP_CupSweepReset(s);return;}
    if(!s->started && leg==s->nextLeg-1)return; /* repeated result frame */
    if(!s->started || leg!=s->nextLeg || leg<0 || leg>=4) {AP_CupSweepReset(s);return;}
    s->mask |= 1u<<leg;s->nextLeg++;s->started=0;
}
static inline int AP_CupSweepReady(const AP_CupSweepAttempt *s,int cup,int legs) {
    return s->active && s->cup==cup && legs==4 && s->nextLeg==4 && s->mask==15 && !s->started;
}
static inline long AP_CupSweepCode(const AP_CupSweepConfig *cfg,int cup) {
    return cfg && cfg->enabled && cup>=0 && cup<5 && cfg->present[cup] ? cfg->locations[cup] : -1;
}
#endif
