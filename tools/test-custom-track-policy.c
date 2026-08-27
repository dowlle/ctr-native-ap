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
//      cup that did not redirect always keeps its four legs.
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
	test_fork_consistency();

	if (g_failures != 0)
	{
		printf("FAILED: %d assertion(s)\n", g_failures);
		return 1;
	}

	printf("test-custom-track-policy: all assertions held\n");
	return 0;
}
