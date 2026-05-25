#include <common.h>

static void Vector_TransposeMatrix(MATRIX *dst, const MATRIX *src)
{
	dst->m[0][0] = src->m[0][0];
	dst->m[0][1] = src->m[1][0];
	dst->m[0][2] = src->m[2][0];
	dst->m[1][0] = src->m[0][1];
	dst->m[1][1] = src->m[1][1];
	dst->m[1][2] = src->m[2][1];
	dst->m[2][0] = src->m[0][2];
	dst->m[2][1] = src->m[1][2];
	dst->m[2][2] = src->m[2][2];
	dst->t[0] = 0;
	dst->t[1] = 0;
	dst->t[2] = 0;
}

static void Vector_LightMatrixMul(MATRIX *matrix, const SVec3 *input, SVec3 *output)
{
	VECTOR mac;

	gte_SetLightMatrix(matrix);
	gte_ldv0((SVECTOR *)input);
	gte_llv0();
	gte_stlvnl(&mac);

	output->x = (s16)mac.vx;
	output->y = (s16)mac.vy;
	output->z = (s16)mac.vz;
}

void Vector_SpecLightNoSpin3D(struct Instance *inst, s16 *rot, s16 *lightDir)
{
	// NOTE(aalhendi): Source-backed NTSC-U 926 0x800576b8-0x80057884.
	// TODO(aalhendi): Complete exact ASM pass after ConvertRotToMatrix_Transpose is ported.
	MATRIX rotMatrix;
	MATRIX lightMatrix;
	SVec3 light = {.x = lightDir[0], .y = lightDir[1], .z = lightDir[2]};
	SVec3 lightLocal;
	struct GameTracker *gGT = sdata->gGT;
	struct InstDrawPerPlayer *idpp = INST_GETIDPP(inst);

	ConvertRotToMatrix(&rotMatrix, rot);
	Vector_TransposeMatrix(&lightMatrix, &rotMatrix);
	Vector_LightMatrixMul(&lightMatrix, &light, &lightLocal);

	inst->unk53 = (char)lightLocal.x;
	inst->reflectionRGBA = (u16)lightLocal.z;

	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		struct PushBuffer *pb = &gGT->pushBuffer[i];
		SVec3 viewLocal;
		SVec3 halfVector;
		SVec3 view = {
		    .x = inst->matrix.t[0] - pb->pos[0],
		    .y = inst->matrix.t[1] - pb->pos[1],
		    .z = inst->matrix.t[2] - pb->pos[2],
		};

		MATH_VectorNormalize(&view);
		Vector_LightMatrixMul(&lightMatrix, &view, &viewLocal);

		halfVector.x = lightLocal.x + viewLocal.x;
		halfVector.y = lightLocal.y + viewLocal.y;
		halfVector.z = lightLocal.z + viewLocal.z;
		MATH_VectorNormalize(&halfVector);

		idpp[i].specLight[0] = halfVector.x;
		idpp[i].specLight[1] = halfVector.y;
		idpp[i].specLight[2] = halfVector.z;
	}
}
