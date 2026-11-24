#ifndef AP_LOOKS_H
#define AP_LOOKS_H

// Character looks at runtime (ap/ap_look_logic.h has the rules and the table).

struct Model;

// Connect reset: the owned looks are rebuilt by the ReceivedItems replay.
void AP_LooksConnectReset(void);
// The drain saw a look item (new or replayed): the look is owned in this seed.
void AP_LooksReceiveOwned(long long itemIndex);
// A look item arrived for the first time in this room (through AP_FxOffer):
// the newest look wins and becomes the racer's saved choice.
void AP_LooksReceiveNew(long long itemIndex);
// VehBirth_GetModelByName's last step: the model to use for `searchName` given
// the model the engine found. Returns `found` unless a look is in effect for
// that racer and its file loaded and validated.
struct Model *AP_LooksModelFor(const char *searchName, struct Model *found);
// The model whose animations time a driver instance: the original model the
// engine found when `m` is a look model, otherwise `m` itself.
struct Model *AP_LooksTimingModel(struct Model *m);
// After a quick load: each racing driver gets the model its birth would give
// now, so no restored pointer names a look model of an earlier run.
void AP_LooksRebindDrivers(void);

// The OPTIONS row (game/230/MM_ConfigMenu.c).
int AP_LooksIsRowValue(const int *valuePtr);
const char *AP_LooksRowLabel(const int *valuePtr);
void AP_LooksRowStep(int *valuePtr, int dir);
int AP_LooksRowHasChoice(const int *valuePtr);

#endif
