// Production side of the startup disc resolution (issue #334, slice 2): the
// real operations the freestanding decision logic in
// include/platform/native_disc_resolution.h drives. The native SDL3 file
// picker, the SDL confirmation, the atomic copy and the single-candidate
// validator live here; the precedence, fallthrough, copy decision and path
// validation stay in the header so the host harness can pin them with stubs.
//
// Nothing here persists config or connection state. NativeDiscResolution_Commit
// is called by main.c only after the chosen disc has also passed full asset
// validation.

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "platform/native_assets.h"
#include "platform/native_config.h"
#include "platform/native_disc_image.h"
#include "platform/native_disc_resolution.h"

static int NativeDisc_RealFileExists(void *ctx, const char *path)
{
	FILE *file;

	(void)ctx;

	if (path == NULL)
		return 0;

	file = fopen(path, "rb");
	if (file == NULL)
		return 0;

	fclose(file);
	return 1;
}

static enum NativeDiscImageValidation NativeDisc_RealValidate(void *ctx, const char *path, char *chosenPath, size_t chosenPathSize)
{
	enum NativeDiscImageValidation result = NativeDiscImage_ValidateCandidate(path, 1);

	(void)ctx;

	if (result == NATIVE_DISC_IMAGE_VALID)
		NativeDiscResolution_CopyString(chosenPath, chosenPathSize, NativeDiscImage_GetPath());
	else
		fprintf(stderr, "[CTR Native] disc candidate rejected: %s\n", NativeDiscImage_LastDetail());

	return result;
}

static int NativeDisc_RealScanAssets(void *ctx, char *chosenPath, size_t chosenPathSize)
{
	(void)ctx;

	if (!NativeAssets_MountDiscFromAssetsDir())
		return 0;

	NativeDiscResolution_CopyString(chosenPath, chosenPathSize, NativeDiscImage_GetPath());
	return 1;
}

// The dialog callback can run on another thread, so completion and the chosen
// path are published through a file-scope state rather than a stack frame. A
// late callback after a bounded wait therefore writes to live memory.
struct NativeDiscPickState
{
	SDL_AtomicInt done;
	SDL_AtomicInt cancelled;
	char path[NATIVE_DISC_PATH_MAX];
};

static struct NativeDiscPickState s_nativeDiscPickState;

static void SDLCALL NativeDisc_PickCallback(void *userdata, const char *const *filelist, int filter)
{
	struct NativeDiscPickState *state = (struct NativeDiscPickState *)userdata;

	(void)filter;

	if ((filelist == NULL) || (filelist[0] == NULL))
	{
		SDL_SetAtomicInt(&state->cancelled, 1);
		SDL_SetAtomicInt(&state->done, 1);
		return;
	}

	NativeDiscResolution_CopyString(state->path, sizeof(state->path), filelist[0]);
	SDL_SetAtomicInt(&state->done, 1);
}

static int NativeDisc_RealPick(void *ctx, char *outPath, size_t outPathSize)
{
	static const SDL_DialogFileFilter filters[] = {
	    {"PlayStation disc images", "bin"},
	};
	int ownVideo = 0;
	Uint64 deadline;

	(void)ctx;

	SDL_SetAtomicInt(&s_nativeDiscPickState.done, 0);
	SDL_SetAtomicInt(&s_nativeDiscPickState.cancelled, 0);
	s_nativeDiscPickState.path[0] = '\0';

	// The picker needs the video subsystem, which the game has not started yet.
	// Bring it up just for the dialog and hand it back afterwards.
	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			fprintf(stderr, "[CTR Native] cannot show the disc picker: %s\n", SDL_GetError());
			return 0;
		}
		ownVideo = 1;
	}

	SDL_ShowOpenFileDialog(NativeDisc_PickCallback, &s_nativeDiscPickState, NULL, filters, 1, NULL, false);

	// Bounded wait: a real player gets minutes, a broken or headless dialog
	// backend never blocks startup forever.
	deadline = SDL_GetTicks() + 300000u;
	while (!SDL_GetAtomicInt(&s_nativeDiscPickState.done) && (SDL_GetTicks() < deadline))
	{
		SDL_PumpEvents();
		SDL_Delay(10);
	}

	if (ownVideo)
		SDL_QuitSubSystem(SDL_INIT_VIDEO);

	if (!SDL_GetAtomicInt(&s_nativeDiscPickState.done))
	{
		fprintf(stderr, "[CTR Native] disc picker did not answer in time\n");
		return 0;
	}

	if (SDL_GetAtomicInt(&s_nativeDiscPickState.cancelled))
		return 0;

	NativeDiscResolution_CopyString(outPath, outPathSize, s_nativeDiscPickState.path);
	return 1;
}

static int NativeDisc_RealConfirm(void *ctx, const char *question)
{
	static const SDL_MessageBoxButtonData buttons[] = {
	    {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"},
	    {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"},
	};
	SDL_MessageBoxData data;
	int buttonID = 0;

	(void)ctx;

	memset(&data, 0, sizeof(data));
	data.flags = SDL_MESSAGEBOX_INFORMATION;
	data.window = NULL;
	data.title = "Crash Team Racing";
	data.message = question;
	data.numbuttons = (int)(sizeof(buttons) / sizeof(buttons[0]));
	data.buttons = buttons;
	data.colorScheme = NULL;

	if (!SDL_ShowMessageBox(&data, &buttonID))
	{
		fprintf(stderr, "[CTR Native] cannot ask the disc question: %s\n", SDL_GetError());
		return 0;
	}

	return buttonID == 1;
}

static int NativeDisc_FlushFile(FILE *file)
{
	if (fflush(file) != 0)
		return 0;

#if defined(_WIN32)
	(void)_commit(_fileno(file));
#else
	(void)fsync(fileno(file));
#endif

	return 1;
}

static int NativeDisc_ReplaceFile(const char *tempPath, const char *destinationPath)
{
#if defined(_WIN32)
	return MoveFileExA(tempPath, destinationPath, MOVEFILE_REPLACE_EXISTING) != 0;
#else
	return rename(tempPath, destinationPath) == 0;
#endif
}

static int NativeDisc_RealCopyAndMount(void *ctx, const char *source, const char *destination)
{
	char tempPath[NATIVE_DISC_PATH_MAX + 8];
	FILE *in = NULL;
	FILE *out = NULL;
	char buffer[65536];
	size_t count;
	int ok = 0;

	(void)ctx;

	if ((source == NULL) || (destination == NULL) || (destination[0] == '\0'))
		return 0;

	if (snprintf(tempPath, sizeof(tempPath), "%s.tmp", destination) >= (int)sizeof(tempPath))
		return 0;

	in = fopen(source, "rb");
	if (in == NULL)
		return 0;

	out = fopen(tempPath, "wb");
	if (out == NULL)
	{
		fclose(in);
		return 0;
	}

	while ((count = fread(buffer, 1, sizeof(buffer), in)) > 0)
	{
		if (fwrite(buffer, 1, count, out) != count)
			goto done;
	}

	if (ferror(in))
		goto done;

	if (!NativeDisc_FlushFile(out))
		goto done;

	if (fclose(out) != 0)
	{
		out = NULL;
		goto done;
	}
	out = NULL;

	// Validate the completed copy before it can replace anything.
	if (NativeDiscImage_ValidateCandidate(tempPath, 0) != NATIVE_DISC_IMAGE_VALID)
		goto done;

	if (!NativeDisc_ReplaceFile(tempPath, destination))
		goto done;

	// Mount the destination so this run uses it.
	if (NativeDiscImage_ValidateCandidate(destination, 1) != NATIVE_DISC_IMAGE_VALID)
		goto done;

	ok = 1;

done:
	if (in != NULL)
		fclose(in);
	if (out != NULL)
		fclose(out);
	if (!ok)
		remove(tempPath);
	return ok;
}

static void NativeDisc_RealReport(void *ctx, const char *line)
{
	(void)ctx;
	printf("[CTR Native] %s\n", line);
}

int NativeDiscResolution_Resolve(const char *explicitPath, int allowWizard, NativeDiscResolutionResult *result)
{
	static const NativeDiscResolutionOps ops = {
	    NativeDisc_RealFileExists,
	    NativeDisc_RealValidate,
	    NativeDisc_RealScanAssets,
	    NativeDisc_RealPick,
	    NativeDisc_RealConfirm,
	    NativeDisc_RealCopyAndMount,
	    NativeDisc_RealReport,
	};
	NativeDiscResolutionRequest request;
	char destination[NATIVE_DISC_PATH_MAX];

	if (!NativeAssets_BuildPath(NATIVE_DISC_DESTINATION_NAME, destination, sizeof(destination)))
		destination[0] = '\0';

	request.explicitPath = explicitPath;
	request.savedPath = g_config.discPath;
	request.destinationPath = destination;
	request.allowWizard = allowWizard;

	return NativeDiscResolution_Run(&ops, NULL, &request, result);
}

void NativeDiscResolution_Commit(const NativeDiscResolutionResult *result)
{
	NativeDiscPathStatus pathStatus;

	if ((result == NULL) || !result->persistExternal || (result->chosenPath[0] == '\0'))
		return;

	// Only a path the ini format can keep verbatim is persisted; anything else
	// is reported and left unsaved rather than stored lossily.
	pathStatus = NativeDiscPath_Validate(result->chosenPath, strlen(result->chosenPath));
	if (pathStatus != NATIVE_DISC_PATH_OK)
	{
		fprintf(stderr, "[CTR Native] disc path not saved: %s\n", NativeDiscPath_StatusText(pathStatus));
		return;
	}

	NativeDiscResolution_CopyString(g_config.discPath, sizeof(g_config.discPath), result->chosenPath);
	NativeConfig_Save();
}
