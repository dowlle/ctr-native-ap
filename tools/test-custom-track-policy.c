// Out-of-engine assertions for the custom-track loader's DECISIONS and its
// content-verification primitive (Baby T Park event spike, rung 1, ruled
// 2026-08-28). Compiles the REAL code: include/platform/native_sha256.h and
// include/platform/native_custom_tracks_policy.h are freestanding by design and
// include nothing from the engine, so this harness links nothing from the game
// and runs on any host, with no disc, no display and no seed.
//
//   cc -Wall -Wextra -DCTR_CUSTOM_TRACKS -I include -o /tmp/test-custom-track-policy tools/test-custom-track-policy.c
//   /tmp/test-custom-track-policy
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// The binding behaviour under test, in one sentence: a custom track is served
// only after its bytes have been proven to be the bytes that were promised, and
// the same proof is what turns the configured Gem Cup destination into a single
// race -- so unverified content can never produce either wrong track bytes or a
// wrong-content race.
//
// What this pins:
//   1. SHA-256 itself, against the published NIST vectors, because every
//      refusal in this feature is downstream of the digest being right,
//   2. the digest COMPARISON, which is the actual gate: case-insensitivity,
//      and that a wrong length or a non-hex character is a non-match rather
//      than a parse that happens to pass,
//   3. pair auto-expand: that one .lev and one .vrm cover all four mode-pairs
//      of an arcade group, with the right parity, and that the group's
//      boundaries are exact -- the subfile one below and one above answer NONE,
//   4. the mappable-levelID bound, which is not stylistic: past 17 the engine
//      indexes data.ArcadeDifficulty[18] and data.metaDataLEV[0x41] with no
//      range check,
//   5. the redirect's five terms, each shown to be INDEPENDENTLY load-bearing
//      by a pair of rows differing in that term alone,
//   6. THE CONSISTENCY INVARIANT, which is the whole reason the decisions live
//      in one header: for every reachable config, the answer AH_WarpPad.c uses
//      to redirect and the answer UI_CupStandings.c uses to complete the cup
//      agree. A cup that becomes a single race always finishes as one, and a
//      cup that did not redirect always keeps its four legs,
//   7. ST1 table entry presence, against the three real table shapes: that the
//      count thresholds CAM.c used to guard its two cameras with both PASS on
//      Baby T Park's full-width-with-holes table while the predicate that
//      replaced them refuses, and that on every retail shape -- arcade and
//      battle -- the two agree, which is the safety argument for the swap.
//
// MUTATION SENSITIVITY. Each redirect term is asserted in a pair of rows that
// differ in that term alone, so dropping any one of them from
// CustomTrackPolicy_ShouldRedirectCup turns a row red rather than merely losing
// coverage. The parity assertions fail if VRM and LEV are swapped. The
// consistency sweep fails if either fork's leg count is changed on its own.

#include <stdio.h>
#include <string.h>

#include <platform/native_sha256.h>
#include <platform/native_custom_tracks_policy.h>

static int g_failures = 0;

static void expect_int(int got, int want, const char *what)
{
	if (got == want)
		return;
	printf("FAIL %s: got %d, want %d\n", what, got, want);
	g_failures++;
}

static void expect_str(const char *got, const char *want, const char *what)
{
	if (strcmp(got, want) == 0)
		return;
	printf("FAIL %s:\n  got  %s\n  want %s\n", what, got, want);
	g_failures++;
}

// ---------------------------------------------------------------------------
// 1. SHA-256 against the published NIST vectors.
// ---------------------------------------------------------------------------

static void hash_string(const char *msg, char out[NATIVE_SHA256_HEX_BYTES])
{
	struct NativeSha256Ctx ctx;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];

	NativeSha256_Init(&ctx);
	NativeSha256_Update(&ctx, msg, strlen(msg));
	NativeSha256_Final(&ctx, digest);
	NativeSha256_ToHex(digest, out);
}

static void test_sha256_vectors(void)
{
	char hex[NATIVE_SHA256_HEX_BYTES];
	struct NativeSha256Ctx ctx;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
	int i;

	// The empty message: exercises the padding path with zero data bytes.
	hash_string("", hex);
	expect_str(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256(\"\")");

	// One block after padding.
	hash_string("abc", hex);
	expect_str(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(\"abc\")");

	// 56 bytes: the exact length that forces padding into a SECOND block, which
	// is the branch a track file will never take but a truncated one might.
	hash_string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", hex);
	expect_str(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
	           "sha256(448-bit NIST vector)");

	// 112 bytes: multi-block with the length field in the second block.
	hash_string("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
	            "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
	            hex);
	expect_str(hex, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
	           "sha256(896-bit NIST vector)");

	// One million 'a': the long-message vector. Fed in chunks so the streaming
	// path (Update called many times) is what is being pinned, which is how the
	// loader actually hashes a multi-MiB track file.
	NativeSha256_Init(&ctx);
	for (i = 0; i < 1000000; i++)
		NativeSha256_Update(&ctx, "a", 1);
	NativeSha256_Final(&ctx, digest);
	NativeSha256_ToHex(digest, hex);
	expect_str(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
	           "sha256(1e6 x 'a', streamed)");

	// The same message in one 64 KiB-shaped chunking as the loader uses, to show
	// the digest does not depend on how the input is split.
	{
		static char buf[1000000];
		char hex2[NATIVE_SHA256_HEX_BYTES];
		size_t off;

		memset(buf, 'a', sizeof(buf));
		NativeSha256_Init(&ctx);
		for (off = 0; off < sizeof(buf); off += 65536)
		{
			size_t n = sizeof(buf) - off;
			if (n > 65536)
				n = 65536;
			NativeSha256_Update(&ctx, buf + off, n);
		}
		NativeSha256_Final(&ctx, digest);
		NativeSha256_ToHex(digest, hex2);
		expect_str(hex2, hex, "sha256 is chunking-independent");
	}
}

// ---------------------------------------------------------------------------
// 2. The digest comparison -- the actual accept/reject gate.
// ---------------------------------------------------------------------------

static void test_hash_compare(void)
{
	const char *abcHex = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	char actual[NATIVE_SHA256_HEX_BYTES];

	hash_string("abc", actual);

	expect_int(NativeSha256_HexEquals(abcHex, actual), 1, "exact digest matches");

	expect_int(NativeSha256_HexEquals("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", actual),
	           1, "uppercase digest matches (config is hand-edited)");

	expect_int(NativeSha256_HexEquals("Ba7816BF8f01cfEA414140de5dae2223b00361a396177a9cb410ff61f20015aD", actual),
	           1, "mixed-case digest matches");

	// A single flipped digit is a mismatch. This is the wrong-content case.
	expect_int(NativeSha256_HexEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ae", actual),
	           0, "one wrong digit is a mismatch");

	// Truncated: 63 digits. Must NOT match on a prefix.
	expect_int(NativeSha256_HexEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015a", actual),
	           0, "63-digit expected is a mismatch");

	// Too long: the right 64 digits followed by junk.
	expect_int(NativeSha256_HexEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015adzz", actual),
	           0, "65+ digit expected is a mismatch");

	// Non-hex character inside an otherwise correct-length string.
	expect_int(NativeSha256_HexEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015aZ", actual),
	           0, "non-hex character is a mismatch");

	expect_int(NativeSha256_HexEquals("", actual), 0, "empty expected is a mismatch");
	expect_int(NativeSha256_HexEquals(NULL, actual), 0, "NULL expected is a mismatch");

	// The digest of a DIFFERENT message must not match: the primitive, not just
	// the string compare, is what distinguishes content.
	{
		char other[NATIVE_SHA256_HEX_BYTES];
		hash_string("abd", other);
		expect_int(NativeSha256_HexEquals(abcHex, other), 0, "digest of different content mismatches");
	}
}

// ---------------------------------------------------------------------------
// 3. Pair auto-expand.
// ---------------------------------------------------------------------------

static void test_pair_auto_expand(void)
{
	const int mapped = 6; // Roo Tubes; group is subfiles 48..55
	int base = mapped * CTR_CT_GROUP_SIZE;
	int slot;

	// One .vrm and one .lev cover all four mode-pairs: even slots are the VRM,
	// odd slots the LEV. 1P reads only slots 0 and 1; 2/3, 4/5 and 6/7 are the
	// 2P, 4P and relic modes.
	for (slot = 0; slot < CTR_CT_GROUP_SIZE; slot++)
	{
		char what[64];
		int want = (slot & 1) ? CTR_CT_ROLE_LEV : CTR_CT_ROLE_VRM;

		snprintf(what, sizeof(what), "group slot %d role", slot);
		expect_int(CustomTrackPolicy_SubfileRole(base + slot, mapped), want, what);
	}

	// The group's boundaries are exact: the neighbouring subfiles belong to the
	// tracks either side and must never be served from the custom track.
	expect_int(CustomTrackPolicy_SubfileRole(base - 1, mapped), CTR_CT_ROLE_NONE, "subfile below group");
	expect_int(CustomTrackPolicy_SubfileRole(base + CTR_CT_GROUP_SIZE, mapped), CTR_CT_ROLE_NONE,
	           "subfile above group");
	expect_int(CustomTrackPolicy_SubfileRole(0, mapped), CTR_CT_ROLE_NONE, "subfile 0 with a non-zero mapping");

	// Slot 0 of the FIRST arcade track is subfile 0; a zero mapping must still
	// resolve, so the "no override" answer cannot be conflated with index 0.
	expect_int(CustomTrackPolicy_SubfileRole(0, 0), CTR_CT_ROLE_VRM, "subfile 0 with mapping 0 is the VRM");
	expect_int(CustomTrackPolicy_SubfileRole(1, 0), CTR_CT_ROLE_LEV, "subfile 1 with mapping 0 is the LEV");

	// A disarmed loader (mappedLevelID -1) answers NONE for everything.
	expect_int(CustomTrackPolicy_SubfileRole(base, -1), CTR_CT_ROLE_NONE, "no mapping serves nothing");
	expect_int(CustomTrackPolicy_SubfileRole(base, CTR_CT_MAX_LEVELS), CTR_CT_ROLE_NONE,
	           "out-of-range mapping serves nothing");

	// Negative indices are refused rather than dividing into a plausible group.
	expect_int(CustomTrackPolicy_SubfileRole(-1, mapped), CTR_CT_ROLE_NONE, "negative subfile index");

	// The mappable bound, stated explicitly.
	expect_int(CustomTrackPolicy_LevelIDIsMappable(-1), 0, "levelID -1 not mappable");
	expect_int(CustomTrackPolicy_LevelIDIsMappable(0), 1, "levelID 0 mappable");
	expect_int(CustomTrackPolicy_LevelIDIsMappable(17), 1, "levelID 17 mappable (last arcade track)");
	expect_int(CustomTrackPolicy_LevelIDIsMappable(18), 0, "levelID 18 not mappable (NITRO_COURT, a battle arena)");
	expect_int(CustomTrackPolicy_LevelIDIsMappable(25), 0, "levelID 25 not mappable (a hub)");
}

// ---------------------------------------------------------------------------
// 4. The redirect. Every term shown independently load-bearing.
// ---------------------------------------------------------------------------

// The ruled configuration: the feature on, content verified, Roo Tubes' slot
// taken over, Purple Gem Cup (4) becoming one 7-lap race.
static struct CustomTrackFeatureConfig ruled_config(void)
{
	struct CustomTrackFeatureConfig cfg;

	cfg.mappedLevelID = 6;
	cfg.contentVerified = 1;
	cfg.raceEnabled = 1;
	cfg.raceCupID = 4;
	cfg.raceLaps = 7;
	cfg.raceBoxes = 1;
	return cfg;
}

static struct CustomTrackLoadContext make_ctx(int levelID, int adventureCupActive, int cupID)
{
	struct CustomTrackLoadContext ctx;

	ctx.levelID = levelID;
	ctx.adventureCupActive = adventureCupActive;
	ctx.cupID = cupID;
	return ctx;
}

static void test_redirect_terms(void)
{
	struct CustomTrackFeatureConfig cfg;

	// The ruled case redirects.
	cfg = ruled_config();
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 4, 1), 1, "ruled config redirects Purple");
	expect_int(CustomTrackPolicy_RaceLevelID(&cfg, 4, 1), 6, "ruled config races levelID 6");
	expect_int(CustomTrackPolicy_RaceLaps(&cfg, 4, 1), 7, "ruled config races 7 laps");

	// TERM: raceEnabled. Same config, feature off.
	cfg = ruled_config();
	cfg.raceEnabled = 0;
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 4, 1), 0, "feature off does not redirect");
	expect_int(CustomTrackPolicy_RaceLevelID(&cfg, 4, 1), -1, "feature off has no race levelID");
	expect_int(CustomTrackPolicy_RaceLaps(&cfg, 4, 1), 0, "feature off writes no lap count");

	// TERM: contentVerified. This is the one that matters most: an unverified
	// track must not merely fall back for the LEV read, it must leave the cup
	// alone, or the player races 7 laps on the RETAIL contents of slot 6.
	cfg = ruled_config();
	cfg.contentVerified = 0;
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 4, 1), 0, "unverified content does not redirect");
	expect_int(CustomTrackPolicy_RaceLaps(&cfg, 4, 1), 0, "unverified content writes no lap count");

	// TERM: isAdventureCup. Arcade/VS cups share gGT->cup.cupID.
	cfg = ruled_config();
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 4, 0), 0, "an arcade cup is never redirected");
	cfg.raceCupID = 2; // a cupID an arcade cup really can have (ArcadeCups[4])
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 2, 0), 0, "arcade cup 2 is not redirected");
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 2, 1), 1, "adventure cup 2 with that config is");

	// TERM: mappedLevelID. Half-filled config has nowhere to send the player.
	cfg = ruled_config();
	cfg.mappedLevelID = -1;
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 4, 1), 0, "no mapped slot does not redirect");
	cfg.mappedLevelID = 18;
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 4, 1), 0, "unmappable slot does not redirect");

	// TERM: cupID match. Only the configured cup is replaced.
	cfg = ruled_config();
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 0, 1), 0, "Red Gem Cup keeps its legs");
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 1, 1), 0, "Green Gem Cup keeps its legs");
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 2, 1), 0, "Blue Gem Cup keeps its legs");
	expect_int(CustomTrackPolicy_ShouldRedirectCup(&cfg, 3, 1), 0, "Yellow Gem Cup keeps its legs");

	// A NULL config is refused rather than dereferenced.
	expect_int(CustomTrackPolicy_ShouldRedirectCup(NULL, 4, 1), 0, "NULL config does not redirect");

	// Lap bounds. gGT->numLaps is a char and the retail ladder tops out at 7, so
	// anything outside 1..7 is refused rather than truncated into the field.
	cfg = ruled_config();
	cfg.raceLaps = 0;
	expect_int(CustomTrackPolicy_RaceLaps(&cfg, 4, 1), 0, "0 laps refused");
	cfg.raceLaps = -3;
	expect_int(CustomTrackPolicy_RaceLaps(&cfg, 4, 1), 0, "negative laps refused");
	cfg.raceLaps = 8;
	expect_int(CustomTrackPolicy_RaceLaps(&cfg, 4, 1), 0, "8 laps refused (above the retail ladder)");
	cfg.raceLaps = 1;
	expect_int(CustomTrackPolicy_RaceLaps(&cfg, 4, 1), 1, "1 lap accepted");
	cfg.raceLaps = 7;
	expect_int(CustomTrackPolicy_RaceLaps(&cfg, 4, 1), 7, "7 laps accepted");
}

// ---------------------------------------------------------------------------
// 5. Cup completion, and the invariant that ties the two engine forks together.
// ---------------------------------------------------------------------------

static void test_cup_completion(void)
{
	struct CustomTrackFeatureConfig cfg = ruled_config();

	// A redirected cup ran one race: complete at trackIndex 1.
	expect_int(CustomTrackPolicy_CupIsComplete(&cfg, 4, 1, 1), 1, "redirected Purple completes after one race");

	// Every other cup keeps vanilla's four legs, with the same config loaded.
	expect_int(CustomTrackPolicy_CupIsComplete(&cfg, 0, 1, 1), 0, "Red is not complete at leg 1");
	expect_int(CustomTrackPolicy_CupIsComplete(&cfg, 0, 1, 2), 0, "Red is not complete at leg 2");
	expect_int(CustomTrackPolicy_CupIsComplete(&cfg, 0, 1, 3), 0, "Red is not complete at leg 3");
	expect_int(CustomTrackPolicy_CupIsComplete(&cfg, 0, 1, 4), 1, "Red completes at leg 4");

	// With the feature off, even the configured cup keeps four legs -- this is
	// the guard-off-runtime identity the ruling requires.
	cfg.raceEnabled = 0;
	expect_int(CustomTrackPolicy_CupIsComplete(&cfg, 4, 1, 1), 0, "feature off: Purple is not complete at leg 1");
	expect_int(CustomTrackPolicy_CupIsComplete(&cfg, 4, 1, 4), 1, "feature off: Purple completes at leg 4");

	// Unverified content likewise leaves the cup at four legs, matching the
	// redirect's refusal at the pad.
	cfg = ruled_config();
	cfg.contentVerified = 0;
	expect_int(CustomTrackPolicy_CupIsComplete(&cfg, 4, 1, 1), 0, "unverified: Purple is not complete at leg 1");
	expect_int(CustomTrackPolicy_CupIsComplete(&cfg, 4, 1, 4), 1, "unverified: Purple completes at leg 4");
}

// THE CONSISTENCY INVARIANT. AH_WarpPad.c decides whether to redirect; then, one
// race later and in a different file, UI_CupStandings.c decides whether the cup
// is over. If those two could disagree, a redirected cup would try to load a leg
// 1 it never had, or a vanilla cup would award its gem after one race. Sweep
// every reachable combination and assert they cannot.
static void test_fork_consistency(void)
{
	int enabled, verified, adventure, mapped, cup, laps;
	int checked = 0;

	for (enabled = 0; enabled <= 1; enabled++)
		for (verified = 0; verified <= 1; verified++)
			for (adventure = 0; adventure <= 1; adventure++)
				for (mapped = -1; mapped < 19; mapped++)
					for (laps = 0; laps <= 8; laps++)
						for (cup = 0; cup <= 4; cup++)
						{
							struct CustomTrackFeatureConfig cfg;
							int redirected;
							int completeAt1;
							int completeAt4;

							struct CustomTrackLoadContext ctx;
							struct CustomTrackLoadContext retailCtx;

							cfg.raceEnabled = enabled;
							cfg.contentVerified = verified;
							cfg.mappedLevelID = mapped;
							cfg.raceCupID = 4;
							cfg.raceLaps = laps;
							cfg.raceBoxes = 1;

							redirected = CustomTrackPolicy_ShouldRedirectCup(&cfg, cup, adventure);
							completeAt1 = CustomTrackPolicy_CupIsComplete(&cfg, cup, adventure, 1);
							completeAt4 = CustomTrackPolicy_CupIsComplete(&cfg, cup, adventure, 4);
							checked++;

							// THE RUNG-2A INVARIANT. A load that looks exactly
							// like the event race except that no gem cup is in
							// progress -- i.e. the host slot's own retail race
							// pad -- must NEVER be served custom bytes. Asserted
							// for every config, not just the ruled one, because
							// this is the property the whole serve-context
							// decision exists to guarantee.
							ctx = make_ctx(mapped, 1, cup);
							retailCtx = make_ctx(mapped, 0, cup);

							if (CustomTrackPolicy_ShouldServe(&cfg, &retailCtx))
							{
								printf("FAIL retail pad served custom bytes: enabled=%d verified=%d "
								       "mapped=%d cup=%d\n",
								       enabled, verified, mapped, cup);
								g_failures++;
								return;
							}

							// Serving implies the pad redirected. The loader can
							// never serve a race the warp pad did not send the
							// player to.
							if (CustomTrackPolicy_ShouldServe(&cfg, &ctx) &&
							    !CustomTrackPolicy_ShouldRedirectCup(&cfg, cup, 1))
							{
								printf("FAIL served without a redirect: enabled=%d verified=%d "
								       "mapped=%d cup=%d\n",
								       enabled, verified, mapped, cup);
								g_failures++;
								return;
							}

							// The box verdict speaks only for loads it owns.
							if ((CustomTrackPolicy_BoxVerdict(&cfg, &ctx) != CTR_CT_BOX_UNCHANGED) !=
							    CustomTrackPolicy_ShouldServe(&cfg, &ctx))
							{
								printf("FAIL box verdict disagrees with serve: enabled=%d verified=%d "
								       "mapped=%d cup=%d\n",
								       enabled, verified, mapped, cup);
								g_failures++;
								return;
							}

							// The HUD counter agrees with the completion fork.
							if (CustomTrackPolicy_CupLegCount(&cfg, cup, adventure) !=
							    (redirected ? 1 : 4))
							{
								printf("FAIL leg count disagrees with redirect: cup=%d\n", cup);
								g_failures++;
								return;
							}

							// Redirected exactly when the cup ends after one race.
							if (redirected != completeAt1)
							{
								printf("FAIL fork disagreement: enabled=%d verified=%d adv=%d mapped=%d "
								       "cup=%d laps=%d -> redirect=%d completeAt1=%d\n",
								       enabled, verified, adventure, mapped, cup, laps, redirected,
								       completeAt1);
								g_failures++;
								return;
							}

							// A cup is always complete by leg 4, redirected or not:
							// no configuration can make a cup run forever.
							if (!completeAt4)
							{
								printf("FAIL cup never completes: enabled=%d verified=%d adv=%d mapped=%d "
								       "cup=%d laps=%d\n",
								       enabled, verified, adventure, mapped, cup, laps);
								g_failures++;
								return;
							}

							// A redirect always names a real slot and a legal lap
							// count; a non-redirect always names neither.
							if (redirected)
							{
								if (!CustomTrackPolicy_LevelIDIsMappable(
								        CustomTrackPolicy_RaceLevelID(&cfg, cup, adventure)))
								{
									printf("FAIL redirect to unmappable levelID: mapped=%d\n", mapped);
									g_failures++;
									return;
								}
							}
							else
							{
								if (CustomTrackPolicy_RaceLevelID(&cfg, cup, adventure) != -1 ||
								    CustomTrackPolicy_RaceLaps(&cfg, cup, adventure) != 0)
								{
									printf("FAIL non-redirect leaked a destination: mapped=%d laps=%d\n",
									       mapped, laps);
									g_failures++;
									return;
								}
							}
						}

	printf("  consistency sweep: %d configurations, forks agree on every one\n", checked);
}


// ---------------------------------------------------------------------------
// 6. Serve context -- the rung-2a deliverable.
// ---------------------------------------------------------------------------

// THE RULED SEMANTICS, stated as a test: only the event cup's destination
// becomes Baby T Park. The host slot's own retail race must still load retail
// bytes, in the SAME session, from the SAME eight subfile indices.
static void test_serve_context(void)
{
	struct CustomTrackFeatureConfig cfg = ruled_config(); // mapped onto levelID 6
	struct CustomTrackLoadContext ctx;

	// The event race: a gem cup is in progress, it is the configured cup, and
	// the level being loaded is the mapped slot.
	ctx = make_ctx(6, 1, 4);
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 1, "event race serves custom bytes");

	// TERM: adventureCupActive. The host slot's RETAIL race pad. This is the
	// case rung 2a exists to fix, and it is also why cupID must never be tested
	// alone: cup.cupID is never reset, so after any Purple cup it still reads 4
	// forever. Only the ADVENTURE_CUP flag distinguishes the two loads.
	ctx = make_ctx(6, 0, 4);
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 0,
	           "retail race pad to the host slot loads retail bytes (stale cupID 4)");

	// TERM: cupID match. A different gem cup whose legs were shuffled onto the
	// host slot still races the retail track.
	ctx = make_ctx(6, 1, 1);
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 0,
	           "another gem cup's leg on the host slot loads retail bytes");
	ctx = make_ctx(6, 1, 0);
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 0, "Red cup leg on the host slot stays retail");

	// TERM: levelID match. The event cup loading anything else serves nothing.
	ctx = make_ctx(7, 1, 4);
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 0, "a different level in the event cup stays retail");
	ctx = make_ctx(25, 1, 4);
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 0, "a hub load in the event cup stays retail");

	// TERM: contentVerified, and TERM: raceEnabled -- both folded in through
	// ShouldRedirectCup, so an unverified or disabled feature serves nothing
	// even on a load that otherwise looks exactly like the event race.
	ctx = make_ctx(6, 1, 4);
	cfg.contentVerified = 0;
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 0, "unverified content serves nothing");
	cfg = ruled_config();
	cfg.raceEnabled = 0;
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 0, "event destination off serves nothing");

	// NULLs are refused rather than dereferenced.
	cfg = ruled_config();
	expect_int(CustomTrackPolicy_ShouldServe(NULL, &ctx), 0, "NULL config serves nothing");
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, NULL), 0, "NULL context serves nothing");

	// A configurable event cup moves the whole answer with it, so nothing may be
	// keyed on the literal 4.
	cfg = ruled_config();
	cfg.raceCupID = 2;
	ctx = make_ctx(6, 1, 2);
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 1, "a reconfigured event cup serves");
	ctx = make_ctx(6, 1, 4);
	expect_int(CustomTrackPolicy_ShouldServe(&cfg, &ctx), 0, "cup 4 no longer serves once cup 2 is the event");
}

// ---------------------------------------------------------------------------
// 7. HUD leg count and the AP-box verdict.
// ---------------------------------------------------------------------------

static void test_leg_count(void)
{
	struct CustomTrackFeatureConfig cfg = ruled_config();

	expect_int(CustomTrackPolicy_CupLegCount(&cfg, 4, 1), 1, "redirected cup reads TRACK n/1");
	expect_int(CustomTrackPolicy_CupLegCount(&cfg, 0, 1), 4, "Red cup still reads TRACK n/4");
	expect_int(CustomTrackPolicy_CupLegCount(&cfg, 4, 0), 4, "an arcade cup still reads TRACK n/4");

	cfg.raceEnabled = 0;
	expect_int(CustomTrackPolicy_CupLegCount(&cfg, 4, 1), 4, "feature off reads TRACK n/4");
}

static void test_box_verdict(void)
{
	struct CustomTrackFeatureConfig cfg = ruled_config();
	struct CustomTrackLoadContext ctx = make_ctx(6, 1, 4);

	// Ruled default: boxes allowed on the event race.
	expect_int(CustomTrackPolicy_BoxVerdict(&cfg, &ctx), CTR_CT_BOX_ALLOW, "event race allows boxes by default");

	cfg.raceBoxes = 0;
	expect_int(CustomTrackPolicy_BoxVerdict(&cfg, &ctx), CTR_CT_BOX_DENY, "boxes can be denied on the event race");

	// Everything that is not the event race leaves the existing cup-leg policy
	// alone -- the verdict must never speak for a load it does not own.
	cfg = ruled_config();
	ctx = make_ctx(6, 0, 4);
	expect_int(CustomTrackPolicy_BoxVerdict(&cfg, &ctx), CTR_CT_BOX_UNCHANGED, "retail pad: box policy unchanged");
	ctx = make_ctx(6, 1, 1);
	expect_int(CustomTrackPolicy_BoxVerdict(&cfg, &ctx), CTR_CT_BOX_UNCHANGED, "another cup's leg: unchanged");
	ctx = make_ctx(3, 1, 4);
	expect_int(CustomTrackPolicy_BoxVerdict(&cfg, &ctx), CTR_CT_BOX_UNCHANGED, "a different level: unchanged");

	cfg.raceBoxes = 0;
	expect_int(CustomTrackPolicy_BoxVerdict(&cfg, &ctx), CTR_CT_BOX_UNCHANGED,
	           "denying boxes never leaks onto a non-event load");
}


// ---------------------------------------------------------------------------
// 8. Measured capability flags (rung 2c).
// ---------------------------------------------------------------------------

// Only two of the descriptor's eight measured flags gate the race, and both are
// hard requirements of the ruled semantics rather than preferences. The other
// six are carried so the check rungs above the Gem have an honest input when
// they land; refusing on a flag this build cannot act on would reject tracks it
// can perfectly well serve.
static void test_measured_flags(void)
{
	const char *why;

	// The Purple Gem Cup grids five karts: MainInit_Drivers gives cupID 4
	// numPlyrCurrGame + 4 rather than a full field, and LOAD_Assets forces that
	// roster to the four bosses. Every other adventure cup grids eight.
	expect_int(CustomTrackPolicy_RequiredSpawns(4), 5, "the Purple cup grids five karts");
	expect_int(CustomTrackPolicy_RequiredSpawns(0), 8, "Red grids eight");
	expect_int(CustomTrackPolicy_RequiredSpawns(3), 8, "Yellow grids eight");

	// ai_nav: the ruling says AI bots on, and a track with no LevNavTable has no
	// paths for them to drive. Racing it alone would silently drop half the ruled
	// behaviour, so it is refused rather than degraded.
	why = NULL;
	expect_int(CustomTrackPolicy_FlagsSupportRace(0, 8, 4, &why), 0, "no AI nav is refused");
	expect_int(why != NULL, 1, "and says why");

	// spawns: one DriverSpawn slot per kart on the grid.
	why = NULL;
	expect_int(CustomTrackPolicy_FlagsSupportRace(1, 4, 4, &why), 0, "4 spawns is one short for cup 4");
	expect_int(CustomTrackPolicy_FlagsSupportRace(1, 5, 4, &why), 1, "5 spawns is exactly enough");
	expect_int(CustomTrackPolicy_FlagsSupportRace(1, 8, 4, &why), 1, "8 spawns is plenty");

	// The same track measured against a cup that grids eight.
	expect_int(CustomTrackPolicy_FlagsSupportRace(1, 5, 1, &why), 0, "5 spawns is short for cup 1");
	expect_int(CustomTrackPolicy_FlagsSupportRace(1, 8, 1, &why), 1, "8 spawns covers cup 1");

	// Baby T Park's own measured shape: 8 spawns, AI nav present.
	expect_int(CustomTrackPolicy_FlagsSupportRace(1, 8, 4, &why), 1, "the event track's shape is servable");

	// outWhy is optional: the loader passes one, a caller that only wants the
	// verdict must not have to.
	expect_int(CustomTrackPolicy_FlagsSupportRace(0, 8, 4, NULL), 0, "a NULL reason is allowed");
}

// ---------------------------------------------------------------------------
// 9. ST1 table entry presence -- the camera-absent guard.
// ---------------------------------------------------------------------------

// The ST1_* values, restated here because this harness is engine-free by
// design and include/namespace_Level.h is engine. They are a wire format in
// practice: they index the pointer array inside a LEV's SpawnType1 table, so a
// build that disagreed with the file would be reading the wrong slot. Pinning
// them here is deliberate -- if the enum in namespace_Level.h is ever
// reordered, these rows stop describing the tracks they claim to describe.
enum
{
	T_ST1_MAP = 0,
	T_ST1_SPAWN = 1,
	T_ST1_CAMERA_EOR = 2,
	T_ST1_CAMERA_PATH = 3,
	T_ST1_NTROPY = 4,
	T_ST1_NOXIDE = 5,
	T_ST1_CREDITS = 6
};

// Three real table shapes, measured by relocating real LEVs through the
// engine's own LOAD_RunPtrMap and reading struct Level::ptrSpawnType1.
//
//   retail arcade -- all 18 tracks in the NTSC-U BIGFILE: count == 4, and
//                    ST1_CAMERA_EOR and ST1_CAMERA_PATH non-NULL on every one.
//                    (15 of the 18 carry a NULL ST1_SPAWN, which is why "some
//                    entry is NULL" alone is not the interesting condition.)
//   retail battle -- all 7 arenas: count == 0, so no index is in bounds.
//   Baby T Park   -- count == 7 with ST1_MAP, ST1_CAMERA_EOR, ST1_CAMERA_PATH
//                    and ST1_CREDITS NULL. The full-width-table-with-holes
//                    encoding no retail level uses.
//
// A non-NULL entry's actual value is irrelevant to the predicate, so these use
// a single dummy address to stand for "present".
static void test_st1_entry_present(void)
{
	static const int present = 0;
	const void *const *entries;

	const void *retailArcade[4] = { &present, NULL, &present, &present };
	const void *retailBattle[1] = { NULL }; // count 0; the array is never indexed
	const void *babyTPark[7] = { NULL, &present, NULL, NULL, &present, &present, NULL };

	// --- the shape the crash was reported on -------------------------------
	entries = babyTPark;

	// What the engine used to ask. Both thresholds pass, which is the bug:
	// CAM.c's fly-in guard was count < 4 and its end-of-race guard count < 3.
	expect_int(7 >= 4, 1, "Baby T Park clears the old count < 4 fly-in threshold");
	expect_int(7 >= 3, 1, "Baby T Park clears the old count < 3 end-of-race threshold");

	// What it asks now. Both refuse, which is the fix.
	expect_int(CustomTrackPolicy_St1EntryPresent(7, T_ST1_CAMERA_PATH, entries), 0,
	           "Baby T Park has no intro camera path");
	expect_int(CustomTrackPolicy_St1EntryPresent(7, T_ST1_CAMERA_EOR, entries), 0,
	           "Baby T Park has no end-of-race cameras");

	// The same table's entries that ARE there still read as present, so the
	// predicate is answering per-index rather than condemning the whole table.
	expect_int(CustomTrackPolicy_St1EntryPresent(7, T_ST1_SPAWN, entries), 1, "it does have object spawns");
	expect_int(CustomTrackPolicy_St1EntryPresent(7, T_ST1_NTROPY, entries), 1, "and an N. Tropy ghost");
	expect_int(CustomTrackPolicy_St1EntryPresent(7, T_ST1_NOXIDE, entries), 1, "and an Oxide ghost");
	expect_int(CustomTrackPolicy_St1EntryPresent(7, T_ST1_MAP, entries), 0, "but no minimap");
	expect_int(CustomTrackPolicy_St1EntryPresent(7, T_ST1_CREDITS, entries), 0, "and no credits camera");

	// --- retail must be unchanged ------------------------------------------
	// This is the whole safety argument for swapping the thresholds: on retail
	// content the new predicate and the old count test give the same answer, so
	// no retail track can change behaviour.
	entries = retailArcade;
	expect_int(CustomTrackPolicy_St1EntryPresent(4, T_ST1_CAMERA_PATH, entries), 1,
	           "a retail arcade track has an intro camera path");
	expect_int(CustomTrackPolicy_St1EntryPresent(4, T_ST1_CAMERA_EOR, entries), 1,
	           "and end-of-race cameras");
	expect_int(CustomTrackPolicy_St1EntryPresent(4, T_ST1_CAMERA_PATH, entries), 4 >= 4,
	           "so the fly-in guard agrees with the old threshold on retail");
	expect_int(CustomTrackPolicy_St1EntryPresent(4, T_ST1_CAMERA_EOR, entries), 4 >= 3,
	           "and so does the end-of-race guard");

	entries = retailBattle;
	expect_int(CustomTrackPolicy_St1EntryPresent(0, T_ST1_CAMERA_PATH, entries), 0,
	           "a battle arena has no intro camera path");
	expect_int(CustomTrackPolicy_St1EntryPresent(0, T_ST1_CAMERA_EOR, entries), 0,
	           "nor end-of-race cameras");
	expect_int(CustomTrackPolicy_St1EntryPresent(0, T_ST1_CAMERA_PATH, entries), 0 >= 4,
	           "so the fly-in guard agrees with the old threshold on battle maps");
	expect_int(CustomTrackPolicy_St1EntryPresent(0, T_ST1_CAMERA_EOR, entries), 0 >= 3,
	           "and so does the end-of-race guard");

	// --- the bound is checked BEFORE the array is indexed -------------------
	// Load-bearing, not defensive: CAM_EndOfRace's guard runs on a table it has
	// only proven to have count > 1, and asks about index 2. A predicate that
	// indexed first would read one past the end of a two-entry table.
	entries = retailArcade;
	expect_int(CustomTrackPolicy_St1EntryPresent(2, T_ST1_CAMERA_EOR, entries), 0,
	           "index 2 is out of bounds in a two-entry table");
	expect_int(CustomTrackPolicy_St1EntryPresent(3, T_ST1_CAMERA_EOR, entries), 1,
	           "and in bounds in a three-entry table");
	expect_int(CustomTrackPolicy_St1EntryPresent(3, T_ST1_CAMERA_PATH, entries), 0,
	           "while index 3 is not");

	// Degenerate inputs cannot be made to read anything.
	expect_int(CustomTrackPolicy_St1EntryPresent(7, T_ST1_CAMERA_PATH, NULL), 0, "a NULL array is absent");
	expect_int(CustomTrackPolicy_St1EntryPresent(0, 0, entries), 0, "an empty table is absent");
	expect_int(CustomTrackPolicy_St1EntryPresent(-1, 0, entries), 0, "a negative count is absent");
	expect_int(CustomTrackPolicy_St1EntryPresent(4, -1, entries), 0, "a negative index is absent");
}

// ---------------------------------------------------------------------------
// 10. Room for one more primitive -- the sky and star clamps.
// ---------------------------------------------------------------------------

// POLY_G3 is 0x1C bytes and TILE_1 is 0x0C, both static-asserted in engine
// (include/psx/libgpu.h, game/RenderStars.c). Restated as literals because this
// harness is engine-free.
#define T_POLY_G3_SIZE 0x1CuL
#define T_TILE_1_SIZE  0x0CuL

static void test_prim_fits(void)
{
	// A stand-in arena. Only the addresses matter, never the contents.
	static char arena[256];
	const char *start = arena;
	const char *guard = arena + 128; // this arena's "guardEnd"

	// The ordinary answer: room measured from the cursor to the guard.
	expect_int(CustomTrackPolicy_PrimFits(start, T_POLY_G3_SIZE, guard), 1, "a fresh arena has room");
	expect_int(CustomTrackPolicy_PrimFits(guard - 100, T_POLY_G3_SIZE, guard), 1, "100 bytes short of the guard has room");

	// The exact edge. The engine's idiom is that `cursor + size >= guard` means
	// NO room, so a primitive landing exactly ON the guard is refused -- that is
	// what preserves the 0x100 bytes retail reserves past it.
	expect_int(CustomTrackPolicy_PrimFits(guard - (T_POLY_G3_SIZE + 1), T_POLY_G3_SIZE, guard), 1,
	           "one byte more than the primitive needs is room");
	expect_int(CustomTrackPolicy_PrimFits(guard - T_POLY_G3_SIZE, T_POLY_G3_SIZE, guard), 0,
	           "landing exactly on the guard is refused");
	expect_int(CustomTrackPolicy_PrimFits(guard - (T_POLY_G3_SIZE - 1), T_POLY_G3_SIZE, guard), 0,
	           "one byte short is refused");

	// A cursor already at or past the guard. Not defensiveness: an emitter
	// earlier in the frame can leave it there, and this one must then refuse
	// rather than compute a negative span and wrap it into a huge positive.
	expect_int(CustomTrackPolicy_PrimFits(guard, T_POLY_G3_SIZE, guard), 0, "a cursor on the guard is full");
	expect_int(CustomTrackPolicy_PrimFits(guard + 1, T_POLY_G3_SIZE, guard), 0, "a cursor past the guard is full");
	expect_int(CustomTrackPolicy_PrimFits(guard + 100, T_POLY_G3_SIZE, guard), 0, "far past the guard is still full");

	// The size argument is honoured, so the two call sites get different
	// answers at the same cursor. RenderStars reserves a TILE_1 plus its
	// trailing draw-mode packet, so it must be able to ask for more than one
	// primitive's worth.
	expect_int(CustomTrackPolicy_PrimFits(guard - 0x10, T_TILE_1_SIZE, guard), 1, "a TILE_1 fits in 0x10 bytes");
	expect_int(CustomTrackPolicy_PrimFits(guard - 0x10, T_POLY_G3_SIZE, guard), 0, "a POLY_G3 does not");

	expect_int(CustomTrackPolicy_PrimFits(NULL, T_POLY_G3_SIZE, guard), 0, "a NULL cursor is full");
	expect_int(CustomTrackPolicy_PrimFits(start, T_POLY_G3_SIZE, NULL), 0, "a NULL guard is full");

	// --- the measured demand this clamp exists for -------------------------
	// data.primMem_SizePerLEV_1P[6] is 0x67 (game/zGlobal_DATA.c), and
	// MainInit.c shifts it left by 10 for a 1P race.
	{
		const long budget = 0x67L << 10; // 105,472 bytes for levelID 6
		const long usable = budget - 0x100;

		// DrawSky_Full draws four of the skybox's eight segments per frame.
		// Measured on the event track: 2,772 faces in every segment.
		const long eventFaces = 4L * 2772L;
		const long eventBytes = eventFaces * (long)T_POLY_G3_SIZE;

		// The worst of the 18 retail arcade tracks, measured the same way.
		const long retailWorstBytes = 385L * (long)T_POLY_G3_SIZE;

		expect_int(eventBytes > usable, 1, "the event track's sky alone overruns the arena");
		expect_int(eventBytes > 2 * budget, 1, "and by more than double the whole arena");
		expect_int(retailWorstBytes < usable / 8, 1, "while the worst retail sky uses under an eighth of it");

		// Growing the budget is not an available answer: the table is u8 << 10.
		expect_int((255L << 10) < eventBytes, 1,
		           "even the largest budget that table can express is smaller than this one sky");
	}
}

// ---------------------------------------------------------------------------
// 11. How big the primitive arena is.
// ---------------------------------------------------------------------------

static void test_prim_arena_bytes(void)
{
	const unsigned long retailSlot6 = 0x67uL << 10; // 105,472
	const unsigned long retailWidest = 0x6euL << 10; // 112,640, the table's largest real entry

	// A load that is not the event race is handed the retail figure untouched.
	// This is the whole retail-identity argument: the same binary races both,
	// and only the custom track's load differs.
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, retailSlot6) == retailSlot6, 1,
	           "a retail load keeps the retail arena exactly");
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, retailWidest) == retailWidest, 1,
	           "including the table's widest entry");
	expect_int(CustomTrackPolicy_PrimArenaBytes(0, CTR_CT_PRIM_ARENA_BYTES + 1) == CTR_CT_PRIM_ARENA_BYTES + 1, 1,
	           "and a hypothetical huge one");

	// The event race gets the measured arena.
	expect_int(CustomTrackPolicy_PrimArenaBytes(1, retailSlot6) == CTR_CT_PRIM_ARENA_BYTES, 1,
	           "the event race gets the measured arena");

	// It is a FLOOR, not a replacement: a slot whose retail budget was already
	// larger must not be shrunk by turning the feature on.
	expect_int(CustomTrackPolicy_PrimArenaBytes(1, CTR_CT_PRIM_ARENA_BYTES + 4096) == CTR_CT_PRIM_ARENA_BYTES + 4096, 1,
	           "a bigger retail arena is never shrunk");
	expect_int(CustomTrackPolicy_PrimArenaBytes(1, CTR_CT_PRIM_ARENA_BYTES) == CTR_CT_PRIM_ARENA_BYTES, 1,
	           "an exactly-equal arena is unchanged");

	// --- the demand it was sized against -----------------------------------
	{
		const unsigned long skyWorst = 4uL * 2772uL * 0x1CuL;  // 310,464
		const unsigned long geomWorst = 2388uL * 4uL * 0x34uL; // 496,704: every quadblock, near LOD

		expect_int(CTR_CT_PRIM_ARENA_BYTES > skyWorst, 1, "the arena covers the worst sky frame");
		expect_int(CTR_CT_PRIM_ARENA_BYTES > geomWorst, 1, "and the whole level at near LOD");
		expect_int(CTR_CT_PRIM_ARENA_BYTES > skyWorst + geomWorst, 1,
		           "and both at once, which cannot actually happen");

		// The retail table could not have expressed this at any value.
		expect_int((255uL << 10) < skyWorst, 1, "the retail table's ceiling could not hold even the sky");
	}

	// --- the ceilings it was checked against --------------------------------
	{
		// GPU link tokens are handed out counting DOWN from this base, so every
		// registered range shares it (platform/native_gpu_links.c).
		const unsigned long tokenBudget = 0x00f00000uL; // 15,728,640
		const unsigned long otPair = 2uL * 0x2000uL;    // 1P/2P OT, MainInit_OTMem
		const unsigned long swapPair = 2uL * ((1uL << 12) | 0x18uL);
		const unsigned long registered = 2uL * CTR_CT_PRIM_ARENA_BYTES + otPair + swapPair;

		expect_int(registered < tokenBudget, 1, "both arenas plus OT and swapchain fit the token space");
		expect_int(registered * 3 < tokenBudget, 1, "with room to spare three times over");

		// MEMPACK, measured free during this race on the 8 MiB arena the
		// custom-track build already selects.
		{
			const unsigned long freeDuringRace = 5418356uL;
			const unsigned long cost = 2uL * (CTR_CT_PRIM_ARENA_BYTES - retailSlot6);

			expect_int(cost < freeDuringRace, 1, "the expansion fits the free MEMPACK measured in a real race");
			expect_int(cost * 2 < freeDuringRace, 1, "with more than half of it still free afterwards");
		}
	}
}

int main(void)
{
	test_sha256_vectors();
	test_hash_compare();
	test_pair_auto_expand();
	test_redirect_terms();
	test_cup_completion();
	test_serve_context();
	test_leg_count();
	test_box_verdict();
	test_measured_flags();
	test_st1_entry_present();
	test_prim_fits();
	test_prim_arena_bytes();
	test_fork_consistency();

	if (g_failures != 0)
	{
		printf("FAILED: %d assertion(s)\n", g_failures);
		return 1;
	}

	printf("test-custom-track-policy: all assertions held\n");
	return 0;
}
