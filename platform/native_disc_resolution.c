// SDL-backed operations for the startup disc resolution (issue #334, slice 2):
// the native file picker and the yes/no confirmation. The precedence, the
// fallthrough, the copy decision and the startup sequence itself are
// freestanding (include/platform/native_disc_resolution.h and
// include/platform/native_startup.h); this file only supplies the two
// operations that need a windowing backend.
//
// Nothing here persists config or connection state.

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "platform/native_disc_picker.h"
#include "platform/native_disc_resolution.h"

// The picker callback can run on another thread, so completion and the chosen
// path are published through a per-invocation heap state rather than a stack
// frame or a shared static. The bounded wait and the abandon-on-timeout
// ownership rule live in the freestanding header, so the host harness can pin
// all three callback orders (before timeout, after timeout, never) with a fake
// backend. The two flags are guarded by an SDL mutex carried in the context.
typedef struct NativeDiscPickCtx
{
	SDL_Mutex *mutex;
	NativeDiscPickerState *state;
} NativeDiscPickCtx;

static int NativeDisc_PickerStart(void *ctx, NativeDiscPickerState *state);
static void NativeDisc_PickerPump(void *ctx);
static unsigned long long NativeDisc_PickerNow(void *ctx);
static void NativeDisc_PickerSleep(void *ctx, unsigned int ms);
static void NativeDisc_PickerRelease(void *ctx, NativeDiscPickerState *state);
static void NativeDisc_PickerLock(void *ctx);
static void NativeDisc_PickerUnlock(void *ctx);

static const NativeDiscPickerOps g_nativeDiscPickerOps = {
    NativeDisc_PickerStart,
    NativeDisc_PickerPump,
    NativeDisc_PickerNow,
    NativeDisc_PickerSleep,
    NativeDisc_PickerRelease,
    NativeDisc_PickerLock,
    NativeDisc_PickerUnlock,
};

static void SDLCALL NativeDisc_PickCallback(void *userdata, const char *const *filelist, int filter)
{
	NativeDiscPickCtx *pickCtx = (NativeDiscPickCtx *)userdata;

	(void)filter;

	NativeDiscPicker_Complete(pickCtx, &g_nativeDiscPickerOps, pickCtx->state,
	                          ((filelist != NULL) && (filelist[0] != NULL)) ? filelist[0] : NULL);
}

int NativeDiscResolution_RealPick(void *ctx, char *outPath, size_t outPathSize)
{
	int ownVideo = 0;
	int answered;
	NativeDiscPickCtx *pickCtx;

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

	// Heap allocated on purpose: a callback that arrives after the bounded wait
	// has abandoned this invocation must only ever touch this orphaned context
	// and its own state, never a stack frame that has gone out of scope.
	pickCtx = (NativeDiscPickCtx *)calloc(1, sizeof(*pickCtx));
	if (pickCtx == NULL)
	{
		if (ownVideo)
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
		return 0;
	}

	pickCtx->mutex = SDL_CreateMutex();
	pickCtx->state = (NativeDiscPickerState *)calloc(1, sizeof(*pickCtx->state));
	if ((pickCtx->mutex == NULL) || (pickCtx->state == NULL))
	{
		if (pickCtx->mutex != NULL)
			SDL_DestroyMutex(pickCtx->mutex);
		free(pickCtx->state);
		free(pickCtx);
		if (ownVideo)
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
		return 0;
	}

	// The wait releases the context when the callback completed in time; on a
	// timeout it abandons it and the late callback releases it instead.
	answered = NativeDiscPicker_Wait(pickCtx, &g_nativeDiscPickerOps, pickCtx->state, NATIVE_DISC_PICK_TIMEOUT_MS, outPath, outPathSize);

	if (ownVideo)
		SDL_QuitSubSystem(SDL_INIT_VIDEO);

	return answered;
}

static int NativeDisc_PickerStart(void *ctx, NativeDiscPickerState *state)
{
	static const SDL_DialogFileFilter filters[] = {
	    {"PlayStation disc images", "bin"},
	};
	NativeDiscPickCtx *pickCtx = (NativeDiscPickCtx *)ctx;

	(void)state;

	// The callback receives the context, which carries the state.
	SDL_ShowOpenFileDialog(NativeDisc_PickCallback, pickCtx, NULL, filters, 1, NULL, false);
	return 1;
}

static void NativeDisc_PickerPump(void *ctx)
{
	(void)ctx;
	SDL_PumpEvents();
}

static unsigned long long NativeDisc_PickerNow(void *ctx)
{
	(void)ctx;
	return (unsigned long long)SDL_GetTicks();
}

static void NativeDisc_PickerSleep(void *ctx, unsigned int ms)
{
	(void)ctx;
	SDL_Delay(ms);
}

static void NativeDisc_PickerLock(void *ctx)
{
	NativeDiscPickCtx *pickCtx = (NativeDiscPickCtx *)ctx;

	SDL_LockMutex(pickCtx->mutex);
}

static void NativeDisc_PickerUnlock(void *ctx)
{
	NativeDiscPickCtx *pickCtx = (NativeDiscPickCtx *)ctx;

	SDL_UnlockMutex(pickCtx->mutex);
}

static void NativeDisc_PickerRelease(void *ctx, NativeDiscPickerState *state)
{
	NativeDiscPickCtx *pickCtx = (NativeDiscPickCtx *)ctx;

	// The state is released through the context that owns the mutex. A late
	// callback is handed the same context, so the state it abandons is freed
	// exactly once. The mutex itself is destroyed here too, after the callback
	// has finished with it.
	(void)state;

	free(pickCtx->state);
	pickCtx->state = NULL;

	if (pickCtx->mutex != NULL)
	{
		SDL_DestroyMutex(pickCtx->mutex);
		pickCtx->mutex = NULL;
	}

	free(pickCtx);
}

int NativeDiscResolution_RealConfirm(void *ctx, const char *question)
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
