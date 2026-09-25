#ifndef AP_TRACKER_H
#define AP_TRACKER_H
int AP_TrackerMenuFrame(void);
int AP_TrackerOpen(void);
void AP_TrackerPresent(void);
void AP_TrackerShutdown(void);
/* Hub pause menu TRACKER row: shown when this is true, opens on the next frame. */
int AP_TrackerAvailable(void);
void AP_TrackerRequestOpen(void);
#endif
