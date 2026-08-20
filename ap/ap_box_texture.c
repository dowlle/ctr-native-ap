// Upload the AP box's own face art as the AP sideload texture.
//
// WHAT REPLACED WHAT
// ------------------
// This module replaces the retail-crate harvest that previously produced the
// same atlas. That harvest read a 1p LEV+VRM pair out of BIGFILE, ran the
// engine's pointer fixup on it, walked `crate_question`'s command lists to the
// texture layouts it genuinely used, converted its 4bpp+CLUT texels to RGBA on
// the CPU and packed them into a 128x64 atlas. It worked, but it made the AP
// box's appearance a function of the player's game data, and it could fail --
// on an unreadable BIGFILE, an unexpected level layout, or a stray pointer --
// leaving the plain fallback cube behind. The art is now shipped, so none of
// that can happen: there is nothing to read, walk, or fail.
//
// WHY THE ATLAS LOOKS THE WAY IT DOES
// -----------------------------------
// The atlas keeps the shape the harvest produced, so the layout below is the
// same twelve bytes the model consumed before, and only the pixels changed:
//
//   ( 0, 0) 16x16  the wood frame
//   (16, 0) 64x64  the near-LOD face -- the ONLY rect the box model samples
//   (80, 0) 32x32  the far-LOD face
//
// tpage and clut are copied from the retail layout this replaces, measured off
// NTSC-U BIGFILE entry 1 (1p level 0), model `crate_question`, near-LOD header.
// They are not decorative:
//
//   tpage bits 5-6 (the semi-transparency field) are 3 here. The engine's own
//   primitive writer branches on exactly that field: with it non-zero it emits
//   an OPAQUE code word, and with it zero it emits a semi-transparent one, so a
//   layout that lost those bits draws the box see-through. That is not
//   hypothetical -- overwriting the whole tpage word instead of OR-ing the
//   sideload bit into it drew the crate 50% transparent (observed live
//   2026-08-17). Bits 0-4 (the VRAM page) are meaningless once the primitive
//   samples the sideload texture, and bits 7-8 (colour depth) are overridden to
//   RGBA by the sideload path; they are kept only so the word stays the retail
//   word plus one flag.
//
//   clut is likewise inert for a sideloaded primitive -- the sideload path
//   forces TF_32_BIT_RGBA, so nothing resolves a palette -- and is carried for
//   the same reason.
//
// AP_TPAGE_SIDELOAD_BIT is what actually routes the primitive at the sideload
// texture. It is OR-ed in, never assigned over the word.

#ifdef CTR_AP

#include <common.h>
#include <stdio.h>

#include "ap_box_texture.h"
#include "ap_box_texture_data.h"
#include "ap_hooks.h" // AP_LogLine

#include <platform/native_gpu.h>      // AP_TPAGE_SIDELOAD_BIT, NativeGpu_SetSideloadTexture
#include <platform/native_renderer.h> // NativeRenderer_CreateRGBATexture

// Retail `crate_question` near-LOD values, carried verbatim. See the note above.
#define AP_BOX_TEXTURE_FACE_TPAGE 0x0069
#define AP_BOX_TEXTURE_FACE_CLUT  0x3F6A

// Unity-build names must be module-specific: several AP modules land in the same
// C translation unit, and a generic static tentative definition silently
// coalesces in C.
static int                  s_apBoxTextureState; // 0 = untried, 1 = ready, 2 = failed
static struct TextureLayout s_apBoxTextureFace;

int AP_BoxTexture_EnsureFace(struct TextureLayout *outFace)
{
	unsigned tex;
	char     msg[128];

	if (outFace == 0 || s_apBoxTextureState == 2)
		return 0;

	if (s_apBoxTextureState == 1)
	{
		*outFace = s_apBoxTextureFace;
		return 1;
	}

	tex = (unsigned)NativeRenderer_CreateRGBATexture(AP_BOX_TEXTURE_ATLAS_W, AP_BOX_TEXTURE_ATLAS_H,
	                                                (u8 *)s_apBoxTextureAtlas);
	if (tex == 0)
	{
		AP_LogLine("[AP BOX] box atlas upload failed; keeping the fallback cube\n");
		s_apBoxTextureState = 2;
		return 0;
	}

	NativeGpu_SetSideloadTexture(tex, AP_BOX_TEXTURE_ATLAS_W, AP_BOX_TEXTURE_ATLAS_H);

	// Corner order matches the retail layout this replaces: top-left,
	// bottom-left, top-right, and a fourth corner that repeats the third
	// because the source layout is a triangle. Preserved exactly so the face
	// cannot arrive mirrored or rotated relative to what shipped.
	s_apBoxTextureFace.u0 = (u8)(AP_BOX_TEXTURE_FACE_X);
	s_apBoxTextureFace.v0 = (u8)(AP_BOX_TEXTURE_FACE_Y);
	s_apBoxTextureFace.u1 = (u8)(AP_BOX_TEXTURE_FACE_X);
	s_apBoxTextureFace.v1 = (u8)(AP_BOX_TEXTURE_FACE_Y + AP_BOX_TEXTURE_FACE_H - 1);
	s_apBoxTextureFace.u2 = (u8)(AP_BOX_TEXTURE_FACE_X + AP_BOX_TEXTURE_FACE_W - 1);
	s_apBoxTextureFace.v2 = (u8)(AP_BOX_TEXTURE_FACE_Y);
	s_apBoxTextureFace.u3 = s_apBoxTextureFace.u2;
	s_apBoxTextureFace.v3 = s_apBoxTextureFace.v2;
	s_apBoxTextureFace.clut = (u16)AP_BOX_TEXTURE_FACE_CLUT;
	s_apBoxTextureFace.tpage = (u16)(AP_BOX_TEXTURE_FACE_TPAGE | AP_TPAGE_SIDELOAD_BIT);

	s_apBoxTextureState = 1;
	*outFace = s_apBoxTextureFace;

	snprintf(msg, sizeof msg, "[AP BOX] box face atlas uploaded: %dx%d, face rect %dx%d at (%d,%d)\n",
	         AP_BOX_TEXTURE_ATLAS_W, AP_BOX_TEXTURE_ATLAS_H, AP_BOX_TEXTURE_FACE_W, AP_BOX_TEXTURE_FACE_H,
	         AP_BOX_TEXTURE_FACE_X, AP_BOX_TEXTURE_FACE_Y);
	AP_LogLine(msg);

	return 1;
}

#endif // CTR_AP
