#ifdef CTR_EDITOR

#include "editor.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern SDL_Window *g_window;
void CAM_FindClosestQuadblock(struct ScratchpadStruct *sps, struct CameraDC *cDC, struct Driver *d, const Vec3 *pos);
extern int g_dbg_wireframeMode;

#define EDITOR_PATH_MAX 1024
#define EDITOR_MAX_OBJECTS 128
#define EDITOR_HISTORY_DEPTH 16
#define EDITOR_SPAWN_INVALID (-1)
#define EDITOR_DIAGNOSTIC_SPAWNS 12
#define EDITOR_SPAWN_COUNT (EDITOR_MAX_OBJECTS + 1 + EDITOR_DIAGNOSTIC_SPAWNS)
#define EDITOR_RELOAD_EXIT 75
#define EDITOR_ROLLBACK_EXIT 76
// Same flags the AP client gives its runtime spawns (ap/ap_spawn.c). 0x2000000
// (VISIBLE_DURING_GAMEPLAY) is RB_INSTANCE_SKIP_OT_RANGE to RenderBucket: with it
// the marker never gets an OT depth range and every draw was rejected.
#define EDITOR_SPAWN_FLAGS DRAW_COLLISION_MASK

enum EditorObjectKind
{
	EDITOR_OBJECT_ITEM_CRATE = 0,
	EDITOR_OBJECT_WUMPA_CRATE = 1,
	EDITOR_OBJECT_WUMPA_FRUIT = 2,
	EDITOR_OBJECT_AP_CANDIDATE = 3,
	EDITOR_OBJECT_KIND_COUNT = 4,
};

enum EditorTransformMode
{
	EDITOR_TRANSFORM_TRANSLATE = 0,
	EDITOR_TRANSFORM_ROTATE = 1,
};

struct EditorObject
{
	int id;
	int kind;
	Vec3 pos;
	SVec3 rot;
};

struct EditorSnapshot
{
	int count;
	int selected;
	int nextID;
	u8 selectedSet[EDITOR_MAX_OBJECTS];
	struct EditorObject objects[EDITOR_MAX_OBJECTS];
};

struct EditorSpawn
{
	int used;
	int modelID;
	Vec3 pos;
	SVec3 rot;
	u32 colour;
	struct Instance *instance;
	char name[16];
};

static int s_configured;
static int s_hostSlot = -1;
static char s_levPath[EDITOR_PATH_MAX];
static char s_sourceLevPath[EDITOR_PATH_MAX];
static char s_vrmPath[EDITOR_PATH_MAX];
static char s_levHash[65];
static char s_sourceLevHash[65];
static char s_vrmHash[65];
static char s_lastCleanExportHash[65];
static char s_lastCleanExportPath[EDITOR_PATH_MAX];
static char s_contentID[96] = "unconfigured";
static char s_contentTitle[128] = "Untitled track";
static char s_contentCreator[128];
static char s_contentVersion[32] = "0.1.0";
static char s_minimumEditorVersion[32] = "0.1.0-editor-dev";
static char s_sidecarPath[EDITOR_PATH_MAX] = "ctr-editor-project.json";
static char s_editorLogPath[EDITOR_PATH_MAX] = "ctr-editor.log";
static int s_loadRequested;
static int s_hotReloadEnabled;
static int s_launchValidated;
static int s_active;
static int s_capture;
static int s_hudCapacityWarned;
static int s_cameraBackedUp;
static int s_backupLevel = -1;
static struct CameraDC s_cameraBackup;
static SVec3 s_pushPosBackup;
static SVec3 s_pushRotBackup;
static u32 s_hudBackup;
static Vec3 s_cameraPos;
static SVec3 s_cameraRot;
static int s_cameraSpeed = 32;
static int s_cameraConfigured;
// Headless frame dump state (--editor-dump-*).
static char s_dumpDir[1024];
static int s_dumpEvery = 30;
static int s_dumpLimit;
static int s_dumpStart = 90;
static int s_dumpNoHud;
static int s_dumpMute;
static int s_dumpSkipIntro;
// Renderer A/B for the native-only two-pass textured semi-transparent draw
// path (DrawSplit, platform/native_gpu.c). 0 = shipped two-pass; 1 = collapse
// to a single blended pass so a dump run can tell whether the glass artifacts
// come from the STP two-pass masking. Editor-only.
int g_editorTextureSemiTransMode;
static int s_dumpActiveFrames;
static int s_dumpWritten;
static int s_dumpQuitRequested;
static int s_palette;
static int s_selected = -1;
static int s_nextID = 1;
static int s_objectCount;
static struct EditorObject s_objects[EDITOR_MAX_OBJECTS];
static struct EditorSnapshot s_undo[EDITOR_HISTORY_DEPTH];
static struct EditorSnapshot s_redo[EDITOR_HISTORY_DEPTH];
static int s_undoCount;
static int s_redoCount;
static int s_generation;
static int s_cleanGeneration;
static struct EditorSpawn s_spawns[EDITOR_SPAWN_COUNT];
static int s_prevKeys[SDL_SCANCODE_COUNT];
static SDL_MouseButtonFlags s_prevMouse;
static u8 s_selectedSet[EDITOR_MAX_OBJECTS];
static int s_hovered = -1;
static int s_transformMode;
static int s_transformAxis;
static int s_snapStep = 16;
static int s_mouseTransformActive;
static int s_inspectorPage;
static int s_overlayMode;
static int s_numericActive;
static char s_numericInput[16];
static int s_numericLength;
static int s_hasSurfaceHit;
static SVec3 s_surfaceHit;
static SVec3 s_surfaceNormal;
static struct QuadBlock *s_surfaceQuad;
static struct BSP *s_surfaceLeaf;
static int s_surfaceLeafIndex = -1;
static int s_surfacePVSInstances;
static int s_surfaceHitboxes;
static int s_sourceGraphMeasured;
static int s_sourceGlobalReferences;
static int s_sourcePVSLists;
static int s_sourcePVSReferences;
static int s_sourceBSPLists;
static int s_sourceBSPReferences;
static char s_status[160] = "Waiting for editor track";

static const char *const s_kindNames[] = {"item-crate", "wumpa-crate", "wumpa-fruit", "ap-candidate"};
static const int s_kindModels[] = {PU_RANDOM_CRATE, PU_FRUIT_CRATE, PU_WUMPA_FRUIT, PU_RANDOM_CRATE};

static int Editor_FindNearest(void);

static void Editor_Log(const char *format, ...)
{
	FILE *stream = fopen(s_editorLogPath, "ab");
	va_list arguments;
	if (stream == NULL)
		return;
	va_start(arguments, format);
	vfprintf(stream, format, arguments);
	va_end(arguments);
	fputc('\n', stream);
	fclose(stream);
}

static int Editor_ParseInt(const char *text, int *out)
{
	char *end;
	long value;
	if (text == NULL || *text == 0)
		return 0;
	value = strtol(text, &end, 10);
	if (*end != 0 || value < -2147483647L || value > 2147483647L)
		return 0;
	*out = (int)value;
	return 1;
}

static int Editor_NormalizeAngle(int angle)
{
	angle &= 0xfff;
	if (angle > 0x7ff)
		angle -= 0x1000;
	return angle;
}

static void Editor_CopyPath(char *destination, const char *source)
{
	size_t length = 0;
	if (source == NULL)
		return;
	while (length < EDITOR_PATH_MAX - 1 && source[length] != 0)
		length++;
	memmove(destination, source, length);
	destination[length] = 0;
}

static int Editor_KindFromName(const char *name)
{
	int kind;
	for (kind = 0; kind < EDITOR_OBJECT_KIND_COUNT; kind++)
		if (strcmp(name, s_kindNames[kind]) == 0)
			return kind;
	return -1;
}

static void Editor_ParseCamera(const char *text)
{
	int x, y, z, rx, ry, rz, speed;
	if (sscanf(text, "%d,%d,%d,%d,%d,%d,%d", &x, &y, &z, &rx, &ry, &rz, &speed) != 7)
		return;
	s_cameraSpeed = speed > 0 ? speed : 32;
	// Version-1 projects used an all-zero camera as their placeholder. CTR's
	// neutral camera pitch is 0x800, so replaying that placeholder would start
	// at the world origin with an inverted view. Let runtime initialization
	// replace this exact legacy sentinel with the live track camera.
	if (x == 0 && y == 0 && z == 0 && rx == 0 && ry == 0 && rz == 0)
		return;
	s_cameraPos.x = x;
	s_cameraPos.y = y;
	s_cameraPos.z = z;
	s_cameraRot.x = (s16)Editor_NormalizeAngle(rx);
	s_cameraRot.y = (s16)(ry & 0xfff);
	s_cameraRot.z = (s16)Editor_NormalizeAngle(rz);
	s_cameraConfigured = 1;
}

static void Editor_ParseObject(const char *text)
{
	struct EditorObject *object;
	char kindName[32];
	int id, x, y, z, rx, ry, rz, kind;
	if (s_objectCount >= EDITOR_MAX_OBJECTS ||
	    sscanf(text, "%d,%31[^,],%d,%d,%d,%d,%d,%d", &id, kindName, &x, &y, &z, &rx, &ry, &rz) != 8)
		return;
	kind = Editor_KindFromName(kindName);
	if (kind < 0)
		return;
	object = &s_objects[s_objectCount++];
	object->id = id;
	object->kind = kind;
	object->pos.x = x;
	object->pos.y = y;
	object->pos.z = z;
	object->rot.x = (s16)rx;
	object->rot.y = (s16)ry;
	object->rot.z = (s16)rz;
	if (id >= s_nextID)
		s_nextID = id + 1;
}

void Editor_ConfigureFromArgs(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--editor-lev") == 0 && i + 1 < argc)
			Editor_CopyPath(s_levPath, argv[++i]);
		else if (strcmp(argv[i], "--editor-vrm") == 0 && i + 1 < argc)
			Editor_CopyPath(s_vrmPath, argv[++i]);
		else if (strcmp(argv[i], "--editor-lev-sha256") == 0 && i + 1 < argc)
		{
			strncpy(s_levHash, argv[++i], sizeof(s_levHash) - 1);
			s_levHash[sizeof(s_levHash) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-source-lev") == 0 && i + 1 < argc)
			Editor_CopyPath(s_sourceLevPath, argv[++i]);
		else if (strcmp(argv[i], "--editor-source-lev-sha256") == 0 && i + 1 < argc)
		{
			strncpy(s_sourceLevHash, argv[++i], sizeof(s_sourceLevHash) - 1);
			s_sourceLevHash[sizeof(s_sourceLevHash) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-vrm-sha256") == 0 && i + 1 < argc)
		{
			strncpy(s_vrmHash, argv[++i], sizeof(s_vrmHash) - 1);
			s_vrmHash[sizeof(s_vrmHash) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-host-slot") == 0 && i + 1 < argc)
			Editor_ParseInt(argv[++i], &s_hostSlot);
		else if (strcmp(argv[i], "--editor-sidecar") == 0 && i + 1 < argc)
			Editor_CopyPath(s_sidecarPath, argv[++i]);
		else if (strcmp(argv[i], "--editor-hot-reload") == 0)
			s_hotReloadEnabled = 1;
		else if (strcmp(argv[i], "--editor-validation-ok") == 0)
			s_launchValidated = 1;
		else if (strcmp(argv[i], "--editor-log") == 0 && i + 1 < argc)
			Editor_CopyPath(s_editorLogPath, argv[++i]);
		else if (strcmp(argv[i], "--editor-camera") == 0 && i + 1 < argc)
			Editor_ParseCamera(argv[++i]);
		else if (strcmp(argv[i], "--editor-object") == 0 && i + 1 < argc)
			Editor_ParseObject(argv[++i]);
		else if (strcmp(argv[i], "--editor-generation") == 0 && i + 1 < argc)
			Editor_ParseInt(argv[++i], &s_generation);
		else if (strcmp(argv[i], "--editor-clean-generation") == 0 && i + 1 < argc)
			Editor_ParseInt(argv[++i], &s_cleanGeneration);
		else if (strcmp(argv[i], "--editor-last-export-sha256") == 0 && i + 1 < argc)
		{
			strncpy(s_lastCleanExportHash, argv[++i], sizeof(s_lastCleanExportHash) - 1);
			s_lastCleanExportHash[sizeof(s_lastCleanExportHash) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-last-export-path") == 0 && i + 1 < argc)
			Editor_CopyPath(s_lastCleanExportPath, argv[++i]);
		else if (strcmp(argv[i], "--editor-content-id") == 0 && i + 1 < argc)
		{
			strncpy(s_contentID, argv[++i], sizeof(s_contentID) - 1); s_contentID[sizeof(s_contentID) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-content-title") == 0 && i + 1 < argc)
		{
			strncpy(s_contentTitle, argv[++i], sizeof(s_contentTitle) - 1); s_contentTitle[sizeof(s_contentTitle) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-content-creator") == 0 && i + 1 < argc)
		{
			strncpy(s_contentCreator, argv[++i], sizeof(s_contentCreator) - 1); s_contentCreator[sizeof(s_contentCreator) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-content-version") == 0 && i + 1 < argc)
		{
			strncpy(s_contentVersion, argv[++i], sizeof(s_contentVersion) - 1); s_contentVersion[sizeof(s_contentVersion) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-minimum-version") == 0 && i + 1 < argc)
		{
			strncpy(s_minimumEditorVersion, argv[++i], sizeof(s_minimumEditorVersion) - 1); s_minimumEditorVersion[sizeof(s_minimumEditorVersion) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-dump-dir") == 0 && i + 1 < argc)
		{
			strncpy(s_dumpDir, argv[++i], sizeof(s_dumpDir) - 1); s_dumpDir[sizeof(s_dumpDir) - 1] = 0;
		}
		else if (strcmp(argv[i], "--editor-dump-every") == 0 && i + 1 < argc)
		{
			int value = 0;
			if (Editor_ParseInt(argv[++i], &value) && value > 0)
				s_dumpEvery = value;
		}
		else if (strcmp(argv[i], "--editor-dump-count") == 0 && i + 1 < argc)
		{
			int value = 0;
			if (Editor_ParseInt(argv[++i], &value) && value >= 0)
				s_dumpLimit = value;
		}
		else if (strcmp(argv[i], "--editor-dump-start") == 0 && i + 1 < argc)
		{
			int value = 0;
			if (Editor_ParseInt(argv[++i], &value) && value >= 0)
				s_dumpStart = value;
		}
		else if (strcmp(argv[i], "--editor-dump-nohud") == 0)
		{
			s_dumpNoHud = 1;
		}
		else if (strcmp(argv[i], "--editor-dump-mute") == 0)
		{
			s_dumpMute = 1;
		}
		else if (strcmp(argv[i], "--editor-dump-skip-intro") == 0)
		{
			s_dumpSkipIntro = 1;
		}
		else if (strcmp(argv[i], "--editor-render-semitrans-single") == 0)
		{
			g_editorTextureSemiTransMode = 1;
		}
	}

	if (s_sourceLevPath[0] == 0)
		Editor_CopyPath(s_sourceLevPath, s_levPath);
	if (s_sourceLevHash[0] == 0)
	{
		strncpy(s_sourceLevHash, s_levHash, sizeof(s_sourceLevHash) - 1);
		s_sourceLevHash[sizeof(s_sourceLevHash) - 1] = 0;
	}
	s_configured = s_levPath[0] != 0 && s_sourceLevPath[0] != 0 && s_vrmPath[0] != 0 && s_hostSlot >= 0 && s_hostSlot < 18;
	if (s_configured)
	{
		printf("[CTR Editor] LEV: %s\n", s_levPath);
		printf("[CTR Editor] VRM: %s\n", s_vrmPath);
		printf("[CTR Editor] host slot confirmed: %d\n", s_hostSlot);
		printf("[CTR Editor] sidecar: %s\n", s_sidecarPath);
		Editor_Log("LOAD project=%s active_lev=%s source_lev=%s vrm=%s host_slot=%d", s_sidecarPath, s_levHash, s_sourceLevHash, s_vrmHash, s_hostSlot);
		if (s_dumpDir[0] != 0)
		{
			SDL_CreateDirectory(s_dumpDir);
			printf("[CTR Editor] frame dump: dir=%s every=%d count=%d start=%d nohud=%d\n", s_dumpDir, s_dumpEvery, s_dumpLimit,
			       s_dumpStart, s_dumpNoHud);
			Editor_Log("DUMP_CONFIG dir=%s every=%d count=%d start=%d nohud=%d", s_dumpDir, s_dumpEvery, s_dumpLimit, s_dumpStart,
			           s_dumpNoHud);
		}
	}
	else
	{
		printf("[CTR Editor] No project configured. Use --editor-lev FILE --editor-vrm FILE --editor-host-slot 0..17 [--editor-sidecar FILE]\n");
	}
}

// Called from main.c immediately after NativeConfig_Load so a dump run can
// silence audio and skip the boot intro regardless of config.ini. The volume
// fields are reapplied authoritatively by RaceConfig_LoadGameOptions at race
// load, so zeroing them here silences the track itself, not only the boot
// sequence.
void Editor_ApplyDumpRuntimeOverrides(void)
{
	if (s_dumpMute)
	{
		g_config.volFx = 0;
		g_config.volMusic = 0;
		g_config.volVoice = 0;
		Editor_Log("DUMP_RUNTIME mute=1");
	}
	if (s_dumpSkipIntro)
	{
		g_config.skipIntro = 1;
		Editor_Log("DUMP_RUNTIME skip_intro=1");
	}
}

int Editor_IsConfigured(void)
{
	return s_configured;
}

int Editor_IsInputCaptured(void)
{
	return s_capture;
}

int Editor_GetLoadOverride(int subfileIndex, const char **path, u32 *size)
{
	struct stat status;
	const char *selected = NULL;
	if (!s_configured)
		return 0;
	if (subfileIndex == BI_ARCADETRACKS + s_hostSlot * LOAD_TRACK_FILES_PER_LOD_GROUP + LVI_VRAM)
		selected = s_vrmPath;
	else if (subfileIndex == BI_ARCADETRACKS + s_hostSlot * LOAD_TRACK_FILES_PER_LOD_GROUP + LVI_LEV)
		selected = s_levPath;
	else
		return 0;
	if (stat(selected, &status) != 0 || status.st_size <= 0 || (unsigned long)status.st_size > 0xffffffffUL)
	{
		printf("[CTR Editor] ERROR: override file unavailable: %s\n", selected);
		Editor_Log("REFUSAL unavailable_override=%s", selected);
		return 0;
	}
	if (path != NULL)
		*path = selected;
	if (size != NULL)
		*size = (u32)status.st_size;
	printf("[CTR Editor] serving subfile %d from %s (%ld bytes)\n", subfileIndex, selected, (long)status.st_size);
	return 1;
}

int Editor_ReadLoadOverride(const char *path, void *destination, u32 bufferBytes, u32 fileBytes)
{
	FILE *stream;
	size_t received;
	if (path == NULL || destination == NULL || fileBytes > bufferBytes)
		return 0;
	stream = fopen(path, "rb");
	if (stream == NULL)
		return 0;
	received = fread(destination, 1, fileBytes, stream);
	fclose(stream);
	if (received != fileBytes)
		return 0;
	if (bufferBytes > fileBytes)
		memset((u8 *)destination + fileBytes, 0, bufferBytes - fileBytes);
	return 1;
}

static struct EditorSnapshot Editor_CaptureSnapshot(void)
{
	struct EditorSnapshot snapshot;
	snapshot.count = s_objectCount;
	snapshot.selected = s_selected;
	snapshot.nextID = s_nextID;
	memcpy(snapshot.selectedSet, s_selectedSet, sizeof(s_selectedSet));
	memcpy(snapshot.objects, s_objects, sizeof(s_objects));
	return snapshot;
}

static void Editor_PushSnapshot(struct EditorSnapshot *history, int *count)
{
	if (*count == EDITOR_HISTORY_DEPTH)
	{
		memmove(&history[0], &history[1], sizeof(history[0]) * (EDITOR_HISTORY_DEPTH - 1));
		*count = EDITOR_HISTORY_DEPTH - 1;
	}
	history[(*count)++] = Editor_CaptureSnapshot();
}

static void Editor_SpawnRemoveAll(void)
{
	int i;
	for (i = 0; i < EDITOR_SPAWN_COUNT; i++)
	{
		if (s_spawns[i].instance != NULL)
			INSTANCE_Death(s_spawns[i].instance);
		memset(&s_spawns[i], 0, sizeof(s_spawns[i]));
	}
}

static void Editor_RebuildSpawns(void)
{
	int i;
	Editor_SpawnRemoveAll();
	for (i = 0; i < s_objectCount; i++)
	{
		struct EditorSpawn *spawn = &s_spawns[i];
		spawn->used = 1;
		spawn->modelID = s_kindModels[s_objects[i].kind];
		spawn->pos = s_objects[i].pos;
		spawn->rot = s_objects[i].rot;
		spawn->colour = s_selectedSet[i] ? 0x0000ffff :
		                (s_objects[i].kind == EDITOR_OBJECT_AP_CANDIDATE ? 0x00ff80ff : 0);
		snprintf(spawn->name, sizeof(spawn->name), "edit%04d", s_objects[i].id);
	}
}

static void Editor_RestoreSnapshot(const struct EditorSnapshot *snapshot)
{
	s_objectCount = snapshot->count;
	s_selected = snapshot->selected;
	s_nextID = snapshot->nextID;
	memcpy(s_selectedSet, snapshot->selectedSet, sizeof(s_selectedSet));
	memcpy(s_objects, snapshot->objects, sizeof(s_objects));
	Editor_RebuildSpawns();
}

static void Editor_Undo(void)
{
	if (s_undoCount == 0)
		return;
	Editor_PushSnapshot(s_redo, &s_redoCount);
	Editor_RestoreSnapshot(&s_undo[--s_undoCount]);
	s_generation++;
	snprintf(s_status, sizeof(s_status), "Undo: %d objects", s_objectCount);
}

static void Editor_Redo(void)
{
	if (s_redoCount == 0)
		return;
	Editor_PushSnapshot(s_undo, &s_undoCount);
	Editor_RestoreSnapshot(&s_redo[--s_redoCount]);
	s_generation++;
	snprintf(s_status, sizeof(s_status), "Redo: %d objects", s_objectCount);
}

static void Editor_BeforeMutation(void)
{
	Editor_PushSnapshot(s_undo, &s_undoCount);
	s_redoCount = 0;
	s_generation++;
}

static void Editor_WriteJsonString(FILE *stream, const char *text)
{
	const unsigned char *cursor = (const unsigned char *)text;
	fputc('"', stream);
	while (*cursor)
	{
		if (*cursor == '"' || *cursor == '\\')
			fputc('\\', stream);
		if (*cursor >= 0x20)
			fputc(*cursor, stream);
		cursor++;
	}
	fputc('"', stream);
}

static int Editor_SaveSidecar(void)
{
	char temporary[EDITOR_PATH_MAX + 8];
	FILE *stream;
	int i, emitted;
	if (snprintf(temporary, sizeof(temporary), "%s.tmp", s_sidecarPath) >= (int)sizeof(temporary))
	{
		snprintf(s_status, sizeof(s_status), "Save failed: project path is too long");
		Editor_Log("SAVE_REFUSAL path_too_long project=%s", s_sidecarPath);
		return 0;
	}
	stream = fopen(temporary, "wb");
	if (stream == NULL)
	{
		snprintf(s_status, sizeof(s_status), "Save failed: cannot open temporary project");
		Editor_Log("SAVE_REFUSAL open_temp=%s errno=%d", temporary, errno);
		return 0;
	}
	fprintf(stream, "{\n  \"schema\": \"ctr-native-editor-project\",\n  \"version\": 1,\n  \"metadata\": {\"content_id\": ");
	Editor_WriteJsonString(stream, s_contentID);
	fprintf(stream, ", \"title\": "); Editor_WriteJsonString(stream, s_contentTitle);
	fprintf(stream, ", \"creator\": "); Editor_WriteJsonString(stream, s_contentCreator);
	fprintf(stream, ", \"content_version\": "); Editor_WriteJsonString(stream, s_contentVersion);
	fprintf(stream, ", \"minimum_editor_version\": "); Editor_WriteJsonString(stream, s_minimumEditorVersion);
	fprintf(stream, "},\n  \"sources\": {\n    \"lev\": {\"path\": ");
	Editor_WriteJsonString(stream, s_sourceLevPath);
	fprintf(stream, ", \"sha256\": ");
	Editor_WriteJsonString(stream, s_sourceLevHash);
	fprintf(stream, "},\n    \"vrm\": {\"path\": ");
	Editor_WriteJsonString(stream, s_vrmPath);
	fprintf(stream, ", \"sha256\": ");
	Editor_WriteJsonString(stream, s_vrmHash);
	fprintf(stream, "}\n  },\n  \"host_slot\": %d,\n  \"units\": \"ctr-world-s16\",\n", s_hostSlot);
	fprintf(stream, "  \"camera\": {\"position\": [%d, %d, %d], \"rotation\": [%d, %d, %d], \"speed\": %d},\n",
	        s_cameraPos.x, s_cameraPos.y, s_cameraPos.z, s_cameraRot.x, s_cameraRot.y, s_cameraRot.z, s_cameraSpeed);
	fprintf(stream, "  \"vanilla_objects\": [\n");
	emitted = 0;
	for (i = 0; i < s_objectCount; i++)
	{
		struct EditorObject *object = &s_objects[i];
		if (object->kind == EDITOR_OBJECT_AP_CANDIDATE)
			continue;
		fprintf(stream, "%s    {\"id\": \"obj-%04d\", \"kind\": \"%s\", \"provenance\": \"authored\", \"position\": [%d, %d, %d], \"rotation\": [%d, %d, %d]}",
		        emitted++ ? ",\n" : "",
		        object->id, s_kindNames[object->kind], object->pos.x, object->pos.y, object->pos.z,
		        object->rot.x, object->rot.y, object->rot.z);
	}
	fprintf(stream, "%s  ],\n  \"ap_candidates\": [\n", emitted ? "\n" : "");
	{
		int ordinal = 0;
		emitted = 0;
		for (i = 0; i < s_objectCount; i++)
		{
			struct EditorObject *object = &s_objects[i];
			if (object->kind != EDITOR_OBJECT_AP_CANDIDATE)
				continue;
			fprintf(stream, "%s    {\"id\": \"ap-%04d\", \"ordinal\": %d, \"position\": [%d, %d, %d], \"rotation\": [%d, %d, %d]}",
			        emitted++ ? ",\n" : "",
			        object->id, ordinal++, object->pos.x, object->pos.y, object->pos.z,
			        object->rot.x, object->rot.y, object->rot.z);
		}
	}
	fprintf(stream, "%s  ],\n  \"history\": {\"generation\": %d, \"clean_generation\": %d},\n  \"last_clean_export_sha256\": ",
	        emitted ? "\n" : "", s_generation, s_cleanGeneration);
	if (s_lastCleanExportHash[0])
		Editor_WriteJsonString(stream, s_lastCleanExportHash);
	else
		fprintf(stream, "null");
	fprintf(stream, ",\n  \"last_clean_export_path\": ");
	if (s_lastCleanExportPath[0])
		Editor_WriteJsonString(stream, s_lastCleanExportPath);
	else
		fprintf(stream, "null");
	fprintf(stream, "\n}\n");
	if (fclose(stream) != 0)
	{
		snprintf(s_status, sizeof(s_status), "Save failed: cannot flush temporary project");
		Editor_Log("SAVE_REFUSAL close_temp=%s errno=%d", temporary, errno);
		return 0;
	}
	{
		char replaceError[128];
		replaceError[0] = 0;
		if (!Editor_ReplaceFileAtomically(temporary, s_sidecarPath, replaceError, sizeof(replaceError)))
		{
			snprintf(s_status, sizeof(s_status), "Save failed: %s", replaceError);
			Editor_Log("SAVE_REFUSAL replace project=%s reason=%s", s_sidecarPath, replaceError);
			return 0;
		}
	}
	snprintf(s_status, sizeof(s_status), "Saved %d objects", s_objectCount);
	Editor_Log("SAVE project=%s generation=%d objects=%d", s_sidecarPath, s_generation, s_objectCount);
	return 1;
}

static int Editor_KeyTapped(const bool *keys, SDL_Scancode key)
{
	int down = keys != NULL && keys[key];
	int tapped = down && !s_prevKeys[key];
	s_prevKeys[key] = down;
	return tapped;
}

static int Editor_ClampS16(int value)
{
	if (value < -32768)
		return -32768;
	if (value > 32767)
		return 32767;
	return value;
}

static void Editor_Forward(Vec3 *forward)
{
	int yawSin = MATH_Sin((u32)s_cameraRot.y);
	int yawCos = MATH_Cos((u32)s_cameraRot.y);
	int pitchSin = MATH_Sin((u32)s_cameraRot.x);
	int pitchCos = MATH_Cos((u32)s_cameraRot.x);
	forward->x = -(yawSin * pitchCos >> 12);
	forward->y = pitchSin;
	forward->z = -(yawCos * pitchCos >> 12);
}

static void Editor_SurfacePick(struct GameTracker *gGT)
{
	struct ScratchpadStruct *scratch = &sdata->scratchpadStruct;
	Vec3 forward;
	SVec3 begin;
	SVec3 end;
	if (gGT->level1 == NULL || gGT->level1->ptr_mesh_info == NULL)
	{
		s_hasSurfaceHit = 0;
		return;
	}
	Editor_Forward(&forward);
	begin.x = (s16)Editor_ClampS16(s_cameraPos.x);
	begin.y = (s16)Editor_ClampS16(s_cameraPos.y);
	begin.z = (s16)Editor_ClampS16(s_cameraPos.z);
	end.x = (s16)Editor_ClampS16(s_cameraPos.x + (forward.x * 0x6000 >> 12));
	end.y = (s16)Editor_ClampS16(s_cameraPos.y + (forward.y * 0x6000 >> 12));
	end.z = (s16)Editor_ClampS16(s_cameraPos.z + (forward.z * 0x6000 >> 12));
	scratch->Union.QuadBlockColl.quadFlagsWanted = QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_COLLISION_SURFACE | QUADBLOCK_FLAG_CAMERA_SEARCH;
	scratch->Union.QuadBlockColl.quadFlagsIgnored = 0;
	scratch->Union.QuadBlockColl.searchFlags = COLL_SEARCH_HIGH_LOD;
	scratch->ptr_mesh_info = gGT->level1->ptr_mesh_info;
	COLL_SearchBSP_CallbackQUADBLK(&begin, &end, scratch, 0);
	s_hasSurfaceHit = scratch->boolDidTouchQuadblock != 0;
	if (s_hasSurfaceHit)
	{
		s_surfaceHit = scratch->Union.QuadBlockColl.hitPos;
		s_surfaceNormal = scratch->hit.plane.normal;
		s_surfaceQuad = scratch->hit.ptrQuadblock;
	}
}

static int Editor_PointInsideBox(const SVec3 *point, const struct BoundingBox *box)
{
	return point->x >= box->min.x && point->x <= box->max.x &&
	       point->y >= box->min.y && point->y <= box->max.y &&
	       point->z >= box->min.z && point->z <= box->max.z;
}

static void Editor_UpdateSurfaceDiagnostics(struct GameTracker *gGT)
{
	struct mesh_info *mesh;
	int i;
	s_surfaceLeaf = NULL;
	s_surfaceLeafIndex = -1;
	s_surfacePVSInstances = 0;
	s_surfaceHitboxes = 0;
	if (!s_hasSurfaceHit || gGT->level1 == NULL || gGT->level1->ptr_mesh_info == NULL)
		return;
	mesh = gGT->level1->ptr_mesh_info;
	for (i = 0; i < mesh->numBspNodes; i++)
	{
		struct BSP *node = &mesh->bspRoot[i];
		if ((node->flag & BSP_NODE_FLAG_LEAF) && Editor_PointInsideBox(&s_surfaceHit, &node->box))
		{
			s_surfaceLeaf = node;
			s_surfaceLeafIndex = i;
			break;
		}
	}
	if (s_surfaceQuad != NULL && s_surfaceQuad->pvs != NULL && s_surfaceQuad->pvs->visInstSrc != NULL)
	{
		while (s_surfacePVSInstances < 10000 && s_surfaceQuad->pvs->visInstSrc[s_surfacePVSInstances] != NULL)
			s_surfacePVSInstances++;
	}
	if (s_surfaceLeaf != NULL && s_surfaceLeaf->data.leaf.bspHitboxArray != NULL)
	{
		while (s_surfaceHitboxes < 10000 && s_surfaceLeaf->data.leaf.bspHitboxArray[s_surfaceHitboxes].flag != 0)
			s_surfaceHitboxes++;
	}
}

static void Editor_MeasureSourceGraph(struct GameTracker *gGT)
{
	struct mesh_info *mesh;
	int i, j;
	if (s_sourceGraphMeasured || gGT->level1 == NULL || gGT->level1->ptr_mesh_info == NULL)
		return;
	mesh = gGT->level1->ptr_mesh_info;
	s_sourceGlobalReferences = (int)gGT->level1->numInstances;
	s_sourcePVSLists = 0;
	s_sourcePVSReferences = 0;
	for (i = 0; i < mesh->numQuadBlock; i++)
	{
		struct Instance **list;
		int duplicate = 0;
		if (mesh->ptrQuadBlockArray[i].pvs == NULL)
			continue;
		list = mesh->ptrQuadBlockArray[i].pvs->visInstSrc;
		if (list == NULL)
			continue;
		for (j = 0; j < i; j++)
			if (mesh->ptrQuadBlockArray[j].pvs != NULL && mesh->ptrQuadBlockArray[j].pvs->visInstSrc == list)
				duplicate = 1;
		if (duplicate)
			continue;
		s_sourcePVSLists++;
		for (j = 0; j < 100000 && list[j] != NULL; j++)
			s_sourcePVSReferences++;
	}
	s_sourceBSPLists = 0;
	s_sourceBSPReferences = 0;
	for (i = 0; i < mesh->numBspNodes; i++)
	{
		struct BSP *list;
		int duplicate = 0;
		if (!(mesh->bspRoot[i].flag & BSP_NODE_FLAG_LEAF))
			continue;
		list = mesh->bspRoot[i].data.leaf.bspHitboxArray;
		if (list == NULL)
			continue;
		for (j = 0; j < i; j++)
			if ((mesh->bspRoot[j].flag & BSP_NODE_FLAG_LEAF) && mesh->bspRoot[j].data.leaf.bspHitboxArray == list)
				duplicate = 1;
		if (duplicate)
			continue;
		s_sourceBSPLists++;
		for (j = 0; j < 100000 && list[j].flag != 0; j++)
			if (list[j].data.hitbox.instDef != NULL)
				s_sourceBSPReferences++;
	}
	s_sourceGraphMeasured = 1;
}

static void Editor_UpdatePVS(struct GameTracker *gGT, struct CameraDC *camera)
{
	struct ScratchpadStruct *scratch = &sdata->scratchpadStruct;
	struct QuadBlock *previous = camera->ptrQuadBlock;
	Vec3 position = s_cameraPos;
	CAM_FindClosestQuadblock(scratch, camera, camera->driverToFollow, &position);
	if (!camera->quadBlockSearchHit && s_surfaceQuad != NULL)
	{
		camera->ptrQuadBlock = s_surfaceQuad;
		camera->quadBlockSearchHit = true;
	}
	else if (!camera->quadBlockSearchHit && previous != NULL)
	{
		camera->ptrQuadBlock = previous;
		camera->quadBlockSearchHit = true;
	}
	if (camera->ptrQuadBlock != NULL && camera->ptrQuadBlock->pvs != NULL)
	{
		struct PVS *pvs = camera->ptrQuadBlock->pvs;
		camera->visLeafSrc = pvs->visLeafSrc;
		camera->visFaceSrc = pvs->visFaceSrc;
		camera->visInstSrc = pvs->visInstSrc;
		if ((gGT->level1->configFlags & 4) == 0)
			camera->visOVertSrc = pvs->visExtraSrc;
		else
			camera->visSCVertSrc = pvs->visExtraSrc;
	}
}

static void Editor_PlaceObject(void)
{
	struct EditorObject *object;
	Vec3 forward;
	int saved;
	if (s_objectCount >= EDITOR_MAX_OBJECTS)
	{
		snprintf(s_status, sizeof(s_status), "Object limit reached (%d)", EDITOR_MAX_OBJECTS);
		return;
	}
	Editor_BeforeMutation();
	object = &s_objects[s_objectCount++];
	memset(object, 0, sizeof(*object));
	object->id = s_nextID++;
	object->kind = s_palette;
	if (s_hasSurfaceHit)
	{
		object->pos.x = s_surfaceHit.x;
		object->pos.y = s_surfaceHit.y;
		object->pos.z = s_surfaceHit.z;
	}
	else
	{
		Editor_Forward(&forward);
		object->pos.x = s_cameraPos.x + (forward.x >> 3);
		object->pos.y = s_cameraPos.y + (forward.y >> 3);
		object->pos.z = s_cameraPos.z + (forward.z >> 3);
	}
	object->rot.y = s_cameraRot.y;
	s_selected = s_objectCount - 1;
	memset(s_selectedSet, 0, sizeof(s_selectedSet));
	s_selectedSet[s_selected] = 1;
	Editor_RebuildSpawns();
	saved = Editor_SaveSidecar();
	Editor_Log("PLACE id=obj-%04d kind=%s position=%d,%d,%d pick=%s saved=%d", object->id, s_kindNames[object->kind], object->pos.x,
	           object->pos.y, object->pos.z, s_hasSurfaceHit ? "hit" : "miss", saved);
}

static void Editor_DeleteSelected(void)
{
	if (s_selected < 0 || s_selected >= s_objectCount)
		return;
	Editor_BeforeMutation();
	memmove(&s_selectedSet[s_selected], &s_selectedSet[s_selected + 1], (size_t)(s_objectCount - s_selected - 1));
	memmove(&s_objects[s_selected], &s_objects[s_selected + 1], sizeof(s_objects[0]) * (s_objectCount - s_selected - 1));
	s_objectCount--;
	s_selectedSet[s_objectCount] = 0;
	if (s_objectCount == 0)
		s_selected = -1;
	else if (s_selected >= s_objectCount)
		s_selected = s_objectCount - 1;
	if (s_selected >= 0)
		s_selectedSet[s_selected] = 1;
	Editor_RebuildSpawns();
	Editor_SaveSidecar();
}

static void Editor_MoveSelected(int x, int y, int z, int yaw)
{
	int i;
	if (s_selected < 0 || s_selected >= s_objectCount)
		return;
	Editor_BeforeMutation();
	for (i = 0; i < s_objectCount; i++)
	{
		struct EditorObject *object;
		if (!s_selectedSet[i])
			continue;
		object = &s_objects[i];
		object->pos.x = Editor_ClampS16(object->pos.x + x);
		object->pos.y = Editor_ClampS16(object->pos.y + y);
		object->pos.z = Editor_ClampS16(object->pos.z + z);
		object->rot.y = (s16)((object->rot.y + yaw) & 0xfff);
	}
	Editor_RebuildSpawns();
	Editor_SaveSidecar();
}

static int Editor_SelectedCount(void)
{
	int i, count = 0;
	for (i = 0; i < s_objectCount; i++)
		count += s_selectedSet[i] != 0;
	return count;
}

static void Editor_ApplyMouseTransform(int horizontal, int vertical)
{
	int i;
	int amount = horizontal - vertical;
	if (amount == 0)
		return;
	for (i = 0; i < s_objectCount; i++)
	{
		struct EditorObject *object;
		if (!s_selectedSet[i])
			continue;
		object = &s_objects[i];
		if (s_transformMode == EDITOR_TRANSFORM_ROTATE)
		{
			s16 *rotation = s_transformAxis == 0 ? &object->rot.x : (s_transformAxis == 1 ? &object->rot.y : &object->rot.z);
			*rotation = (s16)Editor_NormalizeAngle(*rotation + amount * 16);
		}
		else
		{
			int *coordinate = s_transformAxis == 0 ? &object->pos.x : (s_transformAxis == 1 ? &object->pos.y : &object->pos.z);
			*coordinate = Editor_ClampS16(*coordinate + amount * s_snapStep);
		}
	}
	Editor_RebuildSpawns();
}

static void Editor_StartNumericEdit(void)
{
	struct EditorObject *object;
	int value;
	if (s_selected < 0 || s_selected >= s_objectCount)
		return;
	object = &s_objects[s_selected];
	if (s_transformMode == EDITOR_TRANSFORM_ROTATE)
		value = s_transformAxis == 0 ? object->rot.x : (s_transformAxis == 1 ? object->rot.y : object->rot.z);
	else
		value = s_transformAxis == 0 ? object->pos.x : (s_transformAxis == 1 ? object->pos.y : object->pos.z);
	snprintf(s_numericInput, sizeof(s_numericInput), "%d", value);
	s_numericLength = (int)strlen(s_numericInput);
	s_numericActive = 1;
}

static void Editor_CommitNumericEdit(void)
{
	int value, i;
	if (!Editor_ParseInt(s_numericInput, &value))
	{
		snprintf(s_status, sizeof(s_status), "Invalid numeric value: %s", s_numericInput);
		s_numericActive = 0;
		return;
	}
	Editor_BeforeMutation();
	for (i = 0; i < s_objectCount; i++)
	{
		struct EditorObject *object;
		if (!s_selectedSet[i])
			continue;
		object = &s_objects[i];
		if (s_transformMode == EDITOR_TRANSFORM_ROTATE)
		{
			s16 *rotation = s_transformAxis == 0 ? &object->rot.x : (s_transformAxis == 1 ? &object->rot.y : &object->rot.z);
			*rotation = (s16)Editor_NormalizeAngle(value);
		}
		else
		{
			int *coordinate = s_transformAxis == 0 ? &object->pos.x : (s_transformAxis == 1 ? &object->pos.y : &object->pos.z);
			*coordinate = Editor_ClampS16(value);
		}
	}
	s_numericActive = 0;
	Editor_RebuildSpawns();
	Editor_SaveSidecar();
}

static int Editor_UpdateNumericEdit(const bool *keys)
{
	static const SDL_Scancode digitKeys[10] = {SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
	                                                 SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9};
	static const SDL_Scancode keypadKeys[10] = {SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4,
	                                                  SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9};
	int digit;
	if (!s_numericActive)
		return 0;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_ESCAPE))
	{
		s_numericActive = 0;
		return 1;
	}
	if (Editor_KeyTapped(keys, SDL_SCANCODE_RETURN) || Editor_KeyTapped(keys, SDL_SCANCODE_KP_ENTER))
	{
		Editor_CommitNumericEdit();
		return 1;
	}
	if (Editor_KeyTapped(keys, SDL_SCANCODE_BACKSPACE) && s_numericLength > 0)
		s_numericInput[--s_numericLength] = 0;
	if ((Editor_KeyTapped(keys, SDL_SCANCODE_MINUS) || Editor_KeyTapped(keys, SDL_SCANCODE_KP_MINUS)) &&
	    s_numericLength == 0 && s_numericLength < (int)sizeof(s_numericInput) - 1)
	{
		s_numericInput[s_numericLength++] = '-';
		s_numericInput[s_numericLength] = 0;
	}
	for (digit = 0; digit < 10; digit++)
	{
		if ((Editor_KeyTapped(keys, digitKeys[digit]) || Editor_KeyTapped(keys, keypadKeys[digit])) &&
		    s_numericLength < (int)sizeof(s_numericInput) - 1)
		{
			s_numericInput[s_numericLength++] = (char)('0' + digit);
			s_numericInput[s_numericLength] = 0;
		}
	}
	return 1;
}

int Editor_RunSelfTests(void)
{
	struct EditorSnapshot snapshot;
	char copied[EDITOR_PATH_MAX];
	memset(s_objects, 0, sizeof(s_objects));
	memset(s_selectedSet, 0, sizeof(s_selectedSet));
	memset(s_spawns, 0, sizeof(s_spawns));
	s_objectCount = 2;
	s_selected = 0;
	s_objects[0].id = 1;
	s_objects[0].pos = (Vec3){100, 200, 300};
	s_objects[1].id = 2;
	s_objects[1].pos = (Vec3){140, 200, 300};
	s_selectedSet[0] = 1;
	s_selectedSet[1] = 1;
	s_transformMode = EDITOR_TRANSFORM_TRANSLATE;
	s_transformAxis = 0;
	s_snapStep = 16;
	Editor_ApplyMouseTransform(2, 0);
	if (s_objects[0].pos.x != 132 || s_objects[1].pos.x != 172)
		return 1;
	s_transformMode = EDITOR_TRANSFORM_ROTATE;
	s_transformAxis = 2;
	Editor_ApplyMouseTransform(1, 0);
	if (s_objects[0].rot.z != 16 || s_objects[1].rot.z != 16)
		return 2;
	snapshot = Editor_CaptureSnapshot();
	s_objects[0].pos.x = -1;
	s_selectedSet[1] = 0;
	Editor_RestoreSnapshot(&snapshot);
	if (s_objects[0].pos.x != 132 || !s_selectedSet[1])
		return 3;
	s_hasSurfaceHit = 1;
	s_surfaceHit = (SVec3){170, 200, 300};
	if (Editor_FindNearest() != 1)
		return 4;
	Editor_CopyPath(copied, "D:\\tracks\\source.lev");
	if (strcmp(copied, "D:\\tracks\\source.lev") != 0)
		return 5;
	if (Editor_ClampS16(-40000) != -32768 || Editor_ClampS16(40000) != 32767 || Editor_NormalizeAngle(0xF00) != -0x100)
		return 6;
	printf("CTR Editor self-test: PASS\n");
	return 0;
}

static void Editor_DuplicateSelected(void)
{
	struct EditorObject *copy;
	if (s_selected < 0 || s_selected >= s_objectCount || s_objectCount >= EDITOR_MAX_OBJECTS)
		return;
	Editor_BeforeMutation();
	copy = &s_objects[s_objectCount];
	*copy = s_objects[s_selected];
	copy->id = s_nextID++;
	copy->pos.x = Editor_ClampS16(copy->pos.x + 32);
	s_selected = s_objectCount++;
	memset(s_selectedSet, 0, sizeof(s_selectedSet));
	s_selectedSet[s_selected] = 1;
	Editor_RebuildSpawns();
	Editor_SaveSidecar();
}

static void Editor_SnapSelectedToSurface(void)
{
	int i, dx, dy, dz;
	if (!s_hasSurfaceHit || s_selected < 0 || s_selected >= s_objectCount)
		return;
	Editor_BeforeMutation();
	dx = s_surfaceHit.x - s_objects[s_selected].pos.x;
	dy = s_surfaceHit.y - s_objects[s_selected].pos.y;
	dz = s_surfaceHit.z - s_objects[s_selected].pos.z;
	for (i = 0; i < s_objectCount; i++)
	{
		if (!s_selectedSet[i])
			continue;
		s_objects[i].pos.x = Editor_ClampS16(s_objects[i].pos.x + dx);
		s_objects[i].pos.y = Editor_ClampS16(s_objects[i].pos.y + dy);
		s_objects[i].pos.z = Editor_ClampS16(s_objects[i].pos.z + dz);
	}
	Editor_RebuildSpawns();
	Editor_SaveSidecar();
}

static int Editor_FindNearest(void)
{
	int i, nearest = -1;
	s64 nearestDistance = 0;
	Vec3 target = s_hasSurfaceHit ? (Vec3){s_surfaceHit.x, s_surfaceHit.y, s_surfaceHit.z} : s_cameraPos;
	for (i = 0; i < s_objectCount; i++)
	{
		s64 dx = s_objects[i].pos.x - target.x;
		s64 dy = s_objects[i].pos.y - target.y;
		s64 dz = s_objects[i].pos.z - target.z;
		s64 distance = dx * dx + dy * dy + dz * dz;
		if (nearest < 0 || distance < nearestDistance)
		{
			nearest = i;
			nearestDistance = distance;
		}
	}
	return nearest;
}

static void Editor_SelectNearest(int additive)
{
	int nearest = Editor_FindNearest();
	if (!additive)
		memset(s_selectedSet, 0, sizeof(s_selectedSet));
	s_selected = nearest;
	if (nearest >= 0)
	{
		if (additive)
			s_selectedSet[nearest] ^= 1;
		else
			s_selectedSet[nearest] = 1;
		snprintf(s_status, sizeof(s_status), "Selected obj-%04d", s_objects[nearest].id);
		Editor_Log("SELECT id=obj-%04d index=%d", s_objects[nearest].id, nearest);
		if (!s_selectedSet[nearest])
		{
			for (s_selected = 0; s_selected < s_objectCount && !s_selectedSet[s_selected]; s_selected++) {}
			if (s_selected == s_objectCount)
				s_selected = -1;
		}
	}
	else
	{
		snprintf(s_status, sizeof(s_status), "Nothing to select");
		Editor_Log("SELECT none objects=%d", s_objectCount);
	}
	Editor_RebuildSpawns();
}

static void Editor_UpdatePreview(void)
{
	struct EditorSpawn *preview = &s_spawns[EDITOR_MAX_OBJECTS];
	// A HUD-less dump is a clean picture: no placement preview at the aim point.
	if (!s_capture || !s_hasSurfaceHit || (s_dumpNoHud && s_dumpDir[0] != 0))
	{
		if (preview->instance != NULL)
			INSTANCE_Death(preview->instance);
		memset(preview, 0, sizeof(*preview));
		return;
	}
	preview->used = 1;
	preview->modelID = s_kindModels[s_palette];
	preview->pos.x = s_surfaceHit.x;
	preview->pos.y = s_surfaceHit.y;
	preview->pos.z = s_surfaceHit.z;
	preview->rot.y = s_cameraRot.y;
	preview->colour = 0x0080ffff;
	strcpy(preview->name, "edit-preview");
}

static void Editor_UpdateDiagnosticSpawns(void)
{
	int i;
	int first = EDITOR_MAX_OBJECTS + 1;
	for (i = 0; i < EDITOR_DIAGNOSTIC_SPAWNS; i++)
		s_spawns[first + i].used = 0;
	if (!s_capture || !s_hasSurfaceHit || s_overlayMode == 0)
		goto remove_unused;
	for (i = 0; i < 4; i++)
	{
		struct EditorSpawn *spawn = &s_spawns[first + i];
		spawn->used = 1;
		spawn->modelID = PU_WUMPA_FRUIT;
		spawn->pos.x = s_surfaceHit.x + (s_surfaceNormal.x * (i + 1) >> 5);
		spawn->pos.y = s_surfaceHit.y + (s_surfaceNormal.y * (i + 1) >> 5);
		spawn->pos.z = s_surfaceHit.z + (s_surfaceNormal.z * (i + 1) >> 5);
		spawn->colour = 0x0000ff00;
		snprintf(spawn->name, sizeof(spawn->name), "edit-norm%d", i);
	}
	if (s_surfaceLeaf != NULL)
	{
		for (i = 0; i < 8; i++)
		{
			struct EditorSpawn *spawn = &s_spawns[first + 4 + i];
			spawn->used = 1;
			spawn->modelID = PU_WUMPA_FRUIT;
			spawn->pos.x = (i & 1) ? s_surfaceLeaf->box.max.x : s_surfaceLeaf->box.min.x;
			spawn->pos.y = (i & 2) ? s_surfaceLeaf->box.max.y : s_surfaceLeaf->box.min.y;
			spawn->pos.z = (i & 4) ? s_surfaceLeaf->box.max.z : s_surfaceLeaf->box.min.z;
			spawn->colour = 0x00ff00ff;
			snprintf(spawn->name, sizeof(spawn->name), "edit-bsp%d", i);
		}
	}
remove_unused:
	for (i = 0; i < EDITOR_DIAGNOSTIC_SPAWNS; i++)
	{
		struct EditorSpawn *spawn = &s_spawns[first + i];
		if (!spawn->used && spawn->instance != NULL)
		{
			INSTANCE_Death(spawn->instance);
			memset(spawn, 0, sizeof(*spawn));
		}
	}
}

static void Editor_UpdateSpawns(struct GameTracker *gGT)
{
	int i;
	for (i = 0; i < EDITOR_SPAWN_COUNT; i++)
	{
		struct EditorSpawn *spawn = &s_spawns[i];
		if (!spawn->used)
			continue;
		if (spawn->instance == NULL && spawn->modelID >= 0 && spawn->modelID < (int)(sizeof(gGT->modelPtr) / sizeof(gGT->modelPtr[0])) &&
		    gGT->modelPtr[spawn->modelID] != NULL)
		{
			spawn->instance = INSTANCE_Birth3D(gGT->modelPtr[spawn->modelID], spawn->name, NULL);
			if (spawn->instance != NULL)
				spawn->instance->flags = EDITOR_SPAWN_FLAGS;
		}
		if (spawn->instance != NULL)
		{
			if (i < s_objectCount)
				spawn->colour = s_selectedSet[i] ? 0x0000ffff : (i == s_hovered ? 0x00ff8000 :
				                (s_objects[i].kind == EDITOR_OBJECT_AP_CANDIDATE ? 0x00ff80ff : 0));
			ConvertRotToMatrix(&spawn->instance->matrix, &spawn->rot);
			spawn->instance->matrix.t[0] = spawn->pos.x;
			spawn->instance->matrix.t[1] = spawn->pos.y;
			spawn->instance->matrix.t[2] = spawn->pos.z;
			spawn->instance->scale.x = i > EDITOR_MAX_OBJECTS ? 0x300 : 0x1000;
			spawn->instance->scale.y = i > EDITOR_MAX_OBJECTS ? 0x300 : 0x1000;
			spawn->instance->scale.z = i > EDITOR_MAX_OBJECTS ? 0x300 : 0x1000;
			spawn->instance->colorRGBA = spawn->colour;
		}
	}
}

void Editor_OnPoolReset(void)
{
	int i;
	for (i = 0; i < EDITOR_SPAWN_COUNT; i++)
		s_spawns[i].instance = NULL;
}

static void Editor_SetCapture(int capture, struct GameTracker *gGT)
{
	struct CameraDC *camera;
	struct PushBuffer *push;
	if (gGT == NULL || gGT->numPlyrCurrGame < 1)
		return;
	camera = &gGT->cameraDC[0];
	push = camera->pushBuffer;
	if (push == NULL)
		return;
	if (capture)
	{
		if (!s_cameraBackedUp)
		{
			s_cameraBackup = *camera;
			s_pushPosBackup = push->pos;
			s_pushRotBackup = push->rot;
			s_hudBackup = gGT->hudFlags;
			s_backupLevel = gGT->levelID;
			if (!s_cameraConfigured)
			{
				struct Driver *driver = camera->driverToFollow;
				s_cameraPos.x = push->pos.x;
				s_cameraPos.y = push->pos.y;
				s_cameraPos.z = push->pos.z;
				s_cameraRot.x = (s16)Editor_NormalizeAngle(push->rot.x - 0x800);
				s_cameraRot.y = push->rot.y & 0xfff;
				s_cameraRot.z = 0;
				// A skipped start-line fly-in can leave the retail push buffer at
				// the origin. Fall back to a safe view above the spawned driver.
				if (s_cameraPos.x == 0 && s_cameraPos.y == 0 && s_cameraPos.z == 0 && driver != NULL)
				{
					s_cameraPos.x = driver->posCurr.x >> 8;
					s_cameraPos.y = (driver->posCurr.y >> 8) - 0x200;
					s_cameraPos.z = driver->posCurr.z >> 8;
					s_cameraRot.x = 0x100;
					s_cameraRot.y = (driver->angle + 0x800) & 0xfff;
				}
			}
			s_cameraBackedUp = 1;
		}
		s_capture = 1;
		// An editor session starts directly in the authored scene. Do not retain
		// the retail start-line fly-in or its traffic-light countdown after the
		// detached camera takes ownership.
		gGT->gameMode1 &= ~START_OF_RACE;
		gGT->trafficLightsTimer = -960;
		sdata->trafficLightsTimer_prevFrame = -960;
		gGT->hudFlags = 0;
		SDL_SetWindowRelativeMouseMode(g_window, true);
		snprintf(s_status, sizeof(s_status), "Editor camera active");
	}
	else
	{
		if (s_cameraBackedUp && s_backupLevel == gGT->levelID)
		{
			*camera = s_cameraBackup;
			push = camera->pushBuffer;
			if (push != NULL)
			{
				push->pos = s_pushPosBackup;
				push->rot = s_pushRotBackup;
			}
			gGT->hudFlags = s_hudBackup;
		}
		s_cameraBackedUp = 0;
		s_capture = 0;
		SDL_SetWindowRelativeMouseMode(g_window, false);
		snprintf(s_status, sizeof(s_status), "Test drive active. F1 returns to editor");
	}
}

static void Editor_RequestDirectLoad(struct GameTracker *gGT)
{
	if (s_loadRequested || gGT->levelID != MAIN_MENU_LEVEL || gGT->level1 == NULL || sdata->Loading.stage != LOAD_IDLE)
		return;
	gGT->numPlyrCurrGame = 1;
	gGT->numBotsNextGame = 0;
	gGT->currLEV = s_hostSlot;
	// The direct editor load does not pass through a retail menu selection.
	// Retire the live menu and attract-mode state now so its 30-second idle
	// transition cannot request a demo track behind the authored scene.
	gGT->gameMode1 &= ~(MAIN_MENU | ADVENTURE_MODE | ADVENTURE_ARENA | BATTLE_MODE | TIME_TRIAL | RELIC_RACE | CRYSTAL_CHALLENGE);
	gGT->gameMode1 |= ARCADE_MODE;
	gGT->boolDemoMode = 0;
	gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
	data.characterIDs[0] = CRASH_BANDICOOT;
	sdata->Loading.OnBegin.AddBitsConfig0 |= ARCADE_MODE;
	sdata->Loading.OnBegin.RemBitsConfig0 |= MAIN_MENU | ADVENTURE_MODE | ADVENTURE_ARENA | BATTLE_MODE | TIME_TRIAL | RELIC_RACE | CRYSTAL_CHALLENGE;
	MainRaceTrack_RequestLoad((s16)s_hostSlot);
	s_loadRequested = 1;
	snprintf(s_status, sizeof(s_status), "Loading editor host slot %d", s_hostSlot);
}

void Editor_Frame(struct GameTracker *gGT)
{
	const bool *keys;
	SDL_MouseButtonFlags mouse;
	float mouseX = 0.0f;
	float mouseY = 0.0f;
	int moveSpeed;
	int controlHeld;
	Vec3 forward;
	int rightX;
	int rightZ;
	if (!s_configured || gGT == NULL)
		return;
	if (s_active && (gGT->levelID != s_hostSlot || gGT->level1 == NULL))
	{
		s_active = 0;
		s_capture = 0;
		s_cameraBackedUp = 0;
		s_sourceGraphMeasured = 0;
		SDL_SetWindowRelativeMouseMode(g_window, false);
		Editor_Log("CLOSE level_changed=%d", gGT->levelID);
	}
	Editor_RequestDirectLoad(gGT);
	if (gGT->levelID != s_hostSlot || gGT->level1 == NULL || gGT->numPlyrCurrGame < 1 || gGT->cameraDC[0].pushBuffer == NULL)
		return;
	// Keep the retail attract timer inert even if a stale menu object survives
	// the direct-load boundary for a few frames.
	gGT->boolDemoMode = 0;
	gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
	if (!s_active)
	{
		s_active = 1;
		Editor_SetCapture(1, gGT);
		Editor_RebuildSpawns();
		Editor_SaveSidecar();
	}
	Editor_MeasureSourceGraph(gGT);
	keys = SDL_GetKeyboardState(NULL);
	if (Editor_KeyTapped(keys, SDL_SCANCODE_F1))
		Editor_SetCapture(!s_capture, gGT);
	if (!s_capture)
	{
		Editor_UpdatePreview();
		Editor_UpdateSpawns(gGT);
		return;
	}
	if (Editor_UpdateNumericEdit(keys))
	{
		Editor_UpdatePreview();
		Editor_UpdateDiagnosticSpawns();
		Editor_UpdateSpawns(gGT);
		return;
	}

	mouse = SDL_GetRelativeMouseState(&mouseX, &mouseY);
	if ((mouse & SDL_BUTTON_MMASK) && !(s_prevMouse & SDL_BUTTON_MMASK) && Editor_SelectedCount() > 0)
	{
		Editor_BeforeMutation();
		s_mouseTransformActive = 1;
	}
	if (s_mouseTransformActive && (mouse & SDL_BUTTON_MMASK))
		Editor_ApplyMouseTransform((int)mouseX, (int)mouseY);
	else
	{
		s_cameraRot.y = (s16)((s_cameraRot.y - (int)(mouseX * 4.0f)) & 0xfff);
		s_cameraRot.x = (s16)(s_cameraRot.x + (int)(mouseY * 4.0f));
	}
	if (s_mouseTransformActive && !(mouse & SDL_BUTTON_MMASK))
	{
		s_mouseTransformActive = 0;
		Editor_SaveSidecar();
	}
	if (s_cameraRot.x > 0x3c0)
		s_cameraRot.x = 0x3c0;
	if (s_cameraRot.x < -0x3c0)
		s_cameraRot.x = -0x3c0;
	controlHeld = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
	moveSpeed = s_cameraSpeed;
	if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
		moveSpeed *= 4;
	if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])
		moveSpeed = moveSpeed > 4 ? moveSpeed / 4 : 1;
	Editor_Forward(&forward);
	rightX = MATH_Cos((u32)s_cameraRot.y);
	rightZ = -MATH_Sin((u32)s_cameraRot.y);
	if (!controlHeld && keys[SDL_SCANCODE_W])
	{
		s_cameraPos.x += forward.x * moveSpeed >> 12;
		s_cameraPos.y += forward.y * moveSpeed >> 12;
		s_cameraPos.z += forward.z * moveSpeed >> 12;
	}
	if (!controlHeld && keys[SDL_SCANCODE_S])
	{
		s_cameraPos.x -= forward.x * moveSpeed >> 12;
		s_cameraPos.y -= forward.y * moveSpeed >> 12;
		s_cameraPos.z -= forward.z * moveSpeed >> 12;
	}
	if (!controlHeld && keys[SDL_SCANCODE_D])
	{
		s_cameraPos.x += rightX * moveSpeed >> 12;
		s_cameraPos.z += rightZ * moveSpeed >> 12;
	}
	if (!controlHeld && keys[SDL_SCANCODE_A])
	{
		s_cameraPos.x -= rightX * moveSpeed >> 12;
		s_cameraPos.z -= rightZ * moveSpeed >> 12;
	}
	if (!controlHeld && keys[SDL_SCANCODE_E])
		s_cameraPos.y -= moveSpeed;
	if (!controlHeld && keys[SDL_SCANCODE_Q])
		s_cameraPos.y += moveSpeed;
	s_cameraPos.x = Editor_ClampS16(s_cameraPos.x);
	s_cameraPos.y = Editor_ClampS16(s_cameraPos.y);
	s_cameraPos.z = Editor_ClampS16(s_cameraPos.z);
	Editor_SurfacePick(gGT);
	Editor_UpdateSurfaceDiagnostics(gGT);
	s_hovered = Editor_FindNearest();
	{
		struct CameraDC *camera = &gGT->cameraDC[0];
		struct PushBuffer *push = camera->pushBuffer;
		push->pos.x = (s16)s_cameraPos.x;
		push->pos.y = (s16)s_cameraPos.y;
		push->pos.z = (s16)s_cameraPos.z;
		push->rot.x = (s16)((s_cameraRot.x + 0x800) & 0xfff);
		push->rot.y = s_cameraRot.y & 0xfff;
		push->rot.z = 0;
		camera->cameraMode = CAMERA_MODE_FREECAM;
		camera->cameraModePrev = CAMERA_MODE_FREECAM;
		Editor_UpdatePVS(gGT, camera);
	}

	if (Editor_KeyTapped(keys, SDL_SCANCODE_1)) s_palette = EDITOR_OBJECT_ITEM_CRATE;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_2)) s_palette = EDITOR_OBJECT_WUMPA_CRATE;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_3)) s_palette = EDITOR_OBJECT_WUMPA_FRUIT;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_4)) s_palette = EDITOR_OBJECT_AP_CANDIDATE;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_TAB) && s_objectCount > 0)
	{
		s_selected = (s_selected + 1) % s_objectCount;
		memset(s_selectedSet, 0, sizeof(s_selectedSet));
		s_selectedSet[s_selected] = 1;
		Editor_RebuildSpawns();
	}
	if (Editor_KeyTapped(keys, SDL_SCANCODE_DELETE)) Editor_DeleteSelected();
	if ((keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) && Editor_KeyTapped(keys, SDL_SCANCODE_Z)) Editor_Undo();
	if ((keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) && Editor_KeyTapped(keys, SDL_SCANCODE_Y)) Editor_Redo();
	if ((keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) && Editor_KeyTapped(keys, SDL_SCANCODE_S)) Editor_SaveSidecar();
	if ((keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) && Editor_KeyTapped(keys, SDL_SCANCODE_D)) Editor_DuplicateSelected();
	if (Editor_KeyTapped(keys, SDL_SCANCODE_G)) Editor_SnapSelectedToSurface();
	if (Editor_KeyTapped(keys, SDL_SCANCODE_H))
	{
		int rollback = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
		if (!s_hotReloadEnabled)
		{
			snprintf(s_status, sizeof(s_status), "Reload unavailable: launch with --hot-reload-output");
		}
		else if (!Editor_SaveSidecar())
		{
			snprintf(s_status, sizeof(s_status), "Reload refused: project save failed");
		}
		else
		{
			Editor_Log("%s_REQUEST generation=%d project=%s", rollback ? "ROLLBACK" : "RELOAD", s_generation, s_sidecarPath);
			exit(rollback ? EDITOR_ROLLBACK_EXIT : EDITOR_RELOAD_EXIT);
		}
	}
	if (Editor_KeyTapped(keys, SDL_SCANCODE_I)) s_inspectorPage = (s_inspectorPage + 1) % 3;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_O))
	{
		s_overlayMode = (s_overlayMode + 1) % 3;
		g_dbg_wireframeMode = s_overlayMode == 2;
	}
	if (Editor_KeyTapped(keys, SDL_SCANCODE_M)) s_transformMode = !s_transformMode;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_X)) s_transformAxis = 0;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_Y)) s_transformAxis = 1;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_Z) && !controlHeld) s_transformAxis = 2;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_V)) s_snapStep = s_snapStep == 1 ? 16 : (s_snapStep == 16 ? 64 : 1);
	if (Editor_KeyTapped(keys, SDL_SCANCODE_N)) Editor_StartNumericEdit();
	if (Editor_KeyTapped(keys, SDL_SCANCODE_LEFTBRACKET) && s_cameraSpeed > 1) s_cameraSpeed /= 2;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_RIGHTBRACKET) && s_cameraSpeed < 1024) s_cameraSpeed *= 2;
	if (Editor_KeyTapped(keys, SDL_SCANCODE_LEFT)) Editor_MoveSelected(-16, 0, 0, 0);
	if (Editor_KeyTapped(keys, SDL_SCANCODE_RIGHT)) Editor_MoveSelected(16, 0, 0, 0);
	if (Editor_KeyTapped(keys, SDL_SCANCODE_UP)) Editor_MoveSelected(0, 0, 16, 0);
	if (Editor_KeyTapped(keys, SDL_SCANCODE_DOWN)) Editor_MoveSelected(0, 0, -16, 0);
	if (Editor_KeyTapped(keys, SDL_SCANCODE_PAGEUP)) Editor_MoveSelected(0, -16, 0, 0);
	if (Editor_KeyTapped(keys, SDL_SCANCODE_PAGEDOWN)) Editor_MoveSelected(0, 16, 0, 0);
	if (Editor_KeyTapped(keys, SDL_SCANCODE_T)) Editor_MoveSelected(0, 0, 0, 0x100);
	if (Editor_KeyTapped(keys, SDL_SCANCODE_L) || ((mouse & SDL_BUTTON_LMASK) && !(s_prevMouse & SDL_BUTTON_LMASK))) Editor_PlaceObject();
	if (Editor_KeyTapped(keys, SDL_SCANCODE_R) || ((mouse & SDL_BUTTON_RMASK) && !(s_prevMouse & SDL_BUTTON_RMASK)))
		Editor_SelectNearest(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
	s_prevMouse = mouse;
	Editor_UpdatePreview();
	Editor_UpdateDiagnosticSpawns();
	Editor_UpdateSpawns(gGT);
}

static int Editor_DrawHUDLine(struct GameTracker *gGT, char *text, s16 x, s16 y, s16 font, s16 colour)
{
	struct PrimMem *primMem = &gGT->backBuffer->primMem;
	size_t lineBytes = strlen(text) * sizeof(POLY_GT4);
	if ((uintptr_t)primMem->cursor > (uintptr_t)primMem->guardEnd ||
	    lineBytes > (size_t)((uintptr_t)primMem->guardEnd - (uintptr_t)primMem->cursor))
	{
		if (!s_hudCapacityWarned)
		{
			s_hudCapacityWarned = 1;
			Editor_Log("WARN hud_primitive_capacity cursor=%p guard=%p", primMem->cursor, primMem->guardEnd);
		}
		return 0;
	}
	DecalFont_DrawLine(text, x, y, font, colour);
	return 1;
}

static int Editor_DrawHUDNext(struct GameTracker *gGT, char *text, int *y, s16 colour)
{
	if (!Editor_DrawHUDLine(gGT, text, 8, (s16)*y, FONT_SMALL, colour))
		return 0;
	*y += 10;
	return 1;
}

void Editor_DrawHUD(struct GameTracker *gGT)
{
	char line[192];
	int y = 8;
	int candidateCount = 0;
	if (!s_active || gGT == NULL)
		return;
	// --editor-dump-nohud: the HUD is drawn into the same frame that is about to
	// be captured, so suppressing it has to happen here rather than at capture.
	if (s_dumpNoHud && s_dumpDir[0] != 0)
		return;
	snprintf(line, sizeof(line), "CTR EDITOR  %s  page:%d/3  I pages  O overlay:%s", s_capture ? "EDIT" : "TEST DRIVE", s_inspectorPage + 1,
	        s_overlayMode == 0 ? "off" : (s_overlayMode == 1 ? "diagnostics" : "wireframe"));
	if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
	for (int i = 0; i < s_objectCount; i++)
		if (s_objects[i].kind == EDITOR_OBJECT_AP_CANDIDATE) candidateCount++;
	snprintf(line, sizeof(line), "%.28s  LevelID:%d  candidates:%d", s_contentTitle, s_hostSlot, candidateCount);
	if (!Editor_DrawHUDNext(gGT, line, &y, ORANGE)) return;
	if (s_inspectorPage == 0)
	{
		snprintf(line, sizeof(line), "1 item  2 wumpa crate  3 fruit  4 AP   palette:%s", s_kindNames[s_palette]);
		if (!Editor_DrawHUDNext(gGT, line, &y, ORANGE)) return;
		snprintf(line, sizeof(line), "WASD/QE camera  L/LMB place  R/RMB select  Shift RMB multi-select");
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		snprintf(line, sizeof(line), "MMB drag %s axis:%c snap:%d  M mode  XYZ axis  V snap  N numeric",
		        s_transformMode == EDITOR_TRANSFORM_TRANSLATE ? "translate" : "rotate", "XYZ"[s_transformAxis], s_snapStep);
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		snprintf(line, sizeof(line), "Arrows/PgUp/PgDn move  T rotate  G surface snap  Ctrl D/Z/Y/S  Del  F1 test");
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		snprintf(line, sizeof(line), "objects:%d selected:%d hover:%d camera [%d %d %d] speed:%d", s_objectCount, Editor_SelectedCount(),
		        s_hovered + 1, s_cameraPos.x, s_cameraPos.y, s_cameraPos.z, s_cameraSpeed);
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		if (s_selected >= 0 && s_selected < s_objectCount)
		{
			struct EditorObject *selected = &s_objects[s_selected];
			snprintf(line, sizeof(line), "obj-%04d %s model:%d pos[%d %d %d] rot[%d %d %d]",
			        selected->id, s_kindNames[selected->kind], s_kindModels[selected->kind], selected->pos.x, selected->pos.y, selected->pos.z,
			        selected->rot.x, selected->rot.y, selected->rot.z);
			if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		}
		if (s_numericActive)
		{
			snprintf(line, sizeof(line), "NUMERIC %s %c = %s   Enter apply  Esc cancel",
			        s_transformMode == EDITOR_TRANSFORM_TRANSLATE ? "position" : "rotation", "XYZ"[s_transformAxis], s_numericInput);
			if (!Editor_DrawHUDNext(gGT, line, &y, ORANGE)) return;
		}
	}
	else if (s_inspectorPage == 1)
	{
		int kinds[EDITOR_OBJECT_KIND_COUNT] = {0};
		int i;
		for (i = 0; i < s_objectCount; i++) kinds[s_objects[i].kind]++;
		snprintf(line, sizeof(line), "PROJECT %.48s  creator:%.32s  version:%.16s  host:%d", s_contentTitle, s_contentCreator[0] ? s_contentCreator : "(unset)",
		        s_contentVersion, s_hostSlot);
		if (!Editor_DrawHUDNext(gGT, line, &y, ORANGE)) return;
		snprintf(line, sizeof(line), "source LEV %.12s  active %.12s  VRM %.12s  validation:%s", s_sourceLevHash, s_levHash, s_vrmHash,
		        s_launchValidated ? "PASS" : "UNVERIFIED");
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		snprintf(line, sizeof(line), "source instances:%u models:%u refs global:%d PVS:%d/%d BSP:%d/%d", gGT->level1->numInstances, gGT->level1->numModels,
		        s_sourceGlobalReferences, s_sourcePVSReferences, s_sourcePVSLists, s_sourceBSPReferences, s_sourceBSPLists);
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		snprintf(line, sizeof(line), "authored item:%d crate:%d fruit:%d AP:%d total:%d", kinds[0], kinds[1], kinds[2], kinds[3], s_objectCount);
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		snprintf(line, sizeof(line), "generation:%d clean:%d dirty:%s export:%.80s %.12s", s_generation, s_cleanGeneration,
		        s_generation == s_cleanGeneration ? "no" : "yes", s_lastCleanExportPath[0] ? s_lastCleanExportPath : "none", s_lastCleanExportHash);
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		snprintf(line, sizeof(line), "project:%.100s  minimum editor:%.24s", s_sidecarPath, s_minimumEditorVersion);
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
	}
	else
	{
		int quadIndex = -1;
		if (s_surfaceQuad != NULL && gGT->level1->ptr_mesh_info != NULL)
			quadIndex = (int)(s_surfaceQuad - gGT->level1->ptr_mesh_info->ptrQuadBlockArray);
		snprintf(line, sizeof(line), "GEOMETRY pick:%s pos[%d %d %d] normal[%d %d %d]", s_hasSurfaceHit ? "hit" : "miss",
		        s_surfaceHit.x, s_surfaceHit.y, s_surfaceHit.z, s_surfaceNormal.x, s_surfaceNormal.y, s_surfaceNormal.z);
		if (!Editor_DrawHUDNext(gGT, line, &y, ORANGE)) return;
		if (s_surfaceQuad != NULL)
			snprintf(line, sizeof(line), "quad:%d block:%d terrain:%u flags:%04x checkpoint:%u", quadIndex, s_surfaceQuad->blockID,
			        s_surfaceQuad->terrain_type, s_surfaceQuad->quadFlags, s_surfaceQuad->checkpointIndex);
		else
			snprintf(line, sizeof(line), "quad:none");
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		snprintf(line, sizeof(line), "PVS instances:%d  BSP leaf:%d quads:%d hitboxes:%d", s_surfacePVSInstances, s_surfaceLeafIndex,
		        s_surfaceLeaf != NULL ? s_surfaceLeaf->data.leaf.numQuads : 0, s_surfaceHitboxes);
		if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		if (s_surfaceLeaf != NULL)
		{
			snprintf(line, sizeof(line), "leaf bbox min[%d %d %d] max[%d %d %d]", s_surfaceLeaf->box.min.x, s_surfaceLeaf->box.min.y,
			        s_surfaceLeaf->box.min.z, s_surfaceLeaf->box.max.x, s_surfaceLeaf->box.max.y, s_surfaceLeaf->box.max.z);
			if (!Editor_DrawHUDNext(gGT, line, &y, WHITE)) return;
		}
		snprintf(line, sizeof(line), "overlay green:surface normal  magenta:BSP leaf bounds  wireframe:%s", g_dbg_wireframeMode ? "on" : "off");
		if (!Editor_DrawHUDNext(gGT, line, &y, LIGHT_GREEN)) return;
	}
	Editor_DrawHUDLine(gGT, s_status, 8, y, FONT_SMALL, LIGHT_GREEN);
}

// Called from Platform_EndScene right after the present and before the window
// swap, so the back buffer still holds exactly the image that is about to be
// shown. Counting starts when the editor becomes active on its host track, not
// at process start, so --editor-dump-start is measured from a loaded scene.
void Editor_AfterPresent(void)
{
	char path[1200];
	int elapsed;

	if (!s_configured || s_dumpDir[0] == 0 || !s_active || s_dumpQuitRequested)
		return;

	s_dumpActiveFrames++;
	elapsed = s_dumpActiveFrames - s_dumpStart;
	if (elapsed < 0)
		return;
	if ((elapsed % s_dumpEvery) != 0)
		return;

	if (snprintf(path, sizeof(path), "%s/frame-%05d.bmp", s_dumpDir, s_dumpWritten) >= (int)sizeof(path))
	{
		Editor_Log("DUMP_REFUSAL path_too_long dir=%s index=%d", s_dumpDir, s_dumpWritten);
		return;
	}

	if (!Editor_CaptureBackBufferToBMP(path))
	{
		Editor_Log("DUMP_REFUSAL capture_failed path=%s index=%d", path, s_dumpWritten);
		return;
	}

	Editor_Log("DUMP index=%d file=%s active_frame=%d camera_pos=%d,%d,%d camera_rot=%d,%d,%d speed=%d", s_dumpWritten, path,
	           s_dumpActiveFrames, s_cameraPos.x, s_cameraPos.y, s_cameraPos.z, s_cameraRot.x, s_cameraRot.y, s_cameraRot.z,
	           s_cameraSpeed);
	printf("[CTR Editor] dump %d -> %s pos=%d,%d,%d rot=%d,%d,%d\n", s_dumpWritten, path, s_cameraPos.x, s_cameraPos.y,
	       s_cameraPos.z, s_cameraRot.x, s_cameraRot.y, s_cameraRot.z);
	fflush(stdout);
	s_dumpWritten++;

	if (s_dumpLimit > 0 && s_dumpWritten >= s_dumpLimit)
	{
		SDL_Event quit;
		s_dumpQuitRequested = 1;
		memset(&quit, 0, sizeof(quit));
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
		Editor_Log("DUMP_COMPLETE count=%d dir=%s", s_dumpWritten, s_dumpDir);
		printf("[CTR Editor] dump complete: %d frames in %s\n", s_dumpWritten, s_dumpDir);
		fflush(stdout);
	}
}

#endif
