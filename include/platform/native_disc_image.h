#ifndef NATIVE_DISC_IMAGE_H
#define NATIVE_DISC_IMAGE_H

#include <macros.h>

#include <stddef.h>

struct NativeDiscImageFile
{
	u32 lba;
	u32 size;
};

// Result of validating one candidate disc image (issue #334, slice 2). The
// single-candidate validator below is the only place that decides whether a
// file really is the NTSC-U Crash Team Racing disc: raw MODE2/2352 sectors, a
// valid ISO9660 volume and boot id SCUS_944.26. Explicit argument, saved path,
// assets scan and validation-only mode all funnel through it.
enum NativeDiscImageValidation
{
	NATIVE_DISC_IMAGE_VALID = 0,
	NATIVE_DISC_IMAGE_OPEN_FAILED,
	NATIVE_DISC_IMAGE_NOT_MODE2,
	NATIVE_DISC_IMAGE_NO_BOOT_ID,
	NATIVE_DISC_IMAGE_WRONG_REGION
};

int NativeDiscImage_Init(const char *assetsDir);
int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut);
int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst);
int NativeDiscImage_ReadRawSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst);
int NativeDiscImage_ReadFileBytes(const char *path, int rawSectors, u8 **dataOut, int *sizeOut);

// Validate one candidate. keepMounted != 0 makes a valid candidate the active
// disc (the previous mount, if any, is closed); keepMounted == 0 is a read-only
// check that always restores the active disc. On any failure the active disc is
// left exactly as it was, so a failed candidate never destroys a good mount.
enum NativeDiscImageValidation NativeDiscImage_ValidateCandidate(const char *path, int keepMounted);

// Human-readable reason for a validation result. The region case gets the
// detected serial and release named via NativeDiscImage_LastDetail.
const char *NativeDiscImage_ValidationText(enum NativeDiscImageValidation result);

// Detail for the most recent NativeDiscImage_ValidateCandidate failure (empty
// on success). Never contains more than the candidate path and disc identity.
const char *NativeDiscImage_LastDetail(void);

// Path of the currently mounted disc, or an empty string when none is mounted.
const char *NativeDiscImage_GetPath(void);

#endif
