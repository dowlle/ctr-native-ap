#ifndef AP_PODIUM_SKIP_LOGIC_H
#define AP_PODIUM_SKIP_LOGIC_H

#ifdef CTR_AP

// Freestanding ceremony-skip policy for issue #285. The local "Skip Podium
// Ceremonies" client option drops the hub podium ceremony for an ordinary
// Trophy, a CTR Challenge and an ordinary Relic, while every other ceremony is
// preserved. Pure and engine-independent, so the host harness
// (tools/test-podium-skip.c) pins the decision directly; the production call
// sites consume this function rather than keeping a second copy of the policy.
//
// Rulings this encodes:
//   * Boss and Gem Cup podiums are never skipped. Both can carry ordinary
//     race-mode flags, so their classification takes priority over the rest.
//   * A Relic podium that would open Oxide's Final Challenge keeps its
//     STATIC_RELIC reward id: CS_Camera_BoolGotoBoss reads that id (plus the
//     relic gate) to select the Oxide transition. Only an ordinary relic,
//     whose Oxide predicate is false, may be skipped.
//   * Option off -> nothing is skipped, so the build behaves as it does today.

// Mirror of the engine model ids (namespace_Instance.h), spelled out so the
// policy stays freestanding. ap_hooks.c static-asserts them against the engine
// constants so the mirrors can never drift.
#define AP_PODIUM_SKIP_TROPHY_MODEL 0x62 // STATIC_TROPHY
#define AP_PODIUM_SKIP_RELIC_MODEL  0x61 // STATIC_RELIC

// Mirror of the gameMode1 words the 0xd call site requires (namespace_Main.h).
// The skip is an Adventure-relic action only: the UI_RaceEnd_MenuProc case 0xd
// exit is shared by unrelated modes, so the caller enforces BOTH flags rather
// than trusting the classification. Pinned against the engine in ap_hooks.c.
#define AP_PODIUM_SKIP_ADVENTURE_MODE 0x80000  // ADVENTURE_MODE
#define AP_PODIUM_SKIP_RELIC_RACE     0x4000000 // RELIC_RACE
#define AP_PODIUM_SKIP_NEW_RELIC      0x2000000 // NEW_RELIC (gameModeEnd)

// Mirror of the gameMode2 count-up / freeze bits a watched ceremony clears by
// the time it ends (namespace_Main.h): CS_Podium_Prize_ThDestroy clears the
// three INC_* bits and CS_DestroyPodium_StartDriving clears VEH_FREEZE_PODIUM.
// A skipped ceremony never sets them, and clearing them defensively makes the
// skipped end state identical to a watched one. ap_hooks.c static-asserts the
// mirror against the engine constants.
#define AP_PODIUM_SKIP_INC_RELIC      0x1000000
#define AP_PODIUM_SKIP_INC_KEY        0x2000000
#define AP_PODIUM_SKIP_INC_TROPHY     0x4000000
#define AP_PODIUM_SKIP_FREEZE_PODIUM  0x4
#define AP_PODIUM_SKIP_CLEAN_MASK \
	(AP_PODIUM_SKIP_INC_RELIC | AP_PODIUM_SKIP_INC_KEY | \
	 AP_PODIUM_SKIP_INC_TROPHY | AP_PODIUM_SKIP_FREEZE_PODIUM)

typedef enum
{
	AP_PODIUM_SKIP_NONE = 0,
	AP_PODIUM_SKIP_TROPHY,
	AP_PODIUM_SKIP_RELIC,
	AP_PODIUM_SKIP_CTR_CHALLENGE,
	AP_PODIUM_SKIP_BOSS,
	AP_PODIUM_SKIP_CUP
} AP_PodiumSkipKind;

// Classify the pending hub podium from the mode words and the reward id.
// Priority: boss, then cup, then relic, then CTR Challenge, then Trophy. The
// boss and cup arms are load-bearing: a cup leg and a boss race both set
// ordinary race flags, and neither ceremony may be skipped. A CTR Challenge is
// a Trophy-model podium with the token-race flag still set.
static inline AP_PodiumSkipKind AP_PodiumSkipClassify(
    int rewardId, int isBossRace, int isCup, int isRelicRace, int isTokenRace)
{
	if (isBossRace)
		return AP_PODIUM_SKIP_BOSS;
	if (isCup)
		return AP_PODIUM_SKIP_CUP;
	if (isRelicRace && rewardId == AP_PODIUM_SKIP_RELIC_MODEL)
		return AP_PODIUM_SKIP_RELIC;
	if (isTokenRace && rewardId == AP_PODIUM_SKIP_TROPHY_MODEL)
		return AP_PODIUM_SKIP_CTR_CHALLENGE;
	if (rewardId == AP_PODIUM_SKIP_TROPHY_MODEL)
		return AP_PODIUM_SKIP_TROPHY;
	return AP_PODIUM_SKIP_NONE;
}

// The skippable set: ordinary Trophy, CTR Challenge, ordinary Relic.
static inline int AP_PodiumSkipKindIsSkippable(AP_PodiumSkipKind kind)
{
	return kind == AP_PODIUM_SKIP_TROPHY || kind == AP_PODIUM_SKIP_RELIC ||
	       kind == AP_PODIUM_SKIP_CTR_CHALLENGE;
}

// The UI_RaceEnd_MenuProc case 0xd call-site gate: the skip may run ONLY for an
// Adventure relic exit, so both flags must be set. case 0xd is shared by
// unrelated modes (cup, boss, Time Trial, arcade), and although the
// classification normally rejects those reward/mode combinations, this is the
// invariant the caller must hold on its own rather than infer. Pure, so the
// host harness drives it directly.
static inline int AP_PodiumSkipCallAllowed(int gameMode1)
{
	return ((gameMode1 & AP_PODIUM_SKIP_ADVENTURE_MODE) != 0) &&
	       ((gameMode1 & AP_PODIUM_SKIP_RELIC_RACE) != 0);
}

// Relic results skip (0.2.1 rc1 Steam testing, 2026-09-22). With Skip Podium
// Ceremonies on, an Adventure relic race that earned a new relic leaves its
// results (crate countdown, relic banner, high score and name entry, Best
// Times, Retry / Exit to Map) for the hub at once, through the same exit Exit to
// Map takes, so the podium skip and the Oxide transition rules still apply. A
// run without a new relic keeps the full results and Retry. The relic flag and
// its AP check were already sent at the finish (RR_EndEvent_UnlockAward), so
// only presentation is skipped.
static inline int AP_RelicResultsSkipDecision(
    int optionEnabled, int gameMode1, int gameModeEnd)
{
	return optionEnabled && AP_PodiumSkipCallAllowed(gameMode1) &&
	       ((gameModeEnd & AP_PODIUM_SKIP_NEW_RELIC) != 0);
}

// The production decision. `oxideRelicQualifying` is consulted only for a
// relic, and is 1 when this relic podium would open the Oxide Final Challenge.
static inline int AP_PodiumSkipDecision(
    int optionEnabled, int rewardId, int isBossRace, int isCup,
    int isRelicRace, int isTokenRace, int oxideRelicQualifying)
{
	AP_PodiumSkipKind kind;

	if (!optionEnabled)
		return 0;

	kind = AP_PodiumSkipClassify(rewardId, isBossRace, isCup, isRelicRace,
	                             isTokenRace);

	if (kind == AP_PODIUM_SKIP_RELIC && oxideRelicQualifying)
		return 0;

	return AP_PodiumSkipKindIsSkippable(kind);
}

// The gameMode2 value a skipped ceremony ends on: the INC_* count-up bits and
// VEH_FREEZE_PODIUM cleared, exactly as a watched ceremony leaves them.
static inline int AP_PodiumSkipCleanGameMode2(int gameMode2)
{
	return gameMode2 & ~AP_PODIUM_SKIP_CLEAN_MASK;
}

// ---------------------------------------------------------------------------
// "This relic podium will go to Oxide" -- ONE predicate, TWO consumers.
//
// CS_Camera_BoolGotoBoss (game/233/CS_Camera.c) selects the relic ceremony that
// opens N. Oxide's Final Challenge; the skip decision must preserve exactly that
// ceremony or the player loses the transition while the mode word still claims
// it happened. They were written as two spellings of the same condition and
// drifted: AP_ShouldSkipPodium asked only the relic threshold, while
// BoolGotoBoss ALSO requires ADV_REWARD_BEAT_OXIDE_SECOND == 0. After Oxide's
// second defeat the skip would fire on a relic whose transition can no longer
// play. This helper is that one condition.
//
// `apPresentationReady` is the relic threshold half of the predicate: for a
// relic this is AP_OxideFinalEncounterPresentationReady(cfgActive,
// vanillaRelics, offersFinal, finalRelicMet), the shipped gate, which already
// folds in ctr_cfg_active() and falls back to the retail 18-Sapphire rule when
// no slot_data is active. Both call sites keep their own threshold inputs,
// because CS_Camera_BoolGotoBoss reads the live currAdvProfile.numRelics channel
// while AP_ShouldSkipPodium reads the raw Sapphire bits at the race-end skip
// point. Only the composition with the beat-Oxide bit is shared.
//
// `hasRelicPodium` is 1 only when the pending reward is STATIC_RELIC; every
// other reward keeps its own ceremony regardless of the beat-Oxide bit.
static inline int AP_PodiumRelicWillGotoOxide(int hasRelicPodium,
                                              int apPresentationReady,
                                              int beatOxideSecond)
{
	return hasRelicPodium && apPresentationReady && beatOxideSecond == 0;
}

#endif // CTR_AP
#endif // AP_PODIUM_SKIP_LOGIC_H
