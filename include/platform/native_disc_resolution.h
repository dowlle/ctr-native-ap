#ifndef NATIVE_DISC_RESOLUTION_H
#define NATIVE_DISC_RESOLUTION_H

// Freestanding decision logic for locating the game disc at startup (issue
// #334, implementation slice 2). The precedence, fallthrough, copy decision and
// path validation live here as plain functions over injected operations, so the
// host harness drives the same decisions production runs with stubbed
// filesystem and dialog calls. Production owns the real operations in
// platform/native_disc_resolution.c (native SDL3 picker, SDL confirmation,
// atomic copy, the real disc validator).
//
// Startup sequence this implements (contract correction 5): base and assets
// discovery happen without mounting, config.ini is read, then the disc is
// resolved explicit argument -> saved path -> assets folder, and only after the
// chosen disc validated is anything persisted.
//
// Nothing here includes SDL or touches a real file: every fact arrives through
// NativeDiscResolutionOps.

#include <stddef.h>

#include "platform/native_disc_image.h"
#include "platform/native_disc_limits.h"

// Conventional name for the disc copied into the assets folder.
#define NATIVE_DISC_DESTINATION_NAME "ctr-u.bin"

// Wizard wording, kept in one place so it is easy to change. Plain and short.
#define NATIVE_DISC_TEXT_PICK    "Select your Crash Team Racing (NTSC-U) disc image"
#define NATIVE_DISC_TEXT_COPY    "Copy this disc image into the game folder? If you choose No, the game will remember where it is."
#define NATIVE_DISC_TEXT_REPLACE "The file in the game folder is not a valid disc image. Replace it with the selected one?"
#define NATIVE_DISC_TEXT_INVALID "That file is not a valid Crash Team Racing NTSC-U disc image."

// Status lines the resolution emits through the reportStatus operation.
#define NATIVE_DISC_STATUS_EXPLICIT_INVALID "the requested disc image is not a valid Crash Team Racing NTSC-U disc image"
#define NATIVE_DISC_STATUS_SAVED_STALE      "the remembered disc image is no longer usable; looking in the game folder"
#define NATIVE_DISC_STATUS_COPY_FAILED      "could not copy the disc image into the game folder; the game will remember where it is"

// Where the active disc came from.
typedef enum
{
	NATIVE_DISC_SOURCE_NONE = 0,
	NATIVE_DISC_SOURCE_EXPLICIT,
	NATIVE_DISC_SOURCE_SAVED,
	NATIVE_DISC_SOURCE_ASSETS,
	NATIVE_DISC_SOURCE_PICKED
} NativeDiscSource;

// Validation of one persisted disc path value (not the file).
typedef enum
{
	NATIVE_DISC_PATH_OK = 0,
	NATIVE_DISC_PATH_TOO_LONG,
	NATIVE_DISC_PATH_CONTROL,
	NATIVE_DISC_PATH_INVALID_UTF8,
	NATIVE_DISC_PATH_NOT_LOSSLESS
} NativeDiscPathStatus;

static inline const char *NativeDiscPath_StatusText(NativeDiscPathStatus status)
{
	switch (status)
	{
	case NATIVE_DISC_PATH_OK:
		return "ok";
	case NATIVE_DISC_PATH_TOO_LONG:
		return "longer than the maximum path length";
	case NATIVE_DISC_PATH_CONTROL:
		return "contains a control character";
	case NATIVE_DISC_PATH_INVALID_UTF8:
		return "is not valid UTF-8";
	case NATIVE_DISC_PATH_NOT_LOSSLESS:
		return "has leading or trailing whitespace the ini format cannot keep";
	}

	return "is not a usable path";
}

static inline int NativeDisc_IsAsciiSpace(unsigned char c)
{
	return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\v') || (c == '\f') || (c == '\r');
}

// Strict UTF-8: rejects overlong encodings, UTF-16 surrogates and code points
// above U+10FFFF, the same contract the launch-request parser uses.
static inline int NativeDisc_Utf8Valid(const unsigned char *s, size_t len)
{
	size_t i = 0;

	while (i < len)
	{
		unsigned char c = s[i];

		if (c < 0x80)
		{
			i++;
			continue;
		}

		if ((c >= 0xC2) && (c <= 0xDF))
		{
			if ((i + 1 >= len) || ((s[i + 1] & 0xC0) != 0x80))
				return 0;
			i += 2;
			continue;
		}

		if ((c >= 0xE0) && (c <= 0xEF))
		{
			if (i + 2 >= len)
				return 0;
			if (((s[i + 1] & 0xC0) != 0x80) || ((s[i + 2] & 0xC0) != 0x80))
				return 0;
			if ((c == 0xE0) && (s[i + 1] < 0xA0))
				return 0;
			if ((c == 0xED) && (s[i + 1] >= 0xA0))
				return 0;
			i += 3;
			continue;
		}

		if ((c >= 0xF0) && (c <= 0xF4))
		{
			if (i + 3 >= len)
				return 0;
			if (((s[i + 1] & 0xC0) != 0x80) || ((s[i + 2] & 0xC0) != 0x80) || ((s[i + 3] & 0xC0) != 0x80))
				return 0;
			if ((c == 0xF0) && (s[i + 1] < 0x90))
				return 0;
			if ((c == 0xF4) && (s[i + 1] >= 0x90))
				return 0;
			i += 4;
			continue;
		}

		return 0;
	}

	return 1;
}

// Validate a value that is about to be persisted as the external disc path.
// Empty is fine (it means "no external disc"). A value is stored verbatim only
// when it fits, is valid UTF-8, carries no control characters and survives the
// ini round trip unchanged (no leading or trailing whitespace, which the loader
// trims). Anything else is rejected whole.
static inline NativeDiscPathStatus NativeDiscPath_Validate(const char *value, size_t len)
{
	size_t i;

	if ((value == NULL) || (len == 0))
		return NATIVE_DISC_PATH_OK;

	if (len > (size_t)NATIVE_DISC_PATH_CONTENT_MAX)
		return NATIVE_DISC_PATH_TOO_LONG;

	for (i = 0; i < len; i++)
	{
		unsigned char c = (unsigned char)value[i];

		if ((c < 0x20) || (c == 0x7F))
			return NATIVE_DISC_PATH_CONTROL;
	}

	if (!NativeDisc_Utf8Valid((const unsigned char *)value, len))
		return NATIVE_DISC_PATH_INVALID_UTF8;

	if (NativeDisc_IsAsciiSpace((unsigned char)value[0]) || NativeDisc_IsAsciiSpace((unsigned char)value[len - 1]))
		return NATIVE_DISC_PATH_NOT_LOSSLESS;

	return NATIVE_DISC_PATH_OK;
}

// Copy decision for the wizard (contract correction 6), decided before any
// question is asked:
//   * a valid destination is reused and never overwritten;
//   * an invalid destination needs a separate explicit replacement question;
//   * no destination asks the ordinary copy question.
typedef enum
{
	NATIVE_DISC_COPY_REUSE_DESTINATION = 0,
	NATIVE_DISC_COPY_ASK,
	NATIVE_DISC_COPY_ASK_REPLACE
} NativeDiscCopyPlan;

static inline NativeDiscCopyPlan NativeDisc_PlanCopy(int destinationExists, int destinationValid)
{
	if (destinationExists && destinationValid)
		return NATIVE_DISC_COPY_REUSE_DESTINATION;
	if (destinationExists)
		return NATIVE_DISC_COPY_ASK_REPLACE;
	return NATIVE_DISC_COPY_ASK;
}

// Injected operations. Production wires the real ones; the harness wires stubs.
typedef struct NativeDiscResolutionOps
{
	// 1 when a regular file exists at path.
	int (*fileExists)(void *ctx, const char *path);
	// Validate one candidate. On OK, chosenPath receives the candidate path and
	// production has mounted it; a failure leaves the active disc untouched.
	enum NativeDiscImageValidation (*validateCandidate)(void *ctx, const char *path, char *chosenPath, size_t chosenPathSize);
	// Scan the assets folder for a valid disc. Returns 1 and fills chosenPath on
	// success, 0 when the folder holds no valid disc.
	int (*scanAssets)(void *ctx, char *chosenPath, size_t chosenPathSize);
	// Show the native picker. Returns 1 and fills outPath when the user chose a
	// file, 0 on cancel or when the dialog API is unavailable.
	int (*pickDisc)(void *ctx, char *outPath, size_t outPathSize);
	// Ask a yes/no question. Returns 1 only for an affirmative answer.
	int (*confirm)(void *ctx, const char *question);
	// Copy source into destination through a temporary sibling, flush it,
	// validate the completed copy and atomically replace destination. Returns 1
	// on success with destination mounted; 0 on any failure with destination
	// untouched.
	int (*copyAndMount)(void *ctx, const char *source, const char *destination);
	// Emit one plain status line.
	void (*reportStatus)(void *ctx, const char *line);
} NativeDiscResolutionOps;

typedef struct NativeDiscResolutionRequest
{
	const char *explicitPath;    // --disc value, or NULL/empty
	const char *savedPath;       // g_config.discPath, or NULL/empty
	const char *destinationPath; // assets/ctr-u.bin
	int allowWizard;             // 0 for validation-only mode
} NativeDiscResolutionRequest;

typedef struct NativeDiscResolutionResult
{
	int found;                       // 1 when a valid disc is active
	NativeDiscSource source;
	enum NativeDiscImageValidation validation; // of the chosen disc, or last candidate
	int explicitInvalid;             // explicit path given but not valid (terminal)
	int staleSavedPath;              // saved path present but no longer usable
	int wizardRan;
	int cancelled;
	int destinationReused;           // existing valid destination reused
	int copyOffered;
	int copyAccepted;
	int replacementConfirmed;
	int persistExternal;             // save chosenPath as the external disc path
	char chosenPath[NATIVE_DISC_PATH_MAX];
	char pickedPath[NATIVE_DISC_PATH_MAX];
	char destinationPath[NATIVE_DISC_PATH_MAX];
} NativeDiscResolutionResult;

static inline void NativeDiscResolution_CopyString(char *dst, size_t dstSize, const char *src)
{
	if ((dst == NULL) || (dstSize == 0))
		return;

	if (src == NULL)
	{
		dst[0] = '\0';
		return;
	}

	while ((*src != '\0') && (dstSize > 1))
	{
		*dst++ = *src++;
		dstSize--;
	}

	*dst = '\0';
}

// The decision state machine. Returns 1 when a valid disc is active, 0
// otherwise. Writes nothing to config or connection state; the caller persists
// only after the chosen disc has also passed full asset validation.
static inline int NativeDiscResolution_Run(const NativeDiscResolutionOps *ops, void *ctx, const NativeDiscResolutionRequest *request,
                                           NativeDiscResolutionResult *result)
{
	result->found = 0;
	result->source = NATIVE_DISC_SOURCE_NONE;
	result->validation = NATIVE_DISC_IMAGE_OPEN_FAILED;
	result->explicitInvalid = 0;
	result->staleSavedPath = 0;
	result->wizardRan = 0;
	result->cancelled = 0;
	result->destinationReused = 0;
	result->copyOffered = 0;
	result->copyAccepted = 0;
	result->replacementConfirmed = 0;
	result->persistExternal = 0;
	result->chosenPath[0] = '\0';
	result->pickedPath[0] = '\0';
	result->destinationPath[0] = '\0';
	NativeDiscResolution_CopyString(result->destinationPath, sizeof(result->destinationPath), request->destinationPath);

	// 1. Explicit argument wins. An invalid explicit path is terminal: the user
	//    asked for a specific disc and the game must not quietly use another.
	if ((request->explicitPath != NULL) && (request->explicitPath[0] != '\0'))
	{
		result->validation = ops->validateCandidate(ctx, request->explicitPath, result->chosenPath, sizeof(result->chosenPath));
		if (result->validation == NATIVE_DISC_IMAGE_VALID)
		{
			result->found = 1;
			result->source = NATIVE_DISC_SOURCE_EXPLICIT;
			return 1;
		}

		result->explicitInvalid = 1;
		ops->reportStatus(ctx, NATIVE_DISC_STATUS_EXPLICIT_INVALID);
		return 0;
	}

	// 2. Remembered external path. Missing or invalid falls through to assets
	//    with a stale-path status.
	if ((request->savedPath != NULL) && (request->savedPath[0] != '\0'))
	{
		result->validation = ops->validateCandidate(ctx, request->savedPath, result->chosenPath, sizeof(result->chosenPath));
		if (result->validation == NATIVE_DISC_IMAGE_VALID)
		{
			result->found = 1;
			result->source = NATIVE_DISC_SOURCE_SAVED;
			return 1;
		}

		result->staleSavedPath = 1;
		ops->reportStatus(ctx, NATIVE_DISC_STATUS_SAVED_STALE);
	}

	// 3. The assets folder, exactly today's behavior.
	if (ops->scanAssets(ctx, result->chosenPath, sizeof(result->chosenPath)))
	{
		result->found = 1;
		result->source = NATIVE_DISC_SOURCE_ASSETS;
		return 1;
	}

	// 4. Wizard, only when nothing valid was found.
	if (!request->allowWizard)
		return 0;

	result->wizardRan = 1;
	for (;;)
	{
		if (!ops->pickDisc(ctx, result->pickedPath, sizeof(result->pickedPath)))
		{
			result->cancelled = 1;
			result->found = 0;
			result->source = NATIVE_DISC_SOURCE_NONE;
			return 0;
		}

		result->validation = ops->validateCandidate(ctx, result->pickedPath, result->chosenPath, sizeof(result->chosenPath));
		if (result->validation == NATIVE_DISC_IMAGE_VALID)
			break;

		ops->reportStatus(ctx, NATIVE_DISC_TEXT_INVALID);
	}

	result->found = 1;
	result->source = NATIVE_DISC_SOURCE_PICKED;

	// Copy decision (contract correction 6). A valid destination is reused and
	// never overwritten, even though the player just picked something else.
	{
		int destinationExists = 0;
		int destinationValid = 0;
		int accepted;

		if ((request->destinationPath != NULL) && (request->destinationPath[0] != '\0'))
		{
			char destinationCheck[NATIVE_DISC_PATH_MAX];

			destinationExists = ops->fileExists(ctx, request->destinationPath);
			if (destinationExists)
			{
				enum NativeDiscImageValidation destinationResult =
				    ops->validateCandidate(ctx, request->destinationPath, destinationCheck, sizeof(destinationCheck));
				destinationValid = (destinationResult == NATIVE_DISC_IMAGE_VALID);
				if (destinationValid)
				{
					NativeDiscResolution_CopyString(result->chosenPath, sizeof(result->chosenPath), request->destinationPath);
					result->destinationReused = 1;
					result->source = NATIVE_DISC_SOURCE_ASSETS;
					return 1;
				}
			}
		}

		result->copyOffered = 1;
		accepted = ops->confirm(ctx, destinationExists ? NATIVE_DISC_TEXT_REPLACE : NATIVE_DISC_TEXT_COPY);
		if (destinationExists)
			result->replacementConfirmed = accepted;

		if (accepted)
		{
			result->copyAccepted = 1;
			if (ops->copyAndMount(ctx, result->pickedPath, request->destinationPath))
			{
				NativeDiscResolution_CopyString(result->chosenPath, sizeof(result->chosenPath), request->destinationPath);
				result->source = NATIVE_DISC_SOURCE_ASSETS;
				return 1;
			}

			ops->reportStatus(ctx, NATIVE_DISC_STATUS_COPY_FAILED);
		}

		// Declined or copy failed: keep the picked external disc and remember
		// where it is. The destination stays untouched.
		result->source = NATIVE_DISC_SOURCE_PICKED;
		result->persistExternal = 1;
		return 1;
	}
}

// Production entry points (platform/native_disc_resolution.c). Declared here so
// production and the harness share the request/result contract; the harness
// never calls them, it drives NativeDiscResolution_Run with stubs instead.
int NativeDiscResolution_Resolve(const char *explicitPath, int allowWizard, NativeDiscResolutionResult *result);
void NativeDiscResolution_Commit(const NativeDiscResolutionResult *result);

#endif // NATIVE_DISC_RESOLUTION_H
