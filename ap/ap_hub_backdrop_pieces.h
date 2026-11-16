/* Static retail hub scenery for name entry. Included with engine types in
 * scope, and by the host harness with a small engine fixture. No threads,
 * progression reads, collision changes or model-header writes belong here. */
#ifndef AP_HUB_BACKDROP_PIECES_H
#define AP_HUB_BACKDROP_PIECES_H

static int AP_HNE_AddBackdropPiece(struct GameTracker *gGT, struct Instance *inst)
{
	struct Model *model;
	struct Instance *piece;
	int id, doorID = 0, i;
	if (inst == NULL || inst->model == NULL || inst->instDef == NULL || inst->thread != NULL)
		return 0;
	id = inst->model->id;
	if (id == STATIC_DOOR)
	{
		/* AH_Door_LInB: the LEV has the left leaf only. Match its choice
		 * of holes on the mirrored right leaf, in the closed pose. */
		for (i = 5; i < 16 && inst->name[i] >= '0' && inst->name[i] <= '9'; i++)
			doorID = doorID * 10 + inst->name[i] - '0';
		id = gGT->levelID == GLACIER_PARK ? STATIC_DOOR3 :
		     (gGT->levelID == N_SANITY_BEACH && doorID == 5 ? STATIC_DOOR2 : STATIC_DOOR);
		model = gGT->modelPtr[id];
		if (model == NULL)
			return 0;
		piece = INSTANCE_Birth3D(model, "backdrop-door", NULL);
		if (piece == NULL)
			return 0;
		inst->flags |= SPLIT_SPECIAL;
		piece->flags |= SPLIT_SPECIAL | REVERSE_CULL_DIRECTION;
		piece->matrix = inst->matrix;
		piece->scale.x = -0x1000;
		piece->matrix.t[0] += MATH_Cos(inst->instDef->rot.y) * 0x600 >> 12;
		piece->matrix.t[2] += MATH_Sin(inst->instDef->rot.y) * 0x600 >> 12;
		/* Retail also sets model-header bit 2, which the native renderer
		 * does not read. Leave shared model headers alone. */
		return 1;
	}
	/* Oxide's door has no separate top, exactly as AH_Garage_LInB. */
	if (id != STATIC_ROOGARAGE && id != STATIC_PAPUGARAGE &&
	    id != STATIC_JOEGARAGE && id != STATIC_PINGARAGE)
		return 0;
	model = gGT->modelPtr[STATIC_GARAGETOP];
	if (model == NULL)
		return 0;
	piece = INSTANCE_Birth3D(model, "backdrop-top", NULL);
	if (piece == NULL)
		return 0;
	/* AH_Garage_LInB's closed cap, before any gate or animation logic. */
	piece->matrix = inst->matrix;
	piece->matrix.t[0] += MATH_Sin(inst->instDef->rot.y) * 0x4c >> 12;
	piece->matrix.t[1] += 0x300;
	piece->matrix.t[2] += MATH_Cos(inst->instDef->rot.y) * 0x4c >> 12;
	piece->depthBiasNormal = 0xfe;
	inst->depthBiasNormal = 1;
	inst->depthBiasSecondary = 1;
	inst->unk53 = 0;
	inst->vertSplit = inst->instDef->pos.y + 0x300;
	return 1;
}
#endif
