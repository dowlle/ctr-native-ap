// Production startup disc sequence (issue #334, slice 6). See
// include/platform/native_startup.h for the contract. This unit is deliberately
// SDL-free: the picker and the confirmations arrive through NativeStartupOps, so
// the host harness compiles this file and drives the same sequence production
// does, with only the final asset validation replaced.

#include "platform/native_startup.h"

#include <stdio.h>
#include <string.h>

#include "platform/native_assets.h"
#include "platform/native_config.h"
#include "platform/native_disc_copy.h"
#include "platform/native_disc_image.h"
#include "platform/native_disc_path_store.h"
#include "platform/native_fs_utf8.h"

// ── real (non-injectable) resolution operations ──────────────────────────────

static int NativeStartup_FileExists(void *ctx, const char *path)
{
	(void)ctx;
	return NativeFs_FileExists(path);
}

static enum NativeDiscImageValidation NativeStartup_ValidateCandidate(void *ctx, const char *path, char *chosenPath, size_t chosenPathSize)
{
	enum NativeDiscImageValidation result = NativeDiscImage_ValidateCandidate(path, 1);

	(void)ctx;

	if (result == NATIVE_DISC_IMAGE_VALID)
		NativeDiscResolution_CopyString(chosenPath, chosenPathSize, NativeDiscImage_GetPath());
	else
		fprintf(stderr, "[CTR Native] disc candidate rejected: %s\n", NativeDiscImage_LastDetail());

	return result;
}

static int NativeStartup_ScanAssets(void *ctx, char *chosenPath, size_t chosenPathSize)
{
	(void)ctx;

	if (!NativeAssets_MountDiscFromAssetsDir())
		return 0;

	NativeDiscResolution_CopyString(chosenPath, chosenPathSize, NativeDiscImage_GetPath());
	return 1;
}

static int NativeStartup_CopyAndMount(void *ctx, const char *source, const char *destination)
{
	char tempPath[NATIVE_DISC_PATH_MAX + 32];
	NativeDiscCopyResult copyResult;

	(void)ctx;

	copyResult = NativeDiscCopy_Run(&g_nativeDiscCopyRealOps, ctx, source, destination, tempPath, sizeof(tempPath));
	if (copyResult == NATIVE_DISC_COPY_DIRECTORY_FLUSH_WARNING)
	{
		// The rename already replaced the destination; only the directory
		// durability follow-up failed. Warn and keep the copied disc.
		fprintf(stderr, "[CTR Native] disc copy warning: %s\n", NativeDiscCopy_ResultText(copyResult));
	}
	else if (copyResult != NATIVE_DISC_COPY_OK)
	{
		fprintf(stderr, "[CTR Native] disc copy failed: %s\n", NativeDiscCopy_ResultText(copyResult));
		return 0;
	}

	if (NativeDiscImage_ValidateCandidate(destination, 1) != NATIVE_DISC_IMAGE_VALID)
	{
		fprintf(stderr, "[CTR Native] copied disc did not validate: %s\n", NativeDiscImage_LastDetail());
		return 0;
	}

	return 1;
}

static void NativeStartup_Report(void *ctx, const char *line)
{
	(void)ctx;
	printf("[CTR Native] %s\n", line);
}

// ── arguments ───────────────────────────────────────────────────────────────

NativeStartupArgStatus NativeStartup_ParseArgs(int argc, char *const *argv, NativeStartupArgs *out)
{
	int discArgumentSeen = 0;
	int argIndex;

	out->explicitDiscPath = NULL;
	out->validateDiscOnly = 0;

	for (argIndex = 1; argIndex < argc; argIndex++)
	{
		if (strcmp(argv[argIndex], "--disc") == 0)
		{
			if (discArgumentSeen)
			{
				fprintf(stderr, "[CTR Native] --disc was given more than once; give it a single disc image path\n");
				return NATIVE_STARTUP_ARG_DUPLICATE_DISC;
			}
			if ((argIndex + 1) >= argc)
			{
				fprintf(stderr, "[CTR Native] --disc needs a disc image path, for example --disc \"C:\\Games\\ctr-u.bin\"\n");
				return NATIVE_STARTUP_ARG_MISSING_DISC_VALUE;
			}
			out->explicitDiscPath = argv[++argIndex];
			discArgumentSeen = 1;
		}
		else if (strcmp(argv[argIndex], "--validate-disc") == 0)
		{
			out->validateDiscOnly = 1;
		}
	}

	return NATIVE_STARTUP_ARG_OK;
}

// ── persistence ─────────────────────────────────────────────────────────────

void NativeDiscResolution_Commit(const NativeDiscResolutionResult *result)
{
	if (!NativeDiscResolution_ShouldPersist(result))
		return;

	// The remembered path goes into its own file (disc-path.txt), not config.ini:
	// an options save and a remembered disc path have nothing to do with each
	// other, and a store that cannot be written must not put the settings file
	// at risk. The store validates the path and reports its own refusals, and a
	// failure here is a warning: the next launch simply asks for the disc again.
	(void)NativeDiscPathStore_Save(result->chosenPath);
}

// ── the sequence ────────────────────────────────────────────────────────────

int NativeStartup_ValidateRetailAssets(void *ctx)
{
	(void)ctx;
	return NativeAssets_Validate();
}

NativeStartupStatus NativeStartup_ResolveDisc(const NativeStartupArgs *args, const NativeStartupOps *ops, void *ctx, NativeDiscResolutionResult *resultOut)
{
	NativeDiscResolutionOps resolutionOps;
	NativeDiscResolutionRequest request;
	NativeDiscResolutionResult result;
	char destination[NATIVE_DISC_PATH_MAX];
	char savedPath[NATIVE_DISC_PATH_MAX];
	int discFound;

	// Contract correction 5 order: base and assets discovery already happened
	// without mounting, config.ini and the remembered disc path are read here,
	// then the disc is resolved, and only after the chosen disc validated is
	// anything persisted.
	NativeConfig_Load();

	// The remembered disc path lives in its own file next to config.ini. A
	// missing, empty or unusable store just means "no remembered disc" and the
	// resolution falls through to the assets folder exactly as before.
	if (!NativeDiscPathStore_Load(savedPath, sizeof(savedPath)))
		savedPath[0] = '\0';

	if (!NativeAssets_BuildPath(NATIVE_DISC_DESTINATION_NAME, destination, sizeof(destination)))
		destination[0] = '\0';

	resolutionOps.fileExists = NativeStartup_FileExists;
	resolutionOps.validateCandidate = NativeStartup_ValidateCandidate;
	resolutionOps.scanAssets = NativeStartup_ScanAssets;
	resolutionOps.pickDisc = ops->pickDisc;
	resolutionOps.confirm = ops->confirm;
	resolutionOps.copyAndMount = NativeStartup_CopyAndMount;
	resolutionOps.reportStatus = NativeStartup_Report;

	request.explicitPath = args->explicitDiscPath;
	request.savedPath = savedPath;
	request.destinationPath = destination;
	request.allowWizard = !args->validateDiscOnly;

	discFound = NativeDiscResolution_Run(&resolutionOps, ctx, &request, &result);

	if (resultOut != NULL)
		*resultOut = result;

	if (args->validateDiscOnly)
	{
		// Report the disc the game would use, write neither config nor
		// connection state.
		if (discFound)
		{
			const char *discSource =
			    result.source == NATIVE_DISC_SOURCE_EXPLICIT ? "explicit" :
			    result.source == NATIVE_DISC_SOURCE_SAVED    ? "saved" :
			    result.source == NATIVE_DISC_SOURCE_ASSETS   ? "assets" : "picked";
			printf("[CTR Native] disc validation passed (%s): %s\n", discSource, result.chosenPath);
			fflush(stdout);
			return NATIVE_STARTUP_DISC_VALIDATE_OK;
		}

		printf("[CTR Native] disc validation failed\n");
		fflush(stdout);
		return NATIVE_STARTUP_DISC_VALIDATE_FAILED;
	}

	if (!ops->validateAssets(ctx))
	{
		// The caller (main.c) reports this through the platform message box when
		// there is no terminal; the sequence itself only signals the outcome.
		return NATIVE_STARTUP_ASSETS_INVALID;
	}

	NativeDiscResolution_Commit(&result);
	return NATIVE_STARTUP_OK;
}
