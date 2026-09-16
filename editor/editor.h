#ifndef CTR_NATIVE_EDITOR_H
#define CTR_NATIVE_EDITOR_H

#ifdef CTR_EDITOR

#include <common.h>

// Named engine constants the editor relies on. The client engine spells these
// as literals, so they are defined here rather than in the engine headers to
// keep the CTR_EDITOR=OFF build byte-identical to main.
enum EditorEngineConstants
{
	// Subfiles per arcade-track LOD group in the BIGFILE layout.
	LOAD_TRACK_FILES_PER_LOD_GROUP = 8,
	// Attract-mode idle countdown the editor re-arms so a parked session never
	// falls into the title demo.
	TITLE_DEMO_IDLE_FRAMES = CTR_SECONDS_TO_FRAMES(30),
	// Camera.cameraMode value for the free-look camera.
	CAMERA_MODE_FREECAM = 3,
};

void Editor_ConfigureFromArgs(int argc, char **argv);
void Editor_ApplyDumpRuntimeOverrides(void);
int Editor_RunSelfTests(void);
int Editor_IsConfigured(void);
int Editor_IsInputCaptured(void);
int Editor_GetLoadOverride(int subfileIndex, const char **path, u32 *size);
int Editor_ReadLoadOverride(const char *path, void *destination, u32 bufferBytes, u32 fileBytes);
void Editor_OnPoolReset(void);
void Editor_Frame(struct GameTracker *gGT);
void Editor_DrawHUD(struct GameTracker *gGT);

// Headless frame dump. Editor_AfterPresent is called from Platform_EndScene
// immediately after the present and before the window swap, so it reads the
// final presented back buffer. Editor_CaptureBackBufferToBMP is implemented in
// platform/native_platform.c, where the GL loader symbols are in scope;
// it returns non-zero on success.
void Editor_AfterPresent(void);
int Editor_CaptureBackBufferToBMP(const char *path);

// Atomic sidecar replace. Also implemented in platform/native_platform.c: the
// Windows path needs windows.h, and pulling that in this early in the unity
// translation unit would expose every engine file after it to the Win32 macro
// namespace. Returns non-zero on success; on failure it writes a short reason
// into errorText.
int Editor_ReplaceFileAtomically(const char *temporary, const char *destination, char *errorText, size_t errorTextSize);

#endif
#endif
