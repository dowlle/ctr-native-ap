#ifndef NATIVE_SHA256_H
#define NATIVE_SHA256_H

// SHA-256 (FIPS 180-4), header-only and deliberately freestanding: it includes
// nothing from the engine and nothing from libc beyond size_t, so
// tools/test-custom-track-hash.c can pin it against the published NIST vectors
// out of engine, with no disc, no display and no seed.
//
// Why the port carries its own implementation rather than calling OpenSSL:
// OpenSSL is linked only on the CTR_AP target (CMakeLists.txt, the ap_net
// archive), and the custom-track loader is independent of CTR_AP -- a clean
// build with CTR_CUSTOM_TRACKS on must still be able to refuse a track whose
// bytes are not the bytes it was promised. The digest is used for content
// verification only, never for authentication, so a compact reference-shaped
// implementation is the right amount of code.
//
// Compiled ONLY when CTR_CUSTOM_TRACKS is defined, like the rest of the loader.

#ifdef CTR_CUSTOM_TRACKS

#include <stddef.h>

#define NATIVE_SHA256_DIGEST_BYTES 32
#define NATIVE_SHA256_HEX_BYTES    65 // 64 hex digits + NUL

struct NativeSha256Ctx
{
	unsigned int state[8];
	unsigned int lengthHi; // message length in BITS, high word
	unsigned int lengthLo; // message length in BITS, low word
	unsigned int bufferLen;
	unsigned char buffer[64];
};

static const unsigned int s_nativeSha256K[64] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
	0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
	0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
	0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
	0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
	0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
	0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

#define NATIVE_SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void NativeSha256_Compress(struct NativeSha256Ctx *ctx, const unsigned char *block)
{
	unsigned int w[64];
	unsigned int a, b, c, d, e, f, g, h;
	int i;

	for (i = 0; i < 16; i++)
	{
		w[i] = ((unsigned int)block[i * 4 + 0] << 24) | ((unsigned int)block[i * 4 + 1] << 16) |
		       ((unsigned int)block[i * 4 + 2] << 8) | ((unsigned int)block[i * 4 + 3]);
	}

	for (i = 16; i < 64; i++)
	{
		unsigned int s0 = NATIVE_SHA256_ROTR(w[i - 15], 7) ^ NATIVE_SHA256_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		unsigned int s1 = NATIVE_SHA256_ROTR(w[i - 2], 17) ^ NATIVE_SHA256_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	a = ctx->state[0];
	b = ctx->state[1];
	c = ctx->state[2];
	d = ctx->state[3];
	e = ctx->state[4];
	f = ctx->state[5];
	g = ctx->state[6];
	h = ctx->state[7];

	for (i = 0; i < 64; i++)
	{
		unsigned int S1 = NATIVE_SHA256_ROTR(e, 6) ^ NATIVE_SHA256_ROTR(e, 11) ^ NATIVE_SHA256_ROTR(e, 25);
		unsigned int ch = (e & f) ^ ((~e) & g);
		unsigned int temp1 = h + S1 + ch + s_nativeSha256K[i] + w[i];
		unsigned int S0 = NATIVE_SHA256_ROTR(a, 2) ^ NATIVE_SHA256_ROTR(a, 13) ^ NATIVE_SHA256_ROTR(a, 22);
		unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
		unsigned int temp2 = S0 + maj;

		h = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}

	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

static void NativeSha256_Init(struct NativeSha256Ctx *ctx)
{
	ctx->state[0] = 0x6a09e667u;
	ctx->state[1] = 0xbb67ae85u;
	ctx->state[2] = 0x3c6ef372u;
	ctx->state[3] = 0xa54ff53au;
	ctx->state[4] = 0x510e527fu;
	ctx->state[5] = 0x9b05688cu;
	ctx->state[6] = 0x1f83d9abu;
	ctx->state[7] = 0x5be0cd19u;
	ctx->lengthHi = 0;
	ctx->lengthLo = 0;
	ctx->bufferLen = 0;
}

static void NativeSha256_Update(struct NativeSha256Ctx *ctx, const void *data, size_t len)
{
	const unsigned char *p = (const unsigned char *)data;
	size_t i;

	for (i = 0; i < len; i++)
	{
		ctx->buffer[ctx->bufferLen++] = p[i];

		// 64-bit bit counter kept as two 32-bit halves: the port is 32-bit and a
		// track file is a few MiB, but the carry keeps the padding correct for
		// any input the harness might throw at it.
		unsigned int prevLo = ctx->lengthLo;
		ctx->lengthLo = prevLo + 8u;
		if (ctx->lengthLo < prevLo)
			ctx->lengthHi++;

		if (ctx->bufferLen == 64)
		{
			NativeSha256_Compress(ctx, ctx->buffer);
			ctx->bufferLen = 0;
		}
	}
}

static void NativeSha256_Final(struct NativeSha256Ctx *ctx, unsigned char digest[NATIVE_SHA256_DIGEST_BYTES])
{
	unsigned int lengthHi = ctx->lengthHi;
	unsigned int lengthLo = ctx->lengthLo;
	int i;

	ctx->buffer[ctx->bufferLen++] = 0x80;

	if (ctx->bufferLen > 56)
	{
		while (ctx->bufferLen < 64)
			ctx->buffer[ctx->bufferLen++] = 0;
		NativeSha256_Compress(ctx, ctx->buffer);
		ctx->bufferLen = 0;
	}

	while (ctx->bufferLen < 56)
		ctx->buffer[ctx->bufferLen++] = 0;

	ctx->buffer[56] = (unsigned char)((lengthHi >> 24) & 0xff);
	ctx->buffer[57] = (unsigned char)((lengthHi >> 16) & 0xff);
	ctx->buffer[58] = (unsigned char)((lengthHi >> 8) & 0xff);
	ctx->buffer[59] = (unsigned char)(lengthHi & 0xff);
	ctx->buffer[60] = (unsigned char)((lengthLo >> 24) & 0xff);
	ctx->buffer[61] = (unsigned char)((lengthLo >> 16) & 0xff);
	ctx->buffer[62] = (unsigned char)((lengthLo >> 8) & 0xff);
	ctx->buffer[63] = (unsigned char)(lengthLo & 0xff);

	NativeSha256_Compress(ctx, ctx->buffer);

	for (i = 0; i < 8; i++)
	{
		digest[i * 4 + 0] = (unsigned char)((ctx->state[i] >> 24) & 0xff);
		digest[i * 4 + 1] = (unsigned char)((ctx->state[i] >> 16) & 0xff);
		digest[i * 4 + 2] = (unsigned char)((ctx->state[i] >> 8) & 0xff);
		digest[i * 4 + 3] = (unsigned char)(ctx->state[i] & 0xff);
	}
}

// Render a digest as 64 lowercase hex digits plus a NUL.
static void NativeSha256_ToHex(const unsigned char digest[NATIVE_SHA256_DIGEST_BYTES], char out[NATIVE_SHA256_HEX_BYTES])
{
	static const char hexDigits[] = "0123456789abcdef";
	int i;

	for (i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		out[i * 2 + 0] = hexDigits[(digest[i] >> 4) & 0xf];
		out[i * 2 + 1] = hexDigits[digest[i] & 0xf];
	}
	out[NATIVE_SHA256_DIGEST_BYTES * 2] = '\0';
}

// Case-insensitive compare of a 64-digit hex digest against a candidate string.
// Returns 1 when they match. Any length other than exactly 64, or any non-hex
// character, is a non-match: an expected hash that is not a well-formed digest
// must never accidentally compare equal.
static int NativeSha256_HexEquals(const char *expected, const char actual[NATIVE_SHA256_HEX_BYTES])
{
	int i;

	if (expected == NULL)
		return 0;

	for (i = 0; i < NATIVE_SHA256_DIGEST_BYTES * 2; i++)
	{
		char e = expected[i];
		char a = actual[i];

		if (e >= 'A' && e <= 'F')
			e = (char)(e - 'A' + 'a');

		if (!((e >= '0' && e <= '9') || (e >= 'a' && e <= 'f')))
			return 0;

		if (e != a)
			return 0;
	}

	// Reject a longer string: "<64 correct digits>garbage" is not a digest.
	return expected[NATIVE_SHA256_DIGEST_BYTES * 2] == '\0';
}

#endif // CTR_CUSTOM_TRACKS

#endif // NATIVE_SHA256_H
