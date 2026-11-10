#ifndef CTR_TEXT_BOX_H
#define CTR_TEXT_BOX_H

enum { TEXT_ELLIPSIS, TEXT_MARQUEE };
enum {
    TEXT_SLOT_ARCADE, TEXT_SLOT_ARCADE_DETAIL, TEXT_SLOT_LIBRARY,
    TEXT_SLOT_SOLO, TEXT_SLOT_SOLO_VALUE, TEXT_SLOT_CONNECTION,
    TEXT_SLOT_SHOP, TEXT_SLOT_BANNER, TEXT_SLOT_FEED,
    TEXT_SLOT_COUNT = TEXT_SLOT_FEED + 5
};

/* Box coordinates are left anchored UI pixels. Fitting text retains flags. */
void DecalFont_DrawLineBox(const char *text, int x, int y, int width,
    short font, int flags, int mode, int slot, unsigned int *ot);
int DecalFont_LineBoxOffset(const char *text, int width, short font, int slot);
void DecalFont_ResetLineBox(int slot);

#endif
