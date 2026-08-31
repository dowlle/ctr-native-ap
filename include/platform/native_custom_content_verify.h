#ifndef NATIVE_CUSTOM_CONTENT_VERIFY_H
#define NATIVE_CUSTOM_CONTENT_VERIFY_H

#ifdef CTR_CUSTOM_TRACKS

#include <stddef.h>
#include <platform/native_sha256.h>

#define CTR_CCV_MODE_COUNT 5
#define CTR_CCV_REASON_MAX 192

enum CustomContentEvidence
{
	CTR_CCV_NOT_TESTED = 0,
	CTR_CCV_NO = 1,
	CTR_CCV_YES = 2
};

enum CustomContentMode
{
	CTR_CCV_TIME_TRIAL = 0,
	CTR_CCV_RELIC_RACE,
	CTR_CCV_CTR_CHALLENGE,
	CTR_CCV_ARCADE,
	CTR_CCV_CRYSTAL_CHALLENGE
};

struct CustomContentModeEvidence
{
	int result;
	char reason[CTR_CCV_REASON_MAX];
};

struct CustomContentMeasurements
{
	unsigned long instances;
	unsigned long checkpoints;
	unsigned long spawns;
	unsigned long navPaths;
	unsigned long ordinaryCrates;
	unsigned long fruitCrates;
	unsigned long looseWumpa;
	unsigned long relicCrates;
	unsigned long letterC;
	unsigned long letterT;
	unsigned long letterR;
	unsigned long crystals;
	unsigned long startLines;
	unsigned long finishLines;
};

struct CustomContentVerification
{
	char levSha256[NATIVE_SHA256_HEX_BYTES];
	char vrmSha256[NATIVE_SHA256_HEX_BYTES];
	unsigned long levBytes;
	unsigned long vrmBytes;
	struct CustomContentMeasurements measured;
	struct CustomContentModeEvidence fileAnalysis[CTR_CCV_MODE_COUNT];
};

// Verifies and measures an exact LEV/VRM pair. Returns one only when both files
// are readable and the LEV's bounded structures are internally consistent.
// A structurally valid absence is a NO mode result; malformed input is an
// overall failure and never degrades to a capability answer.
int CustomContentVerify_Files(const char *levPath, const char *vrmPath,
	                           struct CustomContentVerification *out,
	                           char *error, size_t errorBytes);

const char *CustomContentVerify_ModeSlug(int mode);
const char *CustomContentVerify_EvidenceText(int evidence);

#endif
#endif
