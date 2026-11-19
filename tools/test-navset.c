#include <stdio.h>
#include <stdlib.h>

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
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
