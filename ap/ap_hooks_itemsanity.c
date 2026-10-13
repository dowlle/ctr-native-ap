// ap_hooks_itemsanity.c -- part of the ap_hooks unit (issue #305).
// Itemsanity: weapon availability and use, roulette and crystal filters, the
// Item Reroll trap filter and boss catch-up.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

static int AP_ItemsanityActive(void)
{
	// Server location-set membership is the authoritative all-or-none toggle.
	// This also makes absent/old slot_data inert without a bespoke parser path.
	return ctr_cfg_active() && ap_net_location_exists(35016000L);
}

int AP_ItemsanityPadOpportunity(int destLevelID)
{
	unsigned char useLeft[AP_ITEMSANITY_WEAPON_COUNT];
	int i;

	// Retail race tracks only: each has item crates and a plain re-race route,
	// and any of them can serve a pending use check. Cups, trials, arenas,
	// boss garages and custom destinations keep their own lifecycle.
	if (destLevelID < 0 || destLevelID > 15 || !AP_ItemsanityActive())
		return 0;

	for (i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
	{
		int heldItemID = i <= 4 ? i : i + 1; // inverse of AP_ItemsanityWeaponIndex
		int juiced;

		useLeft[i] = 0;
		for (juiced = 0; juiced <= 1; juiced++)
		{
			long code = AP_ItemsanityLocationCode(heldItemID, juiced);
			if (code > 0 && ap_net_location_exists(code) && !ap_net_location_checked(code))
				useLeft[i] = 1;
		}
	}
	return AP_ItemsanityUseLeftPure(ap_itemsanity_owned, useLeft);
}

int AP_ItemsanityWeaponAvailable(int heldItemID)
{
	return !AP_ItemsanityActive() ||
	       AP_ItemsanityUseIsOwned(heldItemID, ap_itemsanity_owned);
}

// Called from VehPickupItem_ShootOnCirclePress with the driver's own held id,
// before vanilla folds Bomb/Bomb x3/Missile x3 into the shared Missile branch.
// That press is the committed-use edge: VehPhysProc has already spent the item
// by the time the fire request reaches here, and the request flag is cleared
// before this call, so one press emits its pair exactly once. AP_EmitClassCheck
// adds the absent-code and already-checked guards on top.
void AP_ItemsanityOnUse(struct Driver *driver, int heldItemID)
{
	struct GameTracker *gGT = sdata->gGT;
	long plain;
	long juiced;
	if (!AP_ItemsanityActive() || !gGT || driver != gGT->drivers[0])
		return;

	// Ownership precondition, ruled 2026-08-20. Every path the itemsanity filters
	// actually cover arrives here holding a weapon the player received, so this
	// is inert for ADVENTURE races and for boss assistance. It is NOT inert for
	// the ruled Crystal Challenge exception: the arena hardcode grants its Bomb
	// or Turbo without consulting the received set, and firing that used to bank
	// the weapon's itemsanity check. The grant stays, because the challenge is
	// unwinnable without it; the check does not.
	//
	// It is not inert OUTSIDE Adventure either, and that is deliberate rather
	// than incidental. Arcade, VS, Time Trial and Battle are all excluded by
	// AP_ItemsanityShouldFilter, so their rolls are never ownership-filtered, and
	// before this test an unowned weapon fired in one of them banked its check
	// exactly the way Kitkat's arena Turbo did. Gating the mint rather than the
	// mode closes both routes with one rule, and it costs no reachable check: an
	// itemsanity location already requires its own weapon item in logic, so a
	// refusal here can only drop a check the player could not have earned.
	//
	// Deliberately keyed on the received set rather than on the Crystal mode bit,
	// so any future unfiltered grant is covered by the same rule instead of
	// needing an exception of its own.
	if (!AP_ItemsanityUseIsOwned(heldItemID, ap_itemsanity_owned))
	{
		char refused[96];
		snprintf(refused, sizeof refused,
		         "[AP ITEMSANITY] use of unreceived weapon %d minted no check\n",
		         heldItemID);
		AP_AppendLog(refused);
		return;
	}

	// juiced comes back as -1 below 10 Wumpa, and both codes come back as -1 for
	// an id that mints nothing, so the shared emitter's absent-code guard covers
	// the whole fan-out.
	(void)AP_ItemsanityUseCodes(heldItemID, driver->numWumpas, &plain, &juiced);
	AP_EmitClassCheck(plain, 0, -1, 0, 1,
	                  "[AP CHECK] itemsanity use: weapon=%d juiced=0 location %ld\n",
	                  heldItemID, plain);
	AP_EmitClassCheck(juiced, 0, -1, 0, 1,
	                  "[AP CHECK] itemsanity use: weapon=%d juiced=1 location %ld\n",
	                  heldItemID, juiced);
}

int AP_ItemsanityFilterRoll(struct Driver *driver, int rolled, unsigned roll,
	const unsigned char *table, int tableCount)
{
	struct GameTracker *gGT = sdata->gGT;
	int filtered;
	if (!gGT || !AP_ItemsanityShouldFilter(AP_ItemsanityActive(),
	    driver == gGT->drivers[0], (gGT->gameMode1 & ADVENTURE_MODE) != 0,
	    (gGT->gameMode1 & BATTLE_MODE) != 0,
	    (gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0))
		return rolled;

	filtered = AP_ItemsanitySubstituteRoll(rolled, roll, table, tableCount,
	                                      ap_itemsanity_owned);
	if (filtered == AP_ITEMSANITY_NO_ITEM)
	{
		// Ruled Empty Crates shape: keep the box/roulette acquisition path, hold
		// no weapon, and grant one fruit through the engine's normal clamp/effect.
		RB_Player_ModifyWumpa(driver, 1);
		AP_AppendLog("[AP ITEMSANITY] no eligible roulette weapon; granted Wumpa\n");
	}
	return filtered;
}

// Item Reroll's repeat guard (#280). The trap is ruled to exclude the previous
// item when at least two valid alternatives exist, and to allow the same result
// when only one is available.
//
// It lives here rather than in ap_traps.c because the eligible pool is
// ap_itemsanity_owned, which this translation unit owns, and it reuses
// AP_ItemsanitySelectOwned's existing exclusion mask rather than growing a second
// pool walk. The same full-pool decision the draw filter documents applies: the
// replacement is drawn from every owned weapon, not from the rank-weighted table,
// so a reroll cannot be silently narrowed by race position.
//
// When itemsanity is not filtering this roll the player owns everything by
// definition, so the pool is the whole weapon list. Reusing the real ownership
// gate rather than testing AP_ItemsanityActive alone keeps the Crystal Challenge
// and battle exceptions identical to every other filter here.
//
// No RNG draw is consumed: the roll that produced `rolled` is reused, so a given
// seed and roll still produce one deterministic answer.
int AP_TrapRerollFilter(struct Driver *driver, int rolled, unsigned roll)
{
	static const unsigned char apTrapRerollAllOwned[AP_ITEMSANITY_WEAPON_COUNT] =
		{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
	struct GameTracker *gGT = sdata->gGT;
	const unsigned char *owned;
	int excluded;
	int alternative;

	if (!gGT || driver != gGT->drivers[0])
		return rolled;

	excluded = AP_TrapRerollExcludedItem();
	if (excluded < 0)
		return rolled;

	// Compare canonically, so a Spring the engine is about to rewrite to Turbo
	// does not read as a different weapon from the Turbo it becomes.
	excluded = AP_ItemsanityCanonicalWeapon(excluded);
	if (AP_ItemsanityWeaponIndex(excluded) < 0)
		return rolled; // not one of the gated weapons; nothing to exclude against
	if (AP_ItemsanityCanonicalWeapon(rolled) != excluded)
		return rolled; // the roll already differs, which is the common case

	owned = AP_ItemsanityShouldFilter(AP_ItemsanityActive(), 1,
	    (gGT->gameMode1 & ADVENTURE_MODE) != 0,
	    (gGT->gameMode1 & BATTLE_MODE) != 0,
	    (gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0)
	        ? ap_itemsanity_owned : apTrapRerollAllOwned;

	alternative = AP_ItemsanitySelectOwned(roll, AP_ITEMSANITY_ID_BIT((unsigned)excluded),
	                                       AP_ITEMSANITY_FULL_POOL,
	                                       AP_ITEMSANITY_FULL_POOL_SIZE, owned);
	if (alternative == AP_ITEMSANITY_NO_ITEM)
	{
		// One valid item in the pool, so the ruling allows the repeat. This is
		// not the itemsanity no-item path and must not grant a Wumpa: the player
		// is still holding a legitimate weapon.
		AP_AppendLog("[AP TRAP] Item Reroll: only one eligible weapon, repeat allowed\n");
		return rolled;
	}
	return alternative;
}

// Ownership guard for the Crystal Challenge arena hardcode.
//
// RULED 2026-08-20: "The weapon should not arrive in the arenas at all,
// itemsanity also applies to arenas." The arena grant is not a roll and does not
// route through any of the filters here: VehPhysGeneral_SetHeldItem selects
// ITEMSET_CrystalChallenge unconditionally and hands out Bomb or Turbo keyed on
// level id alone. So the gate has to live at that hardcode, and this is it.
//
// Unowned means the arena hands over nothing, and ONLY nothing. RULED
// 2026-08-21 (posting-pack fixing pass): the Empty Crates fruit does NOT apply
// here, unlike every race-context refusal above. Wumpa has no function inside a
// Crystal Challenge arena -- there is no roulette to juice and no reach-10
// standing -- so the consolation fruit was a functionally pointless side
// effect, not a compensation. The empty slot alone is the correct result.
//
// Uses AP_ItemsanityShouldFilter, the same predicate as every other gate in this
// file, so the arena cannot drift away from the rest of itemsanity. That only
// works because the ruling removed the `!isCrystal` term: before it, this call
// would have been inert.
int AP_ItemsanityFilterCrystalGrant(struct Driver *driver, int proposed)
{
	struct GameTracker *gGT = sdata->gGT;
	if (!gGT || !AP_ItemsanityShouldFilter(AP_ItemsanityActive(),
	    driver == gGT->drivers[0], (gGT->gameMode1 & ADVENTURE_MODE) != 0,
	    (gGT->gameMode1 & BATTLE_MODE) != 0,
	    (gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0))
		return proposed;

	if (AP_ItemsanityRollAllowed(proposed, ap_itemsanity_owned))
		return proposed;

	AP_AppendLog("[AP ITEMSANITY] arena weapon not received; slot left empty\n");
	return AP_ITEMSANITY_NO_ITEM;
}

// Ownership guard for vanilla's downstream item rewrites. The draw filter above
// only settles the roll itself; the single-warpball rule and the two-holders
// 3-missile cap rewrite that result afterwards and would otherwise hand out an
// unreceived weapon. The Crystal Challenge hardcode has its own gate directly
// above; the boss-race block has its own guard in AP_ItemsanityBossAssist below.
int AP_ItemsanitySubstituteOwned(struct Driver *driver, int proposed,
	unsigned roll, const unsigned char *table, int tableCount)
{
	struct GameTracker *gGT = sdata->gGT;
	int filtered;
	if (!gGT || !AP_ItemsanityShouldFilter(AP_ItemsanityActive(),
	    driver == gGT->drivers[0], (gGT->gameMode1 & ADVENTURE_MODE) != 0,
	    (gGT->gameMode1 & BATTLE_MODE) != 0,
	    (gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0))
		return proposed;

	filtered = AP_ItemsanitySubstituteDownstream(proposed, roll, table,
	                                            tableCount, ap_itemsanity_owned);
	if (filtered == AP_ITEMSANITY_NO_ITEM)
	{
		// Same ruled Empty Crates shape as the draw filter.
		RB_Player_ModifyWumpa(driver, 1);
		AP_AppendLog("[AP ITEMSANITY] no eligible substitute weapon; granted Wumpa\n");
	}
	return filtered;
}

// Ownership guard for the boss-race rewrite block. Called once, after the whole
// vanilla chain, with the draw the block started from and the item it settled
// on, so the three catch-up branches and the Komodo Joe cap are all covered by
// one call. Reads ap_itemsanity_owned, the same set AP_ItemsanityFilterRoll uses
// for the initial roll, under the same gate.
int AP_ItemsanityBossAssist(struct Driver *driver, int proposed, int rolled)
{
	struct GameTracker *gGT = sdata->gGT;
	int filtered;
	if (!gGT || !AP_ItemsanityShouldFilter(AP_ItemsanityActive(),
	    driver == gGT->drivers[0], (gGT->gameMode1 & ADVENTURE_MODE) != 0,
	    (gGT->gameMode1 & BATTLE_MODE) != 0,
	    (gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0))
		return proposed;

	filtered = AP_ItemsanityBossAssistWeapon(rolled, proposed,
	                                        ap_itemsanity_owned);
	if (filtered == AP_ITEMSANITY_NO_ITEM && rolled != AP_ITEMSANITY_NO_ITEM)
	{
		// Same ruled Empty Crates shape as the filters above. Reachable when
		// the guard itself had to withdraw the block's rewrite (the Komodo Joe
		// cap with nothing owned at or below it). A sentinel that merely
		// passed through untouched already granted its Wumpa in
		// AP_ItemsanityFilterRoll; granting again here would stack a second
		// fruit onto every empty boss draw.
		RB_Player_ModifyWumpa(driver, 1);
		AP_AppendLog("[AP ITEMSANITY] no eligible boss catch-up weapon; granted Wumpa\n");
	}
	else if (filtered != proposed)
	{
		char line[96];
		snprintf(line, sizeof line,
		         "[AP ITEMSANITY] boss catch-up %d not owned; substituted %d\n",
		         proposed, filtered);
		AP_AppendLog(line);
	}
	return filtered;
}

#endif // CTR_AP
