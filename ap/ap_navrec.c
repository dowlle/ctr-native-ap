#ifdef CTR_AP

#include <common.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_navrec.h"
#include "ap_navrec_format.h"
#include "ap_navrec_label_logic.h"
#include "ap_navrec_lane_logic.h"
#include "ap_hooks.h"               // AP_LogLine, ctr_cfg
#include "ap_version.h"             // CTR_AP_VERSION
#include "platform/native_config.h" // g_config.navRecord / navUseRecorded / navDriverName

#if defined(_WIN32)
#include "platform/native_win32.h"
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// The container mirrors struct NavFrame so ap_navrec_format.h can stay free of
// engine headers. If the decomp ever changes NavFrame, this build breaks here
// rather than silently writing files nobody can read.
CTR_STATIC_ASSERT(sizeof(struct AP_NavRecNode) == sizeof(struct NavFrame));
CTR_STATIC_ASSERT(OFFSETOF(struct NavFrame, pos) == OFFSETOF(struct AP_NavRecNode, posX));
CTR_STATIC_ASSERT(OFFSETOF(struct NavFrame, rot) == OFFSETOF(struct AP_NavRecNode, rot));
CTR_STATIC_ASSERT(OFFSETOF(struct NavFrame, distToNextNavXYZ) == OFFSETOF(struct AP_NavRecNode, distToNextNavXYZ));
CTR_STATIC_ASSERT(OFFSETOF(struct NavFrame, distToNextNavXZ) == OFFSETOF(struct AP_NavRecNode, distToNextNavXZ));
CTR_STATIC_ASSERT(OFFSETOF(struct NavFrame, flags) == OFFSETOF(struct AP_NavRecNode, flags));
CTR_STATIC_ASSERT(OFFSETOF(struct NavFrame, pathChangeOpcode) == OFFSETOF(struct AP_NavRecNode, pathChangeOpcode));
CTR_STATIC_ASSERT(OFFSETOF(struct NavFrame, goBackCount) == OFFSETOF(struct AP_NavRecNode, goBackCount));
CTR_STATIC_ASSERT(OFFSETOF(struct NavFrame, specialBits) == OFFSETOF(struct AP_NavRecNode, specialBits));

// Everything this feature writes goes in ONE folder, so a player who changes
// their mind deletes the whole thing instead of hunting loose files out of the
// game directory. Created on demand, never on startup: with nav_record off the
// folder does not appear at all.
//
// The path is relative to the working directory, which is where this client
// already keeps config.ini and ap-config.txt. It is deliberately the same base
// as those, not "beside the executable": the two differ when the game is started
// from elsewhere, and a player looking for their recordings should find them
// next to the config file they already know about.
#define AP_NAVREC_DIR "ap-navpaths"

// Recordings are NUMBERED and never overwritten, so no run is ever destroyed by
// the next one. The reader takes the highest number present for the level.
#define AP_NAVREC_MAX_FILE_INDEX 999

// The loader's open budget, AP_NAVREC_MAX_LOAD_ATTEMPTS, lives with the
// selection rule in ap_navrec_lane_logic.h so the harness asserts the same
// number this build uses.

// Held-item ids, from the switch in VehPickupItem_ShootNow. Only these two are
// counted; the format records no other item.
#define AP_NAVREC_WEAPON_TURBO 0
#define AP_NAVREC_WEAPON_MASK  7

// Completed laps held at once. The writer keeps at most AP_NAVREC_MAX_LAPS, so
// banking more than that would buy nothing: a fourth completed lap either beats
// one of the three held or is discarded on the spot.
#define AP_NAVREC_BANK_LAPS AP_NAVREC_MAX_LAPS

// Sample rows: one per banked lap plus one for the lap in progress. The lap
// being driven never shares a row with a banked one, which is what lets a full
// bank evict its slowest lap without disturbing the drive.
#define AP_NAVREC_SAMPLE_ROWS (AP_NAVREC_BANK_LAPS + 1)
#define AP_NAVREC_LIVE_ROW    AP_NAVREC_BANK_LAPS

// Raw samples per lap, one per race frame. 4000 frames is over two minutes at
// 30 Hz, comfortably past any retail lap. A lap that overruns it is marked
// dirty rather than truncated silently.
#define AP_NAVREC_MAX_SAMPLES 4000

// Recording-only working set, allocated when nav_record is switched ON and
// released when it is switched off. It is the large half of this module and a
// player who never touches the option never pays for it.
struct AP_NavRecScratch
{
	struct AP_NavRecSample samples[AP_NAVREC_SAMPLE_ROWS][AP_NAVREC_MAX_SAMPLES];
	short                  corridorXZ[AP_NAVREC_GRID_POINTS * 2];
	struct AP_NavRecGrid   grid;
	struct AP_NavRecNode   nodes[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
	unsigned int           stamps[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
};

// Playback storage. This one CANNOT be allocated on demand: the engine keeps the
// pointers we hand it for as long as the race runs, and a free would pull the
// nav data out from under the AI. It is static for the process lifetime, which
// is also what lets the savestate relocation in native_checkpoint.c treat it as
// an image pointer.
//
// The union is the whole reason both types exist. The engine wants NavFrame; the
// freestanding format code wants its own mirror. Punning through a union is
// defined, a cast between two struct pointers is not, and the layout assertions
// above guarantee the two views agree byte for byte.
union AP_NavRecLane
{
	struct NavFrame      engine[AP_NAVREC_MAX_NODES];
	struct AP_NavRecNode wire[AP_NAVREC_MAX_NODES];
};

static union AP_NavRecLane s_navrecLane[AP_NAVREC_LANES];
static unsigned int        s_navrecLaneStamps[AP_NAVREC_LANES][AP_NAVREC_MAX_NODES];
static struct NavHeader    s_navrecLaneHeader[AP_NAVREC_LANES];
static unsigned int        s_navrecLaneNodes[AP_NAVREC_LANES];

// One candidate container, decoded. This CANNOT be the lane storage any more:
// the loader now reads several files per level, and decoding the second one into
// the lanes would overwrite lines the first one already owns while the engine is
// about to read them. It lives for the length of a load and is released before
// the race starts, so nothing persists between levels.
struct AP_NavRecLoadBuf
{
	struct AP_NavRecNode nodes[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
	unsigned int         stamps[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
};

struct AP_NavRecFetchCtx
{
	int                      levelID;
	struct AP_NavRecLoadBuf *buf;
};

static struct AP_NavRecScratch *s_navrecScratch;

// Mirrors g_config.navRecord. Starts at 0, the option's own default, so a build
// whose owner never touches the option does no work here at all: the first tick
// sees want == armed and returns before it can allocate, log or drop anything.
static int s_navrecArmed;
static int s_navrecLevelID = -1;
static int s_navrecLastLapIndex = -1;
static int s_navrecWrittenThisRace;
static int s_navrecEndSeen;

static unsigned int s_navrecBankCount;                            // banked laps
static unsigned int s_navrecBankFrames[AP_NAVREC_BANK_LAPS];      // lap length in frames
static unsigned int s_navrecBankSamples[AP_NAVREC_BANK_LAPS];     // samples actually stored
static int          s_navrecBankDirty[AP_NAVREC_BANK_LAPS];
static unsigned int s_navrecBankMaskFires[AP_NAVREC_BANK_LAPS];
static unsigned int s_navrecBankTurboFires[AP_NAVREC_BANK_LAPS];

static unsigned int s_navrecCurSamples;
static unsigned int s_navrecCurFrames;
static int          s_navrecCurDirty;
static unsigned int s_navrecCurMaskFires;
static unsigned int s_navrecCurTurboFires;

static unsigned int s_navrecCorridorPoints;
static int          s_navrecCorridorLevel = -1;
static int          s_navrecGridReady;
static unsigned int s_navrecTrackKind = AP_NAVREC_TRACK_NONAV;

static short s_navrecCharacterId = -1;

// Set while recorded lanes are the live nav data, with the level they belong to.
// Read only by the savestate restore path.
static int s_navrecLanesLive;
static int s_navrecLanesLevel = -1;

// Who each lane belongs to. Per lane, not per level: a race can now be driven on
// three different people's lines at once, and the name over a bot has to be the
// name on the file that bot is actually following. s_navrecLaneLapIndex is which
// lap of that file the lane took, or AP_NAVREC_LANE_SYNTH when the lane is a
// lateral offset of another one rather than a recorded line.
#define AP_NAVREC_LANE_SYNTH (-1)

static char         s_navrecLaneDriverName[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
static unsigned int s_navrecLaneFileIndex[AP_NAVREC_LANES];
static int          s_navrecLaneLapIndex[AP_NAVREC_LANES];

static unsigned char s_navrecActiveIdentityKind;
static unsigned char s_navrecActiveTrackUuid[AP_NAVREC_TRACK_UUID_BYTES];
static unsigned int  s_navrecActiveNavRevision;

// Set for a load that serves custom-track bytes without a usable navigation
// identity. Falling back to the retail interpretation there would let the
// borrowed host LevelID do exactly what NAV3 exists to prevent: retail
// recordings would inject onto custom geometry, and laps recorded here would
// be stamped as retail lines of the host slot. Blocked means neither happens:
// no recording loads, no recording is written, and the level's own lanes run.
static int s_navrecIdentityBlocked;

// Whether the lap currently being driven started with a forward crossing of
// the start line. The standing-start lap and post-reversal fragments do not,
// and are dropped at the boundary instead of banked; see
// AP_NavRecFormat_LapBoundaryBanks.
static int s_navrecLapOpenAtLine;

// ============================================================================
// Small helpers
// ============================================================================

void AP_NavRec_SetActiveCustomTrack(const unsigned char uuid[16], unsigned int navRevision)
{
	if (uuid == NULL)
	{
		AP_NavRec_ClearActiveCustomTrack();
		return;
	}
	s_navrecActiveIdentityKind = AP_NAVREC_IDENTITY_CUSTOM;
	memcpy(s_navrecActiveTrackUuid, uuid, AP_NAVREC_TRACK_UUID_BYTES);
	s_navrecActiveNavRevision = navRevision;
	s_navrecIdentityBlocked = 0;
}

void AP_NavRec_ClearActiveCustomTrack(void)
{
	s_navrecActiveIdentityKind = AP_NAVREC_IDENTITY_RETAIL;
	memset(s_navrecActiveTrackUuid, 0, sizeof s_navrecActiveTrackUuid);
	s_navrecActiveNavRevision = 0;
	s_navrecIdentityBlocked = 0;
}

void AP_NavRec_BlockRecordedLanes(void)
{
	// The blocked state keeps a retail-shaped identity in the statics so any
	// path that reads them without checking the flag matches nothing custom,
	// but the flag is what load and write actually honour.
	s_navrecActiveIdentityKind = AP_NAVREC_IDENTITY_RETAIL;
	memset(s_navrecActiveTrackUuid, 0, sizeof s_navrecActiveTrackUuid);
	s_navrecActiveNavRevision = 0;
	s_navrecIdentityBlocked = 1;
}

// The active identity, in the form the log lines use.
static void AP_NavRec_DescribeIdentity(unsigned char kind, const unsigned char *uuid, unsigned int revision, char *out, size_t cap)
{
	if (kind != AP_NAVREC_IDENTITY_CUSTOM)
	{
		snprintf(out, cap, "retail");
		return;
	}

	snprintf(out, cap,
	         "custom %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x rev %u",
	         uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
	         uuid[12], uuid[13], uuid[14], uuid[15], revision);
}

// A failed or partial load must not leave the previous track's contributors
// painted above bots now driving someone else's lines, or the level's own.
static void AP_NavRec_ClearLaneIdentity(void)
{
	int lane;

	for (lane = 0; lane < AP_NAVREC_LANES; lane++)
	{
		s_navrecLaneDriverName[lane][0] = '\0';
		s_navrecLaneFileIndex[lane] = 0;
		s_navrecLaneLapIndex[lane] = AP_NAVREC_LANE_SYNTH;
	}
}

// Same portable shape native_memcard.c uses for its own save directory, kept
// local rather than widening a platform API for one caller.
static int AP_NavRec_MakeDir(const char *path)
{
#if defined(_WIN32)
	if (CreateDirectoryA(path, NULL) != 0)
		return 1;
	return GetLastError() == ERROR_ALREADY_EXISTS;
#else
	if (mkdir(path, 0777) == 0)
		return 1;
	return errno == EEXIST;
#endif
}

static void AP_NavRec_PathForIndex(int levelID, unsigned int index, char *out, size_t cap)
{
	snprintf(out, cap, AP_NAVREC_DIR "/navpath-%d-%03u.navlap", levelID, index);
}

static int AP_NavRec_FileExists(const char *path)
{
	FILE *f = fopen(path, "rb");

	if (f == NULL)
		return 0;
	fclose(f);
	return 1;
}

// Create a NEW file, failing when one is already there.
//
// This is what actually keeps the promise that nothing is overwritten, and it
// has to be the create itself rather than a probe. fopen("wb") truncates
// whatever it finds, and an existence check followed by fopen leaves a window
// between the two. It also closes the hole in the probe: a file that exists but
// cannot be opened for reading, because it is locked or unreadable, looks absent
// to AP_NavRec_FileExists, and the caller would then have picked its number.
// Here the filesystem answers, not a guess.
static FILE *AP_NavRec_CreateExclusive(const char *path)
{
#if defined(_WIN32)
	HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	int    fd;
	FILE  *f;

	if (h == INVALID_HANDLE_VALUE)
		return NULL;

	fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_BINARY);
	if (fd < 0)
	{
		CloseHandle(h);
		return NULL;
	}

	// From here the descriptor owns the handle, so the handle is never closed
	// separately.
	f = _fdopen(fd, "wb");
	if (f == NULL)
		_close(fd);
	return f;
#else
	int   fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
	FILE *f;

	if (fd < 0)
		return NULL;

	f = fdopen(fd, "wb");
	if (f == NULL)
		close(fd);
	return f;
#endif
}

// Cached highest index, per level. Probing 999 paths is cheap but not free, and
// a savestate restore also asks for it, so the answer is remembered and only
// recomputed when the level changes or the recorder writes a file.
//
// The cache can go stale if a file is dropped into the folder while the game is
// running. That is accepted: the next level change picks it up, and the writer
// never relies on the cache for correctness because its create is exclusive.
static int          s_navrecIndexLevel = -1;
static unsigned int s_navrecIndexHighest;

// The highest numbered recording present for this level, or 0 when there is
// none. The whole range is probed rather than stopping at the first gap, so a
// player who deletes a file out of the middle does not hide everything above it.
static unsigned int AP_NavRec_HighestIndex(int levelID)
{
	unsigned int highest = 0;
	unsigned int i;
	char         path[160];

	if (s_navrecIndexLevel == levelID)
		return s_navrecIndexHighest;

	for (i = 1; i <= AP_NAVREC_MAX_FILE_INDEX; i++)
	{
		AP_NavRec_PathForIndex(levelID, i, path, sizeof path);
		if (AP_NavRec_FileExists(path))
			highest = i;
	}

	s_navrecIndexLevel = levelID;
	s_navrecIndexHighest = highest;
	return highest;
}

// The name written into a file. The option wins; an empty option falls back to
// the configured Archipelago slot name, which is the identity the player already
// hands out. Both go through the same sanitizer, so nothing unprintable reaches
// a file whichever way it arrived.
static void AP_NavRec_ResolveDriverName(char *out, unsigned int cap)
{
	AP_NavRecFormat_SanitizeName(g_config.navDriverName, out, cap);
	if (out[0] != '\0')
		return;
	AP_NavRecFormat_SanitizeName(g_config.slot, out, cap);
}

// Declared shortcut-knowledge tier of the connected seed. schema_version 0 is
// the engine's own "no slot data parsed" state, so it maps to unknown rather
// than to easy.
static unsigned char AP_NavRec_ShortcutTier(void)
{
	if (ctr_cfg.schema_version == 0)
		return 0;
	if ((ctr_cfg.shortcut_knowledge < 0) || (ctr_cfg.shortcut_knowledge > 2))
		return 0;
	return (unsigned char)(ctr_cfg.shortcut_knowledge + 1);
}

// ============================================================================
// Arming
// ============================================================================

// Reset the lap in progress without touching the bank. Used at a boundary
// whose finished samples are not a closed lap: the standing-start lap or a
// post-reversal fragment.
static void AP_NavRec_DropCurrentLap(void)
{
	s_navrecCurSamples = 0;
	s_navrecCurFrames = 0;
	s_navrecCurDirty = 0;
	s_navrecCurMaskFires = 0;
	s_navrecCurTurboFires = 0;
}

static void AP_NavRec_DropBank(void)
{
	s_navrecBankCount = 0;
	AP_NavRec_DropCurrentLap();
	s_navrecLastLapIndex = -1;
	s_navrecWrittenThisRace = 0;
	s_navrecEndSeen = 0;
	s_navrecLapOpenAtLine = 0;
}

// The option is re-read EVERY tick rather than latched. Latching it would mean
// unticking "Save AI Lap Recordings" mid-session left the recorder running until
// the player restarted, which is exactly the surprise the option exists to
// prevent. An option about writing to someone's disk has to take effect when
// they turn it OFF, not only when they turn it on.
static void AP_NavRec_SyncArmed(void)
{
	const int want = g_config.navRecord ? 1 : 0;
	char      msg[224];

	if (want == s_navrecArmed)
		return;

	if (want)
	{
		s_navrecScratch = (struct AP_NavRecScratch *)malloc(sizeof *s_navrecScratch);
		if (s_navrecScratch == NULL)
		{
			// Leave s_navrecArmed alone so the next tick retries. The option is
			// on; the recorder simply has nowhere to put samples yet.
			AP_LogLine("[AP NAVREC] cannot arm: out of memory for the sample bank\n");
			return;
		}

		char name[AP_NAVREC_NAME_CHARS + 1];
		AP_NavRec_ResolveDriverName(name, sizeof name);

		s_navrecArmed = 1;
		AP_NavRec_DropBank();
		s_navrecCorridorLevel = -1;
		s_navrecGridReady = 0;

		snprintf(msg, sizeof msg, "[AP NAVREC] recording armed. driver \"%s\", up to %d laps kept per race, files under %s/\n",
		         (name[0] != '\0') ? name : "(anonymous)", AP_NAVREC_MAX_LAPS, AP_NAVREC_DIR);
		AP_LogLine(msg);
		return;
	}

	// Turning recording off is withdrawing consent. Holding banked laps in
	// memory to write later would not honour that, so they go with the buffer.
	s_navrecArmed = 0;
	AP_NavRec_DropBank();
	free(s_navrecScratch);
	s_navrecScratch = NULL;
	s_navrecCorridorLevel = -1;
	s_navrecGridReady = 0;
	AP_LogLine("[AP NAVREC] recording disarmed; banked laps discarded\n");
}

// ============================================================================
// Retail corridor snapshot, for the shortcut flag
// ============================================================================

// level1->LevNavTable is the LEV's own data and is never written by the
// injection path, which only replaces pointers in sdata. So this snapshot is the
// retail line even in a session that is also playing recorded laps back.
static void AP_NavRec_SnapshotCorridor(struct GameTracker *gGT, int levelID)
{
	struct NavHeader **table;
	unsigned int       points = 0;
	char               msg[192];
	int                lane;

	s_navrecCorridorLevel = levelID;
	s_navrecCorridorPoints = 0;
	s_navrecGridReady = 0;
	s_navrecTrackKind = AP_NAVREC_TRACK_NONAV;

	if ((s_navrecScratch == NULL) || (gGT->level1 == NULL))
		return;

	table = gGT->level1->LevNavTable;
	if (table == NULL)
	{
		AP_LogLine("[AP NAVREC] this level ships no nav table, so laps record with shortcut=unknown\n");
		return;
	}

	for (lane = 0; lane < AP_NAVREC_LANES; lane++)
	{
		const struct NavHeader *nh = table[lane];
		const struct NavFrame  *frames;
		int                     n;

		if (nh == NULL)
			continue;
		if (nh->magicNumber != (s16)-0x1303)
			continue;

		n = (int)nh->numPoints;
		if (n <= 0)
			continue;

		frames = NAVHEADER_GETFRAME(nh);
		for (int i = 0; (i < n) && (points < AP_NAVREC_GRID_POINTS); i++)
		{
			s_navrecScratch->corridorXZ[points * 2u] = frames[i].pos.x;
			s_navrecScratch->corridorXZ[(points * 2u) + 1u] = frames[i].pos.z;
			points++;
		}
	}

	s_navrecCorridorPoints = points;
	if (points > 0)
	{
		s_navrecTrackKind = AP_NAVREC_TRACK_RETAIL;
		s_navrecGridReady = AP_NavRecFormat_GridBuild(&s_navrecScratch->grid, s_navrecScratch->corridorXZ, points);
	}

	snprintf(msg, sizeof msg, "[AP NAVREC] retail corridor for level %d: %u node(s), grid %dx%d cell %d\n", levelID, points,
	         s_navrecGridReady ? s_navrecScratch->grid.nx : 0, s_navrecGridReady ? s_navrecScratch->grid.nz : 0,
	         s_navrecGridReady ? s_navrecScratch->grid.cell : 0);
	AP_LogLine(msg);
}

// ============================================================================
// Banking
// ============================================================================

static void AP_NavRec_BankLap(void)
{
	char         msg[192];
	unsigned int slot;

	if (s_navrecScratch == NULL)
		return;

	if (s_navrecCurSamples < 16u)
	{
		AP_NavRec_DropCurrentLap();
		return;
	}

	slot = s_navrecBankCount;
	if (slot >= AP_NAVREC_BANK_LAPS)
	{
		// Bank full. Evict the SLOWEST banked lap rather than the oldest: the
		// writer keeps the fastest laps anyway, so the slowest is the one that
		// was never going to be written. A dirty lap is always evicted first,
		// because it can never be written at all.
		unsigned int worst = 0;
		unsigned int i;

		for (i = 1; i < AP_NAVREC_BANK_LAPS; i++)
		{
			if (s_navrecBankDirty[i] && !s_navrecBankDirty[worst])
			{
				worst = i;
				continue;
			}
			if (s_navrecBankDirty[worst] && !s_navrecBankDirty[i])
				continue;
			if (s_navrecBankFrames[i] > s_navrecBankFrames[worst])
				worst = i;
		}

		if (!s_navrecBankDirty[worst] && (s_navrecCurDirty || (s_navrecBankFrames[worst] <= s_navrecCurFrames)))
		{
			// Everything banked is clean and already at least as good as the lap
			// just finished.
			AP_LogLine("[AP NAVREC] lap bank full and this lap is not an improvement, dropping it\n");
			AP_NavRec_DropCurrentLap();
			return;
		}

		slot = worst;
	}
	else
	{
		s_navrecBankCount++;
	}

	// The lap was driven into the live row, which is never a banked row, so this
	// copy can never overwrite a lap that is still wanted.
	memcpy(s_navrecScratch->samples[slot], s_navrecScratch->samples[AP_NAVREC_LIVE_ROW],
	       sizeof(struct AP_NavRecSample) * (size_t)s_navrecCurSamples);

	s_navrecBankFrames[slot] = s_navrecCurFrames;
	s_navrecBankSamples[slot] = s_navrecCurSamples;
	s_navrecBankDirty[slot] = s_navrecCurDirty;
	s_navrecBankMaskFires[slot] = s_navrecCurMaskFires;
	s_navrecBankTurboFires[slot] = s_navrecCurTurboFires;

	snprintf(msg, sizeof msg, "[AP NAVREC] banked lap: %u sample(s), %u frame(s), mask %u turbo %u, %s\n", s_navrecCurSamples, s_navrecCurFrames,
	         s_navrecCurMaskFires, s_navrecCurTurboFires, s_navrecCurDirty ? "DIRTY (respawn, hit or crash), will not be written" : "clean");
	AP_LogLine(msg);

	AP_NavRec_DropCurrentLap();
}

// ============================================================================
// Writing
// ============================================================================

// Clean laps only, fastest first, at most AP_NAVREC_MAX_LAPS of them. A lap with
// a respawn, a blast, a spin or a wall crash is not a line worth copying and
// never reaches a file. `out` must hold AP_NAVREC_BANK_LAPS entries.
static unsigned int AP_NavRec_SelectLaps(unsigned int *out)
{
	unsigned int chosen = 0;
	unsigned int i;
	unsigned int j;

	for (i = 0; i < s_navrecBankCount; i++)
	{
		if (!s_navrecBankDirty[i])
			out[chosen++] = i;
	}

	// Selection sort by lap time. At most three elements, so the simplest sort
	// that is obviously correct is the right one.
	for (i = 0; (chosen > 0u) && (i < chosen - 1u); i++)
	{
		unsigned int best = i;

		for (j = i + 1u; j < chosen; j++)
		{
			if (s_navrecBankFrames[out[j]] < s_navrecBankFrames[out[best]])
				best = j;
		}

		if (best != i)
		{
			unsigned int tmp = out[i];
			out[i] = out[best];
			out[best] = tmp;
		}
	}

	return (chosen > AP_NAVREC_MAX_LAPS) ? (unsigned int)AP_NAVREC_MAX_LAPS : chosen;
}

static void AP_NavRec_Write(int levelID)
{
	unsigned int                order[AP_NAVREC_BANK_LAPS];
	struct AP_NavRecLapInfo     laps[AP_NAVREC_MAX_LAPS];
	const struct AP_NavRecNode *nodePtrs[AP_NAVREC_MAX_LAPS];
	const unsigned int         *stampPtrs[AP_NAVREC_MAX_LAPS];
	struct AP_NavRecMeta        meta;
	char                        name[AP_NAVREC_NAME_CHARS + 1];
	char                        path[160];
	char                        msg[512];
	unsigned int                selected;
	unsigned int                written = 0;
	unsigned int                i;
	unsigned int                size = 0;
	unsigned int                index;
	unsigned char              *buf;
	FILE                       *f;
	int                         rc;

	// Belt and braces. The caller is already gated on the option, but this is the
	// only function in the build that creates files on a player's disk, so it
	// re-checks rather than trusting its caller.
	if (!g_config.navRecord || (s_navrecScratch == NULL))
		return;

	// A served custom load without a navigation identity cannot stamp a lap
	// truthfully: written as retail it would later inject onto the host slot's
	// real retail races. Nothing is saved, and the reason is on record.
	if (s_navrecIdentityBlocked)
	{
		AP_LogLine("[AP NAVREC] recording disabled for this load: the served custom track supplies no navigation identity\n");
		return;
	}

	selected = AP_NavRec_SelectLaps(order);
	if (selected == 0)
	{
		AP_LogLine("[AP NAVREC] no clean lap to write; nothing saved\n");
		return;
	}

	for (i = 0; i < selected; i++)
	{
		unsigned int lap = order[i];
		unsigned int nodes;

		nodes = AP_NavRecFormat_Decimate(s_navrecScratch->samples[lap], s_navrecBankSamples[lap], AP_NAVREC_TARGET_NODES,
		                                 s_navrecScratch->nodes[written], s_navrecScratch->stamps[written]);
		if (nodes == 0)
		{
			snprintf(msg, sizeof msg, "[AP NAVREC] lap %u could not be decimated, skipping it\n", lap);
			AP_LogLine(msg);
			continue;
		}

		// The banking discipline should make this unreachable, but this is the
		// last gate before a lap reaches someone's disk, and the loader applies
		// the same rule, so a lap this build refuses to drive is a lap it also
		// refuses to publish.
		if (!AP_NavRecFormat_LapClosed(s_navrecScratch->nodes[written], nodes))
		{
			snprintf(msg, sizeof msg, "[AP NAVREC] lap %u is not a closed loop, skipping it\n", lap);
			AP_LogLine(msg);
			continue;
		}

		laps[written].nodeCount = nodes;
		laps[written].lapFrames = s_navrecBankFrames[lap];
		laps[written].sampleCount = s_navrecBankSamples[lap];
		laps[written].clean = 1;
		laps[written].shortcut =
		    (unsigned char)(s_navrecGridReady
		                        ? AP_NavRecFormat_ClassifyShortcut(s_navrecScratch->nodes[written], nodes, &s_navrecScratch->grid,
		                                                           s_navrecScratch->corridorXZ)
		                        : AP_NAVREC_SHORTCUT_UNKNOWN);
		laps[written].laneHint = (unsigned char)written;
		laps[written].maskFires = (unsigned short)((s_navrecBankMaskFires[lap] > 0xFFFFu) ? 0xFFFFu : s_navrecBankMaskFires[lap]);
		laps[written].turboFires = (unsigned short)((s_navrecBankTurboFires[lap] > 0xFFFFu) ? 0xFFFFu : s_navrecBankTurboFires[lap]);

		nodePtrs[written] = s_navrecScratch->nodes[written];
		stampPtrs[written] = s_navrecScratch->stamps[written];
		written++;
	}

	if (written == 0)
	{
		AP_LogLine("[AP NAVREC] every clean lap failed decimation; nothing saved\n");
		return;
	}

	AP_NavRec_ResolveDriverName(name, sizeof name);

	memset(&meta, 0, sizeof meta);
	meta.levelId = levelID;
	meta.clientVersion = CTR_AP_VERSION;
	meta.driverName = name;
	meta.characterId = s_navrecCharacterId;
	meta.difficultyPreset = (short)g_config.aiDifficulty;
	meta.shortcutTier = AP_NavRec_ShortcutTier();
	meta.trackKind = s_navrecTrackKind;
	meta.identityKind = s_navrecActiveIdentityKind;
	memcpy(meta.trackUuid, s_navrecActiveTrackUuid, sizeof meta.trackUuid);
	meta.navRevision = s_navrecActiveNavRevision;

	size = AP_NavRecFormat_SizeForMeta(&meta, laps, written);
	buf = (unsigned char *)malloc(size);
	if (buf == NULL)
	{
		AP_LogLine("[AP NAVREC] out of memory while serialising; nothing saved\n");
		return;
	}

	rc = AP_NavRecFormat_Write(buf, size, &meta, laps, written, nodePtrs, stampPtrs, &size);
	if (rc != AP_NAVREC_OK)
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] serialise failed: %s\n", AP_NavRecFormat_ErrorText(rc));
		AP_LogLine(msg);
		free(buf);
		return;
	}

	if (!AP_NavRec_MakeDir(AP_NAVREC_DIR))
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] cannot create the directory \"%s\"; nothing saved\n", AP_NAVREC_DIR);
		AP_LogLine(msg);
		free(buf);
		return;
	}

	// Next free number, taken by EXCLUSIVE create rather than by asking whether a
	// file is there. Nothing is ever overwritten: a run the player wants to keep
	// cannot be destroyed by the next race on the same track. If the number is
	// taken, step to the next one and try again, so a stale cache or a file that
	// appeared underneath us costs a retry rather than someone's recording.
	f = NULL;
	index = AP_NavRec_HighestIndex(levelID) + 1u;

	while (index <= AP_NAVREC_MAX_FILE_INDEX)
	{
		AP_NavRec_PathForIndex(levelID, index, path, sizeof path);

		f = AP_NavRec_CreateExclusive(path);
		if (f != NULL)
			break;

		index++;
	}

	if (f == NULL)
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] no free slot for level %d under \"%s\" (cap %d); nothing saved\n", levelID, AP_NAVREC_DIR,
		         AP_NAVREC_MAX_FILE_INDEX);
		AP_LogLine(msg);
		free(buf);
		return;
	}

	// The cache now knows the truth for this level without another scan.
	s_navrecIndexLevel = levelID;
	s_navrecIndexHighest = index;

	if (fwrite(buf, 1, size, f) != size)
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] short write to \"%s\"; the file is not usable\n", path);
		AP_LogLine(msg);
	}
	else
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] wrote \"%s\": %u lap(s), %u bytes, driver \"%s\"\n", path, written, size,
		         (name[0] != '\0') ? name : "(anonymous)");
		AP_LogLine(msg);

		for (i = 0; i < written; i++)
		{
			const char *sc = (laps[i].shortcut == AP_NAVREC_SHORTCUT_YES)
			                     ? "shortcut"
			                     : ((laps[i].shortcut == AP_NAVREC_SHORTCUT_UNKNOWN) ? "shortcut unknown" : "no shortcut");
			snprintf(msg, sizeof msg, "[AP NAVREC]   lap %u: %u nodes, %u frames, mask %u, turbo %u, %s\n", i, laps[i].nodeCount,
			         laps[i].lapFrames, (unsigned int)laps[i].maskFires, (unsigned int)laps[i].turboFires, sc);
			AP_LogLine(msg);
		}
	}

	fclose(f);
	free(buf);
}

// ============================================================================
// Reading and lane assembly
// ============================================================================

static void AP_NavRec_PublishLane(int lane, unsigned int nodes)
{
	s_navrecLaneNodes[lane] = nodes;

	memset(&s_navrecLaneHeader[lane], 0, sizeof s_navrecLaneHeader[lane]);
	s_navrecLaneHeader[lane].magicNumber = (s16)-0x1303; // what BOTS_InitNavPath validates
	s_navrecLaneHeader[lane].numPoints = (s16)nodes;
	s_navrecLaneHeader[lane].posY_firstNode = (int)s_navrecLane[lane].engine[0].pos.y;
	s_navrecLaneHeader[lane].last = &s_navrecLane[lane].engine[nodes];
}

// Read and validate ONE container file into `into`. Returns 1 when the file is
// usable and `info` describes it, 0 when it is missing or was rejected. Rejection
// is still whole-file: nothing here salvages part of a bad container, and no lane
// is touched either way.
static int AP_NavRec_ReadCandidate(const char *path, int levelID, struct AP_NavRecLoadBuf *into, struct AP_NavRecFileInfo *info)
{
	char                     msg[512];
	FILE                    *f;
	long                     len;
	unsigned char           *buf;
	unsigned int             size;
	int                      rc;

	f = fopen(path, "rb");
	if (f == NULL)
		return 0;

	if ((fseek(f, 0, SEEK_END) != 0) || ((len = ftell(f)) < 0) || (fseek(f, 0, SEEK_SET) != 0))
	{
		fclose(f);
		snprintf(msg, sizeof msg, "[AP NAVREC] load: cannot measure \"%s\"\n", path);
		AP_LogLine(msg);
		return 0;
	}

	if (((unsigned long)len) > AP_NAVREC_MAX_FILE_BYTES)
	{
		fclose(f);
		snprintf(msg, sizeof msg, "[AP NAVREC] load: \"%s\" is larger than any valid container\n", path);
		AP_LogLine(msg);
		return 0;
	}

	size = (unsigned int)len;
	buf = (unsigned char *)malloc((size > 0u) ? size : 1u);
	if (buf == NULL)
	{
		fclose(f);
		AP_LogLine("[AP NAVREC] load: out of memory\n");
		return 0;
	}

	if (fread(buf, 1, size, f) != size)
	{
		fclose(f);
		free(buf);
		snprintf(msg, sizeof msg, "[AP NAVREC] load: short read on \"%s\"\n", path);
		AP_LogLine(msg);
		return 0;
	}
	fclose(f);

	// A valid file lands in the load buffer, never straight in the lanes: those
	// belong to files this one may not replace. Read() forces the node fields
	// that can hang the engine, rejects a lap whose goBackCount never varies, and
	// sanitizes the name, so nothing below has to trust the file.
	rc = AP_NavRecFormat_Read(buf, size, info, into->nodes, into->stamps);
	free(buf);

	if (rc != AP_NAVREC_OK)
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] load: rejecting \"%s\": %s\n", path, AP_NavRecFormat_ErrorText(rc));
		AP_LogLine(msg);
		return 0;
	}

	if (info->levelId != levelID)
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] load: \"%s\" was recorded on level %d, not %d\n", path, info->levelId, levelID);
		AP_LogLine(msg);
		return 0;
	}

	if (!AP_NavRecFormat_IdentityMatches(info, levelID, s_navrecActiveIdentityKind,
	                                    s_navrecActiveTrackUuid, s_navrecActiveNavRevision))
	{
		char fileIdent[80];
		char activeIdent[80];

		AP_NavRec_DescribeIdentity(info->identityKind, info->trackUuid, info->navRevision, fileIdent, sizeof fileIdent);
		AP_NavRec_DescribeIdentity(s_navrecActiveIdentityKind, s_navrecActiveTrackUuid, s_navrecActiveNavRevision, activeIdent,
		                           sizeof activeIdent);
		snprintf(msg, sizeof msg, "[AP NAVREC] load: rejecting \"%s\": recorded as %s, this load is %s\n", path, fileIdent, activeIdent);
		AP_LogLine(msg);
		return 0;
	}

	return 1;
}

// Give one lane a recorded lap out of the load buffer, and the name of the file
// it came from.
//
// Read() has already applied the format's printable-ASCII/length sanitizer, so
// the retained name is the exact accepted identity and no draw path ever reopens
// or reparses a community file.
static void AP_NavRec_CommitLane(int lane, const struct AP_NavRecLoadBuf *from, const struct AP_NavRecFileInfo *info, unsigned int lap,
                                 unsigned int fileIndex)
{
	unsigned int nodes = info->laps[lap].nodeCount;

	memcpy(s_navrecLane[lane].wire, from->nodes[lap], sizeof(struct AP_NavRecNode) * nodes);
	memcpy(s_navrecLaneStamps[lane], from->stamps[lap], sizeof(unsigned int) * nodes);

	memset(s_navrecLaneDriverName[lane], 0, sizeof s_navrecLaneDriverName[lane]);
	memcpy(s_navrecLaneDriverName[lane], info->driverName, strlen(info->driverName));
	s_navrecLaneFileIndex[lane] = fileIndex;
	s_navrecLaneLapIndex[lane] = (int)lap;

	AP_NavRec_PublishLane(lane, nodes);
}

// Fill a lane its file could not, by displacing another lane sideways and
// alternating side.
//
// One recorded lap still yields three usable lanes; the bots stack less than
// they would on a single line and they stay on the road, because the offset is a
// fixed lateral step rather than an unbounded guess. goBackCount comes across
// untouched, so the synthesised lane keeps the checkpoint progression the
// killplane rewind needs. The name comes across too: the line is still that
// contributor's, moved over.
static void AP_NavRec_SynthLaneFrom(int lane, int srcLane)
{
	int          lateral = ((lane & 1) != 0) ? AP_NAVREC_FALLBACK_LANE_OFFSET : -AP_NAVREC_FALLBACK_LANE_OFFSET;
	unsigned int nodes = s_navrecLaneNodes[srcLane];

	AP_NavRecFormat_SynthLane(s_navrecLane[srcLane].wire, nodes, lateral, s_navrecLane[lane].wire);
	memcpy(s_navrecLaneStamps[lane], s_navrecLaneStamps[srcLane], sizeof(unsigned int) * nodes);

	memcpy(s_navrecLaneDriverName[lane], s_navrecLaneDriverName[srcLane], sizeof s_navrecLaneDriverName[lane]);
	s_navrecLaneFileIndex[lane] = s_navrecLaneFileIndex[srcLane];
	s_navrecLaneLapIndex[lane] = AP_NAVREC_LANE_SYNTH;

	AP_NavRec_PublishLane(lane, nodes);
}

// The candidate source AP_NavRecLane_Select scans. Returns 0 for a gap, which
// costs nothing, and 1 for a file that was opened, whether or not it survived
// validation.
static int AP_NavRec_FetchCandidate(void *ctx, unsigned int fileIndex, struct AP_NavRecLaneCandidate *out)
{
	struct AP_NavRecFetchCtx *c = (struct AP_NavRecFetchCtx *)ctx;
	struct AP_NavRecFileInfo  info;
	unsigned int              closed[AP_NAVREC_MAX_LAPS];
	unsigned int              closedCount;
	char                      path[160];
	char                      msg[224];

	AP_NavRec_PathForIndex(c->levelID, fileIndex, path, sizeof path);
	if (!AP_NavRec_FileExists(path))
		return 0;

	out->fileIndex = fileIndex;

	if (!AP_NavRec_ReadCandidate(path, c->levelID, c->buf, &info))
		return 1;

	// Only laps that are closed loops count. A file with none is passed over
	// like any other rejected candidate, so an older good file can still fill
	// the lanes; the laps a lane may take are exactly the ones the placement
	// pass will pick through the same AP_NavRecFormat_ClosedLaps mapping.
	closedCount = AP_NavRecFormat_ClosedLaps(&info, c->buf->nodes, closed);
	if (closedCount == 0)
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] load: \"%s\" holds no closed lap; skipping it\n", path);
		AP_LogLine(msg);
		return 1;
	}

	out->usable = 1;
	out->lapCount = closedCount;
	memcpy(out->driverName, info.driverName, sizeof out->driverName);
	return 1;
}

// Fill the three engine lanes for this level from up to three DISTINCT usable
// containers, preferring a different author for each.
//
// Newest FIRST, then downward. A rejected file is rejected whole, which is the
// format's rule and stays true, but refusing to look any further would let one
// corrupt newest recording hide every good older one, and the numbering scheme
// means those older ones are exactly what the player still has. Falling back is
// choosing a different file, not salvaging a bad one. Every rejection is logged
// with its reason, so a player can see which file is the problem.
//
// The scan runs twice over the chosen files: once to decide, once to place. A
// container is up to 74 KiB and there are at most three of them, so paying for a
// second read at a level load buys a loader that never holds a half-built lane
// set, and never needs a second decode buffer to avoid it.
static int AP_NavRec_LoadForLevel(int levelID)
{
	struct AP_NavRecFetchCtx fetchCtx;
	struct AP_NavRecFileInfo info;
	struct AP_NavRecLoadBuf *buf;
	char                     author[AP_NAVREC_LANES][AP_NAVREC_NAME_FIELD + 1];
	char                     path[160];
	char                     msg[384]; // holds a full path plus a driver name and a client version
	unsigned int             chosen[AP_NAVREC_LANES];
	unsigned int             laneFile[AP_NAVREC_LANES];
	unsigned int             laneLap[AP_NAVREC_LANES];
	int                      lanePublished[AP_NAVREC_LANES];
	unsigned int             chosenCount;
	unsigned int             published = 0;
	unsigned int             highest;
	unsigned int             opened = 0;
	unsigned int             c;
	int                      lane;
	int                      firstPublished = -1;

	// Read-only, and gated on its OWN option: someone who wants recorded lines
	// should not have to switch on the half that writes to their disk.
	if (!g_config.navUseRecorded)
		return 0;

	AP_NavRec_ClearLaneIdentity();

	// A served custom load without a navigation identity loads NOTHING. The
	// retail interpretation would match on the borrowed host LevelID and put
	// retail lines under bots on custom geometry; the level's own lanes are the
	// only source proven to belong to what is on screen.
	if (s_navrecIdentityBlocked)
	{
		AP_LogLine("[AP NAVREC] recorded lanes disabled for this load: the served custom track supplies no navigation identity; "
		           "the level's own lanes stay\n");
		return 0;
	}

	highest = AP_NavRec_HighestIndex(levelID);
	if (highest == 0)
		return 0;

	buf = (struct AP_NavRecLoadBuf *)malloc(sizeof *buf);
	if (buf == NULL)
	{
		AP_LogLine("[AP NAVREC] load: out of memory for the decode buffer\n");
		return 0;
	}

	fetchCtx.levelID = levelID;
	fetchCtx.buf = buf;

	memset(author, 0, sizeof author);
	chosenCount = AP_NavRecLane_Select(highest, AP_NAVREC_MAX_LOAD_ATTEMPTS, AP_NavRec_FetchCandidate, &fetchCtx, chosen, author, &opened);

	if (chosenCount == 0)
	{
		free(buf);
		if (opened >= AP_NAVREC_MAX_LOAD_ATTEMPTS)
			snprintf(msg, sizeof msg, "[AP NAVREC] gave up after %d rejected recording(s) for level %d\n", AP_NAVREC_MAX_LOAD_ATTEMPTS,
			         levelID);
		else
			snprintf(msg, sizeof msg, "[AP NAVREC] no usable recording for level %d\n", levelID);
		AP_LogLine(msg);
		return 0;
	}

	AP_NavRecLane_Plan(chosenCount, laneFile, laneLap);
	for (lane = 0; lane < AP_NAVREC_LANES; lane++)
		lanePublished[lane] = 0;

	for (c = 0; c < chosenCount; c++)
	{
		unsigned int closed[AP_NAVREC_MAX_LAPS];
		unsigned int closedCount;

		AP_NavRec_PathForIndex(levelID, chosen[c], path, sizeof path);

		// A file that vanished or turned unreadable between the two passes leaves
		// its lanes unpublished; they are filled from a lane that did load, below.
		if (!AP_NavRec_ReadCandidate(path, levelID, buf, &info))
			continue;

		// The same mapping the selection pass used: a lane only ever takes a lap
		// that is a closed loop, and the lap indices stay in file order so a file
		// whose laps all pass commits byte for byte as it always has.
		closedCount = AP_NavRecFormat_ClosedLaps(&info, buf->nodes, closed);

		snprintf(msg, sizeof msg, "[AP NAVREC] loaded \"%s\": driver \"%s\", %u recorded lap(s), %u closed, client %s\n", path,
		         (info.driverName[0] != '\0') ? info.driverName : "(anonymous)", info.lapCount, closedCount, info.clientVersion);
		AP_LogLine(msg);

		if (closedCount < info.lapCount)
		{
			unsigned int lap;

			for (lap = 0; lap < info.lapCount; lap++)
			{
				unsigned int k;
				int          isClosed = 0;

				for (k = 0; k < closedCount; k++)
				{
					if (closed[k] == lap)
						isClosed = 1;
				}

				if (!isClosed)
				{
					snprintf(msg, sizeof msg,
					         "[AP NAVREC]   lap %u is not a closed loop (its wrap would jump at the finish line); not eligible\n", lap);
					AP_LogLine(msg);
				}
			}
		}

		for (lane = 0; lane < AP_NAVREC_LANES; lane++)
		{
			if (laneFile[lane] != c)
				continue;
			if (laneLap[lane] >= closedCount)
				continue;

			AP_NavRec_CommitLane(lane, buf, &info, closed[laneLap[lane]], chosen[c]);
			lanePublished[lane] = 1;
			if (firstPublished < 0)
				firstPublished = lane;
			published++;
		}
	}

	free(buf);

	if (published == 0)
	{
		snprintf(msg, sizeof msg, "[AP NAVREC] no usable recording for level %d\n", levelID);
		AP_LogLine(msg);
		return 0;
	}

	// Lanes with no line of their own: a file that ran out of laps, or one that
	// stopped being readable. Prefer that file's own first lane, so the offset is
	// a displacement of the line it belongs beside.
	for (lane = 0; lane < AP_NAVREC_LANES; lane++)
	{
		int src;

		if (lanePublished[lane])
			continue;

		src = (int)laneFile[lane];
		if (!lanePublished[src])
			src = firstPublished;

		AP_NavRec_SynthLaneFrom(lane, src);
		lanePublished[lane] = 1;
	}

	return 1;
}

// One line per level load: which file and which of its laps each lane drives,
// and whose name the bots on it will carry.
static void AP_NavRec_LogLaneSummary(int levelID, const char *what)
{
	char msg[512];
	char ident[80];
	int  at;
	int  lane;

	AP_NavRec_DescribeIdentity(s_navrecActiveIdentityKind, s_navrecActiveTrackUuid, s_navrecActiveNavRevision, ident, sizeof ident);
	at = snprintf(msg, sizeof msg, "[AP NAVREC] recorded lanes %s on level %d (identity %s):", what, levelID, ident);

	for (lane = 0; (lane < AP_NAVREC_LANES) && (at > 0) && (at < (int)sizeof msg); lane++)
	{
		char lap[16];

		if (s_navrecLaneLapIndex[lane] == AP_NAVREC_LANE_SYNTH)
			snprintf(lap, sizeof lap, "offset");
		else
			snprintf(lap, sizeof lap, "lap %d", s_navrecLaneLapIndex[lane]);

		at += snprintf(msg + at, sizeof msg - (size_t)at, " lane %d = %03u %s \"%s\" %u node(s);", lane, s_navrecLaneFileIndex[lane], lap,
		               (s_navrecLaneDriverName[lane][0] != '\0') ? s_navrecLaneDriverName[lane] : "(anonymous)", s_navrecLaneNodes[lane]);
	}

	// Turn the trailing separator into the newline every log line ends with, or
	// terminate in place when the line was long enough to truncate.
	if ((at > 0) && (at < (int)sizeof msg))
	{
		msg[at - 1] = '\n';
		msg[at] = '\0';
	}
	else
	{
		msg[sizeof msg - 2] = '\n';
		msg[sizeof msg - 1] = '\0';
	}

	AP_LogLine(msg);
}

// ============================================================================
// Injection
// ============================================================================

static void AP_NavRec_PointEngineAtLanes(int levelID)
{
	int lane;

	for (lane = 0; lane < AP_NAVREC_LANES; lane++)
	{
		sdata->NavPath_ptrHeader[lane] = &s_navrecLaneHeader[lane];
		sdata->NavPath_ptrNavFrameArray[lane] = &s_navrecLane[lane].engine[0];
	}

	// Re-derive nav_ptrFirstPoint / nav_ptrLastPoint / nav_NumPointsOnPath so
	// they point into our lanes rather than the LEV's.
	BOTS_SetGlobalNavData(0);
	s_navrecLanesLive = 1;
	s_navrecLanesLevel = levelID;
}

void AP_NavRec_AfterBotsInit(void)
{
	int levelID;

	s_navrecLanesLive = 0;
	s_navrecLanesLevel = -1;
	AP_NavRec_ClearLaneIdentity();

	if (!g_config.navUseRecorded)
		return;
	if ((sdata == NULL) || (sdata->gGT == NULL))
		return;

	levelID = (int)sdata->gGT->levelID;

	if (!AP_NavRec_LoadForLevel(levelID))
		return;

	AP_NavRec_PointEngineAtLanes(levelID);
	AP_NavRec_LogLaneSummary(levelID, "active");
}

// ============================================================================
// Savestate restore
// ============================================================================

// Called at the end of NativeCheckpoint_RelocateSDataPointers, after the nav
// pointer slots have been relocated.
//
// Relocation alone is not enough across processes. The lanes live in this
// module's statics, which are NOT part of any checkpointed region, so a state
// restored into a fresh process finds them zeroed: the relocated pointers are
// arithmetically correct and the data behind them is blank (magicNumber 0,
// numPoints 0). The only sound repair is to load the file again and republish,
// and to fall back to the level's own nav data when that is not possible.
void AP_NavRec_AfterCheckpointRestore(void)
{
	int levelID;
	int lane;

	if ((sdata == NULL) || (sdata->gGT == NULL))
		return;

	levelID = (int)sdata->gGT->levelID;

	// Same folder, same rule, same field: the selection is deterministic, so a
	// restore reassembles the lane set the race started with unless the files
	// themselves changed underneath it.
	if (g_config.navUseRecorded && AP_NavRec_LoadForLevel(levelID))
	{
		AP_NavRec_PointEngineAtLanes(levelID);
		AP_NavRec_LogLaneSummary(levelID, "reloaded after a savestate restore");
		return;
	}

	if (!s_navrecLanesLive && !g_config.navUseRecorded)
		return; // nothing of ours was ever in those slots

	// Either the option is off now, or the file is gone or no longer valid. Put
	// the engine back on the level's own nav data rather than leaving it pointed
	// at lanes that may be blank.
	s_navrecLanesLive = 0;
	s_navrecLanesLevel = -1;
	AP_NavRec_ClearLaneIdentity();

	if (sdata->gGT->level1 == NULL)
	{
		AP_LogLine("[AP NAVREC] restore: no recorded lap and no level data; leaving nav pointers as restored\n");
		return;
	}

	for (lane = 0; lane < AP_NAVREC_LANES; lane++)
		BOTS_InitNavPath(sdata->gGT, (s16)lane);
	BOTS_SetGlobalNavData(0);

	AP_LogLine("[AP NAVREC] restore: no usable recording, nav pointers rebuilt from the level\n");
}

// ============================================================================
// Community-driver labels
// ============================================================================

// Penguin-MODSK's Bot_Trackrom proves this presentation shape on PS1: project
// a point above the kart through the 1P ViewProj matrix, hide it behind/very
// near the camera, and shrink FONT_SMALL in depth bands. Native uses the same
// engine projection and font path, with explicit clipping and a shadow so pale
// tracks do not erase a white username.
//
// The bands also carry the draw-distance cap: past AP_NAVREC_LABEL_MAX_DEPTH the
// width comes back 0, so the branch that hides a label behind the camera hides
// one too far away as well. ap_navrec_label_logic.h has the measured basis for
// that boundary.
//
// The name is keyed on the lane the bot is DRIVING, read from botData.botPath
// every frame rather than latched. That field is what BOTS.c indexes
// sdata->NavPath_ptr* with, and it is not fixed for the race: the overtake path
// in BOTS_ThTick_Drive reassigns it. Reading it per frame is the only mapping
// that stays true whatever the engine does with it.
void AP_NavRec_DrawBotNames(void)
{
	struct GameTracker *gGT;
	MATRIX *view;
	int firstBot;
	int totalDrivers;
	int i;
	int lane;
	int named = 0;
	int oldWidth;

	if (!s_navrecLanesLive)
		return;

	for (lane = 0; lane < AP_NAVREC_LANES; lane++)
	{
		if (s_navrecLaneDriverName[lane][0] != '\0')
			named = 1;
	}
	if (!named)
		return;

	if ((sdata == NULL) || ((gGT = sdata->gGT) == NULL))
		return;
	if ((gGT->numPlyrCurrGame != 1) || ((gGT->gameMode1 & PAUSE_ALL) != 0))
		return;
	if ((gGT->drivers[0] == NULL) || ((gGT->drivers[0]->actionsFlagSet & ACTION_RACE_FINISHED) != 0))
		return;

	firstBot = (int)(unsigned char)gGT->numPlyrCurrGame;
	totalDrivers = firstBot + (int)(unsigned char)gGT->numBotsNextGame;
	if (totalDrivers > 8)
		totalDrivers = 8;

	view = &gGT->pushBuffer[0].matrix_ViewProj;
	oldWidth = data.font_charPixWidth[FONT_SMALL];

	for (i = firstBot; i < totalDrivers; i++)
	{
		struct Driver *bot = gGT->drivers[i];
		char *name; // DecalFont_DrawLine takes char *, so this cannot be const
		SVECTOR world;
		s16 screen[2];
		u32 flag;
		s32 depth;
		int width;

		if ((bot == NULL) || (bot->instSelf == NULL) || (bot->invisibleTimer != 0))
			continue;
		if ((bot->actionsFlagSet & ACTION_RACE_FINISHED) != 0)
			continue;

		// botData only means anything for a driver the AI owns. A human converted
		// to AI mid-race memsets it before assigning botPath, so the flag is the
		// thing to test, not the driver's index in the field.
		if ((bot->actionsFlagSet & ACTION_BOT) == 0)
			continue;

		lane = (int)bot->botData.botPath;
		if ((lane < 0) || (lane >= AP_NAVREC_LANES))
			continue;

		// An unnamed contributor draws no label, which is what an empty name has
		// always done here. Now it is per lane: the other lanes still draw.
		name = s_navrecLaneDriverName[lane];
		if (name[0] == '\0')
			continue;

		world.vx = (s16)(bot->posCurr.x >> 8);
		world.vy = (s16)((bot->posCurr.y >> 8) + 75);
		world.vz = (s16)(bot->posCurr.z >> 8);
		world.pad = 0;

		gte_SetRotMatrix(view);
		gte_SetTransMatrix(view);
		CTR_GteLoadSV0(&world);
		gte_rtps();
		CTR_GteStoreSXY(screen);
		gte_stsz(&depth);
		gte_stflg(&flag);

		width = AP_NavRec_LabelWidthForDepth((int)depth);
		if ((width == 0) || ((flag & 0x40000u) != 0))
			continue;
		if ((screen[0] < -32) || (screen[0] > 544) || (screen[1] < -12) || (screen[1] > 228))
			continue;

		data.font_charPixWidth[FONT_SMALL] = (s16)width;
		DecalFont_DrawLine(name, screen[0], screen[1] - 4,
		                   FONT_SMALL, JUSTIFY_CENTER | WHITE);
		// CTR's ordering table renders later submissions behind earlier ones.
		// Match MM_HighScore_Text3D: foreground first, offset shadow second.
		DecalFont_DrawLine(name, screen[0] + 1, screen[1] - 3,
		                   FONT_SMALL, JUSTIFY_CENTER | BLACK);
	}

	data.font_charPixWidth[FONT_SMALL] = (s16)oldWidth;
}

// ============================================================================
// Held-item counters
// ============================================================================

// Called from VehPickupItem_ShootNow, the one place a held item actually fires.
// Counted from the FIRE, not from the pickup: an item picked up and never used
// tells us nothing about how the lap was driven.
//
// Firing the Mask does NOT make a lap dirty. It is a legitimate way to drive a
// lap and the count is what lets a later consumer decide what to do about it.
void AP_NavRec_NoteItemFire(struct Driver *d, int weaponID)
{
	if (!s_navrecArmed || (s_navrecScratch == NULL) || (d == NULL))
		return;
	if ((sdata == NULL) || (sdata->gGT == NULL))
		return;
	if (d != sdata->gGT->drivers[0])
		return; // bots and bosses fire through here too

	if (weaponID == AP_NAVREC_WEAPON_MASK)
	{
		if (s_navrecCurMaskFires < 0xFFFFu)
			s_navrecCurMaskFires++;
	}
	else if (weaponID == AP_NAVREC_WEAPON_TURBO)
	{
		if (s_navrecCurTurboFires < 0xFFFFu)
			s_navrecCurTurboFires++;
	}
}

// ============================================================================
// Per-frame recording
// ============================================================================

void AP_NavRec_Tick(struct GameTracker *gGT)
{
	struct Driver *d;
	int            levelID;
	int            atEnd;

	AP_NavRec_SyncArmed();

	if (!s_navrecArmed || (gGT == NULL) || (s_navrecScratch == NULL))
		return;

	// Never sample while a load is in flight. The driver struct survives a level
	// transition but the level data it points into does not, so a driver field
	// such as currBlockTouching can be non-NULL and still reference freed memory.
	if ((sdata == NULL) || (sdata->Loading.stage != LOAD_IDLE))
		return;

	levelID = (int)gGT->levelID;

	if (levelID != s_navrecLevelID)
	{
		// New level: the corpus belongs to the old track, so it goes. Skip this
		// frame outright as well. The driver's block pointer still belongs to the
		// level just left; the engine repopulates it on the next gameplay frame,
		// and sampling here dereferenced freed block data and crashed on entering
		// Polar Pass during the 2026-08-17 prototype. A NULL check does not help:
		// the pointer is stale, not null.
		s_navrecLevelID = levelID;
		AP_NavRec_DropBank();
		s_navrecCorridorLevel = -1;
		s_navrecGridReady = 0;
		return;
	}

	if (s_navrecCorridorLevel != levelID)
		AP_NavRec_SnapshotCorridor(gGT, levelID);

	d = gGT->drivers[0];
	if (d == NULL)
		return;

	s_navrecCharacterId = (short)d->driverID;

	atEnd = ((gGT->gameMode1 & END_OF_RACE) != 0);

	if (atEnd)
	{
		// The race is over. Bank whatever is in progress and write once. There is
		// no key to press: a player who ticked the option and drove a race has
		// already said what they want, and a hidden key would be an authoring
		// affordance in a player-facing feature.
		//
		// The final lap banks only if it started on the line. In a one-lap race
		// the lap in hand is the standing-start lap, which begins on the grid
		// and is not a loop; banking it is how grid-anchored lines used to reach
		// files and teleport bots at the finish.
		if (!s_navrecWrittenThisRace)
		{
			s_navrecWrittenThisRace = 1;
			if (s_navrecLapOpenAtLine)
				AP_NavRec_BankLap();
			else if (s_navrecCurSamples > 0)
			{
				AP_LogLine("[AP NAVREC] final lap did not start on the line (standing start or reversal); dropping it\n");
				AP_NavRec_DropCurrentLap();
			}
			AP_NavRec_Write(levelID);
		}
		s_navrecEndSeen = 1;
		return;
	}

	if (s_navrecEndSeen)
	{
		// A fresh race on the same track. Start over rather than blending two
		// races into one file.
		s_navrecEndSeen = 0;
		AP_NavRec_DropBank();
	}

	if (gGT->trafficLightsTimer > 0)
		return;

	// Lap boundary. Only a lap that both STARTED and ENDED with a forward line
	// crossing is a closed loop worth banking. The standing-start lap begins on
	// the grid; a backwards crossing (blast or reversal) invalidates the samples
	// in hand and makes the segment up to the next forward crossing a fragment.
	// Both used to be banked, and both put a discontinuity exactly on the finish
	// line of any lane they later fed.
	{
		int lapIndex = (int)d->lapIndex;
		if ((s_navrecLastLapIndex >= 0) && (lapIndex != s_navrecLastLapIndex))
		{
			if (AP_NavRecFormat_LapBoundaryBanks(s_navrecLastLapIndex, lapIndex, &s_navrecLapOpenAtLine))
				AP_NavRec_BankLap();
			else
			{
				AP_LogLine("[AP NAVREC] lap boundary without a closed lap behind it (standing start or reversal); dropping the samples\n");
				AP_NavRec_DropCurrentLap();
			}
		}
		s_navrecLastLapIndex = lapIndex;
	}

	// Anything that means this lap is not a line worth copying. KS_MASK_GRABBED
	// covers both a respawn and a mask reset; KS_CRASHING is a wall crash, which
	// zeroes the driver's speed and leaves a line no bot should be asked to
	// follow. Firing the Mask item is deliberately NOT here: it is counted
	// instead, in AP_NavRec_NoteItemFire.
	{
		u8 ks = d->kartState;
		if ((ks == KS_MASK_GRABBED) || (ks == KS_BLASTED) || (ks == KS_SPINNING) || (ks == KS_CRASHING))
			s_navrecCurDirty = 1;
	}

	s_navrecCurFrames++;

	if (s_navrecCurSamples >= AP_NAVREC_MAX_SAMPLES)
	{
		// Past the sample cap. Keep counting frames so the lap time stays true,
		// but mark the lap dirty: a truncated line is not a line.
		s_navrecCurDirty = 1;
		return;
	}

	{
		struct AP_NavRecSample *s = &s_navrecScratch->samples[AP_NAVREC_LIVE_ROW][s_navrecCurSamples];
		unsigned short          flags = 0;

		s->x = d->posCurr.x;
		s->y = d->posCurr.y;
		s->z = d->posCurr.z;
		s->rotY = (short)d->rotCurr.y;

		// The driver's checkpoint index at this frame. This becomes the node's
		// goBackCount, which BOTS_Killplane's rewind loop needs to VARY along the
		// lane or it never terminates.
		s->checkpoint = d->checkpoint.currentIndex;

		if (d->currBlockTouching != NULL)
			flags |= (unsigned short)(((unsigned short)d->currBlockTouching->terrain_type & 0xFu) << 3);

		// Airborne frames ARE recorded. Retail lines carry a jump flag at 0x400,
		// so real nodes exist mid-jump; dropping them left 2200-unit gaps across
		// jumps where retail's longest segment on Crash Cove is 499.
		if ((d->actionsFlagSet & ACTION_TOUCH_GROUND) == 0)
			flags |= 0x400u;

		s->flags = flags;
	}

	s_navrecCurSamples++;
}

#endif // CTR_AP
