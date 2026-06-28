#ifndef CTR_NATIVE_NAMESPACE_SELECTPROFILE_H
#define CTR_NATIVE_NAMESPACE_SELECTPROFILE_H

struct SelectProfileLoadSaveIcon
{
	struct Instance *inst;
	SVec3 rot;
	s16 padding;
};

struct SelectProfileLoadSaveObj
{
	struct Thread *thread;
	struct SelectProfileLoadSaveIcon *icons;
};

CTR_STATIC_ASSERT(sizeof(struct SelectProfileLoadSaveIcon) == 0xc);
CTR_STATIC_ASSERT(sizeof(struct SelectProfileLoadSaveObj) == 0x8);

#endif
