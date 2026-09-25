// Manager-light filesystem/state harness. It compiles the real manager and
// drives a synthetic registered package through every Alpha6 foundation state;
// no third-party track asset is needed or copied.
//
// Built with -DCTR_AP too, so it also exercises CustomTrackManager_SaveYaml's
// AP log line: AP_LogLine is stubbed below to capture into s_apLogCaptured
// instead of linking the real ap_hooks.c.
//
//   cc -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -DCTR_AP -I include -I . tools/test-custom-track-manager.c -o /tmp/test-custom-track-manager
//   /tmp/test-custom-track-manager

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CTR_CUSTOM_TRACKS 1

#ifdef CTR_AP
static char s_apLogCaptured[4096];
void AP_LogLine(const char *msg)
{
	size_t used = strlen(s_apLogCaptured);
	strncat(s_apLogCaptured, msg, sizeof(s_apLogCaptured) - used - 1);
}
#endif

#include "platform/native_custom_track_manager.c"

static int checks;
static int failures;

static void expect_int(int got, int want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		printf("FAIL %s (got %d, want %d)\n", name, got, want);
	}
}

static void expect_contains(const char *text, const char *needle, const char *name)
{
	checks++;
	if (text == NULL || strstr(text, needle) == NULL)
	{
		failures++;
		printf("FAIL %s (missing %s)\n", name, needle);
	}
}

static void write_bytes(const char *path, const unsigned char *bytes, size_t len)
{
	FILE *file = fopen(path, "wb");
	if (file == NULL || fwrite(bytes, 1, len, file) != len || fclose(file) != 0)
	{
		printf("FATAL could not write fixture %s\n", path);
		exit(2);
	}
}

// ── synthetic LEV images ───────────────────────────────────────────────────
//
// The fixture LEV used to be the three bytes "abc", which was enough while the
// manager only hashed it. It now WALKS the file to measure wumpa_collectible, so
// the fixture has to be a real relocatable image: a four-byte pointer-map offset,
// a `struct Level` header carrying numInstances and ptrInstDefs, an instance
// table of 0x40-byte records, and a pointer map at the end.
//
// Built here rather than checked in as a binary so a reader can see exactly which
// bytes produce which measurement, and so a case can be varied by argument.
#define FIXTURE_LEV_INSTDEFS 0x200
#define FIXTURE_LEV_MAX (FIXTURE_LEV_INSTDEFS + 64 * 0x40 + 4)

static void put_u32(unsigned char *buf, size_t offset, unsigned long value)
{
	buf[offset + 0] = (unsigned char)(value & 0xFF);
	buf[offset + 1] = (unsigned char)((value >> 8) & 0xFF);
	buf[offset + 2] = (unsigned char)((value >> 16) & 0xFF);
	buf[offset + 3] = (unsigned char)((value >> 24) & 0xFF);
}

// `fruitCrates` PU_FRUIT_CRATE instances, `looseFruit` PU_WUMPA_FRUIT, and
// `others` instances of an unrelated model so the walk is shown to be selecting
// rather than counting everything it sees.
static size_t build_lev(unsigned char *buf, int fruitCrates, int looseFruit,
	                     int others)
{
	const int total = fruitCrates + looseFruit + others;
	const size_t tableBytes = (size_t)total * 0x40;
	const size_t ptrMapOffset = FIXTURE_LEV_INSTDEFS + tableBytes;
	const size_t payload = ptrMapOffset + 4;
	int i;
	int written = 0;

	memset(buf, 0, payload + 4);
	put_u32(buf, 0, (unsigned long)ptrMapOffset);
	put_u32(buf, 4 + 0x0C, (unsigned long)total);
	put_u32(buf, 4 + 0x10, FIXTURE_LEV_INSTDEFS);
	for (i = 0; i < fruitCrates; i++, written++)
		put_u32(buf, 4 + FIXTURE_LEV_INSTDEFS + (size_t)written * 0x40 + 0x3C,
		        CTR_CT_MODEL_FRUIT_CRATE);
	for (i = 0; i < looseFruit; i++, written++)
		put_u32(buf, 4 + FIXTURE_LEV_INSTDEFS + (size_t)written * 0x40 + 0x3C,
		        CTR_CT_MODEL_WUMPA_FRUIT);
	for (i = 0; i < others; i++, written++)
		put_u32(buf, 4 + FIXTURE_LEV_INSTDEFS + (size_t)written * 0x40 + 0x3C,
		        0x08 /* PU_RANDOM_CRATE: a weapon box pays no fruit */);
	return payload + 4;
}

static void hex_digest(const unsigned char *bytes, size_t len,
	                    char out[NATIVE_SHA256_HEX_BYTES])
{
	struct NativeSha256Ctx ctx;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
	NativeSha256_Init(&ctx);
	NativeSha256_Update(&ctx, bytes, len);
	NativeSha256_Final(&ctx, digest);
	NativeSha256_ToHex(digest, out);
}

// The fixture LEV: two fruit crates and nothing else, which is exactly ten
// guaranteed fruit -- the threshold itself, so the fixture proves the boundary
// is inclusive every time it is used.
static unsigned char s_fixtureLev[FIXTURE_LEV_MAX];
static size_t s_fixtureLevLen;
static char s_fixtureLevSha[NATIVE_SHA256_HEX_BYTES];

static void write_fixture_lev(const char *path)
{
	write_bytes(path, s_fixtureLev, s_fixtureLevLen);
}

static void write_file(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");
	if (file == NULL || fwrite(text, 1, strlen(text), file) != strlen(text) || fclose(file) != 0)
	{
		printf("FATAL could not write fixture %s\n", path);
		exit(2);
	}
}

static void read_file(const char *path, char *dst, size_t cap)
{
	FILE *file = fopen(path, "rb");
	size_t got;
	if (file == NULL)
	{
		printf("FATAL could not read fixture %s\n", path);
		exit(2);
	}
	got = fread(dst, 1, cap - 1, file);
	dst[got] = '\0';
	fclose(file);
}

static struct CustomTrackManagerPackage fixture_package(void)
{
	struct CustomTrackManagerPackage package = {
		"fixture-track",
		"11111111-2222-4333-8444-555555555555",
		"1.2.3",
		"Fixture Track",
		"Fixture Author",
		"https://example.invalid/tracks/fixture",
		"https://example.invalid/api/tracks/fixture/downloads",
		"0.2.0-alpha6",
		"0.2.0-alpha6",
		// the synthetic LEV built above, and sha256("def") for the VRM
		"",
		"cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34",
		"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
		2,
		7,
		// crates .. ghosts, wumpa_collectible, spawns, checkpoints. The fixture
		// LEV measures exactly ten guaranteed fruit, so the registry says true.
		1, 1, 1, 1, 0, 0, 1, 8, 35
	};
	package.levSha256 = s_fixtureLevSha;
	return package;
}

static struct CustomTrackManagerRequirement requirement_for(const struct CustomTrackManagerPackage *package)
{
	struct CustomTrackManagerRequirement requirement = {
		package->id,
		package->packageUuid,
		package->version,
		package->minimumClientVersion,
		package->minimumApworldVersion,
		package->levSha256,
		package->vrmSha256,
		package->navigationUuid,
		package->navigationRevision,
		package->laps,
		0,
		package->flagCrates,
		package->flagCtrLetters,
		package->flagRelicCrates,
		package->flagAiNav,
		package->flagMinimap,
		package->flagGhosts,
		package->flagWumpaCollectible,
		package->flagSpawns,
		package->flagCheckpoints
	};
	return requirement;
}

int main(void)
{
	char tempTemplate[] = "/tmp/ctr-ct-manager-XXXXXX";
	char *tempRoot = mkdtemp(tempTemplate);
	char assets[CTR_CT_MANAGER_PATH_MAX];
	char yaml[CTR_CT_MANAGER_TEXT_MAX];
	char saved[CTR_CT_MANAGER_TEXT_MAX];
	struct CustomTrackManagerPackage package;
	struct CustomTrackWumpaMeasurement measured;

	// The fixture LEV and its digest have to exist before the fixture package
	// names them, because the package IS the digest.
	s_fixtureLevLen = build_lev(s_fixtureLev, 2, 0, 3);
	hex_digest(s_fixtureLev, s_fixtureLevLen, s_fixtureLevSha);
	package = fixture_package();
	struct CustomTrackManagerPackage invalid = package;
	struct CustomTrackManagerRequirement requirement = requirement_for(&package);
	struct CustomTrackManagerStatus status;
	struct CustomTrackManagerStatus exported;
	const struct CustomTrackManagerPackage *baby = CustomTrackManager_BabyTPark();

	if (tempRoot == NULL || !Manager_Join(assets, sizeof assets, tempRoot, "assets"))
		return 2;

	expect_contains(baby->packageUuid, "60d5a8a8-b69a-4f6a-a0d8-9a43d91e3f2e",
	                "Baby T package identity is permanent");
	expect_int(strcmp(baby->packageUuid, baby->navigationUuid) != 0, 1,
	           "package and navigation identities stay separate");
	expect_int(CustomTrackManager_ScanPackage(assets, &package, &status),
	           CTR_CT_MANAGER_NOT_INSTALLED, "absent package is Not Installed");
	expect_contains(status.detail, "Rescan", "absent status is actionable");

	expect_int(CustomTrackManager_PrepareFolder(assets, &package, &status), 1,
	           "prepare creates deterministic folder skeleton");
	expect_int(status.state, CTR_CT_MANAGER_NOT_INSTALLED,
	           "prepared empty folder stays Not Installed");

	write_fixture_lev(status.levPath);
	expect_int(CustomTrackManager_ScanPackage(assets, &package, &status),
	           CTR_CT_MANAGER_MISSING_FILES, "one source file is Missing Files");
	expect_contains(status.detail, "VRM", "missing role is named");

	write_file(status.vrmPath, "wrong");
	expect_int(CustomTrackManager_ScanPackage(assets, &package, &status),
	           CTR_CT_MANAGER_HASH_MISMATCH, "wrong source file is Hash Mismatch");
	expect_int(CustomTrackManager_FinalizePackage(assets, &package, &status), 0,
	           "mismatched content cannot create a manifest");

	write_file(status.vrmPath, "def");
	expect_int(CustomTrackManager_ScanPackage(assets, &package, &status),
	           CTR_CT_MANAGER_MANIFEST_MISSING, "recognized pair awaits local manifest");
	expect_int(CustomTrackManager_RenderYaml(&package, &status, yaml, sizeof yaml), 0,
	           "unfinalized package cannot export YAML");

	expect_int(CustomTrackManager_FinalizePackage(assets, &package, &status), 1,
	           "finalize writes the canonical manifest");
	expect_int(status.state, CTR_CT_MANAGER_READY, "finalized package is Ready");
	expect_int(CustomTrackManager_RenderYaml(&package, &status, yaml, sizeof yaml), 1,
	           "Ready package renders YAML");
	expect_contains(yaml, "custom_tracks:\n  fixture-track:",
	                "YAML is one combined custom_tracks mapping");
	expect_contains(yaml, "boxes: false", "Alpha6 export fails boxes closed");
	expect_contains(yaml, "package_uuid: 11111111-2222-4333-8444-555555555555",
	                "YAML carries package provenance");
	expect_contains(yaml, "navigation:\n      uuid: aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
	                "YAML carries navigation identity separately");
	expect_contains(yaml, package.levSha256, "YAML carries verified LEV identity");
	{
		// Ready describes the bytes that were scanned. Exporting it under a
		// package with other digests (the old menu paired a Ready 1.0.2 scan
		// with the 1.0.0 handle) must be refused, not written.
		struct CustomTrackManagerPackage other = package;
		struct CustomTrackManagerStatus crossed = status;
		other.levSha256 = CustomTrackManager_BabyTPark()->levSha256;
		expect_int(CustomTrackManager_RenderYaml(&other, &status, yaml, sizeof yaml), 0,
		           "Ready status cannot export another package's LEV identity");
		expect_int(CustomTrackManager_SaveYaml(assets, &other, &status, &crossed), 0,
		           "Ready status cannot save another package's LEV identity");
		other = package;
		other.vrmSha256 = CustomTrackManager_BabyTPark()->vrmSha256;
		expect_int(CustomTrackManager_RenderYaml(&other, &status, yaml, sizeof yaml), 0,
		           "Ready status cannot export another package's VRM identity");
		expect_int(CustomTrackManager_RenderYaml(&package, &status, yaml, sizeof yaml), 1,
		           "refused cross export leaves the scanned package exportable");
	}

	// Preflight only recognizes release-owned registry entries. The fixture is
	// useful for scanner tests, but it must not become selectable by a seed.
	expect_int(CustomTrackManager_Preflight(assets, &requirement, 1, &status),
	           CTR_CT_MANAGER_UNSUPPORTED, "unregistered package requirement is refused");

	requirement = requirement_for(baby);
	expect_int(CustomTrackManager_Preflight(assets, &requirement, 1, &status),
	           CTR_CT_MANAGER_NOT_INSTALLED, "recognized seed requirement reaches installed-content scan");
	requirement.boxes = 1;
	expect_int(CustomTrackManager_Preflight(assets, &requirement, 1, &status),
	           CTR_CT_MANAGER_INCOMPATIBLE, "Alpha6 seed requirement cannot enable AP boxes");
	requirement.boxes = 0;
	requirement.navigationRevision++;
	expect_int(CustomTrackManager_Preflight(assets, &requirement, 1, &status),
	           CTR_CT_MANAGER_INCOMPATIBLE, "navigation revision drift is incompatible");
	expect_int(CustomTrackManager_ScanPackage(assets, &package, &status),
	           CTR_CT_MANAGER_READY, "fixture remains Ready after registry refusal tests");

	expect_int(CustomTrackManager_SaveYaml(assets, &package, &status, &exported), 1,
	           "Ready package saves YAML");
	read_file(exported.yamlPath, saved, sizeof saved);
	expect_int(strcmp(yaml, saved), 0, "clipboard-ready and file YAML are byte-identical");
#ifdef CTR_AP
	expect_contains(s_apLogCaptured, exported.yamlPath,
	                 "saved YAML path is logged in full, untruncated");
#endif

	// The on-screen message area is tiny; a short path passes through as-is,
	// and a very long Windows-style path is left-truncated with a leading
	// "..." so the file name and nearest folders stay visible instead of
	// overflowing the buffer or the display.
	{
		char shortDisplay[96];
		char longDisplay[96];
		char tinyDisplay[8];
		const char *shortPath = "C:\\Users\\Player\\Documents\\custom_tracks.generated.yaml";
		const char *longPath =
			"C:\\Users\\SomePlayerWithAVeryLongWindowsUserName\\Documents\\My Games\\"
			"CTR Archipelago\\assets\\tracks\\baby-t-park-1.0.2\\custom_tracks.generated.yaml";

		CustomTrackManager_FormatSavedPathForDisplay(shortPath, shortDisplay, sizeof shortDisplay);
		expect_int(strcmp(shortDisplay, "Saved to: C:\\Users\\Player\\Documents\\custom_tracks.generated.yaml"), 0,
		           "short path displays in full");

		CustomTrackManager_FormatSavedPathForDisplay(longPath, longDisplay, sizeof longDisplay);
		expect_int((int)strlen(longDisplay) <= CTR_CT_SAVED_PATH_DISPLAY_MAX, 1,
		           "long path display stays within the message-area budget");
		expect_contains(longDisplay, "...", "long path display is left-truncated with an ellipsis");
		expect_contains(longDisplay, "custom_tracks.generated.yaml",
		                 "long path display keeps the file name visible");
		expect_contains(longDisplay, "baby-t-park-1.0.2",
		                 "long path display keeps the nearest folder visible");
		expect_int(strstr(longDisplay, "SomePlayerWithAVeryLongWindowsUserName") == NULL, 1,
		           "long path display drops the far (left) end of the path, not the near end");

		// A destination buffer smaller than the display budget must still come
		// back NUL-terminated and never overrun -- the whole point of bounding
		// every snprintf here.
		CustomTrackManager_FormatSavedPathForDisplay(longPath, tinyDisplay, sizeof tinyDisplay);
		expect_int((int)strlen(tinyDisplay) < (int)sizeof tinyDisplay, 1,
		           "tiny destination buffer is not overrun");
	}

	write_file(status.manifestPath, "{}\n");
	expect_int(CustomTrackManager_ScanPackage(assets, &package, &status),
	           CTR_CT_MANAGER_MANIFEST_INVALID, "changed manifest is rejected strictly");
	expect_int(CustomTrackManager_FinalizePackage(assets, &package, &status), 1,
	           "Verify repairs only the manager-owned manifest");

	write_file(status.levPath, "changed after ready");
	expect_int(CustomTrackManager_ScanPackage(assets, &package, &status),
	           CTR_CT_MANAGER_HASH_MISMATCH, "content drift returns to mismatch");
	expect_int(CustomTrackManager_FinalizePackage(assets, &package, &status), 0,
	           "content drift cannot be papered over with a new manifest");

	// Project Saphi preserves creator-facing UUID filenames. A player can copy
	// those downloads into original unchanged; discovery is by extension + the
	// release-owned hash, not by a magic track.lev / track.vrm spelling.
	{
		char original[CTR_CT_MANAGER_PATH_MAX];
		char canonicalLev[CTR_CT_MANAGER_PATH_MAX];
		char canonicalVrm[CTR_CT_MANAGER_PATH_MAX];
		char uuidLev[CTR_CT_MANAGER_PATH_MAX];
		char uuidVrm[CTR_CT_MANAGER_PATH_MAX];
		Manager_Join(original, sizeof original, status.packageRoot, "original");
		Manager_Join(canonicalLev, sizeof canonicalLev, original, "track.lev");
		Manager_Join(canonicalVrm, sizeof canonicalVrm, original, "track.vrm");
		remove(canonicalLev);
		remove(canonicalVrm);
		Manager_Join(uuidLev, sizeof uuidLev, original, "aaaaaaaa-1111-4222-8333-bbbbbbbbbbbb_v1.2.3.lev");
		Manager_Join(uuidVrm, sizeof uuidVrm, original, "cccccccc-1111-4222-8333-dddddddddddd_v1.2.3.vrm");
		write_fixture_lev(uuidLev);
		write_file(uuidVrm, "def");
		expect_int(CustomTrackManager_ScanPackage(assets, &package, &status),
		           CTR_CT_MANAGER_MANIFEST_INVALID, "UUID source filenames are discovered by verified content");
		expect_contains(status.levPath, "aaaaaaaa-1111-4222-8333-bbbbbbbbbbbb_v1.2.3.lev",
		                "discovered LEV path preserves the Saphi filename");
		expect_int(CustomTrackManager_FinalizePackage(assets, &package, &status), 1,
		           "finalize repairs the manifest for discovered filenames");
		read_file(status.manifestPath, saved, sizeof saved);
		expect_contains(saved, "original/aaaaaaaa-1111-4222-8333-bbbbbbbbbbbb_v1.2.3.lev",
		                "manifest records the verified source filename");
	}

	invalid.id = "../escape";
	expect_int(CustomTrackManager_ScanPackage(assets, &invalid, &status),
	           CTR_CT_MANAGER_UNSUPPORTED, "unsafe registry id is rejected before path construction");

	// ── wumpa_collectible: the measurement itself ──────────────────────────
	//
	// Driven directly, on images built for each case, so what the counter does
	// is pinned independently of the scan flow that consumes it.
	{
		unsigned char lev[FIXTURE_LEV_MAX];
		char path[CTR_CT_MANAGER_PATH_MAX];
		size_t len;

		Manager_Join(path, sizeof path, tempRoot, "measure.lev");

		// A fruit crate pays 5..8; the floor is what counts, so two crates are
		// exactly ten and one crate is not.
		len = build_lev(lev, 2, 0, 0);
		write_bytes(path, lev, len);
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 1,
		           "a well-formed LEV is measurable");
		expect_int(measured.fruitCrates, 2, "both fruit crates are counted");
		expect_int(measured.guaranteedFruit, 10,
		           "a fruit crate contributes its guaranteed floor of five");
		expect_int(measured.collectible, 1, "ten guaranteed fruit is collectible");

		len = build_lev(lev, 1, 0, 0);
		write_bytes(path, lev, len);
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 1,
		           "one crate is still measurable");
		expect_int(measured.guaranteedFruit, 5, "one crate guarantees five");
		expect_int(measured.collectible, 0,
		           "five guaranteed fruit cannot reach ten");

		// Loose fruit are worth one each and are counted alongside crates.
		len = build_lev(lev, 1, 5, 0);
		write_bytes(path, lev, len);
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 1,
		           "a mixed LEV is measurable");
		expect_int(measured.looseFruit, 5, "loose fruit are counted");
		expect_int(measured.guaranteedFruit, 10, "crates and loose fruit sum");
		expect_int(measured.collectible, 1, "the sum can reach the threshold");

		// A track full of weapon boxes has crates and no route to ten fruit.
		// This is the case the whole capability exists for: `crates` would say
		// true here and the measurement says false.
		len = build_lev(lev, 0, 0, 20);
		write_bytes(path, lev, len);
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 1,
		           "a crate-only LEV is measurable");
		expect_int(measured.fruitCrates, 0, "weapon boxes are not fruit crates");
		expect_int(measured.collectible, 0,
		           "crate instances alone are not a route to ten fruit");

		// A level with no instances at all measures zero, which is an ANSWER.
		len = build_lev(lev, 0, 0, 0);
		write_bytes(path, lev, len);
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 1,
		           "an empty instance table is measurable, not an error");
		expect_int(measured.collectible, 0, "an empty level has no fruit");

		// ── refusals. Each one is "not measurable", never "no fruit". ──
		expect_int(CustomTrackManager_MeasureWumpa("/nonexistent/track.lev",
		                                           &measured), 0,
		           "an absent file is not measurable");

		write_file(path, "abc");
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 0,
		           "a file too small to hold a header is not measurable");

		// Pointer map beyond the payload: every bound is derived from it, so it
		// is checked before anything is read through it.
		len = build_lev(lev, 2, 0, 0);
		put_u32(lev, 0, 0x7FFFFFFF);
		write_bytes(path, lev, len);
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 0,
		           "a pointer map outside the payload is not measurable");

		// Instance table running past the pointer map.
		len = build_lev(lev, 2, 0, 0);
		put_u32(lev, 4 + 0x0C, 4096);
		write_bytes(path, lev, len);
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 0,
		           "an instance table overrunning the payload is not measurable");

		// An implausible instance count is bounded before it sizes a walk.
		len = build_lev(lev, 2, 0, 0);
		put_u32(lev, 4 + 0x0C, 0xFFFFFFFFul);
		write_bytes(path, lev, len);
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 0,
		           "an absurd instance count is refused rather than walked");

		// ptrInstDefs pointing into the pointer map is out of bounds too.
		len = build_lev(lev, 2, 0, 0);
		put_u32(lev, 4 + 0x10, 0xFFFFFF00ul);
		write_bytes(path, lev, len);
		expect_int(CustomTrackManager_MeasureWumpa(path, &measured), 0,
		           "an instance pointer past the pointer map is not measurable");

		remove(path);
	}

	// ── the measurement is a gate on the scan, not just a number ───────────
	{
		struct CustomTrackManagerPackage lying = fixture_package();
		unsigned char lev[FIXTURE_LEV_MAX];
		char levSha[NATIVE_SHA256_HEX_BYTES];
		size_t len;
		struct CustomTrackManagerStatus wumpaStatus;

		// Restore the fixture package to a Ready state first: the UUID-filename
		// block above left the discovered names in place.
		expect_int(CustomTrackManager_ScanPackage(assets, &package, &wumpaStatus),
		           CTR_CT_MANAGER_READY, "fixture is Ready before the capability tests");
		expect_int(wumpaStatus.wumpaMeasured, 1,
		           "a Ready package always carries a measurement");
		expect_int(wumpaStatus.wumpa.collectible, 1,
		           "the fixture LEV measures collectible");
		read_file(wumpaStatus.manifestPath, saved, sizeof saved);
		expect_contains(saved, "\"wumpa_collectible\": true",
		                "the manifest exports the measured capability");
		expect_int(CustomTrackManager_RenderYaml(&package, &wumpaStatus, yaml,
		                                         sizeof yaml), 1,
		           "Ready package still renders YAML");
		expect_contains(yaml, "wumpa_collectible: true",
		                "the YAML fragment exports the measured capability");

		// A registry entry that claims a capability the installed bytes do not
		// have is refused. The digests match, so this can only mean the registry
		// was measured against different bytes -- and a seed would then mint a
		// per-track Wumpa check for a track that cannot pay it.
		len = build_lev(lev, 1, 0, 0); // five guaranteed fruit: not collectible
		hex_digest(lev, len, levSha);
		lying.levSha256 = levSha;
		write_bytes(wumpaStatus.levPath, lev, len);
		expect_int(CustomTrackManager_ScanPackage(assets, &lying, &wumpaStatus),
		           CTR_CT_MANAGER_UNSUPPORTED,
		           "a registry claim the bytes do not support is refused");
		expect_contains(wumpaStatus.detail, "wumpa_collectible",
		                "the refusal names the capability that disagreed");
		expect_int(CustomTrackManager_FinalizePackage(assets, &lying, &wumpaStatus), 0,
		           "a disagreeing package cannot be finalized past the refusal");

		// The same bytes with an honest registry entry are Ready.
		lying.flagWumpaCollectible = 0;
		expect_int(CustomTrackManager_FinalizePackage(assets, &lying, &wumpaStatus), 1,
		           "an honest false measurement is a perfectly valid package");
		read_file(wumpaStatus.manifestPath, saved, sizeof saved);
		expect_contains(saved, "\"wumpa_collectible\": false",
		                "the manifest exports a false measurement too");
	}

	// A seed may not redefine the capability any more than it may redefine a
	// digest: preflight compares it like every other measured flag.
	{
		struct CustomTrackManagerRequirement wumpaReq = requirement_for(baby);
		struct CustomTrackManagerStatus wumpaStatus;
		wumpaReq.flagWumpaCollectible = !baby->flagWumpaCollectible;
		expect_int(CustomTrackManager_Preflight(assets, &wumpaReq, 1, &wumpaStatus),
		           CTR_CT_MANAGER_INCOMPATIBLE,
		           "a seed cannot redefine the measured wumpa capability");
	}

	{
		// The page's default is the current Saphi release; 1.0.0 stays a
		// separate, still matchable profile with its own folder.
		const struct CustomTrackManagerPackage *cur = CustomTrackManager_BabyTParkCurrent();
		struct CustomTrackManagerStatus ready;
		expect_int(cur == &s_babyTParkCurrent, 1, "current getter returns the 1.0.2 profile");
		expect_contains(cur->version, "1.0.2", "current profile is 1.0.2");
		expect_contains(baby->version, "1.0.0", "legacy getter stays 1.0.0");
		memset(&ready, 0, sizeof ready);
		ready.state = CTR_CT_MANAGER_READY;
		snprintf(ready.actualLevSha256, sizeof ready.actualLevSha256, "%s", cur->levSha256);
		snprintf(ready.actualVrmSha256, sizeof ready.actualVrmSha256, "%s", cur->vrmSha256);
		expect_int(CustomTrackManager_RenderYaml(cur, &ready, yaml, sizeof yaml), 1,
		           "Ready 1.0.2 scan exports under the 1.0.2 handle");
		expect_contains(yaml, "package_version: 1.0.2", "1.0.2 export names 1.0.2");
		expect_contains(yaml, cur->levSha256, "1.0.2 export carries the 1.0.2 LEV digest");
		expect_int(CustomTrackManager_RenderYaml(baby, &ready, yaml, sizeof yaml), 0,
		           "Ready 1.0.2 scan cannot be exported as 1.0.0");
		snprintf(ready.actualLevSha256, sizeof ready.actualLevSha256, "%s", baby->levSha256);
		snprintf(ready.actualVrmSha256, sizeof ready.actualVrmSha256, "%s", baby->vrmSha256);
		expect_int(CustomTrackManager_RenderYaml(baby, &ready, yaml, sizeof yaml), 1,
		           "Ready 1.0.0 scan still exports as 1.0.0");
		expect_int(CustomTrackManager_RenderYaml(cur, &ready, yaml, sizeof yaml), 0,
		           "Ready 1.0.0 scan cannot be exported as 1.0.2");
	}
	{
		struct CustomTrackManagerRequirement current = requirement_for(&s_babyTParkCurrent);
		struct CustomTrackManagerRequirement legacy = requirement_for(baby);
		struct CustomTrackManagerStatus currentStatus, legacyStatus;
		expect_int(CustomTrackManager_MatchingPackage(&current) == &s_babyTParkCurrent, 1,
		           "current exact profile selected");
		expect_int(CustomTrackManager_MatchingPackage(&legacy) == baby, 1,
		           "legacy exact profile still selected");
		Manager_BuildPaths(assets, &s_babyTParkCurrent, &currentStatus);
		Manager_BuildPaths(assets, baby, &legacyStatus);
		expect_contains(currentStatus.packageRoot, "baby-t-park-1.0.2", "current isolated folder");
		expect_int(strcmp(currentStatus.packageRoot, legacyStatus.packageRoot) != 0, 1,
		           "current and legacy roots differ");
		current.levSha256 = legacy.levSha256;
		expect_int(CustomTrackManager_MatchingPackage(&current) == NULL, 1,
		           "cross-version digest mix refused");
		current = requirement_for(&s_babyTParkCurrent);
		current.navigationUuid = legacy.navigationUuid;
		expect_int(CustomTrackManager_MatchingPackage(&current) == NULL, 1,
		           "old navigation identity refused for current pair");
		current = requirement_for(&s_babyTParkCurrent);
		current.minimumClientVersion = legacy.minimumClientVersion;
		expect_int(CustomTrackManager_MatchingPackage(&current) == NULL, 1,
		           "old feature floor refused for current profile");
	}
	printf("test-custom-track-manager: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
