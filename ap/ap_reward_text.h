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
 * single row (35 * 13 = 455 <= 460, while 36 * 13 = 468 already wraps).
 *
 * Truncation is per component, never over the assembled sentence. The literal
 * "HAVE ", "'S " and "." parts are preserved, so a foreign line always keeps a
 * recognizable player AND item joined by the possessive. A component that does
 * not fit its field is shortened to a visible prefix closed with an explicit
 * three-dot ellipsis; the ellipsis takes the place of the sentence period, so a
 * cut line never doubles the full stop. The complete item is preferred when it
 * fits beside a shortened-but-recognizable player; when both names are long,
 * each keeps an identifiable prefix. This fixes the earlier sentence-level cut
 * that could reduce a foreign line to only `HAVE <PLAYER>...`, losing the item
 * entirely.
 *
 * Every output is uppercase, NUL-terminated and limited to that bound.
 * Unsupported bytes, including every UTF-8 multibyte byte and every control
 * character, become spaces; runs of spaces collapse and the ends are trimmed.
 * A component with no supported non-space glyph, an empty component, or the
 * apclientpp "Unknown" placeholder makes the build fail so the caller can keep
 * the retail string.
 */

#define AP_REWARD_TEXT_MAX 35

// Longest component inspected before the per-component bound applies. The
// item/player buffers handed in are 128 bytes (ap_hooks.c); this stays ample.
#define AP_REWARD_TEXT_SCRATCH 128

// Narrowest a shortened component may become: a visible prefix of at least
// four glyphs plus the three-dot ellipsis. A field below this is not considered
// a recognizable component, so the layout falls through to a wider split.
#define AP_REWARD_TEXT_MIN_PART 7

// Fixed literal widths of the two sentence shapes.
#define AP_REWARD_TEXT_OWN_PREFIX 7  // "HAVE A "
#define AP_REWARD_TEXT_FOREIGN_PREFIX 5  // "HAVE "
#define AP_REWARD_TEXT_POSSESSIVE 3  // "'S "
#define AP_REWARD_TEXT_STOP 1  // "."

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

// Append one component at out[*pos] within `budget` visible characters. The
// component is copied whole when it fits; otherwise only its leading
// (budget - 3) glyphs survive, closed by an explicit "...". Callers pass a
// budget of at least AP_REWARD_TEXT_MIN_PART so a cut prefix stays readable.
static inline void AP_RewardTextCopyCut(char *out, int *pos, const char *s,
                                        int budget)
{
	int len = (int)strlen(s);

	if (len <= budget)
	{
		memcpy(out + *pos, s, (size_t)len);
		*pos += len;
		return;
	}

	memcpy(out + *pos, s, (size_t)(budget - 3));
	out[*pos + budget - 3] = '.';
	out[*pos + budget - 2] = '.';
	out[*pos + budget - 1] = '.';
	*pos += budget;
}

// Append a terminating component field of `budget` visible characters. The
// field owns the sentence terminator: when the text plus a period fits, the
// period is the stop; otherwise the final three glyphs are replaced by "...".
// Either way the field consumes exactly its budget.
static inline void AP_RewardTextCopyTail(char *out, int *pos, const char *s,
                                         int budget)
{
	int len = (int)strlen(s);

	if (len + AP_REWARD_TEXT_STOP <= budget)
	{
		memcpy(out + *pos, s, (size_t)len);
		*pos += len;
		out[(*pos)++] = '.';
		return;
	}

	memcpy(out + *pos, s, (size_t)(budget - 3));
	out[*pos + budget - 3] = '.';
	out[*pos + budget - 2] = '.';
	out[*pos + budget - 1] = '.';
	*pos += budget;
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
	int pos;
	int itemLen;
	int playerLen;
	int region;

	if (!out || cap < AP_REWARD_TEXT_MAX + 1)
		return 0;
	out[0] = '\0';

	if (!AP_RewardTextName(itemRaw, item, AP_REWARD_TEXT_SCRATCH))
		return 0;
	if (AP_RewardTextIsUnknown(item))
		return 0;

	itemLen = (int)strlen(item);

	if (own)
	{
		// "HAVE A " + item field, where the field includes the final period.
		region = AP_REWARD_TEXT_MAX - AP_REWARD_TEXT_OWN_PREFIX;
		memcpy(out, "HAVE A ", AP_REWARD_TEXT_OWN_PREFIX);
		pos = AP_REWARD_TEXT_OWN_PREFIX;
		AP_RewardTextCopyTail(out, &pos, item, region);
		out[pos] = '\0';
		return 1;
	}

	if (!AP_RewardTextName(playerRaw, player, AP_REWARD_TEXT_SCRATCH))
		return 0;
	if (AP_RewardTextIsUnknown(player))
		return 0;

	playerLen = (int)strlen(player);

	// "HAVE " + player field + "'S " + item field. The item field owns the
	// final period, so the two fields share this region.
	region = AP_REWARD_TEXT_MAX - AP_REWARD_TEXT_FOREIGN_PREFIX -
	         AP_REWARD_TEXT_POSSESSIVE;
	memcpy(out, "HAVE ", AP_REWARD_TEXT_FOREIGN_PREFIX);
	pos = AP_REWARD_TEXT_FOREIGN_PREFIX;

	if (playerLen + itemLen + AP_REWARD_TEXT_STOP <= region)
	{
		// Both names are short: keep the plain retail-shaped sentence.
		memcpy(out + pos, player, (size_t)playerLen);
		pos += playerLen;
		memcpy(out + pos, "'S ", AP_REWARD_TEXT_POSSESSIVE);
		pos += AP_REWARD_TEXT_POSSESSIVE;
		AP_RewardTextCopyTail(out, &pos, item, itemLen + AP_REWARD_TEXT_STOP);
	}
	else if (itemLen + AP_REWARD_TEXT_STOP + AP_REWARD_TEXT_MIN_PART <= region)
	{
		// The complete item fits; shorten the player to make room.
		int playerBudget = region - itemLen - AP_REWARD_TEXT_STOP;

		AP_RewardTextCopyCut(out, &pos, player, playerBudget);
		memcpy(out + pos, "'S ", AP_REWARD_TEXT_POSSESSIVE);
		pos += AP_REWARD_TEXT_POSSESSIVE;
		memcpy(out + pos, item, (size_t)itemLen);
		pos += itemLen;
		out[pos++] = '.';
	}
	else if (playerLen + AP_REWARD_TEXT_MIN_PART <= region)
	{
		// A short recipient fits whole; shorten the item instead.
		int itemBudget = region - playerLen;

		memcpy(out + pos, player, (size_t)playerLen);
		pos += playerLen;
		memcpy(out + pos, "'S ", AP_REWARD_TEXT_POSSESSIVE);
		pos += AP_REWARD_TEXT_POSSESSIVE;
		AP_RewardTextCopyTail(out, &pos, item, itemBudget);
	}
	else
	{
		// Both names are long: give each an identifiable shortened prefix and
		// the explicit ellipsis. The possessive separator stays between them.
		int playerBudget = region / 2;
		int itemBudget = region - playerBudget;

		AP_RewardTextCopyCut(out, &pos, player, playerBudget);
		memcpy(out + pos, "'S ", AP_REWARD_TEXT_POSSESSIVE);
		pos += AP_REWARD_TEXT_POSSESSIVE;
		AP_RewardTextCopyTail(out, &pos, item, itemBudget);
	}

	out[pos] = '\0';
	return 1;
}

#endif
#endif
