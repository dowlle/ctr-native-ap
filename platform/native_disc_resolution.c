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
#include "platform/native_disc_copy.h"
#include "platform/native_disc_image.h"
#include "platform/native_disc_resolution.h"
#include "platform/native_fs_utf8.h"

static int NativeDisc_RealFileExists(void *ctx, const char *path)
{
	(void)ctx;
	return NativeFs_FileExists(path);
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

// The picker callback can run on another thread, so completion and the chosen
// path are published through a per-invocation heap state rather than a stack
// frame or a shared static. Each request owns its state until its own callback
// has run, so an abandoned request can never write into state a later request
// reads.
struct NativeDiscPickState
{
	SDL_AtomicInt done;
	SDL_AtomicInt cancelled;
	char path[NATIVE_DISC_PATH_MAX];
};

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
	struct NativeDiscPickState *state;
	int ownVideo = 0;
	int answered;

	(void)ctx;

	// The picker needs the video subsystem, which the game has not started yet.
	// Bring it up just for the dialog and hand it back afterwards. A headless
	// host fails here, which is the immediate no-display fallback.
	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			fprintf(stderr, "[CTR Native] cannot show the disc picker: %s\n", SDL_GetError());
			return 0;
		}
		ownVideo = 1;
	}

	state = (struct NativeDiscPickState *)calloc(1, sizeof(*state));
	if (state == NULL)
	{
		if (ownVideo)
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
		return 0;
	}

	SDL_ShowOpenFileDialog(NativeDisc_PickCallback, state, NULL, filters, 1, NULL, false);

	// SDL invokes the callback exactly once per dialog, including when no dialog
	// backend is available (it gets a NULL filelist), so the state is safe to
	// free once done is set. There is deliberately no local timeout: SDL offers
	// no safe cancellation, and returning while the dialog is still live could
	// let a late callback touch freed state.
	while (!SDL_GetAtomicInt(&state->done))
	{
		SDL_PumpEvents();
		SDL_Delay(10);
	}

	answered = !SDL_GetAtomicInt(&state->cancelled);
	if (answered)
		NativeDiscResolution_CopyString(outPath, outPathSize, state->path);

	free(state);

	if (ownVideo)
		SDL_QuitSubSystem(SDL_INIT_VIDEO);

	return answered;
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

static int NativeDisc_RealCopyAndMount(void *ctx, const char *source, const char *destination)
{
	// The state machine and its real operations live in the separately testable
	// copy unit (platform/native_disc_copy.c). It creates a unique temporary
	// sibling exclusively, flushes it to disk, validates the completed copy and
	// replaces the destination atomically; any failure leaves the destination
	// untouched and removes only the temporary file this run owns.
	char tempPath[NATIVE_DISC_PATH_MAX + 32];
	NativeDiscCopyResult copyResult;

	copyResult = NativeDiscCopy_Run(&g_nativeDiscCopyRealOps, ctx, source, destination, tempPath, sizeof(tempPath));
	if (copyResult != NATIVE_DISC_COPY_OK)
	{
		fprintf(stderr, "[CTR Native] disc copy failed: %s\n", NativeDiscCopy_ResultText(copyResult));
		return 0;
	}

	// Mount the destination so this run uses it. Only reached after the copy
	// validated and replaced the destination.
	if (NativeDiscImage_ValidateCandidate(destination, 1) != NATIVE_DISC_IMAGE_VALID)
	{
		fprintf(stderr, "[CTR Native] copied disc did not validate: %s\n", NativeDiscImage_LastDetail());
		return 0;
	}

	return 1;
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

	if (!NativeDiscResolution_ShouldPersist(result))
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
