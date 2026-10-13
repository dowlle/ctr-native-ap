// ap_hooks_net.c -- part of the ap_hooks unit (issue #305).
// Network glue: AP_NetTick (connect, item drain, slot_data activation),
// reconnect, shutdown, the connection status line and the AI-difficulty preset.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

static void AP_NetTick(struct GameTracker *gGT)
{
	if (!ap_net_started)
	{
		char uri[128] = "";
		char slot[64] = "";
		char pass[64] = "";
		char msg[256];
		int haveLegacy = 0;
		int haveIni = (g_config.uri[0] != '\0');

		// Always parse ap-config.txt when it exists: besides uri/slot/password it
		// primes the trap / shortcut / skip_hints test hooks (see AP_ReadConfig). If
		// present it seeds the legacy connection defaults, then its own keys override.
		{
			FILE *f = fopen("ap-config.txt", "r");
			if (f)
			{
				fclose(f);
				snprintf(uri, sizeof uri, "ws://localhost:38281");
				snprintf(slot, sizeof slot, "CtrSmokeTest");
				AP_ReadConfig(uri, sizeof uri, slot, sizeof slot, pass, sizeof pass);
				haveLegacy = 1;
			}
		}

		// config.ini [Connection] wins over ap-config.txt when set (same precedence
		// as AP_SkipHints / AP_MapFlashOn).
		if (haveIni)
		{
			snprintf(uri, sizeof uri, "%s", g_config.uri);
			snprintf(slot, sizeof slot, "%s", g_config.slot);
			snprintf(pass, sizeof pass, "%s", g_config.password);
		}

		// Approved startup change: with NEITHER a config.ini [Connection] uri NOR an
		// ap-config.txt present, do NOT auto-dial the localhost default. Skip the dial
		// and surface a hint (log + the menu's status line show "not connected"); the
		// player sets up the room in OPTIONS > Connection and hits Connect. With any
		// config present, startup auto-connect behaves exactly as before.
		// A room link from this launch, or one another launch handed over,
		// is admitted before the first dial (issue #334): with no session yet
		// there is nothing to confirm a switch away from, and dialing the saved
		// room first would only be torn down again.
		if (AP_LinkBootAdmit())
		{
			// dialed by the link glue
		}
		else if (haveIni || haveLegacy)
		{
			snprintf(msg, sizeof msg, "[AP NET] init uri=%s slot=%s\n", uri, slot);
			AP_AppendLog(msg);
			if (ap_net_init("ctr-native", "Crash Team Racing", uri) == 0)
				ap_net_connect_slot(slot, pass);
			AP_LinkNoteDial(uri, slot);
		}
		else
		{
			AP_AppendLog("[AP NET] not connected - set up your room in "
			             "OPTIONS > Connection (no config.ini [Connection], no ap-config.txt)\n");
		}
		ap_net_started = 1; // attempt once
	}

	// [AP PERF] POLL section: apclientpp's poll() fires every queued network
	// handler inline on this thread (item batches, and the connect-time
	// datapackage sync), so a slow poll is a prime slow-AP-frame suspect.
	AP_PerfSectionBegin(AP_PERF_SEC_POLL);
	ap_net_poll();
	AP_PerfSectionEnd(AP_PERF_SEC_POLL);

	// On a fresh slot-connect (new seed, reconnect, or server switch) the server
	// resends the FULL ReceivedItems list from index 0. Zero the per-session
	// tallies + goal/notify state FIRST so everything rebuilds from the
	// authoritative list, instead of accumulating on top of the previous
	// connection (which double-counted on reconnect and carried a prior seed's
	// items across a server switch). AP_ApplyItems reconciles AdvProgress bits to
	// ap_recv_count each frame, so zeroing here clears the stale in-game counts.
	// Foundation for a future in-game connection manager (clean per-connect reset).
	if (ap_net_take_recv_reset())
	{
		int k;
		AP_FxConnectReset(); // #299: effect markers pending until the room answers
		for (k = 0; k < AP_ITEM_INDEX_COUNT; k++)
		{
			ap_recv_count[k] = 0;
			ap_recv_count_foreign[k] = 0; // #85: rebuilds from the resent ReceivedItems list
		}
		for (k = 0; k < AP_VERIFY_ITEM_INDEX_COUNT; k++)
			ap_verify_recv_foreign[k] = 0;
		AP_WinLogicTallyReset(); // #449: rebuilt from the resent ReceivedItems list
		memset(ap_letter_received, 0, sizeof ap_letter_received);
		memset(ap_custom_letter_received, 0, sizeof ap_custom_letter_received);
		memset(ap_cortex_letter_received, 0, sizeof ap_cortex_letter_received);
		memset(ap_verify_custom_letter_foreign, 0, sizeof ap_verify_custom_letter_foreign);
		for (k = 0; k < AP_CAT_COUNT; k++)
			ap_item_count[k] = 0;
		for (k = 0; k < AP_ITEMSANITY_WEAPON_COUNT; k++)
			ap_itemsanity_owned[k] = 0;
		AP_SurfaceReset(); // rebuilt by the authoritative ReceivedItems replay below
		// Capability chains (#12/#13): same discipline. These are COUNTS, not
		// one-shot effects, so the resent full list must rebuild them from zero --
		// accumulating on top of the previous connection would hand the player a
		// doubled boost tier on every reconnect.
		AP_CapabilityReset();
		// Tizi Helper (#223): both receipts are booleans rebuilt from the same
		// authoritative replay, so a reconnect or a slot switch can never carry
		// one slot's helper into another's session.
		AP_TiziReset();
		// Turbo Grant (#224): zero the session counters and reload this seed and
		// slot's FIRED count (the room's AP_NET_FX_TURBO_FIRED key, #299).
		// Deliberately not the effect-marker
		// mechanism above -- that marks an item consumed when its batch drains,
		// which would swallow a grant the player could not receive yet. See
		// ap_turbogrant_logic.h.
		AP_TurboGrantReset();
		AP_WumpaConnectReset();
		AP_UsefulConnectReset();
		AP_HitEncounterConnectReset(); // ticket 06: re-arm the Hit-check session mask
		for (k = 0; k < 6; k++)
			ap_notified_mask[k] = 0;
		ap_oxide_first_beaten = 0;
		ap_oxide_final_beaten = 0;
		ap_goal_sent = 0;
		AP_GoalPresentationReset(&ap_goal_presentation);
		ap_state_gen++; // fresh connect: slot_data (re)activates -> pad states may all shift
		AP_FeedConnectReset(); // drop stale toasts + re-arm initial-inventory absorb
		// Same discipline, same call site (2026-08-11, Bandi-slot session): the
		// trap registry is per-session state, and a primed-but-unfired trap that
		// survived a reconnect fired on a DIFFERENT slot -- one the server had
		// never sent a trap to. See AP_Trap_ConnectReset in ap_traps.h.
		AP_Trap_ConnectReset();
		AP_DemoCam_ConnectReset(); // same rule: a camera snapshot cannot outlive its session
		// Re-arm the once-per-connect [AP CHECK DIAG] lines: a new connect means a
		// (possibly) new seed config, so each absent-rung / mismatch shape may log
		// one fresh line. See ap_checkdiag_once.h for the spam this prevents.
		AP_CheckDiagOnceReset(&ap_checkdiag_once);
#ifdef CTR_CUSTOM_TRACKS
		AP_CustomTrophyCeremonyReset(&ap_custom_trophy_ceremony);
		ap_custom_ceremony_bit = AP_CUSTOM_TROPHY_PSEUDO_BIT;
#endif
		AP_AppendLog("[AP NET] fresh connect -> reset received-item tally + session state\n");
#ifdef CTR_CUSTOM_TRACKS
		// slot_data was parsed by the callback inside ap_net_poll above. Refuse
		// gameplay immediately if this seed selects content that is not Ready;
		// the Options Rescan action can satisfy and unlock this same seed later.
		AP_CustomContentPreflightSeed(1);
		AP_CustomContentLogStatus("seed preflight");
		AP_OxideFinalContentPreflight();
#endif

		// AI-difficulty option sync: subscribe to (and fetch) the per-slot override,
		// seeded by this seed's slot_data default. ap_diff_pulled re-arms the one-shot
		// pull below so a reconnect / server switch re-reads the authoritative value.
		ap_net_difficulty_subscribe(ctr_cfg.ai_difficulty_default);
		ap_diff_pulled = 0;

		// Current racer (#54/#209): same per-slot data-storage path, seeded by
		// this seed's own starting racer so a first-ever connect is correct
		// before the Get round-trips. Only for a seed that actually carries the
		// character phase -- writing a racer key for a pre-0.2.0 seed would
		// leave state behind for a feature that seed does not have. The seat is
		// re-armed so a reconnect re-applies the authoritative value rather than
		// keeping whatever the local save happened to hold.
		//
		// The RESET runs unconditionally, outside that gate. It is what drops the
		// previous slot's racer AND its editable-stat deltas, and a switch from a
		// character-phase seed to a pre-0.2.0 one is exactly a case where nothing
		// later would ever overwrite them. It also runs here, synchronously in the
		// connect handler, so the tables are already zero before any asynchronous
		// Retrieved reply for the new slot can land.
		AP_CharSwap_ConnectReset();
		if (ctr_cfg.character_phase_present)
		{
			ap_net_character_subscribe(ctr_cfg.starting_character);
			// Editable stat package: subscribed unconditionally alongside the
			// racer, because whether it is APPLIED is a separate decision made
			// at restore time (only when the editor owns this seed's stats).
			// Fetching it always keeps the two keys' lifecycles identical.
			ap_net_editstats_subscribe();
		}

		// DeathLink (issue #6): reset per-session send/receive state and, when this
		// seed opted in, enable the DeathLink connection tag. slot_data was parsed in
		// the slot-connected handler that ran inside ap_net_poll() above, so
		// ctr_cfg.death_link is authoritative here.
		AP_DeathLinkConnectReset();
	}

	// One-shot connect pull: as soon as a difficulty value is known from the server
	// (the slot_data default seed, or the Get reply that overrides it), mirror it
	// into the local config so the menu slider and the effect read one source. Done
	// exactly once per connect so it never clobbers a live in-menu edit mid-session
	// (cross-device changes land on the next reconnect, not live -- v1 limitation).
	if (!ap_diff_pulled)
	{
		int dv;
		if (ap_net_difficulty_known(&dv))
		{
			g_config.aiDifficulty = dv;
			ap_diff_pulled = 1;
		}
	}

	// Podium backstop (connect-time): reconcile rungs for any trophy race whose
	// win was already recorded (a prior session, or a crash that lost the live
	// capture). Winning == 1st == all rungs, so the durable trophy-checked signal
	// is sufficient. Deduped + cheap (16 tracks) so it is safe to run every tick;
	// this is what unblocks an existing stuck save on reconnect WITHOUT re-racing.
	AP_ReconcilePodiumFromTrophies();

	// [AP PERF] ITEM_APPLY section: the received-item drain runs AFTER the POLL
	// section closes, so before this bracket existed an item burst (or the
	// connect-time initial-inventory dump) was invisible -- it landed in "rest"
	// with poll=0. Covers the drain, the per-item bookkeeping, and the feed hand-off.
	AP_PerfSectionBegin(AP_PERF_SEC_ITEM_APPLY);

	// Received items: tally by category. Applied to AdvProgress bits in
	// AP_ApplyItems() (adventure + save-safe only). The tally is zeroed on each
	// fresh connect (above), so the resent full list rebuilds counts exactly.
	// #299: resolve the room's effect markers (releasing held effects) before
	// this frame's items are offered against them.
	AP_FxStorageTick();
	long long items[32];
	int n = ap_net_drain_items(items, 32);
	AP_PerfNoteItemsReceived(n, n > 0 ? items[n - 1] : 0); // frame stall line context
	int liveItemBatch = ap_feed_primed;
	int i;
	char st[64];
	AP_FmtState(gGT, st, sizeof st); // game-state breadcrumb for this drain (crash diag)
	for (i = 0; i < n; i++)
	{
		// Hub item feed: queue a toast for this receipt (sender, server index and
		// AP classification flags from the aligned drain batch). Display-only;
		// self-suppresses off-hub / unprimed.
		AP_FeedOnItemReceived(items[i], ap_net_recv_batch_player(i),
		                      ap_net_recv_batch_index(i), ap_net_recv_batch_flags(i));

		// #449: received count by raw AP item id (progression-flagged copies
		// only, start inventory at location -2 skipped: the win_logic block's
		// `start` table counts it) for the race-loss stakes evaluator
		// (ap_win_logic.h). Read nowhere else; the gate counters below are unchanged.
		AP_WinLogicTallyItem(items[i], ap_net_recv_batch_location(i),
		                     ap_net_recv_batch_flags(i));

		// Authoritative gate counter: tally by raw item TYPE index 0..14.
		long long idx = items[i] - AP_ITEM_BASE;
		// Sparse custom item addresses cannot index the legacy retail tally.
		// Authoritative replay rebuilds this slot-owned set after reset above.
		int customLetterSlot, customLetter;
		if (AP_CustomLetterItemToIdentityPure(items[i], &customLetterSlot, &customLetter))
		{
			ap_custom_letter_received[customLetterSlot - 1][customLetter] = 1;
			if (ap_net_recv_batch_player(i) != ap_net_self_slot() || ap_net_recv_batch_location(i) <= 0)
				ap_verify_custom_letter_foreign[(customLetterSlot - 1) * 3 + customLetter] = 1;
			ap_state_gen++;
		}
		// Schema 15: Cortex Vortex letter items (idx 200..202). Booleans rebuilt
		// by the authoritative replay after the connect reset.
		{
			int cvLetter = AP_CortexLetterItemIndexPure(idx);
			if (cvLetter >= 0)
			{
				ap_cortex_letter_received[cvLetter] = 1;
				ap_state_gen++;
			}
		}
		// Full verifier inventory uses the same own-vs-foreign classifier as the
		// legacy gate split. Own-world location rewards are reconstructed from
		// scouts at the exact simulated collection edge; starting inventory,
		// server grants and other players' items seed the fixed point here.
		if (idx >= 0 && idx < AP_VERIFY_ITEM_INDEX_COUNT)
		{
			int       verifyPl  = ap_net_recv_batch_player(i);
			long long verifyLoc = ap_net_recv_batch_location(i);
			if (verifyPl != ap_net_self_slot() || verifyLoc <= 0)
				ap_verify_recv_foreign[idx]++;
			// The 0.2.0 verifier reads capability, weapon, character and letter
			// inventory beyond the legacy 0..14 gate table. Receipt of any such
			// item must invalidate its fixed point just like a Trophy or Key does.
			if ((idx >= AP_CAPABILITY_ITEM_FIRST_INDEX &&
			     idx < AP_ITEMSANITY_ITEM_FIRST_INDEX + AP_ITEMSANITY_WEAPON_COUNT) ||
			    (idx >= AP_CHARACTER_ITEM_FIRST_INDEX && idx <= 186) ||
			    AP_LetterItemIndexIsLetterPure((int)idx))
				ap_state_gen++;
		}
		if (idx >= 0 && idx < AP_ITEM_INDEX_COUNT)
		{
			ap_recv_count[idx]++;
			// Split tally for the seed verifier (issue #85): count this receipt as
			// FOREIGN unless it is an own-world location check (which the verifier
			// banks from the scout cache synchronously with checked-state instead).
			// Defensive classifier (risk 1): sender != self OR location <= 0 -> foreign.
			// Starting inventory / server grants carry no scout entry, so counting them
			// foreign here keeps them from being dropped from BOTH tallies regardless of
			// how the server reports the sender slot.
			int       pl  = ap_net_recv_batch_player(i);
			long long loc = ap_net_recv_batch_location(i);
			if (pl != ap_net_self_slot() || loc <= 0)
				ap_recv_count_foreign[idx]++;
			ap_state_gen++; // a gate-relevant count changed -> pad states may shift
			if (liveItemBatch && idx >= AP_IDX_GEM_RED &&
			    idx < AP_IDX_GEM_RED + 5)
				AP_GoalArmLiveEvent();
		}

		// Trap items -> arm the matching trap effect. The 20 trap identities do NOT
		// form one contiguous index window: they sit at 16..20, 106..116 and
		// 190..193, with weapon unlocks, the Wumpa family, characters and letters in
		// between, so the mapping is a table (ap_trap_items.h) and not arithmetic off
		// a first index. An identity whose native effect is still a wave 2 scaffold
		// is armed, logged once and retained; AP_TrapReceive never consumes what this
		// build cannot perform. Traps are not gate items, so they never touch
		// ap_recv_count above. A pre-trap native ignores these ids by construction
		// (idx fails the count guard and AP_ItemCategory -> AP_CAT_NONE -> logged
		// filler/unmapped, below).
		else if (AP_TrapItemIndexIsTrap(idx))
		{
			AP_FxOffer(items[i], ap_net_recv_batch_index(i));
		}

		// Permanent comfort items (idx 21..25): remember presence for the natural
		// terrain metadata hook. They are booleans, so duplicates are harmless.
		else if (idx >= AP_SURFACE_ITEM_FIRST_INDEX &&
		         idx < AP_SURFACE_ITEM_FIRST_INDEX + AP_SURFACE_ITEM_COUNT)
		{
			AP_SurfaceReceive((int)(idx - AP_SURFACE_ITEM_FIRST_INDEX));
		}

		// Shared-global capability chains (idx 27..30): Progressive Boost, Top
		// Speed, Acceleration, Turning. Unlike the surface booleans these are
		// COUNTS -- the count IS the tier -- so every duplicate legitimately
		// increments. Idempotence comes from AP_CapabilityReset zeroing them on
		// each fresh connect before the resent list replays. Not gate items, so
		// they never touch ap_recv_count; nothing in logic reads a tier yet
		// (spine 1 shipped them `useful` for exactly that reason).
		else if (idx >= AP_CAPABILITY_ITEM_FIRST_INDEX &&
		         idx < AP_CAPABILITY_ITEM_FIRST_INDEX + AP_CAPABILITY_ITEM_COUNT)
		{
			AP_CapabilityReceive((int)(idx - AP_CAPABILITY_ITEM_FIRST_INDEX));
		}

		// Per-character capability block (idx 31..94): the same four chains again,
		// once per racer, character-major in the apworld's roster order. Counts
		// like the shared-global block above and rebuilds the same way; which
		// racer's row the kart actually expresses is decided at read time by the
		// character being driven, not here. No generation on apworld main creates
		// these yet (per_character raises OptionError pending
		// dowlle/ctr-native-ap#71), so today they only arrive from a hand-built
		// seed -- the drain path is ready for when #71 lands.
		else if (idx >= AP_CAPABILITY_PC_ITEM_FIRST_INDEX &&
		         idx < AP_CAPABILITY_PC_ITEM_FIRST_INDEX + AP_CAPABILITY_PC_ITEM_COUNT)
		{
			AP_CapabilityReceivePerCharacter((int)(idx - AP_CAPABILITY_PC_ITEM_FIRST_INDEX));
		}
		else if (AP_LetterItemIndexIsLetterPure((int)idx))
		{
			int level, letter;
			if (AP_LetterItemIndexToIdentityPure((int)idx, &level, &letter))
				ap_letter_received[level][letter] = 1;
		}

		// Itemsanity weapon unlocks (indexes 95..105): booleans by weapon type.
		// Duplicate receipts are harmless; reconnect/slot reset clears the set
		// before the authoritative ReceivedItems replay rebuilds it.
		else if (idx >= AP_ITEMSANITY_ITEM_FIRST_INDEX &&
		         idx < AP_ITEMSANITY_ITEM_FIRST_INDEX + AP_ITEMSANITY_WEAPON_COUNT)
		{
			ap_itemsanity_owned[idx - AP_ITEMSANITY_ITEM_FIRST_INDEX] = 1;
			if (idx == AP_TIZI_MASK_ITEM_INDEX)
				AP_TiziReceiveMask();
			// #224's ruled gate needs the same signal for the `Turbo` weapon
			// (the first itemsanity item) that #223 needs for the Mask.
			if (idx == AP_TURBOGRANT_TURBO_ITEM_INDEX)
				AP_TurboGrantReceiveTurboWeapon();
		}
		else if (idx >= AP_USEFUL_ITEM_FIRST_INDEX &&
		         idx < AP_USEFUL_ITEM_FIRST_INDEX + AP_USEFUL_ITEM_COUNT)
		{
			AP_UsefulReceive((int)(idx - AP_USEFUL_ITEM_FIRST_INDEX));
		}

		// Character unlocks (idx 123..138, issues #54/#209): one item per racer,
		// same roster order as the capability blocks above. Receiving one makes
		// that racer selectable in the hub picker and, on a racer-locked seed,
		// opens any pad that demands them. Idempotent on a reconnect replay.
		else if (idx >= AP_CHARACTER_ITEM_FIRST_INDEX &&
		         idx < AP_CHARACTER_ITEM_FIRST_INDEX + AP_CHARACTER_ITEM_COUNT)
		{
			AP_CharacterReceive((int)(idx - AP_CHARACTER_ITEM_FIRST_INDEX));
		}

		// Tizi Helper (#223, idx 188) and the itemsanity `Mask` weapon it needs
		// when itemsanity is on (idx 101). Two booleans; the ruled AND lives in
		// ap_tizi_logic.h. The Mask index is claimed here as well as by the #145
		// itemsanity drain when that lands -- both are idempotent sets of
		// independent flags, so the merge-up is a dedupe, not a conflict.
		else if (idx == AP_TIZI_ITEM_INDEX)
		{
			AP_TiziReceiveHelper();
		}

		// Turbo Grant (#224, idx 189). A COUNT, not a boolean: every receipt is
		// one more Turbo the player is owed, and duplicates are legitimate. It is
		// rebuilt from zero on each fresh connect (AP_TurboGrantReset above), so
		// the server's full replay reconstructs exactly the same total -- which
		// is why this must NOT be filtered through the effect marker the way the
		// trap and Wumpa arms are. A grant that has not been delivered yet is
		// still owed, whatever index it arrived at.
		else if (idx == AP_TURBOGRANT_ITEM_INDEX)
		{
			AP_TurboGrantReceive();
		}
		else if (idx == AP_WUMPA_SMALL_BUNDLE_ITEM_INDEX ||
		         idx == AP_WUMPA_BIG_BUNDLE_ITEM_INDEX)
		{
			AP_FxOffer(items[i], ap_net_recv_batch_index(i));
		}
		else if (idx == AP_WUMPA_PROGRESSIVE_ITEM_INDEX)
		{
			AP_WumpaReceiveStarting();
		}
		// Wumpa Fruit filler (idx 15) -> bank one fruit; AP_WumpaTick hands it to the
		// local player in-race (issue #11). Not a gate item, so it never touches
		// ap_recv_count. Cosmetic/QoL only; still logged as filler/unmapped below.
		else if (AP_ItemCategory(items[i]) == AP_CAT_WUMPA)
		{
			AP_FxOffer(items[i], ap_net_recv_batch_index(i));
		}

		// Coarse category tally: kept only to drive the cosmetic AdvProgress
		// bits (progress %, podium) in AP_ApplyItems -- gates ignore these.
		AP_ItemCat c = AP_ItemCategory(items[i]);
		char msg[160];
		if (c >= 0 && c < AP_CAT_COUNT)
		{
			ap_item_count[c]++;
			snprintf(msg, sizeof msg, "[AP ITEM] received %s (id %lld) -> have %d | %s\n",
			         AP_CATEGORY_POOLS[c].name, items[i], ap_item_count[c], st);
		}
		// The branches below are log-text only (no bit pool, so nothing above
		// tallies them): AP_ItemCategory answers AP_CAT_CRYSTAL or AP_CAT_NONE for
		// all of them, which used to fall straight into the generic "filler/unmapped"
		// line below even though the item was fully understood and handled above.
		else if (idx >= AP_CAPABILITY_ITEM_FIRST_INDEX &&
		         idx < AP_CAPABILITY_ITEM_FIRST_INDEX + AP_CAPABILITY_ITEM_COUNT)
		{
			int chain = (int)(idx - AP_CAPABILITY_ITEM_FIRST_INDEX);
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received capability item (id %lld) (capability: %s, shared) | %s\n",
			         items[i], AP_LOG_CAP_CHAIN_NAME[chain], st);
		}
		else if (idx >= AP_CAPABILITY_PC_ITEM_FIRST_INDEX &&
		         idx < AP_CAPABILITY_PC_ITEM_FIRST_INDEX + AP_CAPABILITY_PC_ITEM_COUNT)
		{
			int blockIndex = (int)(idx - AP_CAPABILITY_PC_ITEM_FIRST_INDEX);
			int slot  = blockIndex / AP_CAP_CHAIN_COUNT;
			int chain = blockIndex % AP_CAP_CHAIN_COUNT;
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received capability item (id %lld) (capability: %s, roster slot %d) | %s\n",
			         items[i], AP_LOG_CAP_CHAIN_NAME[chain], slot, st);
		}
		else if (idx >= AP_SURFACE_ITEM_FIRST_INDEX &&
		         idx < AP_SURFACE_ITEM_FIRST_INDEX + AP_SURFACE_ITEM_COUNT)
		{
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received surface item (id %lld) (surface: %s) | %s\n",
			         items[i], AP_LOG_SURFACE_ITEM_NAME[idx - AP_SURFACE_ITEM_FIRST_INDEX], st);
		}
		else if (idx >= AP_ITEMSANITY_ITEM_FIRST_INDEX &&
		         idx < AP_ITEMSANITY_ITEM_FIRST_INDEX + AP_ITEMSANITY_WEAPON_COUNT)
		{
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received itemsanity weapon item (id %lld) (weapon: %s) | %s\n",
			         items[i], AP_LOG_ITEMSANITY_WEAPON_NAME[idx - AP_ITEMSANITY_ITEM_FIRST_INDEX], st);
		}
		else if (idx >= AP_CHARACTER_ITEM_FIRST_INDEX &&
		         idx < AP_CHARACTER_ITEM_FIRST_INDEX + AP_CHARACTER_ITEM_COUNT)
		{
			int rosterSlot = (int)(idx - AP_CHARACTER_ITEM_FIRST_INDEX);
			int characterID = AP_CapabilityRosterCharacter(rosterSlot);
			const char *name = (characterID >= 0 && data.MetaDataCharacters[characterID].name_Debug)
			                        ? data.MetaDataCharacters[characterID].name_Debug
			                        : "?";
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received character unlock item (id %lld) (character: %s, roster slot %d) | %s\n",
			         items[i], name, rosterSlot, st);
		}
		else if (AP_LetterItemIndexIsLetterPure((int)idx))
		{
			int level, letterInTrack;
			AP_LetterItemIndexToIdentityPure((int)idx, &level, &letterInTrack);
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received letter item (id %lld) (letter %d of %d, track %s) | %s\n",
			         items[i], letterInTrack + 1, CTR_CFG_LETTER_COUNT,
			         level >= 0 ? AP_AuthorLevelName(level) : "?", st);
		}
		else if (AP_CortexLetterItemIndexPure(idx) >= 0)
		{
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received letter item (id %lld) (letter %d of %d, track Cortex Vortex) | %s\n",
			         items[i], AP_CortexLetterItemIndexPure(idx) + 1, CTR_CFG_LETTER_COUNT, st);
		}
		else if (idx == AP_TIZI_ITEM_INDEX)
		{
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received Tizi Helper item (id %lld) | %s\n", items[i], st);
		}
		else if (idx == AP_TURBOGRANT_ITEM_INDEX)
		{
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received Turbo Grant item (id %lld) | %s\n", items[i], st);
		}
		else if (AP_TrapItemIndexIsTrap(idx))
		{
			int trapEffect = AP_TrapEffectForItemIndex(idx);
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received trap item (id %lld) (trap: %s) | %s\n",
			         items[i], AP_TRAP_DESC[trapEffect].name, st);
		}
		else
		{
			snprintf(msg, sizeof msg,
			         "[AP ITEM] received item id %lld (filler/unmapped) | %s\n", items[i], st);
		}
		AP_AppendLog(msg);
	}
	// Replay dedup high-water: after the batch, remember the highest server index
	// drained (effects for anything at or below it must never run again) and
	// write it to the room when it moved. Uses the OVERALL max, not just effect
	// items: any future live item always has a higher index than everything
	// already seen. While the marker is pending this only records the high
	// water; the held effects are released against the room's value (#299).
	if (n > 0)
	{
		long long batchMax = -1;
		for (i = 0; i < n; i++)
		{
			long long srvIdx = ap_net_recv_batch_index(i);
			if (srvIdx > batchMax)
				batchMax = srvIdx;
		}
		AP_FxBatchEnd(batchMax);
	}

	AP_FeedEndDrain(n); // hub feed: prime once the initial inventory dump goes quiet

	AP_PerfSectionEnd(AP_PERF_SEC_ITEM_APPLY);
}

// In-game connection manager: tear down the current client and re-dial. The menu
// is reachable only from the main menu, so there is no in-flight item or live
// adventure state to strand -- AP_NetTick keeps polling, and the fresh slot-connect
// resets the per-session tallies through ap_net_take_recv_reset() as usual.
void AP_Net_Reconnect(const char *uri, const char *slot, const char *password)
{
	char msg[256];
	snprintf(msg, sizeof msg, "[AP NET] reconnect uri=%s slot=%s\n",
	         uri ? uri : "", slot ? slot : "");
	AP_AppendLog(msg);

	ap_net_shutdown();
	if (ap_net_init("ctr-native", "Crash Team Racing", uri) == 0)
		ap_net_connect_slot(slot, password);
	ap_net_started = 1; // suppress the boot-time auto-dial from re-running
	AP_LinkNoteDial(uri, slot); // a later room link for this session is not a switch
}

// Quit-from-main-menu (#211): close the live socket before the process exits,
// same call AP_Net_Reconnect makes ahead of a re-dial, just with no re-dial
// after it.
void AP_Net_Shutdown(void)
{
	ap_net_shutdown();
}

// Longest host the status row can render before the line runs off the panel
// (FONT_SMALL is 13 px per character), leaving room for the fixed wording around
// it. A longer host is cut and marked with "..".
#define AP_STATUS_HOST_MAX 16

// One-line status for the menu's read-only status row.
const char *AP_Net_StatusLine(void)
{
	static char line[128];
	switch (ap_net_status())
	{
	case AP_NET_STATUS_CONNECTED:
		snprintf(line, sizeof line, "Connected");
		break;
	case AP_NET_STATUS_CONNECTING:
		snprintf(line, sizeof line, "Connecting...");
		break;
	case AP_NET_STATUS_UNREACHABLE:
	{
		// Post-connect drop: names what could not be reached so a dead address,
		// a wrong port or a server that is not up yet reads differently from a
		// handshake still in progress (issue #146). The client is still retrying
		// either way. The uri row above this one carries the full address, so a
		// trimmed host here costs the player nothing.
		char host[64];
		if (ap_net_host(host, sizeof host))
		{
			if ((int)strlen(host) > AP_STATUS_HOST_MAX)
				snprintf(host + AP_STATUS_HOST_MAX - 2, 3, "..");
			snprintf(line, sizeof line, "Cannot reach %s, retrying", host);
		}
		else
			snprintf(line, sizeof line, "Cannot reach server, retrying");
		break;
	}
	case AP_NET_STATUS_RETRY_STOPPED:
		// Pre-connect budget exhausted: automatic attempts have stopped and the
		// only way forward is the Connect row below. Deliberately does not say
		// "Cannot reach", "Not connected" or anything "retrying" -- the state is
		// a stopped one, and the uri row above still shows the target address.
		snprintf(line, sizeof line, "Auto-retry stopped, press Connect");
		break;
	case AP_NET_STATUS_ERROR:
	{
		const char *e = ap_net_last_error();
		if (CTR_MenuErrorIsInvalidSlot(e))
			snprintf(line, sizeof line, "%s", CTR_MenuInvalidSlotMessage());
		else if (e && *e)
			snprintf(line, sizeof line, "Error: %s", e);
		else
			snprintf(line, sizeof line, "Connection error");
		break;
	}
	default:
		snprintf(line, sizeof line, "Not connected");
		break;
	}
	return line;
}

// ── AI-difficulty preset ──
// The local config value is the single source of truth for both the menu row and
// the effect; the connect pull mirrors any server override into it. It holds the
// raw engine difficulty VALUE (0 = vanilla; presets 0x50/0xA0/0xF0/0x140/0x280).
// An out-of-ladder value from slot_data is used as-is (only display snaps to the
// nearest preset). Returns it clamped non-negative.
int AP_AiDifficultyValue(void)
{
	int v = g_config.aiDifficulty;
	if (v < 0)
		v = 0;
	return v;
}

void AP_AiDifficultyCommit(void)
{
	// No-op when not connected (ap_net_difficulty_set guards on state). When
	// connected this persists the local value to the per-slot data-storage key.
	ap_net_difficulty_set(AP_AiDifficultyValue());
}

#endif // CTR_AP
