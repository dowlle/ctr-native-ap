#include <common.h>

struct SelectProfileLoadSaveIcon
{
	struct Instance *inst;
	s16 rot[3];
	s16 padding;
};

struct SelectProfileLoadSaveObj
{
	struct Thread *thread;
	struct SelectProfileLoadSaveIcon *icons;
};

_Static_assert(sizeof(struct SelectProfileLoadSaveIcon) == 0xc);
_Static_assert(sizeof(struct SelectProfileLoadSaveObj) == 0x8);

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80047dfc-0x80047f20.
void SelectProfile_ThTick(struct Thread *t)
{
	struct SelectProfileLoadSaveObj *obj;
	struct SelectProfileLoadSaveIcon *icon;
	int i;

	obj = (struct SelectProfileLoadSaveObj *)t->object;
	icon = obj->icons;

	for (i = 0; i < 12; i++, icon++)
	{
		int slot = i % 3;

		icon->rot[1] = (s16)(icon->rot[1] + sdata->LoadSave_SpinRateY[slot]);
		ConvertRotToMatrix(&icon->inst->matrix, &icon->rot[0]);

		if (slot != 1)
		{
			Vector_SpecLightSpin3D(icon->inst, &icon->rot[0], &data.MetaDataLoadSave[i].vec3_specular_inverted[0]);
		}
	}
}
