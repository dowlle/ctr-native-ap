#ifndef NATIVE_ASSETS_H
#define NATIVE_ASSETS_H

#include <stddef.h>
#include <stdio.h>

#include "platform/native_str8.h"

enum NativeAssetReadMode
{
	NATIVE_ASSET_READ_DATA_FILE,
	NATIVE_ASSET_READ_RAW_CD_SECTORS,
};

struct NativeAssetsByteBuffer
{
	u8 *data;
	int size;
};

int NativeAssets_Init(const char *executableBasePath);
// Mount the first valid .bin in the assets folder. Separate from Init so
// startup can read config.ini first (issue #334, slice 2). Returns 1 when a
// valid disc is mounted.
int NativeAssets_MountDiscFromAssetsDir(void);
const char *NativeAssets_GetBaseDir(void);
const char *NativeAssets_GetAssetDir(void);
int NativeAssets_BuildPathStr8(NativeStr8 relativePath, char *dst, size_t dstSize);
int NativeAssets_BuildPath(const char *relativePath, char *dst, size_t dstSize);
int NativeAssets_ResolvePathStr8(NativeStr8 relativePath, char *dst, size_t dstSize);
int NativeAssets_ResolvePath(const char *relativePath, char *dst, size_t dstSize);
FILE *NativeAssets_OpenHostStr8(NativeStr8 relativePath, const char *mode);
FILE *NativeAssets_OpenHost(const char *relativePath, const char *mode);
FILE *NativeAssets_OpenHostBigfile(const char *mode);
int NativeAssets_ReadBytes(const char *path, int readMode, struct NativeAssetsByteBuffer *bytes);
void NativeAssets_FreeBytes(struct NativeAssetsByteBuffer *bytes);
int NativeAssets_Validate(void);

#endif
