// ap_hooks_pads.c -- part of the ap_hooks unit (issue #305).
// Warp pads: the unified pad state (Warp-Pad State Model v2), trial and Cortex
// Vortex reward helpers, plan-owned glow, box counts and entry diagnostics.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// Content-plan pad lifecycle and reward display. Physical IDs own both gates
// and locations; the loaded engine track does not select a retail seed row.
static int AP_PadBoxLive(long code, void *ctx);
static int AP_PadBoxChecked(long code, void *ctx);

static long AP_TrialTrackLocation(int levelID, int challenge)
{
	int track = levelID - 16;
	if (!ctr_cfg_active() || track < 0 || track >= CTR_CFG_TRIAL_TRACK_COUNT ||
	    challenge < 0 || challenge >= CTR_CFG_TRIAL_CHECK_COUNT ||
	    !ctr_cfg.trial_track_valid[track])
		return -1;
	return ctr_cfg.trial_track_locations[track][challenge];
}

int AP_TrialTrackConfigured(int levelID)
{
	int track = levelID - 16;
	return ctr_cfg_active() && track >= 0 && track < CTR_CFG_TRIAL_TRACK_COUNT &&
	       ctr_cfg.trial_track_valid[track];
}

int AP_TrialTrackLocationChecked(int levelID, int challenge)
{
	long code = AP_TrialTrackLocation(levelID, challenge);
	return code > 0 && ap_net_location_checked(code);
}

// One feed popup per session when the trial CTR Challenges cannot be offered
// because the retail letter assets were refused (ruling 2026-10-09). Waits for
// the BIGFILE directory: before it is read, AP_TrialLetters_Prepare returns 0
// without having tried.
static void AP_TrialLettersRefusedNotice(void)
{
	static int s_shown;

	if (s_shown || !sdata->ptrBigfile1)
		return;
	s_shown = 1;
	AP_FeedNoticeLine("TRIAL CTR CHALLENGES OFF:");
	AP_FeedNoticeLine("LETTER ASSETS NOT FOUND");
}

// Can this trial destination serve its CTR Challenge? It needs the seed's
// Trophy+CTR mode, a placed CTR Challenge location and the retail letter assets
// (AP_TrialLetters_Prepare, which latches its answer after the first read).
// The pad lifecycle (letter count, glow) and the entry route both ask this, so
// a refused asset load cannot leave the pad open for checks its entry route
// will never offer. Same predicate the CTR Challenge spawn path gates on.
int AP_TrialChallengeServable(int destLevelID)
{
	int track = destLevelID - 16;

	if (!AP_TrialTrackConfigured(destLevelID))
		return 0;
	if (ctr_cfg.trial_track_mode[track] < 2 ||
	    ctr_cfg.trial_track_locations[track][CTR_CFG_TRIAL_CTR] <= 0)
		return 0;
	if (AP_TrialLetters_Prepare())
		return 1;
	AP_TrialLettersRefusedNotice();
	return 0;
}

// ── Cortex Vortex pad track (schema 15) ─────────────────────────────────────
// The identity half is the loader's latch (platform/native_cortex_track_latch.h);
// the code half is ctr_cfg.cortex_track. A build without the custom-track
// loader can never serve the track, so the identity is constantly 0 there and
// every pad hosting 110 refuses entry (AP_CortexTrackEntryReady).
int AP_CortexTrackActive(void)
{
#ifdef CTR_CUSTOM_TRACKS
	struct GameTracker *gGT = sdata ? sdata->gGT : 0;
	if (gGT == 0)
		return 0;
	return CustomTrack_CortexTrackIntent((int)gGT->levelID,
	                                     (gGT->gameMode1 & ADVENTURE_BOSS) != 0);
#else
	return 0;
#endif
}

int AP_CortexTrackBit(int slot)
{
	return AP_CortexPseudoBit(slot);
}

static long AP_CortexTrackCode(int slot)
{
	return ctr_cfg_active() ? AP_CortexSlotCode(&ctr_cfg.cortex_track, slot) : -1;
}

int AP_CortexTrackChecked(int slot)
{
	long code = AP_CortexTrackCode(slot);
	return code > 0 && ap_net_location_checked(code);
}

int AP_CortexTrackPrepareLoad(int destLevelID)
{
	int cortex = 0;
	int level = CortexTrackLatch_ResolveDest(destLevelID, &cortex);
#ifdef CTR_CUSTOM_TRACKS
	CustomTrack_CortexTrackSelectNextLoad(cortex);
#endif
	if (cortex)
	{
		char msg[96];
		snprintf(msg, sizeof msg,
		         "[AP CORTEX] loading destination 110 on host level %d\n", level);
		AP_AppendLog(msg);
	}
	return level;
}

// Server checked-state of a content-plan code, the callback the pure
// track-mode decisions (ap_content_modes.h) take.
int AP_ContentCodeChecked(int64_t code)
{
	return code > 0 && ap_net_location_checked((long)code);
}

int AP_RelicTimeFor(int levelID, int tier)
{
	if (tier < 0 || tier > 2)
		tier = 0;
#ifdef CTR_CUSTOM_TRACKS
	{
		// A content-plan Relic Race shows its own track's targets (#133 PR 3).
		int target = AP_StandaloneRelicTarget(tier);
		if (target > 0)
			return target;
	}
#endif
	if (AP_CortexTrackActive())
		return AP_CortexRelicTime(tier);
	if (levelID < 0 || levelID >= 18)
		return data.RelicTime[tier];
	return data.RelicTime[levelID * 3 + tier];
}

const char *AP_CortexTrackDisplayName(void)
{
	return AP_CortexTrackActive() ? "CORTEX VORTEX" : 0;
}

// Content-plan pad enumeration. openOnly hides the checks of a mode still locked
// behind stage 2 (decision 26, review finding 11); the pad's glow asks for that,
// while the pad state and the tracker count every open location, locked or not.
static int AP_GlowPush(int *outBits, int64_t *outCodes, int count, int bit, int64_t code)
{
	outBits[count] = bit;
	if (outCodes) outCodes[count] = code;
	return count + 1;
}

// outCodes, when not NULL, gets the location code of each listed bit at the
// same index, so Pad Approach Hints (#124) hint exactly what the glow lists.
static int AP_ContentPadGlowBits(int destLevelID, int *outBits, int64_t *outCodes, int cap, int openOnly)
{
		const ctr_content_pad *pad = ap_content_plan_pad(destLevelID);
        int count=0,n;
        if(!pad || !pad->occupied || !outBits || cap<=0)return 0;
        if(pad->cup) {
            // Cups (#133 PR 5): the Win, the sweep and each leg track's Cup
            // Leg Victory once, then each track's first lap, Wumpa check and
            // boxes once. A leg breaks a box only while the track's own pad
            // is open, so the glow keeps a shut pad's boxes hidden.
            int64_t codes[6]; int slots[6], k, c=ap_content_cup_codes(pad,codes,slots);
            ap_content_cup_track_check rows[AP_CONTENT_CUP_TRACK_CHECKS];
            for(k=0;k<c && count<cap;++k)
                if(!ap_net_location_checked((long)codes[k]))
                    count=AP_GlowPush(outBits,outCodes,count,
                                      slots[k]<0 ? AP_CONTENT_BIT_TROPHY+pad->physical
                                                 : AP_CONTENT_BIT_CUP+pad->physical*AP_CONTENT_CUP_STRIDE+slots[k],
                                      codes[k]);
            c=ap_content_cup_track_checks(pad,ap_content_plan_pad,rows);
            for(k=0;k<c && count<cap;++k) {
                if(ap_net_location_checked((long)rows[k].code))continue;
                if(openOnly && rows[k].box_pad>=0 && !AP_ContentPadOpen(ap_content_plan_pad(rows[k].box_pad)))continue;
                count=AP_GlowPush(outBits,outCodes,count,rows[k].bit,rows[k].code);
            }
            return count;
        }
        if(pad->crystal) {
            if(!ap_net_location_checked((long)pad->trophy_location))
                count=AP_GlowPush(outBits,outCodes,count,AP_CONTENT_BIT_TROPHY+pad->physical,pad->trophy_location);
        }
        // Track modes (#133 PR 3): every unchecked check of every mode that is
        // open now, in the pad's mode order, as the glow ruling of 2026-09-30
        // asks. A mode still locked behind stage 2 stays hidden, like a legacy
        // pad (decision 26, review finding 11).
        for(n=0;!pad->crystal && n<pad->mode_count && count<cap;++n) {
            int64_t codes[3]={0,0,0};
            int k,c;
            if(openOnly && !AP_ContentModeOpen(pad,pad->modes[n]))continue;
            c=ap_content_mode_codes(pad,pad->modes[n],codes);
            for(k=0;k<c && count<cap;++k) {
                if(codes[k]<=0 || ap_net_location_checked((long)codes[k]))continue;
                count=AP_GlowPush(outBits,outCodes,count,
                                  pad->modes[n]==AP_CONTENT_MODE_TROPHY ? AP_CONTENT_BIT_TROPHY+pad->physical :
                                  pad->modes[n]==AP_CONTENT_MODE_CTR ? AP_CONTENT_BIT_CTR+pad->physical :
                                  AP_CONTENT_BIT_RELIC+pad->physical*3+k,
                                  codes[k]);
            }
            // ctr_first (J11): the CTR Challenge also pays the Trophy Race
            // check, so it glows under the CTR mode, right after the CTR.
            {
                const int64_t paired=ap_content_mode_paired_trophy(pad,pad->modes[n]);
                if(paired>0 && count<cap && !ap_net_location_checked((long)paired))
                    count=AP_GlowPush(outBits,outCodes,count,AP_CONTENT_BIT_TROPHY+pad->physical,paired);
            }
        }
        // Per-track families (#133 PR 3b): podium rungs, the track's Wumpa
        // check (once, whichever modes reach it) and the letter checks; the
        // Relic Race Perfect (#133 PR 3c).
        {
            unsigned seen=0;
            for(n=0;!pad->crystal && n<pad->mode_count && count<cap;++n) {
                int64_t codes[AP_CONTENT_FAMILY_SLOTS];
                int slots[AP_CONTENT_FAMILY_SLOTS];
                int k,c;
                if(openOnly && !AP_ContentModeOpen(pad,pad->modes[n]))continue;
                c=ap_content_mode_family_codes(pad,pad->modes[n],codes,slots);
                for(k=0;k<c && count<cap;++k) {
                    if((seen & (1u<<slots[k])) || ap_net_location_checked((long)codes[k]))continue;
                    seen|=1u<<slots[k];
                    count=AP_GlowPush(outBits,outCodes,count,
                                      AP_CONTENT_BIT_FAMILY+pad->physical*AP_CONTENT_FAMILY_STRIDE+slots[k],codes[k]);
                }
            }
        }
        // Item Box checks (#133 PR 4e): every unchecked box of the pad.
        for(n=0;!pad->crystal && n<pad->box_count && n<15 && count<cap;++n)
            if(pad->box_locations[n]>0 && !ap_net_location_checked((long)pad->box_locations[n]))
                count=AP_GlowPush(outBits,outCodes,count,AP_CONTENT_BIT_BOX+pad->physical*AP_CONTENT_BOX_STRIDE+n,
                                  pad->box_locations[n]);
        for(n=0;n<pad->crystal_count && count<cap;++n)
            if(!ap_net_location_checked((long)pad->crystal_locations[n]))
                count=AP_GlowPush(outBits,outCodes,count,60000+pad->physical*20+n,pad->crystal_locations[n]);
        return count;
}

// Pad Approach Hints (#124, ap_pad_hints_logic.h). Called every frame the
// pad's name shows. Filters the glow's open, unchecked locations through
// each exported location rule on an open content-plan pad; the net side
// drops codes the room latch already holds, so a standing kart sends nothing
// after the first frame.
void AP_PadApproachHints(int levelID)
{
	int bits[AP_PAD_DISPLAY_BITS_MAX];
	int64_t codes[AP_PAD_DISPLAY_BITS_MAX];
	const ctr_content_pad *pad;
	int n, i, eligible = 0, contentReady = 1;
	if (!ctr_cfg_active() || !ctr_cfg.pad_approach_hints || !ap_net_pad_hints_ready()) return;
	// A2-23: held back until Warp Pad Preview, when asked. Nothing is latched.
	if (AP_HintFilter_PadWaits(ctr_cfg.pad_hints_wait_for_spoiler, ctr_cfg.spoiler_items, AP_SpoilerPadPreviewShown()))
		return;
	pad = ap_content_plan_active() ? ap_content_plan_pad(levelID) : NULL;
#ifdef CTR_CUSTOM_TRACKS
	contentReady = AP_ContentPadReady(levelID);
#endif
	if (!AP_PadHint_Eligible(ctr_cfg.pad_approach_hints, 1, ap_content_plan_active(), pad && pad->occupied,
	                         contentReady, pad && AP_ContentPadOpen(pad)))
		return;
	n = AP_ContentPadGlowBits(levelID, bits, codes, AP_PAD_DISPLAY_BITS_MAX, 1);
	for (i = 0; i < n; ++i)
	{
		unsigned flags = 0;
		int known = ap_net_scout_known(codes[i], NULL, NULL, &flags);
		// A2-23: a skipped location is not latched, so it stays hintable. One
		// whose item is still unknown waits for the connect-time scouts.
		if (AP_HintFilter_Progression(ctr_cfg.pad_hints_progression_only, known, flags) != AP_HINT_KEEP) continue;
		if (AP_LocationInLogic(codes[i])) codes[eligible++] = codes[i];
	}
	if (eligible > 0) ap_net_pad_approach_hint(levelID, codes, eligible);
}

int AP_PadUncollectedGlowBits(int destLevelID, int *outBits, int cap)
{
	if (ap_content_plan_active())
		return AP_ContentPadGlowBits(destLevelID, outBits, NULL, cap, 0);
	return 0;
}

// Server truth, in the shape AP_BoxMap_CountStanding wants. ap_boxes.c holds its
// own pair for the spawn set; these two ask the same two questions of the same
// two sets, which is what keeps the pad's idea of "still standing" identical to
// the track's.
static int AP_PadBoxLive(long code, void *ctx)
{
	(void)ctx;
	return ap_net_location_exists((long long)code); // §7: live in this slot's seed
}

static int AP_PadBoxChecked(long code, void *ctx)
{
	(void)ctx;
	return ap_net_location_checked((long long)code); // already broken
}

// How many breakable boxes still stand behind this pad's destination. A cup pad
// aggregates its four legs, exactly as AP_PadUncollectedGlowBits does for rungs
// and for the same reason: a cup leg is not independently raceable from the hub,
// so a Done cup pad would strand its legs' boxes permanently.
int AP_PadUncollectedBoxCount(int destLevelID)
{
	int n = 0;

	if (!ctr_cfg_active())
		return 0;

	// #133 PR 4e: a content-plan pad owns its Item Box checks by its physical
	// id, whatever track it holds; never the retail row of that number.
	if (ap_content_plan_active())
	{
		const ctr_content_pad *pad = ap_content_plan_pad(destLevelID);
		int b;
		for (b = 0; pad && pad->occupied && b < pad->box_count && b < 15; b++)
			if (pad->box_locations[b] > 0 && AP_PadBoxLive((long)pad->box_locations[b], 0) &&
			    !AP_PadBoxChecked((long)pad->box_locations[b], 0))
				n++;
		return n;
	}
	// One generator row 12: an active config is always a content plan.
	return 0;
}

// Display only the plan-owned locations whose race mode is open.
int AP_PadUncollectedDisplayBits(int destLevelID, int *outBits, int cap)
{
	if (ap_content_plan_active())
		return AP_ContentPadGlowBits(destLevelID, outBits, NULL, cap, 1);
	return 0;
}

// The display buffer must hold a Gem Cup's four legs and the busiest single pad.
CTR_STATIC_ASSERT(AP_PAD_DISPLAY_BITS_MAX >= AP_PADGLOW_WORST_CASE_BITS);
CTR_STATIC_ASSERT(AP_PAD_DISPLAY_BITS_MAX >=
                  5 + 1 + 2 + CTR_CFG_PODIUM_RUNG_COUNT + AP_BOX_SLOTS_PER_TRACK +
                  CTR_CFG_LETTER_COUNT + 1);

// Reward-type group of a glow bit -> the prize slot that owns it under
// by_reward_type (issue #59). The three groups are exactly the three slots the
// pad already births as trophy / relic / token placeholders
// (s_warpPadRewardModelIDs in AH_WarpPad.c), so a grouped slot keeps showing the
// kind of check its placeholder was born for:
//   0 RACE   trophy bit + that track's podium rung pseudo-bits (>= the pseudo
//            base) -- one race result, one slot, which is why this slot can hold
//            six items (trophy + 5 rungs) on a rung seed.
//   1 RELIC  the sapphire/gold/platinum Time-Trial tiers, i.e. the contiguous
//            span from the first sapphire bit up to the first CTR token bit.
//   2 TOKEN  everything else the glow enumerators can produce: the CTR Token
//            challenge, a gem-cup gem and an arena crystal (purple token).
// A trial pad's Trophy and CTR Challenge pseudo-bits (#343) join groups 0 and 2.
// Item boxes and per-track Wumpa join group 0, CTR letters group 2 (ruling,
// 2026-09-30); Cortex Vortex Wumpa follows its retail sibling into group 0.
// Bit ranges follow the enumerators above, which build every bit as
// <first-bit-of-its-block> + index, so the block bases ARE the boundaries.
static int AP_GlowBitRewardGroup(int globalBit)
{
    // A cup's sweep rides the race slot on a normal room too, like a content
    // plan's (decision 29, review finding 10).
    if(globalBit>=AP_CUP_SWEEP_PSEUDO_BASE && globalBit<AP_CUP_SWEEP_PSEUDO_BASE+5)return 0;
    if(globalBit>=60000 && globalBit<62100)return 2;
    {
        // Content-plan pads: Trophy rides the race slot, the relic tiers the
        // relic slot, CTR and a Crystal Challenge the token slot.
        int contentPad,contentMode,contentTier;
        if(ap_content_bit_decode(globalBit,&contentPad,&contentMode,&contentTier)) {
            const ctr_content_pad *p=ap_content_plan_pad(contentPad);
            if(p && p->crystal)return 2;
            return contentMode==AP_CONTENT_MODE_RELIC ? 1 : contentMode==AP_CONTENT_MODE_CTR ? 2 : 0;
        }
        // Rungs and Wumpa ride the race slot, letters the token slot, as on
        // retail pads (ruling 2026-09-30); the perfect rides the relic slot.
        if(ap_content_family_bit_decode(globalBit,&contentPad,&contentTier))
            return ap_content_family_reward_group(contentTier);
        // Item Box checks (#133 PR 4e) ride the race slot, and so do a cup's
        // sweep, Cup Leg Victory (#133 PR 5) and first lap checks.
        if(ap_content_box_bit_decode(globalBit,&contentPad,&contentTier))
            return 0;
        if(ap_content_cup_bit_decode(globalBit,&contentPad,&contentTier))
            return 0;
        if(ap_content_cup_lap_bit_decode(globalBit,&contentPad,&contentTier))
            return 0;
        if(ap_content_cup_wumpa_bit_decode(globalBit,&contentPad,&contentTier))
            return 0;
        if(ap_content_cup_rung_bit_decode(globalBit,&contentPad,&contentTier))
            return 0;
    }
    if (globalBit>=AP_CRYSTAL_PSEUDO_BASE && globalBit<AP_CRYSTAL_PSEUDO_BASE+80) return 2;
	int cortexGroup = AP_CortexPseudoRewardGroup(globalBit);
	int trialGroup = AP_TrialPseudoRewardGroup(globalBit);
	int perfectGroup = AP_RelicPerfectPseudoRewardGroup(globalBit);
	int itemGroup = AP_PadGlowItemRewardGroup(globalBit);
#ifdef CTR_CUSTOM_TRACKS
	if (globalBit == AP_CUSTOM_CTR_PSEUDO_BIT)
		return 2; // custom CTR Challenge -> token slot, like every CTR Challenge
#endif
	if (itemGroup >= 0)
		return itemGroup; // boxes + Wumpa -> race slot, CTR letters -> token slot
	if (perfectGroup >= 0)
		return perfectGroup; // #439: Relic Race Perfect rides in the relic slot
	if (cortexGroup >= 0)
		return cortexGroup; // schema 15: Cortex Vortex pseudo-bits
	if (trialGroup >= 0)
		return trialGroup; // #343: trial Trophy -> race slot, CTR Challenge -> token slot
	if (globalBit >= AP_PODIUM_PSEUDO_BASE)
		return 0; // podium rung -- rides with its track's trophy
	if (globalBit < ADV_REWARD_FIRST_SAPPHIRE_RELIC)
		return 0; // trophy (ADV_REWARD_FIRST_TROPHY + track)
	if (globalBit < ADV_REWARD_FIRST_CTR_TOKEN)
		return 1; // sapphire / gold / platinum relic tiers
	return 2;     // CTR token, gem-cup gem, arena crystal
}

// Decide WHICH of the `n` uncollected bits each of the pad's three prize slots
// advertises this frame, writing the per-slot bit (or -1 = hide the slot) into
// outSlot3. `phase` is the display tick (frame counter / 0x3C, so it steps once
// every 2 seconds); the caller owns the frame clock. Sole consumer is the AP glow
// pass in AH_WarpPad_ThTick.
//
// Layout follows ctr_cfg.warp_pad_item_display (issue #59):
//   ONE_PILE (default, and every seed predating the option): all n items share
//     the three slots. n > 3 cycles a 3-wide window over the whole list, n in
//     1..3 puts item i in slot i, and the leftover slots hide. Unchanged from
//     what the glow has always done, which is what the absent-key fallback owes.
//   BY_REWARD_TYPE: each reward type keeps its own slot (see
//     AP_GlowBitRewardGroup) and rotates only within that type, so a floating
//     sapphire in the relic slot is the relic check and never the CTR check. A
//     type with nothing left hides its slot, so a destination carrying a single
//     reward (arena crystal, cup gem) shows it in the token slot rather than
//     slot 0 -- the price of a fixed slot per type, and the point of the option.
void AP_PadGlowSlots(const int *bits, int n, int phase, int *outSlot3)
{
	AP_GlowSlots_Select(
	    bits, n, phase,
	    ctr_cfg_active() &&
	        ctr_cfg.warp_pad_item_display == WARP_PAD_DISPLAY_BY_REWARD_TYPE,
	    AP_GlowBitRewardGroup, outSlot3);
}

// Physical-pad entry gate shared by boxes and the plan lifecycle.
int AP_PadStage1Met(int physLevelID)
{
	return ap_content_plan_active()
	    ? AP_ContentPadOpen(ap_content_plan_pad(physLevelID)) : !ctr_cfg_active();
}

// The effective player at a physical pad. A met racer lock OVERRIDES the chosen
// racer, because AH_WarpPad.c calls AP_RacerLock_ForceForWarp immediately before
// the load reads characterIDs[0] -- so the hub-time chosen racer is not what
// will actually race. Predicting from the chosen racer alone could advertise a
// locked guest as an opponent, or seat the player as their own opponent.
static int AP_HitEffectivePlayer(int physLevelID)
{
	int chosen = (data.characterIDs[0] >= 0) ? (int)data.characterIDs[0] : -1;
	int lock = ctr_cfg_racer_lock(physLevelID);
	return AP_HitEffectivePlayerPure(chosen, lock, ctr_cfg_racer_lock_met(physLevelID));
}

// The unified pad state (Warp-Pad State Model v2). Returns:
//   1 = Locked      (stage-1 unmet)                         -> RED
//   2 = Raceable    (stage-1 met, primary check available)  -> GREEN (flicker)
//   3 = Re-locked   (race dest only: trophy checked,
//                    stage-2 unmet)                          -> ORANGE
//   4 = Tier-2 open (race dest only: stage-2 met, checks
//                    remain)                                 -> PERIWINKLE
//   5 = Done        (all destination locations checked;
//                    HARD-LOCKED, pure UX)                   -> GRAY
//   0 = vanilla / not applicable (no slot_data, or an
//       unrecognised destination)                           -> untouched
// physLevelID = the physical pad (requirement key); destLevelID = the loaded
// destination track (location + lifecycle-category key). Under no shuffle these
// are equal; the callers pass both so it is correct under destination shuffle.
int AP_PadState(int physLevelID, int destLevelID)
{
	if (ap_content_plan_active())
	{
		const ctr_content_pad *pad = ap_content_plan_pad(physLevelID);
		if (!pad || !pad->occupied) return 5;
#ifdef CTR_CUSTOM_TRACKS
		if (!AP_ContentPadReady(physLevelID)) return 5;
#endif
		if (!AP_ContentPadOpen(pad)) return 1;
        if(pad->crystal) {
            int bits[21];
            return AP_PadUncollectedGlowBits(physLevelID,bits,21) ? 2 : 5;
        }
        // Cups (#133 PR 5): open while a cup check or a check of its tracks
        // is left, then Done (AP_PadReplayable keeps the race open).
        if(pad->cup) {
            int bits[6+AP_CONTENT_CUP_TRACK_CHECKS];
            return AP_PadUncollectedGlowBits(physLevelID,bits,6+AP_CONTENT_CUP_TRACK_CHECKS) ? 2 : 5;
        }
		// Track modes (#133 PR 3), the states of ap_pad_state.h.
		return AP_ContentTrackPadState(pad);
	}
	return 0; // No AP pad state outside a content plan.
}

// AP_PadState plus the racer fact, as the shared colour table's display state
// (ap_pad_colour.h). Only a closed plan pad can be racer-locked.
// A3-37: how a plan pad's open checks stand against the exported logic, as the
// counts ap_pad_logic_look.h decides from. The open set is the pad's display
// set, the checks a player can act on now (a mode still locked behind stage 2
// stays out, like the glow). Asked fresh each call, so map, beam and tracker
// cannot read an old answer; the rebuild redraws the pad when the answer moves
// (AP_PadRedrawGen, read through the birth key's look).
static int AP_PadLogicLook(int physLevelID, int look)
{
	int bits[AP_PAD_DISPLAY_BITS_MAX];
	int64_t codes[AP_PAD_DISPLAY_BITS_MAX];
	int n, i, in = 0, unknown = 0;

	if (look != AP_PAD_LOOK_OPEN && look != AP_PAD_LOOK_STAGE2_OPEN)
		return look;
	if (!ap_content_plan_active())
		return look;
	n = AP_ContentPadGlowBits(physLevelID, bits, codes, AP_PAD_DISPLAY_BITS_MAX, 1);
	for (i = 0; i < n; i++)
	{
		int st = AP_LocationLogicState((long long)codes[i]);
		if (st == AP_PAD_LOGIC_IN) in++;
		else if (st == AP_PAD_LOGIC_UNKNOWN) unknown++;
	}
	return AP_PadLookByLogic(look, n, in, unknown);
}

int AP_PadLook(int physLevelID, int destLevelID)
{
	int state = AP_PadState(physLevelID, destLevelID);
	int racerMissing = 0;
	if (state == 1 && ap_content_plan_active())
	{
		const ctr_content_pad *pad = ap_content_plan_pad(physLevelID);
		racerMissing = pad && !AP_ContentPadRacerMet(pad);
	}
	return AP_PadLogicLook(physLevelID, AP_PadLookFromState(state, racerMissing));
}

// A Hit Character or Itemsanity use, or the `wumpa: global` check, a race on this
// plan pad can still serve (one generator row 6). They belong to no pad, so a
// closing pad would strand them, and logic counts every first race as a route:
// they keep the first race open, as on an ordinary pad. Asked for the engine
// track the pad's race loads.
int AP_ContentPadPlainLeft(const ctr_content_pad *pad)
{
	const ctr_content_checks *fam = ap_content_plan_checks();
	int host;
	if (!pad || !pad->occupied || pad->crystal || pad->cup)
		return 0;
	if (fam->wumpa == 1 && pad->wumpa_modes && fam->wumpa_global > 0 && !AP_ContentCodeChecked(fam->wumpa_global))
		return 1;
	host = ap_content_pad_host(pad);
	return (AP_HitEncounterEnabled() &&
	        AP_HitEncounterOpportunity(host, AP_HitEffectivePlayer(pad->physical)) >= 0) ||
	       AP_ItemsanityPadOpportunity(host);
}

int AP_PadReplayable(int physLevelID, int destLevelID)
{
#ifdef CTR_CUSTOM_TRACKS
	const ctr_content_pad *pad = ap_content_plan_active() ? ap_content_plan_pad(physLevelID) : NULL;
	int state;
	if (!pad || !pad->occupied || !AP_ContentPadReady(physLevelID) || !AP_ContentPadOpen(pad))
		return 0;
	state = AP_PadState(physLevelID, destLevelID);
	// Finished Crystal Challenges replay through their normal single-activity
	// route, like finished races and cups. Collected checks remain collected.
	if (state == 5) return 1;
	// A won stage-one Trophy/CTR race remains an income route while stage
	// two is closed. Keep the pad's state and its remaining-check display.
	return state == 3 && !pad->cup && ap_content_first_mode_replayable(pad, AP_ContentCodeChecked);
#else
	(void)physLevelID;
	(void)destLevelID;
	return 0;
#endif
}

// One line per menu decision, so a player log shows what a pad offered.
void AP_ContentLogMenu(const ctr_content_pad *pad, const ap_rs_rows *rows, int decision)
{
	static const char *const names[AP_RS_COUNT] = {"trophy", "ctr", "relic"};
	char msg[160];
	int len, i;
	len = snprintf(msg, sizeof msg, "[AP PAD] plan pad %d: race menu rows", pad->physical);
	for (i = 0; i < rows->count && len > 0 && len < (int)sizeof msg; i++)
		len += snprintf(msg + len, sizeof msg - len, " %s%s", names[rows->mode[i]],
		                rows->productive[i] ? "" : "(grey)");
	if (len > 0 && len < (int)sizeof msg)
		snprintf(msg + len, sizeof msg - len, " -> %s\n",
		         decision == AP_RS_OPEN_MENU ? "menu" : decision == AP_RS_NO_ROUTE ? "refused" : names[decision]);
	AP_LogLine(msg);
}

// The warp-pad entry fail-safe (2026-10-09). AH_WarpPad.c calls this when a
// pad that is not Done reached an entry route with nothing to load. The kart is
// released instead of being held in the warp beam; this line names the pad so a
// player log shows which counted check had no route.
void AP_PadLogEntryRefused(int physLevelID, int destLevelID)
{
	char msg[160];

	snprintf(msg, sizeof msg,
	         "[AP PAD] pad %d -> dest %d: entry refused, no route can load a race "
	         "for the open checks (state=%d); kart released\n",
	         physLevelID, destLevelID, AP_PadState(physLevelID, destLevelID));
	AP_LogLine(msg);
}

#endif // CTR_AP
