#ifndef AP_RELIC_PERFECT_H
#define AP_RELIC_PERFECT_H

// Relic Race perfect checks (issue #49): breaking every time crate in a Relic
// Race is a location check. Freestanding and header-only so the host harness
// (tools/test-relic-perfect.c) exercises the exact decisions production uses.
//
// THE SIGNAL. Retail already computes it: RR_EndEvent_UnlockAward (game/223.c)
// compares driver->numTimeCrates with gGT->timeCratesInLEV to take ten seconds
// off the race time. The producer reads the same two counters at the same
// point, so "perfect" means exactly what the retail ten-second bonus means. It
// is independent of the relic tier reached (or none), of an already-owned
// relic and of the race time.
//
// THE IDENTITY. Retail relic tracks are keyed by engine LevelID 0..17 (the 16
// trophy tracks plus Slide Coliseum 16 and Turbo Track 17). Each owns one
// frozen code, 35012400 + its index in the Sapphire Time Trial order, minted in
// the 0.2.0 datapackage. The seed's `relic_perfect_checks` block says which of
// them exist this seed; the parser (ap_seedcfg.cpp) refuses any row whose code
// is not the frozen code for its LevelID.
//
// NOT A RETAIL IDENTITY. A Cortex Vortex pad race borrows LevelID 13 and a
// custom package borrows its host LevelID; neither may send the borrowed retail
// track's code. Cortex Vortex's perfect (35026005) is reserved but not minted,
// so it resolves to nothing. A custom package resolves through its own seed
// slot (`customCode`), which is -1 until the reserved custom perfect family is
// minted and carried on the wire (see AP_RelicPerfectCustomCode in ap_hooks.c).

#define AP_RELIC_PERFECT_TRACK_COUNT 18
#define AP_RELIC_PERFECT_CODE_BASE   35012400L

// Frozen code for an engine LevelID, or -1 outside 0..17. Indexed by LevelID;
// the values are the Sapphire-order indices of the apworld's RELIC_TRACKS.
static inline long AP_RelicPerfectExpectedCode(int levelID)
{
	static const int order[AP_RELIC_PERFECT_TRACK_COUNT] = {
		7,  // 0  Dingo Canyon
		9,  // 1  Dragon Mines
		8,  // 2  Blizzard Bluff
		0,  // 3  Crash Cove
		5,  // 4  Tiger Temple
		6,  // 5  Papu's Pyramid
		1,  // 6  Roo's Tubes
		12, // 7  Hot Air Skyway
		3,  // 8  Sewer Speedway
		2,  // 9  Mystery Caves
		13, // 10 Cortex Castle
		14, // 11 N. Gin Labs
		10, // 12 Polar Pass
		15, // 13 Oxide Station
		4,  // 14 Coco Park
		11, // 15 Tiny Arena
		16, // 16 Slide Coliseum
		17, // 17 Turbo Track
	};
	if (levelID < 0 || levelID >= AP_RELIC_PERFECT_TRACK_COUNT)
		return -1;
	return AP_RELIC_PERFECT_CODE_BASE + order[levelID];
}

// Canonical wire key -> LevelID, or -1. Only "0".."17" exactly: no sign, no
// leading zero, no whitespace, no trailing text, so "3", "03" and "3junk" can
// never alias one slot.
static inline int AP_RelicPerfectKeyLevel(const char *key)
{
	int value = 0;
	int i;

	if (key == 0 || key[0] == '\0')
		return -1;
	if (key[0] == '0')
		return key[1] == '\0' ? 0 : -1;
	for (i = 0; key[i] != '\0'; i++)
	{
		if (key[i] < '0' || key[i] > '9' || i >= 2)
			return -1;
		value = value * 10 + (key[i] - '0');
	}
	return value < AP_RELIC_PERFECT_TRACK_COUNT ? value : -1;
}

// Everything the race-end producer knows. Filled by AP_NotifyRelicPerfect from
// the live GameTracker; the harness fills it by hand.
typedef struct
{
	const long *retail;  // AP_RELIC_PERFECT_TRACK_COUNT codes, -1 = absent this seed
	int levelID;         // gGT->levelID at the relic race end
	int broken;          // driver->numTimeCrates
	int total;           // gGT->timeCratesInLEV
	int cortexActive;    // Cortex Vortex pad track is on screen (borrowed 13)
	int customServing;   // a custom package's bytes are this load (borrowed host)
	long customCode;     // that package's own perfect code, -1 = none
} AP_RelicPerfectFacts;

// The one decision: which code (if any) this relic race end sends.
// -1 = nothing. The caller has already applied the #286 forced-loss guard and
// the per-seed "is this code a live location" check lives in the emitter.
static inline long AP_RelicPerfectResolvePure(const AP_RelicPerfectFacts *f)
{
	if (f == 0)
		return -1;
	// A level without time crates has no perfect: 0 == 0 is not an achievement.
	if (f->total <= 0 || f->broken != f->total)
		return -1;
	// Borrowed LevelIDs never pay the retail track's code.
	if (f->cortexActive)
		return -1; // Cortex Vortex's own perfect identity is not minted
	if (f->customServing)
		return f->customCode > 0 ? f->customCode : -1;
	if (f->retail == 0 || f->levelID < 0 ||
	    f->levelID >= AP_RELIC_PERFECT_TRACK_COUNT)
		return -1;
	return f->retail[f->levelID] > 0 ? f->retail[f->levelID] : -1;
}

#endif
