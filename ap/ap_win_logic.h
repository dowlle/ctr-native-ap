#ifndef AP_WIN_LOGIC_H
#define AP_WIN_LOGIC_H

// Race-loss DeathLink stakes (native #449, 0.2.4): the freestanding half.
//
// With the race_loss send trigger on, a loss, a lost Gem Cup, or RESTART / EXIT
// TO MAP from the pause menu sends a DeathLink only when the race has STAKES:
// at least one of its win checks is not yet collected and is in logic. The
// apworld owns logic and exports it in the slot_data block `win_logic` (spec:
// ctr-artifacts/win-logic/SCHEMA.md, block version 1). Native parses that block
// once per connection (ap_seedcfg.cpp) into the flat tables below and only
// INTERPRETS the exported terms here; it never re-derives logic from options
// and never reads ap_verify.c's own mirror.
//
// Everything in this header is engine-free C that also compiles as C++, so the
// parser (C++ lib), the production glue (ap_win_logic.c, C unity build) and the
// host harnesses (tools/test-win-logic-*.cpp) share these exact functions.
//
// Fallback: a seed without the block, with a newer block version, or with an
// invalid block keeps 0.2.3 behaviour, where every loss sends.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AP_WL_VERSION_KNOWN 1

// Pool sizes. The apworld keeps the serialised block under 32 KiB and the spec
// asks native to accept up to 64 KiB; even 64 KiB of the smallest term ("true,")
// stays under these. A block that does not fit is refused as a whole.
#define AP_WL_MAX_NODES    16384
#define AP_WL_MAX_KIDS     16384
#define AP_WL_MAX_IDS      16384
#define AP_WL_MAX_CHECKS   1024
#define AP_WL_MAX_REGIONS  1024
#define AP_WL_MAX_FAMILIES 256
// Recursion guard for one term while parsing. The apworld promises at most 16
// levels including ref expansion; this guard is looser on purpose so a shape
// the apworld counts differently is not refused, while still bounding the stack.
#define AP_WL_MAX_DEPTH    40
#define AP_WL_PROBLEM_CAP  160

enum
{
	AP_WL_ABSENT  = 0, // no block (seed predates the feature)
	AP_WL_VALID   = 1,
	AP_WL_INVALID = 2, // malformed: treated as absent
	AP_WL_NEWER   = 3  // version > AP_WL_VERSION_KNOWN: treated as absent
};

enum
{
	AP_WL_T_FALSE = 0,
	AP_WL_T_TRUE,
	AP_WL_T_ALL,            // a = first index into kids[], b = child count
	AP_WL_T_ANY,            // same
	AP_WL_T_REQ,            // a = type, b = count, c = colour
	AP_WL_T_TOKENS_NO_PURPLE, // a = count
	AP_WL_T_ITEMS_SUM,      // a = count, b = first index into ids[], c = id count
	AP_WL_T_ITEMS_DISTINCT, // same
	AP_WL_T_CAP,            // a = boost, b = racer (-1 = no racer bound)
	AP_WL_T_BOSSES,         // a = count
	AP_WL_T_FAMILIES,       // a = count
	AP_WL_T_REF             // a = location code; b = resolved check index
};

enum
{
	AP_WL_KIND_TROPHY = 1,
	AP_WL_KIND_SAPPHIRE,
	AP_WL_KIND_GOLD,
	AP_WL_KIND_PLATINUM,
	AP_WL_KIND_CTR,
	AP_WL_KIND_BOSS,
	AP_WL_KIND_GEM,
	AP_WL_KIND_CRYSTAL,
	AP_WL_KIND_OXIDE,
	AP_WL_KIND_OXIDE_FINAL
};

typedef struct
{
	int tag;
	long a, b, c;
} AP_WinLogicNode;

typedef struct
{
	long code;
	int kind;
	int region;
	int rule; // node index
} AP_WinLogicCheck;

typedef struct
{
	int state;
	int version;
	int node_count;
	int kid_count;
	int id_count;
	int family_count;
	int region_count;
	int check_count;
	AP_WinLogicNode nodes[AP_WL_MAX_NODES];
	int kids[AP_WL_MAX_KIDS];
	long ids[AP_WL_MAX_IDS];
	int family_first[AP_WL_MAX_FAMILIES]; // into ids[]
	int family_len[AP_WL_MAX_FAMILIES];
	int region_root[AP_WL_MAX_REGIONS];   // node index
	AP_WinLogicCheck checks[AP_WL_MAX_CHECKS]; // sorted by code once VALID
	char problem[AP_WL_PROBLEM_CAP];      // first problem, for the one log line
} AP_WinLogic;

// The parsed block of the current connection (defined in ap_seedcfg.cpp,
// rewritten by every ap_seedcfg_parse_json call).
extern AP_WinLogic ctr_win_logic;

static inline const char *AP_WinLogicKindName(int kind)
{
	switch (kind)
	{
	case AP_WL_KIND_TROPHY:      return "trophy";
	case AP_WL_KIND_SAPPHIRE:    return "sapphire";
	case AP_WL_KIND_GOLD:        return "gold";
	case AP_WL_KIND_PLATINUM:    return "platinum";
	case AP_WL_KIND_CTR:         return "ctr";
	case AP_WL_KIND_BOSS:        return "boss";
	case AP_WL_KIND_GEM:         return "gem";
	case AP_WL_KIND_CRYSTAL:     return "crystal";
	case AP_WL_KIND_OXIDE:       return "oxide";
	case AP_WL_KIND_OXIDE_FINAL: return "oxide_final";
	}
	return 0;
}

// Binary search over the sorted check table. -1 when the code has no entry.
static inline int AP_WinLogicFindCheck(const AP_WinLogic *wl, long code)
{
	int lo = 0, hi = wl->check_count - 1;
	while (lo <= hi)
	{
		int mid = lo + (hi - lo) / 2;
		long c = wl->checks[mid].code;
		if (c == code)
			return mid;
		if (c < code)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

// ── Received-items-by-id tally ──────────────────────────────────────────────
// "Received count of id X" in the spec (SCHEMA.md, schema change 2026-10-02
// 20:46): ReceivedItems entries for this slot, any sender, start inventory
// included, whose NetworkItem.flags has bit 0 (progression) set. A copy
// generated as useful or filler never enters Archipelago's or Universal
// Tracker's logic state, so it never counts here either, although the native
// gate counters (AP_GateCount*) count every copy. Hence a separate count keyed by
// raw AP item id. Open addressing; an empty slot has count 0. Rebuilt from the
// full resent list on every fresh connect, exactly like the gate counters.
#define AP_WL_TALLY_CAP 2048 // power of two; a CTR slot has well under 1000 distinct ids

typedef struct
{
	long long id[AP_WL_TALLY_CAP];
	int count[AP_WL_TALLY_CAP];
	int used;
	int overflow; // a distinct id did not fit: counts are no longer trustworthy
} AP_ItemIdTally;

static inline void AP_ItemIdTallyReset(AP_ItemIdTally *t)
{
	int i;
	for (i = 0; i < AP_WL_TALLY_CAP; i++)
	{
		t->id[i] = 0;
		t->count[i] = 0;
	}
	t->used = 0;
	t->overflow = 0;
}

static inline unsigned AP_ItemIdTallySlot(long long id)
{
	unsigned long long x = (unsigned long long)id * 0x9E3779B97F4A7C15ULL;
	return (unsigned)(x >> 40) & (AP_WL_TALLY_CAP - 1);
}

static inline void AP_ItemIdTallyAdd(AP_ItemIdTally *t, long long id)
{
	unsigned s = AP_ItemIdTallySlot(id);
	int n;
	for (n = 0; n < AP_WL_TALLY_CAP; n++, s = (s + 1) & (AP_WL_TALLY_CAP - 1))
	{
		if (t->count[s] == 0)
		{
			if (t->used >= AP_WL_TALLY_CAP - 1)
			{
				t->overflow = 1;
				return;
			}
			t->id[s] = id;
			t->count[s] = 1;
			t->used++;
			return;
		}
		if (t->id[s] == id)
		{
			if (t->count[s] < 0x7fffffff)
				t->count[s]++;
			return;
		}
	}
	t->overflow = 1;
}

// One ReceivedItems entry: counted only when its flags carry the progression bit.
#define AP_WL_FLAG_PROGRESSION 1u
static inline void AP_ItemIdTallyReceive(AP_ItemIdTally *t, long long id, unsigned flags)
{
	if (flags & AP_WL_FLAG_PROGRESSION)
		AP_ItemIdTallyAdd(t, id);
}

static inline int AP_ItemIdTallyCount(const AP_ItemIdTally *t, long long id)
{
	unsigned s = AP_ItemIdTallySlot(id);
	int n;
	for (n = 0; n < AP_WL_TALLY_CAP; n++, s = (s + 1) & (AP_WL_TALLY_CAP - 1))
	{
		if (t->count[s] == 0)
			return 0;
		if (t->id[s] == id)
			return t->count[s];
	}
	return 0;
}

// ── Evaluation ──────────────────────────────────────────────────────────────
// The leaf inputs. Production wires these to native state (ap_win_logic.c);
// harnesses wire them to fixtures.
typedef struct
{
	void *user;
	// Received count of one AP item id (progression-flagged copies only).
	int (*item_count)(void *user, long id);
	// AP_ComposedBossesWon: boss races this player has personally won.
	int (*bosses_won)(void *user);
	// Location is in the slot's checked set (own sends plus server-reported).
	int (*collected)(void *user, long code);
	// ctr_options inputs of the cap leaf.
	int boost_mode;         // 0 off, 1 shared, 2 per character
	int starting_character; // engine id
	int character_unlocks;  // bool
	// 1 when the inputs above cannot be trusted (tally overflow): stakes fall
	// back to 0.2.3 behaviour rather than guess.
	int unreliable;
} AP_WinLogicEnv;

#define AP_WL_PROGRESSIVE_BOOST_ID 35010027L

// Engine id -> character unlock item and per-character Progressive Boost item
// (SCHEMA.md "The cap leaf" table).
static const long AP_WL_UNLOCK_ITEM[16] = {
	35010123L, 35010127L, 35010134L, 35010124L, 35010135L, 35010133L, 35010125L, 35010126L,
	35010132L, 35010130L, 35010129L, 35010131L, 35010128L, 35010138L, 35010136L, 35010137L};
static const long AP_WL_BOOST_ITEM[16] = {
	35010031L, 35010047L, 35010075L, 35010035L, 35010079L, 35010071L, 35010039L, 35010043L,
	35010067L, 35010059L, 35010055L, 35010063L, 35010051L, 35010091L, 35010083L, 35010087L};

static inline int AP_WinLogicDriveable(const AP_WinLogicEnv *env, int c)
{
	return c == env->starting_character || !env->character_unlocks ||
	       env->item_count(env->user, AP_WL_UNLOCK_ITEM[c]) >= 1;
}

static inline int AP_WinLogicBoostOk(const AP_WinLogicEnv *env, int boost, int c)
{
	int have;
	if (boost == 0 || env->boost_mode == 0)
		return 1;
	if (env->boost_mode == 1)
		have = env->item_count(env->user, AP_WL_PROGRESSIVE_BOOST_ID);
	else
		have = env->item_count(env->user, AP_WL_BOOST_ITEM[c]);
	return have >= boost;
}

// The cap leaf, exactly as SCHEMA.md defines it. Deliberately NOT built on
// AP_CharacterUnlocked / AP_CapabilityBoostTierForCharacter: the first answers
// "the racer being driven" and pre-character-phase defaults, the second clamps
// to the chain ceiling, and neither is the definition the apworld exports.
static inline int AP_WinLogicCap(const AP_WinLogicEnv *env, int boost, int racer)
{
	int c;
	if (racer >= 0)
		return racer < 16 && AP_WinLogicDriveable(env, racer) &&
		       AP_WinLogicBoostOk(env, boost, racer);
	if (boost == 0 || env->boost_mode == 0)
		return 1;
	for (c = 0; c < 16; c++)
		if (AP_WinLogicDriveable(env, c) && AP_WinLogicBoostOk(env, boost, c))
			return 1;
	return 0;
}

// The req leaf: the Contract section 2 Req meaning (the same type and colour
// table as AP_ReqMetCounts), counted from the progression-only received counts
// by item id rather than the gate counters. Ids: Trophy 35010000, Relics
// 35010001..3 (Sapphire, Gold, Platinum), CTR Tokens 35010004..8 (Red, Green,
// Blue, Yellow, Purple), Gems 35010009..13 (same order), Key 35010014.
static inline int AP_WinLogicReq(const AP_WinLogicEnv *env, int type, int count, int colour)
{
	long i, sum = 0;
#define AP_WL_N(id) ((long)env->item_count(env->user, (id)))
	switch (type)
	{
	case 1: // Trophies
		return AP_WL_N(35010000L) >= count;
	case 2: // Keys
		return AP_WL_N(35010014L) >= count;
	case 3: // one token colour; legacy -1 sums like type 6
		if (colour >= 0 && colour <= 4)
			return AP_WL_N(35010004L + colour) >= count;
		/* fall through */
	case 6: // any CTR Token, Purple included
		for (i = 0; i < 5; i++)
			sum += AP_WL_N(35010004L + i);
		return sum >= count;
	case 4: // one relic tier: 0 Sapphire, 1 Gold, 2 Platinum; legacy -1 = Sapphire
		i = (colour >= 0 && colour <= 2) ? colour : 0;
		return AP_WL_N(35010001L + i) >= count;
	case 5: // one gem colour; legacy -1 sums like type 8
		if (colour >= 0 && colour <= 4)
			return AP_WL_N(35010009L + colour) >= count;
		/* fall through */
	case 8: // any Gem, Purple included
		for (i = 0; i < 5; i++)
			sum += AP_WL_N(35010009L + i);
		return sum >= count;
	case 7: // any Relic
		return AP_WL_N(35010001L) + AP_WL_N(35010002L) + AP_WL_N(35010003L) >= count;
	}
#undef AP_WL_N
	return 0; // type 0 is never emitted and the parser refuses it
}

static inline int AP_WinLogicInLogic(const AP_WinLogic *wl, const AP_WinLogicEnv *env,
                                     int checkIndex, int depth);

static inline int AP_WinLogicEval(const AP_WinLogic *wl, const AP_WinLogicEnv *env,
                                  int nodeIndex, int depth)
{
	const AP_WinLogicNode *n;
	long i, sum;

	if (nodeIndex < 0 || nodeIndex >= wl->node_count || depth > 2 * AP_WL_MAX_DEPTH + 2)
		return 0; // unreachable on a VALID block; fail closed
	n = &wl->nodes[nodeIndex];
	switch (n->tag)
	{
	case AP_WL_T_FALSE:
		return 0;
	case AP_WL_T_TRUE:
		return 1;
	case AP_WL_T_ALL:
		for (i = 0; i < n->b; i++)
			if (!AP_WinLogicEval(wl, env, wl->kids[n->a + i], depth + 1))
				return 0;
		return 1;
	case AP_WL_T_ANY:
		for (i = 0; i < n->b; i++)
			if (AP_WinLogicEval(wl, env, wl->kids[n->a + i], depth + 1))
				return 1;
		return 0;
	case AP_WL_T_REQ:
		return AP_WinLogicReq(env, (int)n->a, (int)n->b, (int)n->c);
	case AP_WL_T_TOKENS_NO_PURPLE:
		sum = 0;
		for (i = 35010004L; i <= 35010007L; i++)
			sum += env->item_count(env->user, i);
		return sum >= n->a;
	case AP_WL_T_ITEMS_SUM:
		sum = 0;
		for (i = 0; i < n->c; i++)
			sum += env->item_count(env->user, wl->ids[n->b + i]);
		return sum >= n->a;
	case AP_WL_T_ITEMS_DISTINCT:
		sum = 0;
		for (i = 0; i < n->c; i++)
			if (env->item_count(env->user, wl->ids[n->b + i]) >= 1)
				sum++;
		return sum >= n->a;
	case AP_WL_T_CAP:
		return AP_WinLogicCap(env, (int)n->a, (int)n->b);
	case AP_WL_T_BOSSES:
		return env->bosses_won(env->user) >= n->a;
	case AP_WL_T_FAMILIES:
	{
		long held = 0;
		int f, k;
		for (f = 0; f < wl->family_count; f++)
			for (k = 0; k < wl->family_len[f]; k++)
				if (env->item_count(env->user, wl->ids[wl->family_first[f] + k]) >= 1)
				{
					held++;
					break;
				}
		return held >= n->a;
	}
	case AP_WL_T_REF:
		return AP_WinLogicInLogic(wl, env, (int)n->b, depth + 1);
	}
	return 0;
}

// in_logic(L) = eval(regions[checks[L].region]) AND eval(checks[L].rule)
static inline int AP_WinLogicInLogic(const AP_WinLogic *wl, const AP_WinLogicEnv *env,
                                     int checkIndex, int depth)
{
	const AP_WinLogicCheck *c;
	if (checkIndex < 0 || checkIndex >= wl->check_count)
		return 0;
	c = &wl->checks[checkIndex];
	return AP_WinLogicEval(wl, env, wl->region_root[c->region], depth) &&
	       AP_WinLogicEval(wl, env, c->rule, depth);
}

// ── The stakes decision ─────────────────────────────────────────────────────
enum
{
	AP_WS_WHY_STAKES = 0,       // an open, in-logic win: the race counts
	AP_WS_WHY_NO_BLOCK,         // no win_logic block: 0.2.3, counts
	AP_WS_WHY_INVALID,          // invalid block: 0.2.3, counts
	AP_WS_WHY_NEWER,            // newer block version: 0.2.3, counts
	AP_WS_WHY_UNRELIABLE,       // inputs untrustworthy: 0.2.3, counts
	AP_WS_WHY_COLLECTED,        // every win entry already collected: exempt
	AP_WS_WHY_OUT_OF_LOGIC,     // every open win entry out of logic: exempt
	AP_WS_WHY_COLLECTED_OR_OUT, // some collected, the rest out of logic: exempt
	AP_WS_WHY_NO_ENTRY          // the race's win set has no entry: exempt
};

typedef struct
{
	int stakes;
	int why;
	long code; // the deciding open in-logic code when stakes via logic, else -1
} AP_WinStakes;

static inline const char *AP_WinStakesWhyText(int why)
{
	switch (why)
	{
	case AP_WS_WHY_STAKES:           return "win open and in logic";
	case AP_WS_WHY_NO_BLOCK:         return "seed has no win_logic block, 0.2.3 rule";
	case AP_WS_WHY_INVALID:          return "win_logic block invalid, 0.2.3 rule";
	case AP_WS_WHY_NEWER:            return "win_logic block newer than this client, 0.2.3 rule";
	case AP_WS_WHY_UNRELIABLE:       return "received-item tally overflowed, 0.2.3 rule";
	case AP_WS_WHY_COLLECTED:        return "win already collected";
	case AP_WS_WHY_OUT_OF_LOGIC:     return "win out of logic";
	case AP_WS_WHY_COLLECTED_OR_OUT: return "wins collected or out of logic";
	case AP_WS_WHY_NO_ENTRY:         return "race has no win check in this seed";
	}
	return "?";
}

static inline AP_WinStakes AP_WinStakesDecide(const AP_WinLogic *wl, const AP_WinLogicEnv *env,
                                              const long *winSet, int n)
{
	AP_WinStakes r;
	int i, entries = 0, collected = 0, outOfLogic = 0;

	r.stakes = 1;
	r.code = -1;
	if (wl == 0 || wl->state != AP_WL_VALID)
	{
		r.why = (wl == 0 || wl->state == AP_WL_ABSENT) ? AP_WS_WHY_NO_BLOCK
		      : wl->state == AP_WL_NEWER                ? AP_WS_WHY_NEWER
		                                                : AP_WS_WHY_INVALID;
		return r;
	}
	if (env->unreliable)
	{
		r.why = AP_WS_WHY_UNRELIABLE;
		return r;
	}
	for (i = 0; i < n; i++)
	{
		int idx = AP_WinLogicFindCheck(wl, winSet[i]);
		if (idx < 0)
			continue;
		entries++;
		if (env->collected(env->user, winSet[i]))
		{
			collected++;
			continue;
		}
		if (AP_WinLogicInLogic(wl, env, idx, 0))
		{
			r.why = AP_WS_WHY_STAKES;
			r.code = winSet[i];
			return r;
		}
		outOfLogic++;
	}
	r.stakes = 0;
	if (entries == 0)
		r.why = AP_WS_WHY_NO_ENTRY;
	else if (outOfLogic == 0)
		r.why = AP_WS_WHY_COLLECTED;
	else if (collected == 0)
		r.why = AP_WS_WHY_OUT_OF_LOGIC;
	else
		r.why = AP_WS_WHY_COLLECTED_OR_OUT;
	return r;
}

// ── Which locations are "the win" of the race being started ─────────────────
// The codes native itself sends for that race's win (SCHEMA.md "Kinds and race
// modes", ticket 01). Facts are read from the engine by ap_win_logic.c; retail
// AdvProgress bits go through bitCode (AP_LookupLocationCode in production, the
// generated AP_LOCATION_TABLE in harnesses).
enum
{
	AP_WS_MODE_NONE = 0,
	AP_WS_MODE_TROPHY,
	AP_WS_MODE_RELIC,
	AP_WS_MODE_CTR,
	AP_WS_MODE_BOSS,
	AP_WS_MODE_GEM_CUP,
	AP_WS_MODE_CRYSTAL
};

typedef struct
{
	int adventure;
	int boss;       // IS_BOSS_RACE
	int bossID;     // 0..3 key bosses, 4 Oxide first, 5 Oxide final
	int cup;        // ADVENTURE_CUP
	int cupID;      // 0..4
	int cupCustom;  // the cup is redirected to the custom track (one race)
	int relic;      // RELIC_RACE
	int token;      // TOKEN_RACE (CTR Token Challenge)
	int crystal;    // CRYSTAL_CHALLENGE
	int levelID;
	int cortex;     // Cortex Vortex pad track on screen
	int trial;      // AP_TrialTrackConfigured(levelID)
	long trialTrophy, trialCtr;   // trial-track direct codes (-1 absent)
	long cortexCodes[5];          // trophy, sapphire, gold, platinum, CTR (-1 absent)
	long customTrophy, customCtr; // custom-slot direct codes (-1 absent)
} AP_WinRaceFacts;

#define AP_WS_WINSET_MAX 3

// AdvProgress bit layout (namespace_Memcard.h), restated so this stays engine-free.
#define AP_WS_BIT_TROPHY    0x06
#define AP_WS_BIT_SAPPHIRE  0x16
#define AP_WS_BIT_RELIC_STRIDE 0x12
#define AP_WS_BIT_CTR_TOKEN 0x4c
#define AP_WS_BIT_BOSS_KEY  0x5e
#define AP_WS_BIT_GEM       0x6a
#define AP_WS_BIT_PURPLE_TOKEN 0x6f
#define AP_WS_BIT_OXIDE_FIRST  115
#define AP_WS_BIT_OXIDE_SECOND 116

// Crystal arena LevelID -> purple-token reward offset (R232.battleTrackArr,
// indexed LevelID - 18): Nitro Court 18 -> 3, Rampage Ruins 19 -> 1, Skull Rock
// 21 -> 0, Rocky Road 23 -> 2.
static inline int AP_WinCrystalOffset(int levelID)
{
	switch (levelID)
	{
	case 18: return 3;
	case 19: return 1;
	case 21: return 0;
	case 23: return 2;
	}
	return -1;
}

static inline int AP_WinSetPush(long *out, int n, long code)
{
	if (code > 0 && n < AP_WS_WINSET_MAX)
		out[n++] = code;
	return n;
}

// Fills out[] with the race's win set and returns its size; *mode names the
// race mode for the log. 0 entries means "no win identified".
static inline int AP_WinSetResolve(const AP_WinRaceFacts *f, long (*bitCode)(int bit),
                                   long *out, int *mode)
{
	int n = 0, t, L = f->levelID;

	*mode = AP_WS_MODE_NONE;
	if (!f->adventure)
		return 0;
	if (f->boss)
	{
		*mode = AP_WS_MODE_BOSS;
		if (f->bossID >= 0 && f->bossID < 4)
			return AP_WinSetPush(out, n, bitCode(AP_WS_BIT_BOSS_KEY + f->bossID));
		if (f->bossID == 4)
			return AP_WinSetPush(out, n, bitCode(AP_WS_BIT_OXIDE_FIRST));
		if (f->bossID == 5)
			return AP_WinSetPush(out, n, bitCode(AP_WS_BIT_OXIDE_SECOND));
		return 0;
	}
	if (f->crystal)
	{
		int off = AP_WinCrystalOffset(L);
		*mode = AP_WS_MODE_CRYSTAL;
		return off < 0 ? 0 : AP_WinSetPush(out, n, bitCode(AP_WS_BIT_PURPLE_TOKEN + off));
	}
	if (f->cup)
	{
		*mode = AP_WS_MODE_GEM_CUP;
		if (f->cupCustom)
			return AP_WinSetPush(out, n, f->token ? f->customCtr : f->customTrophy);
		if (f->cupID < 0 || f->cupID > 4)
			return 0;
		return AP_WinSetPush(out, n, bitCode(AP_WS_BIT_GEM + f->cupID));
	}
	if (f->relic)
	{
		*mode = AP_WS_MODE_RELIC;
		if (f->cortex)
		{
			for (t = 0; t < 3; t++)
				n = AP_WinSetPush(out, n, f->cortexCodes[1 + t]);
			return n;
		}
		if (L < 0 || L > 17)
			return 0;
		for (t = 0; t < 3; t++)
			n = AP_WinSetPush(out, n, bitCode(AP_WS_BIT_SAPPHIRE + AP_WS_BIT_RELIC_STRIDE * t + L));
		return n;
	}
	*mode = f->token ? AP_WS_MODE_CTR : AP_WS_MODE_TROPHY;
	if (f->cortex)
		return AP_WinSetPush(out, n, f->token ? f->cortexCodes[4] : f->cortexCodes[0]);
	if (f->trial)
		return AP_WinSetPush(out, n, f->token ? f->trialCtr : f->trialTrophy);
	if (L < 0 || L > 15)
		return 0;
	return AP_WinSetPush(out, n, bitCode((f->token ? AP_WS_BIT_CTR_TOKEN : AP_WS_BIT_TROPHY) + L));
}

static inline const char *AP_WinModeName(int mode)
{
	switch (mode)
	{
	case AP_WS_MODE_TROPHY:  return "Trophy Race";
	case AP_WS_MODE_RELIC:   return "Relic Race";
	case AP_WS_MODE_CTR:     return "CTR Token Challenge";
	case AP_WS_MODE_BOSS:    return "Boss Race";
	case AP_WS_MODE_GEM_CUP: return "Gem Cup";
	case AP_WS_MODE_CRYSTAL: return "Crystal Challenge";
	}
	return "unidentified race";
}

// ── When stakes are decided (ticket 04: at race start) ──────────────────────
// One decision per race attempt, fixed at its level start. A Gem Cup decides
// once when the cup is entered from the hub (its first leg) and keeps that
// decision for every leg, a leg restart and the cup result. A hub, menu or
// other non-race load forgets both.
enum
{
	AP_WS_NEED_NONE = 0,
	AP_WS_NEED_RACE = 1,
	AP_WS_NEED_CUP  = 2
};

typedef struct
{
	int raceValid;
	AP_WinStakes race;
	int cupValid;
	AP_WinStakes cup;
	int cupArmed; // entered a cup from the hub; its first leg decides
} AP_WinStakesLatch;

static inline void AP_WinStakesLatchReset(AP_WinStakesLatch *l)
{
	l->raceValid = 0;
	l->cupValid = 0;
	l->cupArmed = 0;
}

static inline void AP_WinStakesCupEntered(AP_WinStakesLatch *l)
{
	l->cupArmed = 1;
	l->cupValid = 0;
}

// Level start. isRace: an adventure racing level (track, arena challenge, boss);
// isCup: a Gem Cup leg. Returns which decision the caller must make now.
static inline int AP_WinStakesLevelStart(AP_WinStakesLatch *l, int isRace, int isCup)
{
	l->raceValid = 0;
	if (!isRace)
	{
		l->cupValid = 0;
		return AP_WS_NEED_NONE;
	}
	if (isCup)
		return (l->cupArmed || !l->cupValid) ? AP_WS_NEED_CUP : AP_WS_NEED_NONE;
	l->cupValid = 0;
	return AP_WS_NEED_RACE;
}

static inline void AP_WinStakesRecord(AP_WinStakesLatch *l, int need, AP_WinStakes s)
{
	if (need == AP_WS_NEED_CUP)
	{
		l->cup = s;
		l->cupValid = 1;
		l->cupArmed = 0;
	}
	else if (need == AP_WS_NEED_RACE)
	{
		l->race = s;
		l->raceValid = 1;
	}
}

// The decision a loss event uses. Returns 0 when none was made at race start
// (for example the slot connected mid-race); the caller then decides late.
static inline int AP_WinStakesCurrent(const AP_WinStakesLatch *l, int inCup, AP_WinStakes *out)
{
	if (inCup ? !l->cupValid : !l->raceValid)
		return 0;
	*out = inCup ? l->cup : l->race;
	return 1;
}

#ifdef __cplusplus
}
#endif

// ── Production glue (ap_win_logic.c, C unity build) ─────────────────────────
#if defined(CTR_AP) && !defined(AP_WIN_LOGIC_FREESTANDING_ONLY)
#ifdef __cplusplus
extern "C" {
#endif
struct GameTracker;
// ReceivedItems drain and its per-connect reset (ap_hooks.c).
void AP_WinLogicTallyReset(void);
void AP_WinLogicTallyItem(long long itemId, unsigned flags);
// Cup entry from the hub (AP_CupEnterFromHub) arms the cup decision.
void AP_WinStakesOnCupEnter(void);
// Every level start (AP_RaceAttempt_OnLevelStart): decide the race's stakes.
void AP_WinStakesOnLevelStart(struct GameTracker *gGT, int racingLevel);
// A race_loss send is about to go out. Returns 1 to send, 0 when the race is
// exempt (and logs why). inCup: the event belongs to a Gem Cup.
int AP_WinStakesAllowLossSend(struct GameTracker *gGT, int inCup, const char *cause);
#ifdef __cplusplus
}
#endif
#endif

#endif // AP_WIN_LOGIC_H
