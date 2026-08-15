// AP-owned fallback model for tracks without a resident weapon crate.

#ifdef CTR_AP

#include <common.h>

#include "ap_box_model.h"
#include "ap_box_model_data.h"

struct ApBoxModelFrame
{
	struct ModelFrame frame;
	u8 verts[AP_BOX_MODEL_NUM_VERTS * 3];
};

CTR_STATIC_ASSERT(sizeof(struct ModelFrame) == 0x1c);
CTR_STATIC_ASSERT(offsetof(struct ApBoxModelFrame, verts) == 0x1c);

static struct ApBoxModelFrame s_apBoxFrame;
static struct ModelHeader s_apBoxHeader;
static struct Model s_apBoxModel;
static int s_apBoxBuilt;

#define AP_BOX_FALLBACK_SCALE 0x1000

static void AP_BoxModel_Build(struct GameTracker *gGT)
{
	struct Model *scaleSource = gGT->modelPtr[STATIC_TIME_CRATE_01];
	s16 scale = AP_BOX_FALLBACK_SCALE;
	int i;

	if (scaleSource != 0 && scaleSource->headers != 0 && scaleSource->numHeaders > 0)
		scale = scaleSource->headers[0].scale.x;

	s_apBoxFrame.frame.pos.x = -128;
	s_apBoxFrame.frame.pos.y = -128;
	s_apBoxFrame.frame.pos.z = -128;
	s_apBoxFrame.frame.vertexOffset = 0x1c;
	for (i = 0; i < AP_BOX_MODEL_NUM_VERTS * 3; i++)
		s_apBoxFrame.verts[i] = s_apBoxModelVerts[i];

	s_apBoxHeader.name[0] = 'a';
	s_apBoxHeader.name[1] = 'p';
	s_apBoxHeader.name[2] = 'b';
	s_apBoxHeader.name[3] = 'o';
	s_apBoxHeader.name[4] = 'x';
	s_apBoxHeader.name[5] = '\0';
	s_apBoxHeader.maxDistanceLOD = 0x7fff;
	s_apBoxHeader.flags = 0;
	s_apBoxHeader.scale.x = scale;
	s_apBoxHeader.scale.y = scale;
	s_apBoxHeader.scale.z = scale;
	s_apBoxHeader.ptrCommandList = (u32)(uintptr_t)s_apBoxModelCommands;
	s_apBoxHeader.ptrFrameData = &s_apBoxFrame.frame;
	s_apBoxHeader.ptrTexLayout = 0;
	s_apBoxHeader.ptrColors = (u32 *)(uintptr_t)s_apBoxModelColors;
	s_apBoxHeader.unk3 = 0;
	s_apBoxHeader.numAnimations = 0;
	s_apBoxHeader.ptrAnimations = 0;
	s_apBoxHeader.animtex = 0;

	s_apBoxModel.name[0] = 'a';
	s_apBoxModel.name[1] = 'p';
	s_apBoxModel.name[2] = 'b';
	s_apBoxModel.name[3] = 'o';
	s_apBoxModel.name[4] = 'x';
	s_apBoxModel.name[5] = '\0';
	s_apBoxModel.id = PU_RANDOM_CRATE;
	s_apBoxModel.numHeaders = 1;
	s_apBoxModel.headers = &s_apBoxHeader;
	s_apBoxBuilt = 1;
}

int AP_BoxModel_Ensure(struct GameTracker *gGT)
{
	if (gGT == 0)
		return -1;
	if (gGT->modelPtr[PU_RANDOM_CRATE] != 0)
		return PU_RANDOM_CRATE;
	if (!s_apBoxBuilt)
		AP_BoxModel_Build(gGT);

	// PU_RANDOM_CRATE is a normal per-level slot and LibraryOfModels_Clear
	// clears it on every transition. Reassert only while it is absent, so a
	// retail model always wins on levels that carry one.
	gGT->modelPtr[PU_RANDOM_CRATE] = &s_apBoxModel;
	return PU_RANDOM_CRATE;
}

#endif
