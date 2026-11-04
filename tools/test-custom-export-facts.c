// cc -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -I include -I . tools/test-custom-export-facts.c -o /tmp/test-custom-export-facts
//
// The descriptor export of the Custom Content page with the verifier built in
// (every build with CTR_CUSTOM_PACKAGES, the player client included). Rulebook
// track package rule 2 lists `weapon_crates` and `super_turbo_pads` as
// optional measured facts. The registry holds only a yes/no for crates and no
// super turbo pad field, so CustomTrackManager_RenderYaml measures both on the
// package's own bytes with the verifier the loader compares a descriptor
// against. A missing super_turbo_pads counts as true in logic, so the export
// must write it whenever it is measured.
//
// - The verifier harness's full LEV (8 spawns, AI paths, 1 weapon crate, no
//   mesh) exports `super_turbo_pads: false, weapon_crates: 1`.
// - A LEV with super turbo pad trigger quads and no instances exports
//   `super_turbo_pads: true, weapon_crates: 0`.
// - Files that no longer carry the package's hashes refuse the export, so no
//   fact is written from other bytes.
#define main content_verify_harness_main
#include "test-custom-content-verify.c"
#undef main

#include "platform/native_custom_track_manager.c"

static char s_levSha[NATIVE_SHA256_HEX_BYTES];

static void hex_of(const unsigned char *bytes, size_t len, char out[NATIVE_SHA256_HEX_BYTES])
{
	struct NativeSha256Ctx ctx;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
	NativeSha256_Init(&ctx);
	NativeSha256_Update(&ctx, bytes, len);
	NativeSha256_Final(&ctx, digest);
	NativeSha256_ToHex(digest, out);
}

// A registry entry for the bytes in `lev`, VRM "def". Only the Wumpa flag is
// re-measured by the scan; these LEVs carry no fruit, so it is 0.
static struct CustomTrackManagerPackage package_for(const char *id, int spawns)
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
		"",
		"cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34",
		"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
		2,
		3,
		1, 1, 1, 1, 0, 0, 0, 8, 2
	};
	package.id = id;
	package.levSha256 = s_levSha;
	package.flagSpawns = spawns;
	return package;
}

// Install `lev` and VRM "def" for `package` and bring it to Ready.
static int make_ready(const char *assets, const struct CustomTrackManagerPackage *package,
                      const unsigned char *lev, size_t levBytes, struct CustomTrackManagerStatus *status)
{
	if (!CustomTrackManager_PrepareFolder(assets, package, status)) return 0;
	write_bytes(status->levPath, lev, levBytes);
	write_bytes(status->vrmPath, "def", 3);
	CustomTrackManager_ScanPackage(assets, package, status);
	return CustomTrackManager_FinalizePackage(assets, package, status) && status->state == CTR_CT_MANAGER_READY;
}

int main(void)
{
	static const unsigned pads[][2] = {{QUAD_FLAG_TRIGGER, 0x02}, {QUAD_FLAG_TRIGGER, 0x01}};
	char root[] = "/tmp/ctr-export-facts-XXXXXX";
	char assets[CTR_CT_MANAGER_PATH_MAX];
	char yaml[CTR_CT_MANAGER_TEXT_MAX];
	unsigned char image[IMAGE_BYTES];
	unsigned char mesh[4096];
	struct CustomTrackManagerPackage package;
	struct CustomTrackManagerStatus status;
	size_t n;

	if (mkdtemp(root) == NULL) return 2;
	snprintf(assets, sizeof assets, "%s/assets", root);

	// Full LEV: 8 spawns, AI paths, one PU_RANDOM weapon crate, no mesh.
	n = build_lev(image, 1);
	hex_of(image, n, s_levSha);
	package = package_for("fixture-track", 8);
	expect(make_ready(assets, &package, image, n, &status), "full fixture is Ready");
	expect(CustomTrackManager_RenderYaml(&package, &status, NULL, yaml, sizeof yaml), "full fixture exports");
	expect(strstr(yaml, "capabilities: {time_trial: true, arcade: true, relic: false, ctr_tokens: true, wumpa: false, "
	                    "checkpoints: 2, ai_nav: true, super_turbo_pads: false, weapon_crates: 1}\n") != NULL,
	       "measured: no super turbo pads, one weapon crate, arcade with 8 spawns");

	// The bytes change after Ready: nothing is written from other bytes.
	image[n - 1] ^= 0xff;
	write_bytes(status.levPath, image, n);
	expect(!CustomTrackManager_RenderYaml(&package, &status, NULL, yaml, sizeof yaml),
	       "changed LEV bytes refuse the export");
	image[n - 1] ^= 0xff;
	write_bytes(status.levPath, image, n);
	expect(CustomTrackManager_RenderYaml(&package, &status, NULL, yaml, sizeof yaml), "restored bytes export again");
	remove(status.levPath);
	expect(!CustomTrackManager_RenderYaml(&package, &status, NULL, yaml, sizeof yaml),
	       "an unreadable LEV refuses the export");

	// A LEV with one super turbo pad trigger quad (and one normal pad) and no
	// instances: no weapon crates.
	n = build_mesh_lev(mesh, sizeof mesh, pads, 2, 0);
	hex_of(mesh, n, s_levSha);
	package = package_for("fixture-pads", 8);
	expect(make_ready(assets, &package, mesh, n, &status), "super turbo pad fixture is Ready");
	expect(CustomTrackManager_RenderYaml(&package, &status, NULL, yaml, sizeof yaml), "super turbo pad fixture exports");
	expect(strstr(yaml, ", super_turbo_pads: true, weapon_crates: 0}\n") != NULL,
	       "measured: super turbo pads, no weapon crates");

	printf("custom export facts: %d checks, %d failures\n", checks, failures);
	return failures != 0;
}
