#ifdef CTR_AP

#include <common.h>
#include <stdarg.h>
#include <stdio.h>
#include <namespace_Decal.h> // FONT_*, colour + JUSTIFY_* enums for the ceremony draw

#include <stdlib.h> // realloc: the #299 held-effect list
#include "ap_hooks.h"
#include "ap_deathlink.h" // AP_RaceAttemptIsForcedLoss: #286 result-producer latch
#include "ap_win_logic.h" // #449 received-items-by-id tally for the race stakes
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h> // the seed-driven custom-track descriptor
#include <platform/native_assets.h>
#include "ap_custom_track_download.h"
#include "ap_custom_trophy_ceremony_logic.h"
#endif
#include "ap_ceremony_logic.h"
#include "ap_version.h"     // CTR_AP_VERSION -- this build's half of the shipped pair
#include "ap_version_cmp.h" // freestanding pair-version comparator (#150)
#include <ctr_menu_ux.h>     // connection-menu error guidance
#include "ap_locations.h" // generated bit_index -> AP location_code table (99 locs)
#include "ap_net.h"       // C API into the apclientpp network client (ap_net.cpp)
#include "ap_door_history.h"
#include "ap_items.h"     // item-id -> AdvProgress category bit pools
#include "ap_item_flags.h" // AP classification flags + shared precedence (#195)
#include "ap_item_aliases.h" // native display aliases for long item names (#324)
#include "ap_reward_text.h" // pure bounded boss/pad reward sentence builder (#330)
#include "ap_rung_feed_reason_logic.h" // freestanding held-position reason text (#324)
#include "ap_class_check_policy.h" // freestanding class-check send/toast guards (#319)
#include "ap_relic_perfect.h" // freestanding Relic Race perfect decision (#49)
#include "ap_glow_slots_logic.h"
#include "ap_pad_glow_items.h" // box / letter / Wumpa pad display identities
#include "ap_trial_pad_glow.h" // SC/TT Trophy + CTR pad display identities (#343)
#include "ap_trial_letters.h" // AP_TrialLetters_Prepare: can the SC/TT CTR Challenge be served
#include "ap_cortex_track.h" // Cortex Vortex pad track: direct codes + pseudo-bits (schema 15)
#include <platform/native_cortex_track_latch.h> // destination 110 -> host level 13 (freestanding)
#include "ap_traps.h"      // trap-effect framework (per-frame tick + config trigger)
#include "ap_transition_diag.h" // ap-state.json transition.diag formatter (diagnostics only)
#include "ap_checkdiag_once.h" // once-per-connect gate for [AP CHECK DIAG] lines (log-spam guard)

static ap_checkdiag_once_state ap_checkdiag_once; // [AP CHECK DIAG] once-per-connect gate; reset at fresh connect
#include "ap_trap_items.h" // apworld item id -> trap effect, the 19 scattered ids
#include "ap_fxseen_logic.h" // 0.2.1 local ledger rows, read for migration only (#299)
#include "ap_fxmarker_logic.h" // room-scoped one-shot effect markers (#299)
#include "ap_democam.h"   // Demo Camera trap and direct live-test trigger
#include "ap_shortcut.h"  // Shortcutless mechanism (key poll + config trigger)
#include "ap_wumpa.h"     // Wumpa Fruit filler grant (bank-on-receive, grant in-race)
#include "ap_wumpa_dispatch.h" // freestanding "which Wumpa code does this crossing send"
#include "ap_cup_box_policy.h" // AP_BoxPadAccessible: the shared individual-pad rule
#include "ap_crash.h"     // crash reporter (support-bundle feature)
#include "ap_perf.h"      // always-on frame-stall watchdog ([AP PERF] log lines)
#include "ap_file_writer.h"    // background writer for whole-file rewrites
#include "ap_text_buf_logic.h" // in-memory text for those rewrites
#include "ap_marker_model.h" // STATIC_AP + the compiled-in AP-logo marker model (#124)
#include "ap_reward_policy.h"  // category -> model / tint, the one display decision (#219)
#include "ap_podium_presentation_logic.h" // AP Trophy prize policy (#235)
#include "ap_podium_skip_logic.h" // local podium-skip policy (#285)
#include "ap_retail_crystal.h" // the harvested retail crystal for CTR progression (#219)
#include "ap_retail_wumpa.h"   // the harvested retail Wumpa Fruit model for Wumpa packages (#222)
#include "ap_box_texture.h"   // AP_BoxTexture_Prepare: the AP box wood and crate face from the disc
#include "ap_wumpa_residency_logic.h" // freestanding Wumpa residency transition (#222)
#include "ap_surface.h"    // permanent natural-surface comfort items (#14/#15)
#include "ap_capability.h" // progressive boost + progressive stats (#12/#13)
#include "ap_tizi.h"       // Papu's Pyramid mask helper (#223)
#include "ap_useful.h"     // H-dossier useful grants
#include "ap_itemsanity_logic.h" // #145 frozen weapon ids + pure roulette filter
#include "ap_held_race_rearm.h" // pure per-attempt boundary latch for held rungs
#include "ap_boss_won_flags.h" // #458 personally won boss races and Oxide's Challenge
#include "ap_spawn.h"      // additive model loader (#109 / #124 groundwork)
#include "ap_author.h"     // in-game box placement author mode (#182)
#include "ap_boxes.h"      // AP item boxes: spawn, player-break, check (#109)
#include "ap_pad_state.h"  // freestanding Warp-Pad State Model v2 decision table
#include "ap_hit_policy.h" // freestanding Hit Character encounter decisions (ticket 06)
#include "ap_hit_encounter.h" // Hit Character gather: eligibility + roster + dispatch (ticket 06)
#include "ap_hit_chooser.h" // AP_HIT_CHOOSER_* action codes for the chooser log line
#include "ap_relic_goal.h" // shared Oxide Final relic-count rule (#273)
#include "ap_goal_presentation.h" // composed-goal credits edge (#244)
#include "ap_goal_logic.h" // pure composed-goal predicate (#152/#244)
#include "ap_oxide_encounter.h" // pure Oxide garage encounter + gate decision (#320/#321)
#include "ap_oxide_cutscene.h" // Oxide Final-Challenge presentation readiness (WO-A4)
#include "ap_goal_line.h"       // pure compact pause-menu goal checklist (#322)
#include "ap_oxide_garage_advert.h" // pure multi-line garage panel (#322 repair, 2026-09-03)

// ap_reward_policy.h stays freestanding by mirroring the engine's model ids
// rather than including namespace_Instance.h. ap_trap_items.h mirrors AP_ITEM_BASE
// for the same reason: the host harness has to compile it without ap_items.h. Pin
// every mirror against the real definition here, where both are visible, so no
// copy can drift (#219).
CTR_STATIC_ASSERT(AP_TRAP_ITEM_ID_BASE == AP_ITEM_BASE);
CTR_STATIC_ASSERT(AP_MODEL_CRYSTAL == STATIC_CRYSTAL);
CTR_STATIC_ASSERT(AP_MODEL_GEM == STATIC_GEM);
CTR_STATIC_ASSERT(AP_MODEL_RELIC == STATIC_RELIC);
CTR_STATIC_ASSERT(AP_MODEL_WUMPA == PU_WUMPA_FRUIT);
#ifdef CTR_CUSTOM_TRACKS
// manager-light measures wumpa_collectible by walking a LEV's instance table for
// these two model ids, and mirrors them because platform code compiles without
// the engine headers. This is the one place both definitions are visible.
CTR_STATIC_ASSERT(CTR_CT_MODEL_FRUIT_CRATE == PU_FRUIT_CRATE);
CTR_STATIC_ASSERT(CTR_CT_MODEL_WUMPA_FRUIT == PU_WUMPA_FRUIT);
#endif
CTR_STATIC_ASSERT(AP_MODEL_TROPHY == STATIC_TROPHY);
CTR_STATIC_ASSERT(AP_PODIUM_TROPHY_MODEL == STATIC_TROPHY);
// #285 podium-skip policy mirrors (ap_podium_skip_logic.h). Pin every mirrored
// model id and gameMode2 bit against the engine definition here, where both
// sides are visible, so a copy can never drift.
CTR_STATIC_ASSERT(AP_PODIUM_SKIP_TROPHY_MODEL == STATIC_TROPHY);
CTR_STATIC_ASSERT(AP_PODIUM_SKIP_RELIC_MODEL == STATIC_RELIC);
CTR_STATIC_ASSERT(AP_PODIUM_SKIP_ADVENTURE_MODE == ADVENTURE_MODE);
CTR_STATIC_ASSERT(AP_PODIUM_SKIP_RELIC_RACE == RELIC_RACE);
CTR_STATIC_ASSERT(AP_PODIUM_SKIP_INC_RELIC == INC_RELIC);
CTR_STATIC_ASSERT(AP_PODIUM_SKIP_INC_KEY == INC_KEY);
CTR_STATIC_ASSERT(AP_PODIUM_SKIP_INC_TROPHY == INC_TROPHY);
CTR_STATIC_ASSERT(AP_PODIUM_SKIP_FREEZE_PODIUM == VEH_FREEZE_PODIUM);
CTR_STATIC_ASSERT(AP_MODEL_KEY == STATIC_KEY);
CTR_STATIC_ASSERT(AP_MODEL_TOKEN == STATIC_TOKEN);

// Weapon ownership, rebuilt from ReceivedItems on every fresh connect. The item
// index range itself now lives in ap_itemsanity_logic.h with the rest of the
// frozen weapon facts, so the reward-display policy can classify a weapon unlock
// without pulling in this runtime (#219).
static unsigned char ap_itemsanity_owned[AP_ITEMSANITY_WEAPON_COUNT] = {0};

#ifdef CTR_CUSTOM_TRACKS
static struct CustomTrackManagerStatus ap_custom_content_status;
static int ap_custom_content_scanned = 0;
static int ap_custom_content_seed_selected = 0;
static int ap_custom_content_required = 0;
static unsigned int ap_custom_content_gate_timer = 0;
static int ap_custom_content_gate_cached = 0;
static int ap_oxide_final_content_required = 0;
#endif
// Schema 15: the seed turns the Cortex Vortex pad track on but its block was
// refused or the bundled pair is unavailable. Every pad and cup that leads to
// destination 110 then refuses entry; nothing falls back to Oxide Station.
static int ap_cortex_track_content_required = 0;
// Cortex Vortex letter items (35010200..202), rebuilt from ReceivedItems on
// every fresh connect like the retail letter rows.
static unsigned char ap_cortex_letter_received[CTR_CFG_LETTER_COUNT] = {0};

// ==============================================================
// Archipelago integration. Parts:
//
//   NETWORK (ap_net.cpp / apclientpp): connect once at boot, pump every frame
//   in all game modes so the handshake completes at the title screen and items
//   can arrive anytime. Received items are surfaced here; checks and the goal
//   are sent from the LOCATION EVENTS below.
//
//   LOCATION EVENTS (option A -- AUTHORITATIVE): the game's reward-grant sites
//   call AP_NotifyAdvReward() the instant the player earns a checkable reward
//   (trophy/relic/token/gem/boss), and AP_NotifyGoal() when Oxide is beaten.
//   Reliable regardless of load/menu state. Each fires a network LocationChecks
//   / StatusUpdate(GOAL).
//
//   DEBUG poll: per-frame scan logging *unmapped* AdvProgress bit changes
//   (story/hint flags) for visibility only -- never drives checks.
//
// TODO(ap): apply received items into AdvProgress -- needs the item-id -> bit
// map (ap_items.h, next step). Today received items are logged, not granted.
// ==============================================================

// Raw keyboard probe for the map-overlay hotkey (platform/native_input.c, CTR_AP
// only). Declared here rather than including <SDL3/SDL.h> so the AP module stays
// free of SDL headers in the unity build.
int Platform_InputRawKeyDown(int scancode);

// SDL_SCANCODE_M, anchored to externals/SDL/include/SDL3/SDL_scancode.h:75
// (SDL_SCANCODE_M = 16). Not in s_keyboardMapping defaults (native_input.c:236),
// so toggling the overlay never disturbs gameplay input.
#define AP_MAP_OVERLAY_SCANCODE 16

// AP debug log, next to the exe/config (the working directory) so every user
// HAS one and the support-bundle scripts can collect it. The previous path was
// a hardcoded dev-machine location: fopen failed silently on any other Windows
// box (no AP log at all for exactly the users we need logs from) and on Linux
// it became a literal "D:\..." FILENAME in the working directory.
#define AP_READ_LOG "ctr-ap.log"
#define AP_READ_LOG_OLD "ctr-ap.log.old"
#define AP_READ_LOG_MAX (4 * 1024 * 1024) // rotate at 4 MB, keep one generation

// One handle for the whole run. Each line used to be its own fopen/fputs/
// fclose on the game thread, and field logs show that open/close pair costing
// up to about a second on some Windows hosts (logio= on the PERF lines). The
// handle is opened once, in append mode like before, and every line is flushed
// straight away: that is one write call instead of an open and a close, and a
// line is in the file the moment AP_AppendLog returns, so a crash or a killed
// process loses nothing and the crash reporter (ap_crash.c), which appends
// through its own handle, still lands after the last line.
static FILE *ap_log_file = NULL;

static void AP_AppendLog(const char *msg)
{
	// [AP PERF] LOG_IO section: the line write on the game thread, summed across
	// all lines the frame writes.
	AP_PerfSectionBegin(AP_PERF_SEC_LOG_IO);

	// One-time size check: an append-forever log grows unbounded across weeks
	// of sessions. Rotate the previous log to .old at the first write of a run
	// (crash-safe: rename happens before any new content, so a crash tail is
	// never lost mid-run).
	static int checked = 0;
	if (!checked)
	{
		checked = 1;
		FILE *probe = fopen(AP_READ_LOG, "rb");
		if (probe)
		{
			long sz = (fseek(probe, 0, SEEK_END) == 0) ? ftell(probe) : 0;
			fclose(probe);
			if (sz > AP_READ_LOG_MAX)
			{
				remove(AP_READ_LOG_OLD);
				rename(AP_READ_LOG, AP_READ_LOG_OLD);
			}
		}
	}
	fputs(msg, stderr);
	if (ap_log_file == NULL) // first line, or the open failed before: retry
		ap_log_file = fopen(AP_READ_LOG, "a");
	if (ap_log_file)
	{
		fputs(msg, ap_log_file);
		fflush(ap_log_file);
	}

	AP_PerfSectionEnd(AP_PERF_SEC_LOG_IO);
}

// Clean exit (Platform_Shutdown): let the background writer finish the last
// queued ap-state.json snapshot, bounded so a stuck disk cannot hang the exit,
// then close the log. Every line is already flushed; a line written after this
// reopens the handle.
void AP_ShutdownFiles(void)
{
	ap_file_writer_flush(2000);
	if (ap_log_file != NULL)
	{
		fclose(ap_log_file);
		ap_log_file = NULL;
	}
}

// Non-static shim so game-side gate files can emit AP log lines (AP_AppendLog is
// static to this module). Declared in ap_hooks.h.
void AP_LogLine(const char *msg)
{
	AP_AppendLog(msg);
}

// Skip Aku Aku mask hints (QoL / testing). Set from ap-config.txt at connect;
// read by MainFrame_RequestMaskHint to early-return. Default off.
static int ap_skip_hints = 0;
int AP_SkipHints(void)
{
	// Precedence (migration window, one release): if the in-game options menu has
	// written a config.ini, its skip_hints value wins and takes effect live (the
	// menu mutates g_config in place). Without a config.ini we fall back to the
	// legacy ap-config.txt "skip_hints=" key parsed into ap_skip_hints, so users
	// who never open the menu keep their current behaviour.
	if (NativeConfig_HasIni())
		return g_config.skipHints;
	return ap_skip_hints;
}

// Map flash (Warp-Pad State Model v2): the vanilla-style two-tone flicker that
// signals "Raceable" (state 2 GREEN) on the hub map. Default ON. Turned off via
// ap-config.txt ("map_flash=0") -> state 2 renders a static GREEN. Delivery
// mirrors skip_hints (ap-config.txt first; a YAML option can write it later).
static int ap_map_flash = 1;
int AP_MapFlashOn(void)
{
	// Same config.ini-over-ap-config.txt precedence as AP_SkipHints (see there).
	if (NativeConfig_HasIni())
		return g_config.mapFlash;
	return ap_map_flash;
}

// Exhaust-fire retention tweak (bundled with the ported ReservesMeter module):
// keep power-slide fire visible while holding reserves. Default OFF; set via
// ap-config.txt "hud_reserves_fx=1". Read by VehFire (#ifdef CTR_AP). Visual
// only; delivery mirrors skip_hints (a YAML option can write it later).
static int ap_hud_reserves_fx = 0;
int AP_HudReservesFx(void)
{
	return ap_hud_reserves_fx;
}

// Numpad dev hotkeys (trap test-fire Numpad 1-6, Shortcutless toggle/mark
// Numpad 7-8). Default OFF: the raw-key polls run in ALL game modes, so an
// unguarded numpad press could reach test machinery in a release build -- a
// player typing the port with NumLock on toggled Shortcutless (issue #16).
// Set via ap-config.txt "dev_keys=1"; checked inside the key handlers.
static int ap_dev_keys = 0;
int AP_DevKeysEnabled(void)
{
	return ap_dev_keys;
}

// Diagnostic (crash investigation): format a compact game-state breadcrumb for a
// log line -- frame timer, in-adventure flag, loading stage, and current levelID.
// Lets a flushed log show what the game was doing (e.g. an item arriving while a
// hub transition is mid-load). Logging only -- reads state, never mutates it.
static void AP_FmtState(struct GameTracker *gGT, char *buf, int n)
{
	snprintf(buf, n, "t=%u adv=%d load=%d lvl=%d",
	         (unsigned)gGT->timer,
	         (gGT->gameMode1 & ADVENTURE_MODE) != 0 ? 1 : 0,
	         (int)sdata->Loading.stage,
	         (int)gGT->levelID);
}

// Friendly names for the 16 track trophies (AdvProgress word 0, bits 6..21),
// for easy manual verification in the log. Everything else resolves by code.
static const char *AP_TrophyName(int globalBit)
{
	switch (globalBit)
	{
	case 6:  return "Dingo Canyon Trophy";
	case 7:  return "Dragon Mines Trophy";
	case 8:  return "Blizzard Bluff Trophy";
	case 9:  return "Crash Cove Trophy";
	case 10: return "Tiger Temple Trophy";
	case 11: return "Papu Pyramid Trophy";
	case 12: return "Roo's Tubes Trophy";
	case 13: return "Hot Air Skyway Trophy";
	case 14: return "Sewer Speedway Trophy";
	case 15: return "Mystery Caves Trophy";
	case 16: return "Cortex Castle Trophy";
	case 17: return "N. Gin Labs Trophy";
	case 18: return "Polar Pass Trophy";
	case 19: return "Oxide Station Trophy";
	case 20: return "Coco Park Trophy";
	case 21: return "Tiny Arena Trophy";
	default: return 0;
	}
}

// Resolve a process-local progress identity to its seed-carried AP wire code,
// or -1 when it is not a checkable location. The custom Trophy and Wumpa
// identities are direct pseudo-bits; podium identities use the shared
// retail-plus-custom logical track range. Every bit-keyed consumer (checked
// state, reward model/tint and scouts) therefore handles them unchanged.
static long AP_TrialTrackLocation(int levelID, int challenge);

static long AP_LookupLocationCode(int globalBit)
{
	int i;
	int trialPseudo, trialChallenge;
	int perfectLevel;
	// Schema 15: Cortex Vortex pad-track identities. Checked first because the
	// generic podium range below would otherwise claim these bits.
	if (AP_CortexPseudoDecode(globalBit, 0))
		return ctr_cfg_active() ? AP_CortexPseudoCode(&ctr_cfg.cortex_track, globalBit) : -1;
#ifdef CTR_CUSTOM_TRACKS
	if (globalBit == AP_CUSTOM_CTR_PSEUDO_BIT)
		return ctr_cfg_active() && ctr_cfg.custom_tracks_ok && ctr_cfg.custom_ctr_enabled
		           ? ctr_cfg.custom_ctr_location : -1;
	if (globalBit == AP_CUSTOM_TROPHY_PSEUDO_BIT ||
	    globalBit == AP_CUSTOM_WUMPA_PSEUDO_BIT)
		return AP_CustomPadSpecialLocationCode(&ctr_cfg, globalBit);
#endif
	// Pad display identities of item boxes, CTR letters and per-track Wumpa.
	if (AP_PadGlowIsItemBit(globalBit))
		return ctr_cfg_active() ? AP_PadGlowItemCode(&ctr_cfg, globalBit) : -1;
	// #439: the Relic Race Perfect has no AdvProgress bit either.
	perfectLevel = AP_RelicPerfectPseudoLevel(globalBit);
	if (perfectLevel >= 0)
		return (ctr_cfg_active() && ctr_cfg.relic_perfect_enabled &&
		        ctr_cfg.relic_perfect[perfectLevel] > 0)
		           ? ctr_cfg.relic_perfect[perfectLevel] : -1;
	// #343: the trial Trophy and CTR Challenge have no AdvProgress bit.
	if (AP_TrialPseudoDecode(globalBit, &trialPseudo, &trialChallenge))
	{
		long code = AP_TrialTrackLocation(16 + trialPseudo, trialChallenge);
		return code > 0 ? code : -1;
	}
	if (globalBit >= AP_PODIUM_PSEUDO_BASE)
	{
		int trial = (globalBit - AP_PODIUM_PSEUDO_BASE) / CTR_CFG_PODIUM_RUNG_COUNT
		            - AP_TRIAL_PODIUM_LOGICAL_BASE;
		if (trial >= 0 && trial < CTR_CFG_TRIAL_TRACK_COUNT)
		{
			const ctr_podium_rungs *pr = &ctr_cfg.podium[16+trial];
			long codes[5] = {pr->held_1st, pr->held_3rd, pr->held_5th,
			                 pr->finish_podium, pr->finish_any};
			return ctr_cfg.podium_enabled ? codes[(globalBit-AP_PODIUM_PSEUDO_BASE)%5] : -1;
		}
#ifdef CTR_CUSTOM_TRACKS
		return AP_PodiumPseudoLocationCode(&ctr_cfg, globalBit);
#else
		int off = globalBit - AP_PODIUM_PSEUDO_BASE;
		int track = off / CTR_CFG_PODIUM_RUNG_COUNT;
		int rung = off % CTR_CFG_PODIUM_RUNG_COUNT;
		const ctr_podium_rungs *pr;
		long code;
		if (!ctr_cfg.podium_enabled || track < 0 ||
		    track >= CTR_CFG_PODIUM_TRACK_COUNT)
			return -1;
		pr = &ctr_cfg.podium[track];
		switch (rung)
		{
		case 0:  code = pr->held_1st;      break;
		case 1:  code = pr->held_3rd;      break;
		case 2:  code = pr->held_5th;      break;
		case 3:  code = pr->finish_podium; break;
		default: code = pr->finish_any;    break;
		}
		return (code > 0) ? code : -1;
#endif
	}
	for (i = 0; i < AP_LOCATION_TABLE_LEN; i++)
	{
		if (AP_LOCATION_TABLE[i].bit_index == globalBit)
			return AP_LOCATION_TABLE[i].location_code;
	}
	return -1;
}

// 1 if the AP location at `globalBit` has been CHECKED on the server for our own
// slot. Use this -- NOT CHECK_ADV_BIT on the AdvProgress bits -- to ask "has the
// player completed this location" in AP mode: AP_ApplyItems clears any location
// bit not backed by a received item on every reconcile tick, so a local win is
// wiped and the raw bit can never reflect it. Returns 0 if not a checkable bit / not connected.
int AP_LocationCheckedByBit(int globalBit)
{
	long code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return 0;
	return ap_net_location_checked(code);
}

// See ap_hooks.h. Per-seed existence, not per-seed checked-state: distinguishes a
// real (if still unchecked) location from one this seed never created (issue
// #171/#28 R1).
int AP_LocationExistsByBit(int globalBit)
{
	long code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return 0;
	return ap_net_location_exists(code);
}

// See ap_hooks.h. Merged relic-tier ownership (issue #28 R1 local-grant): server
// truth for a real location this seed, the raw AdvProgress bit for a removed one.
int AP_RelicRewardOwnedByBit(int globalBit)
{
	long code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return 0;
	if (ap_net_location_exists(code))
		return ap_net_location_checked(code);
	return CHECK_ADV_BIT(sdata->advProgress.rewards, globalBit) != 0;
}

// See ap_hooks.h. Issue #330: name the item the boss check actually scouted
// instead of the fixed retail "Have a Key" line. Display-only and English-only;
// the Ripper Roo defeat cutscene is the only script that sets LNG_HAVE_A_KEY
// (game/233/R233.c), so CS_Thread.c substitutes only that localization index with
// the first-boss reward bit. The checked-state path and all cutscene timing are
// untouched: only the string handed to DecalFont_DrawMultiLine changes. Every
// missing or untrustworthy input (inactive AP, absent scout, unscouted location,
// unknown or unsupported text, and the own-world vanilla Key, which keeps the
// retail wording) returns `retail` unchanged. AP_LookupLocationCode stays private
// to this file; callers only see this accessor.
char *AP_RewardSubtitleForBit(int globalBit, char *retail)
{
	static char line[AP_REWARD_TEXT_MAX + 1];
	char itemRaw[128];
	char playerRaw[128];
	char alias[64];
	long code;
	long long itemId = 0;
	int player = -1;
	int own;

	if (!retail || !ctr_cfg_active())
		return retail;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0 ||
	    !ap_net_scout_known(code, &itemId, &player, NULL) ||
	    !ap_net_scout_text(code, itemRaw, (int)sizeof itemRaw,
	                       playerRaw, (int)sizeof playerRaw))
		return retail;

	own = (player == ap_net_self_slot());

	// A local vanilla Key keeps the retail "Have a Key" wording. A Key sent to
	// another player is not the local reward, so it is named as theirs instead.
	if (own && itemId == AP_ITEM_BASE + AP_IDX_KEY)
		return retail;

	if (AP_ItemDisplayAlias(itemId, alias, (int)sizeof alias))
	{
		if (!AP_RewardTextBuild(line, (int)sizeof line, alias, playerRaw, own))
			return retail;
	}
	else if (!AP_RewardTextBuild(line, (int)sizeof line, itemRaw, playerRaw, own))
	{
		return retail;
	}

	return line;
}

// ---------------------------------------------------------------------------
// MAP overlay retirement: the old debug-era per-pad map state
// (AP_BitIsUsefulUnchecked / AP_PadUsefulness / AP_BitUnchecked / AP_PadMapState)
// and its SDL_SCANCODE_M colour-override toggle (g_ap_map_overlay /
// AP_MapOverlayOn) are fully retired -- superseded by the unified AP_PadState
// (Warp-Pad State Model v2), which drives an always-on 5-state map colour and
// drops the own-progression-only GREEN criterion. The M key is now reserved for
// the deferred hub-map zoom (see AP_OnFrame).
// ---------------------------------------------------------------------------

// The generic WHITE GEM marker is RETIRED (#212, ruling 2026-08-10). It used to
// stand in for every foreign item and for own filler/unmapped items, and both
// callers now resolve to a real presentation instead: an OG CTR reward keeps its
// own model (ghosted when it belongs to another CTR player) and everything else
// renders as the Archipelago-logo marker. Nothing packs 0x0ffffff0 any more, so
// no pad glow can show a white gem that is not an actual own/peer gem-cup gem.

// ---------------------------------------------------------------------------
// #124 -- CLASSIFIED FOREIGN-ITEM DISPLAY
//
// A foreign item used to render as an undifferentiated white gem, which told
// the player nothing: pads full of white gems got skipped even when they held
// progression another player was waiting on. Items belonging to players of
// OTHER GAMES now render as the Archipelago-logo marker, tinted by the item's
// AP classification.
//
// ── REVISED BY #212 (ruling 2026-08-10, display design note amendment) ──
// The split is no longer own-vs-foreign but OG-CTR-REWARD-vs-everything-else:
//   1. an OG CTR reward (trophy / relic / token / gem / key) that is MINE
//      renders as its vanilla model, exactly as it always did;
//   2. the same reward owned by ANOTHER CTR PLAYER renders as that same vanilla
//      model, GHOSTED (the time-trial ghost translucency, AP_WarpPadRewardGhost);
//   3. EVERY other item -- traps, capability/comfort items, wumpa, anything an
//      apworld invented, mine or anyone's -- renders as the Archipelago-logo
//      marker. There is deliberately no local/foreign distinction in group 3;
//   4. the generic white gem is retired, so no glow shows one any more.
// ---------------------------------------------------------------------------

// Packed as (R << 0x14) | (G << 0xc) | (B << 0x4), matching the relic tints.
#define AP_PACK_TINT(r, g, b) (((u32)(r) << 0x14) | ((u32)(g) << 0xc) | ((u32)(b) << 0x4))

// In-game-legible approximations of the standard AP scheme, not literal hex
// values from it: these are read on a small spinning model at PSX draw distance,
// so they are pushed for separation rather than fidelity. Filler cyan and useful
// slate blue are the pair most at risk of collapsing into each other, so they are
// split hard on the green channel. Final values are a ruling.
#define AP_TINT_PROGRESSION AP_PACK_TINT(0xc0, 0x88, 0xf0) // plum
#define AP_TINT_USEFUL      AP_PACK_TINT(0x50, 0x78, 0xe0) // slate blue
#define AP_TINT_FILLER      AP_PACK_TINT(0x40, 0xe8, 0xe0) // cyan
#define AP_TINT_TRAP        AP_PACK_TINT(0xff, 0x80, 0x60) // salmon

// #212 point 5: the ONE colour every marker wears when the seed asks for AP item
// types to stay a surprise (ctr_options.ap_item_type_colors = 0). Greyish white,
// The ruling candidate: it is the only neutral that stays neutral under the
// marker's multiplicative-ish tint lerp without collapsing toward any class hue,
// and it cannot be confused with a gem-cup gem because it is never on a gem.
// Never modulate to 0 here (near-black bug) -- the value stays well clear of it.
#define AP_TINT_UNIFORM     AP_PACK_TINT(0xd0, 0xd0, 0xc8) // greyish white

// AP item flags: bit0 = progression, bit1 = useful, bit2 = trap, 0 = filler.
// The precedence that resolves them is the freestanding AP_ItemFlagsClass
// (ap_item_flags.h), shared with the hub feed so the two presentations can
// never disagree about a class.

static int AP_ClassTint(unsigned flags)
{
	// Progression wins over useful when an item carries both, matching how AP's
	// own clients present it.
	switch (AP_ItemFlagsClass(flags))
	{
	case AP_ITEM_CLASS_PROGRESSION: return AP_TINT_PROGRESSION;
	case AP_ITEM_CLASS_USEFUL:      return AP_TINT_USEFUL;
	case AP_ITEM_CLASS_TRAP:        return AP_TINT_TRAP;
	case AP_ITEM_CLASS_FILLER:      return AP_TINT_FILLER;
	}
	return AP_TINT_FILLER;
}

// The colour ONE Archipelago-logo marker wears, honouring the seed's surprise
// toggle (#212 point 5). ctr_options.ap_item_type_colors: absent or nonzero ->
// the class tints above, i.e. exactly the display every shipped client renders;
// 0 -> the single uniform colour, so the AP item pool stays a surprise.
//
// The key is EXPECTED but not required: the apworld half of #212 lands after the
// 0.2.0 name freeze, and a seed generated before it carries no such key. That is
// why the parse defaults to 1 rather than to the new behaviour -- an old seed on
// this client must glow the way it does today, not silently switch to grey.
static int AP_MarkerTint(unsigned flags)
{
	if (!ctr_cfg.ap_item_type_colors)
		return AP_TINT_UNIFORM;
	return AP_ClassTint(flags);
}

// Font-palette colour for one hub item-feed line (issue #195), resolved from the
// same class as the marker tint above. CTR's font palette has no literal RGB
// text colouring path, so this picks fixed, always-present flat ptrColors
// entries: purple (N_GIN_PURPLE) progression, blue (CRASH_BLUE) useful, cyan
// (POLAR_CYAN) filler, red (CORTEX_RED) trap. The exact hues are a legibility
// call (a design ruling): the marker tints are packed model RGBA and
// do not map 1:1 onto the palette.
static int AP_ClassFontColor(unsigned flags)
{
	switch (AP_ItemFlagsClass(flags))
	{
	case AP_ITEM_CLASS_PROGRESSION: return N_GIN_PURPLE;
	case AP_ITEM_CLASS_USEFUL:      return CRASH_BLUE;
	case AP_ITEM_CLASS_TRAP:        return CORTEX_RED;
	case AP_ITEM_CLASS_FILLER:      return POLAR_CYAN;
	}
	return POLAR_CYAN;
}

// The three presentations one scouted pad reward can have (#212). One resolver
// feeds the model, the tint and the ghost flag, so those three can never
// disagree about what a slot is showing.
#define AP_PAD_DISP_VANILLA 0 // my own model-keeping reward -> that model, untouched
#define AP_PAD_DISP_GHOST   1 // another CTR player's -> the same model, ghosted
#define AP_PAD_DISP_MARKER  2 // everything else -> the Archipelago-logo marker
#define AP_PAD_DISP_NONE    3 // undecidable right now -> leave the placeholder alone

// Resolve one scouted (item, player) pair to its presentation, and hand back the
// model-keeping category on the two model paths (AP_CAT_NONE otherwise). Since
// #219 that is the base-game rewards AND the CTR progression crystal, so "OG" no
// longer describes the set -- see ap_reward_policy.h for the ruling.
//
// The sync gate is unchanged from #124 and still matters: the glow pass runs
// every frame from connect, so resolving a FOREIGN item before scouts and slot
// info have landed would flicker a slot between presentations. Before the gate
// opens a foreign item resolves to AP_PAD_DISP_NONE and the slot keeps the
// placeholder model it was born with. An OWN item needs neither (the self slot
// is known at connect), so own rewards never flicker.
//
// AP_ItemCategory reads OUR item-id space, so it is only meaningful for a slot
// that is playing CTR. Another game's ids collide with ours numerically and must
// never be run through it -- that is the ap_net_player_is_ctr guard below, not a
// nicety.
static int AP_PadDisplayKind(long long item, int player, AP_ItemCat *outCat)
{
	AP_ItemCat cat = AP_CAT_NONE;
	int own = (player == ap_net_self_slot());

	if (!own)
	{
		if (!ap_net_scouts_ready() || !ap_net_slot_info_ready())
		{
			if (outCat)
				*outCat = AP_CAT_NONE;
			return AP_PAD_DISP_NONE;
		}
	}

	if (own || ap_net_player_is_ctr(player))
	{
		cat = AP_ItemCategory(item);

		// The one category decision, in one place (ap_reward_policy.h): base-game
		// rewards keep their vanilla model (matrix rules 1-2), CTR progression
		// keeps the crystal (rule 3), and every Wumpa package keeps the retail
		// fruit (the 2026-09-20 amendment). Traps, comfort items and anything an
		// apworld invents are marker material whoever owns them.
		if (!AP_RewardKeepsModel(cat))
			cat = AP_CAT_NONE;

		// ...and one drawability question, which is the half Lessons Learned §24
		// says a display resolver must be able to ask. The crystal is not a stock
		// per-level model: the hub's pack does not carry it, so it exists only
		// where ap_retail_crystal.c managed to harvest and park one. If that
		// failed, this reward is marker material exactly as it is today, rather
		// than a slot left showing whichever placeholder it was born with while the
		// tint is resolved for a crystal.
		//
		// The fruit is the same shape of problem: model 0x02 ships only in
		// loose-fruit level files, never in an MPK pack, so on the hub it is not
		// resident until ap_retail_wumpa.c harvests its own copy. Gate it on that
		// harvest OR a level that carries the model itself; otherwise a Wumpa
		// package resolves to the marker rather than the §24 stale-placeholder bug.
		//
		// Only these two are gated here. The base-game categories keep the
		// arrangement they already had: AH_WarpPad_ThTick reassigns the model only
		// when it is resident, and resolves the colour from the model the slot
		// ENDS UP with, so those two can never disagree.
		if (cat == AP_CAT_CRYSTAL && !AP_RetailCrystal_IsRegistered())
			cat = AP_CAT_NONE;

		// The fruit's own one-line version of the same question, kept in the
		// freestanding header so the host harness pins it.
		cat = AP_WumpaEffectiveCategory(cat, AP_RetailWumpa_IsDrawable(sdata ? sdata->gGT : 0));
	}

	if (outCat)
		*outCat = cat;

	if (cat != AP_CAT_NONE)
		return own ? AP_PAD_DISP_VANILLA : AP_PAD_DISP_GHOST;

	// Marker material. Without the marker model parked we have nothing to show it
	// with (the white gem is retired), so the slot keeps its placeholder rather
	// than inventing a stand-in that would lie about the reward.
	return AP_MarkerModel_IsRegistered() ? AP_PAD_DISP_MARKER : AP_PAD_DISP_NONE;
}

// Resolve a location's global bit straight to (kind, category, flags). Returns
// AP_PAD_DISP_NONE for anything not checkable or not yet scouted, which every
// caller reads as "leave the vanilla/placeholder presentation alone".
static int AP_PadDisplayForBit(int globalBit, AP_ItemCat *outCat, unsigned *outFlags)
{
	long code;
	long long item = 0;
	int player = -1;
	unsigned flags = 0;

	if (outCat)
		*outCat = AP_CAT_NONE;
	if (outFlags)
		*outFlags = 0;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return AP_PAD_DISP_NONE; // not a checkable location
	if (!ap_net_scout_known(code, &item, &player, &flags))
		return AP_PAD_DISP_NONE; // not scouted yet (not connected / pre-scout)

	if (outFlags)
		*outFlags = flags;
	return AP_PadDisplayKind(item, player, outCat);
}

// ---------------------------------------------------------------------------
// REWARD GLOW -- map a location (by its AdvProgress global bit) to the model of
// the AP item placed there, so each warp pad's glow shows its real reward.
// ---------------------------------------------------------------------------

int AP_WarpPadRewardModel(int globalBit)
{
	AP_ItemCat cat = AP_CAT_NONE;
	int kind = AP_PadDisplayForBit(globalBit, &cat, NULL);

	switch (kind)
	{
	case AP_PAD_DISP_MARKER:
		// Every non-OG item, mine or anyone's (#212 point 3).
		return STATIC_AP;

	case AP_PAD_DISP_VANILLA:
	case AP_PAD_DISP_GHOST:
		// A model-keeping reward: the same model either way. The GHOST kind adds
		// translucency at the instance (AP_WarpPadRewardGhost), never a different
		// model -- a peer's Sapphire Relic stays a Sapphire Relic, and a peer's
		// progression item stays a crystal (matrix rules 2 and 3).
		return AP_RewardModelForCat(cat);

	default:
		return -1; // not checkable / not scouted / marker unavailable -> placeholder
	}
}

// 1 when this location's scouted reward belongs to ANOTHER CTR PLAYER and keeps
// its own OG model, i.e. the slot must be ghost-rendered (#212 point 2). Own
// rewards, marker items and everything unresolved answer 0.
int AP_WarpPadRewardGhost(int globalBit)
{
	return AP_PadDisplayForBit(globalBit, NULL, NULL) == AP_PAD_DISP_GHOST ? 1 : 0;
}

// Per-category tint for the reward glow (see header): the relic tiers and, since
// #219, the crystal's purple. colorRGBA is packed as (R<<0x14)|(G<<0xc)|(B<<0x4);
// 0x020a5ff0 is the vanilla relic blue. Returns 0 to leave the caller's default
// colour untouched.
int AP_WarpPadRewardTint(int globalBit)
{
	AP_ItemCat cat = AP_CAT_NONE;
	unsigned flags = 0;
	int kind = AP_PadDisplayForBit(globalBit, &cat, &flags);

	// The Archipelago-logo marker carries the classification (or the one uniform
	// surprise colour) in its tint; the marker's own colours are neutral greys so
	// this lerps cleanly. Same answer for my own non-OG items as for anyone
	// else's -- #212 draws no local/foreign line here.
	if (kind == AP_PAD_DISP_MARKER)
		return AP_MarkerTint(flags);

	// A GHOSTED peer reward takes NO tint: the ghost writer needs colorRGBA 0 (see
	// the ghost block in AH_WarpPad_ThTick), and the caller forces it to 0 anyway.
	if (kind != AP_PAD_DISP_VANILLA)
		return 0;

	// Own relic tiers and the crystal's purple; 0 for everything whose natural
	// per-model colour the glow switch already applies (gem-cup colour, untinted
	// trophy, token group colour, golden key). Own filler and traps do not reach
	// here at all: they are marker items, handled above.
	return AP_RewardTintForCat(cat);
}

int AP_WarpPadRewardScale(int globalBit)
{
	AP_ItemCat cat = AP_CAT_NONE;
	int kind = AP_PadDisplayForBit(globalBit, &cat, NULL);

	if (kind != AP_PAD_DISP_VANILLA && kind != AP_PAD_DISP_GHOST)
		return 0x1000;
	return AP_RewardScaleForCat(cat);
}

int AP_CeremonyRewardProp(struct Instance *prop, int globalBit)
{
	struct GameTracker *gGT;
	int model;
	int ghost;
	int tint;
	int oldModelID;

	if (prop == 0 || !ctr_cfg_active())
		return 0;

	model = AP_WarpPadRewardModel(globalBit);
	if (model < 0)
		return 0;
	gGT = sdata->gGT;
	if (gGT == 0 || gGT->modelPtr[model] == 0)
		return 0;

	ghost = AP_WarpPadRewardGhost(globalBit);
	tint = AP_WarpPadRewardTint(globalBit);
	// Captured before the swap so the animation transition below can tell an
	// entry into the fruit (reset) from a swap away from it (clear).
	oldModelID = (prop->model != 0) ? prop->model->id : -1;
	prop->model = gGT->modelPtr[model];
	prop->flags &= ~(DRAW_TRANSPARENT | USE_SPECULAR_LIGHT | GHOST_DRAW_TRANSPARENT);
	prop->alphaScale = 0;

	switch (prop->model->id)
	{
	case STATIC_RELIC:
		prop->colorRGBA = tint ? tint : 0x020a5ff0;
		prop->flags |= USE_SPECULAR_LIGHT;
		break;
	case STATIC_CRYSTAL:
		prop->colorRGBA = tint ? tint : 0x0d22fff0;
		prop->flags |= USE_SPECULAR_LIGHT;
		break;
	case STATIC_TOKEN:
	{
		int tg = AP_WarpPadRewardTokenColour(globalBit);
		if (tg < 0)
			tg = data.metaDataLEV[gGT->levelID].ctrTokenGroupID;
		prop->colorRGBA = ((u32)data.AdvCups[tg].color[0] << 0x14) |
		                  ((u32)data.AdvCups[tg].color[1] << 0xc) |
		                  ((u32)data.AdvCups[tg].color[2] << 0x4);
		prop->flags |= (DRAW_TRANSPARENT | USE_SPECULAR_LIGHT);
		break;
	}
	case STATIC_GEM:
	{
		int gc = AP_WarpPadRewardGemColour(globalBit);
		if (gc >= 0)
			prop->colorRGBA = ((u32)data.AdvCups[gc].color[0] << 0x14) |
			                  ((u32)data.AdvCups[gc].color[1] << 0xc) |
			                  ((u32)data.AdvCups[gc].color[2] << 0x4);
		else
			prop->colorRGBA = tint;
		prop->flags |= USE_SPECULAR_LIGHT;
		break;
	}
	case STATIC_AP:
		prop->colorRGBA = tint;
		prop->alphaScale = AP_MARKER_TINT_STRENGTH;
		break;
	case STATIC_KEY:
		prop->colorRGBA = 0x0dca6000;
		prop->flags |= USE_SPECULAR_LIGHT;
		break;
	default:
		prop->colorRGBA = 0;
		break;
	}

	if (ghost)
	{
		prop->colorRGBA = 0;
		prop->alphaScale = AP_PAD_GHOST_ALPHA;
		prop->flags &= ~(DRAW_TRANSPARENT | USE_SPECULAR_LIGHT);
		prop->flags |= GHOST_DRAW_TRANSPARENT;
	}

	// #222: reset the fruit animation only on the transition INTO the fruit, and
	// clear the fruit animation bits on the way out so a later placeholder does
	// not inherit a half-driven spin. A per-tick reset would freeze the fruit on
	// frame zero.
	if (AP_WumpaShouldResetAnim(oldModelID, prop->model->id))
		AP_WumpaApplyFruitAnim(&prop->animIndex, &prop->animFrame, &prop->vertSplit, &prop->flags);
	else if (AP_WumpaShouldClearAnim(oldModelID, prop->model->id))
		AP_WumpaClearFruitAnim(&prop->flags);

	return 1;
}

// Colour index of the OWN CTR Token scouted here (see header). The token item id
// encodes its colour: ids AP_ITEM_BASE+4..8 = R,G,B,Y,P (AP_IDX_TOKEN_RED = 4),
// which lines up with data.AdvCups[0..4]. Foreign / unscouted / non-token -> -1.
int AP_WarpPadRewardTokenColour(int globalBit)
{
	long code;
	long long item = 0;
	int player = -1;
	unsigned flags = 0;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return -1;
	if (!ap_net_scout_known(code, &item, &player, &flags))
		return -1;
	if (player != ap_net_self_slot())
		return -1; // foreign multiworld item is not an own coloured token
	if (AP_ItemCategory(item) != AP_CAT_TOKEN)
		return -1;
	return (int)((item - AP_ITEM_BASE) - AP_IDX_TOKEN_RED); // 0..4 = R,G,B,Y,P
}

// Colour index of the OWN Gem scouted here (see header). The gem item id
// encodes its colour: ids AP_ITEM_BASE+9..13 = R,G,B,Y,P (AP_IDX_GEM_RED = 9),
// which lines up with data.AdvCups[0..4]. Foreign / unscouted / non-gem -> -1.
int AP_WarpPadRewardGemColour(int globalBit)
{
	long code;
	long long item = 0;
	int player = -1;
	unsigned flags = 0;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return -1;
	if (!ap_net_scout_known(code, &item, &player, &flags))
		return -1;
	if (player != ap_net_self_slot())
		return -1; // foreign multiworld item is not an own coloured gem
	if (AP_ItemCategory(item) != AP_CAT_GEM)
		return -1;
	return (int)((item - AP_ITEM_BASE) - AP_IDX_GEM_RED); // 0..4 = R,G,B,Y,P
}

// Enumerate the still-UNCOLLECTED (unchecked) AP reward locations of race track
// `destLevelID` (0..15), writing their global AdvProgress bits into `outBits`
// (caller array, capacity `cap`) in fixed tier order -- Trophy +0x06, Sapphire
// +0x16, Gold +0x28, Platinum +0x3a, CTR Token +0x4c -- and returning the count
// written (0..5). A location counts as collected purely by AP CHECKED-STATE
// (AP_LocationCheckedByBit / ap_net_location_checked), NEVER by CHECK_ADV_BIT or
// modelIndex: AP_ApplyItems clears local-win bits on every reconcile tick, so only
// the server's checked set is authoritative. Returns 0 for a non-race destination or
// when every tier is checked (caller then shows nothing). This is the sole input
// to the warp-pad glow's "which rewards to advertise + cycle" decision.
int AP_WarpPadUncollectedBits(int destLevelID, int *outBits, int cap)
{
	static const int kTierBit[5] = {
	    ADV_REWARD_FIRST_TROPHY,
	    ADV_REWARD_FIRST_SAPPHIRE_RELIC,
	    ADV_REWARD_FIRST_GOLD_RELIC,
	    ADV_REWARD_FIRST_PLATINUM_RELIC,
	    ADV_REWARD_FIRST_CTR_TOKEN};
	int count = 0;
	int i;

	if (outBits == 0 || cap <= 0)
		return 0;
	if (destLevelID < 0 || destLevelID >= 16)
		return 0; // only race tracks 0..15 carry this 5-location pool

	for (i = 0; i < 5 && count < cap; i++)
	{
		int bit = destLevelID + kTierBit[i];
		// Not a real location THIS SEED (e.g. a tier the apworld didn't place,
		// or -- issue #171/#28 R1 -- a below-count relic Time Trial that was
		// never created at all) is "not advertisable" -> skip, so the glow
		// never shows an AP check for a location that doesn't exist.
		if (!AP_LocationExistsByBit(bit))
			continue;
		if (!AP_LocationCheckedByBit(bit))
			outBits[count++] = bit;
	}

	return count;
}

// ---------------------------------------------------------------------------
// UNIFIED PAD STATE (Warp-Pad State Model v2).
// One state function shared by the map colour (AH_Map.c), the in-hub look
// (AH_WarpPad_LInB), and the gate (AH_WarpPad_ThTick). A pad is in exactly one
// state; every surface reads it from the same source, no mode switch.
//
// LIFECYCLE keys off the DESTINATION content category (race dest = full 6-state;
// trial/arena/cup dest = reduced 1->2->5). REQUIREMENTS stay keyed to the
// PHYSICAL pad (stage1/stage2 come from warp_pad_unlock[phys]). GREEN means "a
// check you can do" -- ANY item (foreign included), NOT the old own-progression
// criterion (AP_BitIsUsefulUnchecked), which this model drops.
// ---------------------------------------------------------------------------

// Enumerate the still-UNCOLLECTED (unchecked on the server) AP reward locations
// of destination `destLevelID`, for ANY category, writing their global
// AdvProgress bits into `outBits` (capacity `cap`) and returning the count. This
// is the category-general sibling of AP_WarpPadUncollectedBits (race-only), added
// so the state model + the trial-pad glow can see trial/arena/cup locations too.
// "Collected" is decided ONLY by AP checked-state (never CHECK_ADV_BIT). Absent
// tiers (no location_code in this seed) are skipped. Categories:
//   race  0..15 : 5 tiers (trophy +0x06, sapphire +0x16, gold +0x28,
//                 platinum +0x3a, CTR token +0x4c)
//   trial 16,17 : 3 relic tiers (sapphire/gold/platinum). Their Trophy and
//                 CTR Challenge have no bit; AP_PadUncollectedGlowBits adds
//                 them as pseudo-bits (#343)
//   arena 18,19,21,23 : 1 crystal (battleTrackArr[dest-18] + FIRST_PURPLE_TOKEN)
//   cup   100..104     : 1 gem ((dest-100) + FIRST_GEM)
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

static int AP_CortexDestValid(void)
{
	return ctr_cfg_active() && ctr_cfg.cortex_track.valid;
}

int AP_CupLegsCortexTrack(int cup)
{
	int leg;
	if (!ctr_cfg_active() || cup < 0 || cup >= 5 || ctr_cfg_cup_displaced(cup))
		return 0;
	for (leg = 0; leg < 4; leg++)
		if (ctr_cfg_cup_leg(cup, leg) == AP_CORTEX_DEST)
			return 1;
	return 0;
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

int AP_RelicTimeFor(int levelID, int tier)
{
	if (tier < 0 || tier > 2)
		tier = 0;
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

int AP_PadUncollectedBits(int destLevelID, int *outBits, int cap)
{
	static const int kRaceTierBit[5] = {
	    ADV_REWARD_FIRST_TROPHY,
	    ADV_REWARD_FIRST_SAPPHIRE_RELIC,
	    ADV_REWARD_FIRST_GOLD_RELIC,
	    ADV_REWARD_FIRST_PLATINUM_RELIC,
	    ADV_REWARD_FIRST_CTR_TOKEN};
	// Trial pads (Slide Coliseum 16 / Turbo Track 17) carry only the three relic
	// Time-Trial locations (dest + SAPPHIRE/GOLD/PLATINUM: bits 38/56/74 &
	// 39/57/75, verified against AP_LOCATION_TABLE) -- no trophy, no token.
	static const int kTrialTierBit[3] = {
	    ADV_REWARD_FIRST_SAPPHIRE_RELIC,
	    ADV_REWARD_FIRST_GOLD_RELIC,
	    ADV_REWARD_FIRST_PLATINUM_RELIC};
	int count = 0;
	int i;

	if (outBits == 0 || cap <= 0)
		return 0;

	if (destLevelID >= 0 && destLevelID < 16)
	{
		for (i = 0; i < 5 && count < cap; i++)
		{
			int bit = destLevelID + kRaceTierBit[i];
			// Issue #171/#28 R1: skip a below-count relic Time Trial this seed
			// never created, exactly like AP_WarpPadUncollectedBits above.
			if (!AP_LocationExistsByBit(bit))
				continue;
			if (!AP_LocationCheckedByBit(bit))
				outBits[count++] = bit;
		}
	}
	else if (destLevelID == 16 || destLevelID == 17)
	{
		for (i = 0; i < 3 && count < cap; i++)
		{
			int bit = destLevelID + kTrialTierBit[i];
			if (!AP_LocationExistsByBit(bit))
				continue;
			if (!AP_LocationCheckedByBit(bit))
				outBits[count++] = bit;
		}
	}
	else if (destLevelID == 18 || destLevelID == 19 ||
	         destLevelID == 21 || destLevelID == 23)
	{
		// Battle arena crystal (Crystal Bonus Round, bits 111..114). Same mapping
		// as the ThTick glow pass: battleTrackArr[dest-18] + FIRST_PURPLE_TOKEN.
		// Never a removed slot (crystal bonuses are unconditional locations --
		// see the package-3 build note), but checked the same way for consistency.
		int bit = R232.battleTrackArr[destLevelID - 18] + ADV_REWARD_FIRST_PURPLE_TOKEN;
		if (AP_LocationExistsByBit(bit) && !AP_LocationCheckedByBit(bit) && count < cap)
			outBits[count++] = bit;
	}
	else if (destLevelID == AP_CORTEX_DEST)
	{
		// Schema 15: Trophy, relic tiers and CTR token as direct codes. Never
		// LevelID-13 arithmetic, which would name Oxide Station's locations.
		if (AP_CortexDestValid())
			count = AP_CortexPadAppendOpen(&ctr_cfg.cortex_track, AP_CV_APPEND_TIERS,
			                               outBits, cap, count,
			                               AP_PadBoxLive, AP_PadBoxChecked, 0);
	}
	else if (destLevelID >= 100 && destLevelID < 105)
	{
#ifdef CTR_CUSTOM_TRACKS
		if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
			return AP_CustomPadAppendUnchecked(
				&ctr_cfg, destLevelID, 0, outBits, cap, count,
				AP_PadBoxLive, AP_PadBoxChecked, 0);
#endif
		// Gem cup (Gem, bits 106..110), keyed by cup colour 0..4. Never a
		// removed slot -- shuffle_gems off PINS this location (place_locked_item),
		// it stays a real self-consistent AP check (see the package-3 build note).
		int bit = (destLevelID - 100) + ADV_REWARD_FIRST_GEM;
		if (AP_LocationExistsByBit(bit) && !AP_LocationCheckedByBit(bit) && count < cap)
			outBits[count++] = bit;
	}

	return count;
}

// Append the still-unchecked podium rung pseudo-bits of ONE race track (0..15) to
// outBits, advancing *count. Shared by the race-pad path and the cup-pad
// aggregation below. AP_LookupLocationCode returns -1 for a rung this seed does
// not carry (held_5th default-off, a rung the seed did not place), which skips it
// here exactly like an absent tier.
static void AP_AppendTrackRungGlow(int track, int *outBits, int cap, int *count)
{
	int rung;
#ifdef CTR_CUSTOM_TRACKS
	if (track < 0 || track >= AP_PODIUM_LOGICAL_TRACK_COUNT)
#else
	if (track < 0 || (track >= CTR_CFG_PODIUM_TRACK_COUNT &&
	                 (track < AP_TRIAL_PODIUM_LOGICAL_BASE || track >= AP_TRIAL_PODIUM_LOGICAL_BASE+2)))
#endif
		return;
	for (rung = 0; rung < CTR_CFG_PODIUM_RUNG_COUNT && *count < cap; rung++)
	{
		int bit = AP_PODIUM_PSEUDO_BASE + track * CTR_CFG_PODIUM_RUNG_COUNT + rung;
		int j;
		if (AP_LookupLocationCode(bit) < 0)
			continue;
		if (AP_LocationCheckedByBit(bit))
			continue;
		// Dedup within this pad's buffer: a cup pad aggregates four legs, and a
		// track can (in principle) appear more than once, which would else list the
		// same rung twice in the glow cycle. Cheap (buffer <= 24). Two DIFFERENT cup
		// pads advertising overlapping rungs is fine -- they are separate buffers.
		for (j = 0; j < *count; j++)
			if (outBits[j] == bit)
				break;
		if (j < *count)
			continue;
		outBits[(*count)++] = bit;
	}
}

// Glow-only enumerator (see ap_hooks.h): the tier/category bits above PLUS the
// still-unchecked podium rung pseudo-bits. For a RACE destination (0..15) it
// appends that track's own rungs. For a CUP destination (100..104) it appends the
// rungs of ALL FOUR of the cup's leg tracks (data.advCupTrackIDs), because a cup
// pad's position checks live on its legs -- without this a cup pad under-advertises
// (decision 3, 2026-07-16 wayfinder). Kept separate from
// AP_PadUncollectedBits so the tier-2 menu stays tier-only. AP_PadState consumes
// the glow-complete enumeration because a Done lock may not strand a rung.
int AP_PadUncollectedGlowBits(int destLevelID, int *outBits, int cap)
{
	int count;

#ifdef CTR_CUSTOM_TRACKS
	if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
		return AP_CustomPadAppendUnchecked(
			&ctr_cfg, destLevelID, ctr_cfg.podium_enabled,
			outBits, cap, 0, AP_PadBoxLive, AP_PadBoxChecked, 0);
#endif

	count = AP_PadUncollectedBits(destLevelID, outBits, cap);

	// #439 follow-up: an open Relic Race Perfect is advertised like the other
	// relic-side checks, right after the tier bits. Retail race pads and the two
	// trial pads only; Cortex Vortex and custom pads return around it.
	if (AP_PadUncollectedRelicPerfectCount(destLevelID) > 0 && count < cap)
		outBits[count++] = AP_RelicPerfectPseudoBit(destLevelID);

	// Schema 15: the Cortex Vortex pad also advertises its own podium rungs.
	if (destLevelID == AP_CORTEX_DEST)
		return AP_CortexDestValid()
		           ? AP_CortexPadAppendOpen(&ctr_cfg.cortex_track, AP_CV_APPEND_RUNGS,
		                                    outBits, cap, count,
		                                    AP_PadBoxLive, AP_PadBoxChecked, 0)
		           : count;

	// #343: a trial pad also offers its Trophy and CTR Challenge. They have no
	// AdvProgress bit, so they ride as pseudo-bits. Glow only: the tier-2
	// picker reads AP_PadUncollectedBits as its "relics left" test.
	// An unservable CTR Challenge (Trophy-only mode, or refused letter assets)
	// is left out, so the pad cannot stay open for a race it never offers.
	if (AP_TrialTrackConfigured(destLevelID))
	{
		long trialRow[CTR_CFG_TRIAL_CHECK_COUNT];
		trialRow[CTR_CFG_TRIAL_TROPHY] =
		    ctr_cfg.trial_track_locations[destLevelID - 16][CTR_CFG_TRIAL_TROPHY];
		trialRow[CTR_CFG_TRIAL_CTR] = AP_TrialChallengeServable(destLevelID)
		    ? ctr_cfg.trial_track_locations[destLevelID - 16][CTR_CFG_TRIAL_CTR]
		    : -1;
		count = AP_TrialPadAppendUnchecked(trialRow, destLevelID - 16,
			outBits, cap, count, AP_PadBoxChecked, 0);
	}

	if (!ctr_cfg.podium_enabled)
		return count;

	if (destLevelID >= 0 && destLevelID < 18)
	{
		AP_AppendTrackRungGlow(destLevelID < 16 ? destLevelID :
		                      AP_TRIAL_PODIUM_LOGICAL_BASE + destLevelID - 16,
		                      outBits, cap, &count);
	}
	else if (destLevelID >= 100 && destLevelID < 105)
	{
		// Aggregate the four leg tracks' uncollected rungs. ctr_cfg_cup_leg is
		// cup-major (cup, leg) and identity-safe (#166: returns the vanilla
		// data.advCupTrackIDs value when inactive or this seed left the leg at
		// its default); a leg whose track is a trial (16/17) or otherwise outside
		// 0..15 carries no podium rungs and is skipped by AP_AppendTrackRungGlow.
		int cup = destLevelID - 100;
		int leg;
		// A displaced cup runs one custom race and legs no retail track. Its
		// complete four-leg row remains on the wire as the seed's retail table,
		// but advertising those rungs on this pad would promise checks the race
		// cannot honestly emit.
		if (!AP_GlowSlots_CupLegRungsEligible(ctr_cfg_cup_displaced(cup)))
			return count;
		for (leg = 0; leg < 4 && count < cap; leg++)
		{
			int legTrack = ctr_cfg_cup_leg(cup, leg);
			// A Cortex Vortex leg exposes its own rungs, never Oxide Station's.
			if (legTrack == AP_CORTEX_DEST)
			{
				if (AP_CortexDestValid())
					count = AP_CortexPadAppendOpen(&ctr_cfg.cortex_track,
					                               AP_CV_APPEND_RUNGS, outBits, cap, count,
					                               AP_PadBoxLive, AP_PadBoxChecked, 0);
				continue;
			}
			AP_AppendTrackRungGlow(legTrack, outBits, cap, &count);
		}
	}
	return count;
}

// ── §6: boxes behind a pad ──────────────────────────────────────────────────
//
// RULED 2026-08-11 (rulings note §6). AP_PadUncollectedGlowBits enumerates tier
// bits and podium rungs, and neither of those is a box: itemsanity box locations
// (#109) carry no AdvProgress bit at all. So a trophy win with boxes still
// standing Re-locked the pad and stranded them until stage 2, and a track whose
// tier bits were all checked went Done, which HARD-LOCKS the pad and made those
// boxes unreachable for the rest of the seed.
//
// This is counted, not enumerated into outBits, and deliberately so: the glow
// cycle shows REWARD placeholders (trophy / relic / token models) and a box has
// no placeholder to show. The count feeds AP_PadState's lifecycle decisions only;
// the glow itself is byte-identical.
//
// THE PREDICATE IS THE SPAWN PREDICATE. A box counts as uncollected only if it
// could actually be broken: it needs GEOMETRY (a placement at that slot on that
// level) and a LIVE location (in this slot's seed, per §7) that is not yet
// checked. Counting bare locations instead would let a seed that created more box
// locations than the placement set covers keep a pad green forever.
static int AP_PadTrackBoxSlots(int levelID)
{
	// Per-level placement counts, rebuilt only when the placement table changes
	// (a load, or an author-mode drop/undo). AP_PadState runs per pad per frame,
	// so re-walking 241..512 placements for every pad every frame would be real
	// work for a number that changes about twice a session.
	static int s_slots[AP_BOX_TRACK_COUNT];
	static int s_gen = -1;

	int gen;

	if (levelID < 0 || levelID >= AP_BOX_TRACK_COUNT)
		return 0;

	AP_Author_EnsureLoaded();
	gen = AP_Author_PlacementGeneration();
	if (gen != s_gen)
	{
		int i, n = AP_Author_PlacementCount();

		for (i = 0; i < AP_BOX_TRACK_COUNT; i++)
			s_slots[i] = 0;

		for (i = 0; i < n; i++)
		{
			int lvl;
			if (!AP_Author_PlacementGet(i, &lvl, 0, 0, 0, 0))
				continue;
			if (lvl < 0 || lvl >= AP_BOX_TRACK_COUNT)
				continue;
			if (s_slots[lvl] >= AP_BOX_SLOTS_PER_TRACK)
				continue; // past the frozen ceiling: no name, never spawns
			s_slots[lvl]++;
		}
		s_gen = gen;
	}

	return s_slots[levelID];
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

static int AP_PadTrackBoxesLeft(int levelID)
{
	// The count itself is AP_BoxMap_CountStanding (freestanding, and exercised by
	// tools/test-box-map.c); this only supplies the two server-truth predicates,
	// which are the SAME pair the spawn set is built from -- §7 liveness and the
	// checked set. One rule, one implementation, so the pad and the track can
	// never disagree about what is still standing.
	return AP_BoxMap_CountStanding(levelID, AP_PadTrackBoxSlots(levelID),
	                               AP_PadBoxLive, AP_PadBoxChecked, 0);
}

// How many breakable boxes still stand behind this pad's destination. A cup pad
// aggregates its four legs, exactly as AP_PadUncollectedGlowBits does for rungs
// and for the same reason: a cup leg is not independently raceable from the hub,
// so a Done cup pad would strand its legs' boxes permanently.
int AP_PadUncollectedBoxCount(int destLevelID)
{
	int leg, n = 0;

	if (!ctr_cfg_active())
		return 0;

	if (destLevelID >= 0 && destLevelID < AP_BOX_TRACK_COUNT)
		return AP_PadTrackBoxesLeft(destLevelID);

	if (destLevelID >= 100 && destLevelID < 105)
	{
		int cup = destLevelID - 100;
#ifdef CTR_CUSTOM_TRACKS
		// A displaced cup runs no retail legs.  Until custom box identities and
		// placements exist, counting the wire row's retail boxes here would keep
		// the custom pad open for checks it cannot serve.
		if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
			return 0;
#endif
		for (leg = 0; leg < 4; leg++)
			n += AP_PadTrackBoxesLeft(ctr_cfg_cup_leg(cup, leg));
	}
	return n;
}

// Per-track Wumpa checks carry no AdvProgress bit, so they need the same
// count-only lifecycle bridge as AP boxes and Lettersanity. A standalone race
// owns its one destination code. A Gem Cup aggregates the distinct track-owned
// codes of all four legs so checking the Gem cannot hard-lock a remaining
// Wumpa route. Repeated legs are alternatives to one location and count once.
static int AP_PadTrackWumpaLeft(int levelID)
{
	long code;

	if (levelID < 0 || levelID >= CTR_CFG_WUMPA_TRACK_COUNT)
		return 0;
	code = ctr_cfg.wumpa.tracks[levelID];
	return code >= 0 && ap_net_location_exists(code) &&
	       !ap_net_location_checked(code);
}

int AP_PadUncollectedWumpaCount(int destLevelID)
{
	int tracks[4];
	int trackLeft[4];
	int leg;

	if (!ctr_cfg_active() || ctr_cfg.wumpa.mode != CTR_CFG_WUMPA_PER_TRACK)
		return 0;
	if (destLevelID >= 0 && destLevelID < CTR_CFG_WUMPA_TRACK_COUNT)
		return AP_PadTrackWumpaLeft(destLevelID);
	if (destLevelID == AP_CORTEX_DEST)
		return AP_CortexOpenCount(&ctr_cfg.cortex_track, AP_CV_SLOT_WUMPA, 1,
		                          AP_PadBoxLive, AP_PadBoxChecked, 0);
	if (destLevelID < 100 || destLevelID >= 105)
		return 0;

#ifdef CTR_CUSTOM_TRACKS
	// The direct Alpha6 custom destination already includes its Wumpa location
	// in AP_CustomPadAppendUnchecked. Its serialized retail leg row is ignored.
	if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
		return 0;
#endif

	for (leg = 0; leg < 4; leg++)
	{
		tracks[leg] = ctr_cfg_cup_leg(destLevelID - 100, leg);
		// A Cortex Vortex leg counts its own 35016121, keyed one past the
		// retail rows so it never dedups against Oxide Station's row 13.
		if (tracks[leg] == AP_CORTEX_DEST)
		{
			tracks[leg] = CTR_CFG_WUMPA_TRACK_COUNT;
			trackLeft[leg] = AP_CortexOpenCount(&ctr_cfg.cortex_track, AP_CV_SLOT_WUMPA, 1,
			                                    AP_PadBoxLive, AP_PadBoxChecked, 0);
			continue;
		}
		trackLeft[leg] = AP_PadTrackWumpaLeft(tracks[leg]);
	}
	return AP_PadCupWumpaCount(tracks, trackLeft, CTR_CFG_WUMPA_TRACK_COUNT + 1);
}

int AP_PadUncollectedLetterCount(int destLevelID)
{
	int letter, n = 0;

	if (destLevelID == AP_CORTEX_DEST)
		return ctr_cfg_active()
		           ? AP_CortexOpenCount(&ctr_cfg.cortex_track, AP_CV_SLOT_LETTER0, 3,
		                                AP_PadBoxLive, AP_PadBoxChecked, 0)
		           : 0;
#ifdef CTR_CUSTOM_TRACKS
	// Custom CTR letters (2026-10-09): the custom pad must not go Done while one
	// is open, the same rule every other CTR Challenge pad follows.
	if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
		return ctr_cfg_active()
		           ? AP_CustomPadLettersLeft(&ctr_cfg, destLevelID,
		                                     AP_PadBoxLive, AP_PadBoxChecked, 0)
		           : 0;
#endif
	if (!ctr_cfg_active() || destLevelID < 0 || destLevelID >= CTR_CFG_LETTER_TRACK_COUNT)
		return 0;
	// Trial letters are only reachable in a servable trial CTR Challenge.
	if ((destLevelID == 16 || destLevelID == 17) && !AP_TrialChallengeServable(destLevelID))
		return 0;

	for (letter = 0; letter < CTR_CFG_LETTER_COUNT; letter++)
	{
		long code = ctr_cfg.lettersanity_locations[destLevelID][letter];
		if (code >= 0 && ap_net_location_exists(code) && !ap_net_location_checked(code))
			n++;
	}
	return n;
}

// Relic Race Perfect (#49) has no AdvProgress bit, so like Lettersanity it is a
// count-only pad lifecycle input. 1 while this destination's perfect check is a
// live location that the server has not checked; the pad must not go Done or
// hide the relic race while it is (#439). Retail race pads and the two trial
// pads only: the Cortex Vortex perfect identity is not minted.
int AP_PadUncollectedRelicPerfectCount(int destLevelID)
{
	long code;

	if (!ctr_cfg_active() || !ctr_cfg.relic_perfect_enabled)
		return 0;
	if (destLevelID < 0 || destLevelID >= CTR_CFG_RELIC_PERFECT_COUNT)
		return 0;
	code = ctr_cfg.relic_perfect[destLevelID];
	return AP_RelicPerfectPadLeftPure(
	    1, code, code > 0 && ap_net_location_exists(code),
	    code > 0 && ap_net_location_checked(code));
}

// Display-only pad enumeration (see ap_hooks.h): AP_PadUncollectedGlowBits plus
// the open item boxes, CTR letters and per-track Wumpa. Each family uses the
// predicate of its pad-state count (AP_PadUncollectedBoxCount, ...LetterCount,
// ...WumpaCount), so the glow lists exactly what the state counts, and a cup pad
// aggregates its four legs the same way (boxes and Wumpa; letters are per track
// and a cup has none). A leg track repeated inside one cup is listed once. A
// custom cup carries its Trophy and Wumpa in the base enumeration already and no
// boxes or letters, so it returns unchanged. Cortex Vortex lists its letters and
// Wumpa; as a cup leg only its Wumpa, like the count.
int AP_PadUncollectedDisplayBits(int destLevelID, int *outBits, int cap)
{
	int count = AP_PadUncollectedGlowBits(destLevelID, outBits, cap);
	int wumpaOn;

	if (!ctr_cfg_active())
		return count;
#ifdef CTR_CUSTOM_TRACKS
	if (AP_CustomPadOwnsDestination(&ctr_cfg, destLevelID))
	{
		// The custom CTR letters, with the predicate of their pad-state count.
		int letter;
		if (AP_CustomPadLettersLeft(&ctr_cfg, destLevelID, AP_PadBoxLive,
		                            AP_PadBoxChecked, 0) > 0)
			for (letter = 0; letter < CTR_CFG_LETTER_COUNT && count < cap; letter++)
			{
				long code = ctr_cfg.custom_letter_locations[letter];
				if (code > 0 && ap_net_location_exists(code) &&
				    !ap_net_location_checked(code))
					outBits[count++] = AP_PadGlowCustomLetterBit(letter);
			}
		return count;
	}
#endif
	wumpaOn = ctr_cfg.wumpa.mode == CTR_CFG_WUMPA_PER_TRACK;

	if (destLevelID >= 0 && destLevelID < AP_BOX_TRACK_COUNT)
	{
		count = AP_PadGlowAppendBoxes(destLevelID, AP_PadTrackBoxSlots(destLevelID),
		                              outBits, cap, count,
		                              AP_PadBoxLive, AP_PadBoxChecked, 0);
		// Same gate as AP_PadUncollectedLetterCount: trial letters only show
		// while the trial CTR Challenge can be served.
		if ((destLevelID != 16 && destLevelID != 17) ||
		    AP_TrialChallengeServable(destLevelID))
			count = AP_PadGlowAppendLetters(ctr_cfg.lettersanity_locations[destLevelID],
			                                destLevelID, outBits, cap, count,
			                                AP_PadBoxLive, AP_PadBoxChecked, 0);
		if (wumpaOn && destLevelID < CTR_CFG_WUMPA_TRACK_COUNT)
			count = AP_PadGlowAppendWumpa(ctr_cfg.wumpa.tracks[destLevelID], destLevelID,
			                              outBits, cap, count,
			                              AP_PadBoxLive, AP_PadBoxChecked, 0);
	}
	else if (destLevelID == AP_CORTEX_DEST)
	{
		if (AP_CortexDestValid())
			count = AP_CortexPadAppendOpen(&ctr_cfg.cortex_track,
			                               AP_CV_APPEND_LETTERS |
			                                   (wumpaOn ? AP_CV_APPEND_WUMPA : 0),
			                               outBits, cap, count,
			                               AP_PadBoxLive, AP_PadBoxChecked, 0);
	}
	else if (destLevelID >= 100 && destLevelID < 105)
	{
		int cup = destLevelID - 100;
		int leg, prev;
		int seen[4];

		for (leg = 0; leg < 4; leg++)
		{
			int track = ctr_cfg_cup_leg(cup, leg);
			int dup = 0;

			seen[leg] = track;
			for (prev = 0; prev < leg; prev++)
				if (seen[prev] == track)
					dup = 1;
			if (dup)
				continue; // repeated leg: one location, listed once
			if (track == AP_CORTEX_DEST)
			{
				if (wumpaOn && AP_CortexDestValid())
					count = AP_CortexPadAppendOpen(&ctr_cfg.cortex_track,
					                               AP_CV_APPEND_WUMPA, outBits, cap, count,
					                               AP_PadBoxLive, AP_PadBoxChecked, 0);
				continue;
			}
			count = AP_PadGlowAppendBoxes(track, AP_PadTrackBoxSlots(track),
			                              outBits, cap, count,
			                              AP_PadBoxLive, AP_PadBoxChecked, 0);
			if (wumpaOn && track >= 0 && track < CTR_CFG_WUMPA_TRACK_COUNT)
				count = AP_PadGlowAppendWumpa(ctr_cfg.wumpa.tracks[track], track,
				                              outBits, cap, count,
				                              AP_PadBoxLive, AP_PadBoxChecked, 0);
		}
	}
	return count;
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

// Retail races and admitted trial Trophy destinations share the full lifecycle.
// Relic-only/legacy trials remain in the reduced non-race category.
static int AP_DestIsRace(int destLevelID)
{
	if (destLevelID == AP_CORTEX_DEST)
		return AP_CortexDestValid() && AP_CortexTrackCode(AP_CV_SLOT_TROPHY) > 0;
	return (destLevelID >= 0 && destLevelID < 16) ||
	       (AP_TrialTrackConfigured(destLevelID) &&
	        AP_TrialTrackLocation(destLevelID, CTR_CFG_TRIAL_TROPHY) > 0);
}

static int AP_DestTrophyChecked(int destLevelID)
{
	if (destLevelID == AP_CORTEX_DEST)
		return AP_CortexTrackChecked(AP_CV_SLOT_TROPHY);
	if (AP_TrialTrackConfigured(destLevelID))
		return AP_TrialTrackLocationChecked(destLevelID, CTR_CFG_TRIAL_TROPHY);
	return AP_LocationCheckedByBit(destLevelID + ADV_REWARD_FIRST_TROPHY);
}

// Is destination `destLevelID` a recognised warp-pad destination at all (race /
// trial / arena / cup)? Anything else -> AP_PadState returns 0 (vanilla/untouched).
static int AP_DestKnown(int destLevelID)
{
	return AP_DestIsRace(destLevelID) ||
	       destLevelID == 16 || destLevelID == 17 ||
	       destLevelID == 18 || destLevelID == 19 ||
	       destLevelID == 21 || destLevelID == 23 ||
	       (destLevelID >= 100 && destLevelID < 105);
}

// Is the PHYSICAL pad's stage-1 (entry) requirement satisfied? Faithful copy of
// the per-category unlock predicate AH_WarpPad_LInB computes for each pad class
// so the state model agrees with the pad
// the game actually births. Race pads reuse ctr_cfg_warp_unlocked verbatim (the
// same helper ThTick's load gate uses). NON-race categories mirror LInB's
// per-class branch: randomized requirement when slot_data provides one, else the
// vanilla fallback for that class. Keyed by the PHYSICAL pad LevelID.
//
// Non-static since the Gem Cup AP-box policy (WO-A3): ap_boxes.c needs the SAME
// per-class stage-1 answer for the pad that hosts a cup leg's individual race,
// and a second implementation of this routing is exactly the divergence the
// policy exists to remove. Declared in ap_hooks.h.
int AP_PadStage1Met(int physLevelID)
{
	int i, owned;

	// Race tracks (0..15): the canonical helper (shared with ThTick + LInB).
	if (physLevelID >= 0 && physLevelID < 16)
		return ctr_cfg_warp_unlocked(physLevelID);

	// Trial pads: Slide Coliseum (16) = vanilla 10 Sapphire relics; Turbo Track
	// (17) = vanilla all 5 Gem colours. An emitted single-stage req overrides:
	// randomized modes, and for Slide Coliseum also a vanilla-mode seed that
	// lowered the pad to the Sapphires it created (ctr_cfg_slide_coliseum_req).
	if (physLevelID == CTR_CFG_SLIDE_COLISEUM_PAD)
	{
		const ctr_req *slide = ctr_cfg_slide_coliseum_req();
		if (slide)
			return AP_BossReqMet(slide);
		return AP_GateCount(AP_IDX_SAPPHIRE) >= CTR_CFG_SLIDE_COLISEUM_RETAIL_SAPPHIRES;
	}
	if (physLevelID == 17)
	{
		if (ctr_cfg_active() && physLevelID < CTR_CFG_PAD_COUNT &&
		    ctr_cfg.warp_pad_unlock[physLevelID].stage1.type != 0)
			return AP_BossReqMet(&ctr_cfg.warp_pad_unlock[physLevelID].stage1);
		owned = 0;
		for (i = 0; i < 5; i++)
			if (AP_GateCountGemColour(i) >= 1)
				owned++;
		return owned >= 5;
	}

	// Battle arenas (18/19/21/23): randomized req overrides; else the vanilla
	// hub-key gate (LInB GetKeysRequirement: received Keys >= arrKeysNeeded[hub]).
	// Open-when-free: a randomized seed that includes the arenas ALWAYS sends a
	// non-type-0 stage-1 for them -- a FREE arena arrives as the explicit
	// always-true "0 trophies" ({type 1, count 0}), so it opens here with no
	// vanilla-key AND (only its hub door still gates it physically). type 0 is
	// reserved for vanilla unlock mode / arenas-not-included, where the vanilla
	// gate below is the correct behaviour.
	if (physLevelID == 18 || physLevelID == 19 ||
	    physLevelID == 21 || physLevelID == 23)
	{
		if (ctr_cfg_active() && physLevelID < CTR_CFG_PAD_COUNT &&
		    ctr_cfg.warp_pad_unlock[physLevelID].stage1.type != 0)
			return AP_BossReqMet(&ctr_cfg.warp_pad_unlock[physLevelID].stage1);
		return AP_GateCount(AP_IDX_KEY) >=
		       D232.arrKeysNeeded[data.metaDataLEV[physLevelID].hubID];
	}

	// Gem cups (100..104): randomized req overrides; else vanilla 4 Tokens of the
	// cup's colour. gem_cup_unlock is keyed by cup colour 0..4 (identity pad).
	if (physLevelID >= 100 && physLevelID < 105)
	{
		int cupIdx = physLevelID - 100;
		if (ctr_cfg_active() && ctr_cfg.gem_cup_unlock[cupIdx].stage1.type != 0)
			return AP_BossReqMet(&ctr_cfg.gem_cup_unlock[cupIdx].stage1);
		return AP_GateCountTokenColour(cupIdx) >= 4;
	}

	return 1; // unknown pad -> treat as enterable (defensive; AP_PadState gates)
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

// Is there a Hit opportunity behind this pad? Under the pool draw every eligible
// target rotates into every supported destination's field, so any eligible
// racer other than the effective player with an unchecked Hit location is an
// opportunity at the sixteen retail tracks (0..15) and the two trial Trophy
// tracks (16/17). A trial whose trial row is invalid has no Trophy route, so it never
// reports an opportunity (the pad must not stay open with nothing to earn);
// logged once per trial. Cups are ticket 11. Counted by AP_PadState as BOTH a
// remaining location and a plain-rerace check, so Done cannot hard-lock the pad
// while a target can still be encountered, and a stage-2-locked pad keeps its
// phase-1 plain rerace. Also consumed by AH_WarpPad.c's tier-2 choosers.
int AP_HitPadOpportunity(int physLevelID, int destLevelID)
{
	int trialValid = 1;

	if (!AP_HitEncounterEnabled())
		return 0;
	if (destLevelID == 16 || destLevelID == 17)
	{
		int trial = destLevelID - 16;
		trialValid = ctr_cfg.trial_track_valid[trial];
		if (!AP_HitPadDestEligiblePure(destLevelID, trialValid))
		{
			static unsigned s_trialInvalidLogged;
			if ((s_trialInvalidLogged & (1u << trial)) == 0)
			{
				char msg[128];
				s_trialInvalidLogged |= (1u << trial);
				snprintf(msg, sizeof msg,
				         "[AP HIT] trial destination %d has no valid trial row; "
				         "no Hit opportunity\n",
				         destLevelID);
				AP_LogLine(msg);
			}
			return 0;
		}
	}
	else if (!AP_HitPadDestEligiblePure(destLevelID, 1))
		return 0;

	return AP_HitEncounterOpportunity(destLevelID, AP_HitEffectivePlayer(physLevelID)) >= 0;
}

void AP_HitLogChooser(int physLevelID, int destLevelID, int action, int route,
                      int tokenLeft, int relicLeft)
{
	static const char *const kNames[] = {"none", "open", "wait", "plain",
	                                     "apply", "cancel", "vanish"};
	char line[160];
	if (action == AP_HIT_CHOOSER_NONE || action == AP_HIT_CHOOSER_WAIT)
		return;
	if (action < 0 || action > AP_HIT_CHOOSER_VANISH)
		return;
	snprintf(line, sizeof line,
	         "[AP HIT] chooser pad=%d dest=%d action=%s route=%d token=%d relic=%d\n",
	         physLevelID, destLevelID, kNames[action],
	         action == AP_HIT_CHOOSER_APPLY ? route : (action == AP_HIT_CHOOSER_PLAIN ? 0 : -1),
	         tokenLeft, relicLeft);
	AP_LogLine(line);
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
	// Sized for the widest case: a cup pad aggregates its four legs' rungs
	// (4 * CTR_CFG_PODIUM_RUNG_COUNT) plus its own gem bit.
	int uncBits[24];
	int uncN;
	int boxesLeft;
	int lettersLeft;
	int wumpaLeft;
	int perfectLeft;
	int hitOpp;
	int weaponOpp;

	if (!ctr_cfg_active())
		return 0; // vanilla mode -> caller leaves the pad untouched
	if (!AP_DestKnown(destLevelID))
		return 0;

	// Rung-AWARE count (issue #61). The Done state below hard-locks the pad, so it
	// must not trigger while ANY location behind this pad is still checkable. The
	// podium rungs are exactly such locations, and for a cup they live on the four
	// LEG tracks rather than on the cup itself. Counting only the plain tier bits
	// meant a Gem Cup went Done the moment its gem was checked, hard-locking the pad
	// while its legs' held/finish rungs were still uncollected -- and because a cup
	// leg is not independently raceable from the hub, those checks became
	// unreachable for the rest of the seed. The same held for a race pad whose tier
	// bits were all checked while a rung remained.
	// AP_PadUncollectedGlowBits already enumerates rungs and already aggregates the
	// four cup legs, and it degrades to the plain count when podium checks are off,
	// so seeds without the feature are byte-identical.
	uncN = AP_PadUncollectedGlowBits(destLevelID,
	                                 uncBits, (int)(sizeof uncBits / sizeof uncBits[0]));

	// Box-AWARE, and for the same reason the count above is rung-aware (§6, ruled
	// 2026-08-11): both Done and Re-locked strand whatever is left behind the pad,
	// and an itemsanity box is exactly such a location. Boxes carry no AdvProgress
	// bit, so they cannot ride in uncBits and are counted separately.
	boxesLeft = AP_PadUncollectedBoxCount(destLevelID);
	// Letter locations carry no AdvProgress reward bit, just like boxes. They
	// belong to this destination's CTR Challenge and must keep the pad out of
	// Done even after the ordinary Token location is checked.
	lettersLeft = AP_PadUncollectedLetterCount(destLevelID);
	// Per-track Wumpa is track-owned but has several physical routes. Keep a
	// standalone pad and every legging Cup out of Done until the server checks
	// it. Before stage 2 it needs the plain Trophy-race route; after stage 2 the
	// entry chooser treats it as a CTR Challenge-side reason to remain open.
	wumpaLeft = AP_PadUncollectedWumpaCount(destLevelID);
	// The Relic Race Perfect check has no AdvProgress bit either. Without this
	// the pad went Done (hard-locked) once all three relics were checked, even
	// with the perfect still open (#439).
	perfectLeft = AP_PadUncollectedRelicPerfectCount(destLevelID);
	// The trial Trophy and CTR Challenge are already in uncBits (#343).

	// Hit Character encounter (pool draw): an eligible racer other than the
	// player with an unchecked Hit location is a remaining check AND needs a
	// plain race, so it joins both counters. Without the reRaceChecksStanding half a stage-2-locked pad would
	// Re-lock and strand the guest; without the uncCount half the pad would go
	// Done and hard-lock it.
	hitOpp = AP_HitPadOpportunity(physLevelID, destLevelID);
	uncN += hitOpp;

	// Itemsanity use check (#452): same two-counter shape as the Hit
	// opportunity. A received weapon is fired in any retail race, and logic
	// expects one to be available, so the pad must neither go Done nor
	// Re-lock while a received weapon still has an unchecked use check.
	weaponOpp = AP_ItemsanityPadOpportunity(destLevelID);
	uncN += weaponOpp;

	// The table itself lives in ap_pad_state.h so the harness can pin it out of
	// engine; everything above is the gather. Requirements key off the PHYSICAL
	// pad, lifecycle facts off the DESTINATION, which is the model's whole
	// keying invariant.
	return AP_PadStateDecide(AP_DestIsRace(destLevelID),
	                         AP_PadStage1Met(physLevelID),
	                         ctr_cfg_racer_lock_met(physLevelID),
	                         AP_DestTrophyChecked(destLevelID),
	                         ctr_cfg_warp_stage2_unlocked(physLevelID),
	                         uncN + lettersLeft + wumpaLeft + perfectLeft,
	                         boxesLeft + wumpaLeft + hitOpp + weaponOpp);
}

// Is this pad in the phase-1 re-entry window? True exactly when the
// destination's trophy race is already checked and the pad is STILL state 2
// Raceable. For a race destination with the trophy checked, that means an AP
// box or per-track Wumpa check still needs a plain adventure re-race. Any other
// route to 2 has an unchecked trophy or a non-race destination, both excluded.
//
// Defined ON TOP of AP_PadState rather than beside it deliberately: the entry
// gate and the pad's look in AH_WarpPad.c consume this, so they cannot drift
// from the state the map paints. That drift IS issue #232 -- the state model
// promised the pad stayed enterable and the gate had no path for it.
int AP_PadPhase1ReRaceable(int physLevelID, int destLevelID)
{
	if (!ctr_cfg_active())
		return 0;
	if (!AP_DestIsRace(destLevelID))
		return 0;
	if (!AP_DestTrophyChecked(destLevelID))
		return 0;
	return AP_PadState(physLevelID, destLevelID) == 2;
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

// Human-readable name for an AP_PadRoute code. The tier-2 half is written out
// as BASE + the tier-2 enumerator rather than as bare numbers, so a route added
// to AP_PadTier2RouteDecide shows up here as a missing case, not a wrong label.
static const char *AP_PadRouteName(int route)
{
	switch (route)
	{
	case AP_PAD_ROUTE_S2LOCKED_PLAIN_RERACE:
		return "plain-re-race(stage2-locked)";
	case AP_PAD_ROUTE_S2LOCKED_INERT:
		return "inert(stage2-locked)";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_MENU:
		return "tier2-menu";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_TOKEN:
		return "tier2-token";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_RELIC:
		return "tier2-relic";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_BOX_RERACE:
		return "box-re-race(tier2)";
	case AP_PAD_ROUTE_TIER2_BASE + AP_PAD_TIER2_DONE:
		return "done-defensive";
	default:
		break;
	}
	return "unknown";
}

// One "[AP PAD]" line per pad-decision TRANSITION (issues #232 / #265). Both
// gates that call this sit inside AH_WarpPad_ThTick and therefore run every
// frame the player stands on the pad, so the whole reported tuple is remembered
// and an unchanged decision stays silent. Only one pad can be in the entry
// sequence at a time, so a single remembered tuple cannot thrash between pads.
void AP_PadLogRoute(int physLevelID, int destLevelID, int route)
{
	static int s_phys = -1;
	static int s_dest = -1;
	static int s_route = -1;
	static int s_state = -1;
	static int s_trophy = -1;
	static int s_token = -1;
	static int s_relic = -1;
	static int s_boxes = -1;
	static int s_wumpa = -1;

	int uncBits[24];
	int uncN;
	int k;
	int state;
	int trophyChecked;
	int tokenLeft = 0;
	int relicLeft = 0;
	int boxesLeft;
	int wumpaLeft;
	char msg[192];

	if (!ctr_cfg_active())
		return; // vanilla mode -- no AP pad lifecycle to describe
	if (!AP_DestKnown(destLevelID))
		return;

	state = AP_PadState(physLevelID, destLevelID);
	trophyChecked = AP_DestTrophyChecked(destLevelID) ? 1 : 0;
	boxesLeft = AP_PadUncollectedBoxCount(destLevelID);
	wumpaLeft = AP_PadUncollectedWumpaCount(destLevelID);

	// The same enumerator and the same offsets the tier-2 chooser itself reads,
	// so the logged flags cannot disagree with the decision they explain. These
	// are "an unchecked location of this tier still exists" -- a tier this seed
	// never placed reports 0 here exactly as it does to the chooser.
	uncN = AP_PadUncollectedBits(destLevelID, uncBits,
	                             (int)(sizeof uncBits / sizeof uncBits[0]));
	for (k = 0; k < uncN; k++)
	{
		int off = uncBits[k] - destLevelID;
		if (destLevelID == AP_CORTEX_DEST)
		{
			if (uncBits[k] == AP_CortexPseudoBit(AP_CV_SLOT_TOKEN))
				tokenLeft = 1;
			else if (AP_CortexPseudoRelicTier(uncBits[k]) >= 0)
				relicLeft = 1;
			continue;
		}
		if (off == ADV_REWARD_FIRST_CTR_TOKEN)
			tokenLeft = 1;
		else if (off == ADV_REWARD_FIRST_SAPPHIRE_RELIC ||
		         off == ADV_REWARD_FIRST_GOLD_RELIC ||
		         off == ADV_REWARD_FIRST_PLATINUM_RELIC)
			relicLeft = 1;
	}
	tokenLeft = AP_PadTokenSideLeft(
	    tokenLeft, AP_PadUncollectedLetterCount(destLevelID), wumpaLeft);
	relicLeft = AP_PadRelicSideLeft(relicLeft,
	                                AP_PadUncollectedRelicPerfectCount(destLevelID));

	if (physLevelID == s_phys && destLevelID == s_dest && route == s_route &&
	    state == s_state && trophyChecked == s_trophy && tokenLeft == s_token &&
	    relicLeft == s_relic && boxesLeft == s_boxes && wumpaLeft == s_wumpa)
		return; // nothing the line reports has changed since it was last emitted

	s_phys = physLevelID;
	s_dest = destLevelID;
	s_route = route;
	s_state = state;
	s_trophy = trophyChecked;
	s_token = tokenLeft;
	s_relic = relicLeft;
	s_boxes = boxesLeft;
	s_wumpa = wumpaLeft;

	snprintf(msg, sizeof msg,
	         "[AP PAD] pad %d -> dest %d: state=%d trophy_checked=%d token_left=%d "
	         "relic_left=%d boxes_left=%d wumpa_left=%d route=%s\n",
	         physLevelID, destLevelID, state, trophyChecked, tokenLeft, relicLeft,
	         boxesLeft, wumpaLeft, AP_PadRouteName(route));
	AP_LogLine(msg);
}

// ---------------------------------------------------------------------------
// AP STATE GENERATION (Warp-Pad State Model v2, foundation for live re-birth).
// A monotonically increasing counter bumped whenever something that can change a
// pad's AP_PadState happens: a fresh slot-connect (slot_data activation), a
// received item that changes a gate count, or a location-checked notification.
// A future in-hub re-birth consumer tags each pad with the generation it was
// born at and, on mismatch, rebuilds the pad's in-hub instances so the 3D look
// tracks state changes in real time (item arrives / check completes elsewhere
// while standing in the hub) -- and re-remaps a pad born before a late connect.
//
// NOT yet consumed for re-birth: AH_WarpPad_LInB is a LEVEL-LOAD one-shot (see
// INSTANCE_LevLInBs), and the only teardown pattern the codebase proves is
// destruction (inst->thread=0 + THREAD_FLAG_DEAD, e.g. RB_Crate). A safe in-hub
// re-birth needs a lifecycle this code does not yet establish, so that consumer
// is deferred to a focused, in-game-verified change rather than inferred blind
// (crew verify-first rule). The MAP colour and every gate already recompute
// AP_PadState live each frame, so those two surfaces are honest in real time
// today; only the in-hub 3D structural look and a late-connect pad's destination
// remap wait on this re-birth.
// ---------------------------------------------------------------------------
static unsigned ap_state_gen = 0;

unsigned AP_StateGen(void)
{
	return ap_state_gen;
}

// ---------------------------------------------------------------------------
// LOCATION EVENTS (option A) -- authoritative. Called from the game's reward
// grant sites; logs the check and sends it to the server.
// ---------------------------------------------------------------------------

// Goal completion state. The Oxide beats are game EVENTS (set by AP_NotifyGoal);
// item-based goals read received counts. ap_goal_sent makes the send idempotent.
static int ap_oxide_first_beaten = 0;
static int ap_oxide_final_beaten = 0;
static int ap_goal_sent = 0;
static AP_GoalPresentationState ap_goal_presentation = {0};

// Send StatusUpdate(GOAL) once when the composed per-seed goal is met (issue
// #152). ctr_cfg.goal_oxide/goal_bosses/goal_gems replace the old single
// ctr_cfg.goal switch: the goal is the AND of every ACTIVE condition (0
// means "off" for all three; the apworld's generate_early guarantees at
// least one condition is active, so this is never vacuous on a real seed).
//   goal_oxide 1 (first) -> beat N. Oxide's Challenge
//   goal_oxide 2 (final) -> beat N. Oxide's Final Challenge
//   goal_bosses N (>0)   -> N of the 4 boss races personally won, from the
//                           #458 boss-won flags (AP_ComposedBossesWon). NOT
//                           the checked boss-race LOCATION (Collect, Release
//                           and admin commands check it too), NOT
//                           AP_GateCount(Trophy)>=16, and NOT CHECK_ADV_BIT
//                           on the boss key bits 94-97: those are the Key
//                           item pool (AP_POOL_KEY), so AP_ApplyItems sets
//                           them from RECEIVED keys -- holding 4 shuffled
//                           Keys is not beating 4 bosses (the BUG-D class).
//   goal_gems N (>0)     -> N of the 5 Gems held. AP_GateCountGemSum() is
//                           exactly a distinct-colour count here, because
//                           every Gem is a singleton item (at most one
//                           received copy per colour).
// "Any N" on both counted conditions, mirrored identically on the apworld
// side (dossier C11): a tally compared against the configured count, never
// "the first N in a fixed order".
// Without slot_data (vanilla / no AP config) we keep the legacy behaviour:
// the first Oxide beat is the goal.
static void AP_FeedEnqueue(const char *text, int color, int playCue); // defined with the feed block below

// How many of the four boss RACES the player has personally won, in the one
// form every consumer of goal_bosses must use: the #458 boss-won flags
// (ap/ap_boss_won_flags.h), set on each live boss win and kept per seed, team
// and slot in server data storage. NOT the checked boss-race LOCATION: the
// server also checks it for another player's Collect, this player's own
// !collect or !release, and admin commands, so a Collect could complete the
// goal with no boss raced (#458). Never CHECK_ADV_BIT on bits 94-97 and never
// received Keys either -- those are the Key item pool's mirror, rewritten by
// AP_ApplyItems from RECEIVED items on every reconcile tick (the BUG-D class).
// Factored out of AP_EvaluateGoal so the goal evaluator, the goal advert, the
// Oxide garage gate (WO-A1) and the DeathLink win logic cannot drift apart
// about what "won a boss race" means. Counts 0 until the flags are known or a
// win is recorded, which only delays the goal.
static int AP_ComposedBossesWon(void)
{
	return AP_BossWonCount(ap_net_boss_won_bits());
}

void AP_EvaluateGoal(void)
{
	int done;

	if (ap_goal_sent)
		return;

	// #163: never auto-goal on a shape this build did not parse. The verifier
	// already returns early on schema_newer (ap_verify.c) so it never reasons
	// over an unknown shape; the goal evaluator must be at least as careful,
	// because there is NO safe legacy value for ctr_options.goal -- whatever a
	// newer apworld writes there resolves on this build to some real, reachable
	// win condition, so guessing sends GOAL early rather than not at all.
	// (schema_newer is only ever set while slot_data is active, so vanilla /
	// no-config sessions never take this branch.)
	if (ctr_cfg.schema_newer)
	{
		static int warned = 0;
		if (!warned)
		{
			warned = 1;
			AP_LogLine("[AP GOAL] auto-goal disabled: seed schema is newer than this client\n");
			AP_FeedEnqueue("GOAL CHECK OFF: seed needs a newer client", ORANGE, 0);
		}
		return;
	}

	if (!ctr_cfg_active())
	{
		done = ap_oxide_first_beaten;
	}
	else
	{
		int won = AP_ComposedBossesWon();
		done = AP_ComposedGoalMet(
		    ctr_cfg.goal_oxide, ap_oxide_first_beaten, ap_oxide_final_beaten,
		    ctr_cfg.goal_bosses, won,
		    ctr_cfg.goal_gems, AP_GateCountGemSum());
	}

	if (done)
	{
		char msg[128];
		ap_goal_sent = 1;
		snprintf(msg, sizeof msg,
		         "[AP GOAL] goal (oxide=%d bosses=%d gems=%d) complete -- StatusUpdate(GOAL)\n",
		         ctr_cfg_active() ? ctr_cfg.goal_oxide : -1,
		         ctr_cfg_active() ? ctr_cfg.goal_bosses : -1,
		         ctr_cfg_active() ? ctr_cfg.goal_gems : -1);
		AP_AppendLog(msg);
		ap_net_send_goal();
	}

	AP_GoalPresentationEvaluate(&ap_goal_presentation, done);
}

void AP_GoalArmLiveEvent(void)
{
	AP_GoalPresentationArm(&ap_goal_presentation);
}

int AP_GoalClaimOxideEnding(void)
{
	return AP_GoalPresentationClaim(&ap_goal_presentation);
}

// Per-session dedup so a grant site that re-enters (an end-event drawn every
// frame, or a reward path without its own CHECK_ADV_BIT guard) only produces one
// check. 192 bits = the 6 AdvProgress reward words.
static u32 ap_notified_mask[6] = {0};

// Podium fan-out lives with the race listener below; forward-declared here so
// the trophy-award path (a win => 1st => every rung) and the connect-time
// reconciliation sweep can both drive it as crash/miss backstops.
static void AP_SendPodiumChecks(int track, int placement);
static void AP_ReconcilePodiumFromTrophies(void);

// The rest of the ap_hooks unit continues in the ap_hooks_*.c files that
// game/game_unity.h includes right after this one (issue #305).

#endif // CTR_AP
