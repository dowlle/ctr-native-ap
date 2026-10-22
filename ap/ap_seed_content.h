#ifndef AP_SEED_CONTENT_H
#define AP_SEED_CONTENT_H

// Current Seed (#133 PR 7): the content-plan room's required tracks for
// Options > Custom Content, and the one "Install missing content" action.
// Implemented in ap/ap_hooks_seed_content.c; the decisions live in
// ap/ap_seed_content_logic.h.

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)

#include <stddef.h>
#include <stdint.h>
#include "ap_seed_content_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

// 1 when the connected room is a content plan that names custom content
// (packages or Cortex Vortex). Legacy rooms and rooms without custom content
// keep the old Custom Content page unchanged.
int AP_SeedContentActive(void);

// Fill rows with every track the room requires (packages in plan order, then
// Cortex Vortex). Returns the row count.
int AP_SeedContentRows(struct AP_SeedContentRow *rows, int max);

// The overview "Install missing content" shows before it starts: the rows it
// would download, their total size, or why it would refuse. Changes nothing.
// Returns an AP_SEED_PLAN_* value.
int AP_SeedContentInstallPreview(int *plan, int *count, uint64_t *bytes, char *reason, size_t reasonSize);

// Start the install. Every queued track is checked first (trusted profile,
// safe folder, size limits); one failed check refuses the whole action before
// anything changes. Returns 1 when the first download started.
int AP_SeedContentInstallStart(char *message, size_t messageSize);

// Drive the install from the Current Seed page, one track at a time. Returns
// 1 while it runs; the message says what happens. Leaving the page pauses the
// queue after the running download; coming back resumes it.
int AP_SeedContentInstallTick(char *message, size_t messageSize);
int AP_SeedContentInstallBusy(void);

#ifdef __cplusplus
}
#endif

#endif
#endif
