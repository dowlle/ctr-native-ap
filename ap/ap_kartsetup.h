#ifndef AP_KARTSETUP_H
#define AP_KARTSETUP_H

// In-race Kart Setup (ruling of 2026-11-18), ap_kartsetup.c. RowLive says whether
// the Adventure race and cup pause menus carry the KART SETUP row;
// RequestOpen is that row's action; MenuFrame owns input and drawing while the
// page is open inside the pause (called first from RECTMENU_ProcessState).
#include "ap_kartsetup_logic.h"

int AP_KartSetupRowLive(void);
void AP_KartSetupRequestOpen(void);
int AP_KartSetupOpen(void);
int AP_KartSetupMenuFrame(void);
// MainFreeze.c: re-run the race pause row swap on the race pause menu on
// screen, if any (no-op otherwise). Returns 1 when that removed the KART SETUP
// row while it was highlighted.
int MainFreeze_APKartSetupSync(void);

#endif // AP_KARTSETUP_H
