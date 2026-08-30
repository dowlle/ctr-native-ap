#ifndef AP_HOOKS_H
#define AP_HOOKS_H

// Archipelago integration hooks for CTR Native.
//
// Compiled ONLY when CTR_AP is defined (CMake: -DCTR_AP=ON). The clean build
// leaves this out entirely. All Archipelago logic lives in this module so the
// upstream ctr-native diff stays minimal: upstream code calls AP_* hooks, and
// everything else stays here.

#ifdef CTR_AP

#include "ap_seedcfg.h" // per-seed slot_data config (ctr_cfg + getters), Phase 2
#include "ap_lettersanity.h" // freestanding pickup and token-gate decisions
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_track_manager.h>
#include "ap_custom_track_download.h"
#include "ap_custom_pad_logic.h"
#endif

struct GameTracker;
struct Instance;

// Called once per frame from the main loop (CTR_Main in MainMain.c).
void AP_OnFrame(struct GameTracker *gGT);

// Location events (option A) -- called from the game's reward-grant sites the
// instant the player earns a checkable adventure reward. rewardBit is the
// AdvProgress bit index (= word*32 + bit); resolved to an AP location code.
void AP_NotifyAdvReward(int rewardBit);

// Generic custom-slot Trophy identity. A displaced cup's retail Gem bit remains
// presentation state only; the AP check is the custom Trophy code from slot data.
int AP_CustomTrackTrophyChecked(void);
void AP_NotifyCustomTrackTrophy(void);

// Called when the player beats Oxide. oxideSecond != 0 = final win. Records the
// event; whether it COMPLETES the seed depends on the composed goal (issue #152:
// ctr_cfg.goal_oxide/goal_bosses/goal_gems, see AP_EvaluateGoal).
void AP_NotifyGoal(int oxideSecond);

// Evaluate the composed per-seed goal (issue #152: ctr_cfg.goal_oxide/
// goal_bosses/goal_gems, ANDed) against received items + game events and send
// StatusUpdate(GOAL) once when met. Call every adventure frame (the counted
// conditions: N bosses / N gems) and on the Oxide beat.
void AP_EvaluateGoal(void);

// Issue #244 presentation edge. AP_GoalArmLiveEvent marks an actual in-session
// Oxide/boss/gem event; reconnect reconstruction never calls it. The Oxide
// reward path claims a pending presentation to keep its vanilla ending. A goal
// completed elsewhere is claimed by AP_OnFrame only from a safe idle hub.
void AP_GoalArmLiveEvent(void);
int AP_GoalClaimOxideEnding(void);

// ── Race-end ceremony award text (display-only) ──
// Draw the AP award block at x,y from the current race's just-sent checks +
// primaryBit (the reward the ceremony celebrates, or -1 for ledger-only). Returns
// 1 if it drew (caller suppresses its vanilla award line), 0 if AP is inactive or
// nothing is scouted (caller draws the vanilla string). Called from the four
// end-of-race draw functions (game/221.c, game/222.c, game/223.c).
int AP_CeremonyDraw(int x, int y, int primaryBit, int includeLedger);

// Timed variant for ceremonies with a fixed visible window. It anchors cycling
// to ceremony elapsed time and compresses dwell only as far as needed to show
// every distinct entry once before fly-out.
int AP_CeremonyDrawTimed(int x, int y, int primaryBit, int includeLedger,
                         int elapsedFrames, int visibleFrames);

// Right-side fly-out center for a centered block with the given wrap width.
int AP_CeremonyOffscreenX(int logicalWidth, int wrapWidth);

// Highest relic tier the just-finished relic race sent (0 Sapphire, 1 Gold, 2
// Platinum; -1 none). The truthful source for the relic ceremony's tier label +
// relic colour (game/223.c, game/UI/UI_Clock.c), which vanilla derives from the
// AP_ApplyItems-clobbered advProgress bits. -1 during pre-race draws.
int AP_CeremonyRelicTier(void);

// ── Relic-race live target ladder (issue #21) ──
// AP-active seeds replace the vanilla race-start tier selector (which reads
// the AP_ApplyItems-clobbered advProgress bits, so received Gold/Platinum
// items made a first attempt show the PLATINUM time). AP_RelicTargetInit runs
// at relic-race start (UI_Instance.c): it picks the highest tier still
// earnable on this track (location in seed + unchecked), fills the
// sdata->relicTime_* HUD digits, and returns 1 (0 = no slot_data, vanilla
// selector stands). A per-frame tick in AP_OnFrame steps the shown tier down
// when the race clock passes its time, holding at the lowest earnable tier.
// AP_RelicTargetTier is the current shown tier for the HUD label/colour pin
// (UI_Clock.c); -1 = ladder inactive (vanilla label path).
int AP_RelicTargetInit(int levelID);
int AP_RelicTargetTier(void);

// ── Post-goal credits roll: AP section (issue #117, display-only) ──
// Called once from CS_Credits_Init after the vanilla scroll is relocated.
// Returns a merged scroll (hand-authored AP credits section from
// ap_credits_data.h + a copy of the vanilla scroll) allocated in the same
// MEMPACK high-mem arena as the vanilla block, or origScroll unchanged when no
// AP config is active. CS_Credits_Init runs once per credits playthrough (only
// full level loads reach it; the 19 later dancer levels swap in via the
// LOAD_Hub path), so the section appears exactly once per roll.
char *AP_Credits_PrependScroll(char *origScroll);

// ── AP item feed (display-only) ──
// Feed of the AP items you receive and send. AP_FeedOnItemReceived queues one
// drained receipt (called once per item in the received-item drain loop; flags
// is the NetworkItem.flags metadata, used to colour the line by AP class, issue
// #195). AP_FeedConnectReset re-arms initial-inventory suppression on a fresh
// connect; AP_FeedEndDrain(n) primes the feed once that dump goes quiet.
//
// TWO draw surfaces, one shared queue and one shared tick: AP_FeedDrawHub from
// the hub UI pass (bottom-left, the shipped anchor) and AP_FeedDrawRace from the
// race HUD pass (issue #192, upper-left of the track view, 1P and unpaused
// only). RenderAllHUD never runs a hub and a race pass in the same frame, so the
// feed ages exactly once per frame either way. AP_HubFeedOn reflects the
// ap-config.txt "hub_feed=" toggle (default on) and now governs BOTH surfaces --
// the key keeps its name so existing ap-config.txt files keep working.
void AP_FeedOnItemReceived(long long item, int player, long long index, unsigned flags);
void AP_FeedConnectReset(void);
void AP_FeedEndDrain(int drainedThisFrame);
void AP_FeedDrawHub(void);
void AP_FeedDrawRace(void);
int  AP_HubFeedOn(void);
// One trap-state line (armed, incoming, active) on the same two surfaces, in the
// trap class colour. The trap scheduler owns the wording; this is only delivery,
// so trap presentation cannot drift from the item feed's look.
void AP_FeedTrapLine(const char *text);

// ── AP gate counters (received-item model, Option B) ──
// Adventure gates read these received-item-TYPE counts instead of AdvProgress
// location bits, so item fill (generic, high-end) no longer collides with the
// game's own location-bit grants. itemType = raw item index 0..14.
//
// Item-TYPE indices (item id = AP_ITEM_BASE + index). Mirrored in ap_items.h;
// declared here too so the gate sites (game/232/AH_*.c) can read them by name.
// Colour order 0=Red 1=Green 2=Blue 3=Yellow 4=Purple (matches the apworld).
#ifndef AP_IDX_TROPHY
#define AP_IDX_TROPHY        0
#define AP_IDX_SAPPHIRE      1
#define AP_IDX_GOLD          2
#define AP_IDX_PLATINUM      3
#define AP_IDX_TOKEN_RED     4  // tokens: 4..8 = R,G,B,Y,P
#define AP_IDX_GEM_RED       9  // gems:   9..13 = R,G,B,Y,P
#define AP_IDX_KEY          14
#define AP_ITEM_INDEX_COUNT 15
#endif

int AP_GateCount(int itemType);          // received count for one of the 15 item indices
int AP_GateCountForeign(int itemType);   // #85: foreign-only split (multiworld + starting inv)
// Verifier-only foreign/server-granted tally across the complete frozen 0.2.0
// item index space (0..189). Own-location items are deliberately excluded and
// are banked synchronously from scout+checked state by ap_verify.c.
int AP_VerifyForeignItemCount(int itemIndex);
// Useful grants call this before applying a weapon-backed effect. When
// itemsanity is active, Shield and Mask remain queued until their weapon unlock
// has arrived; ids outside the frozen itemsanity weapon set stay available.
int AP_ItemsanityWeaponAvailable(int heldItemID);
int AP_GateCountTokenColour(int colour); // colour 0..4 = R,G,B,Y,P -> token idx 4+colour
int AP_GateCountGemColour(int colour);   // colour 0..4 = R,G,B,Y,P -> gem   idx 9+colour

// ── any-of aggregate counters (requirement_specificity = any_of) ──
// Sum the received counts across a whole item TYPE, for the type 6/7/8 gates.
int AP_GateCountTokenSum(void); // all 5 token colours (idx 4..8)
int AP_GateCountRelicSum(void); // all 3 relic tiers  (idx 1..3 = Sapphire+Gold+Platinum)
int AP_GateCountRelicTier(int colour); // type-4 tier: 0=Sapphire,1=Gold,2=Platinum; -1=Sapphire
int AP_GateCountGemSum(void);   // all 5 gem colours  (idx 9..13)

// Oxide's Final Challenge door gate (issue #23). Reads ctr_cfg.oxide_final_unlock
// (mode) + ctr_cfg.oxide_final_count against the received relic-tier counts:
//   sapphire/gold/platinum -> that tier >= count
//   any    -> any single tier >= count      total -> Sapphire+Gold+Platinum >= count
// Relic tiers are INDEPENDENT (no downward hierarchy) -- matches the apworld
// completion condition exactly. When slot_data is absent, falls back to the
// Phase-1 vanilla rule (18 Sapphire). Returns non-zero when the door should open.
int AP_OxideFinalOpen(void);

// Persistent on-screen warning drawn on the adventure hub when the connected
// seed's slot_data schema is NEWER than this build understands (issue #8;
// ctr_cfg.schema_newer). Self-gates; a no-op on matching/older seeds.
void AP_DrawSchemaWarning(void);

// ── Pair-version update notice (issue #150) ──
// INFORMATIONAL notice shown when the connected seed's ctr_options.world_version
// is a HIGHER pair version than this build's CTR_AP_VERSION -- i.e. a newer
// client/apworld pair exists. Nothing about the session is broken by this, so it
// draws in ORANGE and is deliberately NOT the RED schema banner above (RED means
// a compatibility break). Zero network: the whole test is the seed's own string
// against the compiled one. Both draws self-gate -- no slot_data, an absent /
// unparseable version, an equal or older seed, or [Archipelago] update_check =
// false and they are no-ops.
//
// Title screen (game/230/MM_MenuFlow.c): ONE line, no version numbers -- it only
// announces that an update exists. Shown once per pair version, remembered across
// sessions in [State] update_last_seen so the same version never nags twice.
void AP_DrawTitleUpdateNotice(uint32_t *ot);
// OPTIONS > Connection status area (game/230/MM_ConfigMenu.c): the PERSISTENT and
// DETAILED surface -- it carries the seed's version and this client's, and it
// ignores update_last_seen, so a player who saw the title line can always come
// here to find out which version the seed wants. Draws its lines centred on
// `centreX` starting at `y`, `spacing` apart.
void AP_DrawConnUpdateNotice(uint32_t *ot, int centreX, int y, int spacing);
// True while the detailed Connection-screen update notice owns the footer.
int AP_ConnUpdateNoticeActive(void);

// ── Seed completability verification (ap_verify.c) ──
// Typed requirement comparator against an arbitrary counts array (15 AP_IDX_*
// slots). The single source of truth for requirement semantics: AP_BossReqMet
// delegates here with the live tallies; the verify sweep passes simulated ones.
int AP_ReqMetCounts(const ctr_req *r, const int *counts);

// Per-frame verdict tick + the solo-seed "not completable" banner (drawn at the
// AP_DrawSchemaWarning call sites). Full contract in ap_verify.h.
void AP_VerifyOnFrame(void);
void AP_DrawVerifyWarning(void);

#ifdef CTR_CUSTOM_TRACKS
// Alpha6 manager-light state shared by OPTIONS > Custom Content, connect-time
// seed preflight, and the Gem Cup entry gate.
const struct CustomTrackManagerStatus *AP_CustomContentStatus(void);
int AP_CustomContentSeedSelected(void);
int AP_CustomContentRequired(void);
void AP_CustomContentRescan(void);
void AP_CustomContentVerify(void);
int AP_CustomContentDownloadStart(void);
int AP_CustomContentDownloadStatus(char *detail, int detailBytes);
int AP_CustomContentGateEventEntry(int forceVerify);
void AP_DrawCustomContentWarning(void);
#endif

// Append a line to the AP debug log (forwards to the module's AP_AppendLog).
// Exposed so the game-side gate files (game/232/AH_*.c) can emit confirmation
// lines -- e.g. AH_WarpPad_LInB logs each pad whose destination was remapped.
void AP_LogLine(const char *msg);

// Emit one AP item-box location check (#109). Lives here rather than in
// ap_boxes.c so every optional location class routes through the one #176
// emitter, with its absent-code guard, checked-state guard and diagnostic line.
void AP_EmitBoxCheck(int levelID, int slot, long code);

// 1 if Aku Aku mask hints should be SKIPPED. Read once from ap-config.txt
// (line "skip_hints=1") at connect. Honoured at the single choke point
// MainFrame_RequestMaskHint (#ifdef CTR_AP) by early-returning, so no hint is
// ever armed. Default 0 (hints behave normally). QoL / testing convenience;
// later the apworld can write this value into ap-config.txt from a YAML option.
int AP_SkipHints(void);

// 1 if the hub-map "Raceable" two-tone flicker (state 2 GREEN) is enabled.
// Default 1; set to 0 by ap-config.txt "map_flash=0" for a static GREEN. Read by
// AH_Map_Warppads (#ifdef CTR_AP). Part of the Warp-Pad State Model v2.
int AP_MapFlashOn(void);

// ── In-game connection manager (see game/230/MM_ConfigMenu.c) ──
// Tear down any existing apclientpp client and re-dial with these connection
// settings (the menu's "Connect" action). Only safe from the main menu, where no
// item is in flight and no adventure state depends on the live socket -- the menu
// is reachable only from there by construction, so no mid-session teardown occurs.
void AP_Net_Reconnect(const char *uri, const char *slot, const char *password);

// One-line connection status for the menu's status row ("Not connected" /
// "Connecting..." / "Connected" / "Error: <reason>" / "Auto-retry stopped,
// press Connect" when the pre-connect attempt budget is exhausted). Points at a
// static buffer.
const char *AP_Net_StatusLine(void);

// Graceful client teardown before process exit (main-menu QUIT row, #211): the
// same close-and-forget half of AP_Net_Reconnect, without the re-dial. Safe to
// call even if never connected (ap_net_shutdown() no-ops on a null client).
void AP_Net_Shutdown(void);

// 1 if the exhaust-fire retention tweak is enabled (keep power-slide fire
// visible while holding reserves). Default 0; set by ap-config.txt
// "hud_reserves_fx=1". Read by VehFire_Increment (#ifdef CTR_AP). Visual only;
// bundled with the ported ReservesMeter HUD.
int AP_HudReservesFx(void);

// 1 if the numpad DEV hotkeys are enabled: trap test-fire keys Numpad 1-6
// (ap_traps.c) and the Shortcutless toggle/mark keys Numpad 7-8 (ap_shortcut.c).
// Default 0 -- the keys are DEAD in normal play, so a stray numpad press (e.g.
// typing the port on the connection screen with NumLock on, issue #16) can
// never reach test machinery. Set ap-config.txt "dev_keys=1" for a dev session.
int AP_DevKeysEnabled(void);

// ── AI-difficulty preset (option-sync pattern) ──
// The selected AI-difficulty as a raw engine difficulty VALUE (0 = vanilla; the
// presets are 0x50/0xA0/0xF0/0x140/0x280). Applied by BOTS_Adv_AdjustDifficulty at
// race start, which OVERRIDES the computed difficulty with this value (mirroring
// the reference AdvDifficulty module). Sourced from the local config value (which
// the connect-time pull mirrors from the per-slot data-storage override / slot_data
// default). COMFORT ONLY -- generation never depends on it, and it re-reads every
// race start so a mid-seed change takes effect next race.
int  AP_AiDifficultyValue(void);

// Push the current local difficulty to the per-slot data-storage override (key
// "ctr_difficulty_<slot>") so it persists across sessions/devices. No-op when not
// connected. Called from the options menu on exit (see game/230/MM_ConfigMenu.c).
void AP_AiDifficultyCommit(void);

// ── Reward glow (display model revised by #212) ──
// Model id to DISPLAY in a warp-pad prize slot, for the location identified by
// its AdvProgress global bit (= word*32 + bit) on the pad's DESTINATION track.
// Returns the model of the AP item actually placed at that location, per the
// warp-pad glow matrix recorded in ap_reward_policy.h:
//   * a base-game CTR reward (trophy / relic / token / gem / key) -> its own
//     category model, whether it is MINE or another CTR PLAYER's (the peer's
//     copy is ghost-rendered rather than re-modelled -- AP_WarpPadRewardGhost);
//   * a CTR PROGRESSION item with no vanilla model of its own (the capability
//     ladder, weapon unlocks, character unlocks, letters, Gas Pedal) ->
//     STATIC_CRYSTAL, mine solid and a peer's ghosted the same way (#219). Only
//     while a crystal model is actually parked (ap_retail_crystal.c); without
//     one these fall back to the marker below;
//   * everything else, mine or anyone's (traps, comfort items, wumpa packages,
//     any apworld-invented item, and every item of another GAME) ->
//     STATIC_AP, the Archipelago-logo marker.
// The generic white gem is retired and is never returned any more.
// Returns -1 when not connected, not yet scouted, the bit is not a checkable
// location, or the marker model is not registered yet, so the caller keeps the
// placeholder model. Used by AH_WarpPad_LInB (#ifdef CTR_AP).
int AP_WarpPadRewardModel(int globalBit);

// 1 when the scouted reward here belongs to another CTR PLAYER and keeps a model
// of its own -- base-game reward or progression crystal -- so it renders
// TRANSLUCENT (#212 point 2, the time-trial ghost treatment). 0 for own rewards,
// for marker items and for anything unresolved.
// The caller must zero colorRGBA on this path -- see the ghost block in
// AH_WarpPad_ThTick for why the ghost writer requires it.
int AP_WarpPadRewardGhost(int globalBit);

// Translucency for a ghosted peer reward, as the GTE's IR0 interpolation factor.
// Vanilla's time-trial ghost writes this exact value as a bare literal
// (GhostReplay.c:83, `inst->alphaScale = 0xa00`) and never names it, so this is
// a deliberately documented MIRROR of that line rather than a second definition
// of a constant that already has a name: change one and change the other only if
// pad ghosts are meant to differ from kart ghosts.
#define AP_PAD_GHOST_ALPHA 0xa00

// Reward-glow TINT. Vanilla renders every relic tier the same blue, so a glow
// advertising a Sapphire / Gold / Platinum relic looked identical. Returns a
// tier-specific packed colorRGBA for an OWN relic scouted at this location; the
// vanilla crystal purple for an OWN progression crystal (#219); the
// AP classification colour (or the one uniform "surprise" colour, when the seed
// sets ctr_options.ap_item_type_colors = 0) for an Archipelago-logo marker; or 0
// to keep the caller's default colour (own gem/trophy/token/key, ghosted peer
// rewards, unscouted). Caller applies it to relic + gem + marker models.
int AP_WarpPadRewardTint(int globalBit);

// Re-present a token/challenge ceremony prop with the scouted reward at this
// location. Returns 0 when the vanilla token must remain as the safe fallback.
int AP_CeremonyRewardProp(struct Instance *prop, int globalBit);

// Fixed-point ceremony scale multiplier for the resolved reward (0x1000 = 1x).
int AP_WarpPadRewardScale(int globalBit);

// ── Requirement-hologram relic tint (closed pad) ──
// The relic icon shown on a LOCKED pad advertises that pad's relic REQUIREMENT.
// Vanilla renders it in one blue; the reward-glow rework cycles it through the 3
// tiers so an AnyRelic req reads as "any tier". A specific-tier req (schema-4
// type 4) must instead pin THAT tier's tint. LInB owns the physical-pad/stage
// selection that decides which requirement a pad shows, so it records the shown
// req's tint tier here; AH_WarpPad_ThTick (destination-keyed, must not re-run
// that selection) reads it back keyed by the physical pad.
//
// AP_ReqRelicTintTier maps a resolved requirement to that tier: 0/1/2 =
// Sapphire/Gold/Platinum for a type-4 req (legacy colour -1 = Sapphire, matching
// AP_GateCountRelicTier); any other type -- including AnyRelic (type 7) -- returns
// a negative value meaning "keep the sapphire->gold->platinum cycle".
int AP_ReqRelicTintTier(const ctr_req *r);

// Record / read the tint tier for a physical pad's closed-req relic hologram.
// physLevelID is the PHYSICAL pad LevelID (0..27 warp pads, 100..104 gem cups).
// A negative stored value (the default, and what unknown pads return) means cycle.
void AP_SetWarpReqRelicTint(int physLevelID, int tier);
int  AP_WarpReqRelicTint(int physLevelID);

// ── Requirement-hologram gem colour (closed pad) ── the gem sibling of the
// relic-tint machinery above. A specific-colour gem requirement (type 5) must
// pin its ONE colour on the closed-pad gem hologram -- the vanilla 5-colour
// cycle made a "1x Blue Gem" gate read as "any 1 gem" (live confusion,
// 2026-07-15 playtest). Only a genuine AnyGem req (type 8) keeps the cycle.
// AP_ReqGemColour maps a resolved requirement to its gem colour index (0..4 =
// R,G,B,Y,P, straight onto data.AdvCups[]); any other type -- including AnyGem
// -- returns negative meaning "keep the colour cycle".
int  AP_ReqGemColour(const ctr_req *r);
void AP_SetWarpReqGemColour(int physLevelID, int colour);
int  AP_WarpReqGemColour(int physLevelID);

// ── Requirement-icon token colour (closed pad) ── the token sibling (issue
// #19): the icon-birth cup-index clamp made every non-cup token gate read as
// Red (a "1x Purple CTR Token" gate shown as a red token, live report
// 2026-07-16). AP_ReqTokenColour maps a resolved requirement to 0..4 = R,G,B,
// Y,P (specific-colour type 3, straight onto data.AdvCups[]), -1 = cycle all
// five (AnyToken type 6), or -2 = no resolved token requirement -> the display
// leaves the birth colour alone (a vanilla cup pad's token icon is a FIXED cup
// colour, so no-opinion must be distinct from cycle).
int  AP_ReqTokenColour(const ctr_req *r);
void AP_SetWarpReqTokenColour(int physLevelID, int colour);
int  AP_WarpReqTokenColour(int physLevelID);

// Colour index (0..4 = R,G,B,Y,P) of the OWN Gem scouted at this location, or
// -1 when the scout is not an own gem. The gem model has NO useful unmodulated
// colour -- at colorRGBA 0 it renders near-black (the specular sibling of the
// black-key bug; vanilla always modulates gems, e.g. CS_Podium via
// data.AdvCups[cup].color). The reward glow therefore tints an OWN gem by the
// scouted item's colour; same data.AdvCups mapping as tokens.
int AP_WarpPadRewardGemColour(int globalBit);

// Colour index (0..4 = R,G,B,Y,P) of the OWN CTR Token scouted at this location,
// or -1 when the scout is not an own token (foreign / unscouted / other
// category). The reward glow must tint a token model by the SCOUTED ITEM's
// colour, not the destination track's vanilla token group -- under item shuffle
// a Blue CTR Token can sit at a location whose vanilla token is Yellow. The
// index maps directly onto data.AdvCups[] (same R,G,B,Y,P order). Caller falls
// back to the vanilla token group on -1 (unscouted placeholder).
int AP_WarpPadRewardTokenColour(int globalBit);

// 1 if the AP location at `globalBit` (= word*32+bit) has been CHECKED on the
// server for our own slot. Use this -- NOT CHECK_ADV_BIT on the AdvProgress bits
// -- to ask "has the player completed this location" in AP mode: AP_ApplyItems
// clears any location bit not backed by a received item every frame, so the raw
// bit can't reflect a local win. Returns 0 if not a checkable bit / not connected.
int AP_LocationCheckedByBit(int globalBit);
int AP_LetterAvailable(int track, int letter);
long AP_LetterLocation(int track, int letter);
void AP_LetterCollected(int track, int letter);
void AP_LetterUnavailableTouched(int track, int letter);
int AP_LettersRequiredMet(int track);
int AP_LettersRequiredCount(int track);
int AP_LetterTokenEarned(int track, int didWin, int collected);
void AP_WumpaReachedTen(struct Driver *driver);

// 1 if the AP location at `globalBit` is a REAL location this SEED (present in
// AP's own missing/checked location set for our slot -- see ap_net_location_exists).
// AP_LookupLocationCode alone is NOT enough to answer this: it resolves against the
// compile-time 101/112-name SUPERSET table, which is seed-independent, so it stays
// >= 0 even for a relic Time Trial this particular seed never created (issue
// #171/#28 R1: a below-count tier is removed, not pinned). Use this wherever a
// per-seed "does this check exist at all" answer is needed -- the location-send
// guard (AP_NotifyAdvReward) and every glow/notice enumerator
// (AP_WarpPadUncollectedBits, AP_PadUncollectedBits) -- so a removed slot is never
// sent, and never advertised as an outstanding check. Returns 0 if not a checkable
// bit / not connected.
int AP_LocationExistsByBit(int globalBit);

// Itemsanity (#145): use-time check emission and receive-gated roulette.
// The filters are local-player and active-seed safe; inactive/absent seeds and
// nonlocal drivers receive the vanilla roll unchanged.
// AP_ItemsanityOnUse takes the driver's own held id at the circle-press choke
// point, never the folded fire-time weapon id. AP_ItemsanitySubstituteOwned
// keeps vanilla's downstream item rewrites inside the received set, and never
// re-issues Warpball or Missile x3, whose one-at-a-time and two-holder caps are
// the rewrites it is standing inside.
// AP_ItemsanityBossAssist guards the boss-race rewrite block the same way, but
// walks a fixed strength ladder instead of a substitution pool, because retail
// catch-up assistance is a deliberate escalation rather than a draw.
// AP_ItemsanityFilterCrystalGrant gates the Crystal Challenge arena hardcode,
// which reaches none of the filters above because its itemset routing skips the
// roulette table entirely (ruled 2026-08-20: itemsanity applies to arenas too).
void AP_ItemsanityOnUse(struct Driver *driver, int heldItemID);
int AP_ItemsanityFilterRoll(struct Driver *driver, int rolled, unsigned roll,
                            const unsigned char *table, int tableCount);
int AP_ItemsanityFilterCrystalGrant(struct Driver *driver, int proposed);
int AP_ItemsanitySubstituteOwned(struct Driver *driver, int proposed,
                                 unsigned roll, const unsigned char *table,
                                 int tableCount);
int AP_ItemsanityBossAssist(struct Driver *driver, int proposed, int rolled);

// Item Reroll (#280): when the roll that resolves a trap-started roulette lands
// on the very weapon the trap threw away, substitute a different owned one.
// Returns the roll unchanged when no reroll is in flight, when the driver is not
// the local player, or when the pool holds no eligible alternative. Consumes no
// RNG draw and grants no Wumpa; it never widens the received weapon set.
int AP_TrapRerollFilter(struct Driver *driver, int rolled, unsigned roll);

// Merged relic-tier ownership for `globalBit` (must be one of the 54 Sapphire/
// Gold/Platinum Time Trial bits, 22..75) -- the package-3 (#28 R1) local-grant
// answer to "does the player own this specific tier on this track". Two disjoint
// sources, selected by whether the slot exists this seed:
//   - REAL this seed (AP_LocationExistsByBit true): server truth,
//     AP_LocationCheckedByBit -- unaffected by local-grant, exactly as before.
//   - REMOVED this seed (a below-count tier, #171/#28 R1): there is no AP
//     location to ask, so the raw AdvProgress bit IS the record -- set once by
//     the grant site's own UNLOCK_ADV_BIT and never touched again by
//     AP_ApplyItems (see its own local-grant skip), so it persists exactly like
//     a vanilla flag (survives ticks, reconnects, and save/load) with zero AP
//     meaning: it never derives from or feeds ap_recv_count / any gate.
// Returns 0 for a bit outside the relic ranges (defensive; callers only pass
// relic bits) or when not connected.
int AP_RelicRewardOwnedByBit(int globalBit);

// ── Reward-glow uncollected enumeration ──
// Fill `outBits` (capacity `cap`) with the still-UNCOLLECTED (unchecked) AP reward
// locations of race track `destLevelID` (0..15), in fixed tier order: Trophy
// +0x06, Sapphire +0x16, Gold +0x28, Platinum +0x3a, CTR Token +0x4c. Returns the
// count written (0..5). "Collected" is decided ONLY by AP checked-state (the
// server's set), never by CHECK_ADV_BIT / modelIndex (which AP_ApplyItems clears
// every frame). Tiers absent from this seed's location table are skipped. Returns
// 0 for a non-race destination or when all tiers are checked. The warp-pad glow
// (AH_WarpPad_LInB / _ThTick, #ifdef CTR_AP) drives its model/tint/visibility and
// the multi-reward cycle entirely off this list -- fully decoupled from the
// vanilla pad-state machine.
int AP_WarpPadUncollectedBits(int destLevelID, int *outBits, int cap);

// ── Unified pad state (Warp-Pad State Model v2) ──
// Category-general uncollected-location enumerator: fills `outBits` (capacity
// `cap`) with the still-unchecked AP reward locations of destination
// `destLevelID` for ANY category (race 0..15 = 5 tiers; trial 16/17 = 3 relic
// tiers; arena 18/19/21/23 = 1 crystal; cup 100..104 = 1 gem) and returns the
// count. The category-general sibling of AP_WarpPadUncollectedBits (race-only).
int AP_PadUncollectedBits(int destLevelID, int *outBits, int cap);

// ── Podium rungs in the reward glow ──
// Podium-ladder rungs carry no AdvProgress bit (absent from AP_LOCATION_TABLE),
// so the bit-keyed glow pipeline addresses them via PSEUDO-BITS:
// AP_PODIUM_PSEUDO_BASE + track*CTR_CFG_PODIUM_RUNG_COUNT + rung (schema >= 6:
// 0 held_1st / 1 held_3rd / 2 held_5th / 3 finish_podium / 4 finish_any), safely
// above the 192-bit AdvProgress space. Custom builds extend the logical track
// range with the 32 frozen generic slots and reserve direct pseudo-bits for the
// custom Trophy and per-destination Wumpa check. AP_LookupLocationCode translates
// them to per-seed location codes, so every downstream bit-keyed helper (reward
// model, tint, checked-state and scouts) works unchanged.
//
// AP_PadUncollectedGlowBits = AP_PadUncollectedBits PLUS the still-unchecked rung
// pseudo-bits: for a RACE destination its own track's rungs; for a CUP destination
// the rungs of all four leg tracks (advCupTrackIDs). A displaced custom cup uses
// its generic Trophy, podium and per-destination Wumpa identities instead of the
// absent retail Gem and leg identities. AP_PadState consumes this same complete
// enumeration specifically so Done can never strand an attached check.
#ifndef CTR_CUSTOM_TRACKS
#define AP_PODIUM_PSEUDO_BASE 0x100
#endif
int AP_PadUncollectedGlowBits(int destLevelID, int *outBits, int cap);

// ── Prize-slot layout (issue #59) ──
// Given the `n` bits AP_PadUncollectedGlowBits enumerated for a pad and the
// display tick `phase` (the caller's frame counter / 0x3C, one step per 2s),
// decide which bit each of the pad's three prize slots advertises, writing them
// into outSlot3 (-1 = hide that slot). Honours ctr_cfg.warp_pad_item_display:
// one pile cycling a 3-wide window (the default, and the behaviour of every seed
// predating the option) or one slot per reward type rotating within its type.
// Display only -- reads no gate and moves no state.
void AP_PadGlowSlots(const int *bits, int n, int phase, int *outSlot3);

// AP state-generation counter (foundation for live in-hub pad re-birth). Bumped
// on fresh slot-connect, on a received item that changes a gate count, and on a
// location-checked notification -- i.e. whenever a pad's AP_PadState may change.
// A future re-birth consumer tags each pad at birth and rebuilds it on mismatch.
// Not yet consumed for re-birth (AH_WarpPad_LInB is level-load-only; see the
// note in ap_hooks.c). The map + gates already recompute AP_PadState live.
unsigned AP_StateGen(void);

// The single pad-state function driving map colour, in-hub look, and the gate.
// Returns 1 Locked / 2 Raceable / 3 Re-locked / 4 Tier-2-open / 5 Done, or 0 for
// vanilla mode (no slot_data) / unrecognised destination (leave the pad
// untouched). physLevelID keys the requirement (physical pad); destLevelID keys
// the location + lifecycle category (loaded destination track). Consumed by
// AH_Map_Warppads + AH_WarpPad_LInB/_ThTick (#ifdef CTR_AP).
int AP_PadState(int physLevelID, int destLevelID);

// Is the PHYSICAL pad's stage-1 (entry) requirement satisfied? The per-class
// routing AP_PadState gathers with: race pads 0..15 through
// ctr_cfg_warp_unlocked, trial pads 16/17, battle arenas 18/19/21/23 (their
// vanilla branch is the hub-key gate) and cup pads 100..104. Randomized
// slot_data requirements override every class fallback. Does NOT include the
// racer lock for the non-race classes, so AND ctr_cfg_racer_lock_met when the
// caller needs the full entry answer.
//
// Exposed for the Gem Cup AP-box policy (WO-A3, ap_boxes.c), which has to ask
// this exact question about the pad hosting a cup leg's INDIVIDUAL race.
int AP_PadStage1Met(int physLevelID);

// Is this pad in the §6 box re-entry window (issue #232)? The destination's
// trophy race is checked, this pad's stage-2 is not met, and unbroken AP item
// boxes still stand behind the destination -- so AP_PadState keeps the pad at 2
// Raceable and the map paints it green. AH_WarpPad.c reads this on both of its
// surfaces: the entry gate keeps offering a plain adventure re-race (the only
// way to break a box), and the pad is born OPEN instead of advertising a stage-2
// requirement it is not actually withholding entry on. Same keying as
// AP_PadState. Returns 0 in vanilla mode and for any non-race destination.
int AP_PadBoxReRaceable(int physLevelID, int destLevelID);

// Number of unchecked item-box locations owned by a race destination. This is
// the same server-truth count used by AP_PadState, exposed so the warp-pad HUD
// can explain why a trophy-complete pad remains raceable.
int AP_PadUncollectedBoxCount(int destLevelID);

// Number of enabled Lettersanity locations still unchecked for a race
// destination. These belong to the CTR Challenge race type and therefore keep
// its side of the tier-2 route available even after the Token check itself.
int AP_PadUncollectedLetterCount(int destLevelID);

// ── Pad entry-route diagnostic (issues #232 / #265) ──
// Emit ONE "[AP PAD]" line describing the entry route this pad just took, and
// the state it was read out of. Deduplicated on the whole reported tuple, so an
// unchanged pad decision logs once per TRANSITION rather than once per frame --
// the gates below run every frame the player stands on a pad.
//
// Written for the #232 / #265 combined sequence: it makes "which branch did the
// pad take, and what did it believe" readable from a plain log, which is the
// one thing the two issues' live re-test otherwise has to infer from behaviour.
// No-op in vanilla mode. Same keying as AP_PadState (physical pad = requirement,
// destination = locations). `route` is an enum AP_PadRoute value, declared in
// ap_pad_state.h beside the decision it labels.
void AP_PadLogRoute(int physLevelID, int destLevelID, int route);

// ── Gem-cup return hub (destination-shuffle correctness) ──
// Vanilla returns the player to Gemstone Valley after EVERY gem cup, because a
// cup's four track loads clobber gGT->prevLEV (each MainRaceTrack_RequestLoad
// records prevLEV = the previous TRACK, not the entry hub) and, in the retail
// game, gem cups only ever live in Gemstone Valley -- so the GEM_STONE_VALLEY
// literal is always right. Under warp-pad DESTINATION shuffle a gem cup can be
// hosted on a physical pad in ANY hub, so a player can enter a cup while GV is
// still locked; returning to GV then strands them outside the unloaded map
// (in-game repro, 2026-07). AH_WarpPad_ThTick records the physical
// entry hub at cup ENTRY (gGT->levelID, captured before the track load
// overwrites it); the two cup exit-to-map sites (game/UI/UI_RaceFlow.c,
// game/MAIN/MainFreeze.c) call AP_CupReturnHub() instead of using the
// GEM_STONE_VALLEY literal. AP_CupReturnHub returns -1 (AP inactive or unset) ->
// caller keeps the vanilla GEM_STONE_VALLEY return.
void AP_CupEnterFromHub(int hubLevelID);
int  AP_CupReturnHub(void);

#endif // CTR_AP

#endif // AP_HOOKS_H
