// cc -std=c99 -Wall -Wextra -DCTR_AP -I ap -o /tmp/test-hub-name-entry tools/test-hub-name-entry.c -lm && /tmp/test-hub-name-entry
//
// A4-60c: a random hub behind adventure name entry. Pins the decisions of
// ap/ap_hub_name_entry_logic.h (gating, the random pick, the load and frame
// decisions, the garage fallback, Cancel and confirm, the camera) and replays
// the New Adventure flows the engine glue (ap/ap_hub_name_entry.c, wiring
// checked by tools/test-hub-name-entry-production.py) walks through.
#include <stdio.h>
#include <stdlib.h>
#include "ap_hub_name_entry_logic.h"
#include "ap_name_entry_held_logic.h"

#include "ap_hub_name_entry_stops.h"

static int failures;
#define CHECK(label, expr) do { int ok_ = (expr); printf("%s  %s\n", ok_ ? "ok  " : "FAIL", label); failures += !ok_; } while (0)

enum { LEVEL_GSV = 25, LEVEL_NSB = 26, LEVEL_CITADEL = 29, LEVEL_TITLE = 39, LEVEL_GARAGE = 40 };

static const int ALL[5] = {1, 1, 1, 1, 1};

// The engine as far as these flows see it: the level, the load stage, the
// recorded prevLEV, and the two menu pointers (sdata->ptrActiveMenu, the shown
// menu, and sdata->ptrDesiredMenu, the one asked for next).
struct RectMenu
{
	int id;
};
static struct RectMenu nameEntryMenu = {1}, garagePicker = {2};
static struct
{
	int level, prevLEV, loading;
	struct RectMenu *active, *desired;
} e;

// One frame of MainFrame_RenderFrame and RECTMENU_ProcessState as far as the
// menu pointers go (A4-87): ProcessState runs only while a menu is shown, and
// it is the only code that turns the desired menu into the shown one. Returns
// the menu that ran this frame (ProcessState ran its menu proc), or NULL.
// tools/test-hub-name-entry-production.py pins both facts to the engine.
static struct RectMenu *menu_frame(void)
{
	if (e.active == NULL)
		return NULL;
	if (e.desired != NULL)
	{
		e.active = e.desired;
		e.desired = NULL;
	}
	return e.active;
}

// Name entry is the menu ProcessState runs on each of the next frames.
static int name_entry_runs(void)
{
	int i;
	for (i = 0; i < 3; i++)
		if (menu_frame() != &nameEntryMenu)
			return 0;
	return 1;
}

static void menus_none(void)
{
	e.active = NULL;
	e.desired = NULL;
}

static void level_file(struct AP_HubNameEntryState *s, int next)
{
	e.prevLEV = AP_HNE_OnLevelFile(s, e.level, next);
	e.level = next;
	e.loading = 1;
}

// One frame of the glue's per-frame hook, plus the garage's own init for a
// garage level.
static int frame(struct AP_HubNameEntryState *s, int levelOk, int apRacer)
{
	int a = AP_HNE_Frame(s, e.level, !e.loading, levelOk, apRacer);
	if (a == AP_HNE_ACT_HANDOFF)
		AP_HNE_OpenMenu(&e.active, &e.desired, &nameEntryMenu); // the glue's handoff
	if (a == AP_HNE_ACT_FALLBACK)
		level_file(s, LEVEL_GARAGE);
	return a;
}

static void load_done(int apRacer)
{
	e.loading = 0;
	if (e.level == LEVEL_GARAGE)
	{
		// CS_Garage_Init shows the garage menu; with an AP racer the garage
		// skip then asks for name entry from inside it (CS_Garage_MenuProc).
		e.active = &garagePicker;
		e.desired = apRacer >= 0 ? &nameEntryMenu : NULL;
	}
	else if (e.level >= LEVEL_GSV && e.level <= LEVEL_CITADEL)
		menus_none(); // the backdrop: AP_HubNameEntry_GarageInit clears the shown menu
}

int main(void)
{
	struct AP_HubNameEntryState s = {AP_HNE_OFF, -1, -1, 12345u, 0, 0, -1, {-1, -1, -1, -1, -1}};
	int i, level, counts[5] = {0}, repeats = 0, last = -1, outOfRange = 0;

	// ---- gating -------------------------------------------------------------
	CHECK("offline / old seed / refused seed (no AP racer): New Adventure loads the garage",
	      AP_HNE_BeginNewAdventure(&s, -1, 7u, ALL) == LEVEL_GARAGE && s.phase == AP_HNE_OFF);
	level = AP_HNE_BeginNewAdventure(&s, 3, 7u, ALL);
	CHECK("a seed that owns the racer: New Adventure loads a hub", level >= LEVEL_GSV && level <= LEVEL_CITADEL);
	CHECK("the hub load is the backdrop, other levels are not",
	      AP_HNE_IsBackdropLevel(&s, level) && !AP_HNE_IsBackdropLevel(&s, LEVEL_GARAGE) &&
	          !AP_HNE_IsBackdropLevel(&s, level == LEVEL_GSV ? LEVEL_NSB : LEVEL_GSV));
	{
		const int none[5] = {0, 0, 0, 0, 0};
		CHECK("no usable hub: the garage loads", AP_HNE_BeginNewAdventure(&s, 3, 9u, none) == LEVEL_GARAGE &&
		                                             !AP_HNE_IsBackdropLevel(&s, LEVEL_GSV));
	}
	{
		const int onlyCitadel[5] = {0, 0, 0, 0, 1};
		CHECK("only the usable hubs are picked", AP_HNE_BeginNewAdventure(&s, 3, 11u, onlyCitadel) == LEVEL_CITADEL &&
		                                             AP_HNE_BeginNewAdventure(&s, 3, 13u, onlyCitadel) == LEVEL_CITADEL);
	}

	// ---- random pick -------------------------------------------------------
	s.lastHub = -1;
	for (i = 0; i < 5000; i++)
	{
		level = AP_HNE_BeginNewAdventure(&s, 0, (unsigned)i * 2654435761u, ALL);
		if (level < LEVEL_GSV || level > LEVEL_CITADEL)
		{
			outOfRange++;
			continue;
		}
		counts[level - LEVEL_GSV]++;
		repeats += (level == last);
		last = level;
	}
	CHECK("every pick is one of the five hubs", outOfRange == 0);
	CHECK("all five hubs come up, each between 15% and 25% of 5000 picks",
	      counts[0] > 750 && counts[1] > 750 && counts[2] > 750 && counts[3] > 750 && counts[4] > 750 &&
	          counts[0] < 1250 && counts[1] < 1250 && counts[2] < 1250 && counts[3] < 1250 && counts[4] < 1250);
	CHECK("the same hub never comes twice in a row", repeats == 0);
	printf("      picks: GSV %d, NSB %d, Ruins %d, Glacier %d, Citadel %d\n", counts[0], counts[1], counts[2], counts[3],
	       counts[4]);

	// ---- disc preflight ------------------------------------------------------
	// Sizes from the NTSC-U BIGFILE (A4-60c note): VRAM, LEV, pointer map.
	CHECK("every retail hub's files pass the preflight",
	      AP_HNE_HubFilesUsable(229432, 421168, 48032, 0x20000) && AP_HNE_HubFilesUsable(229396, 403400, 45072, 0x20000) &&
	          AP_HNE_HubFilesUsable(229396, 426012, 47268, 0x20000) &&
	          AP_HNE_HubFilesUsable(229432, 406936, 45492, 0x20000) &&
	          AP_HNE_HubFilesUsable(229396, 426332, 44096, 0x20000));
	CHECK("a missing file, a LEV larger than a hub pack or a pointer map past the scratch is refused",
	      !AP_HNE_HubFilesUsable(0, 421168, 48032, 0x20000) && !AP_HNE_HubFilesUsable(229432, 0, 48032, 0x20000) &&
	          !AP_HNE_HubFilesUsable(229432, 421168, 0, 0x20000) &&
	          !AP_HNE_HubFilesUsable(229432, 0x68801, 48032, 0x20000) &&
	          !AP_HNE_HubFilesUsable(229432, 421168, 48032, 40000));
	CHECK("the garage LEV (896180 bytes) would not pass: the hub needs far less memory",
	      !AP_HNE_HubFilesUsable(458808, 896180, 1, 0x20000));

	// ---- flow 1: AP-owned start, name entry, confirm -------------------------
	AP_HNE_Reset(&s);
	e.level = LEVEL_TITLE;
	menus_none();
	level = AP_HNE_BeginNewAdventure(&s, 5, 99u, ALL);
	level_file(&s, level);
	CHECK("flow 1: the backdrop load keeps the feature on (title -> hub)", s.phase == AP_HNE_LOADING && e.prevLEV == LEVEL_TITLE);
	CHECK("flow 1: nothing happens while the hub still loads", frame(&s, 1, 5) == AP_HNE_ACT_NONE && menu_frame() == NULL);
	load_done(5);
	CHECK("flow 1: over the hub no menu is shown before the handoff", menu_frame() == NULL);
	CHECK("flow 1: the first idle frame commits the racer and opens name entry",
	      frame(&s, 1, 5) == AP_HNE_ACT_HANDOFF && s.phase == AP_HNE_SHOWING);
	CHECK("flow 1 (A4-87): name entry then runs over the hub (ProcessState runs it every frame)", name_entry_runs());
	CHECK("flow 1: the handoff happens once, then the camera moves",
	      frame(&s, 1, 5) == AP_HNE_ACT_CAMERA && frame(&s, 1, 5) == AP_HNE_ACT_CAMERA);
	CHECK("flow 1: AP hub code reads the feature as active on the hub", s.phase != AP_HNE_OFF);
	level_file(&s, LEVEL_NSB); // profile slot chosen: retail loads N. Sanity Beach
	CHECK("flow 1: confirm loads the adventure with prevLEV = garage, exactly as from the garage",
	      e.prevLEV == LEVEL_GARAGE && s.phase == AP_HNE_OFF && !AP_HNE_IsBackdropLevel(&s, LEVEL_NSB));
	load_done(5);
	CHECK("flow 1: the adventure's hub is an ordinary hub (no camera, no handoff)", frame(&s, 1, 5) == AP_HNE_ACT_NONE);

	// ---- A4-87: the alpha3 handoff only asked for name entry -----------------
	// It set the desired menu and nothing else. Over the hub no menu is shown,
	// so ProcessState never ran and never picked it up: the hub drew, name
	// entry never did, and no button reached it.
	menus_none();
	e.desired = &nameEntryMenu;
	CHECK("A4-87: asking for name entry with no menu shown never runs it", menu_frame() == NULL &&
	                                                                         menu_frame() == NULL && !name_entry_runs());
	menus_none();
	AP_HNE_OpenMenu(&e.active, &e.desired, &nameEntryMenu);
	CHECK("A4-87: AP_HNE_OpenMenu shows it and leaves it desired for the retail promotion",
	      e.active == &nameEntryMenu && e.desired == &nameEntryMenu && name_entry_runs() && e.desired == NULL);

	// ---- A4-87b: input held from before name entry opened --------------------
	// The guard arms in SubmitName_RestoreName, which every caller runs when it
	// opens name entry (hub backdrop, garage, garage skip, Time Trial). Pad
	// bits as the engine's BTN_ values (the harness only needs distinct bits).
	{
		enum { CIRCLE = 0x40, CROSS = 0x10, START = 0x1000, TRI = 0x40000, SQ = 0x20, UP = 0x1, L1 = 0x800 };
		const unsigned MASK = CIRCLE | CROSS | START | TRI | SQ;
		struct AP_NEH_Guard g;
		unsigned tap;
		int ign;

		// The hub handoff opens name entry with no wait: it opens at once while Enter is down.
		e.level = LEVEL_TITLE;
		menus_none();
		level = AP_HNE_BeginNewAdventure(&s, 5, 4242u, ALL);
		level_file(&s, level);
		load_done(5);
		CHECK("A4-87b: a held Enter no longer delays the handoff (the 1 s wait is gone)",
		      frame(&s, 1, 5) == AP_HNE_ACT_HANDOFF && name_entry_runs());

		// Keyboard Enter held through the open (restored name, cursor on SAVE).
		AP_NEH_Arm(&g, 0, MASK, 1, 0);
		tap = 0;
		ign = AP_NEH_Filter(&g, 0, 1, 0, &tap);
		CHECK("A4-87b: Enter held at the open is ignored on the first frame", ign == 1);
		ign = AP_NEH_Filter(&g, 0, 1, 0, &tap);
		CHECK("A4-87b: ... and for as long as it stays down (a stuck Enter never submits)", ign == 1);
		ign = AP_NEH_Filter(&g, 0, 0, 0, &tap);
		CHECK("A4-87b: released once: ignored no more", ign == 0);
		ign = AP_NEH_Filter(&g, 0, 1, 0, &tap);
		CHECK("A4-87b: pressed again after the release: Enter works", ign == 0);

		// Enter not held at the open: never ignored.
		AP_NEH_Arm(&g, 0, MASK, 0, 0);
		tap = 0;
		AP_NEH_Filter(&g, 0, 0, 0, &tap); // the first drawn frame
		CHECK("A4-87b: Enter pressed after the open works at once", AP_NEH_Filter(&g, 0, 1, 0, &tap) == 0);

		// Escape (cancel on the keyboard).
		AP_NEH_Arm(&g, 0, MASK, 0, 1);
		tap = 0;
		CHECK("A4-87b: Escape held at the open is ignored until released",
		      AP_NEH_Filter(&g, 0, 0, 1, &tap) == 2 && AP_NEH_Filter(&g, 0, 0, 0, &tap) == 0 &&
		          AP_NEH_Filter(&g, 0, 0, 1, &tap) == 0);

		// Controller: Circle held through the open, and a tap on the open frame.
		AP_NEH_Arm(&g, CIRCLE | UP, MASK, 0, 0);
		tap = CIRCLE | UP;
		AP_NEH_Filter(&g, CIRCLE | UP, 0, 0, &tap);
		CHECK("A4-87b: a pad button held at the open loses its tap, the d-pad keeps it", tap == UP);
		tap = CIRCLE;
		AP_NEH_Filter(&g, CIRCLE, 0, 0, &tap);
		CHECK("A4-87b: ... every frame it stays down", tap == 0);
		tap = 0;
		AP_NEH_Filter(&g, 0, 0, 0, &tap);
		tap = CIRCLE;
		AP_NEH_Filter(&g, CIRCLE, 0, 0, &tap);
		CHECK("A4-87b: released once, then pressed: the tap counts", tap == CIRCLE);

		// Per input: Circle held at the open does not stop Cross.
		AP_NEH_Arm(&g, CIRCLE, MASK, 0, 0);
		tap = 0;
		AP_NEH_Filter(&g, CIRCLE, 0, 0, &tap); // the first drawn frame
		tap = CROSS;
		AP_NEH_Filter(&g, CIRCLE | CROSS, 0, 0, &tap);
		CHECK("A4-87b: only the inputs held at the open are ignored (Cross still works)", tap == CROSS);

		// Start and Triangle (cancel, backspace) are guarded like the rest.
		AP_NEH_Arm(&g, START | TRI | L1, MASK, 0, 0);
		tap = START | TRI | L1;
		AP_NEH_Filter(&g, START | TRI | L1, 0, 0, &tap);
		CHECK("A4-87b: Start and Triangle are guarded, shoulder buttons are not in the guard", tap == L1);

		// A tap of a button held at the open, with the button already up, is no longer held
		// state: the filter keys on the held bits, so a stale tap bit with the button up passes.
		AP_NEH_Arm(&g, SQ, MASK, 0, 0);
		tap = 0;
		AP_NEH_Filter(&g, 0, 0, 0, &tap);
		tap = SQ;
		AP_NEH_Filter(&g, SQ, 0, 0, &tap);
		CHECK("A4-87b: release clears the guard for good", tap == SQ);

		// The hub handoff arms before the frame's input is read: a press first
		// seen on the first drawn frame is covered, one on the second is not.
		AP_NEH_Arm(&g, 0, MASK, 0, 0);
		tap = CROSS;
		ign = AP_NEH_Filter(&g, CROSS, 1, 1, &tap);
		CHECK("A4-87b: input first seen on name entry's first frame is ignored too (arm runs before input is read)",
		      tap == 0 && ign == 3);
		AP_NEH_Filter(&g, 0, 0, 0, &tap);
		tap = CROSS;
		AP_NEH_Filter(&g, CROSS, 0, 0, &tap);
		CHECK("A4-87b: ... until released once, then it works", tap == CROSS);
		AP_NEH_Arm(&g, 0, MASK, 0, 0);
		tap = 0;
		AP_NEH_Filter(&g, 0, 0, 0, &tap);
		tap = CROSS;
		ign = AP_NEH_Filter(&g, CROSS, 1, 1, &tap);
		CHECK("A4-87b: input pressed on the second drawn frame or later works", tap == CROSS && ign == 0);

		// Two callers, back to back: the guard re-arms on every open.
		AP_NEH_Arm(&g, 0, MASK, 0, 0);
		AP_NEH_Arm(&g, CIRCLE, MASK, 1, 0);
		tap = CIRCLE;
		ign = AP_NEH_Filter(&g, CIRCLE, 1, 0, &tap);
		CHECK("A4-87b: each open arms afresh with its own held state", ign == 1 && tap == 0);
	}

	// ---- flow 2: Cancel ----------------------------------------------------
	e.level = LEVEL_TITLE;
	level = AP_HNE_BeginNewAdventure(&s, 5, 123u, ALL);
	CHECK("flow 2: Cancel before the hub is up is not the backdrop's", !AP_HNE_CancelToTitle(&s, level));
	level_file(&s, level);
	load_done(5);
	frame(&s, 1, 5);
	CHECK("flow 2: Cancel on the backdrop goes to the title", AP_HNE_CancelToTitle(&s, e.level));
	level_file(&s, LEVEL_TITLE);
	CHECK("flow 2: the title load ends the feature (prevLEV garage, as the garage Cancel)",
	      e.prevLEV == LEVEL_GARAGE && s.phase == AP_HNE_OFF && !AP_HNE_CancelToTitle(&s, level));

	// ---- flow 3: the hub does not come up usable ------------------------------
	e.level = LEVEL_TITLE;
	level = AP_HNE_BeginNewAdventure(&s, 5, 321u, ALL);
	level_file(&s, level);
	load_done(5);
	CHECK("flow 3: a hub without its level data falls back to the garage",
	      frame(&s, 0, 5) == AP_HNE_ACT_FALLBACK && e.level == LEVEL_GARAGE && s.phase == AP_HNE_OFF);
	CHECK("flow 3: the garage loads as the garage, not as a backdrop", !AP_HNE_IsBackdropLevel(&s, LEVEL_GARAGE));
	load_done(5);
	CHECK("flow 3: the garage then runs its usual AP skip into name entry",
	      frame(&s, 1, 5) == AP_HNE_ACT_NONE && name_entry_runs());

	// ---- flow 4: the seed stops owning the racer while the hub loads ---------
	// AP_CharSwap_GarageRacer() answers -1 here, for example after a refused
	// seed. A plain socket drop keeps the seed configuration and the answer,
	// exactly as the garage skip sees it, so the backdrop follows the garage:
	// it shows whenever the garage would skip its picker.
	e.level = LEVEL_TITLE;
	menus_none();
	level = AP_HNE_BeginNewAdventure(&s, 5, 555u, ALL);
	level_file(&s, level);
	load_done(-1);
	CHECK("flow 4: a seed that stops owning the racer during the load falls back to the garage",
	      frame(&s, 1, -1) == AP_HNE_ACT_FALLBACK && e.level == LEVEL_GARAGE);
	load_done(-1);
	CHECK("flow 4: where the retail picker runs", menu_frame() == &garagePicker && menu_frame() == &garagePicker);

	// ---- flow 5: another load takes over the request -------------------------
	e.level = LEVEL_TITLE;
	level = AP_HNE_BeginNewAdventure(&s, 5, 777u, ALL);
	level_file(&s, LEVEL_TITLE);
	CHECK("flow 5: a different load ends the request", s.phase == AP_HNE_OFF && !AP_HNE_IsBackdropLevel(&s, level));

	// ---- flow 6: offline New Adventure is untouched --------------------------
	e.level = LEVEL_TITLE;
	level = AP_HNE_BeginNewAdventure(&s, -1, 1u, ALL);
	level_file(&s, level);
	load_done(-1);
	CHECK("flow 6: offline gets the garage and its picker, prevLEV as before",
	      level == LEVEL_GARAGE && menu_frame() == &garagePicker && e.prevLEV == LEVEL_TITLE &&
	          frame(&s, 1, -1) == AP_HNE_ACT_NONE);

	// ---- camera ------------------------------------------------------------
	{
		struct AP_HNE_Stop st[3] = {{{0, 100, 0}, {0, 0, 1000}}, {{1000, 100, 0}, {1000, 0, 1000}}, {{2000, 400, 0}, {2000, 0, 1000}}};
		int eye[3], look[3], prev[3], ms, maxStep = 0, d, k;
		unsigned seg = AP_HNE_HOLD_MS + AP_HNE_GLIDE_MS;
		AP_HNE_CameraAt(st, 3, 0, eye, look);
		CHECK("camera: starts on stop 0", eye[0] == 0 && eye[1] == 100 && look[2] == 1000);
		AP_HNE_CameraAt(st, 3, AP_HNE_HOLD_MS - 1, eye, look);
		CHECK("camera: holds through the hold", eye[0] == 0);
		AP_HNE_CameraAt(st, 3, AP_HNE_HOLD_MS + AP_HNE_GLIDE_MS / 2, eye, look);
		CHECK("camera: halfway through the glide is halfway", eye[0] == 500 && look[0] == 500);
		AP_HNE_CameraAt(st, 3, seg, eye, look);
		CHECK("camera: arrives on stop 1", eye[0] == 1000);
		AP_HNE_CameraAt(st, 3, 2 * seg, eye, look);
		CHECK("camera: then stop 2", eye[0] == 2000 && eye[1] == 400);
		AP_HNE_CameraAt(st, 3, 3 * seg, eye, look);
		CHECK("camera: and back to stop 1 (there and back, no jump to stop 0)", eye[0] == 1000);
		AP_HNE_CameraAt(st, 3, 4 * seg, eye, look);
		CHECK("camera: back on stop 0 after a full cycle", eye[0] == 0);
		AP_HNE_CameraAt(st, 3, 0, prev, look);
		for (ms = 16; ms < 8 * (int)seg; ms += 16)
		{
			AP_HNE_CameraAt(st, 3, (unsigned)ms, eye, look);
			for (k = 0; k < 3; k++)
			{
				d = abs(eye[k] - prev[k]);
				if (d > maxStep)
					maxStep = d;
				prev[k] = eye[k];
			}
		}
		CHECK("camera: smooth, never more than 8 units in one 16 ms frame for a 1000 unit glide", maxStep <= 8);
		printf("      largest per-frame step %d units\n", maxStep);
		AP_HNE_CameraAt(st, 2, 2 * seg, eye, look);
		CHECK("camera: two stops run back and forth too", eye[0] == 0);
	}
	{
		int rot[3];
		int eye[3] = {-16472, 2906, -5769}, look[3] = {-14003, 255, -6582};
		AP_HNE_LookRotation(eye, look, rot);
		// shot.look_rotation gives [-518, 3279, 0]; editor.c adds 0x800 to pitch.
		CHECK("look rotation matches the editor camera the pictures were taken with",
		      rot[0] == ((-518 + 0x800) & 0xfff) && rot[1] == 3279 && rot[2] == 0);
	}

	// ---- the shipped path table (A4-60c, A4-90) -------------------------------
	{
		int i, j, k, h, ok = 1, near = 1, perHub[AP_HNE_HUBS] = {0};
		for (i = 0; i < AP_HNE_PATHS; i++)
		{
			const struct AP_HNE_Path *p = &s_apHnePaths[i];
			if (p->hub < 0 || p->hub >= AP_HNE_HUBS || p->count < 2 || p->count > 3)
			{
				ok = 0;
				continue;
			}
			perHub[p->hub]++;
			for (j = 0; j < p->count; j++)
				for (k = 0; k < 3; k++)
					if (p->stops[j].eye[k] < -32768 || p->stops[j].eye[k] > 32767 || p->stops[j].look[k] < -32768 ||
					    p->stops[j].look[k] > 32767)
						ok = 0;
			for (j = 1; j < p->count; j++)
			{
				const int *a = p->stops[j - 1].eye, *b = p->stops[j].eye;
				double dx = a[0] - b[0], dz = a[2] - b[2];
				if (dx * dx + dz * dz > 2500.0 * 2500.0)
					near = 0;
			}
		}
		CHECK("every path names a hub and has two or three stops, all inside the push buffer's 16-bit range", ok);
		CHECK("consecutive stops are close (under 2500 units), so a glide stays over the same stretch", near);
		ok = AP_HNE_PATHS >= 25 && AP_HNE_PATHS <= 64;
		for (h = 0; h < AP_HNE_HUBS; h++)
			ok = ok && perHub[h] >= 4 && perHub[h] == AP_HNE_PathsForHub(s_apHnePaths, AP_HNE_PATHS, h);
		CHECK("A4-90: the pool has 25 to 64 paths, every hub at least four", ok);
		printf("      paths: %d (GSV %d, NSB %d, Ruins %d, Glacier %d, Citadel %d)\n", AP_HNE_PATHS, perHub[0], perHub[1],
		       perHub[2], perHub[3], perHub[4]);
		ok = 1;
		for (i = 0; i < AP_HNE_HUBS; i++)
			ok = ok && s_apHnePaths[i].hub == i;
		CHECK("the five A4-60c paths stay first, one per hub in hub order", ok);
	}

	{
		// The aim offset puts the landmark below name entry's box (bottom edge
		// at 78% of the height). The headless frames in the A4-90 note set its
		// value; this pins it to a sane window so a typo cannot point the
		// camera at the sky or the floor.
		CHECK("A4-90: the aim above the landmark is between 15 and 25 degrees",
		      AP_HNE_AIM_ABOVE * 360 >= 15 * 4096 && AP_HNE_AIM_ABOVE * 360 <= 25 * 4096);
	}

	// ---- path pick (A4-90) ---------------------------------------------------
	{
		struct AP_HubNameEntryState ps = {AP_HNE_OFF, -1, -1, 0x2545f491u, 0, 0, -1, {-1, -1, -1, -1, -1}};
		int pathCount[64] = {0}, lastPathShown = -1, pathRepeats = 0, hubRepeats = 0, lastHubShown = -1, wrongHub = 0,
		    missing = 0, perHubPathRepeat = 0, prevOfHub[AP_HNE_HUBS] = {-1, -1, -1, -1, -1}, n;
		for (i = 0; i < 20000; i++)
		{
			int lv = AP_HNE_BeginNewAdventure(&ps, 1, (unsigned)i * 2246822519u, ALL), hub = lv - LEVEL_GSV, pth;
			pth = AP_HNE_PickPath(&ps, s_apHnePaths, AP_HNE_PATHS, hub);
			if (pth < 0 || pth >= AP_HNE_PATHS)
			{
				missing++;
				continue;
			}
			wrongHub += s_apHnePaths[pth].hub != hub;
			pathRepeats += pth == lastPathShown;
			hubRepeats += hub == lastHubShown;
			perHubPathRepeat += pth == prevOfHub[hub];
			prevOfHub[hub] = pth;
			lastPathShown = pth;
			lastHubShown = hub;
			pathCount[pth]++;
		}
		CHECK("every New Adventure gets a path of the hub it loads", missing == 0 && wrongHub == 0);
		CHECK("hub first: the same hub never comes twice in a row", hubRepeats == 0);
		CHECK("the same path never comes twice in a row, and a hub never repeats the path it showed last time",
		      pathRepeats == 0 && perHubPathRepeat == 0);
		n = 1;
		for (i = 0; i < AP_HNE_PATHS; i++)
		{
			int share = AP_HNE_PathsForHub(s_apHnePaths, AP_HNE_PATHS, s_apHnePaths[i].hub);
			double expect = 20000.0 / AP_HNE_HUBS / share;
			if (pathCount[i] < expect * 0.75 || pathCount[i] > expect * 1.25)
				n = 0;
		}
		CHECK("every path comes up, each within 25% of an even share of its hub over 20000 picks", n);
		{
			const struct AP_HNE_Path one[2] = {{2, 2, {{{0, 0, 0}, {0, 0, 1}}, {{1, 0, 0}, {0, 0, 1}}}},
			                                   {3, 2, {{{0, 0, 0}, {0, 0, 1}}, {{1, 0, 0}, {0, 0, 1}}}}};
			int a = AP_HNE_PickPath(&ps, one, 2, 2), b = AP_HNE_PickPath(&ps, one, 2, 2);
			CHECK("a hub with a single path shows it every time", a == 0 && b == 0);
			CHECK("a hub with no path, or a bad hub, gets no path (the preflight refuses such a hub)",
			      AP_HNE_PickPath(&ps, one, 2, 4) == -1 && ps.path == -1 && AP_HNE_PickPath(&ps, one, 2, 7) == -1 &&
			          AP_HNE_PathsForHub(one, 2, 4) == 0);
		}
		AP_HNE_Reset(&ps);
		CHECK("a reset clears the showing's path but keeps each hub's last path", ps.path == -1 && ps.lastPath[0] >= 0);
	}

	printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
