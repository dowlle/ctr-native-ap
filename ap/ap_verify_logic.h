#ifndef AP_VERIFY_LOGIC_H
#define AP_VERIFY_LOGIC_H

// Pure 0.2.0 seed-verifier rules. Runtime ap_verify.c and the focused host
// harness share this file so character/capability/weapon gates are not tested
// through a second implementation.

#define AP_VF_ITEM_COUNT 200
#define AP_VF_BOOST_SHARED 27
#define AP_VF_PC_FIRST 31
#define AP_VF_WEAPON_FIRST 95
#define AP_VF_CHARACTER_FIRST 123

typedef struct
{
	int boost_mode;
	int stats_mode;
	int character_unlocks;
	int starting_character;
	int itemsanity;
	int logic_difficulty;
	int shortcut_knowledge;
	int boost_blue_fire;    // Blue Fire rank above USF (Platinum floor on easy)
	int oxide_final_cortex; // 1 when the Final Challenge is raced on Cortex Vortex
} AP_VerifyOptions;

// Wire roster slot -> engine character id. Mirrors AP_CAP_ROSTER_CHARACTER.
static const unsigned char AP_VF_ROSTER_CHARACTER[16] =
	{0, 3, 6, 7, 1, 12, 10, 9, 11, 8, 5, 2, 4, 14, 15, 13};

static int AP_VerifyRosterSlot(int character)
{
	int i;
	for (i = 0; i < 16; i++)
		if (AP_VF_ROSTER_CHARACTER[i] == character)
			return i;
	return -1;
}

static int AP_VerifyCharacterUnlocked(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int character)
{
	int slot;
	if (character < 0 || character >= 16)
		return 0;
	if (!o->character_unlocks || character == o->starting_character)
		return 1;
	slot = AP_VerifyRosterSlot(character);
	return slot >= 0 && items[AP_VF_CHARACTER_FIRST + slot] > 0;
}

static int AP_VerifyCapabilityForCharacter(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int character, int boostMin,
	int needsStats)
{
	int slot, chain;
	if (!AP_VerifyCharacterUnlocked(o, items, character))
		return 0;
	slot = AP_VerifyRosterSlot(character);
	if (slot < 0)
		return 0;
	if (boostMin > 0 && o->boost_mode != 0)
	{
		int count = o->boost_mode == 1
			? items[AP_VF_BOOST_SHARED]
			: items[AP_VF_PC_FIRST + slot * 4];
		if (count < boostMin)
			return 0;
	}
	if (needsStats && o->stats_mode != 0)
		for (chain = 1; chain < 4; chain++)
		{
			int count = o->stats_mode == 1
				? items[AP_VF_BOOST_SHARED + chain]
				: items[AP_VF_PC_FIRST + slot * 4 + chain];
			if (count < 1)
				return 0;
		}
	return 1;
}

// All capability terms must be satisfied by one usable racer. A racer-locked
// pad pins that racer; an unlocked pad may use any currently playable racer.
static int AP_VerifyCapabilityGate(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int requiredCharacter, int boostMin,
	int needsStats)
{
	int character;
	if (requiredCharacter >= 0)
		return AP_VerifyCapabilityForCharacter(o, items, requiredCharacter,
			boostMin, needsStats);
	for (character = 0; character < 16; character++)
		if (AP_VerifyCapabilityForCharacter(o, items, character,
			boostMin, needsStats))
			return 1;
	return 0;
}

static int AP_VerifyWeaponOwned(const int items[AP_VF_ITEM_COUNT], int weapon)
{
	return weapon >= 0 && weapon < 11 && items[AP_VF_WEAPON_FIRST + weapon] > 0;
}

static int AP_VerifyUsefulWeaponFamilies(const int items[AP_VF_ITEM_COUNT])
{
	int n = 0;
	n += AP_VerifyWeaponOwned(items, 6);                         // Mask
	n += AP_VerifyWeaponOwned(items, 2) || AP_VerifyWeaponOwned(items, 10); // Missile
	n += AP_VerifyWeaponOwned(items, 1) || AP_VerifyWeaponOwned(items, 9);  // Bomb
	n += AP_VerifyWeaponOwned(items, 8);                         // Warpball
	n += AP_VerifyWeaponOwned(items, 7);                         // Clock
	return n;
}

static int AP_VerifyTigerDoorWeapon(const int items[AP_VF_ITEM_COUNT])
{
	static const unsigned char openers[] = {1, 9, 2, 10, 4, 5, 6};
	unsigned i;
	for (i = 0; i < sizeof openers; i++)
		if (AP_VerifyWeaponOwned(items, openers[i]))
			return 1;
	return 0;
}

typedef struct { unsigned char level, slot, boost, stats; } AP_VerifyBoxRule;
static const AP_VerifyBoxRule AP_VF_BOX_RULES[] = {
	{3,4,1,0},{3,9,1,0},{3,10,1,0},{8,2,1,0},{8,3,1,0},
	{5,6,1,0},{5,12,1,0},{5,7,1,1},{5,10,1,1},{1,2,0,0},
	{14,7,0,0},{12,14,1,0},{12,15,1,0},{12,9,1,1},{10,10,1,0},
	{10,11,2,0},{11,4,2,0},{11,5,2,0},{11,6,2,0},{11,7,2,0},
	{11,8,2,0},{11,9,2,0},{11,14,2,0},{11,15,2,0},{13,6,0,1},
	{13,13,2,0},{7,1,2,0},{7,2,2,0},{7,3,2,0},{7,4,2,0},
	{7,5,2,0},{7,6,2,0},{7,7,2,0},{7,8,2,1},{7,9,2,0},
	{7,13,2,0},{7,14,2,0},{7,15,2,0},{15,1,1,0},{4,5,0,0}
};

static void AP_VerifyBoxRequirement(int level, int slot, int *boost, int *stats)
{
	unsigned i;
	*boost = 0; *stats = 0;
	for (i = 0; i < sizeof AP_VF_BOX_RULES / sizeof AP_VF_BOX_RULES[0]; i++)
		if (AP_VF_BOX_RULES[i].level == level && AP_VF_BOX_RULES[i].slot == slot)
		{
			*boost = AP_VF_BOX_RULES[i].boost;
			*stats = AP_VF_BOX_RULES[i].stats;
			return;
		}
}

static int AP_VerifyBoxGate(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int slot, int requiredCharacter)
{
	int boost, stats;
	AP_VerifyBoxRequirement(level, slot, &boost, &stats);
	if (!AP_VerifyCapabilityGate(o, items, requiredCharacter, boost, stats))
		return 0;
	return !(o->itemsanity && level == 4 && slot == 5) ||
		AP_VerifyTigerDoorWeapon(items);
}

// The boost-rank term every capability rule is built from (apworld
// usf_finish.boost_term): vacuous while the boost chain is not randomized,
// otherwise the racer-aware capability gate at `boostMin`. The guard matters
// wherever no pad gate precedes the call to absorb the gate's racer-unlock
// check: the apworld term is unconditionally True in that case.
static int AP_VerifyBoostTerm(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int requiredCharacter, int boostMin)
{
	if (o->boost_mode == 0 || boostMin <= 0)
		return 1;
	return AP_VerifyCapabilityGate(o, items, requiredCharacter, boostMin, 0);
}

// Tracks whose finish line needs USF (apworld capability_contract finish
// records): Hot Air Skyway and Cortex Castle always, Oxide Station unless the
// seed declared hard shortcut knowledge. Cortex Vortex has no level id here;
// its venue term is AP_VerifyOxideWinTerm's.
static int AP_VerifyFinishNeedsUSF(const AP_VerifyOptions *o, int level)
{
	return level == 7 || level == 10 ||
		(level == 13 && o->shortcut_knowledge != 2);
}

// apworld usf_finish.track_finish_term: the USF rank on a finish-gated track.
static int AP_VerifyFinishTerm(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int requiredCharacter)
{
	if (!AP_VerifyFinishNeedsUSF(o, level))
		return 1;
	return AP_VerifyBoostTerm(o, items, requiredCharacter, 2);
}

// Held 1st carries the track's finish term on Oxide Station (2026-08-14) and
// on Hot Air Skyway (issue #329, ruling 2026-09-27) -- capability_contract
// gate_held_first. Hot Air Skyway has no hard-knowledge escape; Oxide keeps
// its own. Held 3rd and Held 5th stay free on both.
static int AP_VerifyHeldFirstTerm(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int requiredCharacter)
{
	if (level != 7 && level != 13)
		return 1;
	return AP_VerifyFinishTerm(o, items, level, requiredCharacter);
}

// capability_contract.difficulty_gated_tracks(): the measured easy group
// (Crash Cove, Roo's Tubes, Tiger Temple, Coco Park, Mystery Caves, Blizzard
// Bluff, Sewer Speedway) and the ruled group (Papu's Pyramid, Dingo Canyon,
// Dragon Mines, Polar Pass, Tiny Arena, N. Gin Labs, Slide Coliseum, Turbo
// Track). Custom-track slots are gated too (CUSTOM_TRACK_SLOTS_RULED); their
// callers apply AP_VerifyDifficultyTerm directly because a slot has no level.
static int AP_VerifyDifficultyTrack(int level)
{
	static const unsigned char tracks[] =
		{3,6,4,14,9,2,8,5,0,1,12,15,11,16,17};
	unsigned i;
	for (i = 0; i < sizeof tracks; i++)
		if (tracks[i] == level) return 1;
	return 0;
}

// itemsanity.DIFFICULTY_WEAPON_FAMILY_MIN (ruling 2026-09-20).
#define AP_VF_DIFFICULTY_WEAPON_FAMILY_MIN 3

// The logic_difficulty term (apworld add_capability_difficulty_rules). It
// applies whenever Progressive Boost is on. With Itemsanity on it is "first
// boost rank OR three useful weapon families"; with Itemsanity off there are no
// weapon items, so the first boost rank alone (issue #329, 2026-09-27). Hard
// adds nothing.
static int AP_VerifyDifficultyTerm(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int requiredCharacter)
{
	if (o->logic_difficulty == 2 || o->boost_mode == 0)
		return 1;
	if (AP_VerifyCapabilityGate(o, items, requiredCharacter, 1, 0))
		return 1;
	return o->itemsanity &&
		AP_VerifyUsefulWeaponFamilies(items) >= AP_VF_DIFFICULTY_WEAPON_FAMILY_MIN;
}

// Which locations on a difficulty-gated track take that term. `rung` is -1
// for the Trophy Race, otherwise the podium rung index (0 Held 1st, 1 Held
// 3rd, 2 Held 5th, 3 Finish on Podium, 4 Finish Any). Medium gates the Trophy
// Race only; easy also Finish on Podium and Held 1st.
static int AP_VerifyDifficultyGates(const AP_VerifyOptions *o, int rung)
{
	if (rung < 0)
		return o->logic_difficulty != 2;
	return o->logic_difficulty == 0 && (rung == 0 || rung == 3);
}

static int AP_VerifyDifficultyRungTerm(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int rung, int requiredCharacter)
{
	if (!AP_VerifyDifficultyGates(o, rung))
		return 1;
	return AP_VerifyDifficultyTerm(o, items, requiredCharacter);
}

// One podium rung (apworld add_podium_placement_rules and _rung_rule). The
// rung is reachable from the track's own pad (`ownOpen`) or from a Gem Cup that
// legs the track (`cupOpen`, which the caller computes with the cup's own
// finish term, for held rungs too). The track branch of a finish rung (index 3
// and 4) also crosses the finish line; held rungs fire mid-race and need only
// the pad. Two terms then sit on the WHOLE rung, cup branch included: Held 1st
// takes the track's held-first USF term, and on easy Finish on Podium and Held
// 1st take the difficulty term. The difficulty term is not inherited through
// the track branch, so Finish (Any Position) and medium Finish on Podium stay
// free of it. Every term uses the track pad's racer.
static int AP_VerifyPodiumRung(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int rung,
	int requiredCharacter, int ownOpen, int cupOpen)
{
	int ok = ownOpen &&
		(rung < 3 || AP_VerifyFinishTerm(o, items, level, requiredCharacter));
	if (!ok && !cupOpen)
		return 0;
	if (rung == 0 && !AP_VerifyHeldFirstTerm(o, items, level, requiredCharacter))
		return 0;
	if (AP_VerifyDifficultyTrack(level) &&
		!AP_VerifyDifficultyRungTerm(o, items, rung, requiredCharacter))
		return 0;
	return 1;
}

// A retail (or trial) Trophy Race: the finish term plus, on a difficulty-gated
// track, the logic_difficulty term. Time Trials, CTR Token Challenges and
// letters inherit this through the apworld's can_reach of the Trophy Race.
static int AP_VerifyTrophyCapabilityGate(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int requiredCharacter)
{
	if (!AP_VerifyFinishTerm(o, items, level, requiredCharacter))
		return 0;
	if (AP_VerifyDifficultyTrack(level) &&
		!AP_VerifyDifficultyRungTerm(o, items, -1, requiredCharacter))
		return 0;
	return 1;
}

// The win term of an Oxide encounter (apworld add_oxide_access_contract
// first_win_rule / final_win_rule): the first boost rank plus the venue's
// finish term, both unbound from any pad's racer lock because the race starts
// in the garage. The first challenge is raced on Oxide Station (USF or hard
// knowledge); the Final Challenge on the seed's venue, where Cortex Vortex
// needs USF with no escape. Applied to both Oxide LOCATIONS, and so to the
// goal that reads them.
static int AP_VerifyOxideWinTerm(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int finalChallenge)
{
	if (!AP_VerifyBoostTerm(o, items, -1, 1))
		return 0;
	if (finalChallenge && o->oxide_final_cortex)
		return AP_VerifyBoostTerm(o, items, -1, 2);
	return AP_VerifyFinishTerm(o, items, 13, -1);
}

// Boss race win term (apworld add_boss_garage_rules): the first boost rank,
// plus the finish term of a USF boss track -- Pinstripe races Hot Air Skyway
// (boss index 3). Unbound from racer locks, like the Oxide encounters.
static int AP_VerifyBossWinTerm(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int boss)
{
	if (!AP_VerifyBoostTerm(o, items, -1, 1))
		return 0;
	return boss != 3 || AP_VerifyFinishTerm(o, items, 7, -1);
}

// Relic tier boost gates, keyed by AP location code (ruling 2026-08-21,
// posting-pack fixing-pass session; supersedes the narrow 2026-08-19
// Labs-Platinum entry, which is subsumed by the Platinum USF row below).
// Mirrors apworld usf_finish.relic_tier_boost_min: every Gold and Platinum
// Time Trial requires the FIRST boost rank, and the USF-class tracks require
// the full two-boost rank -- Hot Air Skyway (row 12) and Oxide Station
// (row 15) on both tiers, N. Gin Labs (row 14) on Platinum only. Since the
// 2026-09-17 review every Platinum needs rank 2 on easy and medium, and rank
// 3 on easy with Blue Fire; hard keeps the per-track minima. Sapphire
// (35012000 block) carries no gate. Rows are the apworld's canonical relic
// track order (35012000 + tier*100 + row, tier 1 = Gold, 2 = Platinum),
// NOT game level ids -- keying by code sidesteps that identity space.
#define AP_VF_LOC_LABS_PLATINUM 35012214L

static int AP_VerifyLocationBoostMin(const AP_VerifyOptions *o, long code)
{
	long tier, row;
	if (code < 35012100L || code > 35012217L)
		return 0;
	tier = (code - 35012000L) / 100L; /* 1 Gold, 2 Platinum */
	row = (code - 35012000L) % 100L;
	if (row > 17L)
		return 0;
	if (tier == 1L)
		return (row == 12L || row == 15L) ? 2 : 1;
	if (o->logic_difficulty <= 1)
		return (o->logic_difficulty == 0 && o->boost_blue_fire) ? 3 : 2;
	return (row == 12L || row == 14L || row == 15L) ? 2 : 1;
}

// The per-location boost term: the bare capability at the tier's rank,
// racer-aware, and vacuous when the boost chain is not randomized. Not the
// finish term -- tier gates have no hard-knowledge escape, because route
// knowledge does not substitute for boost reserves on a relic pace (and for
// N. Gin Labs does not restore the unreachable boxes).
static int AP_VerifyLocationCapabilityGate(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], long code, int requiredCharacter)
{
	return AP_VerifyBoostTerm(o, items, requiredCharacter,
		AP_VerifyLocationBoostMin(o, code));
}

// One cup LEG's finish term. The apworld's usf_term is unconditionally True
// when the boost chain is not randomized and never evaluates a racer, while
// AP_VerifyCapabilityGate always checks the pinned racer's unlock. Guarding
// here keeps a legacy seed with a racer-locked USF leg pad from reporting the
// whole cup blocked.
static int AP_VerifyCupLegCapability(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int requiredCharacter)
{
	return AP_VerifyFinishTerm(o, items, level, requiredCharacter);
}

// A track's letters as the seed created them. `present` marks a created letter
// location (mode 1 and 2), `item` the item index of that track's letter item,
// -1 when this build has none.
typedef struct
{
	int mode; // lettersanity mode: 0 off, 1 locations, 2 both, 3 items
	int present[3];
	int item[3];
} AP_VerifyLetterSet;

#define AP_VF_WEAPON_TURBO 0
#define AP_VF_WEAPON_MASK 6

// The extra terms a CTR Token Challenge carries on top of its entry rule
// (apworld add_time_trial_and_ctr_requirements and add_lettersanity_rules):
// the first boost rank at every difficulty (ruling 2026-09-20); Oxide
// Station's USF with no escape while a physical T or R has to be taken; the
// Tiger Temple door opener while Itemsanity models weapons and R is needed;
// and in modes 2 and 3 the track's letter items.
static int AP_VerifyTokenTerm(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int requiredCharacter,
	const AP_VerifyLetterSet *letters)
{
	int letter;
	if (!AP_VerifyBoostTerm(o, items, requiredCharacter, 1))
		return 0;
	if (level == 13 &&
		(letters->mode != 2 || letters->present[1] || letters->present[2]) &&
		!AP_VerifyBoostTerm(o, items, requiredCharacter, 2))
		return 0;
	if (level == 4 && o->itemsanity &&
		(letters->mode != 2 || letters->present[2]) &&
		!AP_VerifyTigerDoorWeapon(items))
		return 0;
	if (letters->mode == 2 || letters->mode == 3)
		for (letter = 0; letter < 3; letter++)
			if ((letters->mode == 3 || letters->present[letter]) &&
				(letters->item[letter] < 0 || items[letters->item[letter]] <= 0))
				return 0;
	return 1;
}

// Per-letter capability terms on top of the shared token-challenge entry rule
// (apworld add_lettersanity_rules): Oxide Station T and R need USF with no
// hard-knowledge escape; Tiger Temple R needs a door opener while Itemsanity
// models weapons; Papu's Pyramid C and T need the first boost rank, or Turbo or
// Mask while Itemsanity is on. `letter` is 0 C, 1 T, 2 R.
static int AP_VerifyLetterTerm(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int letter,
	int requiredCharacter)
{
	if (level == 13 && (letter == 1 || letter == 2))
		return AP_VerifyBoostTerm(o, items, requiredCharacter, 2);
	if (level == 4 && letter == 2 && o->itemsanity)
		return AP_VerifyTigerDoorWeapon(items);
	if (level == 5 && (letter == 0 || letter == 1) && o->boost_mode != 0)
		return AP_VerifyCapabilityGate(o, items, requiredCharacter, 1, 0) ||
			(o->itemsanity && (AP_VerifyWeaponOwned(items, AP_VF_WEAPON_TURBO) ||
				AP_VerifyWeaponOwned(items, AP_VF_WEAPON_MASK)));
	return 1;
}

#endif
