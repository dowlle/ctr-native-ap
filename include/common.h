#ifndef COMMON_H
#define COMMON_H

#if defined(CTR_NATIVE)
#include <stdio.h>
#include <string.h>
#endif

#ifndef CTR_NATIVE
#include <psx/psn00b_prelude.h>
#endif

// Project base types and helpers.
#include <macros.h>

// PSX SDK-shaped headers used by game code.
#include <psx/libapi.h>
#include <psx/libcd.h>
#include <psx/libetc.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>
#include <psx/libpad.h>
#include <psx/libspu.h>
#include <psx/strings.h>
#include <psx/inline_c.h>

// Supplemental SDK definitions used by game-owned pad state.
#include <psn00bsdk/include/psxpad.h>
#include <psn00bsdk/include/sys/fcntl.h>

#ifndef RECT
#define RECT RECT16
#endif

// Project-owned helpers layered on top of the PSX-shaped SDK headers.
#include <ctr_math.h>
#include <ctr_gte.h>
#include <ctr_scratchpad.h>
#include <prim.h>

// Game layout and namespace headers.
#include <namespace_Bots.h>
#include <namespace_Camera.h>
#include <namespace_Cdsys.h>
#include <namespace_Coll.h>
#include <namespace_Decal.h>
#include <namespace_Display.h>
#include <namespace_Gamepad.h>
#include <namespace_Ghost.h>
#include <namespace_Howl.h>
#include <namespace_Instance.h>
#include <namespace_Level.h>
#include <namespace_List.h>
#include <namespace_Lng.h>
#include <namespace_JitPool.h>
#include <namespace_Load.h>
#include <namespace_Memcard.h>
#include <namespace_Mempack.h>
#include <namespace_Particle.h>
#include <namespace_Proc.h>
#include <namespace_PushBuffer.h>
#include <namespace_RectMenu.h>
#include <namespace_SelectProfile.h>
#include <namespace_Main.h>
#include <namespace_DrawLevel.h>
#include <namespace_UI.h>
#include <namespace_Vehicle.h>

// Overlay and global region layout headers.
#include <ovr_226.h>
#include <ovr_227.h>
#include <ovr_228.h>
#include <ovr_229.h>
#include <ovr_230.h>
#include <ovr_231.h>
#include <ovr_232.h>
#include <ovr_233.h>
#include <regionsEXE.h>

// Game declarations and GPU helpers that depend on the layout headers above.
#include <functions.h>
#include <gpu.h>

#if defined(CTR_NATIVE)
static inline void *CTR_PsyqMemmove(void *dest, const void *src, s32 count)
{
	// NOTE(aalhendi): Retail PSYQ memmove at NTSC-U 926 0x80077e38 returns
	// immediately for signed lengths <= 0. Host libc takes size_t, so native
	// must preserve the signed PSYQ contract for ASM-verified game code.
	if (count <= 0)
	{
		return dest;
	}

	return memmove(dest, src, (size_t)count);
}

#define memmove(dest, src, count) CTR_PsyqMemmove((dest), (src), (s32)(count))
#endif

#endif
