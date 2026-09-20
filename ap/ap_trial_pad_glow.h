#ifndef AP_TRIAL_PAD_GLOW_H
#define AP_TRIAL_PAD_GLOW_H

// Hub pad display identities for the Slide Coliseum and Turbo Track Trophy and
// CTR Challenge checks (issue #343).
//
// Those two checks have no AdvProgress bit: their codes travel only in
// ctr_cfg.trial_track_locations. The pad glow is keyed by bit, so without an
// identity here a trial pad could never show what is placed on its Trophy or
// CTR Challenge. These are process-local keys above the podium rung range and
// the custom-track pseudo-bits, never wire values. AP_LookupLocationCode turns
// them into the seed's location codes, so the reward model, tint, ghost and
// checked-state helpers work on them unchanged.

#include "ap_seedcfg.h"

#define AP_TRIAL_PSEUDO_BASE 0x210 // + challenge * trial count + trial

static inline int AP_TrialPseudoBit(int trial, int challenge)
{
	return AP_TRIAL_PSEUDO_BASE + challenge * CTR_CFG_TRIAL_TRACK_COUNT + trial;
}

// 1 when `bit` is a trial pseudo-bit, with trial (0 Slide Coliseum, 1 Turbo
// Track) and challenge (CTR_CFG_TRIAL_TROPHY / CTR_CFG_TRIAL_CTR) filled in.
static inline int AP_TrialPseudoDecode(int bit, int *outTrial, int *outChallenge)
{
	int off = bit - AP_TRIAL_PSEUDO_BASE;

	if (off < 0 || off >= CTR_CFG_TRIAL_TRACK_COUNT * CTR_CFG_TRIAL_CHECK_COUNT)
		return 0;
	if (outTrial)
		*outTrial = off % CTR_CFG_TRIAL_TRACK_COUNT;
	if (outChallenge)
		*outChallenge = off / CTR_CFG_TRIAL_TRACK_COUNT;
	return 1;
}

// Prize slot that owns a trial pseudo-bit under by_reward_type: the Trophy
// rides in the race slot with its podium rungs, the CTR Challenge in the token
// slot, the same slots a retail track's Trophy and CTR Token use. -1 when `bit`
// is not a trial pseudo-bit.
static inline int AP_TrialPseudoRewardGroup(int bit)
{
	int challenge;

	if (!AP_TrialPseudoDecode(bit, 0, &challenge))
		return -1;
	return challenge == CTR_CFG_TRIAL_CTR ? 2 : 0;
}

typedef int (*ap_trial_code_checked)(long code, void *ctx);

// Append the still-unchecked Trophy and CTR Challenge identities of one trial.
// `codes` is that trial's trial_track_locations row; a code <= 0 is a check
// this seed did not create. The predicate is the one AP_PadState counted these
// two checks with before #343, so moving them into the glow enumeration keeps
// the pad lifecycle count identical.
static inline int AP_TrialPadAppendUnchecked(const long *codes, int trial,
                                             int *outBits, int cap, int count,
                                             ap_trial_code_checked checked,
                                             void *ctx)
{
	int challenge;

	if (codes == 0 || outBits == 0 || checked == 0 || trial < 0 ||
	    trial >= CTR_CFG_TRIAL_TRACK_COUNT || count < 0)
		return count;
	for (challenge = 0; challenge < CTR_CFG_TRIAL_CHECK_COUNT && count < cap;
	     challenge++)
	{
		if (codes[challenge] > 0 && !checked(codes[challenge], ctx))
			outBits[count++] = AP_TrialPseudoBit(trial, challenge);
	}
	return count;
}

#endif // AP_TRIAL_PAD_GLOW_H
