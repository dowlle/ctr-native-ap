#ifndef AP_HUB_NAME_ENTRY_LOGIC_H
#define AP_HUB_NAME_ENTRY_LOGIC_H

// A random hub behind adventure name entry (A4-60c).
//
// When a connected seed owns the racer, New Adventure never shows the garage
// picker: the garage commits the seed's racer and opens name entry at once
// (ap/ap_garageskip.h). The garage room is then only a backdrop. This feature
// loads one of the five hubs instead, picked at random, as a menu screen, and
// glides the camera between a few spots in it while name entry is open.
//
// The hub is loaded the way the garage is loaded, not the way an adventure
// hub is: MAIN_MENU mode, one player, no LEV swap, no hub overlay, and its
// level instances are drawn but never get their threads
// (gameMode2 NO_LEV_INSTANCE, the podium's own switch). So warp pads, doors,
// garages, the save box and every AP hub hook that keys on ADVENTURE_ARENA or
// on the hub overlay stay asleep. The few AP hooks that key on the hub level
// id alone ask AP_HubNameEntry_Active() (ap/ap_hub_name_entry.c).
//
// Freestanding so tools/test-hub-name-entry.c pins the decisions out of
// engine; tools/test-hub-name-entry-production.py checks the wiring.

#include <math.h>

#define AP_HNE_HUBS 5
#define AP_HNE_FIRST_HUB_LEVEL 25 // GEM_STONE_VALLEY
#define AP_HNE_GARAGE_LEVEL 40    // ADVENTURE_GARAGE
#define AP_HNE_TITLE_LEVEL 39     // MAIN_MENU_LEVEL

// The size of one hub pack in an adventure (LOAD_TenStages, "HUB ALLOC"). A
// hub LEV larger than this could not load in an adventure either, so the
// preflight refuses it.
#define AP_HNE_HUB_PACK_BYTES 0x68800

// Primitive arena for the backdrop: the hub's own adventure budget
// (MainInit_GetPrimMemSize, ADVENTURE_ARENA), not the 0x17c00 every other
// menu level gets. Ordering table: the hub's 0x2c00.
#define AP_HNE_PRIM_BYTES 0x1c000
#define AP_HNE_OT_BYTES 0x2c00

enum
{
	AP_HNE_OFF = 0,
	AP_HNE_LOADING = 1, // New Adventure asked for the hub, the load runs
	AP_HNE_SHOWING = 2  // the hub is up, name entry is (or is about to be) open
};

struct AP_HubNameEntryState
{
	int phase;
	int hubLevel;      // the backdrop hub's level id, -1 when off
	int lastHub;       // previous pick 0..4, -1 before the first
	unsigned rng;      // xorshift32 state
	int handedOff;     // the racer is committed and name entry requested
	unsigned elapsedMs; // camera clock since the hub came up
};

static inline void AP_HNE_Reset(struct AP_HubNameEntryState *s)
{
	s->phase = AP_HNE_OFF;
	s->hubLevel = -1;
	s->handedOff = 0;
	s->elapsedMs = 0;
}

// Can this hub's files load as a backdrop? Every size comes from the disc's
// BIGFILE table. A missing file (size 0) or a LEV larger than an adventure hub
// pack refuses it, and so does a pointer map larger than the native scratch
// the backdrop reads it into.
static inline int AP_HNE_HubFilesUsable(unsigned vramBytes, unsigned levBytes, unsigned ptrBytes, unsigned ptrScratchBytes)
{
	return vramBytes != 0 && levBytes != 0 && ptrBytes != 0 && levBytes <= AP_HNE_HUB_PACK_BYTES &&
	       ptrBytes <= ptrScratchBytes;
}

static inline unsigned AP_HNE_NextRandom(unsigned *rng)
{
	unsigned x = *rng;
	if (x == 0)
		x = 0x9e3779b9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*rng = x;
	return x;
}

// A random usable hub, 0..4, not the previous pick when another is usable.
// -1 when no hub is usable.
static inline int AP_HNE_PickHub(unsigned *rng, int last, const int usable[AP_HNE_HUBS])
{
	int choices[AP_HNE_HUBS];
	int n = 0, i;
	for (i = 0; i < AP_HNE_HUBS; i++)
		if (usable[i] && i != last)
			choices[n++] = i;
	if (n == 0 && last >= 0 && last < AP_HNE_HUBS && usable[last])
		choices[n++] = last;
	if (n == 0)
		return -1;
	return choices[AP_HNE_NextRandom(rng) % (unsigned)n];
}

// New Adventure: which level to request. The hub only when the seed owns the
// racer (apRacer >= 0, AP_CharSwap_GarageRacer) and a hub is usable. Offline
// play, old seeds without the character keys, refused seeds and a connect
// that has not landed yet all get the garage and its picker, unchanged. This
// is the garage skip's own test (CS_Garage_MenuProc): the hub shows exactly
// when the garage would skip its picker, a socket drop included.
static inline int AP_HNE_BeginNewAdventure(struct AP_HubNameEntryState *s, int apRacer, unsigned entropy,
                                           const int usable[AP_HNE_HUBS])
{
	int hub;
	AP_HNE_Reset(s);
	if (apRacer < 0)
		return AP_HNE_GARAGE_LEVEL;
	s->rng ^= entropy;
	hub = AP_HNE_PickHub(&s->rng, s->lastHub, usable);
	if (hub < 0)
		return AP_HNE_GARAGE_LEVEL;
	s->lastHub = hub;
	s->phase = AP_HNE_LOADING;
	s->hubLevel = AP_HNE_FIRST_HUB_LEVEL + hub;
	return s->hubLevel;
}

// Is the hub of this level load the backdrop? Asked by every load step that
// treats the garage specially (LOAD_TenStages, LOAD_DriverMPK, MainInit).
static inline int AP_HNE_IsBackdropLevel(const struct AP_HubNameEntryState *s, int levelID)
{
	return s->phase != AP_HNE_OFF && levelID == s->hubLevel;
}

// LOAD_LevelFile records the level being left as prevLEV. Leaving the
// backdrop records the garage instead, so the adventure's first hub spawns
// at its start line and every prevLEV reader sees exactly what the garage
// path gives it today. Any load other than the backdrop's own ends the
// feature. Returns the prevLEV to record.
static inline int AP_HNE_OnLevelFile(struct AP_HubNameEntryState *s, int leaving, int next)
{
	if (s->phase == AP_HNE_OFF)
		return leaving;
	if (leaving == s->hubLevel && s->phase == AP_HNE_SHOWING)
	{
		AP_HNE_Reset(s);
		return AP_HNE_GARAGE_LEVEL;
	}
	if (next != s->hubLevel)
		AP_HNE_Reset(s);
	return leaving;
}

// What the per-frame hook does on the backdrop.
enum
{
	AP_HNE_ACT_NONE = 0,
	AP_HNE_ACT_HANDOFF = 1,  // commit the racer, open name entry, start the camera
	AP_HNE_ACT_CAMERA = 2,   // move the camera
	AP_HNE_ACT_FALLBACK = 3  // the hub did not come up usable: load the garage
};

// levelOk: the level, its mesh and its quadblocks are there. apRacer as
// above, read at the moment of the handoff: a seed that stopped owning the
// racer while the hub loaded gets the garage, where its picker runs.
static inline int AP_HNE_Frame(struct AP_HubNameEntryState *s, int levelID, int loadIdle, int levelOk, int apRacer)
{
	if (s->phase == AP_HNE_OFF || levelID != s->hubLevel || !loadIdle)
		return AP_HNE_ACT_NONE;
	if (!s->handedOff)
	{
		if (!levelOk || apRacer < 0)
		{
			AP_HNE_Reset(s);
			return AP_HNE_ACT_FALLBACK;
		}
		s->phase = AP_HNE_SHOWING;
		s->handedOff = 1;
		s->elapsedMs = 0;
		return AP_HNE_ACT_HANDOFF;
	}
	return AP_HNE_ACT_CAMERA;
}

// The handoff waits for an idle load and, before name entry first opens, for
// the keyboard Enter key to be up (A4-87 review). Name entry reads Enter as a
// held key (NikoGetEnterKey), and with a name from an earlier session the
// cursor starts on SAVE, so an Enter still held from the title would submit
// the name on name entry's first frame, before it was ever seen. After the
// handoff Enter is name entry's own and the camera keeps running.
static inline int AP_HNE_FrameReady(int loadIdle, int handedOff, int enterHeld)
{
	return loadIdle && (handedOff || !enterHeld);
}

// Opening name entry on the handoff frame (A4-87). Two engine facts decide
// how. RECTMENU_ProcessState is the only code that turns ptrDesiredMenu into
// the shown menu (ptrActiveMenu), and MainFrame_RenderFrame calls it only
// while a menu is already shown. The garage skip sets the desired menu from
// inside the garage's own shown menu, so the next frame picks it up. Over the
// hub no menu is shown (the garage init hook clears the garage's), so a
// desired menu alone is never picked up: nothing draws and no button is read.
// The handoff therefore shows name entry itself, and also leaves it desired,
// so the first ProcessState run still does the retail promotion.
struct RectMenu;
static inline void AP_HNE_OpenMenu(struct RectMenu **activeMenu, struct RectMenu **desiredMenu, struct RectMenu *menu)
{
	*desiredMenu = menu;
	*activeMenu = menu;
}

// Name entry Cancel on the backdrop: back to the title, like the AP-owned
// garage Cancel (H6-02). There is no garage menu to return to here.
static inline int AP_HNE_CancelToTitle(const struct AP_HubNameEntryState *s, int levelID)
{
	return s->phase == AP_HNE_SHOWING && levelID == s->hubLevel;
}

// ---------------------------------------------------------------------------
// Camera. Each hub has two or three stops, an eye and a point it looks at, all
// at driving height above road quadblocks that carry a visibility list, so the
// game draws what a driver there would see. The camera holds at a stop, glides
// to the next with an eased move, and runs the stops back and forth
// (0,1,2,1,0,...).

struct AP_HNE_Stop
{
	int eye[3];
	int look[3];
};

#define AP_HNE_HOLD_MS 2500u
#define AP_HNE_GLIDE_MS 6000u

static inline void AP_HNE_CameraAt(const struct AP_HNE_Stop *stops, int n, unsigned ms, int eye[3], int look[3])
{
	unsigned seg = AP_HNE_HOLD_MS + AP_HNE_GLIDE_MS;
	unsigned legs, leg, in;
	int from, to, i;
	float t;
	if (n <= 1)
	{
		for (i = 0; i < 3; i++)
		{
			eye[i] = stops[0].eye[i];
			look[i] = stops[0].look[i];
		}
		return;
	}
	legs = 2u * (unsigned)(n - 1); // there and back
	leg = (ms / seg) % legs;
	in = ms % seg;
	if (leg < (unsigned)(n - 1))
	{
		from = (int)leg;
		to = from + 1;
	}
	else
	{
		from = (int)(legs - leg);
		to = from - 1;
	}
	if (in < AP_HNE_HOLD_MS)
		t = 0.0f;
	else
	{
		t = (float)(in - AP_HNE_HOLD_MS) / (float)AP_HNE_GLIDE_MS;
		t = t * t * (3.0f - 2.0f * t); // ease in and out
	}
	for (i = 0; i < 3; i++)
	{
		eye[i] = stops[from].eye[i] + (int)lroundf((float)(stops[to].eye[i] - stops[from].eye[i]) * t);
		look[i] = stops[from].look[i] + (int)lroundf((float)(stops[to].look[i] - stops[from].look[i]) * t);
	}
}

// The push buffer rotation that looks from eye at look, in the engine's 4096
// units per turn: the same convention the editor's free camera writes
// (editor.c: rot.x = pitch + 0x800, rot.y = yaw).
static inline void AP_HNE_LookRotation(const int eye[3], const int look[3], int rot[3])
{
	const double k = 2048.0 / 3.14159265358979323846;
	double dx = (double)(look[0] - eye[0]);
	double dy = (double)(look[1] - eye[1]);
	double dz = (double)(look[2] - eye[2]);
	int yaw = 2048 + (int)lround(atan2(dx, dz) * k);
	int pitch = (int)lround(atan2(dy, sqrt(dx * dx + dz * dz)) * k);
	rot[0] = (pitch + 0x800) & 0xfff;
	rot[1] = yaw & 0xfff;
	rot[2] = 0;
}

#endif
