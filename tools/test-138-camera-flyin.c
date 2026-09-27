// Regression harness for issue 138 (Linux race-start fly-in camera).
//
//   cc -m32 -std=gnu11 -DCTR_NATIVE -DBUILD=926 -DCTR_INTERNAL -DCTR_AP -I . -I include -msse -Wall -Wstrict-aliasing=2 -ffunction-sections -fdata-sections -Wl,--gc-sections -O2 -o /tmp/test-138-camera-flyin tools/test-138-camera-flyin.c -lm && /tmp/test-138-camera-flyin
//
// Grew out of the host probe from the 2026-09-20 work order
// (~/ctr-worktrees/native-138-camera-probe/tools/test-138-camera-probe.c).
// That probe answered "does the intro-camera math agree across optimization
// levels and strict-aliasing settings" and found that it did not: at -O2 with
// the default strict-aliasing rules, GCC 13 deletes the 16-bit stores that
// build the fly-in camera path point (game/CAM.c CAM_StartLine_FlyIn reads
// through the GTE load macros in include/psx/inline_c.h, which alias
// SVECTOR's s16 fields through a plain `uint32_t *`; that is undefined
// behaviour under strict aliasing, and GCC's IPA mod/ref analysis plus
// dead-store elimination act on it). See:
//   11-Dev/CTR Archipelago/Development/Issue 138 Linux starting line
//   cutscene research - 2026-09-28.md
//
// This harness fixes that into a permanent CI check: it builds the same real
// camera code at -O2 and asserts the frame trace matches the one stable hash
// observed at -O1, at -O2 -fno-strict-aliasing, and at -O2 against the
// may_alias-patched header. If a future header change (or a toolchain bump
// that starts optimizing more aggressively) reopens the aliasing bug, the
// hash changes or stops being stable and this harness fails.
//
// Build note: the -O2 here is deliberate and load-bearing. The default host
// harness recipe in tools/ci/run-harnesses.py does not pass an optimization
// flag (effectively -O0), which would never exercise the bug this harness
// guards against, so this file carries its own build line instead of relying
// on the generic recipe.
//
// The only stub is COLL_SearchBSP_CallbackQUADBLK (FixY's collision probe): it
// leaves boolDidTouchQuadblock clear so the harness needs no level BSP and
// every difference between builds comes from the camera math, never from
// level data.

#include "game/CAM.c"
#include "game/MATH/MATH_7_MatrixStubs.c"
#include "game/MATH/MATH_0_Sin.c"
#include "game/MATH/MATH_1_Cos.c"
#include "platform/native_gte_core.c"
#include "platform/native_inline_c.c"
#include "platform/native_libgte.c"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Globals the engine normally provides. Defined here for the host harness.
struct sData sdata_static;
struct Data data;
u8 *gCTRNativeScratchpadBase;

// Layout checks the issue-138 result packet asked for, taken on the real
// production types rather than on copies. A 32-bit build must agree.
_Static_assert(sizeof(struct FlyInData) == 12, "FlyInData must be 12 bytes");
_Static_assert(__builtin_offsetof(struct FlyInData, ptrEnd) == 0, "FlyInData.ptrEnd");
_Static_assert(__builtin_offsetof(struct FlyInData, ptrStart) == 4, "FlyInData.ptrStart");
_Static_assert(__builtin_offsetof(struct FlyInData, frameCount1) == 8, "FlyInData.frameCount1");
_Static_assert(__builtin_offsetof(struct FlyInData, frameCount2) == 10, "FlyInData.frameCount2");
_Static_assert(__builtin_offsetof(struct CameraDC, nearOrFar) == 0x0a, "CameraDC.nearOrFar");
_Static_assert(__builtin_offsetof(struct CameraDC, trackPathNode) == 0x88, "CameraDC.trackPathNode");
_Static_assert(__builtin_offsetof(struct CameraDC, transitionBlend) == 0x8c, "CameraDC.transitionBlend");
_Static_assert(__builtin_offsetof(struct CameraDC, transitionFrame) == 0x8e, "CameraDC.transitionFrame");
_Static_assert(__builtin_offsetof(struct CameraDC, trackPathProgress) == 0x94, "CameraDC.trackPathProgress");
_Static_assert(__builtin_offsetof(struct CameraDC, cameraMode) == 0x9a, "CameraDC.cameraMode");
_Static_assert(__builtin_offsetof(struct CameraDC, transitionFrameCount) == 0x9e, "CameraDC.transitionFrameCount");
_Static_assert(sizeof(void *) == 4, "harness must be built 32-bit");

// The trace hash observed at -O1, at -O2 -fno-strict-aliasing, and at -O2
// against the may_alias-patched include/psx/inline_c.h. A plain -O2 build
// against the broken header does not reach this value twice in a row.
#define EXPECTED_TRACE_HASH 0xc9716b5cb8cc53deULL

static int failures;

static void expect(int condition, const char *name)
{
	if (!condition)
	{
		failures++;
		printf("FAIL: %s\n", name);
	}
}

// FixY's collision probe, neutralised: never report a hit, never move the spawn.
void COLL_SearchBSP_CallbackQUADBLK(const SVec3 *top, const SVec3 *bottom, struct ScratchpadStruct *sps, s32 hitRadius)
{
	(void)top;
	(void)bottom;
	(void)hitRadius;
	sps->boolDidTouchQuadblock = 0;
}

// The AP mirror-cull trap hook in GTE_operator (platform/native_gte_core.c:367).
// The fly-in path never issues the NCLIP op that consults it and no trap is
// active, so it reports "not engaged" exactly as the clean source would.
int AP_TrapMirrorCullFlip(void)
{
	return 0;
}

// Fixture: the two authored camera tables the fly-in reads, ptrStart at
// path+0x0 and ptrEnd at path+0x354, exactly as CAM_ThTick hands them to
// CAM_StartLine_FlyIn (game/CAM.c:1677-1680). CAM_StartLine_FlyIn treats a
// record as three s16 and reads the next record through path[3..5]
// (game/CAM.c:880-887), so records are laid out at a 3-s16 stride. Guard
// sentinels on both sides turn any out-of-range index into a detected change.
#define PATH_END_OFFSET 0x354
#define PATH_RECORDS 0x98
#define PATH_GUARD 0x20
#define PATH_S16 (PATH_END_OFFSET / 2 + PATH_RECORDS * 3 + PATH_GUARD * 2)
static s16 cameraPathStore[PATH_S16];
static s16 *const cameraPath = cameraPathStore + PATH_GUARD;
static struct Level probeLevel;

static s16 Probe_Mix(s32 i, s32 k)
{
	// Deterministic signed patterns that cross zero and sit near the s16 bounds.
	s32 base = (k == 0) ? (-3000 + i * 37)     // x
	           : (k == 1) ? (0x7000 - i * 11)  // y
	                      : (-0x7000 + i * 29); // z
	if ((i % 7) == 0)
	{
		base = (k == 0) ? -0x8000 : (k == 1) ? 0x7fff : 0x8000;
	}
	if ((i % 13) == 5)
	{
		base = -1;
	}
	return (s16)base;
}

static void Probe_FillPath(void)
{
	s32 frame;
	s32 endBase = PATH_END_OFFSET / 2;
	s32 i;

	for (i = 0; i < PATH_GUARD; i++)
	{
		cameraPathStore[i] = (s16)0x5a5a;
		cameraPathStore[PATH_GUARD + PATH_S16 - 2 * PATH_GUARD + i] = (s16)0x5a5a;
	}

	for (frame = 0; frame < PATH_RECORDS; frame++)
	{
		for (i = 0; i < 3; i++)
		{
			s16 pos = Probe_Mix(frame, i);
			cameraPath[frame * 3 + i] = pos;
			cameraPath[endBase + frame * 3 + i] = (s16)(pos + 0x40);
		}
	}
}

// Verify the guard sentinels survived, so no path index left the fixture.
static int Probe_PathInBounds(void)
{
	s32 i;
	for (i = 0; i < PATH_GUARD; i++)
	{
		if (cameraPathStore[i] != (s16)0x5a5a)
		{
			return 0;
		}
		if (cameraPathStore[PATH_GUARD + PATH_S16 - 2 * PATH_GUARD + i] != (s16)0x5a5a)
		{
			return 0;
		}
	}
	return 1;
}

static void Probe_FillTrig(void)
{
	s32 i;
	for (i = 0; i < 0x400; i++)
	{
		double a = (2.0 * 3.14159265358979323846 * (double)i) / 1024.0;
		data.trigApprox[i].sin = (s16)lround(sin(a) * 4096.0);
		data.trigApprox[i].cos = (s16)lround(cos(a) * 4096.0);
	}
}

static void Probe_InitLevel(void)
{
	memset(&probeLevel, 0, sizeof probeLevel);
	probeLevel.DriverSpawn[0].rot.x = 0x0100;
	probeLevel.DriverSpawn[0].rot.y = 0x0200;
	probeLevel.DriverSpawn[0].rot.z = 0x0010;
	probeLevel.DriverSpawn[1].pos.x = 1000;
	probeLevel.DriverSpawn[1].pos.y = 500;
	probeLevel.DriverSpawn[1].pos.z = -1000;
	probeLevel.DriverSpawn[2].pos.x = 1400;
	probeLevel.DriverSpawn[2].pos.y = 520;
	probeLevel.DriverSpawn[2].pos.z = -1000;
	probeLevel.DriverSpawn[5].pos.x = 1800;
	probeLevel.DriverSpawn[5].pos.y = 460;
	probeLevel.DriverSpawn[5].pos.z = -900;
	probeLevel.DriverSpawn[1].rot.y = 100;
	probeLevel.DriverSpawn[2].rot.y = 150;

	// CAM_StartOfRace only checks cnt_restart_points > 2; the fly-in itself is
	// driven by CAM_ThTick's fixed offsets.
	probeLevel.cnt_restart_points = 4;
	probeLevel.ptr_restart_points = (struct CheckpointNode *)(void *)cameraPath;
}

// Mirror of the final-blend block in CAM_ThTick (game/CAM.c:1725-1753). The
// production CALC dominates this loop; MATH_Sin/MATH_Cos are the production
// functions, so this only keeps the call shape in one place per arm.
static void Probe_UpdateBlend(struct CameraDC *cDC)
{
	s32 frameCount = cDC->transitionFrameCount;
	s32 frame = cDC->transitionFrame;
	s32 x;

	if (frameCount == 0 || frame > frameCount)
	{
		return;
	}

	x = frameCount >> 1;
	if (frame < x)
	{
		x = MATH_Sin(0x400 - (frame << 10) / x);
		cDC->transitionBlend = (s16)(x / 2) + 0x800;
	}
	else
	{
		x = MATH_Cos(((frame - frameCount) * 0x400) / (frameCount >> 1));
		cDC->transitionBlend = 0x800 - (s16)(x / 2);
	}
}

static unsigned long long Probe_MixHash(unsigned long long h, long long v)
{
	int i;
	for (i = 0; i < 8; i++)
	{
		h ^= (unsigned long long)((v >> (i * 8)) & 0xff);
		h *= 1099511628211ULL;
	}
	return h;
}

int main(void)
{
	struct CameraDC cDC;
	struct FlyInData flyInData;
	SVec3 pushPos;
	SVec3 pushRot;
	unsigned long long hash = 1469598103934665603ULL;
	s32 frame;

	memset(&pushPos, 0, sizeof pushPos);
	memset(&pushRot, 0, sizeof pushRot);

	Probe_FillPath();
	Probe_FillTrig();
	Probe_InitLevel();

	sdata_static.gGT = &sdata_static.gameTracker;
	sdata_static.gGT->level1 = &probeLevel;

	memset(&cDC, 0, sizeof cDC);
	memset(&flyInData, 0, sizeof flyInData);

	// The exact fly-in call shape from CAM_ThTick (game/CAM.c:1674-1693).
	flyInData.ptrStart = (u8 *)cameraPath;
	flyInData.ptrEnd = (u8 *)cameraPath + PATH_END_OFFSET;
	flyInData.frameCount1 = 0x96;
	flyInData.frameCount2 = 0x8e;

	CAM_StartOfRace(&cDC);

	for (frame = 0; frame <= 0xA5; frame++)
	{
		SVec3 desiredPos;
		SVec3 desiredRot;
		s32 flyFrame = 0xa5 - (u16)cDC.transitionFrame;
		s16 blend;

		memset(&desiredPos, 0, sizeof desiredPos);
		memset(&desiredRot, 0, sizeof desiredRot);

		if (flyFrame > 0x96)
		{
			flyFrame = 0x96;
		}

		CAM_StartLine_FlyIn(&flyInData, 0x96, flyFrame, &desiredPos, &desiredRot);

		blend = cDC.transitionBlend;
		CAM_ProcessTransition(&pushPos, &pushRot, &desiredPos, &desiredRot, &pushPos, &pushRot, blend);

		hash = Probe_MixHash(hash, frame);
		hash = Probe_MixHash(hash, flyFrame);
		hash = Probe_MixHash(hash, blend);
		hash = Probe_MixHash(hash, desiredPos.x);
		hash = Probe_MixHash(hash, desiredPos.y);
		hash = Probe_MixHash(hash, desiredPos.z);
		hash = Probe_MixHash(hash, desiredRot.x);
		hash = Probe_MixHash(hash, desiredRot.y);
		hash = Probe_MixHash(hash, desiredRot.z);
		hash = Probe_MixHash(hash, pushPos.x);
		hash = Probe_MixHash(hash, pushPos.y);
		hash = Probe_MixHash(hash, pushPos.z);
		hash = Probe_MixHash(hash, pushRot.x);
		hash = Probe_MixHash(hash, pushRot.y);
		hash = Probe_MixHash(hash, pushRot.z);

		Probe_UpdateBlend(&cDC);
		cDC.transitionFrame--;
		if (cDC.transitionFrame < 0)
		{
			cDC.transitionFrame = 0;
		}
	}

	expect(Probe_PathInBounds(), "path fixture stayed in bounds (guard sentinels intact)");
	printf("# trace hash = %016llx (expected %016llx)\n", hash, EXPECTED_TRACE_HASH);
	expect(hash == EXPECTED_TRACE_HASH,
	       "intro-camera fly-in trace hash matches the known-good value at -O2");

	printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
