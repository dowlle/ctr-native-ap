#ifndef NATIVE_STARTUP_H
#define NATIVE_STARTUP_H

#include <stddef.h>

#include "platform/native_disc_resolution.h"

// Production startup resolution for the game disc (issue #334, slice 6). The
// whole sequence main.c used to open-code -- parse the disc arguments, load
// config.ini and the remembered disc path (disc-path.txt), resolve the disc
// through the wizard, run the final asset validation and persist a remembered
// external path -- lives in one production
// function here. main.c calls it; tools/test-unchanged-startup.c calls the same
// function with only the final retail asset validation stubbed, so the sequence
// cannot drift from production.
//
// The three injectable operations are the picker, the yes/no confirmations and
// the final full asset validation. Everything else (argument parsing, config
// load, disc-path store load and save, filesystem scan, candidate validation,
// copy and mount) is the real production code and is not injectable.

typedef struct NativeStartupArgs
{
	const char *explicitDiscPath; // --disc value, or NULL
	int validateDiscOnly;         // --validate-disc seen
} NativeStartupArgs;

typedef enum
{
	NATIVE_STARTUP_ARG_OK = 0,
	NATIVE_STARTUP_ARG_DUPLICATE_DISC,
	NATIVE_STARTUP_ARG_MISSING_DISC_VALUE
} NativeStartupArgStatus;

// Parse the disc arguments. Diagnostics are printed here so every caller
// reports the same way. Never touches the filesystem or SDL.
NativeStartupArgStatus NativeStartup_ParseArgs(int argc, char *const *argv, NativeStartupArgs *out);

typedef struct NativeStartupOps
{
	// Show the native disc picker. 1 and outPath when chosen, 0 otherwise.
	int (*pickDisc)(void *ctx, char *outPath, size_t outPathSize);
	// Ask a yes/no question. 1 only for an affirmative answer.
	int (*confirm)(void *ctx, const char *question);
	// Final full asset validation (retail BIGFILE/XA layout). 1 when the chosen
	// disc and assets are usable.
	int (*validateAssets)(void *ctx);
} NativeStartupOps;

typedef enum
{
	NATIVE_STARTUP_OK = 0,               // game may continue
	NATIVE_STARTUP_DISC_VALIDATE_OK,     // validation-only mode passed
	NATIVE_STARTUP_DISC_VALIDATE_FAILED, // validation-only mode failed
	NATIVE_STARTUP_ASSETS_INVALID        // disc resolved but assets failed
} NativeStartupStatus;

// Run the startup disc sequence. On NATIVE_STARTUP_OK or
// NATIVE_STARTUP_ASSETS_INVALID, resultOut (may be NULL) receives the disc
// resolution result. A remembered external disc path is persisted only after
// the assets validated.
NativeStartupStatus NativeStartup_ResolveDisc(const NativeStartupArgs *args, const NativeStartupOps *ops, void *ctx, NativeDiscResolutionResult *resultOut);

// The production final asset validation (NativeAssets_Validate), for main.c to
// pass as NativeStartupOps.validateAssets. The host harness substitutes its own
// stub while keeping every other step real.
int NativeStartup_ValidateRetailAssets(void *ctx);

#endif // NATIVE_STARTUP_H
