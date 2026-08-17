// AP-owned fallback model for tracks without a resident weapon crate.

#ifdef CTR_AP

#include <common.h>

#include "ap_box_model.h"
#include "ap_box_model_data.h"

#include <platform/native_gpu.h>      // AP_TPAGE_SIDELOAD_BIT, NativeGpu_SetSideloadTexture
#include <platform/native_renderer.h> // NativeRenderer_CreateRGBATexture

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

// ── sideloaded texture (scoping proof) ──────────────────────────────────────
//
// The cube opts into the AP host texture per PRIMITIVE by setting
// AP_TPAGE_SIDELOAD_BIT in this layout's tpage. RenderBucket copies the layout's
// tpage into the packet unmasked (`p->tpage = tex->tpage`), the GPU layer
// assigns it to activeDrawEnv.tpage verbatim, and AddSplit binds the AP texture
// for exactly those splits. Retail prims cannot set the bit -- their tpage words
// are fixed data on the disc -- so nothing else is affected.
//
// ptrTexLayout is `struct TextureLayout **`, so the header needs an array of
// pointers, not the layout itself. Index 1 in the command list means element 0
// here (RenderBucket_GetCommandTexture is 1-based).
#define AP_BOX_TEX_SIZE 16

static struct TextureLayout  s_apBoxTexLayout;
static struct TextureLayout *s_apBoxTexLayouts[1];
static u8                    s_apBoxTexPixels[AP_BOX_TEX_SIZE * AP_BOX_TEX_SIZE * 4];
static int                   s_apBoxTexReady;

// Build the host texture once and register it in the AP sideload slot. Returns
// non-zero when the cube may use its textured command list.
static int AP_BoxModel_EnsureTexture(void)
{
	unsigned tex;
	int      p;

	if (s_apBoxTexReady)
		return 1;

	// PROOF CONTENT: flat magenta. The real implementation replaces this with
	// the retail crate texture, converted from 4bpp + CLUT to RGBA on the CPU.
	for (p = 0; p < AP_BOX_TEX_SIZE * AP_BOX_TEX_SIZE; p++)
	{
		s_apBoxTexPixels[p * 4 + 0] = 0xFF;
		s_apBoxTexPixels[p * 4 + 1] = 0x00;
		s_apBoxTexPixels[p * 4 + 2] = 0xFF;
		s_apBoxTexPixels[p * 4 + 3] = 0xFF;
	}

	tex = (unsigned)NativeRenderer_CreateRGBATexture(AP_BOX_TEX_SIZE, AP_BOX_TEX_SIZE, s_apBoxTexPixels);
	if (tex == 0)
		return 0;

	NativeGpu_SetSideloadTexture(tex, AP_BOX_TEX_SIZE, AP_BOX_TEX_SIZE);

	s_apBoxTexLayout.tpage = AP_TPAGE_SIDELOAD_BIT;
	s_apBoxTexLayout.clut = 0; // unused for an RGBA host texture
	s_apBoxTexLayout.u0 = 0;
	s_apBoxTexLayout.v0 = 0;
	s_apBoxTexLayout.u1 = AP_BOX_TEX_SIZE - 1;
	s_apBoxTexLayout.v1 = 0;
	s_apBoxTexLayout.u2 = 0;
	s_apBoxTexLayout.v2 = AP_BOX_TEX_SIZE - 1;
	s_apBoxTexLayout.u3 = AP_BOX_TEX_SIZE - 1;
	s_apBoxTexLayout.v3 = AP_BOX_TEX_SIZE - 1;

	s_apBoxTexLayouts[0] = &s_apBoxTexLayout;
	s_apBoxTexReady = 1;
	return 1;
}

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
	// Textured when the sideload texture registered, flat-shaded otherwise. The
	// two command lists differ only in the texture index carried in each
	// command's low bits, so a failure here degrades to the previous look rather
	// than to a missing box.
	if (AP_BoxModel_EnsureTexture())
	{
		s_apBoxHeader.ptrCommandList = (u32)(uintptr_t)s_apBoxModelCommandsTex;
		s_apBoxHeader.ptrTexLayout = s_apBoxTexLayouts;
	}
	else
	{
		s_apBoxHeader.ptrCommandList = (u32)(uintptr_t)s_apBoxModelCommands;
		s_apBoxHeader.ptrTexLayout = 0;
	}
	s_apBoxHeader.ptrFrameData = &s_apBoxFrame.frame;
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
