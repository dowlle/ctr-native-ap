#ifndef NATIVE_TIZI_MADNESS_H
#define NATIVE_TIZI_MADNESS_H
#include <string.h>

/* Tizi Madness (A4-96): Coco Team Racing track 3 by FrostedWombat, the
   training track for the Tiziano shortcut. While it is raced the local player
   always holds a Mask (ap/ap_tizi_madness.c).

   Identity is the exact LEV and VRM pin pair the importer produces from the
   Coco patch (17e43e10..., BIGFILE 47), never the title: a renamed copy still
   counts, and a different track that happens to be called "Tizi Madness" does
   not. The package pin is not used, because it is the hash of a manifest that
   holds the title and the credit, so it changes when the track is renamed or
   imported again (A4-95). The apworld keeps the same pair in
   worlds/ctr/tizi_madness.py; tools/test-tizi-madness.c pins both. */
#define TIZI_MADNESS_LEV_SHA256 "3e80b47339d243db00acc33d6b7eb9efb15eaf946aa830b1766d56f5b1c15e42"
#define TIZI_MADNESS_VRM_SHA256 "74c00036dd24a34134c76236ae89dba1f3caa4b67bb30ca5717988b8987bf6ce"

static inline int CustomTrack_IsTiziMadness(const char *lev, const char *vrm)
{
    return lev && vrm && !strcmp(lev, TIZI_MADNESS_LEV_SHA256) && !strcmp(vrm, TIZI_MADNESS_VRM_SHA256);
}
#endif
