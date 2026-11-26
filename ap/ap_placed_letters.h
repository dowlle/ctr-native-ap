#ifndef AP_PLACED_LETTERS_H
#define AP_PLACED_LETTERS_H

#include <string.h>

struct AP_PlacedLetterSet {
    const char *track;
    const char *lev;
    const char *vrm;
    short anchors[3][4]; /* X, Y, Z and yaw in C/N/K slot order */
};

static inline const struct AP_PlacedLetterSet *AP_PlacedLetters_Find(
    const char *track, const char *lev, const char *vrm)
{
    static const struct AP_PlacedLetterSet placements[] = {
        {"jungle-boogie-n-gage",
         "ac968f6ce814ecbe67a7efd6e926f4fa7c59803c56e8f05a194156962ec0f906",
         "73095c1261541884f9046104134c5c0ff2d95b917cfd847cad2cef687b138e0f",
         {{9281, -2431, 4419, 2183}, {-16460, 1085, -1853, 3852}, {-2805, 50, -1256, 2677}}},
        {"assembly-lane-n-gage",
         "b646db4e5564827ca228f08c3f9159593790203ab57b5bf8244bcc909353edae",
         "2620d0f6b92d23497962ac1dcfa0dd6f283d44057a67918f72aa85db943aa723",
         {{11171, -62, -1236, 3656}, {2377, 89, -2830, 1003}, {7169, -62, -21034, 3061}}},
        {"out-of-time-n-gage",
         "275de2169ef44626e5f9160c4fbdccf7bedacec85ad98bf40d7dbb4120a0d981",
         "3ba0c84b27ab5fcc0a395456561b7df900f7981652c51e7259cb1ba47a3c7b8e",
         {{8636, -4, -5669, 3507}, {9828, -336, 10906, 3725}, {2892, 549, -3008, 0}}},
    };
    for (unsigned i = 0; i < sizeof placements / sizeof *placements; ++i) {
        const struct AP_PlacedLetterSet *set = &placements[i];
        if ((!track || !strcmp(track, set->track)) && !strcmp(lev, set->lev) && !strcmp(vrm, set->vrm))
            return set;
    }
    return NULL;
}
#endif
