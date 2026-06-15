#include <common.h>

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 230 0x800ac178-0x800ac1f0.
void MM_Title_SetTrophyDPP(void)
{
	u32 idpp2_b8;
	struct InstDrawPerPlayer *idpp1;
	struct InstDrawPerPlayer *idpp2;
	struct Title *title = D230.titleObj;
	int e4;
	int e8;
	int dc;

	if (title == NULL)
		return;

	idpp1 = INST_GETIDPP(title->i[1]); // "title"
	idpp2 = INST_GETIDPP(title->i[2]); // another "title"

	idpp2_b8 = idpp2->instFlags;
	if ((idpp2_b8 & 0x100) != 0)
		return;

	idpp2_b8 |= 0xffffffbf;
	idpp1->instFlags &= idpp2_b8;

	e4 = idpp2->otRangeNormal;
	e8 = idpp2->otRangeSecondary;
	dc = *(int *)&idpp2->depthOffset[0];

	idpp1->otRangeNormal = e4;
	idpp1->otRangeSecondary = e8;
	*(int *)&idpp1->depthOffset[0] = dc;
}
