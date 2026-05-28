#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80034bbc-0x80034d54.
void MainFrame_ResetDB(struct GameTracker *gGT)
{
	u_long *puVar3;
	int iVar4;
	struct DB *db;
	int otSwapchainDB;

	// check if new adv hub should be loaded,
	// this was a random place for ND to put it
	LOAD_Hub_Main(sdata->ptrBigfile1);

	gGT->swapchainIndex = 1 - gGT->swapchainIndex;

	gGT->backBuffer = &gGT->db[gGT->swapchainIndex];
	gGT->frameTimer_MainFrame_ResetDB++;

	otSwapchainDB = (int)gGT->otSwapchainDB[gGT->swapchainIndex];

	db = gGT->backBuffer;
	*(u8 *)&db->unk_primMemRelated = 0;
	db->primMem.curr = db->primMem.start;
	db->primMem.unk1 = 0;
	db->otMem.curr = db->otMem.start;

	CTR_EmptyFunc_MainFrame_ResetDB();
	DecalGlobal_EmptyFunc_MainFrame_ResetDB();

	ClearOTagR((u32 *)otSwapchainDB, sdata->gGT->numPlyrCurrGame << 10 | 6);

	for (iVar4 = 0; iVar4 < sdata->gGT->numPlyrCurrGame; iVar4++)
	{
		gGT->pushBuffer[iVar4].ptrOT = (u_long *)((int)otSwapchainDB + (sdata->gGT->numPlyrCurrGame - iVar4 - 1) * 0x1000 + 0x18);
	}

	for (iVar4; iVar4 < 4; iVar4++)
	{
		// but why?
		gGT->pushBuffer[iVar4].ptrOT = (u_long *)((int)otSwapchainDB + 3 * 0x1000 + 0x18);
	}

	puVar3 = (u_long *)((int)otSwapchainDB + 4);
	gGT->pushBuffer_UI.ptrOT = puVar3;
	db->otMem.startPlusFour = puVar3;
	return;
}
