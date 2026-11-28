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
        {"android-alley-n-gage",
         "a06bbb0a6c171b702166271b1024830b7a12b3c1faf43477b1e28f218585665a",
         "f13d8a876620f92bc69407f5de3d4b89eee93f9cbda0abc862529608f353c042",
         {{1899, 366, 14310, 2375}, {9027, 335, 22310, 1024}, {-5464, 195, 14482, 2167}}},
        {"deep-sea-driving-n-gage",
         "9a94ba597c53d5ca62f9d16a3622f713b1455ab48e9b93a7e22d5dc0b043fed8",
         "285e03d7b414b98b73711bb08a8e96d1f6a1f1bbbb3e95a418a92c3aee8f88c8",
         {{13746, 324, 1451, 3875}, {11124, 1344, -17730, 3461}, {474, 245, -11779, 2318}}},
        {"jungle-boogie-n-gage",
         "ac968f6ce814ecbe67a7efd6e926f4fa7c59803c56e8f05a194156962ec0f906",
         "73095c1261541884f9046104134c5c0ff2d95b917cfd847cad2cef687b138e0f",
         {{9281, -2431, 4419, 2183}, {-16460, 1009, -1853, 3852}, {-2805, 50, -1256, 2677}}},
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
