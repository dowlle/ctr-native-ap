#ifndef AP_TRIAL_LETTERS_H
#define AP_TRIAL_LETTERS_H
#ifdef CTR_AP
struct GameTracker;
// Read the player's retail assets without touching the engine load queue.
// Called before offering CTR Challenge; failure leaves Trophy/Relic available.
int AP_TrialLetters_Prepare(void);
void AP_TrialLetters_Register(struct GameTracker *gGT);
void AP_TrialLetters_Spawn(struct GameTracker *gGT);
#endif
#endif
