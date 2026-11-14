/* A random hub behind adventure name entry (A4-60c). The decisions are in
 * ap_hub_name_entry_logic.h; this unit asks the engine and acts. Unity member
 * after ap_hub_savebox.c. */
#include "ap_hub_name_entry_logic.h"
#include "ap_hub_name_entry.h"
#include <time.h>

static struct AP_HubNameEntryState s_apHne = {AP_HNE_OFF, -1, -1, 0x2545f491u, 0, 0};

/* Two or three stops per hub, indexed like the hubs (Gem Stone Valley first).
 * Each eye sits about 350 units above a road quadblock that carries a
 * visibility list, looking at a hub landmark. Picked from headless editor
 * renders of the retail hubs drawn with the game's own camera visibility at
 * the 0x1c000 primitive arena (the A4-60c note has the frames and the
 * figures). */
struct AP_HNE_HubStops
{
	int count;
	struct AP_HNE_Stop stops[3];
};

#include "ap_hub_name_entry_stops.h"

static int AP_HNE_HubUsable(int hub)
{
	struct BigHeader *big = sdata->ptrBigfile1;
	struct BigEntry *e;
	int base = BI_ADVENTUREHUB + hub * 3;
	if (big == NULL || base + LVI_PTR >= big->numEntry)
		return 0;
	if (s_apHneStops[hub].count < 1)
		return 0;
	e = BIG_GETENTRY(big);
	return AP_HNE_HubFilesUsable((unsigned)e[base + LVI_VRAM].size, (unsigned)e[base + LVI_LEV].size,
	                             (unsigned)e[base + LVI_PTR].size, (unsigned)NativePtrScratch_Capacity());
}

int AP_HubNameEntry_NewAdventureLevel(void)
{
	int usable[AP_HNE_HUBS];
	int i, level;
	char line[160];
	unsigned entropy = (unsigned)time(NULL) ^ ((unsigned)sdata->frameCounter * 2654435761u);
	int apRacer = AP_CharSwap_GarageRacer();
	for (i = 0; i < AP_HNE_HUBS; i++)
		usable[i] = AP_HNE_HubUsable(i);
	level = AP_HNE_BeginNewAdventure(&s_apHne, apRacer, entropy, usable);
	if (level != AP_HNE_GARAGE_LEVEL)
	{
		snprintf(line, sizeof line, "[AP HUB NAME] new adventure: hub level %d behind name entry (racer %d)\n", level,
		         apRacer);
		AP_LogLine(line);
	}
	else if (apRacer >= 0)
		AP_LogLine("[AP HUB NAME] new adventure: no usable hub, the garage loads\n");
	return level;
}

int AP_HubNameEntry_Active(void)
{
	return s_apHne.phase != AP_HNE_OFF;
}

int AP_HubNameEntry_IsBackdropLevel(int levelID)
{
	return AP_HNE_IsBackdropLevel(&s_apHne, levelID);
}

int AP_HubNameEntry_GarageInit(void)
{
	int apRacer;
	if (!AP_HNE_IsBackdropLevel(&s_apHne, (int)sdata->gGT->levelID))
		return 0;
	/* Seat the racer before the driver pack loads (stage 4 runs this, then
	 * LOAD_DriverMPK), so the adventure pack read here is the seed's racer. */
	apRacer = AP_CharSwap_GarageRacer();
	if (apRacer >= 0)
	{
		data.characterIDs[0] = (s16)apRacer;
		sdata->advProgress.characterID = (s16)apRacer;
	}
	/* No garage menu: the first idle frame on the hub opens name entry. */
	sdata->ptrActiveMenu = NULL;
	sdata->gGT->gameMode2 &= ~(GARAGE_OSK);
	Garage_Init();
	return 1;
}

int AP_HubNameEntry_OnLevelFile(int leaving, int next)
{
	return AP_HNE_OnLevelFile(&s_apHne, leaving, next);
}

int AP_HubNameEntry_CancelToTitle(void)
{
	if (!AP_HNE_CancelToTitle(&s_apHne, (int)sdata->gGT->levelID))
		return 0;
	/* The transition the garage's own Back uses to reach the title. */
	sdata->mainMenuState = MAIN_MENU_TITLE;
	Garage_Leave();
	MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
	AP_LogLine("[AP HUB NAME] name entry cancelled: back to the title\n");
	return 1;
}

// The host keyboard's Enter, as name entry reads it (SubmitName_DrawMenu).
static int AP_HNE_EnterHeld(void)
{
#if defined(CTR_NATIVE)
	return NikoGetEnterKey();
#else
	return 0;
#endif
}

static int AP_HNE_LevelOk(struct GameTracker *gGT)
{
	struct Level *lev = gGT->level1;
	return lev != NULL && lev->ptr_mesh_info != NULL && lev->ptr_mesh_info->numQuadBlock > 0 &&
	       lev->ptr_mesh_info->bspRoot != NULL && gGT->drivers[0] != NULL;
}

void AP_HubNameEntry_OnFrame(struct GameTracker *gGT)
{
	int action, apRacer, i;
	int eye[3], look[3], rot[3];
	const struct AP_HNE_HubStops *hs;
	struct PushBuffer *pb;
	char line[160];

	if (gGT == NULL || s_apHne.phase == AP_HNE_OFF)
		return;

	apRacer = s_apHne.handedOff ? 0 : AP_CharSwap_GarageRacer();
	action = AP_HNE_Frame(&s_apHne, (int)gGT->levelID,
	                      AP_HNE_FrameReady(sdata->Loading.stage == LOAD_IDLE, s_apHne.handedOff, AP_HNE_EnterHeld()),
	                      AP_HNE_LevelOk(gGT), apRacer);
	if (action == AP_HNE_ACT_NONE)
		return;

	if (action == AP_HNE_ACT_FALLBACK)
	{
		snprintf(line, sizeof line, "[AP HUB NAME] hub level %d not usable (level %d, racer %d): the garage loads\n",
		         (int)gGT->levelID, AP_HNE_LevelOk(gGT), apRacer);
		AP_LogLine(line);
		sdata->mainMenuState = MAIN_MENU_ADVENTURE;
		MainRaceTrack_RequestLoad(ADVENTURE_GARAGE);
		return;
	}

	if (action == AP_HNE_ACT_HANDOFF)
	{
		/* What the garage skip does on its commit frame (CS_Garage_MenuProc),
		 * with the racer AP resolved. Everything after it, name entry, the
		 * profile slot and the hub load, is the unchanged retail path. No
		 * menu is shown over the hub, so name entry is shown here as well as
		 * desired (AP_HNE_OpenMenu says why; A4-87). */
		AP_HNE_OpenMenu(&sdata->ptrActiveMenu, &sdata->ptrDesiredMenu, &data.menuSubmitName);
		data.characterIDs[0] = (s16)apRacer;
		sdata->advProgress.characterID = (s16)apRacer;
		SubmitName_RestoreName(0);
		OtherFX_Play(1, 1);
		snprintf(line, sizeof line, "[AP HUB NAME] hub level %d up, name entry open (racer %d)\n", (int)gGT->levelID,
		         apRacer);
		AP_LogLine(line);
	}
	else
	{
		s_apHne.elapsedMs += (unsigned)(gGT->elapsedTimeMS > 0 ? gGT->elapsedTimeMS : 0);
	}

	/* The menu-mode load births four drivers of the racer at the start line
	 * with a null thread. They are not part of the picture. */
	for (i = 0; i < 4; i++)
	{
		if (gGT->drivers[i] != NULL && gGT->drivers[i]->instSelf != NULL)
			gGT->drivers[i]->instSelf->flags |= HIDE_MODEL;
	}

	hs = &s_apHneStops[s_apHne.hubLevel - AP_HNE_FIRST_HUB_LEVEL];
	AP_HNE_CameraAt(hs->stops, hs->count, s_apHne.elapsedMs, eye, look);
	AP_HNE_LookRotation(eye, look, rot);
	pb = &gGT->pushBuffer[0];
	pb->pos.x = (s16)eye[0];
	pb->pos.y = (s16)eye[1];
	pb->pos.z = (s16)eye[2];
	pb->rot.x = (s16)rot[0];
	pb->rot.y = (s16)rot[1];
	pb->rot.z = (s16)rot[2];
	/* Free camera: CAM_ThTick leaves the position alone and takes the
	 * visibility list of the quadblock under it, as the garage's camera does. */
	gGT->cameraDC[0].cameraMode = 3;
}
