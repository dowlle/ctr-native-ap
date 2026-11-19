// Build one local NAV2 v4 set from seven explicitly selected v3 laps.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CTR_AP
#define CTR_AP
#endif
#include "../include/platform/native_sha256.h"
#include "../ap/ap_navrec_format.h"
#include "../ap/ap_navrec_playback_logic.h"

static unsigned char input[AP_NAVREC_MAX_FILE_BYTES];
static unsigned char output[AP_NAVSET_MAX_FILE_BYTES];
static struct AP_NavRecNode nodes[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static unsigned int stamps[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static struct AP_NavRecNode selectedNodes[AP_NAVSET_MAX_LAPS][AP_NAVREC_MAX_NODES];
static unsigned int selectedCounts[AP_NAVSET_MAX_LAPS];

static int store_distinct_nodes(const struct AP_NavRecNode *decoded, unsigned int count, unsigned int slot)
{
	size_t bytes = count * sizeof *decoded;
	for (unsigned int i = 0; i < slot; i++)
	{
		if (selectedCounts[i] == count && !memcmp(selectedNodes[i], decoded, bytes))
			return 0;
	}
	selectedCounts[slot] = count;
	memcpy(selectedNodes[slot], decoded, bytes);
	return 1;
}

static unsigned int read_recording(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file)
		return 0;
	size_t length = fread(input, 1, sizeof input, file);
	int ok = fgetc(file) == EOF && !ferror(file);
	fclose(file);
	return ok ? (unsigned int)length : 0;
}

static int add_lap(const char *path, const char *index, unsigned int slot, unsigned int *at)
{
	char *end;
	errno = 0;
	unsigned long lap = strtoul(index, &end, 10);
	if (errno || end == index || *end || lap >= AP_NAVREC_MAX_LAPS)
		return 0;

	unsigned int size = read_recording(path);
	struct AP_NavRecFileInfo info;
	if (AP_NavRecFormat_Read(input, size, &info, nodes, stamps) != AP_NAVREC_OK ||
	    info.formatVersion != AP_NAVREC_FORMAT_VERSION_V3 || info.navRevision == 0 || lap >= info.lapCount ||
	    info.laps[lap].clean != 1 || !AP_NavRecFormat_LapClosed(nodes[lap], info.laps[lap].nodeCount) ||
	    AP_NavRecSeam_Process(nodes[lap], info.laps[lap].nodeCount, 1, NULL) != AP_NAVREC_SEAM_SMOOTH)
		return 0;
	const unsigned char zeroUuid[AP_NAVREC_TRACK_UUID_BYTES] = {0};
	if (!memcmp(info.trackUuid, zeroUuid, sizeof zeroUuid))
		return 0;

	if (slot == 0)
	{
		memcpy(output, input, AP_NAVREC_HEADER_BYTES_V3);
		AP_NavRecFormat_PutU16(output + 4, AP_NAVREC_FORMAT_VERSION_V4);
		memset(output + 0x30, 0, AP_NAVREC_NAME_FIELD);
		output[0x55] = AP_NAVSET_MAX_LAPS;
	}
	else if (memcmp(output + 0x64, info.trackUuid, sizeof info.trackUuid) ||
	         AP_NavRecFormat_GetU32(output + 0x74) != info.navRevision)
		return 0;

	const unsigned char *sourceEntry = input + AP_NAVREC_HEADER_BYTES_V3 + lap * AP_NAVREC_LAPDIR_BYTES;
	const unsigned char *frames = input + AP_NavRecFormat_GetU32(sourceEntry + 0x0c);
	const unsigned char *times = input + AP_NavRecFormat_GetU32(sourceEntry + 0x10);
	unsigned int count = info.laps[lap].nodeCount;
	unsigned int frameBytes = count * AP_NAVREC_NODE_BYTES;
	unsigned int stampBytes = count * AP_NAVREC_STAMP_BYTES;
	unsigned char *entry = output + AP_NAVREC_HEADER_BYTES_V3 + slot * AP_NAVSET_LAPDIR_BYTES;
	// Compare what a bot will drive, including the distances recalculated
	// by the seam blend. The serialized payload keeps its original shape.
	if (!store_distinct_nodes(nodes[lap], count, slot))
		return 0;

	memcpy(entry, sourceEntry, 0x1c);
	memcpy(entry + 0x1c, info.driverName, strlen(info.driverName));
	struct NativeSha256Ctx sha;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, input, size);
	NativeSha256_Final(&sha, digest);
	memcpy(entry + 0x3c, digest, 4);

	AP_NavRecFormat_PutU32(entry + 0x0c, *at);
	memcpy(output + *at, frames, frameBytes);
	*at += frameBytes;
	AP_NavRecFormat_PutU32(entry + 0x10, *at);
	memcpy(output + *at, times, stampBytes);
	*at += stampBytes;
	return 1;
}

int main(int argc, char **argv)
{
	if (argc != 2 + 2 * AP_NAVSET_MAX_LAPS)
	{
		fprintf(stderr, "Usage: %s OUTPUT.navset FILE.navlap LAP_INDEX [six more FILE LAP_INDEX pairs]\n", argv[0]);
		return EXIT_FAILURE;
	}

	unsigned int at = AP_NAVREC_HEADER_BYTES_V3 + AP_NAVSET_MAX_LAPS * AP_NAVSET_LAPDIR_BYTES;
	for (unsigned int i = 0; i < AP_NAVSET_MAX_LAPS; i++)
	{
		if (!add_lap(argv[2 + 2 * i], argv[3 + 2 * i], i, &at))
		{
			fprintf(stderr, "Lap %u must be readable, clean, closed, distinct and match the set identity\n", i + 1);
			return EXIT_FAILURE;
		}
	}
	unsigned int size = at + AP_NAVREC_TRAILER_BYTES;
	AP_NavRecFormat_PutU32(output + 8, size);
	AP_NavRecFormat_PutU64(output + at, AP_NavRecFormat_Hash(output, at));
	FILE *file = fopen(argv[1], "wbx");
	if (!file)
	{
		perror(argv[1]);
		return EXIT_FAILURE;
	}
	int ok = fwrite(output, 1, size, file) == size;
	if (fclose(file) != 0)
		ok = 0;
	if (!ok)
	{
		remove(argv[1]);
		fprintf(stderr, "Could not write set\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
