#ifndef AP_REWARD_TEXT_H
#define AP_REWARD_TEXT_H
#ifdef CTR_AP

#include <stdio.h>
#include <string.h>

/*
 * Pure, bounded English sentence builder for small-font reward surfaces
 * (issue #330 subtitle now; the warp-pad and track-pool lines later).
 *
 *   own item:     HAVE A <ITEM>.
 *   foreign item: HAVE <PLAYER>'S <ITEM>.
 *
 * ONE display bound: AP_REWARD_TEXT_MAX visible characters, excluding the NUL.
 * FONT_SMALL advances 13 pixels per glyph and a period 7 (game/zGlobal_DATA.c
 * font_charPixWidth / font_puncPixWidth); the subtitle wraps at 460 pixels
 * (game/233/CS_Thread.c), so 35 glyphs is the widest line that still fits a
 * single row (35 * 13 = 455 <= 460, while 36 * 13 = 468 already wraps). Every
 * output is uppercase, NUL-terminated, limited to that bound, and marks a cut
 * with three trailing periods. Unsupported bytes, including every UTF-8
 * multibyte byte and every control character, become spaces; runs of spaces
 * collapse and the ends are trimmed. A component with no supported non-space
 * glyph, an empty component, or the apclientpp "Unknown" placeholder makes the
 * build fail so the caller can keep the retail string.
 */

#define AP_REWARD_TEXT_MAX 35

// Longest component inspected before the sentence-level bound applies. The
// item/player buffers handed in are 128 bytes (ap_hooks.c); this stays ample.
#define AP_REWARD_TEXT_SCRATCH 128

// The NTSC-U small-font glyph set (mirrors AP_CeremonySanitize, ap_hooks.c).
// Button-icon glyphs @ [ ^ * and everything else are deliberately excluded.
static inline int AP_RewardTextGlyphSupported(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
	       c == ' ' || c == '!' || c == '%' || c == '\'' || c == ',' ||
	       c == '-' || c == '.' || c == '/' || c == ':' || c == '<' ||
	       c == '=' || c == '>' || c == '?' || c == '_' || c == '+';
}

// Uppercase, map unsupported/UTF-8/control bytes to spaces, collapse runs of
// spaces and trim. Returns 1 when at least one supported non-space glyph
// survives; 0 when the result is empty (the caller keeps the retail string).
static inline int AP_RewardTextName(const char *in, char *out, int cap)
{
	int i;
	int j = 0;
	int pendingSpace = 0;
	int anyGlyph = 0;

	if (!out || cap <= 0)
		return 0;
	out[0] = '\0';
	if (!in)
		return 0;

	for (i = 0; in[i] != '\0' && j < cap; i++)
	{
		unsigned char c = (unsigned char)in[i];

		if (c >= 'a' && c <= 'z')
			c = (unsigned char)(c - ('a' - 'A'));

		if (!AP_RewardTextGlyphSupported(c) || c == ' ')
		{
			if (anyGlyph)
				pendingSpace = 1;
			continue;
		}

		if (pendingSpace)
		{
			// Room for the separating space AND the glyph, or stop here.
			if (j + 1 >= cap)
				break;
			out[j++] = ' ';
			pendingSpace = 0;
		}

		out[j++] = (char)c;
		anyGlyph = 1;
	}

	out[j] = '\0';
	return anyGlyph;
}

// The apclientpp placeholder for a name missing from the synced DataPackage.
static inline int AP_RewardTextIsUnknown(const char *s)
{
	return !s || !s[0] ||
	       (s[0] == 'U' && s[1] == 'N' && s[2] == 'K' && s[3] == 'N' &&
	        s[4] == 'O' && s[5] == 'W' && s[6] == 'N' && s[7] == '\0');
}

// Copy `line` into `out` under the single display bound. A line longer than the
// bound keeps the leading characters that fit and closes with three periods.
static inline int AP_RewardTextFit(const char *line, char *out, int cap)
{
	int len;

	if (!out || cap < AP_REWARD_TEXT_MAX + 1)
		return 0;

	len = (int)strlen(line);
	if (len <= AP_REWARD_TEXT_MAX)
	{
		memcpy(out, line, (size_t)len + 1);
		return 1;
	}

	memcpy(out, line, (size_t)(AP_REWARD_TEXT_MAX - 3));
	out[AP_REWARD_TEXT_MAX - 3] = '.';
	out[AP_REWARD_TEXT_MAX - 2] = '.';
	out[AP_REWARD_TEXT_MAX - 1] = '.';
	out[AP_REWARD_TEXT_MAX] = '\0';
	return 1;
}

// Build the bounded sentence. `out` must hold AP_REWARD_TEXT_MAX + 1 bytes.
// Returns 1 on success, 0 when the caller must keep the retail string.
static inline int AP_RewardTextBuild(char *out, int cap,
                                     const char *itemRaw,
                                     const char *playerRaw,
                                     int own)
{
	char item[AP_REWARD_TEXT_SCRATCH + 1];
	char player[AP_REWARD_TEXT_SCRATCH + 1];
	char line[2 * AP_REWARD_TEXT_SCRATCH + 16];

	if (!out || cap <= 0)
		return 0;
	out[0] = '\0';

	if (!AP_RewardTextName(itemRaw, item, AP_REWARD_TEXT_SCRATCH))
		return 0;
	if (AP_RewardTextIsUnknown(item))
		return 0;

	if (own)
	{
		snprintf(line, sizeof line, "HAVE A %s.", item);
	}
	else
	{
		if (!AP_RewardTextName(playerRaw, player, AP_REWARD_TEXT_SCRATCH))
			return 0;
		if (AP_RewardTextIsUnknown(player))
			return 0;
		snprintf(line, sizeof line, "HAVE %s'S %s.", player, item);
	}

	return AP_RewardTextFit(line, out, cap);
}

#endif
#endif
