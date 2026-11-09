#ifndef NATIVE_CUSTOM_FINISH_H
#define NATIVE_CUSTOM_FINISH_H
#include <string.h>

/* Exact LEV/VRM capabilities, never inferred from a title or finish gap.
   LRG2 ALL.BIN: lap table +0xcd38[17] = 1, finish terrain table
   +0xcd4c[17] = 4. The handler at +0x30c4 calls +0x2d10 when the
   touched quad's terrain matches. Both revisions mark end quads 494/496
   (checkpoint 106) with terrain 4. No teleport is involved. */
static inline int CustomFinish_Terrain(const char *lev, const char *vrm)
{
    if (!lev || !vrm) return -1;
    if ((!strcmp(lev, "e22b87a73009b94251f5216424cf07875a02e076eb8461b902624c2b5277ad45") &&
         !strcmp(vrm, "5de0e46152d8fdce3e0cbe7cdd860516d98d98105106f5d81c043b39df9f0753")) ||
        (!strcmp(lev, "736982024dc32b02d1813d724acd7d7fca36a2cbd639b93d895309ae1795d4c6") &&
         !strcmp(vrm, "e403d7b8c42268eeeb43cb3ad75a32127d90d71d378c7c4de65444e45391620a")))
        return 4;
    return -1;
}

/* Keep the refusal seam even while no known broken pair remains blocked.
   Add exact pair hashes here when a defect has no runtime repair. */
static inline const char *CustomFinish_ImportBlock(const char *lev, const char *vrm)
{
    static const struct { const char *lev, *vrm, *reason; } blocked[] = {
        {NULL, NULL, NULL}
    };
    unsigned int i;
    if (!lev || !vrm) return NULL;
    for (i = 0; blocked[i].lev; i++)
        if (!strcmp(lev, blocked[i].lev) && !strcmp(vrm, blocked[i].vrm)) return blocked[i].reason;
    return NULL;
}
#endif
