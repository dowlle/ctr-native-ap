// Frozen first-profile identities copied from the paired apworld registry.
// Retail rows carry each mode's check codes (Trophy `location`, `ctr`, and the
// Sapphire/Gold/Platinum `relic` codes, #133 PR 3) and their per-track check
// families (#133 PR 3b): the five `podium` rungs in SLOT_ORDER, `wumpa`, the
// C/T/R `letters` and `letter_items`. Generic slots carry the same per slot in
// custom_locations, custom_ctr_locations, custom_relic_locations,
// custom_podium_locations, custom_wumpa_locations, custom_letter_locations and
// custom_letter_items; `wumpa_global` is the one `wumpa: global` check.
// Custom packages are not listed: content plans admit any complete descriptor
// (#133 PR 2) and the bytes are verified on load.
// `named_blocks` is the apworld's named-track table (data/named_tracks.json):
// a custom track whose key is listed must use exactly that block and name, and
// any other key takes a generic slot. Rewrite it with
// tools/sync-content-registry.py --apworld <paired checkout> --write.
// tools/test-content-plan-parity.py verifies all of this against the generator.
static const char kContentRegistryJSON[] = R"CTRREG(
{
  "retail": [
    {
      "id": 0,
      "name": "Dingo Canyon",
      "location": 35011007,
      "ctr": 35012307,
      "relic": [
        35012007,
        35012107,
        35012207
      ],
      "podium": [
        35015128,
        35015129,
        35015130,
        35015131,
        35015023
      ],
      "wumpa": 35016108,
      "letters": [
        35012521,
        35012522,
        35012523
      ],
      "letter_items": [
        35010160,
        35010161,
        35010162
      ]
    },
    {
      "id": 1,
      "name": "Dragon Mines",
      "location": 35011009,
      "ctr": 35012309,
      "relic": [
        35012009,
        35012109,
        35012209
      ],
      "podium": [
        35015136,
        35015137,
        35015138,
        35015139,
        35015029
      ],
      "wumpa": 35016110,
      "letters": [
        35012527,
        35012528,
        35012529
      ],
      "letter_items": [
        35010166,
        35010167,
        35010168
      ]
    },
    {
      "id": 2,
      "name": "Blizzard Bluff",
      "location": 35011008,
      "ctr": 35012308,
      "relic": [
        35012008,
        35012108,
        35012208
      ],
      "podium": [
        35015132,
        35015133,
        35015134,
        35015135,
        35015026
      ],
      "wumpa": 35016109,
      "letters": [
        35012524,
        35012525,
        35012526
      ],
      "letter_items": [
        35010163,
        35010164,
        35010165
      ]
    },
    {
      "id": 3,
      "name": "Crash Cove",
      "location": 35011000,
      "ctr": 35012300,
      "relic": [
        35012000,
        35012100,
        35012200
      ],
      "podium": [
        35015100,
        35015101,
        35015102,
        35015103,
        35015002
      ],
      "wumpa": 35016101,
      "letters": [
        35012500,
        35012501,
        35012502
      ],
      "letter_items": [
        35010139,
        35010140,
        35010141
      ]
    },
    {
      "id": 4,
      "name": "Tiger Temple",
      "location": 35011005,
      "ctr": 35012305,
      "relic": [
        35012005,
        35012105,
        35012205
      ],
      "podium": [
        35015120,
        35015121,
        35015122,
        35015123,
        35015017
      ],
      "wumpa": 35016106,
      "letters": [
        35012515,
        35012516,
        35012517
      ],
      "letter_items": [
        35010154,
        35010155,
        35010156
      ]
    },
    {
      "id": 5,
      "name": "Papu's Pyramid",
      "location": 35011006,
      "ctr": 35012306,
      "relic": [
        35012006,
        35012106,
        35012206
      ],
      "podium": [
        35015124,
        35015125,
        35015126,
        35015127,
        35015020
      ],
      "wumpa": 35016107,
      "letters": [
        35012518,
        35012519,
        35012520
      ],
      "letter_items": [
        35010157,
        35010158,
        35010159
      ]
    },
    {
      "id": 6,
      "name": "Roo's Tubes",
      "location": 35011001,
      "ctr": 35012301,
      "relic": [
        35012001,
        35012101,
        35012201
      ],
      "podium": [
        35015104,
        35015105,
        35015106,
        35015107,
        35015005
      ],
      "wumpa": 35016102,
      "letters": [
        35012503,
        35012504,
        35012505
      ],
      "letter_items": [
        35010142,
        35010143,
        35010144
      ]
    },
    {
      "id": 7,
      "name": "Hot Air Skyway",
      "location": 35011012,
      "ctr": 35012312,
      "relic": [
        35012012,
        35012112,
        35012212
      ],
      "podium": [
        35015148,
        35015149,
        35015150,
        35015151,
        35015038
      ],
      "wumpa": 35016113,
      "letters": [
        35012536,
        35012537,
        35012538
      ],
      "letter_items": [
        35010175,
        35010176,
        35010177
      ]
    },
    {
      "id": 8,
      "name": "Sewer Speedway",
      "location": 35011003,
      "ctr": 35012303,
      "relic": [
        35012003,
        35012103,
        35012203
      ],
      "podium": [
        35015112,
        35015113,
        35015114,
        35015115,
        35015011
      ],
      "wumpa": 35016104,
      "letters": [
        35012509,
        35012510,
        35012511
      ],
      "letter_items": [
        35010148,
        35010149,
        35010150
      ]
    },
    {
      "id": 9,
      "name": "Mystery Caves",
      "location": 35011002,
      "ctr": 35012302,
      "relic": [
        35012002,
        35012102,
        35012202
      ],
      "podium": [
        35015108,
        35015109,
        35015110,
        35015111,
        35015008
      ],
      "wumpa": 35016103,
      "letters": [
        35012506,
        35012507,
        35012508
      ],
      "letter_items": [
        35010145,
        35010146,
        35010147
      ]
    },
    {
      "id": 10,
      "name": "Cortex Castle",
      "location": 35011013,
      "ctr": 35012313,
      "relic": [
        35012013,
        35012113,
        35012213
      ],
      "podium": [
        35015152,
        35015153,
        35015154,
        35015155,
        35015041
      ],
      "wumpa": 35016114,
      "letters": [
        35012539,
        35012540,
        35012541
      ],
      "letter_items": [
        35010178,
        35010179,
        35010180
      ]
    },
    {
      "id": 11,
      "name": "N. Gin Labs",
      "location": 35011014,
      "ctr": 35012314,
      "relic": [
        35012014,
        35012114,
        35012214
      ],
      "podium": [
        35015156,
        35015157,
        35015158,
        35015159,
        35015044
      ],
      "wumpa": 35016115,
      "letters": [
        35012542,
        35012543,
        35012544
      ],
      "letter_items": [
        35010181,
        35010182,
        35010183
      ]
    },
    {
      "id": 12,
      "name": "Polar Pass",
      "location": 35011010,
      "ctr": 35012310,
      "relic": [
        35012010,
        35012110,
        35012210
      ],
      "podium": [
        35015140,
        35015141,
        35015142,
        35015143,
        35015032
      ],
      "wumpa": 35016111,
      "letters": [
        35012530,
        35012531,
        35012532
      ],
      "letter_items": [
        35010169,
        35010170,
        35010171
      ]
    },
    {
      "id": 13,
      "name": "Oxide Station",
      "location": 35011015,
      "ctr": 35012315,
      "relic": [
        35012015,
        35012115,
        35012215
      ],
      "podium": [
        35015160,
        35015161,
        35015162,
        35015163,
        35015047
      ],
      "wumpa": 35016116,
      "letters": [
        35012545,
        35012546,
        35012547
      ],
      "letter_items": [
        35010184,
        35010185,
        35010186
      ]
    },
    {
      "id": 14,
      "name": "Coco Park",
      "location": 35011004,
      "ctr": 35012304,
      "relic": [
        35012004,
        35012104,
        35012204
      ],
      "podium": [
        35015116,
        35015117,
        35015118,
        35015119,
        35015014
      ],
      "wumpa": 35016105,
      "letters": [
        35012512,
        35012513,
        35012514
      ],
      "letter_items": [
        35010151,
        35010152,
        35010153
      ]
    },
    {
      "id": 15,
      "name": "Tiny Arena",
      "location": 35011011,
      "ctr": 35012311,
      "relic": [
        35012011,
        35012111,
        35012211
      ],
      "podium": [
        35015144,
        35015145,
        35015146,
        35015147,
        35015035
      ],
      "wumpa": 35016112,
      "letters": [
        35012533,
        35012534,
        35012535
      ],
      "letter_items": [
        35010172,
        35010173,
        35010174
      ]
    },
    {
      "id": 16,
      "name": "Slide Coliseum",
      "location": 35016200,
      "ctr": 35016210,
      "relic": [
        35012016,
        35012116,
        35012216
      ],
      "podium": [
        35015200,
        35015201,
        35015202,
        35015203,
        35015204
      ],
      "wumpa": 35016117,
      "letters": [
        35012548,
        35012549,
        35012550
      ],
      "letter_items": [
        35010194,
        35010195,
        35010196
      ]
    },
    {
      "id": 17,
      "name": "Turbo Track",
      "location": 35016201,
      "ctr": 35016211,
      "relic": [
        35012017,
        35012117,
        35012217
      ],
      "podium": [
        35015205,
        35015206,
        35015207,
        35015208,
        35015209
      ],
      "wumpa": 35016118,
      "letters": [
        35012551,
        35012552,
        35012553
      ],
      "letter_items": [
        35010197,
        35010198,
        35010199
      ]
    }
  ],
  "pads": [
    {
      "physical": 0,
      "hub": 27,
      "keys": 1
    },
    {
      "physical": 1,
      "hub": 28,
      "keys": 2
    },
    {
      "physical": 2,
      "hub": 28,
      "keys": 2
    },
    {
      "physical": 3,
      "hub": 26,
      "keys": 0
    },
    {
      "physical": 4,
      "hub": 27,
      "keys": 1
    },
    {
      "physical": 5,
      "hub": 27,
      "keys": 1
    },
    {
      "physical": 6,
      "hub": 26,
      "keys": 0
    },
    {
      "physical": 7,
      "hub": 29,
      "keys": 3
    },
    {
      "physical": 8,
      "hub": 26,
      "keys": 0
    },
    {
      "physical": 9,
      "hub": 26,
      "keys": 0
    },
    {
      "physical": 10,
      "hub": 29,
      "keys": 3
    },
    {
      "physical": 11,
      "hub": 29,
      "keys": 3
    },
    {
      "physical": 12,
      "hub": 28,
      "keys": 2
    },
    {
      "physical": 13,
      "hub": 29,
      "keys": 3
    },
    {
      "physical": 14,
      "hub": 27,
      "keys": 1
    },
    {
      "physical": 15,
      "hub": 28,
      "keys": 2
    },
    {
      "physical": 16,
      "hub": 25,
      "keys": 1
    },
    {
      "physical": 17,
      "hub": 25,
      "keys": 1
    },
    {
      "physical": 18,
      "hub": 29,
      "keys": 3
    },
    {
      "physical": 19,
      "hub": 27,
      "keys": 1
    },
    {
      "physical": 21,
      "hub": 26,
      "keys": 0
    },
    {
      "physical": 23,
      "hub": 28,
      "keys": 2
    },
    {
      "physical": 100,
      "hub": 25,
      "keys": 2
    },
    {
      "physical": 101,
      "hub": 25,
      "keys": 2
    },
    {
      "physical": 102,
      "hub": 25,
      "keys": 2
    },
    {
      "physical": 103,
      "hub": 25,
      "keys": 2
    },
    {
      "physical": 104,
      "hub": 25,
      "keys": 2
    }
  ],
  "custom_locations": [
    35016300,
    35016301,
    35016302,
    35016303,
    35016304,
    35016305,
    35016306,
    35016307,
    35016308,
    35016309,
    35016310,
    35016311,
    35016312,
    35016313,
    35016314,
    35016315,
    35016316,
    35016317,
    35016318,
    35016319,
    35016320,
    35016321,
    35016322,
    35016323,
    35016324,
    35016325,
    35016326,
    35016327,
    35016328,
    35016329,
    35016330,
    35016331
  ],
  "custom_ctr_locations": [
    35023000,
    35023001,
    35023002,
    35023003,
    35023004,
    35023005,
    35023006,
    35023007,
    35023008,
    35023009,
    35023010,
    35023011,
    35023012,
    35023013,
    35023014,
    35023015,
    35023016,
    35023017,
    35023018,
    35023019,
    35023020,
    35023021,
    35023022,
    35023023,
    35023024,
    35023025,
    35023026,
    35023027,
    35023028,
    35023029,
    35023030,
    35023031
  ],
  "custom_relic_locations": [
    [
      35022000,
      35022001,
      35022002
    ],
    [
      35022003,
      35022004,
      35022005
    ],
    [
      35022006,
      35022007,
      35022008
    ],
    [
      35022009,
      35022010,
      35022011
    ],
    [
      35022012,
      35022013,
      35022014
    ],
    [
      35022015,
      35022016,
      35022017
    ],
    [
      35022018,
      35022019,
      35022020
    ],
    [
      35022021,
      35022022,
      35022023
    ],
    [
      35022024,
      35022025,
      35022026
    ],
    [
      35022027,
      35022028,
      35022029
    ],
    [
      35022030,
      35022031,
      35022032
    ],
    [
      35022033,
      35022034,
      35022035
    ],
    [
      35022036,
      35022037,
      35022038
    ],
    [
      35022039,
      35022040,
      35022041
    ],
    [
      35022042,
      35022043,
      35022044
    ],
    [
      35022045,
      35022046,
      35022047
    ],
    [
      35022048,
      35022049,
      35022050
    ],
    [
      35022051,
      35022052,
      35022053
    ],
    [
      35022054,
      35022055,
      35022056
    ],
    [
      35022057,
      35022058,
      35022059
    ],
    [
      35022060,
      35022061,
      35022062
    ],
    [
      35022063,
      35022064,
      35022065
    ],
    [
      35022066,
      35022067,
      35022068
    ],
    [
      35022069,
      35022070,
      35022071
    ],
    [
      35022072,
      35022073,
      35022074
    ],
    [
      35022075,
      35022076,
      35022077
    ],
    [
      35022078,
      35022079,
      35022080
    ],
    [
      35022081,
      35022082,
      35022083
    ],
    [
      35022084,
      35022085,
      35022086
    ],
    [
      35022087,
      35022088,
      35022089
    ],
    [
      35022090,
      35022091,
      35022092
    ],
    [
      35022093,
      35022094,
      35022095
    ]
  ],
  "custom_podium_locations": [
    [
      35016400,
      35016401,
      35016402,
      35016403,
      35016404
    ],
    [
      35016405,
      35016406,
      35016407,
      35016408,
      35016409
    ],
    [
      35016410,
      35016411,
      35016412,
      35016413,
      35016414
    ],
    [
      35016415,
      35016416,
      35016417,
      35016418,
      35016419
    ],
    [
      35016420,
      35016421,
      35016422,
      35016423,
      35016424
    ],
    [
      35016425,
      35016426,
      35016427,
      35016428,
      35016429
    ],
    [
      35016430,
      35016431,
      35016432,
      35016433,
      35016434
    ],
    [
      35016435,
      35016436,
      35016437,
      35016438,
      35016439
    ],
    [
      35016440,
      35016441,
      35016442,
      35016443,
      35016444
    ],
    [
      35016445,
      35016446,
      35016447,
      35016448,
      35016449
    ],
    [
      35016450,
      35016451,
      35016452,
      35016453,
      35016454
    ],
    [
      35016455,
      35016456,
      35016457,
      35016458,
      35016459
    ],
    [
      35016460,
      35016461,
      35016462,
      35016463,
      35016464
    ],
    [
      35016465,
      35016466,
      35016467,
      35016468,
      35016469
    ],
    [
      35016470,
      35016471,
      35016472,
      35016473,
      35016474
    ],
    [
      35016475,
      35016476,
      35016477,
      35016478,
      35016479
    ],
    [
      35016480,
      35016481,
      35016482,
      35016483,
      35016484
    ],
    [
      35016485,
      35016486,
      35016487,
      35016488,
      35016489
    ],
    [
      35016490,
      35016491,
      35016492,
      35016493,
      35016494
    ],
    [
      35016495,
      35016496,
      35016497,
      35016498,
      35016499
    ],
    [
      35016500,
      35016501,
      35016502,
      35016503,
      35016504
    ],
    [
      35016505,
      35016506,
      35016507,
      35016508,
      35016509
    ],
    [
      35016510,
      35016511,
      35016512,
      35016513,
      35016514
    ],
    [
      35016515,
      35016516,
      35016517,
      35016518,
      35016519
    ],
    [
      35016520,
      35016521,
      35016522,
      35016523,
      35016524
    ],
    [
      35016525,
      35016526,
      35016527,
      35016528,
      35016529
    ],
    [
      35016530,
      35016531,
      35016532,
      35016533,
      35016534
    ],
    [
      35016535,
      35016536,
      35016537,
      35016538,
      35016539
    ],
    [
      35016540,
      35016541,
      35016542,
      35016543,
      35016544
    ],
    [
      35016545,
      35016546,
      35016547,
      35016548,
      35016549
    ],
    [
      35016550,
      35016551,
      35016552,
      35016553,
      35016554
    ],
    [
      35016555,
      35016556,
      35016557,
      35016558,
      35016559
    ]
  ],
  "custom_wumpa_locations": [
    35016600,
    35016601,
    35016602,
    35016603,
    35016604,
    35016605,
    35016606,
    35016607,
    35016608,
    35016609,
    35016610,
    35016611,
    35016612,
    35016613,
    35016614,
    35016615,
    35016616,
    35016617,
    35016618,
    35016619,
    35016620,
    35016621,
    35016622,
    35016623,
    35016624,
    35016625,
    35016626,
    35016627,
    35016628,
    35016629,
    35016630,
    35016631
  ],
  "custom_letter_locations": [
    [
      35020000,
      35020001,
      35020002
    ],
    [
      35020003,
      35020004,
      35020005
    ],
    [
      35020006,
      35020007,
      35020008
    ],
    [
      35020009,
      35020010,
      35020011
    ],
    [
      35020012,
      35020013,
      35020014
    ],
    [
      35020015,
      35020016,
      35020017
    ],
    [
      35020018,
      35020019,
      35020020
    ],
    [
      35020021,
      35020022,
      35020023
    ],
    [
      35020024,
      35020025,
      35020026
    ],
    [
      35020027,
      35020028,
      35020029
    ],
    [
      35020030,
      35020031,
      35020032
    ],
    [
      35020033,
      35020034,
      35020035
    ],
    [
      35020036,
      35020037,
      35020038
    ],
    [
      35020039,
      35020040,
      35020041
    ],
    [
      35020042,
      35020043,
      35020044
    ],
    [
      35020045,
      35020046,
      35020047
    ],
    [
      35020048,
      35020049,
      35020050
    ],
    [
      35020051,
      35020052,
      35020053
    ],
    [
      35020054,
      35020055,
      35020056
    ],
    [
      35020057,
      35020058,
      35020059
    ],
    [
      35020060,
      35020061,
      35020062
    ],
    [
      35020063,
      35020064,
      35020065
    ],
    [
      35020066,
      35020067,
      35020068
    ],
    [
      35020069,
      35020070,
      35020071
    ],
    [
      35020072,
      35020073,
      35020074
    ],
    [
      35020075,
      35020076,
      35020077
    ],
    [
      35020078,
      35020079,
      35020080
    ],
    [
      35020081,
      35020082,
      35020083
    ],
    [
      35020084,
      35020085,
      35020086
    ],
    [
      35020087,
      35020088,
      35020089
    ],
    [
      35020090,
      35020091,
      35020092
    ],
    [
      35020093,
      35020094,
      35020095
    ]
  ],
  "custom_letter_items": [
    [
      35021000,
      35021001,
      35021002
    ],
    [
      35021003,
      35021004,
      35021005
    ],
    [
      35021006,
      35021007,
      35021008
    ],
    [
      35021009,
      35021010,
      35021011
    ],
    [
      35021012,
      35021013,
      35021014
    ],
    [
      35021015,
      35021016,
      35021017
    ],
    [
      35021018,
      35021019,
      35021020
    ],
    [
      35021021,
      35021022,
      35021023
    ],
    [
      35021024,
      35021025,
      35021026
    ],
    [
      35021027,
      35021028,
      35021029
    ],
    [
      35021030,
      35021031,
      35021032
    ],
    [
      35021033,
      35021034,
      35021035
    ],
    [
      35021036,
      35021037,
      35021038
    ],
    [
      35021039,
      35021040,
      35021041
    ],
    [
      35021042,
      35021043,
      35021044
    ],
    [
      35021045,
      35021046,
      35021047
    ],
    [
      35021048,
      35021049,
      35021050
    ],
    [
      35021051,
      35021052,
      35021053
    ],
    [
      35021054,
      35021055,
      35021056
    ],
    [
      35021057,
      35021058,
      35021059
    ],
    [
      35021060,
      35021061,
      35021062
    ],
    [
      35021063,
      35021064,
      35021065
    ],
    [
      35021066,
      35021067,
      35021068
    ],
    [
      35021069,
      35021070,
      35021071
    ],
    [
      35021072,
      35021073,
      35021074
    ],
    [
      35021075,
      35021076,
      35021077
    ],
    [
      35021078,
      35021079,
      35021080
    ],
    [
      35021081,
      35021082,
      35021083
    ],
    [
      35021084,
      35021085,
      35021086
    ],
    [
      35021087,
      35021088,
      35021089
    ],
    [
      35021090,
      35021091,
      35021092
    ],
    [
      35021093,
      35021094,
      35021095
    ]
  ],
  "wumpa_global": 35016100,
  "named_blocks": [
    {
      "key": "acid-lake",
      "block": 0,
      "name": "Acid Lake"
    },
    {
      "key": "ancient-lake",
      "block": 1,
      "name": "Ancient Lake"
    },
    {
      "key": "android-alley-n-gage",
      "block": 2,
      "name": "Android Alley N-Gage"
    },
    {
      "key": "arabian-heights-day",
      "block": 3,
      "name": "Arabian Heights Day"
    },
    {
      "key": "arabian-heights-night",
      "block": 4,
      "name": "Arabian Heights Night"
    },
    {
      "key": "arcane-garden",
      "block": 5,
      "name": "Arcane Garden"
    },
    {
      "key": "arctic-alleyway",
      "block": 6,
      "name": "Arctic Alleyway"
    },
    {
      "key": "assembly-lane-n-gage",
      "block": 7,
      "name": "Assembly Lane N-Gage"
    },
    {
      "key": "baby-t-park",
      "block": 8,
      "name": "Baby T Park"
    },
    {
      "key": "big-forest",
      "block": 9,
      "name": "Big Forest"
    },
    {
      "key": "bowser-castle-1",
      "block": 10,
      "name": "Bowser Castle 1"
    },
    {
      "key": "breeze-harbor",
      "block": 11,
      "name": "Breeze Harbor"
    },
    {
      "key": "chasqui-rally",
      "block": 12,
      "name": "Chasqui Rally"
    },
    {
      "key": "choco-island-2",
      "block": 13,
      "name": "Choco Island 2"
    },
    {
      "key": "coco-park-2025",
      "block": 15,
      "name": "Coco Park 2025"
    },
    {
      "key": "country-speedway",
      "block": 17,
      "name": "Country Speedway"
    },
    {
      "key": "crystal-caves",
      "block": 18,
      "name": "Crystal Caves"
    },
    {
      "key": "cubic-circuit",
      "block": 19,
      "name": "Cubic Circuit"
    },
    {
      "key": "cursed-trial-roulette",
      "block": 20,
      "name": "Cursed Trial Roulette"
    },
    {
      "key": "dark-ruins",
      "block": 21,
      "name": "Dark Ruins"
    },
    {
      "key": "deep-sea-driving-n-gage",
      "block": 22,
      "name": "Deep Sea Driving N-Gage"
    },
    {
      "key": "diwali",
      "block": 23,
      "name": "Diwali"
    },
    {
      "key": "diwali-no-usf",
      "block": 24,
      "name": "Diwali (no USF)"
    },
    {
      "key": "dk-pass",
      "block": 25,
      "name": "DK Pass"
    },
    {
      "key": "draculas-castle",
      "block": 26,
      "name": "Draculas Castle"
    },
    {
      "key": "dreamy-heights",
      "block": 27,
      "name": "Dreamy Heights"
    },
    {
      "key": "drive-thru-danger",
      "block": 28,
      "name": "Drive Thru Danger"
    },
    {
      "key": "ds-rainbow-road",
      "block": 29,
      "name": "DS Rainbow Road"
    },
    {
      "key": "dust-keepers",
      "block": 30,
      "name": "Dust Keepers"
    },
    {
      "key": "frozen-depths",
      "block": 31,
      "name": "Frozen Depths"
    },
    {
      "key": "garden-speedway",
      "block": 32,
      "name": "Garden Speedway"
    },
    {
      "key": "glacier-lagoon",
      "block": 33,
      "name": "Glacier Lagoon"
    },
    {
      "key": "glazed-dessert",
      "block": 34,
      "name": "Glazed Dessert"
    },
    {
      "key": "green-hill-raceway",
      "block": 35,
      "name": "Green Hill Raceway"
    },
    {
      "key": "hangtime-harbor",
      "block": 36,
      "name": "Hangtime Harbor"
    },
    {
      "key": "hasty-hedges",
      "block": 37,
      "name": "Hasty Hedges"
    },
    {
      "key": "haven-stadium",
      "block": 38,
      "name": "Haven Stadium"
    },
    {
      "key": "haven-stadium-reverse",
      "block": 39,
      "name": "Haven Stadium - Reverse"
    },
    {
      "key": "holiday-hills",
      "block": 40,
      "name": "Holiday Hills"
    },
    {
      "key": "inferno-island",
      "block": 41,
      "name": "Inferno Island"
    },
    {
      "key": "jungle-boogie-n-gage",
      "block": 42,
      "name": "Jungle Boogie N-Gage"
    },
    {
      "key": "kong-dungeon",
      "block": 43,
      "name": "Kong Dungeon"
    },
    {
      "key": "lava-lane",
      "block": 44,
      "name": "Lava Lane"
    },
    {
      "key": "lights-out",
      "block": 45,
      "name": "Lights Out"
    },
    {
      "key": "mario-circuit-1",
      "block": 46,
      "name": "Mario Circuit 1"
    },
    {
      "key": "mario-circuit-2",
      "block": 47,
      "name": "Mario Circuit 2"
    },
    {
      "key": "mario-raceway-n64",
      "block": 48,
      "name": "Mario Raceway N64"
    },
    {
      "key": "matsuri",
      "block": 49,
      "name": "Matsuri"
    },
    {
      "key": "namek",
      "block": 50,
      "name": "Namek"
    },
    {
      "key": "neon-paradise",
      "block": 51,
      "name": "Neon Paradise"
    },
    {
      "key": "neonstadion",
      "block": 52,
      "name": "Neonstadion"
    },
    {
      "key": "north-pole",
      "block": 53,
      "name": "North Pole"
    },
    {
      "key": "out-of-time-n-gage",
      "block": 54,
      "name": "Out Of Time N-Gage"
    },
    {
      "key": "papu-cave",
      "block": 55,
      "name": "Papu Cave"
    },
    {
      "key": "papu-s-sg-temple",
      "block": 56,
      "name": "Papu's SG Temple"
    },
    {
      "key": "pepsi-city",
      "block": 57,
      "name": "Pepsi City"
    },
    {
      "key": "redrock-ravine",
      "block": 58,
      "name": "Redrock Ravine"
    },
    {
      "key": "royal-raceway",
      "block": 59,
      "name": "Royal Raceway"
    },
    {
      "key": "sampa-run",
      "block": 60,
      "name": "Sampa Run"
    },
    {
      "key": "sid-house",
      "block": 61,
      "name": "Sid House"
    },
    {
      "key": "skater-s-retreat",
      "block": 62,
      "name": "Skater's Retreat"
    },
    {
      "key": "skull-cave",
      "block": 63,
      "name": "Skull Cave"
    },
    {
      "key": "snes-bowser-s-castle-2",
      "block": 64,
      "name": "SNES Bowser's Castle 2"
    },
    {
      "key": "snowfall-keep",
      "block": 65,
      "name": "Snowfall Keep"
    },
    {
      "key": "snowy-valley",
      "block": 66,
      "name": "Snowy Valley"
    },
    {
      "key": "space-labs",
      "block": 67,
      "name": "Space Labs"
    },
    {
      "key": "sunken-sprint",
      "block": 69,
      "name": "Sunken Sprint"
    },
    {
      "key": "sunset-vista",
      "block": 70,
      "name": "Sunset Vista"
    },
    {
      "key": "super-snocross-2",
      "block": 71,
      "name": "Super Snocross 2"
    },
    {
      "key": "the-dairy-track",
      "block": 72,
      "name": "The Dairy Track"
    },
    {
      "key": "the-lab",
      "block": 73,
      "name": "The Lab"
    },
    {
      "key": "toasty-terrace",
      "block": 74,
      "name": "Toasty Terrace"
    },
    {
      "key": "tomb-temple",
      "block": 75,
      "name": "Tomb Temple"
    },
    {
      "key": "towers",
      "block": 76,
      "name": "Towers"
    },
    {
      "key": "tribal-trials",
      "block": 77,
      "name": "Tribal Trials"
    },
    {
      "key": "tv-world",
      "block": 78,
      "name": "TV World"
    },
    {
      "key": "ursa-glacier",
      "block": 79,
      "name": "Ursa Glacier"
    },
    {
      "key": "vanilla-lake-2",
      "block": 80,
      "name": "Vanilla Lake 2"
    },
    {
      "key": "wario-stadium",
      "block": 81,
      "name": "Wario Stadium"
    },
    {
      "key": "wumpa-world",
      "block": 82,
      "name": "Wumpa World"
    },
    {
      "key": "yoshi-circuit",
      "block": 83,
      "name": "Yoshi Circuit"
    },
    {
      "key": "yoshi-circuit-legacy",
      "block": 84,
      "name": "Yoshi Circuit Legacy"
    }
  ]
}
)CTRREG";
