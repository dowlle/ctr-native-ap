#ifndef NATIVE_CUSTOM_RACE_CONTEXT_H
#define NATIVE_CUSTOM_RACE_CONTEXT_H

#include <stdint.h>
#include <string.h>

// A standalone AP race owns a logical route independently of its engine host.
// Value copies keep a pending load/restart independent of mutable seed tables.
// Package bytes must be admitted before staging this context.
struct CustomTrackRaceContext
{
	int hostLevelID;
	int physicalPad;
	int returnHub;
	int slot;
	int laps;
	int retail; // New content-plan retail routes also own a standalone context.
	uint64_t seedEpoch;
	int64_t trophyLocation;
	char entryID[65];
	char trackID[65];
	char levSha256[65];
	char vrmSha256[65];
	int crystal, timeLimitTicks;
	// Content-plan track mode (#133 PR 3): 0 Trophy Race, 1 CTR Challenge,
	// 2 Relic Race. trophyLocation is then the mode's route code (Trophy,
	// CTR, or the track's frozen Sapphire code, which the seed need not hold
	// as a check). Crystal Challenges use 0.
	int mode;
	// Content-plan Cortex Vortex (#133 PR 3c): the bundled pair raced as a
	// custom-bytes standalone race on host 13, with no custom slot.
	int cortex;
	// Content-plan cup (#133 PR 5): a leg of a four-leg cup. entryID is the
	// cup's entry, trackID the leg's track, trophyLocation the cup's Win and
	// laps this leg's laps. The context moves to the next leg at each leg
	// advance (CustomRaceLatch_StageNext) and survives a restart.
	int cup, leg;
};

struct CustomTrackRaceLatch
{
	struct CustomTrackRaceContext pending, current, previous;
	int pendingKind; // 0 none, 1 explicit retail, 2 admitted custom
	int active;
	int previousValid;
	int lastRequested;
	uint64_t generation; // increments on a new custom entry, not on restart
};

static inline int CustomRaceContext_IsPad(int id)
{
	return (id >= 0 && id <= 19) || id == 21 || id == 23 ||
	       (id >= 100 && id <= 104) ||
	       (id >= 1000 && id <= 1003); /* boss cup garages, AP_CONTENT_GARAGE_CUP_PAD (#133 PR 6b) */
}

static inline int CustomRaceContext_Key(const char *text, size_t size)
{
	size_t i;
	for (i = 0; i < size && text[i]; ++i)
	{
		unsigned char c = (unsigned char)text[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || strchr("_.:/+-", c))) return 0;
	}
	return i > 0 && i < size;
}

static inline int CustomRaceContext_Hash(const char text[65])
{
	int i;
	for (i = 0; i < 64; ++i)
		if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f')))
			return 0;
	return text[64] == 0;
}

static inline int CustomRaceContext_Valid(const struct CustomTrackRaceContext *c)
{
	return c && ((c->crystal == 0 && c->timeLimitTicks == 0 && c->hostLevelID >= 0 && c->hostLevelID < 18 &&
	              c->mode >= 0 && c->mode <= 2) ||
               (c->crystal == 1 && c->retail == 1 && c->hostLevelID >= 18 && c->hostLevelID <= 24 && c->mode == 0 &&
                c->laps == 1 && c->seedEpoch && c->timeLimitTicks > 0 && c->timeLimitTicks <= 576000)) &&
	       CustomRaceContext_IsPad(c->physicalPad) &&
	       c->returnHub >= 25 && c->returnHub <= 29 &&
	       (c->retail == 0 || c->retail == 1) && (c->cortex == 0 || c->cortex == 1) &&
	       (!c->cortex || (!c->retail && !c->crystal && c->hostLevelID == 13)) &&
	       (c->cup == 0 || c->cup == 1) && c->leg >= 0 && c->leg <= 3 && (c->cup || c->leg == 0) &&
	       (!c->cup || (!c->crystal && c->mode == 0)) &&
	       ((c->retail || c->cortex) ? c->slot == 0 : (c->slot >= 1 && c->slot <= 132)) &&
	       c->laps >= 1 && c->laps <= 7 &&
	       c->trophyLocation > 0 && c->trophyLocation <= INT64_C(9007199254740991) &&
	       CustomRaceContext_Key(c->entryID, sizeof c->entryID) &&
	       CustomRaceContext_Key(c->trackID, sizeof c->trackID) &&
	       (c->retail ? (!c->levSha256[0] && !c->vrmSha256[0]) :
	        (CustomRaceContext_Hash(c->levSha256) && CustomRaceContext_Hash(c->vrmSha256)));
}

static inline int CustomRaceLatch_Stage(struct CustomTrackRaceLatch *l,
	                                    const struct CustomTrackRaceContext *c)
{
	if (!l || !CustomRaceContext_Valid(c) || l->active || l->pendingKind)
		return 0;
	l->pending = *c;
	l->pendingKind = 2;
	return 1;
}

// The next leg of the running cup (#133 PR 5): only while the current race
// is a cup leg of the same seed, cup entry and pad, and only for the next leg.
// The latch stays active; the next load request makes the staged leg current.
static inline int CustomRaceLatch_StageNext(struct CustomTrackRaceLatch *l,
	                                        const struct CustomTrackRaceContext *c)
{
	if (!l || !CustomRaceContext_Valid(c) || !l->active || l->pendingKind || !l->current.cup || !c->cup ||
	    c->seedEpoch != l->current.seedEpoch || c->physicalPad != l->current.physicalPad ||
	    c->leg != l->current.leg + 1 || strcmp(c->entryID, l->current.entryID) ||
	    c->trophyLocation != l->current.trophyLocation)
		return 0;
	l->pending = *c;
	l->pendingKind = 2;
	return 1;
}

// All explicitly retail entry sites must select retail, including a retail
// race on the same engine host. No selection on that host means a restart.
static inline void CustomRaceLatch_Retail(struct CustomTrackRaceLatch *l)
{
	l->pendingKind = 1;
}

static inline void CustomRaceLatch_OnRequest(struct CustomTrackRaceLatch *l, int host)
{
	if (!l->pendingKind && host == l->lastRequested)
		return;
	l->previousValid = l->active;
	if (l->active)
	{
		l->previous = l->current;
		l->previousValid = 1;
	}
	l->active = l->pendingKind == 2 && l->pending.hostLevelID == host;
	if (l->active)
	{
		l->current = l->pending;
		++l->generation;
	}
	l->pendingKind = 0;
	l->lastRequested = host;
}

// Identity does not depend on verification: a failed custom load must never
// be reclassified as the retail host for rewards, boxes or navigation.
static inline int CustomRaceLatch_Intent(const struct CustomTrackRaceLatch *l, int host)
{
	return l->active && l->current.hostLevelID == host;
}

#endif
