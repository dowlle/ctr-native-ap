#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef CTR_AP
#define CTR_AP
#endif
#include "../include/bots_nav_lanes.h"
#define main NavSetMintMain
#include "navset-mint.c"
#undef main

static unsigned char fixture[AP_NAVSET_MAX_FILE_BYTES];
static struct AP_NavRecNode setNodes[AP_NAVSET_MAX_LAPS][AP_NAVREC_MAX_NODES];
static unsigned int setStamps[AP_NAVSET_MAX_LAPS][AP_NAVREC_MAX_NODES];
static struct AP_NavSetInfo info;
static int failures;

static void check(int pass, const char *message)
{
	if (!pass)
	{
		fprintf(stderr, "%s\n", message);
		failures++;
	}
}

static void checksum(unsigned int size)
{
	unsigned int at = size - AP_NAVREC_TRAILER_BYTES;
	AP_NavRecFormat_PutU64(fixture + at, AP_NavRecFormat_Hash(fixture, at));
}

static unsigned int makeSet(unsigned int laps)
{
	const unsigned int count = AP_NAVREC_MIN_NODES;
	unsigned int at = AP_NAVREC_HEADER_BYTES_V3 + laps * AP_NAVSET_LAPDIR_BYTES;

	memset(fixture, 0, sizeof fixture);
	memcpy(fixture, "NAV2", 4);
	AP_NavRecFormat_PutU16(fixture + 4, AP_NAVREC_FORMAT_VERSION_V4);
	AP_NavRecFormat_PutU16(fixture + 6, AP_NAVREC_HEADER_BYTES_V3);
	fixture[0x55] = laps;
	fixture[0x56] = AP_NAVREC_NODE_BYTES;
	fixture[0x60] = AP_NAVREC_IDENTITY_CUSTOM;
	fixture[0x64] = 0x26;
	AP_NavRecFormat_PutU32(fixture + 0x74, 0xbbab52f2u);
	for (unsigned int lap = 0; lap < laps; lap++)
	{
		unsigned char *entry = fixture + AP_NAVREC_HEADER_BYTES_V3 + lap * AP_NAVSET_LAPDIR_BYTES;
		AP_NavRecFormat_PutU32(entry, count);
		AP_NavRecFormat_PutU32(entry + 4, count + lap);
		entry[0x14] = 1;
		snprintf((char *)entry + 0x1c, AP_NAVREC_NAME_FIELD, "Driver %u", lap + 1);
		entry[0x3c] = lap + 1;
		AP_NavRecFormat_PutU32(entry + 0x0c, at);
		for (unsigned int node = 0; node < count; node++, at += AP_NAVREC_NODE_BYTES)
		{
			AP_NavRecFormat_PutU16(fixture + at, lap * 100 + node);
			fixture[at + 0x12] = 1 + node % 4;
		}
		AP_NavRecFormat_PutU32(entry + 0x10, at);
		for (unsigned int node = 0; node < count; node++, at += AP_NAVREC_STAMP_BYTES)
			AP_NavRecFormat_PutU32(fixture + at, node);
	}
	at += AP_NAVREC_TRAILER_BYTES;
	AP_NavRecFormat_PutU32(fixture + 8, at);
	checksum(at);
	return at;
}

static int write_mint_fixture(const char *path, unsigned int variant, int differentLine)
{
	struct AP_NavRecLapInfo lap = {.nodeCount = 32, .lapFrames = 900, .sampleCount = 900, .clean = 1};
	struct AP_NavRecNode line[32] = {0};
	unsigned int time[32];
	const struct AP_NavRecNode *nodeRows[] = {line};
	const unsigned int *stampRows[] = {time};
	struct AP_NavRecMeta meta = {.clientVersion = "harness", .driverName = "Harness driver",
	                           .identityKind = AP_NAVREC_IDENTITY_CUSTOM, .trackUuid = {1}, .navRevision = 1};
	unsigned int size;

	for (unsigned int i = 0; i < lap.nodeCount; i++)
	{
		double angle = i * (6.283185307179586 / lap.nodeCount);
		double radius = 1000 + (differentLine ? variant * 20 : 0);
		line[i].posX = (short)(radius * sin(angle));
		line[i].posZ = (short)(radius * cos(angle));
		line[i].goBackCount = i + 1;
		time[i] = i * 28;
	}
	AP_NavRecFormat_FillDistances(line, lap.nodeCount);
	line[16].distToNextNavXZ += variant;
	if (AP_NavRecFormat_Write(fixture, sizeof fixture, &meta, &lap, 1, nodeRows, stampRows, &size) != AP_NAVREC_OK)
		return 0;
	FILE *file = fopen(path, "wb");
	if (!file)
		return 0;
	int written = fwrite(fixture, 1, size, file) == size;
	return fclose(file) == 0 && written;
}

static void check_mint_cli(void)
{
	char dir[] = "/tmp/ctr-navset-test-XXXXXX";
	if (!mkdtemp(dir))
	{
		check(0, "create temporary mint fixture directory");
		return;
	}
	char outputPath[96], paths[AP_NAVSET_MAX_LAPS][96];
	char *args[3 + 2 * AP_NAVSET_MAX_LAPS] = {"navset-mint", outputPath};
	snprintf(outputPath, sizeof outputPath, "%s/recorded-ai.navset", dir);
	for (unsigned int i = 0; i < AP_NAVSET_MAX_LAPS; i++)
	{
		snprintf(paths[i], sizeof paths[i], "%s/%u.navlap", dir, i);
		args[2 + 2 * i] = paths[i];
		args[3 + 2 * i] = "0";
		check(write_mint_fixture(paths[i], i, 0), "write a distance-only variant");
	}
	check(NavSetMintMain(2 + 2 * AP_NAVSET_MAX_LAPS, args) == EXIT_FAILURE &&
	      access(outputPath, F_OK) != 0, "mint refuses seven copies of the same blended line");

	for (unsigned int i = 0; i < AP_NAVSET_MAX_LAPS; i++)
		check(write_mint_fixture(paths[i], i, 1), "write a distinct circle");
	check(NavSetMintMain(2 + 2 * AP_NAVSET_MAX_LAPS, args) == EXIT_SUCCESS &&
	      access(outputPath, F_OK) == 0, "mint accepts seven distinct blended lines");
	for (unsigned int i = 0; i < AP_NAVSET_MAX_LAPS; i++)
		unlink(paths[i]);
	unlink(outputPath);
	rmdir(dir);
}

int main(void)
{
	unsigned int size = makeSet(AP_NAVSET_MAX_LAPS);
	unsigned char *entry = fixture + AP_NAVREC_HEADER_BYTES_V3;

	check(BOTS_NAV_PATH_COUNT == 3 && BOTS_NAV_LANE_MAX == 10, "three LEV paths plus seven recorded lanes");
	check(BOTS_NavFallbackPath(9, 1, 0) == 1 && BOTS_NavFallbackPath(9, 1, 1) == 0 &&
	      BOTS_NavFallbackPath(9, 1, 2) == 2, "recorded lane falls back to the original track lane first");
	check(BOTS_NavFallbackPath(2, 0, 1) == 1 && BOTS_NavFallbackPath(0, 2, 1) == 2,
	      "track lanes fall back in descending order");
	check(AP_NavSet_Read(fixture, size, &info, setNodes, setStamps) == AP_NAVREC_OK, "reads seven distinct laps");
	check(info.header.lapCount == 7 && info.header.navRevision == 0xbbab52f2u &&
	      info.header.trackUuid[0] == 0x26, "preserves the track identity");
	check(!strcmp(info.driverNames[6], "Driver 7") && info.sourceShaPrefix[6][0] == 7 &&
	      info.laps[6].lapFrames == AP_NAVREC_MIN_NODES + 6 && setNodes[6][0].posX == 600 &&
	      setStamps[6][1] == 1, "preserves per-lap metadata, nodes and timestamps");
	check(store_distinct_nodes(setNodes[0], info.laps[0].nodeCount, 0), "first decoded line is distinct");
	check(store_distinct_nodes(setNodes[1], info.laps[1].nodeCount, 1), "a second decoded line can differ");
	check(AP_NavRecFormat_Read(fixture, size, &info.header, setNodes, setStamps) == AP_NAVREC_ERR_VERSION,
	      "the ordinary lap loader refuses sets");
	check(AP_NavSet_Read(fixture, size - 1, &info, setNodes, setStamps) == AP_NAVREC_ERR_SIZE, "rejects truncation");
	fixture[size - 1] ^= 1;
	check(AP_NavSet_Read(fixture, size, &info, setNodes, setStamps) == AP_NAVREC_ERR_HASH, "rejects damaged checksum");
	fixture[0x55] = 8;
	checksum(size);
	check(AP_NavSet_Read(fixture, size, &info, setNodes, setStamps) == AP_NAVREC_ERR_LAPCOUNT, "rejects eight entries");
	size = makeSet(1);
	check(AP_NavSet_Read(fixture, size, &info, setNodes, setStamps) == AP_NAVREC_OK, "reads a one-lap set");
	unsigned int frames = AP_NavRecFormat_GetU32(entry + 0x0c);
	AP_NavRecFormat_PutU16(fixture + frames + 0x10, 0x1234);
	checksum(size);
	check(AP_NavSet_Read(fixture, size, &info, setNodes, setStamps) == AP_NAVREC_OK &&
	      !store_distinct_nodes(setNodes[0], info.laps[0].nodeCount, 2),
	      "two lines differing only in the serialized lane-change opcode are duplicates");
	AP_NavRecFormat_PutU32(entry + 0x10, AP_NavRecFormat_GetU32(entry + 0x0c));
	checksum(size);
	check(AP_NavSet_Read(fixture, size, &info, setNodes, setStamps) == AP_NAVREC_ERR_OVERLAP, "rejects overlapping payloads");
	AP_NavRecFormat_PutU32(entry + 0x10, 0xffffffffu);
	checksum(size);
	check(AP_NavSet_Read(fixture, size, &info, setNodes, setStamps) == AP_NAVREC_ERR_OFFSETS, "rejects an offset out of range");
	size = makeSet(1);
	fixture[0x64] = 0;
	checksum(size);
	check(AP_NavSet_Read(fixture, size, &info, setNodes, setStamps) == AP_NAVREC_ERR_HEADER_SHAPE, "rejects an empty track UUID");
	check_mint_cli();
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
