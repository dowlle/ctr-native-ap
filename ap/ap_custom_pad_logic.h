#ifndef AP_CUSTOM_PAD_LOGIC_H
#define AP_CUSTOM_PAD_LOGIC_H

// Freestanding custom-destination lifecycle helpers.
//
// A custom track borrows a Gem Cup's engine LevelID, but none of that cup's
// retail location identities.  The hub pad therefore needs a small identity
// bridge for its generic Trophy, podium and per-destination Wumpa checks.  Keep
// the bridge pure so the complete before/after lifecycle is regression-tested
// without booting the engine or inventing AdvProgress bits for custom content.

#include "ap_seedcfg.h"
#include "ap_wumpa_dispatch.h"

// All 16 retail podium tracks followed by the 32 frozen custom slots.  A custom
// slot N uses logical track CTR_CFG_PODIUM_TRACK_COUNT + N - 1, matching the
// race listener and reconciliation paths.
#define AP_PODIUM_LOGICAL_TRACK_COUNT \
	(CTR_CFG_PODIUM_TRACK_COUNT + CTR_CFG_CT_SLOT_COUNT)

// Pseudo identities live above the real 192-bit AdvProgress space.  Podium
// rungs occupy [0x100, 0x1ef]; the two direct custom checks sit after that
// range.  These are process-local display/lifecycle keys, never wire values.
#define AP_PODIUM_PSEUDO_BASE          0x100
#define AP_CUSTOM_TROPHY_PSEUDO_BIT    0x200
#define AP_CUSTOM_WUMPA_PSEUDO_BIT     0x201

// A selected custom destination can be entered only while the manager is
// Ready and no serve-time fault remains latched. Rescan clears the latter only
// after it has verified and re-armed the package.
static inline int AP_CustomPadContentReady(int seedSelected, int contentRequired,
	                                       const char *serveFaultReason)
{
	return seedSelected && !contentRequired && serveFaultReason == 0;
}

static inline int AP_CustomPadOwnsDestination(const ctr_seed_config *cfg,
	                                           int destLevelID)
{
	return cfg != 0 && cfg->custom_tracks_ok &&
	       cfg->custom_track.slot >= 1 &&
	       cfg->custom_track.slot <= CTR_CFG_CT_SLOT_COUNT &&
	       cfg->custom_track.replaces_cup_level_id == destLevelID;
}

static inline int AP_CustomPadPodiumTrack(const ctr_seed_config *cfg,
	                                      int destLevelID)
{
	if (!AP_CustomPadOwnsDestination(cfg, destLevelID))
		return -1;
	return CTR_CFG_PODIUM_TRACK_COUNT + cfg->custom_track.slot - 1;
}

static inline const ctr_podium_rungs *AP_PodiumRungsForLogicalTrack(
	const ctr_seed_config *cfg, int track)
{
	if (cfg == 0 || !cfg->podium_enabled || track < 0)
		return 0;
	if (track < CTR_CFG_PODIUM_TRACK_COUNT)
		return &cfg->podium[track];
	if (cfg->custom_tracks_ok && cfg->custom_track.slot >= 1 &&
	    cfg->custom_track.slot <= CTR_CFG_CT_SLOT_COUNT &&
	    track == CTR_CFG_PODIUM_TRACK_COUNT + cfg->custom_track.slot - 1)
		return &cfg->custom_track.podium;
	return 0;
}

static inline long AP_PodiumPseudoLocationCode(const ctr_seed_config *cfg,
	                                           int globalBit)
{
	int off;
	int track;
	int rung;
	const ctr_podium_rungs *pr;

	if (globalBit < AP_PODIUM_PSEUDO_BASE ||
	    globalBit >= AP_PODIUM_PSEUDO_BASE +
	                 AP_PODIUM_LOGICAL_TRACK_COUNT * CTR_CFG_PODIUM_RUNG_COUNT)
		return -1;
	off = globalBit - AP_PODIUM_PSEUDO_BASE;
	track = off / CTR_CFG_PODIUM_RUNG_COUNT;
	rung = off % CTR_CFG_PODIUM_RUNG_COUNT;
	pr = AP_PodiumRungsForLogicalTrack(cfg, track);
	if (pr == 0)
		return -1;
	switch (rung)
	{
	case 0:  return pr->held_1st > 0 ? pr->held_1st : -1;
	case 1:  return pr->held_3rd > 0 ? pr->held_3rd : -1;
	case 2:  return pr->held_5th > 0 ? pr->held_5th : -1;
	case 3:  return pr->finish_podium > 0 ? pr->finish_podium : -1;
	default: return pr->finish_any > 0 ? pr->finish_any : -1;
	}
}

// Resolve the per-destination Wumpa code under the same identity and measured-
// capability checks as the live send path.  A global Wumpa check is deliberately
// not owned by any one pad and therefore does not participate here.
static inline long AP_CustomPadWumpaLocationCode(const ctr_seed_config *cfg,
	                                             int destLevelID)
{
	const ctr_wumpa_custom_destination *slot;
	if (!AP_CustomPadOwnsDestination(cfg, destLevelID) ||
	    cfg->wumpa.mode != CTR_CFG_WUMPA_PER_TRACK)
		return -1;
	slot = AP_WumpaCustomSlot(&cfg->wumpa, destLevelID);
	if (slot == 0 ||
	    !AP_WumpaTextEqualsFold(slot->package_uuid,
	                            cfg->custom_track.package_uuid) ||
	    slot->wumpa_collectible != cfg->custom_track.flags.wumpa_collectible ||
	    !slot->wumpa_collectible)
		return -1;
	return slot->code > 0 ? slot->code : -1;
}

static inline long AP_CustomPadSpecialLocationCode(const ctr_seed_config *cfg,
	                                               int globalBit)
{
	if (globalBit == AP_CUSTOM_TROPHY_PSEUDO_BIT)
		return cfg != 0 &&
		       AP_CustomPadOwnsDestination(
		           cfg, cfg->custom_track.replaces_cup_level_id) &&
			       cfg->custom_track.trophy_location > 0
			       ? cfg->custom_track.trophy_location : -1;
	if (globalBit == AP_CUSTOM_WUMPA_PSEUDO_BIT)
		return cfg != 0
			       ? AP_CustomPadWumpaLocationCode(
			             cfg, cfg->custom_track.replaces_cup_level_id)
			       : -1;
	return -1;
}

typedef int (*ap_custom_pad_location_query)(long code, void *ctx);

// Append every unchecked identity owned by this custom destination.  The plain
// tier enumerator asks for Trophy + Wumpa only; the lifecycle/glow enumerator
// sets includePodium and gets the enabled rungs too.  Production and the
// regression harness therefore exercise the same exact gather rather than two
// plausible copies of it.
static inline int AP_CustomPadAppendUnchecked(
	const ctr_seed_config *cfg, int destLevelID, int includePodium,
	int *outBits, int cap, int count,
	ap_custom_pad_location_query exists,
	ap_custom_pad_location_query checked, void *ctx)
{
	int bits[2 + CTR_CFG_PODIUM_RUNG_COUNT];
	int n = 0;
	int i;
	int track;

	if (!AP_CustomPadOwnsDestination(cfg, destLevelID) || outBits == 0 ||
	    cap <= 0 || count < 0 || count >= cap || exists == 0 || checked == 0)
		return count;
	bits[n++] = AP_CUSTOM_TROPHY_PSEUDO_BIT;
	bits[n++] = AP_CUSTOM_WUMPA_PSEUDO_BIT;
	track = AP_CustomPadPodiumTrack(cfg, destLevelID);
	if (includePodium && track >= 0)
		for (i = 0; i < CTR_CFG_PODIUM_RUNG_COUNT; i++)
			bits[n++] = AP_PODIUM_PSEUDO_BASE +
			            track * CTR_CFG_PODIUM_RUNG_COUNT + i;

	for (i = 0; i < n && count < cap; i++)
	{
		long code = bits[i] >= AP_PODIUM_PSEUDO_BASE &&
		            bits[i] < AP_CUSTOM_TROPHY_PSEUDO_BIT
		                ? AP_PodiumPseudoLocationCode(cfg, bits[i])
		                : AP_CustomPadSpecialLocationCode(cfg, bits[i]);
		if (code > 0 && exists(code, ctx) && !checked(code, ctx))
			outBits[count++] = bits[i];
	}
	return count;
}

#endif // AP_CUSTOM_PAD_LOGIC_H
