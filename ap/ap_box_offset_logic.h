#ifndef AP_BOX_OFFSET_LOGIC_H
#define AP_BOX_OFFSET_LOGIC_H

// AP crate mesh measurement and the spawn-height correction, deliberately
// freestanding, exactly like ap_box_map.h and ap_cup_box_policy.h: the engine
// half lives in ap_box_model.c (walking a live struct Model, logging the probe),
// the ARITHMETIC lives here so tools/test-box-offset.c can pin it out of engine,
// with no disc, no display and no seed.
//
// ── WHY A CORRECTION EXISTS AT ALL ──────────────────────────────────────────
//
// The authoring tool records `Driver.posCurr` straight into the placement row
// (ap_author.c, AP_AuthorDrop), and the runtime copied that row straight into
// the crate's spawn transform. Two separately checked facts make that put the
// crate half underground:
//
//   1. `Driver.posCurr` is the kart's GROUND CONTACT POINT, not its centre.
//      VehBirth_TeleportSelf spawns a racer at `posCurr.y = quadblock hitPos.y`
//      exactly (VehBirth.c:288, race start passes spawnPosY 0, BOTS.c:3047), and
//      the swept driver collision confirms the same geometry from the other
//      side: the sphere it sweeps is centred on `posCurr + originToCenter`, and
//      originToCenter is (0, 25, 0) rotated into kart space (VehPhysForce.c:511)
//      while the sphere's radius is COLL_MOVED_PLAYER_HIT_RADIUS = 0x19 = 25
//      (COLL.c:2169, :2344-2352). Centre 25 up, radius 25: the sphere's bottom
//      is the origin, so a resting kart's origin sits ON the road.
//   2. The AP crate's model origin is its CENTRE. The cube's frame origin is
//      (-128,-128,-128) and its vertex bytes span 32..224 on every axis
//      (ap_box_model_data.h), i.e. a symmetric +/-96 model units about the
//      origin.
//
// So an unmodified authored anchor buries the bottom half of the crate. The
// correction is therefore not a tuned constant: it is "lift the model until its
// own lowest face sits on the anchor", derived from the model's measured bounds
// and its live header scale, so the built-in table and any external authoring
// override behave identically and no placement row has to move.
//
// ── MODEL UNITS TO WORLD UNITS ──────────────────────────────────────────────
//
// One model unit is `headerScale / 0x1000` world units, and both halves of that
// come from the same render path:
//
//   * the vertex fed to the GTE is ((frame.pos + vertexByte) << 2), packed
//     16-bit pairs (RenderBucket_QueueExecute.c:2235-2264), and
//   * the header scale is loaded into the GTE light-colour matrix as
//     (scale >> 2) in 1.3.12, then multiplied by the instance scale with sf=1
//     (RenderBucket_BuildM3x3, :1381-1404). AP spawns keep the INSTANCE_Birth
//     default instance scale 0x1000 = 1.0 (ap_spawn.c).
//
// The <<2 on the vertex and the >>2 on the scale cancel, which is why the same
// ratio also falls out of the bounds projection at :1097-1101 (min = pos << 2,
// span = 255 << 2). The near-depth branch scales BOTH the model matrix and the
// view translation by 4, so it changes no ratio.
//
// AXIS PAIRING IS NOT (x,y,z). The packed vertex writer pairs frame pos.x with
// vertex byte0 (horizontal), pos.y with byte2 (VERTICAL) and pos.z with byte1
// (depth) -- QueueExecute.c:2237-2244 and :2264, the same pairing ap_marker_model.c
// records. Everything below therefore indexes the vertical as byte 2 rather than
// assuming the middle byte is up.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

// 1.0 in the engine's model-scale fixed point (0x1000 = one), the same unit the
// instance scale uses (ap_spawn.c) and the GTE 1.3.12 matrix entries carry.
#define AP_BOX_SCALE_ONE 0x1000

// Vertex bytes are a de-indexed (byte0, byte1, byte2) triple per vertex, and
// byte 2 is the vertical one. Named rather than written as a bare 2 or 3.
#define AP_BOX_VERT_STRIDE  3
#define AP_BOX_VERT_AXIS_UP 2

// Walk guards. Both are the values the shipped measurement already used: a
// command list longer than this, or a mesh with more vertices than this, is not
// something this measurement understands, and saying "I could not measure it"
// beats a confident wrong number (Lessons Learned §4).
#define AP_BOX_MESH_MAX_COMMANDS 8192
#define AP_BOX_MESH_MAX_VERTS    2048

typedef struct
{
	int lo[AP_BOX_VERT_STRIDE];
	int hi[AP_BOX_VERT_STRIDE];
} AP_BoxMeshBounds;

// How many vertices a command list consumes, walked exactly the way the renderer
// consumes it (RenderBucket_DrawFunc_Normal): skip the first command word (it is
// the colour count), then every word with a nonzero high half and bit 26 clear
// takes the next vertex from the frame data, until the 0xffffffff terminator.
// Returns 0 for a list this walk cannot make sense of.
static int AP_BoxMesh_CountVerts(const unsigned int *cmd)
{
	int n = 0, guard = 0;

	if (cmd == 0)
		return 0;

	for (cmd++; *cmd != 0xffffffffu && guard < AP_BOX_MESH_MAX_COMMANDS; cmd++, guard++)
	{
		if ((*cmd >> 16) == 0)
			continue; // colour-only command, no vertex
		if (((*cmd >> 24) & 4) == 0)
			n++;
	}

	if (n <= 0 || n > AP_BOX_MESH_MAX_VERTS)
		return 0;
	return n;
}

// Per-axis byte range over a de-indexed vertex block. Returns 0 on a degenerate
// input rather than handing back a range nobody may trust.
static int AP_BoxMesh_Bounds(const unsigned char *verts, int count, AP_BoxMeshBounds *out)
{
	int i;

	if (verts == 0 || out == 0 || count <= 0 || count > AP_BOX_MESH_MAX_VERTS)
		return 0;

	for (i = 0; i < AP_BOX_VERT_STRIDE; i++)
	{
		out->lo[i] = 255;
		out->hi[i] = 0;
	}

	for (i = 0; i < count * AP_BOX_VERT_STRIDE; i++)
	{
		int a = i % AP_BOX_VERT_STRIDE;

		if (verts[i] < out->lo[a])
			out->lo[a] = verts[i];
		if (verts[i] > out->hi[a])
			out->hi[a] = verts[i];
	}

	return 1;
}

// The mesh's largest span across the three axes, in model units. This is the
// "extent" the size ruling (2026-08-21) compares between the AP cube and the
// retail crate.
static int AP_BoxMesh_Extent(const AP_BoxMeshBounds *b)
{
	int i, ext = 0;

	if (b == 0)
		return 0;

	for (i = 0; i < AP_BOX_VERT_STRIDE; i++)
	{
		if (b->hi[i] - b->lo[i] > ext)
			ext = b->hi[i] - b->lo[i];
	}
	return ext;
}

// Model units -> world units at a header scale.
static int AP_BoxOffset_ModelToWorld(int modelUnits, int headerScale)
{
	if (headerScale <= 0)
		return 0;
	return (modelUnits * headerScale) / AP_BOX_SCALE_ONE;
}

// The header scale that renders a mesh of `apExtent` model units at exactly the
// size a source model of `srcExtent` renders at its own `srcScale`. The ruled
// AP-box size rule (2026-08-21): the AP crate is the exact size of the retail
// item crate, everywhere, derived live from whichever crate the level carries.
static int AP_BoxOffset_DeriveScale(int srcScale, int srcExtent, int apExtent)
{
	if (srcScale <= 0 || srcExtent <= 0 || apExtent <= 0)
		return 0;
	return (srcScale * srcExtent) / apExtent;
}

// THE CORRECTION. How far a model's own origin sits above its lowest face, in
// world units, at a given header scale -- i.e. how far up a spawn has to move so
// the model rests on its authored anchor instead of straddling it.
//
// Zero when the origin is already at or below the lowest face: such a model
// needs no lift, and a negative "correction" would push a crate INTO the road,
// which is the defect this exists to remove.
static int AP_BoxOffset_BaseY(int framePosUp, int loVertUp, int headerScale)
{
	int localMin = framePosUp + loVertUp;

	if (headerScale <= 0 || localMin >= 0)
		return 0;
	return AP_BoxOffset_ModelToWorld(-localMin, headerScale);
}

// Rendered height of a mesh in world units, for the probe line and the harness.
static int AP_BoxOffset_Height(int loVertUp, int hiVertUp, int headerScale)
{
	if (hiVertUp <= loVertUp)
		return 0;
	return AP_BoxOffset_ModelToWorld(hiVertUp - loVertUp, headerScale);
}

#endif // CTR_AP
#endif // AP_BOX_OFFSET_LOGIC_H
