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
#include "ap_boss_map_star.h" // #458 follow-up: boss garage star on the hub map
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
// (SDL_SCANCODE_M = 16). Not bound by default on the Controls page
// (s_defaultKeyboard in platform/native_controls.c), so toggling the overlay
// never disturbs gameplay input unless a player binds M themselves.
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

// The rest of the ap_hooks unit continues in the ap_hooks_*.c files that
// game/game_unity.h includes right after this one (issue #305).

#endif // CTR_AP
