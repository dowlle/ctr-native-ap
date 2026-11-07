/* Solo mode (#89): the bundled runtime, its jobs and the private solo server.

   The client folder carries solo-runtime/ (embedded CPython, trimmed
   Archipelago core, ctr_solo.py) beside ctr.apworld. Every solo file the
   player makes lives in <client>/solo/, so Stable and Testing never share it.

   One job runs at a time (list, describe, generate, import-room,
   import-yaml). One solo session runs at a time: the client starts
   `ctr_solo.py serve` for a room, connects to it on 127.0.0.1 through the
   normal AP client, checks the server's seed against the room and keeps a
   check journal so a lost server save heals on the next connect.

   All functions run on the game thread. AP_Solo_Tick runs every frame from
   AP_NetTick. */
#ifndef AP_SOLO_H
#define AP_SOLO_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum APSoloJobState
{
	AP_SOLO_JOB_IDLE = 0,
	AP_SOLO_JOB_RUNNING,
	AP_SOLO_JOB_DONE,
	AP_SOLO_JOB_FAILED,
};

enum APSoloSessionState
{
	AP_SOLO_SESSION_NONE = 0,
	AP_SOLO_SESSION_STARTING,   /* server process started, waiting for ready */
	AP_SOLO_SESSION_CONNECTING, /* AP client dialing 127.0.0.1 */
	AP_SOLO_SESSION_LIVE,       /* slot connected, seed matches the room */
	AP_SOLO_SESSION_FAILED,
	AP_SOLO_SESSION_STOPPING,
};

#define AP_SOLO_TEXT 128

struct APSoloRoomInfo
{
	char roomId[20];
	char seedName[48];
	char slot[AP_SOLO_TEXT];
	char source[AP_SOLO_TEXT];  /* preset id, "imported" or "YAML" */
	char created[32];
	int checksDone;             /* -1 unknown */
	int checksTotal;
	int versionMatches;         /* room world version equals this client */
	char worldVersion[24];
};

struct APSoloStartInfo
{
	char label[AP_SOLO_TEXT];
	char path[1024];
	char error[AP_SOLO_TEXT];   /* non-empty: refused, shown greyed */
	int isPreset;
	char presetId[32];
	char name[AP_SOLO_TEXT];    /* imported YAML's own player name */
};

enum APSoloOptionKind
{
	AP_SOLO_OPT_TOGGLE = 0,
	AP_SOLO_OPT_CHOICE,
	AP_SOLO_OPT_RANGE,
	AP_SOLO_OPT_SET,     /* a checklist page (A2-41); value is "4 of 7" */
	AP_SOLO_OPT_WEIGHTS, /* a number page, one row per trap (A3-22); value is "2 custom" */
};

struct APSoloOptionInfo
{
	char name[64];
	char label[64];
	char group[32];
	char value[64];
	int kind;
	int visible;
	int forced;                 /* greyed, value fixed */
	int changed;                /* the value differs from the preset's */
	int blocked;                /* a guard refuses this value: Continue is held */
	char note[AP_SOLO_TEXT];    /* forced reason, guard reason or a warning */
};

/* One row of a set option's checklist, or of a weights option's number page
   (A3-22): weight and defaultWeight are the row's numbers then (on and
   isDefault only describe a checklist row). */
struct APSoloItemInfo
{
	char label[256];             /* full hint names are readable on the help page */
	int on;
	int isDefault;              /* the option's default checks this row */
	int weight;                 /* weights page: the row's weight, 0 to the maximum */
	int defaultWeight;          /* weights page: the option's default weight */
};

/* An installed custom track the new-seed flow can include (decision 32). */
enum APSoloTrackPick
{
	AP_SOLO_TRACK_OFF = 0,
	AP_SOLO_TRACK_POOL,       /* in the pool: the generator may use it */
	AP_SOLO_TRACK_GUARANTEED, /* in the pool with its own Adventure pad */
};

struct APSoloTrackInfo
{
	char id[72];               /* package/<track key>/<revision> */
	char title[AP_SOLO_TEXT];
	char revision[32];
	char author[AP_SOLO_TEXT];
	char modes[32];            /* "Trophy, CTR, Relic" */
	int timeTrial;              /* installed package can run standalone Time Trial */
	int pick;                  /* enum APSoloTrackPick */
	int needsLaps;             /* 1: an imported track waiting for its laps */
	int laps;                  /* the laps shown while needsLaps, 1 to 7 */
};

/* 1 when solo-runtime/ and its interpreter are present in the client folder. */
int AP_Solo_RuntimePresent(void);

/* Jobs. Start returns 0 when another job runs or the runtime is missing. */
int AP_Solo_ListStart(void);
/* The options screen's rows and their values in a preset or imported YAML. */
int AP_Solo_DescribeStart(const char *yamlPath);
int AP_Solo_ImportRoomStart(const char *path);
int AP_Solo_ImportYamlStart(const char *path);
/* Installed custom tracks from the shared package store or a legacy room
   folder. Picks survive a new list for the tracks still listed. */
int AP_Solo_TracksStart(const char *yamlPath);
/* yamlPath: preset or saved/inbox YAML. Options from the options screen are
   passed for every YAML opened on the options screen (useOptions 1).
   The picked custom tracks are passed
   for both; a YAML keeps its own settings and gets the tracks added. */
int AP_Solo_GenerateStart(const char *yamlPath, const char *slotName, const char *presetId, int useOptions);
int AP_Solo_JobState(void);
const char *AP_Solo_JobPhase(void);
const char *AP_Solo_JobErrorText(void);   /* player-facing */
const char *AP_Solo_JobErrorOption(void); /* option to highlight, "" if none */
const char *AP_Solo_JobResultRoom(void);  /* room id after generate / import-room */
void AP_Solo_JobCancel(void);
void AP_Solo_JobClear(void);

/* The last list result. Starts: presets, then saved YAMLs, then inbox. */
int AP_Solo_RoomCount(void);
int AP_Solo_RoomGet(int index, struct APSoloRoomInfo *out);
int AP_Solo_StartCount(void);
int AP_Solo_StartGet(int index, struct APSoloStartInfo *out);
const char *AP_Solo_InboxPath(void);
int AP_Solo_RoomDelete(const char *roomId);

/* Options screen (describe result plus the shipped overlay). */
int AP_Solo_OptionCount(void);
int AP_Solo_OptionGet(int index, struct APSoloOptionInfo *out);
void AP_Solo_OptionStep(int index, int direction);
const char *AP_Solo_OptionHelp(int index);
int AP_Solo_OptionIndex(const char *name);
void AP_Solo_OptionsReset(void);          /* back to the preset's values */
/* A set option's checklist rows. */
int AP_Solo_OptionItemCount(int index);
int AP_Solo_OptionItemOnCount(int index); /* cached for large hint checklists */
int AP_Solo_OptionItemGet(int index, int item, struct APSoloItemInfo *out);
void AP_Solo_OptionItemToggle(int index, int item);
/* A weights option's number page (A3-22): step wraps 0 to the maximum,
   reset returns the row to the option's default weight. */
void AP_Solo_OptionItemStep(int index, int item, int direction);
void AP_Solo_OptionItemReset(int index, int item);
/* The first option a guard refuses (Continue opens it), or -1. */
int AP_Solo_OptionsBlocked(void);

/* Custom-track step. Cycle: off, in the pool, guaranteed a pad, off. */
int AP_Solo_TrackCount(void);
int AP_Solo_TrackGet(int index, struct APSoloTrackInfo *out);
void AP_Solo_TrackCycle(int index);
int AP_Solo_TracksPicked(void);
void AP_Solo_TracksClear(void);           /* a new start: the next list sets the picks */
/* Every YAML's Custom Tracks step sits behind its options screen. */
int AP_Solo_TracksViaOptions(int isPreset);

/* The client verified a content-plan package's files (#133): keep its
   descriptor in solo/tracks so the custom-track step can offer the track. */
int AP_Solo_RecordTrackDescriptor(const char *contentId, const char *revision, const char *json);
/* The xdelta importer installed a track whose laps nobody gave (ruling of
   2026-11-02): keep its descriptor, without laps, in solo/needs-laps. The
   custom-track step lists it and asks for the laps before it can be picked.
   An entry for the same track key and version in solo/tracks stays when it
   pins the same files (its laps were picked before); conflicting pins fail. */
int AP_Solo_RecordTrackNeedsLaps(const char *contentId, const char *revision, const char *json);
/* The importer replaced an old copy of a track (A3-20): drop the descriptor
   files its track key used, keeping the laps the player picked on the key of
   the copy that took its place. 1 when nothing is left to retire. */
int AP_Solo_RetireReplacedTrack(const char *oldKey, const char *revision, const char *uuid, const char *newKey);
/* Rebuild the descriptor index from verified package-store revisions. The
   optional pin publishes just one install; an empty pin rescans all of them. */
int AP_Solo_PublishInstalledStore(const char *pin, char *error, size_t errorSize);
/* On a row waiting for its laps: Left/Right change the laps shown, Keep writes
   the descriptor with them to solo/tracks (1) and drops the waiting copy.
   List the tracks again afterwards. */
void AP_Solo_TrackStepLaps(int index, int direction);
int AP_Solo_TrackKeepLaps(int index);

/* Import file dialog (SDL, answers on another thread). room 1: rooms,
   0: YAMLs. Take returns 1 with a path, -1 cancelled, -2 path too long,
   0 nothing yet. */
int AP_Solo_PickStart(int room);
int AP_Solo_PickTake(char *out, int size);

/* Session. */
/* Empty by default. Set before starting/resuming; a running server keeps its
   password until stopped. The setting lasts for this client process only. */
const char *AP_Solo_Password(void);
int AP_Solo_SetPassword(const char *password);
int AP_Solo_SessionStart(const char *roomId);
int AP_Solo_SessionState(void);
const char *AP_Solo_SessionError(void);
const char *AP_Solo_SessionRoom(void);
void AP_Solo_SessionStop(void);           /* /save, /exit, then end the child */
int AP_Solo_SessionActive(void);          /* starting, connecting, live or stopping */
int AP_Solo_SessionOwnsConnection(void);  /* starting, connecting or live: not stopping */

/* AP_Net_Reconnect from anything but the solo session ends the solo session. */
void AP_Solo_BeforeReconnect(void);

void AP_Solo_Tick(void);
/* Blocking stop for quit and process exit (at most about 5 seconds). */
void AP_Solo_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
