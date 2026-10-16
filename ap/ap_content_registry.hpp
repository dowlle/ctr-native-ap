// Frozen first-profile identities copied from the paired apworld registry.
// Custom packages are not listed: content plans admit any complete descriptor
// (#133 PR 2) and the bytes are verified on load.
// tools/test-content-plan-parity.py verifies these against the generator.
static const char kContentRegistryJSON[] = R"CTRREG(
{
  "retail": [
    {
      "id": 0,
      "name": "Dingo Canyon",
      "location": 35011007
    },
    {
      "id": 1,
      "name": "Dragon Mines",
      "location": 35011009
    },
    {
      "id": 2,
      "name": "Blizzard Bluff",
      "location": 35011008
    },
    {
      "id": 3,
      "name": "Crash Cove",
      "location": 35011000
    },
    {
      "id": 4,
      "name": "Tiger Temple",
      "location": 35011005
    },
    {
      "id": 5,
      "name": "Papu's Pyramid",
      "location": 35011006
    },
    {
      "id": 6,
      "name": "Roo's Tubes",
      "location": 35011001
    },
    {
      "id": 7,
      "name": "Hot Air Skyway",
      "location": 35011012
    },
    {
      "id": 8,
      "name": "Sewer Speedway",
      "location": 35011003
    },
    {
      "id": 9,
      "name": "Mystery Caves",
      "location": 35011002
    },
    {
      "id": 10,
      "name": "Cortex Castle",
      "location": 35011013
    },
    {
      "id": 11,
      "name": "N. Gin Labs",
      "location": 35011014
    },
    {
      "id": 12,
      "name": "Polar Pass",
      "location": 35011010
    },
    {
      "id": 13,
      "name": "Oxide Station",
      "location": 35011015
    },
    {
      "id": 14,
      "name": "Coco Park",
      "location": 35011004
    },
    {
      "id": 15,
      "name": "Tiny Arena",
      "location": 35011011
    },
    {
      "id": 16,
      "name": "Slide Coliseum",
      "location": 35016200
    },
    {
      "id": 17,
      "name": "Turbo Track",
      "location": 35016201
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
  ]
}
)CTRREG";
