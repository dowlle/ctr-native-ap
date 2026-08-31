#ifdef CTR_CUSTOM_TRACKS

#include <platform/native_custom_content_verify.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define CCV_LEVEL_NUM_INSTANCES 0x0c
#define CCV_LEVEL_PTR_INSTDEFS 0x10
#define CCV_LEVEL_DRIVER_SPAWN 0x6c
#define CCV_LEVEL_CHECKPOINT_COUNT 0x148
#define CCV_LEVEL_NAV_TABLE 0x188
#define CCV_INSTDEF_STRIDE 0x40
#define CCV_INSTDEF_MODEL_ID 0x3c
#define CCV_MAX_INSTANCES 65536ul
#define CCV_MAX_CHECKPOINTS 65535ul
#define CCV_DRIVER_SPAWNS 8
#define CCV_SPAWN_BYTES 12
#define CCV_NAV_PATHS 3

enum
{
	CCV_PU_WUMPA = 0x02,
	CCV_PU_EXPLOSIVE_CRATE = 0x06,
	CCV_PU_FRUIT_CRATE = 0x07,
	CCV_PU_RANDOM_CRATE = 0x08,
	CCV_PU_TIME_CRATE_1 = 0x09,
	CCV_PU_TIME_CRATE_2 = 0x0a,
	CCV_PU_TIME_CRATE_3 = 0x0b,
	CCV_START_LINE = 0x15,
	CCV_FINISH_LINE = 0x23,
	CCV_STATIC_TNT = 0x27,
	CCV_STATIC_TIME_CRATE_1 = 0x5c,
	CCV_STATIC_CRYSTAL = 0x60,
	CCV_STATIC_TIME_CRATE_2 = 0x64,
	CCV_STATIC_TIME_CRATE_3 = 0x65,
	CCV_STATIC_C = 0x93,
	CCV_STATIC_T = 0x94,
	CCV_STATIC_R = 0x95
};

static void ccv_error(char *dst, size_t bytes, const char *text)
{
	if (dst == NULL || bytes == 0) return;
	snprintf(dst, bytes, "%s", text);
}

static int ccv_read_u32(FILE *file, unsigned long offset, unsigned long *out)
{
	unsigned char raw[4];
	if (offset > 0x7ffffffful || fseek(file, (long)offset, SEEK_SET) != 0 ||
	    fread(raw, 1, sizeof raw, file) != sizeof raw)
		return 0;
	*out = (unsigned long)raw[0] | ((unsigned long)raw[1] << 8) |
	       ((unsigned long)raw[2] << 16) | ((unsigned long)raw[3] << 24);
	return 1;
}

static int ccv_hash_file(const char *path, char out[NATIVE_SHA256_HEX_BYTES],
	                     unsigned long *outBytes)
{
	struct NativeSha256Ctx ctx;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
	unsigned char buf[65536];
	struct stat st;
	FILE *file;
	size_t got;
	unsigned long total = 0;
	(void)NativeSha256_HexEquals; // keep the shared header warning-clean here

	if (path == NULL || stat(path, &st) != 0 || st.st_size <= 0) return 0;
	file = fopen(path, "rb");
	if (file == NULL) return 0;
	NativeSha256_Init(&ctx);
	while ((got = fread(buf, 1, sizeof buf, file)) > 0)
	{
		NativeSha256_Update(&ctx, buf, got);
		total += (unsigned long)got;
	}
	if (ferror(file)) { fclose(file); return 0; }
	fclose(file);
	NativeSha256_Final(&ctx, digest);
	NativeSha256_ToHex(digest, out);
	*outBytes = total;
	return 1;
}

static int ccv_range(unsigned long start, unsigned long bytes, unsigned long limit)
{
	return start <= limit && bytes <= limit - start;
}

static int ccv_nonzero(FILE *file, unsigned long offset, unsigned long bytes)
{
	unsigned char raw[CCV_SPAWN_BYTES];
	unsigned long i;
	if (bytes > sizeof raw || fseek(file, (long)offset, SEEK_SET) != 0 ||
	    fread(raw, 1, (size_t)bytes, file) != bytes) return -1;
	for (i = 0; i < bytes; i++) if (raw[i] != 0) return 1;
	return 0;
}

static void ccv_mode(struct CustomContentVerification *out, int mode, int yes,
	                 const char *reason)
{
	out->fileAnalysis[mode].result = yes ? CTR_CCV_YES : CTR_CCV_NO;
	snprintf(out->fileAnalysis[mode].reason,
	         sizeof out->fileAnalysis[mode].reason, "%s", reason);
}

static void ccv_derive_modes(struct CustomContentVerification *out)
{
	const struct CustomContentMeasurements *m = &out->measured;
	ccv_mode(out, CTR_CCV_TIME_TRIAL,
	         m->checkpoints > 0,
	         m->checkpoints > 0
	             ? "checkpoint/restart graph found"
	             : "requires a checkpoint/restart graph");
	ccv_mode(out, CTR_CCV_RELIC_RACE,
	         m->checkpoints > 0 && m->relicCrates > 0,
	         m->checkpoints > 0 && m->relicCrates > 0
	             ? "checkpoint graph and relic time crates found"
	             : "requires checkpoints and at least one relic time crate");
	ccv_mode(out, CTR_CCV_CTR_CHALLENGE,
	         m->checkpoints > 0 && m->letterC > 0 && m->letterT > 0 && m->letterR > 0,
	         m->checkpoints > 0 && m->letterC > 0 && m->letterT > 0 && m->letterR > 0
	             ? "checkpoint graph and C/T/R instances found"
	             : "requires checkpoints and all three C/T/R instances");
	ccv_mode(out, CTR_CCV_ARCADE,
	         m->checkpoints > 0 && m->spawns >= 2 && m->navPaths > 0,
	         m->checkpoints > 0 && m->spawns >= 2 && m->navPaths > 0
	             ? "checkpoints, multi-kart spawns and embedded bot paths found"
	             : "requires checkpoints, at least two spawns and embedded bot paths");
	ccv_mode(out, CTR_CCV_CRYSTAL_CHALLENGE,
	         m->spawns > 0 && m->crystals > 0,
	         m->spawns > 0 && m->crystals > 0
	             ? "driver spawn and crystal instances found"
	             : "requires a driver spawn and crystal instances");
}

int CustomContentVerify_Files(const char *levPath, const char *vrmPath,
	                           struct CustomContentVerification *out,
	                           char *error, size_t errorBytes)
{
	FILE *file = NULL;
	unsigned long ptrMap, payloadBytes, numInstances, instDefs, tableBytes;
	unsigned long checkpoints, navTable, i;

	if (out == NULL) { ccv_error(error, errorBytes, "output is required"); return 0; }
	memset(out, 0, sizeof *out);
	if (!ccv_hash_file(levPath, out->levSha256, &out->levBytes))
	{ ccv_error(error, errorBytes, "LEV is missing, empty or unreadable"); return 0; }
	if (!ccv_hash_file(vrmPath, out->vrmSha256, &out->vrmBytes))
	{ ccv_error(error, errorBytes, "VRM is missing, empty or unreadable"); return 0; }
	if (out->levBytes < 8) { ccv_error(error, errorBytes, "LEV is too small"); return 0; }
	payloadBytes = out->levBytes - 4;
	file = fopen(levPath, "rb");
	if (file == NULL) { ccv_error(error, errorBytes, "LEV could not be reopened"); return 0; }
	if (!ccv_read_u32(file, 0, &ptrMap) || ptrMap < 0x190 || ptrMap > payloadBytes)
	{ ccv_error(error, errorBytes, "LEV pointer-map offset is invalid"); goto fail; }
	if (!ccv_read_u32(file, 4 + CCV_LEVEL_NUM_INSTANCES, &numInstances) ||
	    !ccv_read_u32(file, 4 + CCV_LEVEL_PTR_INSTDEFS, &instDefs) ||
	    numInstances > CCV_MAX_INSTANCES)
	{ ccv_error(error, errorBytes, "LEV instance header is invalid"); goto fail; }
	tableBytes = numInstances * CCV_INSTDEF_STRIDE;
	if (numInstances > 0 && !ccv_range(instDefs, tableBytes, ptrMap))
	{ ccv_error(error, errorBytes, "LEV instance table is out of bounds"); goto fail; }
	out->measured.instances = numInstances;
	for (i = 0; i < numInstances; i++)
	{
		unsigned long model;
		if (!ccv_read_u32(file, 4 + instDefs + i * CCV_INSTDEF_STRIDE + CCV_INSTDEF_MODEL_ID, &model))
		{ ccv_error(error, errorBytes, "LEV instance table is truncated"); goto fail; }
		switch (model)
		{
		case CCV_PU_WUMPA: out->measured.looseWumpa++; break;
		case CCV_PU_FRUIT_CRATE: out->measured.fruitCrates++; out->measured.ordinaryCrates++; break;
		case CCV_PU_RANDOM_CRATE:
		case CCV_PU_EXPLOSIVE_CRATE:
		case CCV_STATIC_TNT: out->measured.ordinaryCrates++; break;
		case CCV_PU_TIME_CRATE_1:
		case CCV_PU_TIME_CRATE_2:
		case CCV_PU_TIME_CRATE_3:
		case CCV_STATIC_TIME_CRATE_1:
		case CCV_STATIC_TIME_CRATE_2:
		case CCV_STATIC_TIME_CRATE_3: out->measured.relicCrates++; break;
		case CCV_STATIC_C: out->measured.letterC++; break;
		case CCV_STATIC_T: out->measured.letterT++; break;
		case CCV_STATIC_R: out->measured.letterR++; break;
		case CCV_STATIC_CRYSTAL: out->measured.crystals++; break;
		case CCV_START_LINE: out->measured.startLines++; break;
		case CCV_FINISH_LINE: out->measured.finishLines++; break;
		default: break;
		}
	}
	if (!ccv_read_u32(file, 4 + CCV_LEVEL_CHECKPOINT_COUNT, &checkpoints) ||
	    checkpoints > CCV_MAX_CHECKPOINTS)
	{ ccv_error(error, errorBytes, "LEV checkpoint count is invalid"); goto fail; }
	out->measured.checkpoints = checkpoints;
	for (i = 0; i < CCV_DRIVER_SPAWNS; i++)
	{
		int present;
		unsigned long at = CCV_LEVEL_DRIVER_SPAWN + i * CCV_SPAWN_BYTES;
		if (!ccv_range(at, CCV_SPAWN_BYTES, ptrMap))
		{ ccv_error(error, errorBytes, "LEV driver-spawn table is out of bounds"); goto fail; }
		present = ccv_nonzero(file, 4 + at, CCV_SPAWN_BYTES);
		if (present < 0) { ccv_error(error, errorBytes, "LEV driver-spawn table is truncated"); goto fail; }
		if (present) out->measured.spawns++;
	}
	if (!ccv_read_u32(file, 4 + CCV_LEVEL_NAV_TABLE, &navTable))
	{ ccv_error(error, errorBytes, "LEV navigation field is unreadable"); goto fail; }
	if (navTable != 0)
	{
		if (!ccv_range(navTable, CCV_NAV_PATHS * 4, ptrMap))
		{ ccv_error(error, errorBytes, "LEV navigation table is out of bounds"); goto fail; }
		for (i = 0; i < CCV_NAV_PATHS; i++)
		{
			unsigned long path;
			if (!ccv_read_u32(file, 4 + navTable + i * 4, &path))
			{ ccv_error(error, errorBytes, "LEV navigation table is truncated"); goto fail; }
			if (path != 0 && path < ptrMap) out->measured.navPaths++;
		}
	}
	fclose(file);
	ccv_derive_modes(out);
	ccv_error(error, errorBytes, "");
	return 1;
fail:
	fclose(file);
	memset(out, 0, sizeof *out);
	return 0;
}

const char *CustomContentVerify_ModeSlug(int mode)
{
	static const char *slugs[CTR_CCV_MODE_COUNT] = {
		"time_trial", "relic_race", "ctr_challenge", "arcade", "crystal_challenge"};
	return mode >= 0 && mode < CTR_CCV_MODE_COUNT ? slugs[mode] : "unknown";
}

const char *CustomContentVerify_EvidenceText(int evidence)
{
	if (evidence == CTR_CCV_YES) return "Yes";
	if (evidence == CTR_CCV_NO) return "No";
	return "Not tested";
}

#endif
