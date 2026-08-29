#ifndef AP_WUMPA_DISPATCH_H
#define AP_WUMPA_DISPATCH_H

// Which AP location code a 10-wumpa crossing sends, and why (2026-08-29
// specification, Lane A). Freestanding on the ap_cup_box_policy.h pattern: the
// gather lives in engine (ap_hooks.c AP_WumpaReachedTen), the DECISION lives
// here so tools/test-wumpa-dispatch.c can pin the whole truth table with no
// disc, no display and no seed.
//
// THE RULE, in one sentence: the wire says which codes exist, the runtime says
// which destination is being raced, and a crossing sends the code those two
// agree on or it sends nothing at all.
//
// SIX STEPS, in the specification's order:
//   1. off              -> nothing.
//   2. global           -> the one global code, whatever is loaded. Its identity
//                          is deliberately not tied to a destination, so it is
//                          valid on any race where fruit can be collected --
//                          including a custom track.
//   3. per_track retail -> the DESTINATION level's code. Destination, not
//                          physical pad: loading Crash Cove from another pad
//                          still sends Crash Cove's code.
//   4. cup leg          -> the destination's own physical pad must be accessible
//                          right now, the same AP_BoxPadAccessible answer the
//                          AP-box policy uses. Cup access alone grants nothing.
//   5. per_track custom -> resolve the serving package and destination role
//                          through the predicate that owns custom bytes, and
//                          send the bound destination-slot code only when the
//                          measured wumpa capability is true on BOTH the wire
//                          and the seed's own descriptor, and the package
//                          identities match.
//   6. anything unclear -> refuse. There is deliberately no fall-back to the
//                          host retail level's code: a custom track borrowing
//                          arcade slot 6 is not Roo's Tubes, and sending Roo's
//                          Tubes' check for it would be a wrong-content send the
//                          player cannot undo.
//
// The caller ANDs server location membership and the ordinary dedup path on top
// of whatever this returns. That has not changed: membership was the only gate
// before per-track and it is still the last one.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

#include "ap_seedcfg.h"

// Why a crossing sent nothing. Logged rather than swallowed: a player whose
// per-track check did not fire needs to be able to tell "this seed has no check
// for this track" from "the client refused the content it was handed".
enum AP_WumpaRefusal
{
	AP_WUMPA_SENT = 0,             // a code was resolved
	AP_WUMPA_REFUSE_MODE_OFF,      // no Wumpa checks in this seed
	AP_WUMPA_REFUSE_NO_GLOBAL,     // global mode with no global code on the wire
	AP_WUMPA_REFUSE_UNKNOWN_LEVEL, // destination is not a retail race level 0..17
	AP_WUMPA_REFUSE_NO_TRACK_CODE, // this seed minted no check for that level
	AP_WUMPA_REFUSE_CUP_PAD,       // cup leg, that track's own pad is not open
	AP_WUMPA_REFUSE_NO_CUSTOM_SLOT,// no destination slot for the serving cup
	AP_WUMPA_REFUSE_CUSTOM_PACKAGE,// slot names a different package than the seed
	AP_WUMPA_REFUSE_NOT_COLLECTIBLE,// the measured capability says no route to 10
	AP_WUMPA_REFUSE_CAPABILITY_DISAGREE // wire and descriptor disagree
};

// Everything the decision reads, gathered by the caller. Kept as one struct so
// the harness can build a case as data and so adding a term is a compile error
// at every gather site rather than a silently defaulted zero.
struct AP_WumpaDispatchFacts
{
	const ctr_wumpa_checks *wumpa; // this seed's parsed block, never NULL

	// ── retail ──
	int destLevelID; // the DESTINATION track being raced, 0..17, or -1
	int isCupLeg;    // this race is an Adventure Gem Cup leg
	int padAccessible; // AP_BoxPadAccessible for the destination's physical pad

	// ── custom ──
	int servingCustom;   // CustomTrack_ServingLoad said the custom bytes are live
	int servingCupLevelID; // the Gem Cup LevelID that load is running under
	// The seed's own custom_tracks descriptor, for the cross-check. `ok` is
	// ctr_cfg.custom_tracks_ok: 0 means there is no usable descriptor at all.
	int         seedCustomOk;
	const char *seedPackageUuid;
	int         seedWumpaCollectible;
};

// ASCII case-insensitive equality. UUIDs are hex-and-dashes, and the apworld
// lower-cases them while a hand-written config may not, so the comparison has to
// be case-blind to be honest about identity rather than about spelling.
static inline int AP_WumpaTextEqualsFold(const char *a, const char *b)
{
	if (a == 0 || b == 0)
		return 0;
	for (; *a != '\0' && *b != '\0'; a++, b++)
	{
		int ca = (*a >= 'A' && *a <= 'Z') ? *a + ('a' - 'A') : *a;
		int cb = (*b >= 'A' && *b <= 'Z') ? *b + ('a' - 'A') : *b;
		if (ca != cb)
			return 0;
	}
	return *a == '\0' && *b == '\0';
}

// The destination slot bound to `cupLevelID`, or NULL. Linear because the array
// holds one entry today and would hold a handful at most; a map would be more
// code than the thing it indexes.
static inline const ctr_wumpa_custom_destination *AP_WumpaCustomSlot(
    const ctr_wumpa_checks *wumpa, int cupLevelID)
{
	int i;

	if (wumpa == 0 || cupLevelID < 100 || cupLevelID > 104)
		return 0;
	for (i = 0; i < wumpa->custom_count && i < CTR_CFG_WUMPA_CUSTOM_MAX; i++)
		if (wumpa->custom[i].cup_level_id == cupLevelID &&
		    wumpa->custom[i].code >= 0)
			return &wumpa->custom[i];
	return 0;
}

// THE decision. Returns the location code to send, or -1, and always writes a
// reason so the caller can log one line whichever way it went.
static inline long AP_WumpaResolveCode(const struct AP_WumpaDispatchFacts *f,
                                       int *outReason)
{
	const ctr_wumpa_custom_destination *slot;
	int reason = AP_WUMPA_SENT;
	long code = -1;

	if (f == 0 || f->wumpa == 0)
	{
		if (outReason != 0)
			*outReason = AP_WUMPA_REFUSE_MODE_OFF;
		return -1;
	}

	switch (f->wumpa->mode)
	{
	case CTR_CFG_WUMPA_GLOBAL:
		// Step 2. One location for the seed, valid on any race where fruit can
		// be collected -- a custom track included, because the global identity
		// is deliberately not a destination's.
		code = f->wumpa->global_code;
		if (code < 0)
			reason = AP_WUMPA_REFUSE_NO_GLOBAL;
		break;

	case CTR_CFG_WUMPA_PER_TRACK:
		if (f->servingCustom)
		{
			// Step 5. The custom destination, resolved through the same cup
			// identity the serving predicate answered for.
			slot = AP_WumpaCustomSlot(f->wumpa, f->servingCupLevelID);
			if (slot == 0)
				reason = AP_WUMPA_REFUSE_NO_CUSTOM_SLOT;
			else if (!f->seedCustomOk ||
			         !AP_WumpaTextEqualsFold(slot->package_uuid,
			                                 f->seedPackageUuid))
				// Step 6. The slot names a package this seed is not serving.
				// Refuse; never reach for the host retail level's code.
				reason = AP_WUMPA_REFUSE_CUSTOM_PACKAGE;
			else if (slot->wumpa_collectible != f->seedWumpaCollectible)
				// The wire's `wumpa_checks` block and the seed's own
				// `custom_tracks` descriptor disagree about a MEASURED
				// capability. One of them is wrong and nothing here can tell
				// which, so neither is believed.
				reason = AP_WUMPA_REFUSE_CAPABILITY_DISAGREE;
			else if (!slot->wumpa_collectible)
				reason = AP_WUMPA_REFUSE_NOT_COLLECTIBLE;
			else
				code = slot->code;
			break;
		}

		// Steps 3 and 4. Retail content.
		if (f->destLevelID < 0 || f->destLevelID >= CTR_CFG_WUMPA_TRACK_COUNT)
			reason = AP_WUMPA_REFUSE_UNKNOWN_LEVEL;
		else if (f->wumpa->tracks[f->destLevelID] < 0)
			reason = AP_WUMPA_REFUSE_NO_TRACK_CODE;
		else if (f->isCupLeg && !f->padAccessible)
			// The AP-box ruling, applied to this check: a cup leg may collect
			// fruit on a track the seed's logic cannot yet reach through that
			// track's own physical pad, and awarding the check there would hand
			// out a location the logic does not believe is reachable.
			reason = AP_WUMPA_REFUSE_CUP_PAD;
		else
			code = f->wumpa->tracks[f->destLevelID];
		break;

	default:
		// Step 1, and every unknown mode. An unknown mode is a seed from a build
		// this client does not understand; sending nothing is the only answer
		// that cannot be wrong.
		reason = AP_WUMPA_REFUSE_MODE_OFF;
		break;
	}

	if (code < 0 && reason == AP_WUMPA_SENT)
		reason = AP_WUMPA_REFUSE_MODE_OFF;
	if (outReason != 0)
		*outReason = reason;
	return code;
}

// One short phrase per refusal, for the log line. Present for every enumerator
// so a future addition shows up as a compiler warning here rather than as an
// empty log message in a player's support bundle.
static inline const char *AP_WumpaRefusalText(int reason)
{
	switch (reason)
	{
	case AP_WUMPA_SENT: return "sent";
	case AP_WUMPA_REFUSE_MODE_OFF: return "this seed has no Wumpa checks";
	case AP_WUMPA_REFUSE_NO_GLOBAL: return "global mode carries no global code";
	case AP_WUMPA_REFUSE_UNKNOWN_LEVEL: return "not a retail race destination";
	case AP_WUMPA_REFUSE_NO_TRACK_CODE: return "no check minted for this destination";
	case AP_WUMPA_REFUSE_CUP_PAD: return "cup leg, this track's own pad is not open yet";
	case AP_WUMPA_REFUSE_NO_CUSTOM_SLOT: return "no custom destination slot for this cup";
	case AP_WUMPA_REFUSE_CUSTOM_PACKAGE: return "custom destination names a different package";
	case AP_WUMPA_REFUSE_NOT_COLLECTIBLE: return "this package measured no route to 10 fruit";
	case AP_WUMPA_REFUSE_CAPABILITY_DISAGREE: return "wire and descriptor disagree on wumpa_collectible";
	default: return "unknown reason";
	}
}

#endif // CTR_AP
#endif // AP_WUMPA_DISPATCH_H
