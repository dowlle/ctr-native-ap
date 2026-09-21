// Large-file positioning must be requested before any system header is pulled
// in (issue #334, slice 2). On 32-bit POSIX builds this makes off_t 64-bit so
// fseeko can address an image beyond 2 GiB. The CMake build sets the same flag
// globally; this guard covers a standalone translation unit (the host harness).
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif

#include "platform/native_disc_image.h"

#include <platform/native_fs_utf8.h>
#include <platform/native_path.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The raw-sector seek uses a 64-bit offset on both platforms. Assert the offset
// type is wide enough at compile time; a 32-bit build without large-file
// support must fail here rather than silently keep the 2 GiB limit.
#if defined(_WIN32)
CTR_STATIC_ASSERT(sizeof(long long) >= 8);
#else
CTR_STATIC_ASSERT(sizeof(off_t) >= 8);
#endif

#define NATIVE_DISC_IMAGE_PATH_MAX          1024
#define NATIVE_DISC_IMAGE_BIN_PATH          "ctr-u.bin"
#define NATIVE_DISC_IMAGE_BIN_EXTENSION     ".bin"
#define NATIVE_DISC_IMAGE_SYSTEM_CNF_PATH   "SYSTEM.CNF"
#define NATIVE_DISC_IMAGE_NTSC_U_SERIAL     "SCUS94426"
#define NATIVE_DISC_IMAGE_NTSC_U_BOOT_ID    "SCUS_944.26"
#define NATIVE_DISC_IMAGE_SERIAL_MAX        32
#define NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE   2352u
#define NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET 24u
#define NATIVE_DISC_IMAGE_FORM1_DATA_SIZE   2048u
#define NATIVE_DISC_IMAGE_MODE2_USER_OFFSET 16u
#define NATIVE_DISC_IMAGE_MODE2_USER_SIZE   2336u
#define NATIVE_DISC_IMAGE_PVD_LBA           16u
#define NATIVE_DISC_IMAGE_PVD_ROOT_RECORD   156u
#define NATIVE_DISC_IMAGE_DIRECTORY_FLAG    0x02u

// NOTE(aalhendi): This hardcodes only the common NTSC-U raw BIN layout:
// one MODE2/2352 data track at byte zero. The user still supplies all disc
// contents; native only uses this sector contract to read the image.

struct NativeDiscImageDirRecord
{
	u32 lba;
	u32 size;
	u8 flags;
	u8 nameLen;
	const u8 *name;
};

global_variable char s_nativeDiscImagePath[NATIVE_DISC_IMAGE_PATH_MAX];
global_variable FILE *s_nativeDiscImageFile;
global_variable struct NativeDiscImageFile s_nativeDiscImageRoot;
global_variable int s_nativeDiscImageAvailable;
global_variable char s_nativeDiscImageDetail[256];

internal int NativeDiscImage_HasBinExtension(NativeStr8 name)
{
	NativeStr8 extension = NATIVE_STR8_LIT(NATIVE_DISC_IMAGE_BIN_EXTENSION);

	if (name.len < extension.len)
	{
		return 0;
	}

	return NativeStr8_EqualsIgnoreCaseAscii(NativeStr8_Skip(name, name.len - extension.len), extension);
}

// Pass 1 tries only the conventional ctr-u.bin name; pass 2 tries every other
// .bin file so a correctly dumped disc is found regardless of its filename.
internal int NativeDiscImage_ShouldTryEntry(NativeStr8 entryName, int canonicalPass)
{
	int isCanonical = NativeStr8_EqualsIgnoreCaseAscii(entryName, NATIVE_STR8_LIT(NATIVE_DISC_IMAGE_BIN_PATH));

	if (canonicalPass)
	{
		return isCanonical;
	}

	return !isCanonical && NativeDiscImage_HasBinExtension(entryName);
}

internal u32 NativeDiscImage_ReadLE32(const u8 *data)
{
	return ((u32)data[0]) | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

internal int NativeDiscImage_CheckRawSectorHeader(const u8 *sector)
{
	u32 i;

	if ((sector[0] != 0x00) || (sector[11] != 0x00) || (sector[15] != 0x02))
	{
		return 0;
	}

	for (i = 1; i < 11; i++)
	{
		if (sector[i] != 0xff)
		{
			return 0;
		}
	}

	return 1;
}

internal int NativeDiscImage_ReadRawSector(u32 lba, u8 *sector)
{
	u64 offset;

	if (s_nativeDiscImageFile == NULL)
	{
		return 0;
	}

	offset = (u64)lba * NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE;

	// 64-bit positioning: the whole raw image must be addressable, so a 32-bit
	// build is not capped at 2 GiB (issue #334, slice 2). Behavior for an
	// in-range mounted-disc read is unchanged.
#if defined(_WIN32)
	if (_fseeki64(s_nativeDiscImageFile, (long long)offset, SEEK_SET) != 0)
#else
	if (fseeko(s_nativeDiscImageFile, (off_t)offset, SEEK_SET) != 0)
#endif
	{
		return 0;
	}

	if (fread(sector, 1, NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE, s_nativeDiscImageFile) != NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE)
	{
		return 0;
	}

	return NativeDiscImage_CheckRawSectorHeader(sector);
}

internal int NativeDiscImage_ReadDataSector(u32 lba, u8 *payload)
{
	u8 sector[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];

	if (!NativeDiscImage_ReadRawSector(lba, sector))
	{
		return 0;
	}

	memcpy(payload, &sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET], NATIVE_DISC_IMAGE_FORM1_DATA_SIZE);
	return 1;
}

internal int NativeDiscImage_ParseDirRecord(const u8 *src, size_t available, struct NativeDiscImageDirRecord *record)
{
	u8 length;

	if ((src == NULL) || (record == NULL) || (available < 34))
	{
		return 0;
	}

	length = src[0];
	if ((length == 0) || (length > available) || (length < 34))
	{
		return 0;
	}

	record->lba = NativeDiscImage_ReadLE32(&src[2]);
	record->size = NativeDiscImage_ReadLE32(&src[10]);
	record->flags = src[25];
	record->nameLen = src[32];
	record->name = &src[33];

	if ((u32)record->nameLen + 33u > length)
	{
		return 0;
	}

	return 1;
}

internal int NativeDiscImage_NameEquals(const struct NativeDiscImageDirRecord *record, NativeStr8 component)
{
	size_t recordLen;
	size_t componentLen;
	size_t i;

	if ((record == NULL) || (record->name == NULL) || (component.ptr == NULL))
	{
		return 0;
	}

	recordLen = record->nameLen;
	componentLen = component.len;

	while ((recordLen > 0) && (record->name[recordLen - 1u] == ' '))
	{
		recordLen--;
	}

	if ((recordLen > 2) && (record->name[recordLen - 2u] == ';') && (record->name[recordLen - 1u] == '1'))
	{
		recordLen -= 2;
	}

	if ((componentLen > 2) && (component.ptr[componentLen - 2u] == ';') && (component.ptr[componentLen - 1u] == '1'))
	{
		componentLen -= 2;
	}

	if (recordLen != componentLen)
	{
		return 0;
	}

	for (i = 0; i < componentLen; i++)
	{
		if (NativeStr8_ToUpperAscii(record->name[i]) != NativeStr8_ToUpperAscii(component.ptr[i]))
		{
			return 0;
		}
	}

	return 1;
}

internal NativeStr8 NativeDiscImage_NextPathComponent(NativeStr8 *path)
{
	NativeStr8 result = *path;
	size_t i;

	while ((result.len != 0) && NativePath_IsSeparator(result.ptr[0]))
	{
		result = NativeStr8_Skip(result, 1);
	}

	for (i = 0; i < result.len; i++)
	{
		if (NativePath_IsSeparator(result.ptr[i]))
		{
			NativeStr8 component = {result.ptr, i};
			*path = NativeStr8_Skip(result, i + 1u);
			return component;
		}
	}

	*path = NativeStr8_Skip(result, result.len);
	return result;
}

internal u32 NativeDiscImage_DataSectorCount(u32 size)
{
	return (size + (NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - 1u)) / NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
}

internal u32 NativeDiscImage_RawSectorCount(u32 size)
{
	if ((size != 0) && ((size % NATIVE_DISC_IMAGE_MODE2_USER_SIZE) == 0))
	{
		return size / NATIVE_DISC_IMAGE_MODE2_USER_SIZE;
	}

	return NativeDiscImage_DataSectorCount(size);
}

internal int NativeDiscImage_ReadDirectoryBytes(const struct NativeDiscImageFile *dir, u8 **dataOut, int *sizeOut)
{
	u32 sectorCount;
	u8 *data;
	u32 sector;

	*dataOut = NULL;
	*sizeOut = 0;

	if ((dir == NULL) || (dir->size == 0) || (dir->size > 0x7fffffff))
	{
		return 0;
	}

	sectorCount = NativeDiscImage_DataSectorCount(dir->size);
	if (sectorCount == 0)
	{
		return 0;
	}

	data = (u8 *)malloc((size_t)sectorCount * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE);
	if (data == NULL)
	{
		return 0;
	}

	for (sector = 0; sector < sectorCount; sector++)
	{
		if (!NativeDiscImage_ReadDataSector(dir->lba + sector, &data[sector * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE]))
		{
			free(data);
			return 0;
		}
	}

	*dataOut = data;
	*sizeOut = (int)dir->size;
	return 1;
}

internal int NativeDiscImage_FindInDirectory(const struct NativeDiscImageFile *dir, NativeStr8 component, struct NativeDiscImageFile *fileOut, u8 *flagsOut)
{
	u8 *data;
	int size;
	int offset;
	int found = 0;

	if (!NativeDiscImage_ReadDirectoryBytes(dir, &data, &size))
	{
		return 0;
	}

	offset = 0;
	while (offset < size)
	{
		struct NativeDiscImageDirRecord record;
		u8 length = data[offset];

		if (length == 0)
		{
			offset = (offset + (int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE) & ~((int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - 1);
			continue;
		}

		if ((length < 34) || (offset + length > size))
		{
			break;
		}

		if (NativeDiscImage_ParseDirRecord(&data[offset], (size_t)(size - offset), &record) && NativeDiscImage_NameEquals(&record, component))
		{
			fileOut->lba = record.lba;
			fileOut->size = record.size;
			*flagsOut = record.flags;
			found = 1;
			break;
		}

		offset += length;
	}

	free(data);
	return found;
}

internal int NativeDiscImage_LoadRoot(void)
{
	u8 sector[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];
	struct NativeDiscImageDirRecord root;

	if (!NativeDiscImage_ReadRawSector(NATIVE_DISC_IMAGE_PVD_LBA, sector))
	{
		return 0;
	}

	if ((memcmp(&sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET + 1], "CD001", 5) != 0) || (sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET] != 1) ||
	    (sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET + 6] != 1))
	{
		return 0;
	}

	if (!NativeDiscImage_ParseDirRecord(&sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET + NATIVE_DISC_IMAGE_PVD_ROOT_RECORD],
	                                    NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - NATIVE_DISC_IMAGE_PVD_ROOT_RECORD, &root))
	{
		return 0;
	}

	s_nativeDiscImageRoot.lba = root.lba;
	s_nativeDiscImageRoot.size = root.size;
	return 1;
}

internal void NativeDiscImage_Unmount(void)
{
	if (s_nativeDiscImageFile != NULL)
	{
		fclose(s_nativeDiscImageFile);
		s_nativeDiscImageFile = NULL;
	}

	s_nativeDiscImagePath[0] = '\0';
	s_nativeDiscImageRoot.lba = 0;
	s_nativeDiscImageRoot.size = 0;
	s_nativeDiscImageAvailable = 0;
}

// Extracts the boot executable name from a SYSTEM.CNF BOOT line, e.g.
// "BOOT = cdrom:\SCUS_944.26;1" -> "SCUS_944.26". Same detection the asset
// extractor uses for region checks.
internal int NativeDiscImage_ParseBootSerial(const u8 *data, int size, char *dst, size_t dstSize)
{
	int i = 0;

	while (i < size)
	{
		int lineStart = i;
		int lineEnd = lineStart;
		int cursor;

		while ((lineEnd < size) && (data[lineEnd] != '\n') && (data[lineEnd] != '\r'))
		{
			lineEnd++;
		}

		i = lineEnd + 1;

		cursor = lineStart;
		while ((cursor < lineEnd) && ((data[cursor] == ' ') || (data[cursor] == '\t')))
		{
			cursor++;
		}

		if (((lineEnd - cursor) >= 4) && (NativeStr8_ToUpperAscii(data[cursor]) == 'B') && (NativeStr8_ToUpperAscii(data[cursor + 1]) == 'O') &&
		    (NativeStr8_ToUpperAscii(data[cursor + 2]) == 'O') && (NativeStr8_ToUpperAscii(data[cursor + 3]) == 'T'))
		{
			int nameStart;
			int nameEnd;
			int position = cursor;

			while ((position < lineEnd) && (data[position] != '='))
			{
				position++;
			}

			if (position >= lineEnd)
			{
				continue;
			}

			nameStart = position + 1;
			for (position = nameStart; position < lineEnd; position++)
			{
				u8 byte = data[position];

				if ((byte == '\\') || (byte == '/') || (byte == ':'))
				{
					nameStart = position + 1;
				}
			}

			nameEnd = nameStart;
			while ((nameEnd < lineEnd) && (data[nameEnd] != ';'))
			{
				nameEnd++;
			}

			while ((nameStart < nameEnd) && (data[nameStart] == ' '))
			{
				nameStart++;
			}

			while ((nameEnd > nameStart) && (data[nameEnd - 1] == ' '))
			{
				nameEnd--;
			}

			if (nameEnd <= nameStart)
			{
				return 0;
			}

			return NativeStr8_CopyToCString(dst, dstSize, (NativeStr8){&data[nameStart], (size_t)(nameEnd - nameStart)});
		}
	}

	return 0;
}

internal int NativeDiscImage_ReadBootSerial(char *dst, size_t dstSize)
{
	u8 *data;
	int size;
	int ok;

	if (!NativeDiscImage_ReadFileBytes(NATIVE_DISC_IMAGE_SYSTEM_CNF_PATH, 0, &data, &size))
	{
		return 0;
	}

	ok = NativeDiscImage_ParseBootSerial(data, size, dst, dstSize);
	free(data);
	return ok;
}

internal void NativeDiscImage_NormalizeSerial(const char *serial, char *dst, size_t dstSize)
{
	size_t out = 0;
	size_t i;

	for (i = 0; (serial[i] != '\0') && (out + 1 < dstSize); i++)
	{
		u8 byte = (u8)serial[i];

		if (((byte >= 'a') && (byte <= 'z')) || ((byte >= 'A') && (byte <= 'Z')) || ((byte >= '0') && (byte <= '9')))
		{
			dst[out++] = (char)NativeStr8_ToUpperAscii(byte);
		}
	}

	dst[out] = '\0';
}

internal const char *NativeDiscImage_DescribeForeignSerial(const char *normalizedSerial)
{
	local_persist const char *palPrefixes[] = {"SCES", "SLES", "SCED", "SLED"};
	local_persist const char *ntscJPrefixes[] = {"SCPS", "SLPS", "SLPM", "SCAJ"};
	size_t i;

	for (i = 0; i < sizeof(palPrefixes) / sizeof(palPrefixes[0]); i++)
	{
		if (strncmp(normalizedSerial, palPrefixes[i], strlen(palPrefixes[i])) == 0)
		{
			return "the PAL (European) release";
		}
	}

	for (i = 0; i < sizeof(ntscJPrefixes) / sizeof(ntscJPrefixes[0]); i++)
	{
		if (strncmp(normalizedSerial, ntscJPrefixes[i], strlen(ntscJPrefixes[i])) == 0)
		{
			return "the NTSC-J (Japanese) release";
		}
	}

	return "not the Crash Team Racing NTSC-U disc";
}

// The single-candidate validator (issue #334, slice 2). Opens one candidate,
// validates it against the raw MODE2/2352 + ISO9660 + boot id contract and, on
// success, makes it the active disc when keepMounted is set. A failure always
// restores whatever disc was active, so a bad candidate never clobbers a good
// mount; the reason is left in s_nativeDiscImageDetail.
enum NativeDiscImageValidation NativeDiscImage_ValidateCandidate(const char *path, int keepMounted)
{
	char normalizedPath[NATIVE_DISC_IMAGE_PATH_MAX];
	char serial[NATIVE_DISC_IMAGE_SERIAL_MAX];
	char normalized[NATIVE_DISC_IMAGE_SERIAL_MAX];
	FILE *candidate;
	FILE *previousFile;
	char previousPath[NATIVE_DISC_IMAGE_PATH_MAX];
	struct NativeDiscImageFile previousRoot;
	int previousAvailable;
	enum NativeDiscImageValidation result;

	s_nativeDiscImageDetail[0] = '\0';

	if ((path == NULL) || !NativePath_NormalizeSlashes(normalizedPath, sizeof(normalizedPath), NativeStr8_FromCString(path)))
	{
		return NATIVE_DISC_IMAGE_OPEN_FAILED;
	}

	candidate = NativeFs_OpenRead(normalizedPath);
	if (candidate == NULL)
	{
		snprintf(s_nativeDiscImageDetail, sizeof(s_nativeDiscImageDetail), "cannot open file");
		return NATIVE_DISC_IMAGE_OPEN_FAILED;
	}

	previousFile = s_nativeDiscImageFile;
	memcpy(previousPath, s_nativeDiscImagePath, sizeof(previousPath));
	previousRoot = s_nativeDiscImageRoot;
	previousAvailable = s_nativeDiscImageAvailable;

	s_nativeDiscImageFile = candidate;
	s_nativeDiscImagePath[0] = '\0';
	s_nativeDiscImageRoot.lba = 0;
	s_nativeDiscImageRoot.size = 0;
	s_nativeDiscImageAvailable = 0;

	if (!NativeDiscImage_LoadRoot())
	{
		snprintf(s_nativeDiscImageDetail, sizeof(s_nativeDiscImageDetail),
		         "not a raw MODE2/2352 PlayStation disc image (a cooked 2048-byte .iso does not work)");
		result = NATIVE_DISC_IMAGE_NOT_MODE2;
		goto restorePrevious;
	}

	s_nativeDiscImageAvailable = 1;

	if (!NativeDiscImage_ReadBootSerial(serial, sizeof(serial)))
	{
		snprintf(s_nativeDiscImageDetail, sizeof(s_nativeDiscImageDetail), "no PlayStation boot id (SYSTEM.CNF) found on the disc");
		result = NATIVE_DISC_IMAGE_NO_BOOT_ID;
		goto restorePrevious;
	}

	NativeDiscImage_NormalizeSerial(serial, normalized, sizeof(normalized));
	if (strcmp(normalized, NATIVE_DISC_IMAGE_NTSC_U_SERIAL) != 0)
	{
		snprintf(s_nativeDiscImageDetail, sizeof(s_nativeDiscImageDetail), "boot id %s is %s; this port needs the North American (NTSC-U) release, boot id %s",
		         serial, NativeDiscImage_DescribeForeignSerial(normalized), NATIVE_DISC_IMAGE_NTSC_U_BOOT_ID);
		result = NATIVE_DISC_IMAGE_WRONG_REGION;
		goto restorePrevious;
	}

	if (keepMounted)
	{
		if (previousFile != NULL)
		{
			fclose(previousFile);
		}
		memcpy(s_nativeDiscImagePath, normalizedPath, sizeof(s_nativeDiscImagePath));
		return NATIVE_DISC_IMAGE_VALID;
	}

	fclose(candidate);
	s_nativeDiscImageFile = previousFile;
	memcpy(s_nativeDiscImagePath, previousPath, sizeof(previousPath));
	s_nativeDiscImageRoot = previousRoot;
	s_nativeDiscImageAvailable = previousAvailable;
	return NATIVE_DISC_IMAGE_VALID;

restorePrevious:
	fclose(candidate);
	s_nativeDiscImageFile = previousFile;
	memcpy(s_nativeDiscImagePath, previousPath, sizeof(previousPath));
	s_nativeDiscImageRoot = previousRoot;
	s_nativeDiscImageAvailable = previousAvailable;
	return result;
}

// Wrapper the assets scan uses: validates and keeps a valid candidate mounted,
// printing the same one-line reason on stderr as before so an almost-right
// setup still tells the user what is wrong.
internal int NativeDiscImage_TryMountImage(const char *path)
{
	enum NativeDiscImageValidation result = NativeDiscImage_ValidateCandidate(path, 1);

	if (result == NATIVE_DISC_IMAGE_VALID)
	{
		printf("[CTR Native] Disc image: %s\n", s_nativeDiscImagePath);
		return 1;
	}

	fprintf(stderr, "[CTR Native] ignoring %s: %s\n", path, NativeDiscImage_LastDetail());
	return 0;
}

const char *NativeDiscImage_ValidationText(enum NativeDiscImageValidation result)
{
	switch (result)
	{
	case NATIVE_DISC_IMAGE_VALID:
		return "valid";
	case NATIVE_DISC_IMAGE_OPEN_FAILED:
		return "cannot open file";
	case NATIVE_DISC_IMAGE_NOT_MODE2:
		return "not a raw MODE2/2352 PlayStation disc image";
	case NATIVE_DISC_IMAGE_NO_BOOT_ID:
		return "no PlayStation boot id (SYSTEM.CNF) found";
	case NATIVE_DISC_IMAGE_WRONG_REGION:
		return "not the NTSC-U Crash Team Racing release";
	}

	return "invalid disc image";
}

const char *NativeDiscImage_LastDetail(void)
{
	return s_nativeDiscImageDetail;
}

const char *NativeDiscImage_GetPath(void)
{
	return s_nativeDiscImagePath;
}

internal int NativeDiscImage_ScanHostDir(const char *dirPath, int canonicalPass)
{
	// Directory enumeration goes through the UTF-8 filesystem layer, so a
	// non-ASCII assets folder works on Windows (issue #334, slice 2).
	NativeFsDir *dir = NativeFs_OpenDir(dirPath);
	char entryName[NATIVE_DISC_IMAGE_PATH_MAX];

	if (dir == NULL)
	{
		return 0;
	}

	while (NativeFs_ReadDir(dir, entryName, sizeof(entryName)))
	{
		char path[NATIVE_DISC_IMAGE_PATH_MAX];

		if (!NativeDiscImage_ShouldTryEntry(NativeStr8_FromCString(entryName), canonicalPass))
		{
			continue;
		}

		if (!NativePath_Join(path, sizeof(path), NativeStr8_FromCString(dirPath), NativeStr8_FromCString(entryName)))
		{
			continue;
		}

		if (NativeDiscImage_TryMountImage(path))
		{
			NativeFs_CloseDir(dir);
			return 1;
		}
	}

	NativeFs_CloseDir(dir);
	return 0;
}

int NativeDiscImage_Init(const char *assetsDir)
{
	char dirPath[NATIVE_DISC_IMAGE_PATH_MAX];

	NativeDiscImage_Unmount();

	if ((assetsDir == NULL) || !NativePath_NormalizeSlashes(dirPath, sizeof(dirPath), NativeStr8_FromCString(assetsDir)))
	{
		return 0;
	}

	if (NativeDiscImage_ScanHostDir(dirPath, 1))
	{
		return 1;
	}

	return NativeDiscImage_ScanHostDir(dirPath, 0);
}

int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut)
{
	NativeStr8 remaining = NativePath_SkipLeadingSeparators(NativeStr8_FromCString(path));
	struct NativeDiscImageFile current = s_nativeDiscImageRoot;
	u8 flags = NATIVE_DISC_IMAGE_DIRECTORY_FLAG;

	if (!s_nativeDiscImageAvailable || (path == NULL) || (fileOut == NULL))
	{
		return 0;
	}

	while (remaining.len != 0)
	{
		NativeStr8 component = NativeDiscImage_NextPathComponent(&remaining);

		if (component.len == 0)
		{
			continue;
		}

		if ((flags & NATIVE_DISC_IMAGE_DIRECTORY_FLAG) == 0)
		{
			return 0;
		}

		if (!NativeDiscImage_FindInDirectory(&current, component, &current, &flags))
		{
			return 0;
		}
	}

	if ((flags & NATIVE_DISC_IMAGE_DIRECTORY_FLAG) != 0)
	{
		return 0;
	}

	*fileOut = current;
	return 1;
}

internal int NativeDiscImage_ReadDataBytes(const struct NativeDiscImageFile *file, u32 offset, void *dst, size_t size)
{
	u8 sector[NATIVE_DISC_IMAGE_FORM1_DATA_SIZE];
	u8 *out = (u8 *)dst;

	if ((file == NULL) || (dst == NULL) || ((u64)offset + size > file->size))
	{
		return 0;
	}

	while (size != 0)
	{
		u32 sectorIndex = offset / NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
		u32 sectorOffset = offset % NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
		size_t copySize = NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - sectorOffset;

		if (copySize > size)
		{
			copySize = size;
		}

		if (!NativeDiscImage_ReadDataSector(file->lba + sectorIndex, sector))
		{
			return 0;
		}

		memcpy(out, &sector[sectorOffset], copySize);
		out += copySize;
		offset += (u32)copySize;
		size -= copySize;
	}

	return 1;
}

int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst)
{
	u8 *out = (u8 *)dst;
	u32 fileSectorCount;
	u32 i;

	if ((file == NULL) || (dst == NULL))
	{
		return 0;
	}

	fileSectorCount = NativeDiscImage_DataSectorCount(file->size);
	if ((sector > fileSectorCount) || (sectorCount > fileSectorCount - sector))
	{
		return 0;
	}

	for (i = 0; i < sectorCount; i++)
	{
		if (!NativeDiscImage_ReadDataSector(file->lba + sector + i, &out[i * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE]))
		{
			return 0;
		}
	}

	return 1;
}

int NativeDiscImage_ReadRawSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst)
{
	u8 raw[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];
	u8 *out = (u8 *)dst;
	u32 fileSectorCount;
	u32 i;

	if ((file == NULL) || (dst == NULL))
	{
		return 0;
	}

	fileSectorCount = NativeDiscImage_RawSectorCount(file->size);
	if ((sector > fileSectorCount) || (sectorCount > fileSectorCount - sector))
	{
		return 0;
	}

	for (i = 0; i < sectorCount; i++)
	{
		if (!NativeDiscImage_ReadRawSector(file->lba + sector + i, raw))
		{
			return 0;
		}

		memcpy(&out[i * NATIVE_DISC_IMAGE_MODE2_USER_SIZE], &raw[NATIVE_DISC_IMAGE_MODE2_USER_OFFSET], NATIVE_DISC_IMAGE_MODE2_USER_SIZE);
	}

	return 1;
}

int NativeDiscImage_ReadFileBytes(const char *path, int rawSectors, u8 **dataOut, int *sizeOut)
{
	struct NativeDiscImageFile file;
	u32 sectorCount;
	u32 size;
	u8 *data;

	*dataOut = NULL;
	*sizeOut = 0;

	if (!NativeDiscImage_FindFile(path, &file))
	{
		return 0;
	}

	if (rawSectors)
	{
		sectorCount = NativeDiscImage_RawSectorCount(file.size);
		size = sectorCount * NATIVE_DISC_IMAGE_MODE2_USER_SIZE;
	}
	else
	{
		sectorCount = NativeDiscImage_DataSectorCount(file.size);
		size = file.size;
	}

	if ((sectorCount == 0) || (size > 0x7fffffffu))
	{
		return 0;
	}

	data = (u8 *)malloc((size_t)size);
	if (data == NULL)
	{
		return 0;
	}

	if (rawSectors)
	{
		if (!NativeDiscImage_ReadRawSectors(&file, 0, sectorCount, data))
		{
			free(data);
			return 0;
		}
	}
	else if (!NativeDiscImage_ReadDataBytes(&file, 0, data, size))
	{
		free(data);
		return 0;
	}

	*dataOut = data;
	*sizeOut = (int)size;
	return 1;
}
