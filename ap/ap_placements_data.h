#ifndef AP_PLACEMENTS_DATA_H
#define AP_PLACEMENTS_DATA_H

// AP ITEM BOX PLACEMENTS -- the COMPILED-IN DEFAULT set (#109).
//
// GENERATED, NOT HAND-WRITTEN. Regenerate from the authoritative placement file
// rather than editing a row here; a hand edit that shifts a row also re-points
// every later "Item Box N" name on that track (slot assignment is positional --
// see ap_box_map.h).
//
// PROVENANCE
//   source file : "2026-08-10 154700 -- AP box placements -- FINAL 241 across 18
//                  tracks (logic survey complete).json"
//                 (the authoring session's FINAL export, logic survey complete)
//   format      : ctr-ap-box-placements version 1, written by client v0.1.5
//   sha256      : 5966c2664b28210aad9159eb4ff812b08208ea8829a955a6cc0e3b4076ee0a4f
//   generated   : 2026-08-11, for PR #214 (issue #109 packaging round)
//   units       : pos is LEV InstDef world units (signed 16-bit); rot_y is an engine angle, 0x1000 = one full turn
//
// ROW ORDER IS LOAD-BEARING. The Nth row listed for a level is that level's box
// slot N, so this table is emitted in the source file's own order, NOT sorted by
// level. The FINAL file appends four late placements after TURBO_TRACK; they keep
// their tail position here for exactly that reason.
//
// PER-TRACK COUNTS (241 placements across 18 tracks, none above the frozen
// 15-slot ceiling). tools/test-box-map.c asserts this table against these numbers.
//   level  0  DINGO_CANYON    15
//   level  1  DRAGON_MINES    13
//   level  2  BLIZZARD_BLUFF  14
//   level  3  CRASH_COVE      10
//   level  4  TIGER_TEMPLE    10
//   level  5  PAPU_PYRAMID    12
//   level  6  ROO_TUBES       11
//   level  7  HOT_AIR_SKYWAY  15
//   level  8  SEWER_SPEEDWAY  15
//   level  9  MYSTERY_CAVES   13
//   level 10  CORTEX_CASTLE   14
//   level 11  N_GIN_LABS      15
//   level 12  POLAR_PASS      15
//   level 13  OXIDE_STATION   14
//   level 14  COCO_PARK       10
//   level 15  TINY_ARENA      15
//   level 16  SLIDE_COLISEUM  15
//   level 17  TURBO_TRACK     15
//
// This is the DEFAULT, not the law: an external "ap-box-placements.json" next to the
// executable overrides it wholesale (ap_author.c, AP_AuthorLoad). See SETUP.md.

#ifdef CTR_AP

typedef struct
{
	short level;
	short x, y, z;
	short rotY;
} AP_EmbeddedPlacement;

static const AP_EmbeddedPlacement AP_EMBEDDED_PLACEMENTS[] = {
	{ 3,  -9378,     17,  -5718,  4011}, // CRASH_COVE
	{ 3, -11797,     82,   -579,   691}, // CRASH_COVE
	{ 3, -12974,     15,  -2308,   123}, // CRASH_COVE
	{ 3,  -6191,    589,  11273,  2015}, // CRASH_COVE
	{ 3,  13877,    129,   1908,  1490}, // CRASH_COVE
	{ 3,  14384,    956,  -3142,  2360}, // CRASH_COVE
	{ 3,  12873,   1556,  -5318,  2114}, // CRASH_COVE
	{ 3,  11175,    -20,  -7563,  2815}, // CRASH_COVE
	{ 6,  -3563,    872,   5516,  3494}, // ROO_TUBES
	{ 6,  -3016,    332,   1866,  2045}, // ROO_TUBES
	{ 6,  -6727,   1232,   3110,  3531}, // ROO_TUBES
	{ 6,   -778,  -1431,  -6351,  3131}, // ROO_TUBES
	{ 6,   3770,   -119, -10404,  2425}, // ROO_TUBES
	{ 6,   7023,   1409, -16794,   193}, // ROO_TUBES
	{ 6,    899,   -681,  -9725,   476}, // ROO_TUBES
	{ 6,    855,   -591, -11970,  1144}, // ROO_TUBES
	{ 6,   2308,    337, -17493,  1420}, // ROO_TUBES
	{ 6,   5838,    842, -12907,  3861}, // ROO_TUBES
	{ 4, -12700,   -920,  -3009,  3009}, // TIGER_TEMPLE
	{ 4,  -8501,    -64,   1874,   986}, // TIGER_TEMPLE
	{ 4,   2413,    -52,    203,   724}, // TIGER_TEMPLE
	{ 4,   4177,    -80,    563,   419}, // TIGER_TEMPLE
	{ 4,  14181,   -313,   5385,  1644}, // TIGER_TEMPLE
	{ 4,  14428,   -761,   5299,  2190}, // TIGER_TEMPLE
	{ 4,  11390,  -1067,   9255,  2785}, // TIGER_TEMPLE
	{ 4,   7810,      1,   -632,  3120}, // TIGER_TEMPLE
	{ 4,   1957,    -82,   -946,  2994}, // TIGER_TEMPLE
	{ 4,  -8723,   -606,  -2196,  3857}, // TIGER_TEMPLE
	{14,  -4423,     33,  -4568,  3059}, // COCO_PARK
	{14,  -8309,    865,   3142,   185}, // COCO_PARK
	{14,   4673,     92,   3047,  2511}, // COCO_PARK
	{14,   4755,    746,   7736,  3802}, // COCO_PARK
	{14,   8651,    734,   2430,  3567}, // COCO_PARK
	{14,   4775,     31,  -3326,  3104}, // COCO_PARK
	{14,  -2171,    129,  -3800,    31}, // COCO_PARK
	{14,  -3100,    345,  -8335,  2065}, // COCO_PARK
	{14,  -5943,     10,  -3189,   146}, // COCO_PARK
	{14,  -6165,     96,   7140,  1057}, // COCO_PARK
	{ 9,   8902,    354,   7862,  1058}, // MYSTERY_CAVES
	{ 9,  10801,     61,   3723,  1832}, // MYSTERY_CAVES
	{ 9,   5250,    527,   1610,  2582}, // MYSTERY_CAVES
	{ 9,    802,  -1606,  -7473,  2261}, // MYSTERY_CAVES
	{ 9,  -7447,  -1162, -14272,  3233}, // MYSTERY_CAVES
	{ 9,  -8636,  -1215,  -8545,   173}, // MYSTERY_CAVES
	{ 9,  -6146,  -1261,  -3951,   355}, // MYSTERY_CAVES
	{ 9,  -8469,    -48,   5313,    77}, // MYSTERY_CAVES
	{ 9,  -3148,    441,   9756,   286}, // MYSTERY_CAVES
	{ 9,  -4918,    665,  15323,  2548}, // MYSTERY_CAVES
	{ 9,  -1768,    -48,  16197,   624}, // MYSTERY_CAVES
	{ 9,  12101,     22,   5167,  2819}, // MYSTERY_CAVES
	{ 2,  -3959,    551,   2286,  1961}, // BLIZZARD_BLUFF
	{ 2,  -2257,    732,   6723,  3593}, // BLIZZARD_BLUFF
	{ 2,  -4634,   -529,  10478,  4323}, // BLIZZARD_BLUFF
	{ 2,   8210,    624,   7669,  1996}, // BLIZZARD_BLUFF
	{ 2,   9608,    575,   9360,  3489}, // BLIZZARD_BLUFF
	{ 2,  11157,    497,   4576,  1623}, // BLIZZARD_BLUFF
	{ 2,   9007,    804,    369,  2954}, // BLIZZARD_BLUFF
	{ 2,   -455,    622,   1813,   492}, // BLIZZARD_BLUFF
	{ 2,    528,    905,  -2751,  2557}, // BLIZZARD_BLUFF
	{ 2,   -847,   -408,   5548,  2231}, // BLIZZARD_BLUFF
	{ 2,  -3192,  -1131,  10873,  1175}, // BLIZZARD_BLUFF
	{ 2,  -1451,  -1136,  13578,  2234}, // BLIZZARD_BLUFF
	{ 2,   4920,     83,  12578,  2838}, // BLIZZARD_BLUFF
	{ 2,  12871,    462,   8995,   767}, // BLIZZARD_BLUFF
	{ 8,  -3444,    604,  -9141,  1451}, // SEWER_SPEEDWAY
	{ 8,  -2753,    895, -23808,  1956}, // SEWER_SPEEDWAY
	{ 8,   4428,    801, -26880,   160}, // SEWER_SPEEDWAY
	{ 8,   7305,    523, -18424,    86}, // SEWER_SPEEDWAY
	{ 8,   7817,   -719, -13310,  2727}, // SEWER_SPEEDWAY
	{ 8,   1099,   -748,  -9709,  4170}, // SEWER_SPEEDWAY
	{ 8,   2894,   -392,  -5986,  2701}, // SEWER_SPEEDWAY
	{ 8,   2035,    125,     29,  1000}, // SEWER_SPEEDWAY
	{ 8,  -5499,   -739,  -8139,   925}, // SEWER_SPEEDWAY
	{ 8,  -2900,    -56, -10902,  1892}, // SEWER_SPEEDWAY
	{ 8,   1299,      1, -15639,  4031}, // SEWER_SPEEDWAY
	{ 8,  -1251,      1, -26492,    86}, // SEWER_SPEEDWAY
	{ 8,   2786,      1, -24040,  1243}, // SEWER_SPEEDWAY
	{ 8,   2998,      1, -26416,  2060}, // SEWER_SPEEDWAY
	{ 8,   7288,   -763, -17128,   547}, // SEWER_SPEEDWAY
	{ 0,   3856,   2303,  -4008,   876}, // DINGO_CANYON
	{ 0,   5624,   2303,  -7244,   439}, // DINGO_CANYON
	{ 0,  11612,    868,  -1526,  2351}, // DINGO_CANYON
	{ 0,  22214,    -32,    956,  1096}, // DINGO_CANYON
	{ 0,  20225,    771,   8888,    54}, // DINGO_CANYON
	{ 0,  19577,    815,  10038,  2036}, // DINGO_CANYON
	{ 0,  18124,   1141,   9550,  2541}, // DINGO_CANYON
	{ 0,  10832,   2160,  10369,   427}, // DINGO_CANYON
	{ 0,   6946,   2323,   9593,   993}, // DINGO_CANYON
	{ 0,   3821,   3240,   4331,  2158}, // DINGO_CANYON
	{ 0,    520,   2304,   2852,  1514}, // DINGO_CANYON
	{ 0,   1979,   2318,   -125,  2704}, // DINGO_CANYON
	{ 0,   3340,   2303,   3788,   925}, // DINGO_CANYON
	{ 0,   5323,   2303,  -4642,  -891}, // DINGO_CANYON
	{ 0,   4562,   2304,  -2884,  3206}, // DINGO_CANYON
	{ 5,  -4766,   1390,   2811,  1028}, // PAPU_PYRAMID
	{ 5,  -4500,   2332,    958,  -246}, // PAPU_PYRAMID
	{ 5,  -7737,   2291,    842,  3177}, // PAPU_PYRAMID
	{ 5,  -9852,   2314,   2008,  4248}, // PAPU_PYRAMID
	{ 5,  -8613,   2348,   4331,   831}, // PAPU_PYRAMID
	{ 5,  -1705,   1980,   5894,  1282}, // PAPU_PYRAMID
	{ 5,  -5228,   2310,  -6612,  1877}, // PAPU_PYRAMID
	{ 5,  -7418,   2137,  -4303,  2775}, // PAPU_PYRAMID
	{ 5,  -7283,   2123,  -8452,  1283}, // PAPU_PYRAMID
	{ 5,  -1552,   1067,  -5994,   735}, // PAPU_PYRAMID
	{ 5,  -7441,   2384,  -1491,  1825}, // PAPU_PYRAMID
	{ 5,   -859,   1390,     -4,  1853}, // PAPU_PYRAMID
	{ 1,   8072,    -70,  -5388,  2215}, // DRAGON_MINES
	{ 1,   2086,   -382,  -7216,   552}, // DRAGON_MINES
	{ 1,  -2421,     10, -11986,  3126}, // DRAGON_MINES
	{ 1,  -5246,   -158,  -8834,  3887}, // DRAGON_MINES
	{ 1,  -3280,    -32,  -9752,  2343}, // DRAGON_MINES
	{ 1,  -3932,     -6,  -4604,  4048}, // DRAGON_MINES
	{ 1,  -5285,   -150,  -4967,  2268}, // DRAGON_MINES
	{ 1,  -5373,    111,  -2243,  3525}, // DRAGON_MINES
	{ 1,  -5918,   1403,    448,  2612}, // DRAGON_MINES
	{ 1,  -4245,   1633,    826,   368}, // DRAGON_MINES
	{ 1,  -1222,   1522,   3392,  1126}, // DRAGON_MINES
	{ 1,   3397,    -43,  -3378,   441}, // DRAGON_MINES
	{ 1,   6037,    -49,  -5400,  1950}, // DRAGON_MINES
	{12,  10431,    610,    411,  2086}, // POLAR_PASS
	{12,   4284,    474,  -4254,  2213}, // POLAR_PASS
	{12,   6715,    354, -17333,  1352}, // POLAR_PASS
	{12,  -3740,   1813, -20684,  3471}, // POLAR_PASS
	{12,  -6796,   1247, -12344,  3044}, // POLAR_PASS
	{12,  -3763,   1701, -10195,   852}, // POLAR_PASS
	{12,  -6577,   1872,  -2267,  4399}, // POLAR_PASS
	{12,  -7271,    568,   -458,  2953}, // POLAR_PASS
	{12,   1486,    274,   -810,  1271}, // POLAR_PASS
	{12,   6862,   1051,   1336,    68}, // POLAR_PASS
	{12,  11357,     95,  -3009,  1997}, // POLAR_PASS
	{12,  14609,   1245, -16715,   391}, // POLAR_PASS
	{12,   4889,    525, -14837,  1683}, // POLAR_PASS
	{12,    790,   1790, -24972,  3677}, // POLAR_PASS
	{12,    895,   1790, -23536,  3368}, // POLAR_PASS
	{10,  -7586,      1,  10787,  3643}, // CORTEX_CASTLE
	{10,  -3276,   1535,   2909,   681}, // CORTEX_CASTLE
	{10,  -2776,   1558,  -1550,  1438}, // CORTEX_CASTLE
	{10,   3430,   2365,  -4859,   793}, // CORTEX_CASTLE
	{10,  10048,   3400,  -8140,  2958}, // CORTEX_CASTLE
	{10,   1049,   2687,  -8842,  3585}, // CORTEX_CASTLE
	{10,   -128,   2304,  -9187,  3916}, // CORTEX_CASTLE
	{10,  -1102,   1919,  -8624,   554}, // CORTEX_CASTLE
	{10,  -1606,   1535,  -7542,  1206}, // CORTEX_CASTLE
	{10,  -6627,   2226,  -3685,  3985}, // CORTEX_CASTLE
	{10,  -2753,   3387,     -3,  1073}, // CORTEX_CASTLE
	{10,   4321,      1,   -393,   795}, // CORTEX_CASTLE
	{10,   4524,      1,    399,  2646}, // CORTEX_CASTLE
	{10,  -1390,      1,   5469,  3023}, // CORTEX_CASTLE
	{15,  -7846,    390,  -6907,  2222}, // TINY_ARENA
	{15,   4585,      1,  -1118,  2977}, // TINY_ARENA
	{15,  -4854,    -20,   5888,  2111}, // TINY_ARENA
	{15,   8388,     44,   1692,  2816}, // TINY_ARENA
	{15,   3547,    242,     47,  3162}, // TINY_ARENA
	{15,   -599,    288,    763,  3015}, // TINY_ARENA
	{15,  -5809,     34,   1670,  1408}, // TINY_ARENA
	{15,  -3093,   -309,   3074,  -528}, // TINY_ARENA
	{15,  -4607,    240,   3224,   969}, // TINY_ARENA
	{15,    972,    -25,   5531,  1033}, // TINY_ARENA
	{15,  -3135,     38,    322,  3011}, // TINY_ARENA
	{15, -11981,     15,  -6055,   279}, // TINY_ARENA
	{15,  -4069,    399, -11401,  1079}, // TINY_ARENA
	{15,  -1387,    489, -11478,  1052}, // TINY_ARENA
	{15,   5114,    209,  -9427,  1531}, // TINY_ARENA
	{13,  -3532,   -420,  10578,  1644}, // OXIDE_STATION
	{13,  -2981,    280,   6311,  2150}, // OXIDE_STATION
	{13,   -559,      0,   3341,  1970}, // OXIDE_STATION
	{13,  18128,     37,    458,  1680}, // OXIDE_STATION
	{ 7,   8433,   -268,   4495,  2877}, // HOT_AIR_SKYWAY
	{ 7,   5656,   -664,  -2134,  1857}, // HOT_AIR_SKYWAY
	{ 7,  -3011,    923,  -3471,    76}, // HOT_AIR_SKYWAY
	{ 7,   4372,     84,   -720,  2137}, // HOT_AIR_SKYWAY
	{ 7,   8827,   -389,   6259,  2957}, // HOT_AIR_SKYWAY
	{11,  14723,      1,   1646,   470}, // N_GIN_LABS
	{11,  17443,    503,  16111,     5}, // N_GIN_LABS
	{11,   7426,    767,  23254,  3772}, // N_GIN_LABS
	{11,   6094,    767,  21794,  2661}, // N_GIN_LABS
	{11,   6452,   -906,  22330,   452}, // N_GIN_LABS
	{11,   8910,   -838,  24621,   305}, // N_GIN_LABS
	{11,  11751,   -189,  27796,   815}, // N_GIN_LABS
	{11,  15744,    263,  26395,   493}, // N_GIN_LABS
	{11,  14132,    720,  20701,  2056}, // N_GIN_LABS
	{11,   9229,   -721,   9153,  2145}, // N_GIN_LABS
	{11,   9608,    170,   4406,   928}, // N_GIN_LABS
	{11,  17776,    458,   6829,  4025}, // N_GIN_LABS
	{11,  12854,    420,  19694,  2945}, // N_GIN_LABS
	{11,  -2682,   -768,  19444,  1537}, // N_GIN_LABS
	{11,   2363,   -768,  20378,  3397}, // N_GIN_LABS
	{13,   4640,  -1678,  12337,  4009}, // OXIDE_STATION
	{13,   8169,   -419,  11141,  2332}, // OXIDE_STATION
	{13,  10657,    -98,   -651,  2724}, // OXIDE_STATION
	{13,   5346,    110,   8727,  3618}, // OXIDE_STATION
	{13,   -810,   -324,  17664,  3046}, // OXIDE_STATION
	{13,  11098,    484,  12381,  1038}, // OXIDE_STATION
	{13,  17436,    499,  15873,   -21}, // OXIDE_STATION
	{13,  13411,    623,  17435,  2865}, // OXIDE_STATION
	{13,  10628,    477,  18083,  3574}, // OXIDE_STATION
	{13,  11875,   -430,   6099,  1635}, // OXIDE_STATION
	{16,  -1014,      1,  -9960,  3700}, // SLIDE_COLISEUM
	{16,  -1793,      1,  -6745,  3275}, // SLIDE_COLISEUM
	{16,  -6385,     63,  -5619,   -39}, // SLIDE_COLISEUM
	{16,  -8182,    461,   2797,  4205}, // SLIDE_COLISEUM
	{16,  -4540,      1,   7352,  3809}, // SLIDE_COLISEUM
	{16,   -716,      2,   2137,  3044}, // SLIDE_COLISEUM
	{16,   -806,    310,   -897,  1597}, // SLIDE_COLISEUM
	{16,   7839,     28,  -3236,    51}, // SLIDE_COLISEUM
	{16,   4343,      1,  -6233,  1185}, // SLIDE_COLISEUM
	{16,   5582,     50,  -8647,  2392}, // SLIDE_COLISEUM
	{16,   2272,      1,  -9671,  3108}, // SLIDE_COLISEUM
	{16,  -6766,      1,  -8548,  3487}, // SLIDE_COLISEUM
	{16,  -8991,     25,  -6455,  3972}, // SLIDE_COLISEUM
	{16,  -6235,      1,  -1033,  3273}, // SLIDE_COLISEUM
	{16,  -4060,    395,   5887,  1296}, // SLIDE_COLISEUM
	{ 7,   9966,    349,  -1256,  2902}, // HOT_AIR_SKYWAY
	{ 7,  14644,    549,   4749,  2678}, // HOT_AIR_SKYWAY
	{ 7,   1991,   -144,  -7723,  1880}, // HOT_AIR_SKYWAY
	{ 7,  -2261,    933,  -3268,    52}, // HOT_AIR_SKYWAY
	{ 7,  -8261,   -259,   5806,  1208}, // HOT_AIR_SKYWAY
	{ 7,  -5918,    202,   3038,  1914}, // HOT_AIR_SKYWAY
	{ 7,   -943,    269,    -89,   433}, // HOT_AIR_SKYWAY
	{ 7,   4204,  -1410,  -5590,  2336}, // HOT_AIR_SKYWAY
	{ 7,   8603,   -458,  -8051,  2305}, // HOT_AIR_SKYWAY
	{ 7,     -4,     13,  -8180,  3133}, // HOT_AIR_SKYWAY
	{17,  10275,     25,  -7102,  2379}, // TURBO_TRACK
	{17,   8309,     13,  -9648,  3262}, // TURBO_TRACK
	{17,  -1593,    230,  -9439,  3508}, // TURBO_TRACK
	{17,  -9863,     49,  -9573,  3993}, // TURBO_TRACK
	{17, -10401,     61,   -989,   634}, // TURBO_TRACK
	{17,  -6224,     45,  -6833,  1756}, // TURBO_TRACK
	{17,  -2366,      1,  -1946,  1353}, // TURBO_TRACK
	{17,    -53,      1,  -5402,  3823}, // TURBO_TRACK
	{17,  -1734,    167,   1080,  3868}, // TURBO_TRACK
	{17,  -2773,     30,   7568,   982}, // TURBO_TRACK
	{17,   2953,    159,   7937,  1511}, // TURBO_TRACK
	{17,   2969,    135,   3737,  1224}, // TURBO_TRACK
	{17,   9394,     68,  -1131,  2220}, // TURBO_TRACK
	{17,   8064,     42,  -7782,  3147}, // TURBO_TRACK
	{17, -11030,      1, -10404,  4651}, // TURBO_TRACK
	{ 3,  -3542,    599,   3423,  1963}, // CRASH_COVE
	{ 3,  -5503,    575,   5515,  1431}, // CRASH_COVE
	{ 6,  -5683,   1094,    620,  4077}, // ROO_TUBES
	{ 9,  -7750,    390,   6233,   148}, // MYSTERY_CAVES
};

#define AP_EMBEDDED_PLACEMENT_COUNT \
	((int)(sizeof(AP_EMBEDDED_PLACEMENTS) / sizeof(AP_EMBEDDED_PLACEMENTS[0])))

// Count assertion. The generator wrote both numbers; if a later hand edit adds or
// drops a row without updating this, the build stops here rather than shipping a
// silently re-pointed track (Lessons Learned §4: never let a bound drop data
// quietly). The negative array size is the C99 way to say static_assert.
#define AP_EMBEDDED_PLACEMENT_EXPECTED 241
#define AP_EMBEDDED_PLACEMENT_TRACKS   18

typedef char ap_embedded_placement_count_assert
    [(AP_EMBEDDED_PLACEMENT_COUNT == AP_EMBEDDED_PLACEMENT_EXPECTED) ? 1 : -1];

#endif // CTR_AP
#endif // AP_PLACEMENTS_DATA_H
