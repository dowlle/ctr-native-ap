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
};

enum APSoloOptionKind
{
	AP_SOLO_OPT_TOGGLE = 0,
	AP_SOLO_OPT_CHOICE,
	AP_SOLO_OPT_RANGE,
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
	char note[AP_SOLO_TEXT];    /* forced reason or a warning */
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
	int pick;                  /* enum APSoloTrackPick */
};

/* 1 when solo-runtime/ and its interpreter are present in the client folder. */
int AP_Solo_RuntimePresent(void);

/* Jobs. Start returns 0 when another job runs or the runtime is missing. */
int AP_Solo_ListStart(void);
/* The options screen's rows and their values in yamlPath (a preset). */
int AP_Solo_DescribeStart(const char *yamlPath);
int AP_Solo_ImportRoomStart(const char *path);
int AP_Solo_ImportYamlStart(const char *path);
/* Installed custom tracks (assets/tracks/<track key>/original) with a known
   descriptor. Picks survive a new list for the tracks still listed. */
int AP_Solo_TracksStart(void);
/* yamlPath: preset or saved/inbox YAML. Options from the options screen and
   the picked custom tracks are passed only for presets (useOptions 1). */
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

/* Custom-track step. Cycle: off, in the pool, guaranteed a pad, off. */
int AP_Solo_TrackCount(void);
int AP_Solo_TrackGet(int index, struct APSoloTrackInfo *out);
void AP_Solo_TrackCycle(int index);
int AP_Solo_TracksPicked(void);
void AP_Solo_TracksClear(void);           /* a new start: nothing picked */

/* The client verified a content-plan package's files (#133): keep its
   descriptor in solo/tracks so the custom-track step can offer the track. */
void AP_Solo_RecordTrackDescriptor(const char *contentId, const char *revision, const char *json);

/* Import file dialog (SDL, answers on another thread). room 1: rooms,
   0: YAMLs. Take returns 1 with a path, -1 cancelled, -2 path too long,
   0 nothing yet. */
int AP_Solo_PickStart(int room);
int AP_Solo_PickTake(char *out, int size);

/* Session. */
int AP_Solo_SessionStart(const char *roomId);
int AP_Solo_SessionState(void);
const char *AP_Solo_SessionError(void);
const char *AP_Solo_SessionRoom(void);
void AP_Solo_SessionStop(void);           /* /save, /exit, then end the child */
int AP_Solo_SessionActive(void);          /* starting, connecting, live or stopping */

/* AP_Net_Reconnect from anything but the solo session ends the solo session. */
void AP_Solo_BeforeReconnect(void);

void AP_Solo_Tick(void);
/* Blocking stop for quit and process exit (at most about 5 seconds). */
void AP_Solo_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
