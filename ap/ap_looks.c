#ifdef CTR_AP
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <common.h>
#include <platform/native_assets.h>
#include <platform/native_config.h>
#include <ctr_character_looks.h>
#include "ap_look_logic.h"
#include "ap_looks.h"
#include "ap_hooks.h" // AP_LogLine

// Character looks at runtime. Rules and the look table: ap/ap_look_logic.h.
//
// Owned looks are rebuilt from the ReceivedItems replay on every connect, like
// the Tizi Helper and Spoiler Items booleans. The saved choice is a local
// config.ini value per racer (conservative choice, JL1 decision 4): it stores
// the look's item index, or 0 for ORIGINAL, and is read through the owned bits,
// so a look this seed never delivered drives as the original model.
//
// A look is loaded from a local file on first use into host memory (not MEMPACK).
// The file is checked with AP_LookValidateModel before relocation.
// The loaded driver packs stay intact: VehBirth_GetModelByName returns a small
// look Model (the found model's name and id, the look's one header), so
// switching back to ORIGINAL needs no undo and a racer born before the change
// keeps a valid model.

static AP_LookOwned ap_looks_owned;

enum
{
	AP_LOOK_FILE_UNTRIED = 0,
	AP_LOOK_FILE_READY,
	AP_LOOK_FILE_FAILED,
};

static struct
{
	int state;
	unsigned char *bytes;    // the whole file, relocated in place after +4
	struct ModelHeader *header;
} ap_look_files[AP_LOOK_COUNT];

// The look models handed to VehBirth. One per (look, found model) pair: the
// found model stays the racer's timing source. Driving code reads the frame
// counts of the driver's animations to time wall-hit, spin and landing
// recovery (VehFrameInst_GetNumAnimFrames); a static look has none, so those
// counts come from the original model instead. A small ring
// is enough: one level holds one copy of each racer's model, plus the hub or
// race just left while it unloads.
#define AP_LOOK_MODEL_SLOTS 8
static struct
{
	struct Model model;    // name, id: the found model's; the look's one header
	struct Model *orig;    // the model the engine found (timing source)
	int look;
} ap_look_models[AP_LOOK_MODEL_SLOTS];
static int ap_look_models_next;

static int ap_looks_logged_racer_look[64]; // last logged look + 1 per racer, 0 = none logged

// The OPTIONS row of each racer that has looks. Komodo Joe only for now; a
// look for another racer adds its config field and one line here.
static int *AP_LooksConfigFor(int racer)
{
	if (racer == AP_LOOK_RACER_JOE)
		return &g_config.lookJoe;
	return NULL;
}

static int AP_LooksRacerForRow(const int *valuePtr)
{
	if (CTR_AP_CHARACTER_LOOKS && valuePtr == &g_config.lookJoe)
		return AP_LOOK_RACER_JOE;
	return -1;
}

void AP_LooksConnectReset(void)
{
	ap_looks_owned = 0;
}

void AP_LooksReceiveOwned(long long itemIndex)
{
	int look = CTR_AP_CHARACTER_LOOKS ? AP_LookForItem(itemIndex) : -1;
	if (look >= 0)
		ap_looks_owned |= 1u << look;
}

void AP_LooksReceiveNew(long long itemIndex)
{
	char msg[160];
	int look = CTR_AP_CHARACTER_LOOKS ? AP_LookForItem(itemIndex) : -1;
	int *saved;

	if (look < 0)
		return;
	ap_looks_owned |= 1u << look;
	saved = AP_LooksConfigFor(AP_LOOKS[look].racer);
	if (saved == NULL)
		return;
	if (*saved != AP_LOOKS[look].itemIndex)
	{
		*saved = AP_LOOKS[look].itemIndex;
		NativeConfig_Save();
	}
	snprintf(msg, sizeof msg, "[AP LOOK] %s arrived: %s is the chosen look from the next load\n",
	         AP_LOOKS[look].itemName, AP_LOOKS[look].label);
	AP_LogLine(msg);
}

static int AP_LooksReadFile(const AP_LookDef *def, unsigned char *buf, const char **why)
{
	FILE *fp = NativeAssets_OpenHost(def->assetRel, "rb");
	if (fp == NULL)
	{
		*why = "could not open";
		return 0;
	}
	size_t received = fread(buf, 1, def->size, fp);
	int extra = fgetc(fp);
	int readError = ferror(fp);
	fclose(fp);
	if (readError || received != def->size || extra != EOF)
	{
		*why = readError ? "could not read" : "wrong size";
		return 0;
	}
	return 1;
}

static int AP_LooksValidate(const AP_LookDef *def, const unsigned char *bytes, size_t size,
                            AP_LookModelInfo *info, const char **why)
{
	if (size != def->size)
	{
		*why = "wrong size";
		return 0;
	}
	if (!AP_LookValidateModel(bytes, size, info, why))
		return 0;
	if (info->triangles != def->triangles)
	{
		*why = "unexpected triangle count";
		return 0;
	}
	return 1;
}

static int AP_LooksLoadFile(int look)
{
	const AP_LookDef *def = &AP_LOOKS[look];
	char msg[256];
	AP_LookModelInfo info;
	const char *why;
	unsigned char *buf = NULL;

	if (ap_look_files[look].state != AP_LOOK_FILE_UNTRIED)
		return ap_look_files[look].state == AP_LOOK_FILE_READY;
	ap_look_files[look].state = AP_LOOK_FILE_FAILED;
	if (sizeof(void *) != 4)
	{
		why = "this build's pointers are not 32-bit";
		goto fail;
	}
	buf = malloc(def->size);
	if (buf == NULL)
	{
		why = "could not allocate model buffer";
		goto fail;
	}
	if (!AP_LooksReadFile(def, buf, &why) || !AP_LooksValidate(def, buf, def->size, &info, &why))
		goto fail;
	{
		// Relocate exactly like LOAD_RunPtrMap: every listed slot becomes an
		// absolute pointer into this buffer.
		unsigned char *data = buf + 4;
		for (unsigned k = 0; k < info.numPtrs; k++)
		{
			unsigned off = AP_LookU32(data, info.dataLen + 4 + 4 * k) & ~3u;
			uint32_t v;
			memcpy(&v, data + off, 4);
			v += (uint32_t)(uintptr_t)data;
			memcpy(data + off, &v, 4);
		}
		ap_look_files[look].bytes = buf;
		ap_look_files[look].header = (struct ModelHeader *)(void *)(data + info.headerOff);
	}
	ap_look_files[look].state = AP_LOOK_FILE_READY;
	snprintf(msg, sizeof msg, "[AP LOOK] loaded %s (%u bytes, %d triangles)\n",
	         def->assetRel, def->size, info.triangles);
	AP_LogLine(msg);
	return 1;

fail:
	free(buf);
	snprintf(msg, sizeof msg, "[AP LOOK] %s not used (%s): the racer keeps the original model\n", def->assetRel, why);
	AP_LogLine(msg);
	return 0;
}

struct Model *AP_LooksModelFor(const char *searchName, struct Model *found)
{
	int racer, look;
	int *saved;
	struct Model *m;

	if (!CTR_AP_CHARACTER_LOOKS || found == NULL || searchName == NULL)
		return found;
	for (racer = 0; racer < (int)(sizeof ap_looks_logged_racer_look / sizeof ap_looks_logged_racer_look[0]); racer++)
		if (AP_LooksConfigFor(racer) != NULL && data.MetaDataCharacters[racer].name_Debug != NULL &&
		    strcmp(searchName, data.MetaDataCharacters[racer].name_Debug) == 0)
			break;
	if (racer >= (int)(sizeof ap_looks_logged_racer_look / sizeof ap_looks_logged_racer_look[0]))
		return found;
	saved = AP_LooksConfigFor(racer);
	look = AP_LookEffective(ap_looks_owned, racer, *saved);
	if (look >= 0 && !AP_LooksLoadFile(look))
		look = -1;
	if (ap_looks_logged_racer_look[racer] != look + 1)
	{
		char msg[128];
		ap_looks_logged_racer_look[racer] = look + 1;
		snprintf(msg, sizeof msg, "[AP LOOK] %s wears %s\n", searchName, look < 0 ? "the original model" : AP_LOOKS[look].label);
		AP_LogLine(msg);
	}
	if (look < 0)
		return found;
	{
		int slot = -1;
		for (int i = 0; i < AP_LOOK_MODEL_SLOTS; i++)
			if (ap_look_models[i].orig == found && ap_look_models[i].look == look)
				slot = i;
		if (slot < 0)
		{
			slot = ap_look_models_next;
			ap_look_models_next = (ap_look_models_next + 1) % AP_LOOK_MODEL_SLOTS;
		}
		ap_look_models[slot].orig = found;
		ap_look_models[slot].look = look;
		m = &ap_look_models[slot].model;
	}
	memcpy(m->name, found->name, sizeof m->name);
	m->id = found->id;
	m->numHeaders = 1;
	m->headers = ap_look_files[look].header;
	// The header keeps the file's name; nothing
	// looks a driver header up by name.
	return m;
}

// Quick load (F8) restores instance memory saved by an earlier frame, maybe by
// an earlier run of the client, so a driver's model pointer can name a look
// model of that run. Quick load only restores into the same
// live race, so each racing driver is born again by name here: the look in
// effect now, or the original model. Ghosts are not re-bound.
void AP_LooksRebindDrivers(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int rebound = 0;

	if (!CTR_AP_CHARACTER_LOOKS || gGT == NULL)
		return;
	for (int i = 0; i < 8; i++)
	{
		struct Driver *d = gGT->drivers[i];
		struct Model *m;
		if (d == NULL || d->instSelf == NULL)
			continue;
		m = VehBirth_GetModelByName(data.MetaDataCharacters[data.characterIDs[d->driverID]].name_Debug);
		if (m != NULL && m != d->instSelf->model)
		{
			d->instSelf->model = m;
			rebound++;
		}
	}
	if (rebound)
	{
		char msg[96];
		snprintf(msg, sizeof msg, "[AP LOOK] quick load: %d driver model(s) re-bound\n", rebound);
		AP_LogLine(msg);
	}
}

// A Time Trial ghost wearing a look: its model pointer would not survive a
// quick load in a later run (ghosts are not re-bound), so quick save waits.
int AP_LooksGhostWearsLook(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (!CTR_AP_CHARACTER_LOOKS || gGT == NULL)
		return 0;
	for (struct Thread *t = gGT->threadBuckets[GHOST].thread; t != NULL; t = t->siblingThread)
		if (t->inst != NULL && AP_LooksTimingModel(t->inst->model) != t->inst->model)
			return 1;
	return 0;
}

struct Model *AP_LooksTimingModel(struct Model *m)
{
	if (!CTR_AP_CHARACTER_LOOKS)
		return m;
	for (int i = 0; i < AP_LOOK_MODEL_SLOTS; i++)
		if (m != NULL && m == &ap_look_models[i].model && ap_look_models[i].orig != NULL)
			return ap_look_models[i].orig;
	return m;
}

int AP_LooksIsRowValue(const int *valuePtr)
{
	return AP_LooksRacerForRow(valuePtr) >= 0;
}

const char *AP_LooksRowLabel(const int *valuePtr)
{
	int racer = AP_LooksRacerForRow(valuePtr);
	return racer < 0 ? "ORIGINAL" : AP_LookRowLabel(ap_looks_owned, racer, *valuePtr);
}

void AP_LooksRowStep(int *valuePtr, int dir)
{
	int racer = AP_LooksRacerForRow(valuePtr);
	int next;
	if (racer < 0)
		return;
	// Write only when the shown value moves, so stepping a row that shows
	// ORIGINAL because this seed has not delivered the saved look keeps the
	// saved look for the next seed.
	next = AP_LookRowStep(ap_looks_owned, racer, *valuePtr, dir);
	if (next != AP_LookRowValue(ap_looks_owned, racer, *valuePtr))
		*valuePtr = next;
}

int AP_LooksRowHasChoice(const int *valuePtr)
{
	int racer = AP_LooksRacerForRow(valuePtr);
	return racer >= 0 && AP_LookRacerHasOwned(ap_looks_owned, racer);
}
#endif
