// Manager-light filesystem/state harness. It compiles the real manager and
// drives a synthetic registered package through every Alpha6 foundation state;
// no third-party track asset is needed or copied.
//
//   cc -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -I include -I . tools/test-custom-track-manager.c -o /tmp/test-custom-track-manager
//   /tmp/test-custom-track-manager

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CTR_CUSTOM_TRACKS 1
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
		"0.2.0-alpha6",
		"0.2.0-alpha6",
		// sha256("abc") and sha256("def")
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
		"cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34",
		"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
		2,
		7,
		1, 1, 1, 1, 0, 0, 8, 35
	};
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
	struct CustomTrackManagerPackage package = fixture_package();
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

	write_file(status.levPath, "abc");
	expect_int(CustomTrackManager_ScanPackage(assets, &package, &status),
	           CTR_CT_MANAGER_MISSING_FILES, "one source file is Missing Files");
	expect_contains(status.detail, "track.vrm", "missing role is named");

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

	invalid.id = "../escape";
	expect_int(CustomTrackManager_ScanPackage(assets, &invalid, &status),
	           CTR_CT_MANAGER_UNSUPPORTED, "unsafe registry id is rejected before path construction");

	printf("test-custom-track-manager: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
