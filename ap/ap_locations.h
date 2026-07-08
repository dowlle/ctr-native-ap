#ifndef AP_LOCATIONS_H
#define AP_LOCATIONS_H

// GENERATED -- do not edit by hand.
// Source join: CTR-ModSDK AdvProgress bit layout (namespace_Memcard.h)
//   x icebound apworld addresses.py LOCATION_BITS (bit -> name)
//   x icebound apworld data/locations.json (name -> AP code).
// AdvProgress.rewards[] is read in-process; bit_index = word*32 + bit.
#ifdef CTR_AP

typedef struct { int bit_index; long location_code; } AP_LocationEntry;

// 101 checkable adventure locations.
static const AP_LocationEntry AP_LOCATION_TABLE[] = {
	{   6, 35011007 }, // Dingo Canyon: Trophy Race
	{   7, 35011009 }, // Dragon Mines: Trophy Race
	{   8, 35011008 }, // Blizzard Bluff: Trophy Race
	{   9, 35011000 }, // Crash Cove: Trophy Race
	{  10, 35011005 }, // Tiger Temple: Trophy Race
	{  11, 35011006 }, // Papu's Pyramid: Trophy Race
	{  12, 35011001 }, // Roo's Tubes: Trophy Race
	{  13, 35011012 }, // Hot Air Skyway: Trophy Race
	{  14, 35011003 }, // Sewer Speedway: Trophy Race
	{  15, 35011002 }, // Mystery Caves: Trophy Race
	{  16, 35011013 }, // Cortex Castle: Trophy Race
	{  17, 35011014 }, // N. Gin Labs: Trophy Race
	{  18, 35011010 }, // Polar Pass: Trophy Race
	{  19, 35011015 }, // Oxide Station: Trophy Race
	{  20, 35011004 }, // Coco Park: Trophy Race
	{  21, 35011011 }, // Tiny Arena: Trophy Race
	{  22, 35012007 }, // Dingo Canyon: Sapphire Time Trial
	{  23, 35012009 }, // Dragon Mines: Sapphire Time Trial
	{  24, 35012008 }, // Blizzard Bluff: Sapphire Time Trial
	{  25, 35012000 }, // Crash Cove: Sapphire Time Trial
	{  26, 35012005 }, // Tiger Temple: Sapphire Time Trial
	{  27, 35012006 }, // Papu's Pyramid: Sapphire Time Trial
	{  28, 35012001 }, // Roo's Tubes: Sapphire Time Trial
	{  29, 35012012 }, // Hot Air Skyway: Sapphire Time Trial
	{  30, 35012003 }, // Sewer Speedway: Sapphire Time Trial
	{  31, 35012002 }, // Mystery Caves: Sapphire Time Trial
	{  32, 35012013 }, // Cortex Castle: Sapphire Time Trial
	{  33, 35012014 }, // N. Gin Labs: Sapphire Time Trial
	{  34, 35012010 }, // Polar Pass: Sapphire Time Trial
	{  35, 35012015 }, // Oxide Station: Sapphire Time Trial
	{  36, 35012004 }, // Coco Park: Sapphire Time Trial
	{  37, 35012011 }, // Tiny Arena: Sapphire Time Trial
	{  38, 35012016 }, // Slide Coliseum: Sapphire Time Trial
	{  39, 35012017 }, // Turbo Track: Sapphire Time Trial
	{  40, 35012107 }, // Dingo Canyon: Gold Time Trial
	{  41, 35012109 }, // Dragon Mines: Gold Time Trial
	{  42, 35012108 }, // Blizzard Bluff: Gold Time Trial
	{  43, 35012100 }, // Crash Cove: Gold Time Trial
	{  44, 35012105 }, // Tiger Temple: Gold Time Trial
	{  45, 35012106 }, // Papu's Pyramid: Gold Time Trial
	{  46, 35012101 }, // Roo's Tubes: Gold Time Trial
	{  47, 35012112 }, // Hot Air Skyway: Gold Time Trial
	{  48, 35012103 }, // Sewer Speedway: Gold Time Trial
	{  49, 35012102 }, // Mystery Caves: Gold Time Trial
	{  50, 35012113 }, // Cortex Castle: Gold Time Trial
	{  51, 35012114 }, // N. Gin Labs: Gold Time Trial
	{  52, 35012110 }, // Polar Pass: Gold Time Trial
	{  53, 35012115 }, // Oxide Station: Gold Time Trial
	{  54, 35012104 }, // Coco Park: Gold Time Trial
	{  55, 35012111 }, // Tiny Arena: Gold Time Trial
	{  56, 35012116 }, // Slide Coliseum: Gold Time Trial
	{  57, 35012117 }, // Turbo Track: Gold Time Trial
	{  58, 35012207 }, // Dingo Canyon: Platinum Time Trial
	{  59, 35012209 }, // Dragon Mines: Platinum Time Trial
	{  60, 35012208 }, // Blizzard Bluff: Platinum Time Trial
	{  61, 35012200 }, // Crash Cove: Platinum Time Trial
	{  62, 35012205 }, // Tiger Temple: Platinum Time Trial
	{  63, 35012206 }, // Papu's Pyramid: Platinum Time Trial
	{  64, 35012201 }, // Roo's Tubes: Platinum Time Trial
	{  65, 35012212 }, // Hot Air Skyway: Platinum Time Trial
	{  66, 35012203 }, // Sewer Speedway: Platinum Time Trial
	{  67, 35012202 }, // Mystery Caves: Platinum Time Trial
	{  68, 35012213 }, // Cortex Castle: Platinum Time Trial
	{  69, 35012214 }, // N. Gin Labs: Platinum Time Trial
	{  70, 35012210 }, // Polar Pass: Platinum Time Trial
	{  71, 35012215 }, // Oxide Station: Platinum Time Trial
	{  72, 35012204 }, // Coco Park: Platinum Time Trial
	{  73, 35012211 }, // Tiny Arena: Platinum Time Trial
	{  74, 35012216 }, // Slide Coliseum: Platinum Time Trial
	{  75, 35012217 }, // Turbo Track: Platinum Time Trial
	{  76, 35012307 }, // Dingo Canyon: CTR Token Challenge
	{  77, 35012309 }, // Dragon Mines: CTR Token Challenge
	{  78, 35012308 }, // Blizzard Bluff: CTR Token Challenge
	{  79, 35012300 }, // Crash Cove: CTR Token Challenge
	{  80, 35012305 }, // Tiger Temple: CTR Token Challenge
	{  81, 35012306 }, // Papu's Pyramid: CTR Token Challenge
	{  82, 35012301 }, // Roo's Tubes: CTR Token Challenge
	{  83, 35012312 }, // Hot Air Skyway: CTR Token Challenge
	{  84, 35012303 }, // Sewer Speedway: CTR Token Challenge
	{  85, 35012302 }, // Mystery Caves: CTR Token Challenge
	{  86, 35012313 }, // Cortex Castle: CTR Token Challenge
	{  87, 35012314 }, // N. Gin Labs: CTR Token Challenge
	{  88, 35012310 }, // Polar Pass: CTR Token Challenge
	{  89, 35012315 }, // Oxide Station: CTR Token Challenge
	{  90, 35012304 }, // Coco Park: CTR Token Challenge
	{  91, 35012311 }, // Tiny Arena: CTR Token Challenge
	{  94, 35011100 }, // Ripper Roo Garage: Boss Race
	{  95, 35011101 }, // Papu Papu Garage: Boss Race
	{  96, 35011102 }, // Komodo Joe Garage: Boss Race
	{  97, 35011103 }, // Pinstripe Garage: Boss Race
	{ 106, 35011200 }, // Red Gem Cup: Gem
	{ 107, 35011201 }, // Green Gem Cup: Gem
	{ 108, 35011202 }, // Blue Gem Cup: Gem
	{ 109, 35011203 }, // Yellow Gem Cup: Gem
	{ 110, 35011204 }, // Purple Gem Cup: Gem
	{ 111, 35013000 }, // Skull Rock: Crystal Bonus Round
	{ 112, 35013001 }, // Rampage Ruins: Crystal Bonus Round
	{ 113, 35013002 }, // Rocky Road: Crystal Bonus Round
	{ 114, 35013003 }, // Nitro Court: Crystal Bonus Round
	{ 115, 35011104 }, // N. Oxide's Challenge: Boss Race (goals 0/1)
	{ 116, 35011105 }, // N. Oxide's Final Challenge: Boss Race (goal 1)
};
#define AP_LOCATION_TABLE_LEN 101

// Oxide-beat bits. As of slot_data schema 4 (goal-rework v2) these two are REAL
// checkable locations (promoted in locations.json to 35011104 / 35011105 and now
// in AP_LOCATION_TABLE above), sent from the Oxide-beat path
// (AP_NotifyGoal -> AP_NotifyAdvReward). The win itself stays a separate game
// EVENT (ap_oxide_*_beaten) that AP_EvaluateGoal reads, mirroring the apworld's
// companion-event separation. The #defines below name the bits for that send +
// for the goal-0/1 completion flags.
// BEAT_OXIDE_FIRST = 0x73 (bit 115), BEAT_OXIDE_SECOND = 0x74 (bit 116).
#define AP_GOAL_BIT_OXIDE_FIRST  115
#define AP_GOAL_BIT_OXIDE_SECOND 116

#endif // CTR_AP
#endif // AP_LOCATIONS_H
