// ap_hooks_frame.c -- part of the ap_hooks unit (issue #305).
// The ap-state.json state dump and the per-frame entry point AP_OnFrame.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// ---------------------------------------------------------------------------
// STATE DUMP -- write a JSON snapshot of the live AP <-> game state to
// ap-state.json (overwritten on a throttle) so the game's belief can be diffed
// against the apworld seed / slot_data offline. Debug-only; no gameplay effect.
// Relative path -> lands next to the exe (the play folder, same dir as
// ap-config.txt). Pairs received-item truth + the game's own counters + the
// per-pad resolved requirement & whether it's met + trophy-race checked-state,
// which is exactly the surface where game/apworld misalignment shows up.
// ---------------------------------------------------------------------------
#define AP_STATE_FILE "ap-state.json"

// The dump is built in memory on the game thread and written by the background
// writer (ap_file_writer.h): field logs showed the old in-place fopen("w") +
// fprintf run stalling the game for up to 1.7 s on some Windows hosts. The file
// content is unchanged. A dump identical to the last one queued is not written
// again (most of the menu and hub time), unless that write failed.
static AP_TextBuf ap_state_text;        // built by AP_DumpState, reused
static AP_TextBuf ap_state_text_queued; // the last text handed to the writer

// Writer-thread job: only its payload and the file system.
static int AP_StateWriteJob(const void *data, size_t len)
{
	return ap_file_writer_replace_file(AP_STATE_FILE, data, len);
}

static const char *AP_ITEM_NAMES[AP_ITEM_INDEX_COUNT] = {
    "Trophy", "Sapphire Relic", "Gold Relic", "Platinum Relic",
    "Red CTR Token", "Green CTR Token", "Blue CTR Token", "Yellow CTR Token",
    "Purple CTR Token", "Red Gem", "Green Gem", "Blue Gem", "Yellow Gem",
    "Purple Gem", "Key"};

static const char *AP_REQ_TYPE_NAMES[9] = {
    "none", "trophy", "key", "token", "sapphire", "gem",
    "any_token", "any_relic", "any_gem"};

static const char *AP_BOSS_NAMES[CTR_CFG_BOSS_COUNT] = {
    "ripper_roo", "papu_papu", "komodo_joe", "pinstripe", "oxide"};

static const char *AP_ReqTypeName(int t)
{
	return (t >= 0 && t < 9) ? AP_REQ_TYPE_NAMES[t] : "?";
}

// Tier-aware requirement name for the debug dump. A type-4 relic req carries its
// tier in colour (schema 4: 0=Sapphire, 1=Gold, 2=Platinum; legacy -1=Sapphire),
// so the flat "sapphire" name would misread every tier as sapphire. Widen only
// type 4; every other type is colour-independent and defers to AP_ReqTypeName.
static const char *AP_ReqTypeNameColour(int t, int colour)
{
	if (t == 4)
	{
		switch (colour)
		{
		case 1:  return "gold_relic";
		case 2:  return "platinum_relic";
		default: return "sapphire_relic"; // colour 0 or legacy -1
		}
	}
	return AP_ReqTypeName(t);
}

#ifdef CTR_CUSTOM_PACKAGES
// The identity-facing level id of the level being played, and of the one before
// it, for diagnostics. gGT->prevLEV is the raw engine id, so after a custom race
// it names the host slot (the files it borrowed) rather than the custom track.
// Sampled every frame, so the last sample before a level change is the level
// just left, custom or not.
static int s_apIdentityRaw = -1;
static int s_apIdentityLevel = -1;
static int s_apPrevIdentityRaw = -1;
static int s_apPrevIdentityLevel = -1;

static void AP_TrackIdentityLevel(struct GameTracker *gGT)
{
	int raw = (int)gGT->levelID;

	if (raw != s_apIdentityRaw)
	{
		s_apPrevIdentityRaw = s_apIdentityRaw;
		s_apPrevIdentityLevel = s_apIdentityLevel;
		s_apIdentityRaw = raw;
	}
	s_apIdentityLevel = MainRaceTrack_IdentityLevelID();
}

// gGT->prevLEV as an identity, when the level change it records is the one
// sampled above; the raw value otherwise.
static int AP_PrevIdentityLevel(struct GameTracker *gGT)
{
	if (s_apPrevIdentityRaw >= 0 && s_apPrevIdentityRaw == (int)gGT->prevLEV)
		return s_apPrevIdentityLevel;
	return (int)gGT->prevLEV;
}
#endif

static void AP_DumpState(struct GameTracker *gGT)
{
	int i, checked = 0;
	AP_TextBuf *f = &ap_state_text;

	AP_TextBufReset(f);

	for (i = 0; i < AP_LOCATION_TABLE_LEN; i++)
		if (ap_net_location_checked(AP_LOCATION_TABLE[i].location_code))
			checked++;

	AP_TextBufPuts(f, "{\n");
	AP_TextBufPrintf(f, "  \"schema_active\": %d,\n", ctr_cfg_active());
	AP_TextBufPrintf(f, "  \"doors\": {\"ready\": %d, \"history_and_pending\": %u, \"session\": %u, \"keys\": %d},\n",
	        AP_DoorHistoryReady(), ap_net_doors_history(), ap_net_doors_session(), AP_GateCount(AP_IDX_KEY));
	AP_TextBufPrintf(f, "  \"in_adventure\": %d,\n",
	        (gGT->gameMode1 & ADVENTURE_MODE) != 0 ? 1 : 0);
	AP_TextBufPrintf(f, "  \"connected_slot\": %d,\n", ap_net_self_slot());
	AP_TextBufPrintf(f, "  \"locations_checked\": %d,\n", checked);
	AP_TextBufPrintf(f, "  \"locations_total\": %d,\n", AP_LOCATION_TABLE_LEN);
	// Live race placement + the held-position listener's debounce state (the #9
	// live rung listener). held.cand_pos/cand_ms = the position currently being
	// debounced and how long it has held; held.best_pos = best position already
	// fanned out this race (99 = none yet); held.countdown_seen = the load-gap latch.
	AP_TextBufPrintf(f, "  \"live_position\": %d,\n", ap_live_position);
	AP_TextBufPrintf(f,
	        "  \"held\": {\"cand_pos\": %d, \"cand_ms\": %d, \"best_pos\": %d, "
	        "\"countdown_seen\": %d, \"debounce_ms\": %d},\n",
	        ap_held_cand_pos, ap_held_cand_ms, ap_held_best_pos,
	        ap_held_countdown_seen, AP_HELD_DEBOUNCE_MS);
	// Current-track rung contract versus live server membership. This is the
	// minimum state needed to distinguish parser/config drift from a listener or
	// network-send defect in the Roo's Tubes Held 3rd/5th report. Keep it generic
	// so the same diagnostic covers every randomized cup leg and trophy track.
	{
#ifdef CTR_CUSTOM_PACKAGES
		int track = MainRaceTrack_IdentityLevelID();
#else
		int track = (int)gGT->levelID;
#endif
		long rung[CTR_CFG_PODIUM_RUNG_COUNT] = {-1, -1, -1, -1, -1};
		if (track >= 0 && track < CTR_CFG_PODIUM_STORAGE_COUNT)
		{
			const ctr_podium_rungs *pr = &ctr_cfg.podium[track];
			rung[0] = pr->held_1st;
			rung[1] = pr->held_3rd;
			rung[2] = pr->held_5th;
			rung[3] = pr->finish_podium;
			rung[4] = pr->finish_any;
		}
		AP_TextBufPrintf(f,
		        "  \"podium_current_track\": {\"enabled\": %d, \"track\": %d, "
		        "\"codes\": [%ld,%ld,%ld,%ld,%ld], "
		        "\"exists\": [%d,%d,%d,%d,%d], "
		        "\"checked\": [%d,%d,%d,%d,%d]},\n",
		        ctr_cfg.podium_enabled, track,
		        rung[0], rung[1], rung[2], rung[3], rung[4],
		        rung[0] >= 0 && ap_net_location_exists(rung[0]),
		        rung[1] >= 0 && ap_net_location_exists(rung[1]),
		        rung[2] >= 0 && ap_net_location_exists(rung[2]),
		        rung[3] >= 0 && ap_net_location_exists(rung[3]),
		        rung[4] >= 0 && ap_net_location_exists(rung[4]),
		        rung[0] >= 0 && ap_net_location_checked(rung[0]),
		        rung[1] >= 0 && ap_net_location_checked(rung[1]),
		        rung[2] >= 0 && ap_net_location_checked(rung[2]),
		        rung[3] >= 0 && ap_net_location_checked(rung[3]),
		        rung[4] >= 0 && ap_net_location_checked(rung[4]));
	}
	AP_TextBufPrintf(f,
	        "  \"last_race\": {\"track\": %d, \"placement\": %d, "
	        "\"type\": \"%s\"},\n",
	        ap_last_race_track, ap_last_race_place,
	        AP_RaceTypeName(ap_last_race_mode));
	// Boss-to-hub transition diagnostics. These fields make the #269 zero-Key
	// path distinguishable without guessing from symptoms: the two freeze bits
	// are the known modal holds, podium_reward_id selects the boss/podium return
	// path, and SPAWN_AT_BOSS changes where VehBirth places the driver. Keep the
	// raw mode words as well so a future report does not lose an unknown bit.
	{
		struct Driver *driver = gGT->drivers[0];
		// `diag` (2026-08-23 Alpha 3 bundle inspection): the fields the two
		// hub-lock bundles were missing. Which hold the kart is under (kart
		// state + the DRIVER_FUNC_INIT identity the picker and the door freeze
		// both swap), whether a pause / RectMenu / mask hint still owns the
		// screen, the picker's own state machine, the trap scheduler's slot
		// counts, and the authoritative received-Key count beside the cosmetic
		// profile count that VehBirth's door-freeze test actually reads.
		AP_TransitionDiag diag;
		char diagText[512];
		memset(&diag, 0, sizeof diag);
		diag.kartState = driver != NULL ? (int)driver->kartState : -1;
		if (driver == NULL)
			diag.initFunc = AP_DIAG_INIT_NO_DRIVER;
		else if (driver->funcPtrs[DRIVER_FUNC_INIT] == NULL)
			diag.initFunc = AP_DIAG_INIT_NULL;
		else if (driver->funcPtrs[DRIVER_FUNC_INIT] == VehPhysProc_Driving_Init)
			diag.initFunc = AP_DIAG_INIT_DRIVING;
		else if (driver->funcPtrs[DRIVER_FUNC_INIT] == VehPhysProc_FreezeEndEvent_Init)
			diag.initFunc = AP_DIAG_INIT_FREEZE_END;
		else
			diag.initFunc = AP_DIAG_INIT_OTHER;
		diag.pauseState = (int)sdata->pause_state;
		diag.activeMenu = sdata->ptrActiveMenu != NULL;
		diag.akuHintState = (int)sdata->AkuAkuHintState;
		diag.loadingStage = (int)sdata->Loading.stage;
		AP_CharSwap_DiagState(&diag.pickerOpen, &diag.pickerPending, &diag.pickerRestore);
		AP_TrapDiagCounts(&diag.trapsArmed, &diag.trapsWarning, &diag.trapsActive,
		                  &diag.trapsSuspended);
		diag.receivedKeys = AP_GateCount(AP_IDX_KEY);
		diag.profileKeys = (int)gGT->currAdvProfile.numKeys;
		AP_TransitionDiagFormat(diagText, (int)sizeof diagText, &diag);

		AP_TextBufPrintf(f,
		        "  \"transition\": {\"level_id\": %d, \"prev_level_id\": %d, "
		        "\"podium_reward_id\": %d, \"game_mode1\": %u, "
		        "\"game_mode2\": %u, \"freeze_door\": %d, "
		        "\"freeze_podium\": %d, \"spawn_at_boss\": %d, "
		        "\"driver_present\": %d, \"driver_actions\": %u, "
		        "\"diag\": %s},\n",
#ifdef CTR_CUSTOM_PACKAGES
		        MainRaceTrack_IdentityLevelID(), AP_PrevIdentityLevel(gGT),
#else
		        (int)gGT->levelID, (int)gGT->prevLEV,
#endif
		        (int)gGT->podiumRewardID, (unsigned)gGT->gameMode1,
		        (unsigned)gGT->gameMode2,
		        (gGT->gameMode2 & VEH_FREEZE_DOOR) != 0,
		        (gGT->gameMode2 & VEH_FREEZE_PODIUM) != 0,
		        (gGT->gameMode2 & SPAWN_AT_BOSS) != 0,
		        driver != NULL,
		        driver != NULL ? (unsigned)driver->actionsFlagSet : 0u,
		        diagText);
	}
	AP_TextBufPrintf(f,
	        "  \"options\": {\"goal\": %d, \"goal_oxide\": %d, "
	        "\"goal_bosses\": %d, \"goal_gems\": %d, "
	        "\"oxide_final_unlock\": %d, "
	        "\"oxide_final_count\": %d, \"schema_newer\": %d, "
	        "\"warppad_unlock_mode\": %d, \"bossgarage_mode\": %d, "
	        "\"shuffle_warp_pads\": %d},\n",
	        ctr_cfg.goal, ctr_cfg.goal_oxide, ctr_cfg.goal_bosses,
	        ctr_cfg.goal_gems, ctr_cfg.oxide_final_unlock, ctr_cfg.oxide_final_count,
	        ctr_cfg.schema_newer, ctr_cfg.warppad_unlock_mode,
	        ctr_cfg.bossgarage_mode, ctr_cfg.shuffle_warp_pads);

	// The game's own cached counters (what gates/UI read) -- compare vs AP truth.
	// numCtrTokens is a six-int struct, not a scalar. The former varargs call
	// passed that whole struct to %d and happened to expose only its first word
	// on the current ABI. Report the meaningful aggregate and every component
	// explicitly: GAMEPROG counts the 16 race tokens per colour AND in total,
	// and the four Crystal Challenge tokens separately in purple. The per-colour
	// mirrors exist because received_items reports the five token colours
	// separately and a pad requirement can name a specific colour, so an
	// aggregate alone cannot identify a wrong regular-colour mirror (#281).
	// Identities a valid dump must satisfy: tokens_regular == red+green+blue+
	// yellow, tokens == tokens_regular + tokens_purple.
	const int gameTokensRegular = gGT->currAdvProfile.numCtrTokens.total;
	const int gameTokensPurple = gGT->currAdvProfile.numCtrTokens.purple;
	AP_TextBufPrintf(f,
	        "  \"game_counters\": {\"trophies\": %d, \"relics\": %d, "
	        "\"keys\": %d, \"tokens\": %d, \"tokens_regular\": %d, "
	        "\"tokens_red\": %d, \"tokens_green\": %d, \"tokens_blue\": %d, "
	        "\"tokens_yellow\": %d, "
	        "\"tokens_purple\": %d, \"completion_pct\": %d},\n",
	        gGT->currAdvProfile.numTrophies, gGT->currAdvProfile.numRelics,
	        gGT->currAdvProfile.numKeys, gameTokensRegular + gameTokensPurple,
	        gameTokensRegular,
	        gGT->currAdvProfile.numCtrTokens.red,
	        gGT->currAdvProfile.numCtrTokens.green,
	        gGT->currAdvProfile.numCtrTokens.blue,
	        gGT->currAdvProfile.numCtrTokens.yellow,
	        gameTokensPurple,
	        gGT->currAdvProfile.completionPercent);

	// AP-side truth: received-item counts by type.
	AP_TextBufPuts(f, "  \"received_items\": {");
	for (i = 0; i < AP_ITEM_INDEX_COUNT; i++)
		AP_TextBufPrintf(f, "%s\"%s\": %d", i ? ", " : "", AP_ITEM_NAMES[i],
		        AP_GateCount(i));
	AP_TextBufPuts(f, "},\n");

	// Per-pad: destination, resolved requirement, met?, trophy-race checked?
	AP_TextBufPuts(f, "  \"pads\": [\n");
	for (i = 0; i < CTR_CFG_PAD_COUNT; i++)
	{
		const ctr_warp_unlock *u = &ctr_cfg.warp_pad_unlock[i];
		const ctr_req *r = &u->stage1;
		int dest = ctr_cfg_warp_dest(i);
		// trophy_checked only means something for the 16 race-track destinations
		// (LevelID 0..15, the trophy-race bit pool). For non-race dests (trials,
		// arenas, cups) dest+TROPHY_OFFSET collides into a relic bit and would read
		// a false positive -- emit -1 (n/a) instead.
		int trophyChecked =
		    (dest >= 0 && dest < 16)
		        ? AP_LocationCheckedByBit(dest + ADV_REWARD_FIRST_TROPHY)
		        : (dest == AP_CORTEX_DEST ? AP_CortexTrackChecked(AP_CV_SLOT_TROPHY) : -1);
		AP_TextBufPrintf(f,
		        "    {\"pad\": %d, \"dest\": %d, \"req_type\": \"%s\", "
		        "\"count\": %d, \"colour\": %d, \"unlocked\": %d, "
		        "\"stage2_req_type\": \"%s\", \"stage2_count\": %d, "
		        "\"stage2_colour\": %d, \"stage2_unlocked\": %d, "
		        "\"trophy_checked\": %d}%s\n",
		        i, dest, AP_ReqTypeNameColour(r->type, r->colour), r->count, r->colour,
		        ctr_cfg_warp_unlocked(i),
		        AP_ReqTypeNameColour(u->stage2.type, u->stage2.colour),
		        u->stage2.count, u->stage2.colour,
		        ctr_cfg_warp_stage2_unlocked(i), trophyChecked,
		        (i + 1 < CTR_CFG_PAD_COUNT) ? "," : "");
	}
	AP_TextBufPuts(f, "  ],\n");

	// Cup PHYSICAL pads (100..104): map + two-stage req + met, so contract §6.3
	// offline diffing (ap-state.json vs slot_data) covers destination-shuffled cups
	// the same way the dense "pads" array covers 0..27. dest = gem_cup_map (a race
	// destination here means the cup pad hosts a trophy race); trophy_checked follows
	// the DESTINATION when that destination is a race track, else -1 (n/a).
	AP_TextBufPuts(f, "  \"cup_pads\": [\n");
	for (i = 0; i < 5; i++)
	{
		const ctr_warp_unlock *u = &ctr_cfg.gem_cup_unlock[i];
		const ctr_req *r = &u->stage1;
		int phys = 100 + i;
		int dest = ctr_cfg_warp_dest(phys);
		int trophyChecked =
		    (dest >= 0 && dest < 16)
		        ? AP_LocationCheckedByBit(dest + ADV_REWARD_FIRST_TROPHY)
		        : (dest == AP_CORTEX_DEST ? AP_CortexTrackChecked(AP_CV_SLOT_TROPHY) : -1);
		AP_TextBufPrintf(f,
		        "    {\"pad\": %d, \"dest\": %d, \"req_type\": \"%s\", "
		        "\"count\": %d, \"colour\": %d, \"unlocked\": %d, "
		        "\"stage2_req_type\": \"%s\", \"stage2_count\": %d, "
		        "\"stage2_colour\": %d, \"stage2_unlocked\": %d, "
		        "\"trophy_checked\": %d}%s\n",
		        phys, dest, AP_ReqTypeNameColour(r->type, r->colour), r->count, r->colour,
		        ctr_cfg_warp_unlocked(phys),
		        AP_ReqTypeNameColour(u->stage2.type, u->stage2.colour),
		        u->stage2.count, u->stage2.colour,
		        ctr_cfg_warp_stage2_unlocked(phys), trophyChecked,
		        (i + 1 < 5) ? "," : "");
	}
	AP_TextBufPuts(f, "  ],\n");

	// Boss garages: resolved requirement + met?
	AP_TextBufPuts(f, "  \"bosses\": [\n");
	for (i = 0; i < CTR_CFG_BOSS_COUNT; i++)
	{
		const ctr_req *r = &ctr_cfg.boss_req[i];
		AP_TextBufPrintf(f,
		        "    {\"boss\": \"%s\", \"req_type\": \"%s\", \"count\": %d, "
		        "\"met\": %d}%s\n",
		        AP_BOSS_NAMES[i], AP_ReqTypeNameColour(r->type, r->colour), r->count,
		        AP_BossReqMet(r), (i + 1 < CTR_CFG_BOSS_COUNT) ? "," : "");
	}
	AP_TextBufPuts(f, "  ]\n");

	AP_TextBufPuts(f, "}\n");

	// Hand the text to the background writer only when it differs from the last
	// one queued (or that write failed). The open/write/close then happens off
	// the game thread; see ap_file_writer.h.
	if (!AP_TextBufShouldWrite(f, &ap_state_text_queued,
	                           ap_file_writer_last_ok(AP_FILE_WRITER_SLOT_STATE)))
		return;
	ap_file_writer_submit(AP_FILE_WRITER_SLOT_STATE, AP_StateWriteJob, f->data, f->len);
	AP_TextBufCopy(&ap_state_text_queued, f);
}

// Real per-frame body. Wrapped by AP_OnFrame below so the always-on frame-stall
// watchdog (ap_perf.c) can bracket it without touching the body's own early
// returns (the adventure-mode / loading gate near the end). Keeps the public
// AP_OnFrame signature identical.
static void ap_onframe_body(struct GameTracker *gGT)
{
	// M-key: RESERVED for the future hub-map ZOOM (Warp-Pad State Model v2). The
	// old debug map-overlay colour toggle it used to drive is retired -- map
	// colours are now always on (AP_PadState). The zoom itself is deferred: it
	// needs to scale UI_Map_GetIconPos + UI_Map_DrawMap (ASM-verified vanilla,
	// shared by the hub map AND the track-select / in-race minimaps) around the
	// map centre, which is fragile shared-render work to be done + verified in
	// game, not inferred here. AP_MAP_OVERLAY_SCANCODE / Platform_InputRawKeyDown
	// are left declared as the input plumbing that rework will reuse. No handler
	// runs today, so M is an inert no-op.

	// Diagnostic breadcrumbs (crash investigation): a one-time run-start marker so
	// the appended log has a clear per-process boundary, plus a line on every
	// levelID (hub/track) transition. The log flushes per line, so if a crash
	// happens during a transition (e.g. an Autopelago item lands mid hub-load) the
	// tail shows exactly where. Logging only -- no gameplay effect.
	// Crash-reporter context: cache the plain ints the signal/SEH handlers are
	// allowed to read (walking game structures in a dying process is not).
#ifdef CTR_CUSTOM_PACKAGES
	AP_CrashNoteFrame(MainRaceTrack_IdentityLevelID(), ap_net_is_connected());
	AP_TrackIdentityLevel(gGT);
#else
	AP_CrashNoteFrame((int)gGT->levelID, ap_net_is_connected());
#endif
	// Park the #124 AP-logo marker in its model slot. Idempotent, and re-running
	// it every frame is what keeps the slot correct across hub/level loads and
	// savestate restores rather than depending on a single well-timed call.
	AP_MarkerModel_Register(gGT);
	// Park the #219 crystal in STATIC_CRYSTAL wherever the level left that slot
	// empty -- which includes the adventure hub, whose model pack does not carry a
	// crystal at all. Same idempotent every-frame contract as the marker above,
	// and for the same reason: modelPtr[] is refilled per level, so a single
	// well-timed call would not survive a hub swap or a savestate restore. The
	// harvest itself runs at most once, on the first frame with no load in flight.
	AP_RetailCrystal_Register(gGT);
	// Park the #222 Wumpa Fruit model in PU_WUMPA_FRUIT wherever the level left
	// that slot empty. Same idempotent every-frame contract as the crystal above,
	// for the same reason: modelPtr[] is refilled per level and LibraryOfModels_Clear
	// wipes slot 0x02, so a single well-timed call would not survive a hub swap or
	// a savestate restore. The compact harvest itself runs at most once, on the
	// first idle frame, and is sticky on failure.
	AP_RetailWumpa_Register(gGT);
	// The AP box's wood border and crate face: one read from the player's
	// disc on the first idle frame. Sticky either way.
	AP_BoxTexture_Prepare();
	// Box placement author mode (#182) and the additive model loader it draws
	// through (#109 / #124 groundwork). Author first, loader second, so a
	// placement dropped this frame gets its marker in the same frame instead of
	// one frame later. Both self-gate: the author mode on the "Box Author Mode"
	// option, the loader on having any spawn requests at all.
	AP_Author_OnFrame(gGT);
	// The #109 runtime boxes, between the two on purpose: author mode has already
	// decided whether it owns this level's markers (it stands the boxes down when
	// it does), and the loader has not run yet, so a box added this frame is born
	// this frame rather than next.
	AP_Boxes_OnFrame(gGT);
	AP_Spawn_OnFrame(gGT);
	// Seed completability verification: recomputes only when the AP state
	// generation moved (connect / received item / location check), so this is a
	// cheap comparison on every other frame. See ap_verify.c.
	// [AP PERF] VERIFY section: the recompute (when it does run) sweeps the seed,
	// so it is the other named slow-AP-frame suspect.
	AP_PerfSectionBegin(AP_PERF_SEC_VERIFY);
	AP_VerifyOnFrame();
	AP_PerfSectionEnd(AP_PERF_SEC_VERIFY);

	// Character phase: hub picker + hub swap (#54/#209, ruling R7). Placed ahead
	// of the adventure/loading gate lower down because the swap has to see the
	// frames where the hub comes BACK from its reload. Self-gates on the seed
	// actually carrying the character phase (or dev keys) and on the adventure
	// hub being open.
	//
	// Seating this slot's racer runs first, and only once per authoritative
	// answer: the racer is never an item, so if this does not apply it nothing
	// else will, and on a racer-locked seed starting as the wrong racer is the
	// difference between a solvable run and a stuck one. It takes gGT purely to
	// ask whether this is a hub frame, which is when a LATE restore (a stored
	// racer whose unlock only just arrived) is allowed to change the seated
	// racer; the first seat of a connection applies wherever we are.
	AP_CharSwap_SeatStartingCharacter(gGT);
	AP_CharSwap_Tick(gGT);

	{
		static int ap_booted = 0;
		static int ap_prev_level = -999;
		if (!ap_booted)
		{
			ap_booted = 1;
			AP_AppendLog("[AP BOOT] ===== client run start ===== (" CTR_AP_VERSION ")\n");
			AP_AppendLog("[AP BOOT] vendor: " CTR_AP_VENDOR_VERSIONS "\n");
			AP_AppendLog("[AP BOOT] problems? run support-bundle (.bat on Windows, "
			             ".sh on Linux/Deck) next to the game and share the archive\n");
			AP_PerfAnnounce(); // watchdog threshold + video adapter, once per run
			// #166: prime the gem-cup-leg identity/vanilla table from the live
			// data.advCupTrackIDs before any slot_data can possibly parse -- the
			// isolated ap_net/ap_seedcfg C++ lib has no access to `data`, so this
			// boot call is the one place the real vanilla table crosses into it.
			ctr_cfg_set_vanilla_cup_legs(data.advCupTrackIDs);
		}

#ifdef CTR_CUSTOM_TRACKS
		// Connect-time preflight and explicit Rescan own activation. Per-frame
		// work only enforces absence/required as authoritative in both directions.
		const char *ctServeFault = CustomTrack_ServeFaultReason();
		if (ctServeFault != NULL)
		{
			ap_custom_content_required = 1;
			ap_custom_content_gate_cached = 0;
			ap_custom_content_status.state = CTR_CT_MANAGER_HASH_MISMATCH;
			snprintf(ap_custom_content_status.detail,
			         sizeof ap_custom_content_status.detail, "%s", ctServeFault);
			ap_custom_content_scanned = 1;
		}
		if (!AP_CustomPadContentReady(ap_custom_content_seed_selected,
		                              ap_custom_content_required, ctServeFault))
		{
			CustomTrack_ClearSeedDescriptor();
		}
#else
		// This build has no custom-track support compiled in, but the seed may
		// still bind one. Say so once: the cup's legs are displaced in this
		// seed's logic, so racing its four retail legs here is the same
		// reachability desync an out-of-date client would produce, and the
		// player needs to know why the Gem is not coming.
		{
			static int apCtWarned = 0;
			if (!apCtWarned && ctr_cfg_active() && ctr_cfg.custom_tracks_ok)
			{
				apCtWarned = 1;
				AP_AppendLog("[AP CFG] *** this seed binds a custom track to a Gem Cup, and "
				             "this build cannot load custom tracks. That cup is not "
				             "completable here. ***\n");
			}
			static int apCvWarned = 0;
			if (!apCvWarned && ctr_cfg_active() && ctr_cfg.cortex_track.option)
			{
				apCvWarned = 1;
				AP_AppendLog("[AP CFG] *** this seed puts Cortex Vortex on a warp pad, and "
				             "this build cannot load custom tracks. That pad stays "
				             "closed here. ***\n");
			}
		}
#endif
		if ((int)gGT->levelID != ap_prev_level)
		{
			char m[96];
			// free= is MEMPACK headroom at the transition frame (0.1.4 level-40
			// freeze follow-up; pairs with the [AP POOL] jitpools line).
			snprintf(m, sizeof m, "[AP HUB] levelID %d -> %d (t=%u load=%d free=%d)\n",
			         ap_prev_level, (int)gGT->levelID, (unsigned)gGT->timer,
			         (int)sdata->Loading.stage, MEMPACK_GetFreeBytes());
			AP_AppendLog(m);
#ifdef CTR_CUSTOM_PACKAGES
			if (MainRaceTrack_OfflineCustomLoad())
			{
				char c[160];
				snprintf(c, sizeof c, "[AP HUB] level %d is custom track %d \"%.32s\" (slot %d loads its files only)\n",
				         (int)gGT->levelID, MainRaceTrack_IdentityLevelID(),
				         MainRaceTrack_OfflineCustomTitle() ? MainRaceTrack_OfflineCustomTitle() : "?",
				         (int)gGT->levelID);
				AP_AppendLog(c);
			}
#endif
			ap_prev_level = (int)gGT->levelID;

			// AP QoL one-lap cups (#7): restore the vanilla lap count when a
			// level transition lands in a HUB (GEM_STONE_VALLEY..CITADEL_CITY,
			// namespace_Level.h). The adventure cup entry (AH_WarpPad.c) sets
			// numLaps = 1 for the whole cup and nothing in adventure re-derives
			// it, so without this a post-cup trophy/boss/relic race would
			// inherit the 1-lap override. Keyed on the DESTINATION levelID, NOT
			// on !LOAD_IsOpen_RacingOrBattle(): the overlay is still closed for
			// a few frames while a cup LEG loads, so the on-a-track test read
			// "off-track" mid-load and stomped the override before the leg
			// started (batch smoke 2026-07-17, round 2: the cleared line fired
			// on the levelID 26 -> 6 leg transition and the leg raced 3 laps).
			// A transition's destination levelID cannot lie. Gated on the
			// option so the write never fires on seeds without it.
			if (ctr_cfg_active() && ctr_cfg.one_lap_cups &&
			    (int)gGT->levelID >= GEM_STONE_VALLEY &&
			    (int)gGT->levelID <= CITADEL_CITY && gGT->numLaps != 3)
			{
				gGT->numLaps = 3;
				AP_AppendLog("[AP CUP] one-lap override cleared (hub return)\n");
			}

			// Racer lock enforcement: a transition landing in a hub puts the
			// player's own racer back (AH_WarpPad's warp commit seated the
			// lock's racer for the destination). Same destination-levelID
			// timing as the one-lap restore above: the watcher fires early in
			// the hub load, before the driver MPK stage reads
			// characterIDs[0], so the restored racer's model is what loads.
			if ((int)gGT->levelID >= GEM_STONE_VALLEY &&
			    (int)gGT->levelID <= CITADEL_CITY)
			{
				AP_RacerLock_RestoreOnHub();
			}
			// Quit-to-title mid-enforced-race: drop rather than restore.
			// Without this the saved racer survived into the next adventure
			// and its first hub entry overrode the fresh garage pick
			// (review round 2 MINOR). One hub-bound path touches the title
			// level transiently -- SelectProfile's queued hub load -- and
			// the drop firing there is deliberate and harmless today
			// (nothing can be armed at the save menu), but a change that
			// lets the save menu open MID-enforcement would need this
			// revisited or the restore would be lost with the drop.
			else if ((int)gGT->levelID == MAIN_MENU_LEVEL)
			{
				AP_RacerLock_DropOnTitle();
			}
		}
	}

	// Race-placement listener: runs every frame in all modes so the finish
	// transition is never missed by the throttle below. Observation-only.
	AP_RaceListenerTick(gGT);

	// Network: connect once + pump every frame, in all game modes.
	AP_NetTick(gGT);
	// Room links (issue #334): claim handed-over requests, expire, notices.
	AP_LinkTick(gGT);

	// Trap framework: advance prime->fire->clear lifecycle + apply the FP camera,
	// and poll the Shortcutless debug keys. Runs every frame / all modes (the tick
	// gates its own race-only logic). Physics effects apply at their engine sites.
	AP_TrapTick(gGT);
	// Demo Camera: holds or force-clears the cinematic-camera
	// engagement. Shares AP_TrapTick's placement for the same reason -- it runs
	// before the camera PROC ticks this frame, so a clear lands while the camera
	// the snapshot describes is still the live one.
	AP_DemoCamTick(gGT);
	AP_WumpaTick(gGT); // Wumpa Fruit filler: drain banked fruit into drivers[0] in-race (#11)
	AP_UsefulTick(gGT); // H-dossier: apply queued shield, mask and invisibility
	AP_TiziTick(gGT);  // #223: expire a forced Mask whose item roll never resolved
	AP_TurboGrantTick(gGT); // #224: requeue a lost in-flight Turbo, then deliver
	                        // one pending grant into an empty in-race weapon slot
	AP_ShortcutKeys();
	AP_NavRec_Tick(gGT);      // AI lap recorder; inert unless nav_record is on
	AP_ShortcutSkipTick(gGT); // layer-2 checkpoint-% gap-skip detector (Shortcutless)
	AP_RelicTargetTick(gGT);  // issue #21: relic-race live target ladder (steps the
	                          // shown tier down when its time passes; race window only)
	AP_BlueFireTick(gGT);     // #12: presentation-only blue exhaust at the capstone

	// DeathLink (issue #6): send-edge detection, inbound-death queue firing, and
	// the no-loop guard. Runs every frame / all modes (it gates its own adventure-
	// only sending and race-window firing internally).
	AP_DeathLinkTick(gGT);

	// State snapshot (~every 60 frames) -- runs in ALL game modes so it's available
	// at the title screen right after connect. AP-side fields (options, received
	// items, checked locations, per-pad reqs) are valid once slot_data is parsed;
	// game_counters read live only in adventure mode (see "in_adventure" in JSON).
	// [AP PERF] STATE_DUMP section: the game-thread part of the dump -- building
	// the JSON in memory, comparing it with the last one and queueing it. The file
	// write itself runs on the background writer (ap_file_writer.h), so a slow disk
	// no longer shows up here. One frame in 60 pays for it.
	static int dumpTick = 0;
	if ((++dumpTick % 60) == 0)
	{
		AP_PerfSectionBegin(AP_PERF_SEC_STATE_DUMP);
		AP_DumpState(gGT);
		AP_PerfSectionEnd(AP_PERF_SEC_STATE_DUMP);
	}

	// Adventure-only item/goal/debug poll below.
	if ((gGT->gameMode1 & ADVENTURE_MODE) == 0 ||
	    sdata->Loading.stage != LOAD_IDLE)
	{
		ap_dbg_armed = 0;
		return;
	}

	// #244: a non-Oxide arm can complete the composed goal during a race or an
	// item drain. Wait for the ordinary idle-hub state before loading credits so
	// no race ceremony, door cutscene, pause menu or character picker owns the
	// transition. Oxide completion claims the same pending edge in game/222.c and
	// retains its vanilla ending cutscene instead of taking this direct path.
	if (ap_goal_presentation.creditsPending &&
	    gGT->levelID >= GEM_STONE_VALLEY && gGT->levelID <= CITADEL_CITY &&
	    LOAD_IsOpen_AdvHub() != 0 && gGT->drivers[0] != NULL &&
	    (gGT->gameMode1 & (PAUSE_1 | LOADING | GAME_CUTSCENE)) == 0 &&
	    (gGT->gameMode2 & (GAME_MODE2_VEH_FREEZE_MASK | SPAWN_AT_BOSS)) == 0 &&
	    sdata->AkuAkuHintState == 0 && sdata->pause_state == 0 &&
	    sdata->ptrActiveMenu == NULL)
	{
		if (AP_GoalPresentationClaim(&ap_goal_presentation))
		{
			AP_AppendLog("[AP GOAL] composed goal complete in hub -> credits\n");
			RaceFlag_SetDrawOrder(0);
			MainRaceTrack_RequestLoad(CREDITS_CRASH);
			return;
		}
	}

	// Throttle: act ~twice a second, not every frame.
	static int tick = 0;
	if ((++tick % 30) != 0)
		return;

	AP_ApplyItems(&sdata->advProgress); // grant received items into game state
	AP_PrelatchGreetingHint(&sdata->advProgress); // issue #17: kill the new-area greeting hint before it can collide with a hub swap
	AP_EvaluateGoal(); // item-based goals (all-bosses / all-gem-cups / 101%) fire here
	AP_PollDebug(&sdata->advProgress);
}

void AP_OnFrame(struct GameTracker *gGT)
{
	// Always-on frame-stall watchdog (ap_perf.c). Bracketing the body here means
	// its early returns (the adventure-mode / loading gate) need no touching:
	// AP_PerfFrameEnd always runs. levelID + the load stage feed the "stall
	// outside AP" attribution -- a level load legitimately blocks for seconds, so
	// the watchdog must know when we are loading and not report it as a stall.
	// gGT->timer feeds the t= stamp, the same clock the [AP ITEM] / [AP HUB] /
	// [AP RACE] lines stamp, so a stall line can be placed against them in the log.
#ifdef CTR_CUSTOM_PACKAGES
	AP_PerfFrameBegin(MainRaceTrack_IdentityLevelID(), (int)sdata->Loading.stage,
	                  (unsigned)gGT->timer);
#else
	AP_PerfFrameBegin((int)gGT->levelID, (int)sdata->Loading.stage,
	                  (unsigned)gGT->timer);
#endif
	ap_onframe_body(gGT);
	// Discord Rich Presence (#366): outside the body so its early returns do
	// not skip it. Returns at once unless the option was ever switched on.
	AP_Discord_OnFrame(gGT);
	AP_PerfFrameEnd();
}

#endif // CTR_AP
