// Relic target times on the Custom Content page (one generator row 10j): the
// manager-owned times file and the descriptor export. It compiles the real
// manager, drives Baby T Park 1.0.2 and a synthetic package through a Ready
// status, and checks that the export writes `relic: true` with
// `relic_targets` exactly when the author's times are stored and the package
// has relic crates, and refuses times the apworld would refuse.
//
// With CTR_RELIC_EXPORT_DIR set, the exported Baby T Park descriptors are
// also written there, so they can be passed through the apworld's admission.
//
//   cc -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -I include -I . tools/test-custom-relic-times.c -o /tmp/test-custom-relic-times
//   /tmp/test-custom-relic-times

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

static void expect_missing(const char *text, const char *needle, const char *name)
{
	checks++;
	if (text == NULL || strstr(text, needle) != NULL)
	{
		failures++;
		printf("FAIL %s (found %s)\n", name, needle);
	}
}

static void write_file(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");
	if (file == NULL || fwrite(text, 1, strlen(text), file) != strlen(text) || fclose(file) != 0)
	{
		printf("FATAL could not write %s\n", path);
		exit(2);
	}
}

static int read_file(const char *path, char *dst, size_t cap)
{
	FILE *file = fopen(path, "rb");
	size_t got;
	if (file == NULL)
		return 0;
	got = fread(dst, 1, cap - 1, file);
	fclose(file);
	dst[got] = '\0';
	return 1;
}

// A Ready status as a completed scan leaves it: the scanned hashes, sizes and
// the package receipt.
static void ready_status(const struct CustomTrackManagerPackage *package, const char *assets,
	                     unsigned long levBytes, unsigned long vrmBytes,
	                     struct CustomTrackManagerStatus *status)
{
	if (!Manager_BuildPaths(assets, package, status))
		exit(2);
	status->state = CTR_CT_MANAGER_READY;
	status->levBytes = levBytes;
	status->vrmBytes = vrmBytes;
	snprintf(status->actualLevSha256, sizeof status->actualLevSha256, "%s", package->levSha256);
	snprintf(status->actualVrmSha256, sizeof status->actualVrmSha256, "%s", package->vrmSha256);
	if (!Manager_PackageReceipt(package, status, status->verifiedPackageSha256))
		exit(2);
}

static void export_to_dir(const char *name, const char *yaml)
{
	const char *dir = getenv("CTR_RELIC_EXPORT_DIR");
	char path[CTR_CT_MANAGER_PATH_MAX];
	if (dir == NULL || *dir == '\0')
		return;
	snprintf(path, sizeof path, "%s/%s", dir, name);
	write_file(path, yaml);
}

int main(void)
{
	char tempTemplate[] = "/tmp/ctr-relic-times-XXXXXX";
	char *tempRoot = mkdtemp(tempTemplate);
	char assets[CTR_CT_MANAGER_PATH_MAX];
	char path[CTR_CT_MANAGER_PATH_MAX];
	char yaml[CTR_CT_MANAGER_TEXT_MAX];
	char text[CTR_CT_MANAGER_TEXT_MAX];
	const struct CustomTrackManagerPackage *park = CustomTrackManager_BabyTParkCurrent();
	struct CustomTrackManagerStatus status;
	struct CustomTrackManagerStatus saved;
	struct CustomTrackRelicTimes relic;
	int ticks[3];
	int centis;
	int ok;

	if (tempRoot == NULL || !Manager_Join(assets, sizeof assets, tempRoot, "assets"))
		return 2;

	// Hundredths and ticks: every time the page takes reads back as typed, and
	// stays inside the apworld's 1 to 3,456,000 ticks.
	ok = 1;
	for (centis = 1; centis <= CTR_CT_RELIC_CENTIS_MAX; centis++)
	{
		int t = CustomTrackManager_RelicTicksFromCentis(centis);
		if (t < 1 || t > CTR_CT_RELIC_TICKS_MAX || CustomTrackManager_RelicCentisFromTicks(t) != centis ||
		    t <= CustomTrackManager_RelicTicksFromCentis(centis - 1))
		{
			ok = 0;
			printf("centis %d -> ticks %d\n", centis, t);
			break;
		}
	}
	expect_int(ok, 1, "every M:SS.CC from 0:00.01 to 59:59.99 round-trips, strictly increasing");
	expect_int(CustomTrackManager_RelicTicksFromCentis(0), -1, "0:00.00 is not a time");
	expect_int(CustomTrackManager_RelicTicksFromCentis(CTR_CT_RELIC_CENTIS_MAX + 1), -1, "60:00.00 is refused");
	expect_int(CustomTrackManager_RelicTicksFromCentis(8000), 76800, "1:20.00 is 76,800 ticks");
	expect_int(CustomTrackManager_RelicTicksFromCentis(1), 10, "0:00.01 is 10 ticks, the first shown as .01");
	expect_int(CTR_CT_RELIC_TICKS_MAX, 3456000, "tick ceiling equals the apworld's RELIC_TICKS_MAX");

	// The apworld's admission rule: 1 to 3,456,000 each, strictly decreasing.
	{
		int good[3] = {76800, 67200, 57600};
		int equal[3] = {76800, 76800, 57600};
		int rising[3] = {57600, 67200, 76800};
		int zero[3] = {76800, 67200, 0};
		int over[3] = {3456001, 67200, 57600};
		int edge[3] = {3456000, 2, 1};
		expect_int(CustomTrackManager_RelicTimesValid(good), 1, "descending times are valid");
		expect_int(CustomTrackManager_RelicTimesValid(edge), 1, "the apworld's bounds are valid");
		expect_int(CustomTrackManager_RelicTimesValid(equal), 0, "Sapphire equal to Gold refused");
		expect_int(CustomTrackManager_RelicTimesValid(rising), 0, "Platinum slower than Sapphire refused");
		expect_int(CustomTrackManager_RelicTimesValid(zero), 0, "a zero time refused");
		expect_int(CustomTrackManager_RelicTimesValid(over), 0, "over one hour refused");
		expect_int(CustomTrackManager_RelicTimesValid(NULL), 0, "no times refused");
	}

	// Baby T Park 1.0.2, installed and Ready, with its original files.
	expect_int(park->flagRelicCrates, 1, "Baby T Park measures relic crates");
	expect_int(CustomTrackManager_PrepareFolder(assets, park, &status), 1, "package folder prepared");
	write_file(status.levPath, "lev bytes stand-in");
	write_file(status.vrmPath, "vrm bytes stand-in");
	write_file(status.manifestPath, "manifest stand-in");
	ready_status(park, assets, 2558168, 458808, &status);

	expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &status, &relic), 0, "no times stored yet");
	expect_int(relic.set, 0, "nothing loaded");
	expect_int(CustomTrackManager_RenderYaml(park, &status, &relic, yaml, sizeof yaml), 1, "export without times");
	expect_contains(yaml, "relic: false", "without times the export has no Relic Race");
	expect_missing(yaml, "relic_targets", "without times there are no relic_targets");
	expect_int(CustomTrackManager_RenderYaml(park, &status, NULL, yaml, sizeof yaml), 1, "export with no times given");
	expect_contains(yaml, "relic: false", "no times given is relic false");
	export_to_dir("baby-t-park-1.0.2-no-times.yaml", yaml);

	// Saving refuses what the apworld would refuse, and non-Ready packages.
	{
		int rising[3] = {57600, 67200, 76800};
		struct CustomTrackManagerStatus scanning = status;
		struct CustomTrackManagerPackage noCrates = *park;
		ticks[0] = 76800, ticks[1] = 67200, ticks[2] = 57600;
		expect_int(CustomTrackManager_SaveRelicTimes(assets, park, &status, rising), 0, "rising times not saved");
		scanning.state = CTR_CT_MANAGER_HASH_MISMATCH;
		expect_int(CustomTrackManager_SaveRelicTimes(assets, park, &scanning, ticks), 0, "not Ready: not saved");
		noCrates.flagRelicCrates = 0;
		expect_int(CustomTrackManager_SaveRelicTimes(assets, &noCrates, &status, ticks), 0,
		           "a package without relic crates takes no times");
		expect_int(Manager_RelicPath(assets, park, path, sizeof path), 1, "times file path");
		expect_int(access(path, F_OK) != 0, 1, "refused saves wrote nothing");
	}

	// Sapphire 1:20.00, Gold 1:10.00, Platinum 1:00.00, entered on the page.
	ticks[0] = CustomTrackManager_RelicTicksFromCentis(8000);
	ticks[1] = CustomTrackManager_RelicTicksFromCentis(7000);
	ticks[2] = CustomTrackManager_RelicTicksFromCentis(6000);
	expect_int(CustomTrackManager_SaveRelicTimes(assets, park, &status, ticks), 1, "valid times saved");
	expect_contains(path, "baby-t-park-1.0.2/manager-relic-times.json", "times sit in the package's manager folder");
	expect_int(read_file(path, text, sizeof text), 1, "times file readable");
	expect_contains(text, park->levSha256, "times name the LEV they were entered for");
	expect_contains(text, park->vrmSha256, "times name the VRM they were entered for");
	expect_int(read_file(status.levPath, text, sizeof text) && strcmp(text, "lev bytes stand-in") == 0, 1,
	           "the LEV is untouched");
	expect_int(read_file(status.vrmPath, text, sizeof text) && strcmp(text, "vrm bytes stand-in") == 0, 1,
	           "the VRM is untouched");
	expect_int(read_file(status.manifestPath, text, sizeof text) && strcmp(text, "manifest stand-in") == 0, 1,
	           "manifest.json is untouched");

	expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &status, &relic), 1, "stored times load");
	expect_int(relic.set && relic.ticks[0] == 76800 && relic.ticks[1] == 67200 && relic.ticks[2] == 57600, 1,
	           "loaded times equal the entered times");
	expect_int(CustomTrackManager_RenderYaml(park, &status, &relic, yaml, sizeof yaml), 1, "export with times");
	expect_contains(yaml, "capabilities: {time_trial: true, arcade: true, relic: true, ", "times make relic true");
	expect_contains(yaml, "    relic_targets: {sapphire: 76800, gold: 67200, platinum: 57600}\n",
	                "the times are relic_targets in ticks");
	expect_contains(yaml, "    revision: \"1.0.2\"\n", "export names 1.0.2");
	export_to_dir("baby-t-park-1.0.2-relic.yaml", yaml);

	expect_int(CustomTrackManager_SaveYaml(assets, park, &status, &relic, &saved), 1, "Save YAML with times");
	expect_int(read_file(saved.yamlPath, text, sizeof text), 1, "saved YAML readable");
	expect_int(strcmp(text, yaml), 0, "Save YAML writes the same descriptor as Copy YAML");

	// Set but invalid times refuse the export rather than writing a YAML the
	// generator would refuse.
	{
		struct CustomTrackRelicTimes bad = relic;
		bad.ticks[1] = bad.ticks[0];
		expect_int(CustomTrackManager_RenderYaml(park, &status, &bad, yaml, sizeof yaml), 0,
		           "equal Sapphire and Gold refuse the export");
		bad = relic;
		bad.ticks[2] = 0;
		expect_int(CustomTrackManager_RenderYaml(park, &status, &bad, yaml, sizeof yaml), 0,
		           "a zero Platinum time refuses the export");
	}

	// A package without relic crates never exports a Relic Race.
	{
		struct CustomTrackManagerPackage noCrates = *park;
		struct CustomTrackManagerStatus noCratesStatus;
		noCrates.flagRelicCrates = 0;
		ready_status(&noCrates, assets, 2558168, 458808, &noCratesStatus);
		expect_int(CustomTrackManager_RenderYaml(&noCrates, &noCratesStatus, &relic, yaml, sizeof yaml), 1,
		           "export without relic crates");
		expect_contains(yaml, "relic: false", "no relic crates, no Relic Race");
		expect_missing(yaml, "relic_targets", "no relic crates, no relic_targets");
	}

	// When the package's bytes change, the stored times stop applying.
	{
		struct CustomTrackManagerStatus changed = status;
		struct CustomTrackManagerPackage rebuilt = *park;
		struct CustomTrackManagerStatus rebuiltStatus;
		changed.actualVrmSha256[0] = changed.actualVrmSha256[0] == '0' ? '1' : '0';
		expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &changed, &relic), 0,
		           "other scanned bytes load no times");
		changed = status;
		changed.state = CTR_CT_MANAGER_HASH_MISMATCH;
		expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &changed, &relic), 0,
		           "a package that is not Ready loads no times");
		// Same folder, new bytes: the times were entered for the old files.
		rebuilt.levSha256 = "0000000000000000000000000000000000000000000000000000000000000000";
		ready_status(&rebuilt, assets, 2558168, 458808, &rebuiltStatus);
		expect_int(CustomTrackManager_LoadRelicTimes(assets, &rebuilt, &rebuiltStatus, &relic), 0,
		           "times entered for other bytes do not carry over");
		expect_int(CustomTrackManager_RenderYaml(&rebuilt, &rebuiltStatus, &relic, yaml, sizeof yaml), 1,
		           "the changed package still exports");
		expect_contains(yaml, "relic: false", "the changed package exports without a Relic Race");
		// Entering times again replaces the file for the new bytes.
		expect_int(CustomTrackManager_SaveRelicTimes(assets, &rebuilt, &rebuiltStatus, ticks), 1,
		           "new times saved for the new bytes");
		expect_int(CustomTrackManager_LoadRelicTimes(assets, &rebuilt, &rebuiltStatus, &relic), 1,
		           "new times load for the new bytes");
		expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &status, &relic), 0,
		           "and no longer for the old bytes");
		expect_int(CustomTrackManager_SaveRelicTimes(assets, park, &status, ticks), 1, "old bytes' times back");
	}

	// Only the exact text the manager writes loads.
	{
		char good[512];
		expect_int(read_file(path, good, sizeof good), 1, "times file read back");
		write_file(path, "{\"schema_version\": 1, \"lev_sha256\": \"x\"}\n");
		expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &status, &relic), 0, "a broken file loads nothing");
		snprintf(text, sizeof text, "%s", good);
		*strstr(text, "\"gold\": ") = '\0';
		strcat(text, "\"gold\":  67200, \"platinum\": 57600}\n");
		write_file(path, text);
		expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &status, &relic), 0, "a hand-edited file loads nothing");
		snprintf(text, sizeof text, "%s", good);
		*strstr(text, "\"gold\": ") = '\0';
		strcat(text, "\"gold\": 80000, \"platinum\": 57600}\n");
		write_file(path, text);
		expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &status, &relic), 0,
		           "stored times out of order load nothing");
		write_file(path, good);
		expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &status, &relic), 1, "the written text loads");
	}

	// Clear removes the times; export goes back to relic false.
	expect_int(CustomTrackManager_ClearRelicTimes(assets, park), 1, "times cleared");
	expect_int(access(path, F_OK) != 0, 1, "times file removed");
	expect_int(CustomTrackManager_ClearRelicTimes(assets, park), 1, "clearing nothing succeeds");
	expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &status, &relic), 0, "cleared times do not load");
	expect_int(CustomTrackManager_RenderYaml(park, &status, &relic, yaml, sizeof yaml), 1, "export after clear");
	expect_contains(yaml, "relic: false", "cleared times export no Relic Race");

	// 1.0.0 keeps its own folder, so 1.0.2's times never reach it.
	{
		const struct CustomTrackManagerPackage *legacy = CustomTrackManager_BabyTPark();
		struct CustomTrackManagerStatus legacyStatus;
		expect_int(CustomTrackManager_SaveRelicTimes(assets, park, &status, ticks), 1, "1.0.2 times again");
		expect_int(CustomTrackManager_PrepareFolder(assets, legacy, &legacyStatus), 1, "1.0.0 folder prepared");
		ready_status(legacy, assets, 2558168, 458808, &legacyStatus);
		expect_int(CustomTrackManager_LoadRelicTimes(assets, legacy, &legacyStatus, &relic), 0,
		           "1.0.0 has no times of its own");
		ticks[0] = CustomTrackManager_RelicTicksFromCentis(9999);
		ticks[1] = CustomTrackManager_RelicTicksFromCentis(8888);
		ticks[2] = CustomTrackManager_RelicTicksFromCentis(7777);
		expect_int(CustomTrackManager_SaveRelicTimes(assets, legacy, &legacyStatus, ticks), 1, "1.0.0 times saved");
		expect_int(CustomTrackManager_LoadRelicTimes(assets, legacy, &legacyStatus, &relic), 1, "1.0.0 times load");
		expect_int(CustomTrackManager_RenderYaml(legacy, &legacyStatus, &relic, yaml, sizeof yaml), 1,
		           "1.0.0 export with times");
		expect_contains(yaml, "relic_targets: {sapphire: 95991, gold: 85325, platinum: 74660}",
		                "odd hundredths round up to the tick the clock shows");
		export_to_dir("baby-t-park-1.0.0-relic.yaml", yaml);
		expect_int(CustomTrackManager_LoadRelicTimes(assets, park, &status, &relic), 1, "1.0.2 times unchanged");
		expect_int(relic.ticks[0], 76800, "1.0.2 Sapphire unchanged");
	}

	printf("test-custom-relic-times: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
