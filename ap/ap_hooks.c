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

// The rest of the ap_hooks unit continues in the ap_hooks_*.c files that
// game/game_unity.h includes right after this one (issue #305).

#endif // CTR_AP
