// Production matrix builders and NCLIP, with synthetic geometry (no disc needed).
// cc -m32 -std=c99 -DCTR_NATIVE -DCTR_AP -DBUILD=926 -I include -I .
// -ffunction-sections -fdata-sections -Wl,--gc-sections
// tools/test-mirror-model-cull.c platform/native_inline_c.c platform/native_libgte.c
// -lm -o /tmp/test-mirror-model-cull
//
// The orb fixture uses the observed header flags/scale of both NTSC-U LODs:
// ALWAYS_POINT_NORTH, {11120,11120,6748}. Vertices are a representative triangle,
// not retail geometry. A custom basis has negative Y even without Mirror Mode.
#include <stdio.h>
#include <string.h>
#include "../game/RenderBucket/RenderBucket_QueueExecute.c"
#include "../platform/native_gte_core.c"
#include "../game/MATH/MATH_7_MatrixStubs.c"

static int mirrored;
static int checks, failures;
struct sData sdata_static;
int AP_TrapMirrorCullFlip(void) { return mirrored; }

static void expect(int condition, const char *name)
{
	checks++;
	if (!condition) { printf("FAIL %s\n", name); failures++; }
}

static void identity(MATRIX *m)
{
	memset(m, 0, sizeof(*m));
	m->m[0][0] = m->m[1][1] = m->m[2][2] = 4096;
}

static int nclip(struct InstDrawPerPlayer *idpp, int override)
{
	gte_SetRotMatrix(&idpp->mvp);
	gte_SetTransMatrix(&idpp->mvp);
	gte_SetGeomOffset(160, 120);
	gte_SetGeomScreen(256);
	MTC2(RenderBucket_PackXY(-32, -32), 0); MTC2(0, 1);
	MTC2(RenderBucket_PackXY(32, -32), 2); MTC2(0, 3);
	MTC2(RenderBucket_PackXY(0, 32), 4); MTC2(0, 5);
	gte_rtpt();
	NativeGte_ModelCullFlip = override;
	gte_nclip();
	return (int)MFC2(24);
}

static void fixture(unsigned instFlags, unsigned modelFlags, int orb)
{
	struct GameTracker gt = {0};
	struct InstDrawPerPlayer idpp = {0};
	struct Instance inst = {0};
	struct ModelHeader mh = {0};
	struct ModelFrame frame = {0};
	struct RenderBucketMatrixState scale;
	MATRIX projection;
	int mode, baseline = 0;
	sdata->gGT = &gt;
	identity(&inst.matrix);
	inst.scale.x = inst.scale.y = inst.scale.z = 4096;
	mh.flags = modelFlags;
	mh.scale.x = mh.scale.y = orb ? 11120 : 4096;
	mh.scale.z = orb ? 6748 : 4096;
	for (mode = 0; mode < 2; mode++)
	{
		u32 flags = instFlags;
		int area;
		mirrored = mode;
		identity(&gt.pushBuffer[0].matrix_ViewProj);
		if (mode) gt.pushBuffer[0].matrix_ViewProj.m[0][0] = -4096;
		identity(&idpp.m3x3);
		idpp.mvp.t[0] = 0; idpp.mvp.t[1] = 0; idpp.mvp.t[2] = 4096;
		RenderBucket_BuildM3x3(&inst, &mh, 4096, &scale);
		RenderBucket_StoreMatrixWords(&idpp.m3x3, scale.m0, scale.m1, scale.m2, scale.m3, scale.m4);
		RenderBucket_BuildSplitState(&inst, &mh, &frame, 0, &gt.pushBuffer[0], &idpp,
		                            4096, &flags, &scale, &projection);
		expect(idpp.mirrorCullFlip == (mode && !RenderBucket_NeedsCustomMatrix(flags, &mh)), "builder records mirror contribution");
		area = nclip(&idpp, idpp.mirrorCullFlip);
		expect(area != 0, "representative triangle is nondegenerate");
		if (!mode) baseline = area;
		else expect((area > 0) == (baseline > 0), "front face survives Mirror Mode");
		if (mode && RenderBucket_NeedsCustomMatrix(flags, &mh))
		{
			expect(NativeGte_RotMatrixReversesWinding(), "custom fixed tilt reverses determinant");
			expect((nclip(&idpp, -1) > 0) != (baseline > 0), "old determinant policy reproduces the bug");
		}
		if (!mode) expect(nclip(&idpp, -1) == area, "Mirror Mode off is unchanged");
	}
	// A second player's normal view is never mirrored by the trap.
	identity(&gt.pushBuffer[1].matrix_ViewProj);
	RenderBucket_BuildMvp(&gt.pushBuffer[1], &idpp, &projection);
	expect(!idpp.mirrorCullFlip, "other viewport keeps its cull policy");
	// Split/reflection MVP builders also record the view contribution.
	RenderBucket_BuildSplitViewMvp(&gt.pushBuffer[0], &idpp, &projection);
	expect(idpp.mirrorCullFlip, "split view records mirrored view contribution");
	// Metadata is frozen with the matrix, not re-read from live trap state.
	mirrored = 0;
	expect(idpp.mirrorCullFlip, "queued draw retains metadata after trap changes");
	NativeGte_ModelCullFlip = -1;
	sdata->gGT = 0;
}

int main(void)
{
	fixture(0, 0, 0);                         // normal model
	fixture(CUSTOM_MATRIX, 0, 0);             // custom billboard matrix
	fixture(DRAW_BILLBOARD, 0, 0);            // color flag alone is not a matrix
	fixture(0, RB_MODEL_ALWAYS_POINT_NORTH, 1); // DYNAMIC_WARPBALL (0x36), both LODs
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
