#ifndef PLATFORM_NATIVE_CHECKPOINT_H
#define PLATFORM_NATIVE_CHECKPOINT_H

#include <macros.h>

int NativeCheckpoint_GetSize(void);
int NativeCheckpoint_Capture(void *dst, int dstSize);
int NativeCheckpoint_Restore(const void *src, int srcSize);
#ifdef CTR_CUSTOM_PACKAGES
// Age the offline-package refusal every frame, with the common notice's priority.
void NativeCheckpoint_DrawCustomRefusal(u32 *ot, int visible);
#endif
#if defined(CTR_INTERNAL)
void NativeCheckpoint_OnMempackArenaReset(void);
void NativeCheckpoint_RegisterPointerSlot(void *slot);
#endif

#endif
