#ifndef AP_RACE_STRUCTURE_LOGIC_H
#define AP_RACE_STRUCTURE_LOGIC_H

// Adventure race structure and the pad race type selector (#208, 0.3.0).
// Freestanding so tools/test-race-structure.py pins it out of engine.
//
// Two seed options (ctr_options, schema 18):
//   adventure_structure  "classic" | "ctr_first". Under ctr_first a track that
//                        has a CTR Challenge (its Trophy code is in the
//                        `race_structure` award pairs) offers only the CTR
//                        Challenge at stage 1 and the Relic Race at stage 2
//                        (decision 14 of 2026-10-28). A qualifying CTR win
//                        sends the CTR check and the paired Trophy Race check;
//                        boxes, Wumpa and podium rungs come from that race. A
//                        track without a CTR Challenge keeps the classic order
//                        inside a ctr_first seed (decision 4).
//   race_type_selector   "productive" | "always". When the selector opens. The
//                        seed value is the starting value; the in-game Options
//                        row (SEED / ALWAYS / PRODUCTIVE) overrides it live
//                        (decision 15). Presentation only.
//
// The selector serves every AP race pad that is not a content plan: retail
// race destinations, configured trial tracks and the Cortex Vortex pad track.
// Rows, in order: Trophy Race, CTR Challenge, Relic Race. A row exists when
// the destination has that mode and the pad's gates admit it. A row with no
// open check is grey but can still be chosen (a replay). The cursor starts on
// the first row with an open check.

enum
{
	AP_RS_TROPHY = 0,
	AP_RS_CTR = 1,
	AP_RS_RELIC = 2,
	AP_RS_COUNT = 3
};

enum
{
	AP_RS_STRUCTURE_REFUSED = -1, // ctr_first with a bad award block: pads refuse
	AP_RS_STRUCTURE_CLASSIC = 0,
	AP_RS_STRUCTURE_CTR_FIRST = 1
};

enum
{
	AP_RS_SELECTOR_PRODUCTIVE = 0,
	AP_RS_SELECTOR_ALWAYS = 1
};

// AP_RaceSelectorDecide results besides a mode.
#define AP_RS_OPEN_MENU (-1)
#define AP_RS_NO_ROUTE  (-2)

typedef struct
{
	int present[AP_RS_COUNT];    // the destination has this mode at all
	int productive[AP_RS_COUNT]; // entering it can still produce a check
} ap_rs_facts;

typedef struct
{
	int count;
	int mode[AP_RS_COUNT];
	int productive[AP_RS_COUNT];
	int productiveCount;
	int cursor;
} ap_rs_rows;

// The structure one destination runs. A ctr_first seed runs ctr_first only on
// a destination whose Trophy code is in the award pairs (it has a CTR
// Challenge); every other destination keeps the classic order (decision 4).
// A refused seed stays refused, a classic seed stays classic.
static inline int AP_RaceStructureForDest(int seedStructure, int paired)
{
	if (seedStructure == AP_RS_STRUCTURE_CTR_FIRST && !paired)
		return AP_RS_STRUCTURE_CLASSIC;
	return seedStructure;
}

// Which modes the pad's gates admit, for the destination's own structure
// (AP_RaceStructureForDest). The physical pad's stage 1 (and racer lock) is
// already met when a pad is entered at all. Classic: the Trophy Race first,
// then the CTR Challenge and the Relic Race wait for the checked Trophy and
// the physical pad's stage 2. ctr_first: no Trophy Race row at all (decision
// 14); the CTR Challenge is stage 1 and the Relic Race keeps both conditions,
// now earned through a CTR win.
static inline void AP_RaceStructureAdmit(int structure, int trophyChecked, int stage2Met,
                                         int admitted[AP_RS_COUNT])
{
	int tier2 = trophyChecked && stage2Met;
	int ctrFirst = structure == AP_RS_STRUCTURE_CTR_FIRST;
	admitted[AP_RS_TROPHY] = !ctrFirst;
	admitted[AP_RS_CTR] = ctrFirst ? 1 : tier2;
	admitted[AP_RS_RELIC] = tier2;
}

// Whether a row can still produce a check. `trophyUnchecked` is the Trophy
// Race location; `plainLeft` counts checks any race with AI on the track earns
// (AP boxes, per-track Wumpa, open podium rungs, a Hit guest, an Itemsanity
// use); `ctrSideLeft` is the CTR location or a letter (AP_PadTokenSideLeft);
// `relicSideLeft` the relic tiers or the perfect. Under ctr_first the CTR
// Challenge is the only race before stage 2, so it carries the Trophy and
// every plain-race check.
static inline void AP_RaceStructureProductive(int structure, int trophyUnchecked, int plainLeft,
                                              int ctrSideLeft, int relicSideLeft,
                                              int productive[AP_RS_COUNT])
{
	int ctrFirst = structure == AP_RS_STRUCTURE_CTR_FIRST;
	productive[AP_RS_TROPHY] = !ctrFirst && (trophyUnchecked || plainLeft > 0);
	productive[AP_RS_CTR] = ctrSideLeft || (ctrFirst && (trophyUnchecked || plainLeft > 0));
	productive[AP_RS_RELIC] = relicSideLeft;
}

// Whether a finished race of this kind earns the podium rungs of its track:
// a Trophy Race (cup legs included) always; a CTR Challenge only on a ctr_first
// destination, where it is the track's first race and the rungs follow its
// finishing place, letters or not (decision 14).
static inline int AP_RaceStructureRaceEarnsRungs(int trophyRace, int tokenRace, int destCtrFirst)
{
	return trophyRace || (tokenRace && destCtrFirst);
}

// The selector setting in force: the in-game Options row (-1 SEED, 0
// PRODUCTIVE, 1 ALWAYS) over the seed's value (decision 15).
static inline int AP_RaceSelectorEffective(int menuValue, int seedValue)
{
	if (menuValue == AP_RS_SELECTOR_PRODUCTIVE || menuValue == AP_RS_SELECTOR_ALWAYS)
		return menuValue;
	return seedValue;
}

static inline void AP_RaceSelectorBuild(const ap_rs_facts *f, const int admitted[AP_RS_COUNT],
                                        ap_rs_rows *out)
{
	int m;
	out->count = 0;
	out->productiveCount = 0;
	out->cursor = -1;
	for (m = 0; m < AP_RS_COUNT; m++)
	{
		if (!f->present[m] || !admitted[m])
			continue;
		out->mode[out->count] = m;
		out->productive[out->count] = f->productive[m] ? 1 : 0;
		if (out->productive[out->count])
		{
			if (out->cursor < 0)
				out->cursor = out->count;
			out->productiveCount++;
		}
		out->count++;
	}
	if (out->cursor < 0)
		out->cursor = 0;
}

// The entry decision. `always`: the menu opens whenever a row exists.
// `productive`: exactly one productive row loads directly; several, or none
// with rows to replay, open the menu. No row at all: AP_RS_NO_ROUTE, the
// caller releases the kart.
static inline int AP_RaceSelectorDecide(const ap_rs_rows *rows, int selector)
{
	int i;
	if (rows->count == 0)
		return AP_RS_NO_ROUTE;
	if (selector == AP_RS_SELECTOR_ALWAYS || rows->productiveCount != 1)
		return AP_RS_OPEN_MENU;
	for (i = 0; i < rows->count; i++)
		if (rows->productive[i])
			return rows->mode[i];
	return AP_RS_OPEN_MENU;
}

// Grey-but-selectable rows as a bit mask over row indices (RECTMENU replay).
static inline unsigned AP_RaceSelectorReplayMask(const ap_rs_rows *rows)
{
	unsigned mask = 0;
	int i;
	for (i = 0; i < rows->count; i++)
		if (!rows->productive[i])
			mask |= 1u << i;
	return mask;
}

// ── award pairs (`race_structure.stage1_awards`) ────────────────────────────
#define AP_RS_AWARD_MAX 64

typedef struct
{
	int count;
	long ctr[AP_RS_AWARD_MAX];
	long trophy[AP_RS_AWARD_MAX];
} ap_rs_awards;

// The Trophy code a qualifying win of CTR code `ctr` also sends, or -1.
static inline long AP_RaceStructurePairedTrophy(const ap_rs_awards *a, long ctr)
{
	int i;
	if (a == 0 || ctr <= 0)
		return -1;
	for (i = 0; i < a->count; i++)
		if (a->ctr[i] == ctr)
			return a->trophy[i];
	return -1;
}

// Whether `trophy` is earned by a CTR win (so a plain Trophy Race win must not
// send it).
static inline int AP_RaceStructureTrophyIsStage1Ctr(const ap_rs_awards *a, long trophy)
{
	int i;
	if (a == 0 || trophy <= 0)
		return 0;
	for (i = 0; i < a->count; i++)
		if (a->trophy[i] == trophy)
			return 1;
	return 0;
}

// How a destination's own CTR and Trophy codes sit in the pairs: 1 when they
// are one pair, 0 when neither is paired (a classic track, decision 4), -1
// when they cross (its CTR pairs with another Trophy, or its Trophy with
// another CTR). The apworld never emits a crossed pair; the retail result
// path sends the track's own Trophy, so a crossed room fails closed.
static inline int AP_RaceStructurePairState(const ap_rs_awards *a, long ctr, long trophy)
{
	long paired = AP_RaceStructurePairedTrophy(a, ctr);
	int trophyPaired = AP_RaceStructureTrophyIsStage1Ctr(a, trophy);
	if (paired <= 0 && !trophyPaired)
		return 0;
	if (trophy > 0 && paired == trophy)
		return 1;
	return -1;
}

// Validate a parsed pair list: 1..AP_RS_AWARD_MAX rows, positive codes, no
// code used twice in either column or across them. Returns 0 when valid, else
// a short reason.
static inline const char *AP_RaceStructureAwardsProblem(const ap_rs_awards *a)
{
	int i, k;
	if (a->count <= 0)
		return "no award pairs";
	if (a->count > AP_RS_AWARD_MAX)
		return "too many award pairs";
	for (i = 0; i < a->count; i++)
	{
		if (a->ctr[i] <= 0 || a->trophy[i] <= 0 || a->ctr[i] == a->trophy[i])
			return "an award pair has a bad code";
		for (k = 0; k < i; k++)
			if (a->ctr[k] == a->ctr[i] || a->trophy[k] == a->trophy[i] ||
			    a->ctr[k] == a->trophy[i] || a->trophy[k] == a->ctr[i])
				return "an award code repeats";
	}
	return 0;
}

#endif // AP_RACE_STRUCTURE_LOGIC_H
