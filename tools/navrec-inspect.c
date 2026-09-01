// Read-only boundary inspector for AI lap recording containers (NAV2/NAV3).
//
// Compiles the REAL reader: ap/ap_navrec_format.h is freestanding by design, so
// every container accepted here is one the client would accept, byte for byte.
// Nothing is written; the tool exists so a finish-line discontinuity can be
// MEASURED on real artifacts instead of inferred from the follower's behavior.
//
//   cc -Wall -Wextra -o /tmp/navrec-inspect tools/navrec-inspect.c -lm
//   /tmp/navrec-inspect file.navlap [more.navlap ...]
//
// Output, one FILE line per container and one LAP line per accepted lap:
//
//   FILE path=... ver=. level=. identity=retail|custom uuid=... rev=. kind=.
//        driver=... laps=.
//   LAP  file=... lap=. nodes=. frames=. samples=. clean=. shortcut=. hint=.
//        first=(x,y,z) last=(x,y,z) closeXZ=. closeXYZ=. storedLastDist=./.
//        yawFirst=. yawLast=. yawDelta=. gbFirst=. gbLast=. gbDistinct=.
//        stampLast=. maxSeg=. maxSegAt=. medSeg=.
//
// closeXZ/closeXYZ are the last->first node distances in NavFrame units, the
// exact segment the cyclic follower drives when it wraps at the finish line.
// storedLastDist echoes the last node's distToNextNavXZ/XYZ as serialized, which
// FillDistances computed over the closed loop, so the two agree on a well-formed
// file. maxSeg is the longest inter-node segment EXCLUDING the wrap segment,
// which is the yardstick a closure has to be judged against: a wrap segment far
// above maxSeg is a discontinuity the recorded line itself never contains.
//
// A rejected file prints one REJECT line with the reader's reason. Exit 0 when
// every named file was at least parseable enough to report on; 1 on I/O errors.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ap/ap_navrec_format.h"

static struct AP_NavRecNode g_nodes[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];
static unsigned int         g_stamps[AP_NAVREC_MAX_LAPS][AP_NAVREC_MAX_NODES];

static double SegXZ(const struct AP_NavRecNode *a, const struct AP_NavRecNode *b)
{
	double dx = (double)b->posX - (double)a->posX;
	double dz = (double)b->posZ - (double)a->posZ;
	return sqrt((dx * dx) + (dz * dz));
}

static double SegXYZ(const struct AP_NavRecNode *a, const struct AP_NavRecNode *b)
{
	double dx = (double)b->posX - (double)a->posX;
	double dy = (double)b->posY - (double)a->posY;
	double dz = (double)b->posZ - (double)a->posZ;
	return sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

static int CompareDouble(const void *a, const void *b)
{
	double da = *(const double *)a;
	double db = *(const double *)b;
	return (da > db) - (da < db);
}

static void InspectFile(const char *path)
{
	struct AP_NavRecFileInfo info;
	FILE                    *f;
	long                     len;
	unsigned char           *buf;
	unsigned int             i;
	int                      rc;

	f = fopen(path, "rb");
	if (f == NULL)
	{
		printf("REJECT path=%s reason=cannot-open\n", path);
		return;
	}
	if ((fseek(f, 0, SEEK_END) != 0) || ((len = ftell(f)) < 0) || (fseek(f, 0, SEEK_SET) != 0))
	{
		fclose(f);
		printf("REJECT path=%s reason=cannot-measure\n", path);
		return;
	}
	if ((unsigned long)len > AP_NAVREC_MAX_FILE_BYTES)
	{
		fclose(f);
		printf("REJECT path=%s reason=oversize\n", path);
		return;
	}

	buf = (unsigned char *)malloc((len > 0) ? (size_t)len : 1u);
	if (buf == NULL)
	{
		fclose(f);
		printf("REJECT path=%s reason=oom\n", path);
		return;
	}
	if (fread(buf, 1, (size_t)len, f) != (size_t)len)
	{
		fclose(f);
		free(buf);
		printf("REJECT path=%s reason=short-read\n", path);
		return;
	}
	fclose(f);

	rc = AP_NavRecFormat_Read(buf, (unsigned int)len, &info, g_nodes, g_stamps);
	free(buf);
	if (rc != AP_NAVREC_OK)
	{
		printf("REJECT path=%s reason=\"%s\"\n", path, AP_NavRecFormat_ErrorText(rc));
		return;
	}

	printf("FILE path=%s ver=%u level=%d identity=%s", path, info.formatVersion, info.levelId,
	       (info.identityKind == AP_NAVREC_IDENTITY_CUSTOM) ? "custom" : "retail");
	if (info.identityKind == AP_NAVREC_IDENTITY_CUSTOM)
	{
		printf(" uuid=");
		for (i = 0; i < AP_NAVREC_TRACK_UUID_BYTES; i++)
			printf("%02x", info.trackUuid[i]);
		printf(" rev=%u", info.navRevision);
	}
	printf(" kind=%u driver=\"%s\" client=\"%s\" laps=%u\n", info.trackKind, info.driverName, info.clientVersion, info.lapCount);

	for (i = 0; i < info.lapCount; i++)
	{
		const struct AP_NavRecNode *nodes = g_nodes[i];
		unsigned int                count = info.laps[i].nodeCount;
		const struct AP_NavRecNode *first = &nodes[0];
		const struct AP_NavRecNode *last = &nodes[count - 1u];
		double                      segs[AP_NAVREC_MAX_NODES];
		double                      maxSeg = 0.0;
		unsigned int                maxSegAt = 0;
		unsigned int                gbDistinct = 0;
		unsigned char               gbSeen[256];
		unsigned int                n;
		int                         yawFirst = (int)first->rot[1] << 4;
		int                         yawLast = (int)last->rot[1] << 4;
		int                         yawDelta;

		// Yaw is a 12-bit CTR angle; the shortest signed wrap is the delta a
		// follower would actually turn through.
		yawDelta = (yawFirst - yawLast) & 0xFFF;
		if (yawDelta > 0x800)
			yawDelta -= 0x1000;

		memset(gbSeen, 0, sizeof gbSeen);
		for (n = 0; n < count; n++)
		{
			if (!gbSeen[nodes[n].goBackCount])
			{
				gbSeen[nodes[n].goBackCount] = 1;
				gbDistinct++;
			}
			if (n + 1u < count)
			{
				segs[n] = SegXZ(&nodes[n], &nodes[n + 1u]);
				if (segs[n] > maxSeg)
				{
					maxSeg = segs[n];
					maxSegAt = n;
				}
			}
		}

		qsort(segs, count - 1u, sizeof(double), CompareDouble);

		printf("LAP  file=%s lap=%u closed=%d nodes=%u frames=%u samples=%u clean=%u shortcut=%u hint=%u "
		       "first=(%d,%d,%d) last=(%d,%d,%d) closeXZ=%.1f closeXYZ=%.1f storedLastDist=%d/%d "
		       "yawFirst=%d yawLast=%d yawDelta=%d gbFirst=%u gbLast=%u gbDistinct=%u "
		       "stampLast=%u maxSeg=%.1f maxSegAt=%u medSeg=%.1f\n",
		       path, i, AP_NavRecFormat_LapClosed(nodes, count), count, info.laps[i].lapFrames, info.laps[i].sampleCount,
		       info.laps[i].clean, info.laps[i].shortcut,
		       info.laps[i].laneHint, first->posX, first->posY, first->posZ, last->posX, last->posY, last->posZ,
		       SegXZ(last, first), SegXYZ(last, first), last->distToNextNavXZ, last->distToNextNavXYZ, yawFirst, yawLast, yawDelta,
		       first->goBackCount, last->goBackCount, gbDistinct, g_stamps[i][count - 1u], maxSeg, maxSegAt,
		       segs[(count - 1u) / 2u]);
	}
}

int main(int argc, char **argv)
{
	int i;

	if (argc < 2)
	{
		fprintf(stderr, "usage: %s file.navlap [more.navlap ...]\n", argv[0]);
		return 1;
	}

	for (i = 1; i < argc; i++)
		InspectFile(argv[i]);

	return 0;
}
