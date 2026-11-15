#ifndef AP_WARPPAD_DIGITS_LOGIC_H
#define AP_WARPPAD_DIGITS_LOGIC_H

// The number a closed warp pad floats beside its requirement icon (A5-1).
//
// Retail pads never ask for more than 19 of anything, so AH_WarpPad_LInB split
// the count as "tens is 1 or 0, ones is the rest", and the tens instance always
// drew the "1" model. An Archipelago pad can ask for 20 or more (an AnyRelic
// rule of 67 in a player report; random any-of rules reach 27). The ones digit
// then became 10 or more, and AH_WarpPad_AllWarppadNum, which runs every
// frame, read digit model header `ones - 1`: past the eight headers of the
// "1" to "8" model. The garbage frame pointer it copied crashed the hub render
// (RenderBucket_PrepareDrawContext reading mf->pos).
//
// Here the count is split into two real decimal digits, each digit maps to the
// model and header the retail ones digit already uses, and a count above 99 is
// shown as 99: a pad has two digit slots. The gate itself still uses the full
// count; only the floating number is capped.
//
// Freestanding on purpose: tools/test-warppad-digits.c checks it on the host,
// and tools/test-warppad-digits-production.py checks AH_WarpPad.c uses it.

// Model ids of the floating digits (common.h STATIC_BIG1, STATIC_BIG0,
// STATIC_BIG9). STATIC_BIG1 holds "1" to "8" as headers 0 to 7.
#define AP_WARPPAD_DIGIT_MODEL_1TO8 0x38
#define AP_WARPPAD_DIGIT_MODEL_0 0x6d
#define AP_WARPPAD_DIGIT_MODEL_9 0x6e
#define AP_WARPPAD_DIGIT_HEADERS_1TO8 8

// The largest number two digit slots can show.
#define AP_WARPPAD_DIGITS_MAX 99

// Split a requirement count into the pad's tens and ones digits. Counts at or
// below 19 split exactly as retail did. A negative count shows 0; a count
// above 99 shows 99.
static inline void ap_warppad_split_digits(int needed, short *tens, short *ones)
{
	if (needed < 0)
		needed = 0;
	if (needed > AP_WARPPAD_DIGITS_MAX)
		needed = AP_WARPPAD_DIGITS_MAX;
	*tens = (short)(needed / 10);
	*ones = (short)(needed % 10);
}

// The model a digit instance is born with: 0 and 9 have their own models, 1 to
// 8 share STATIC_BIG1. A digit outside 0 to 9 gets the "9" model, so the
// caller never indexes past STATIC_BIG1's headers.
static inline int ap_warppad_digit_model(int digit)
{
	if (digit == 0)
		return AP_WARPPAD_DIGIT_MODEL_0;
	if (digit >= 1 && digit <= 8)
		return AP_WARPPAD_DIGIT_MODEL_1TO8;
	return AP_WARPPAD_DIGIT_MODEL_9;
}

// The STATIC_BIG1 header that draws `digit`, or -1 when the digit has its own
// model (0, 9) or the model has too few headers. `numHeaders` is the loaded
// model's header count; the per-frame number pass must not read past it.
static inline int ap_warppad_digit_header(int digit, int numHeaders)
{
	if (digit < 1 || digit > AP_WARPPAD_DIGIT_HEADERS_1TO8)
		return -1;
	if (digit - 1 >= numHeaders)
		return -1;
	return digit - 1;
}

#endif
