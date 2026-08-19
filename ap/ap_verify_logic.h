#ifndef AP_VERIFY_LOGIC_H
#define AP_VERIFY_LOGIC_H

// Pure 0.2.0 seed-verifier rules. Runtime ap_verify.c and the focused host
// harness share this file so character/capability/weapon gates are not tested
// through a second implementation.

#define AP_VF_ITEM_COUNT 190
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

static int AP_VerifyFinishNeedsUSF(const AP_VerifyOptions *o, int level)
{
	return level == 7 || level == 10 ||
		(level == 13 && o->shortcut_knowledge != 2);
}

static int AP_VerifyDifficultyTrack(int level)
{
	static const unsigned char tracks[] = {3,6,4,14,9,2,8,5,0,1,12,15,11};
	unsigned i;
	for (i = 0; i < sizeof tracks; i++)
		if (tracks[i] == level) return 1;
	return 0;
}

static int AP_VerifyTrophyCapabilityGate(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int requiredCharacter)
{
	if (AP_VerifyFinishNeedsUSF(o, level) &&
		!AP_VerifyCapabilityGate(o, items, requiredCharacter, 2, 0))
		return 0;
	if (o->logic_difficulty != 2 && o->boost_mode != 0 && o->itemsanity &&
		AP_VerifyDifficultyTrack(level) &&
		!AP_VerifyCapabilityGate(o, items, requiredCharacter, 1, 0) &&
		AP_VerifyUsefulWeaponFamilies(items) < 2)
		return 0;
	return 1;
}

// Both Oxide goal conditions are raced on Oxide Station, so they carry that
// track's finish term: the composed goal must not become true from Key 4 and
// the companion flag alone while the ordinary Trophy Race logic still demands
// the capability. AP_VerifyFinishNeedsUSF is the same USF-or-hard-knowledge
// term the Trophy Race gate applies, not a second model of it. The difficulty
// branch of the trophy gate is deliberately not reused: the apworld ANDs the
// finish term only.
//
// The boost_mode guard is explicit here because the goal has no pad gate in
// front of it to absorb the racer-unlock check the capability gate would
// otherwise apply on a seed whose boost chain is not randomized. The apworld
// term is unconditionally True in that case. The same guard protects every
// capability call that is not behind ap_vf_pad_open on ITS OWN pad: the
// cup-leg term (AP_VerifyCupLegCapability) and the podium held-1st cup branch
// in ap_verify.c carry it for the same reason.
static int AP_VerifyOxideGoalFinish(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int requiredCharacter)
{
	if (o->boost_mode == 0 || !AP_VerifyFinishNeedsUSF(o, 13))
		return 1;
	return AP_VerifyCapabilityGate(o, items, requiredCharacter, 2, 0);
}

// Locations that need USF although their TRACK's finish does not. Keyed by AP
// location code because the ruling is per LOCATION: N. Gin Labs' Trophy Race,
// Sapphire, Gold, CTR Token Challenge and cups stay ungated. Without USF two of
// that track's item boxes cannot be reached in a Relic Race, which removes the
// ten-second perfect-box bonus and USF speed together.
#define AP_VF_LOC_LABS_PLATINUM 35012214L

static int AP_VerifyLocationNeedsUSF(long code)
{
	return code == AP_VF_LOC_LABS_PLATINUM;
}

// The per-location USF term: the bare two-boost capability, racer-aware, and
// vacuous when the boost chain is not randomized. Not the finish term -- these
// locations have no hard-knowledge escape, because route knowledge does not
// restore the unreachable boxes.
static int AP_VerifyLocationCapabilityGate(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], long code, int requiredCharacter)
{
	if (o->boost_mode == 0 || !AP_VerifyLocationNeedsUSF(code))
		return 1;
	return AP_VerifyCapabilityGate(o, items, requiredCharacter, 2, 0);
}

// One cup LEG's finish term. The apworld's usf_term is unconditionally True
// when the boost chain is not randomized and never evaluates a racer, while
// AP_VerifyCapabilityGate always checks the pinned racer's unlock. Guarding
// here keeps a legacy seed with a racer-locked USF leg pad from reporting the
// whole cup blocked.
static int AP_VerifyCupLegCapability(const AP_VerifyOptions *o,
	const int items[AP_VF_ITEM_COUNT], int level, int requiredCharacter)
{
	if (o->boost_mode == 0 || !AP_VerifyFinishNeedsUSF(o, level))
		return 1;
	return AP_VerifyCapabilityGate(o, items, requiredCharacter, 2, 0);
}

#endif
