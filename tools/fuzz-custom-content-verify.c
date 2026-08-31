// Deterministic mutation harness for the load-safety boundary.
//
//   cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I. -Iinclude
//      tools/fuzz-custom-content-verify.c -o /tmp/fuzz-custom-content-verify
//   /tmp/fuzz-custom-content-verify [real.lev real.vrm]

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CTR_CUSTOM_TRACKS 1
#include "platform/native_custom_content_verify.c"

#define SEED_MAP 0x1200u
#define SEED_LEV_BYTES (4u + SEED_MAP + 4u)

static void put32(unsigned char *b, size_t at, unsigned long value)
{
	b[at] = (unsigned char)value; b[at + 1] = (unsigned char)(value >> 8);
	b[at + 2] = (unsigned char)(value >> 16); b[at + 3] = (unsigned char)(value >> 24);
}

static unsigned next_random(unsigned *state)
{
	unsigned x = *state;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	*state = x;
	return x;
}

static int fuzz_seed(const unsigned char *levSeed, size_t levBytes,
	                 const unsigned char *vrmSeed, size_t vrmBytes,
	                 unsigned rounds, unsigned randomSeed)
{
	unsigned accepted = 0;
	unsigned i;
	for (i = 0; i < rounds; i++)
	{
		struct CustomContentOwnedPair pair;
		char error[256];
		unsigned edits = 1 + next_random(&randomSeed) % 4;
		unsigned j;
		memset(&pair, 0, sizeof pair);
		pair.lev = (unsigned char *)malloc(levBytes);
		pair.vrm = (unsigned char *)malloc(vrmBytes);
		if (!pair.lev || !pair.vrm) return -1;
		memcpy(pair.lev, levSeed, levBytes); memcpy(pair.vrm, vrmSeed, vrmBytes);
		pair.levBytes = levBytes; pair.vrmBytes = vrmBytes;
		for (j = 0; j < edits; j++)
		{
			unsigned mutateVrm = next_random(&randomSeed) & 1u;
			unsigned char *bytes = mutateVrm ? pair.vrm : pair.lev;
			size_t count = mutateVrm ? pair.vrmBytes : pair.levBytes;
			size_t at = next_random(&randomSeed) % count;
			bytes[at] ^= (unsigned char)(1u << (next_random(&randomSeed) & 7u));
		}
		accepted += CustomContentVerify_ValidateLoadablePair(&pair, error, sizeof error) ? 1u : 0u;
		CustomContentVerify_FreePair(&pair);
	}
	return (int)accepted;
}

int main(int argc, char **argv)
{
	unsigned char lev[SEED_LEV_BYTES] = {0};
	unsigned char singleVrm[22] = {0};
	unsigned char packedVrm[34] = {0};
	struct CustomContentOwnedPair realPair;
	char error[256];
	int acceptedA, acceptedB, acceptedReal = 0;

	put32(lev, 0, SEED_MAP); put32(lev, 4 + SEED_MAP, 0);
	singleVrm[16] = 1; singleVrm[18] = 1;
	put32(packedVrm, 0, 0x20); put32(packedVrm, 4, 22);
	packedVrm[8 + 16] = 1; packedVrm[8 + 18] = 1; put32(packedVrm, 30, 0);
	acceptedA = fuzz_seed(lev, sizeof lev, singleVrm, sizeof singleVrm, 10000, 0x13579bdfu);
	acceptedB = fuzz_seed(lev, sizeof lev, packedVrm, sizeof packedVrm, 10000, 0x2468ace1u);
	if (acceptedA < 0 || acceptedB < 0) return 2;

	if (argc == 3)
	{
		if (!CustomContentVerify_AcquirePair(argv[1], argv[2], &realPair, error, sizeof error))
		{
			fprintf(stderr, "real seed acquisition failed: %s\n", error);
			return 2;
		}
		if (!CustomContentVerify_ValidateLoadablePair(&realPair, error, sizeof error))
		{
			fprintf(stderr, "real seed is not loadable: %s\n", error);
			CustomContentVerify_FreePair(&realPair);
			return 2;
		}
		acceptedReal = fuzz_seed(realPair.lev, realPair.levBytes, realPair.vrm, realPair.vrmBytes,
		                             100, 0xdeadbeefu);
		CustomContentVerify_FreePair(&realPair);
		if (acceptedReal < 0) return 2;
	}

	printf("custom-content mutation fuzz: 20000 built-in mutations, %d accepted", acceptedA + acceptedB);
	if (argc == 3) printf("; 100 real-seed mutations, %d accepted", acceptedReal);
	printf("; 0 harness failures\n");
	return 0;
}
