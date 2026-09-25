#ifndef NATIVE_CUSTOM_CONTENT_VERIFY_H
#define NATIVE_CUSTOM_CONTENT_VERIFY_H

#ifdef CTR_CUSTOM_TRACKS

#include <stddef.h>
#include <platform/native_sha256.h>

#define CTR_CCV_MODE_COUNT 5
#define CTR_CCV_REASON_MAX 192
#define CTR_CCV_FAULT_AFTER_LEV_COPY 1
#define CTR_CCV_FAULT_AFTER_VRM_COPY 2
#define CTR_CCV_FAULT_AFTER_HASHING 3

enum CustomContentEvidence
{
	CTR_CCV_INDETERMINATE = 0,
	CTR_CCV_NOT_DETECTED = 1,
	CTR_CCV_DETECTED = 2,
	CTR_CCV_ERROR = 3
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
	int loadable;
	struct CustomContentMeasurements measured;
	struct CustomContentModeEvidence fileAnalysis[CTR_CCV_MODE_COUNT];
};

struct CustomContentOwnedPair
{
	unsigned char *lev;
	unsigned char *vrm;
	size_t levBytes;
	size_t vrmBytes;
	struct CustomContentVerification report;
};

// Analysis and loader authority are deliberately separate. AcquirePair owns
// and hashes the exact bytes once. AnalyzePair may report cautious structural
// evidence but never grants loader authority. Only ValidateLoadablePair sets
// report.loadable and may be followed by a native loader handoff.
int CustomContentVerify_AcquirePair(const char *levPath, const char *vrmPath,
	                                 struct CustomContentOwnedPair *pair,
	                                 char *error, size_t errorBytes);
int CustomContentVerify_AnalyzePair(struct CustomContentOwnedPair *pair,
	                                 char *error, size_t errorBytes);
int CustomContentVerify_ValidateLoadablePair(struct CustomContentOwnedPair *pair,
	                                          char *error, size_t errorBytes);

// Convenience API for the reporting CLI: acquire, analyze, and validate.
int CustomContentVerify_Files(const char *levPath, const char *vrmPath,
	                           struct CustomContentVerification *out,
	                           char *error, size_t errorBytes);

// Convenience API for the loader: acquire and validate, retaining the owned
// immutable buffers on success.
int CustomContentVerify_ReadPair(const char *levPath, const char *vrmPath,
	                              struct CustomContentOwnedPair *pair,
	                              char *error, size_t errorBytes);
void CustomContentVerify_FreePair(struct CustomContentOwnedPair *pair);

/* Why this pair cannot run an eight-kart Arcade Single Race, as short plain
   text for the menus (at most two 13-character lines: "No AI paths",
   "Grid: 3 of 8", "No lap checkpoints", "Files failed checks"), or "" when the file
   analysis allows it: Arcade structures detected and eight start positions.
   Returns 1 when allowed. Same rule the offline race checks before loading. */
#define CTR_CCV_ARCADE_REASON_MAX 48
int CustomContentVerify_ArcadeRaceReason(const struct CustomContentVerification *report,
	                                      char *reason, size_t reasonBytes);
/* The same for a one-kart Time Trial: a loadable LEV with a lap checkpoint
   graph. A start position is not required to be non-zero: Saphi Time Trial
   tracks start the kart at the world origin facing angle 0, which is eight
   all-zero start slots (measured as no start positions). */
int CustomContentVerify_TimeTrialReason(const struct CustomContentVerification *report,
	                                     char *reason, size_t reasonBytes);

const char *CustomContentVerify_ModeSlug(int mode);
const char *CustomContentVerify_EvidenceText(int evidence);

#endif
#endif
