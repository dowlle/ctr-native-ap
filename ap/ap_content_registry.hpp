// Frozen first-profile identities copied from the paired apworld registry.
// Retail rows carry each mode's check codes (Trophy `location`, `ctr`, and the
// Sapphire/Gold/Platinum `relic` codes, #133 PR 3); generic slots carry the
// same per slot in custom_locations, custom_ctr_locations and custom_relic_locations.
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
