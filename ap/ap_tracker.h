#ifndef AP_TRACKER_H
#define AP_TRACKER_H
int AP_TrackerMenuFrame(void);
int AP_TrackerOpen(void);
void AP_TrackerPresent(void);
void AP_TrackerShutdown(void);
/* Hub pause menu TRACKER row: shown when this is true, opens on the next frame. */
int AP_TrackerAvailable(void);
void AP_TrackerRequestOpen(void);

/* #375: start a pad's or garage's activities from the tracker
 * (ap_tracker_launch.c). The activity lists come from the gates the pad and
 * garage ask; a start resumes the hub and the pad itself takes the kart. */
#include "ap_pad_activity_logic.h"
#include "ap_tracker_launch_logic.h"
struct GameTracker;
int AP_TrackerLaunchEnabled(void);
int AP_PadActivities(int hub, int physical, int dest, ap_padact_list *out);
int AP_GarageActivities(int hub, ap_padact_list *out);
int AP_TrackerLaunchBegin(int hub, int physical, int dest, int activity, int mode);
void AP_TrackerLaunchFrame(struct GameTracker *gt);
int AP_TrackerLaunchPadArrives(int physical, int dest);
int AP_TrackerLaunchGarageArrives(void);
void AP_TrackerLaunchTaken(void);
int AP_TrackerLaunchTakeMode(int physical, int dest);
void AP_TrackerLaunchDrop(int why);
int AP_TrackerLaunchPending(void);
/* MainFreeze.c: the pause menu's own RESUME, for a tracker start. */
void MainFreeze_APResumeFromTracker(void);
#endif
