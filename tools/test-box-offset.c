// Out-of-engine assertions for the AP crate spawn-height correction (Lane C of
// the 2026-08-29 specification). Compiles the REAL arithmetic:
// ap/ap_box_offset_logic.h is freestanding by design, and the mesh it measures
// is the shipped ap/ap_box_model_data.h, so this harness links nothing from the
// game and can run on any host, with no disc, no display and no seed.
//
//   cc -Wall -Wextra -DCTR_AP -o /tmp/test-box-offset tools/test-box-offset.c && /tmp/test-box-offset
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// What it pins:
//   1. the shipped AP cube's own mesh, walked out of its command list exactly
//      the way the renderer walks it: 36 vertices from both the untextured and
//      the textured list, and the per-axis byte ranges those vertices span,
//   2. the model-unit -> world-unit conversion (headerScale / 0x1000) and the
//      derived-scale rule that makes the AP cube exactly retail-crate sized,
//   3. THE SELECTED OFFSET: the AP crate's origin sits 54 world units above its
//      own lowest face at the live-probed retail crate scale, so a spawn lifted
//      by 54 rests its bottom face on the authored anchor,
//   4. that a retail crate centred the same way lands on the SAME 54, which is
//      what "the same practical ground relationship as a retail weapon crate"
//      reduces to once both models are measured rather than assumed,
//   5. the centred-origin invariant (lift is half the rendered height) -- the
//      assertion the pre-correction behaviour of "spawn at the anchor, lift 0"
//      fails, which is what makes this a regression test and not a description,
//   6. fail-closed behaviour: an unmeasurable mesh, a non-positive scale and a
//      model whose origin is already at its base all produce lift 0, i.e. the
//      authored anchor untouched, never a negative lift that would bury a crate.
//
// The engine-side premise this correction rests on -- that a placement row is a
// GROUND point because Driver.posCurr is the kart's ground contact point -- is
// pinned separately, against the decompiled sources themselves, by
// tools/test-box-anchor-premise.py.

#include <stdio.h>

// The shipped model data is engine-typed. These two are the only engine types it
// uses, and they are the same widths ap/ builds with on both targets.
typedef unsigned int u32;
typedef unsigned char u8;

#include "../ap/ap_box_offset_logic.h"
#include "../ap/ap_box_model_data.h"

// The two constants ap_box_model.c derives the live scale from. Restated here
// ONLY as the values under test; see the mismatch assertion below, which is what
// would catch them drifting apart from the shipped mesh.
#define AP_BOX_MODEL_EXTENT   192
#define AP_BOX_FALLBACK_SCALE 0x910

// The live size probe, from a real session's log line (ap_box_model.c logs it
// once per source model): both retail crates measured at header scale 0x6d3 with
// a full 255-unit extent.
#define RETAIL_CRATE_SCALE  0x6d3
#define RETAIL_CRATE_EXTENT 255

// The AP cube's frame origin, written in ap_box_model.c: the mesh's byte cube is
// recentred on the instance origin by (-128, -128, -128), and pos.y is the
// component that offsets the VERTICAL vertex byte.
#define AP_BOX_FRAME_POS (-128)

static int checks;
static int failures;

static void expect(int condition, const char *name)
{
	checks++;
	if (!condition)
	{
		failures++;
		printf("FAIL: %s\n", name);
	}
}

static void expect_eq(int got, int want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		printf("FAIL: %s (got %d, want %d)\n", name, got, want);
	}
}

int main(void)
{
	AP_BoxMeshBounds b;
	int n, scale, lift, height, retailLift;

	// ── 1. the shipped mesh, walked the way the renderer walks it ───────────

	n = AP_BoxMesh_CountVerts(s_apBoxModelCommands);
	expect_eq(n, AP_BOX_MODEL_NUM_VERTS, "untextured command list consumes every vertex");
	expect_eq(AP_BoxMesh_CountVerts(s_apBoxModelCommandsTex), AP_BOX_MODEL_NUM_VERTS,
	          "textured command list consumes the same vertices");
	expect_eq(AP_BOX_MODEL_NUM_VERTS, AP_BOX_MODEL_NUM_TRIS * 3, "twelve triangles, three vertices each");

	expect(AP_BoxMesh_Bounds(s_apBoxModelVerts, n, &b), "the shipped vertex block measures");
	expect_eq(b.lo[0], 32, "horizontal byte floor");
	expect_eq(b.hi[0], 224, "horizontal byte ceiling");
	expect_eq(b.lo[1], 32, "depth byte floor");
	expect_eq(b.hi[1], 224, "depth byte ceiling");
	// The vertical floor is 31, not 32: ap_box_model_data.h pre-decrements the
	// vertical byte on every x=224 vertex because the renderer's packed XY add can
	// carry out of the horizontal half. The ENCODED cube is the symmetric 32..224.
	expect_eq(b.lo[AP_BOX_VERT_AXIS_UP], 31, "vertical byte floor is the carry-compensated 31");
	expect_eq(b.hi[AP_BOX_VERT_AXIS_UP], 224, "vertical byte ceiling");
	expect_eq(b.hi[AP_BOX_VERT_AXIS_UP] - (b.lo[AP_BOX_VERT_AXIS_UP] + 1), AP_BOX_MODEL_EXTENT,
	          "the encoded cube spans exactly AP_BOX_MODEL_EXTENT");
	expect_eq(AP_BoxMesh_Extent(&b), AP_BOX_MODEL_EXTENT + 1,
	          "the raw byte extent is one unit taller than the encoded cube");

	// ── 2. units and the derived scale ──────────────────────────────────────

	expect_eq(AP_BoxOffset_ModelToWorld(AP_BOX_SCALE_ONE, AP_BOX_SCALE_ONE), AP_BOX_SCALE_ONE,
	          "scale 1.0 is the identity");
	expect_eq(AP_BoxOffset_ModelToWorld(100, AP_BOX_SCALE_ONE / 2), 50, "scale 0.5 halves");
	expect_eq(AP_BoxOffset_ModelToWorld(100, 0), 0, "a zero scale measures nothing");
	expect_eq(AP_BoxOffset_ModelToWorld(100, -8), 0, "a negative scale measures nothing");

	scale = AP_BoxOffset_DeriveScale(RETAIL_CRATE_SCALE, RETAIL_CRATE_EXTENT, AP_BOX_MODEL_EXTENT);
	expect_eq(scale, AP_BOX_FALLBACK_SCALE,
	          "the live-probed retail crate derives exactly the shipped fallback scale");
	expect_eq(AP_BoxOffset_DeriveScale(0, RETAIL_CRATE_EXTENT, AP_BOX_MODEL_EXTENT), 0,
	          "a source with no scale derives nothing");
	expect_eq(AP_BoxOffset_DeriveScale(RETAIL_CRATE_SCALE, 0, AP_BOX_MODEL_EXTENT), 0,
	          "a source with no extent derives nothing");

	// The size ruling itself: at the derived scale the AP cube renders exactly as
	// tall as the retail crate it was measured against.
	expect_eq(AP_BoxOffset_Height(0, AP_BOX_MODEL_EXTENT, scale),
	          AP_BoxOffset_Height(0, RETAIL_CRATE_EXTENT, RETAIL_CRATE_SCALE),
	          "AP cube and retail crate render the same height");
	expect_eq(AP_BoxOffset_Height(0, RETAIL_CRATE_EXTENT, RETAIL_CRATE_SCALE), 108,
	          "a retail crate is 108 world units tall");

	// ── 3. THE SELECTED OFFSET ──────────────────────────────────────────────

	lift = AP_BoxOffset_BaseY(AP_BOX_FRAME_POS, b.lo[AP_BOX_VERT_AXIS_UP], scale);
	expect_eq(lift, 54, "the AP crate spawn lift is 54 world units at the derived scale");
	// The carry compensation must not be able to change the answer: the encoded
	// floor and the raw floor land on the same integer lift.
	expect_eq(AP_BoxOffset_BaseY(AP_BOX_FRAME_POS, b.lo[AP_BOX_VERT_AXIS_UP] + 1, scale), lift,
	          "the encoded vertical floor gives the same lift as the raw one");
	expect_eq(AP_BoxOffset_BaseY(AP_BOX_FRAME_POS, b.lo[AP_BOX_VERT_AXIS_UP], AP_BOX_FALLBACK_SCALE), lift,
	          "the fallback scale gives the same lift as the derived one");

	// ── 4. the same lift a centred retail crate would need ──────────────────

	retailLift = AP_BoxOffset_BaseY(AP_BOX_FRAME_POS, 0, RETAIL_CRATE_SCALE);
	expect_eq(retailLift, lift,
	          "a retail crate centred on its origin needs the identical lift");

	// ── 5. the centred-origin invariant (and the pre-fix mutation) ──────────

	height = AP_BoxOffset_Height(b.lo[AP_BOX_VERT_AXIS_UP], b.hi[AP_BOX_VERT_AXIS_UP], scale);
	expect(lift * 2 >= height - 2 && lift * 2 <= height + 2,
	       "the lift is half the rendered height, i.e. the origin is the centre");
	// The behaviour this replaces: spawning at the authored anchor with no lift
	// leaves the bottom half of a 108-unit crate below the road.
	expect(lift > 0, "an uncorrected spawn (lift 0) cannot satisfy the invariant above");
	expect(lift < height, "the lift never exceeds the crate's own height");

	// ── 6. fail closed ──────────────────────────────────────────────────────

	expect_eq(AP_BoxOffset_BaseY(AP_BOX_FRAME_POS, b.lo[AP_BOX_VERT_AXIS_UP], 0), 0,
	          "an unmeasurable scale falls back to the authored anchor");
	expect_eq(AP_BoxOffset_BaseY(0, 0, scale), 0, "an origin already at the base needs no lift");
	expect_eq(AP_BoxOffset_BaseY(0, 40, scale), 0, "an origin below the base is never pushed down");
	expect(AP_BoxOffset_BaseY(AP_BOX_FRAME_POS, b.lo[AP_BOX_VERT_AXIS_UP], scale) >= 0,
	       "a lift is never negative");

	expect_eq(AP_BoxMesh_CountVerts(0), 0, "a null command list measures nothing");
	{
		static const unsigned int emptyList[] = {AP_BOX_MODEL_NUM_COLORS, 0xffffffffu};
		static const unsigned int colorOnly[] = {AP_BOX_MODEL_NUM_COLORS, 0x00000010u, 0xffffffffu};
		static const unsigned int cachedVert[] = {AP_BOX_MODEL_NUM_COLORS, 0x84010000u, 0xffffffffu};
		static unsigned int runaway[AP_BOX_MESH_MAX_COMMANDS + 2];
		unsigned int i;

		expect_eq(AP_BoxMesh_CountVerts(emptyList), 0, "a list with no vertices measures nothing");
		expect_eq(AP_BoxMesh_CountVerts(colorOnly), 0, "a colour-only command consumes no vertex");
		expect_eq(AP_BoxMesh_CountVerts(cachedVert), 0, "a bit-26 command reuses a vertex, it does not take one");

		for (i = 0; i < (unsigned int)(AP_BOX_MESH_MAX_COMMANDS + 2); i++)
			runaway[i] = 0x80010000u;
		expect_eq(AP_BoxMesh_CountVerts(runaway), 0, "an unterminated command list is refused, not walked forever");
	}
	{
		// The vertex ceiling, exactly at and one past. A mesh at the ceiling is a
		// mesh, one past it is refused: a bound that drops data must say so rather
		// than measure a truncated model (Lessons Learned §4).
		static unsigned int atCeiling[AP_BOX_MESH_MAX_VERTS + 2];
		static unsigned int pastCeiling[AP_BOX_MESH_MAX_VERTS + 3];
		int i;

		atCeiling[0] = 1;
		for (i = 0; i < AP_BOX_MESH_MAX_VERTS; i++)
			atCeiling[1 + i] = 0x80010000u;
		atCeiling[1 + AP_BOX_MESH_MAX_VERTS] = 0xffffffffu;
		expect_eq(AP_BoxMesh_CountVerts(atCeiling), AP_BOX_MESH_MAX_VERTS,
		          "a mesh exactly at the vertex ceiling still measures");

		pastCeiling[0] = 1;
		for (i = 0; i < AP_BOX_MESH_MAX_VERTS + 1; i++)
			pastCeiling[1 + i] = 0x80010000u;
		pastCeiling[2 + AP_BOX_MESH_MAX_VERTS] = 0xffffffffu;
		expect_eq(AP_BoxMesh_CountVerts(pastCeiling), 0,
		          "a mesh one vertex past the ceiling is refused, not truncated");
	}

	expect_eq(AP_BoxMesh_Bounds(s_apBoxModelVerts, 0, &b), 0, "a zero-vertex block measures nothing");
	expect_eq(AP_BoxMesh_Bounds(0, 12, &b), 0, "a null vertex block measures nothing");
	expect_eq(AP_BoxMesh_Extent(0), 0, "null bounds have no extent");

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
