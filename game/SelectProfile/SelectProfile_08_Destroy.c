#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800488e0-0x80048960.
void SelectProfile_Destroy(void)
{
	struct SelectProfileLoadSaveObj *obj;

	obj = (struct SelectProfileLoadSaveObj *)sdata->ptrLoadSaveObj;
	if (obj != NULL)
	{
		struct SelectProfileLoadSaveIcon *icon = obj->icons;
		int i;

		for (i = 0; i < 12; i++, icon++)
		{
			if (icon->inst != NULL)
			{
				INSTANCE_Death(icon->inst);
			}
		}

		obj->thread->flags |= 0x800;
		sdata->ptrLoadSaveObj = 0;
	}
}
