#ifndef AP_OXIDE_CUTSCENE_H
#define AP_OXIDE_CUTSCENE_H

// ---------------------------------------------------------------------------
// "Oxide's Final Challenge is now available" PRESENTATION readiness (WO-A4).
//
// THE DIVERGENCE THIS CLOSES. Three retail sites decide whether the Oxide
// relic cutscene plays, and all three still ask the RETAIL rule
// `gGT->currAdvProfile.numRelics >= 18`:
//
//   game/233/CS_Camera.c  CS_Camera_BoolGotoBoss  -- go to the boss cutscene
//                                                    at all, after a relic win
//   game/233/CS_Camera.c  CS_Camera_ThTick_Podium -- pick OXIDE_RELICS_<hub>
//                                                    (indices 9..13)
//   game/233/CS_Thread.c  script opcode 0x21      -- redirect boss cutscene 0
//                                                    to 9 when relics are in
//
// The gate they are describing is NOT that rule any more. Since issue #23 the
// Final Challenge opens on the per-seed relic-goal MODE + COUNT
// (ctr_cfg.oxide_final_unlock / oxide_final_count) via AP_OxideFinalOpen(),
// and game/232/AH_Garage.c already loads the encounter from exactly that.
//
// WHY THE RETAIL RULE IS NOT MERELY A DIFFERENT SPELLING OF IT. Under AP,
// `numRelics` is not "relic races you won". GAMEPROG_AdvPercent recomputes it
// from the ADV_REWARD_FIRST_SAPPHIRE_RELIC bits, and AP_ApplyItems rewrites
// those bits from RECEIVED items on every reconcile tick. So the shipped
// behaviour is "the cutscene fires on the 18th Sapphire Relic ITEM to arrive
// from anywhere in the multiworld", which is neither the player's own progress
// nor this seed's Oxide gate. Two concrete disagreements:
//
//   * mode = platinum, count = 5: the garage can load the Final Challenge with
//     zero Sapphires received, so the cutscene NEVER plays.
//   * mode = total, count = 40: the 18th received Sapphire plays the cutscene
//     while the Final Challenge gate is still shut.
//
// This is presentation following the gate that already ships. It introduces no
// new gate and changes no retail path: without slot_data the answer is still
// the vanilla 18-Sapphire rule, and AP_OxideFinalOpen() itself falls back to
// that same rule, so the two halves agree by construction.
//
// SCOPE. This is deliberately NOT the WO-A1 garage ENTRY predicate
// (ap/ap_oxide_entry.h). Entry readiness asks "may the player go through the
// door"; this asks "which Oxide encounter is the door now offering". They are
// different questions with different inputs and are kept apart on purpose.
// The title-screen Oxide intro (game/230/MM_Title.c, gGT->boolSeenOxideIntro)
// is an attract-mode retail scene with no AP input at all and is not in scope.
// ---------------------------------------------------------------------------

// `cfgActive` is ctr_cfg_active(); `vanillaRelics` is
// gGT->currAdvProfile.numRelics; `apFinalOpen` is AP_OxideFinalOpen().
// Returns non-zero when the Final-Challenge presentation should play.
static inline int AP_OxideFinalPresentationReady(int cfgActive,
                                                 int vanillaRelics,
                                                 int apFinalOpen)
{
	if (cfgActive)
		return apFinalOpen != 0;

	return vanillaRelics >= 18;
}

#endif // AP_OXIDE_CUTSCENE_H
