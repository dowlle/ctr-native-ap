#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80057c44-0x80057c68.
struct Scrub *VehAfterColl_GetSurface(u32 scrubId)
{
	struct Scrub *sc = &data.MetaDataScrub[0];

	if (scrubId < 7)
		return &sc[scrubId];

	return sc;
}
