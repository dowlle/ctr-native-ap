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
// (ap/ap_oxide_encounter.h). That decision asks "may the player go through the
// door"; this asks "which Oxide encounter is the door now offering". They are
// different questions with different inputs and are kept apart on purpose.
// The title-screen Oxide intro (game/230/MM_Title.c, gGT->boolSeenOxideIntro)
// is an attract-mode retail scene with no AP input at all and is not in scope.
// ---------------------------------------------------------------------------

// `cfgActive` is ctr_cfg_active(); `vanillaRelics` is
// gGT->currAdvProfile.numRelics. `apFinalOpen` is the resolved AP presentation
// gate. Engine call sites reach this through AP_OxideFinalSceneWanted below.
// Returns non-zero when the Final-Challenge presentation should play.
static inline int AP_OxideFinalPresentationReady(int cfgActive,
                                                 int vanillaRelics,
                                                 int apFinalOpen)
{
	if (cfgActive)
		return apFinalOpen != 0;

	return vanillaRelics >= 18;
}

// ---------------------------------------------------------------------------
// Issue #377: play the scene ONCE per seed, when the player reaches go mode for
// the Final Challenge (ruling of 2026-09-27).
//
// Before this, the scene played after EVERY Relic Race podium from the moment
// the relic gate opened until Oxide's second defeat. Now it plays on the first
// relic podium (or the Gemstone key-scene chain, see CS_Thread.c 0x21) after
// go mode is reached, and never again for that slot.
//
// GO MODE is the apworld's rule for "N. Oxide Garage: N. Oxide's Final
// Challenge" being in logic, which is what Universal Tracker shows
// (Rules.add_oxide_access_contract, final_win_rule):
//
//   has('Key', 4)                    garage door    -> garageOpen
//   Oxide 1's rule (companions on    first_rule     -> finalOffered: native
//     any_percent)                                     offers the Final only
//                                                      once Oxide 1 is cleared
//   configured relic mode + count    relic_rule     -> garageOpen (finalRelicMet)
//   companions on 101_percent        companions     -> garageOpen
//   first boost rank                 boost_term(1)  -> boostMin >= 1
//   the venue's finish term          track_finish_term(venue, bind_racer=False)
//     USF (two ranks) on Cortex                     -> boostMin 2, or 1 on
//     Vortex; USF or hard shortcut                     Oxide Station at hard
//     knowledge on Oxide Station                       shortcut knowledge
//
// Capability terms hold for ONE driveable racer (progressive_capability
// gate_satisfied): `bestBoostTier` is the best boost tier over the racers the
// player can currently drive. Without a live boost pack every term is vacuous.
//
// garageOpen is AP_OxideGarageOpen() for the offered Final encounter, which also
// refuses a Cortex Vortex venue whose track content is not ready. That is not a
// logic term, but announcing a race the garage would then refuse would spend the
// one play on nothing.
// ---------------------------------------------------------------------------

#define AP_OXIDE_SCENE_SK_HARD 2 // ctr_cfg.shortcut_knowledge: 0 easy / 1 medium / 2 hard

// Boost tier the Final Challenge needs in logic. `boostLive` is 1 when this
// seed randomizes the boost chain (shared or per character).
static inline int AP_OxideFinalBoostMin(int boostLive, int venueIsOxideStation,
                                        int shortcutKnowledge)
{
	if (!boostLive)
		return 0;
	if (venueIsOxideStation && shortcutKnowledge == AP_OXIDE_SCENE_SK_HARD)
		return 1; // the finish term is vacuous; the first-rank floor remains
	return 2;     // USF
}

static inline int AP_OxideFinalGoModePure(int finalOffered, int garageOpen,
                                          int boostMin, int bestBoostTier)
{
	if (!finalOffered || !garageOpen)
		return 0;
	return boostMin <= 0 || bestBoostTier >= boostMin;
}

// THE decision all three scene sites and the podium-skip keep rule share.
// Without slot_data it is still the retail 18-Sapphire rule (which repeats, as
// retail does). With slot_data: go mode, AND the server flag has been read,
// AND it says the scene has not played.
static inline int AP_OxideFinalSceneWanted(int cfgActive, int vanillaRelics,
                                           int goMode, int seenKnown, int seen)
{
	return AP_OxideFinalPresentationReady(cfgActive, vanillaRelics,
	                                      goMode && seenKnown && !seen);
}

#endif // AP_OXIDE_CUTSCENE_H
