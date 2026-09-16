#ifndef CTR_NATIVE_EDITOR_H
#define CTR_NATIVE_EDITOR_H

#ifdef CTR_EDITOR

#include <common.h>

void Editor_ConfigureFromArgs(int argc, char **argv);
int Editor_RunSelfTests(void);
int Editor_IsConfigured(void);
int Editor_IsInputCaptured(void);
int Editor_GetLoadOverride(int subfileIndex, const char **path, u32 *size);
int Editor_ReadLoadOverride(const char *path, void *destination, u32 bufferBytes, u32 fileBytes);
void Editor_OnPoolReset(void);
void Editor_Frame(struct GameTracker *gGT);
void Editor_DrawHUD(struct GameTracker *gGT);

#endif
#endif
