#ifndef PLATFORM_NATIVE_CHECKPOINT_H
#define PLATFORM_NATIVE_CHECKPOINT_H

#include <macros.h>

int NativeCheckpoint_GetSize(void);
int NativeCheckpoint_Capture(void *dst, int dstSize);
int NativeCheckpoint_Restore(const void *src, int srcSize);
#ifdef CTR_CUSTOM_PACKAGES
// A restore refused for another custom track, on the race HUD for a few seconds.
void NativeCheckpoint_DrawCustomRefusal(void);
#endif
#if defined(CTR_INTERNAL)
void NativeCheckpoint_OnMempackArenaReset(void);
void NativeCheckpoint_RegisterPointerSlot(void *slot);
#endif

#endif
