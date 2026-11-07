#ifndef CTR_NATIVE_TEXT_EDIT_LOGIC_H
#define CTR_NATIVE_TEXT_EDIT_LOGIC_H

// Pure cursor editing for the shared NativeText field (platform/native_platform.c).
//
// Kept in a header with no engine, no SDL and no libc dependency (byte loops
// instead of memmove, matching native_platform.c, which tracks length by hand
// to avoid <string.h>) so the platform layer and the host harness
// (tools/test-text-cursor.c) drive the exact same rules, the way
// ap_trap_sched_logic.h does for the trap scheduler.
//
// THE MODEL. The field is a NUL-terminated byte buffer with a length and a
// cursor, both byte offsets, 0 <= cursor <= len. The cursor always rests on a
// codepoint boundary: a multi-byte UTF-8 sequence (possible in a starting
// value handed in by a menu, for example a track title from a pack
// descriptor) is stepped over and deleted as one character. Typed input is
// filtered to printable ASCII by the platform layer before it reaches
// NativeTextEdit_Insert, so insertion never creates a sequence; the UTF-8
// handling here exists for movement and deletion over pre-existing text.
//
// Every function returns the (possibly unchanged) length and keeps the buffer
// NUL-terminated. A full buffer refuses inserts, exactly as the old
// append-only path did.

// Length in bytes of the UTF-8 codepoint starting at byte i (0 when i >= len).
// A broken sequence (a lead byte without its continuation bytes, or a stray
// continuation byte) counts as one byte, so it can always be stepped over and
// deleted instead of trapping the cursor.
static inline int NativeTextEdit_GlyphLen(const char *buf, int len, int i)
{
	int n, k;
	unsigned char c;
	if ((buf == 0) || (i < 0) || (i >= len)) return 0;
	c = (unsigned char)buf[i];
	if ((c & 0x80) == 0) return 1;
	if ((c & 0xE0) == 0xC0) n = 2;
	else if ((c & 0xF0) == 0xE0) n = 3;
	else if ((c & 0xF8) == 0xF0) n = 4;
	else return 1;
	for (k = 1; k < n; k++)
		if ((i + k >= len) || (((unsigned char)buf[i + k] & 0xC0) != 0x80))
			return 1;
	return n;
}

// One codepoint left; already at the start stays.
static inline int NativeTextEdit_CursorLeft(const char *buf, int cursor)
{
	if ((buf == 0) || (cursor <= 0)) return 0;
	cursor--;
	while ((cursor > 0) && (((unsigned char)buf[cursor] & 0xC0) == 0x80)) cursor--;
	return cursor;
}

// One codepoint right; already at the end stays.
static inline int NativeTextEdit_CursorRight(const char *buf, int len, int cursor)
{
	int n;
	if ((buf == 0) || (cursor < 0) || (cursor >= len)) return len < 0 ? 0 : len;
	n = NativeTextEdit_GlyphLen(buf, len, cursor);
	if (n < 1) n = 1;
	cursor += n;
	return cursor > len ? len : cursor;
}

// Insert one byte at the cursor, advancing the cursor past it. Returns the new
// length; a full buffer (cap includes the NUL) refuses and returns len.
static inline int NativeTextEdit_Insert(char *buf, int cap, int len, int *cursor, char c)
{
	int i;
	if ((buf == 0) || (cursor == 0)) return len;
	if (*cursor < 0) *cursor = 0;
	if (*cursor > len) *cursor = len;
	if (len >= cap - 1) return len;
	for (i = len; i > *cursor; i--) buf[i] = buf[i - 1];
	buf[*cursor] = c;
	(*cursor)++;
	buf[len + 1] = '\0';
	return len + 1;
}

// Delete the codepoint before the cursor, moving the cursor onto its place.
// Returns the new length; a cursor at the start changes nothing.
static inline int NativeTextEdit_Backspace(char *buf, int len, int *cursor)
{
	int at, n, i;
	if ((buf == 0) || (cursor == 0)) return len;
	if (*cursor <= 0) { *cursor = 0; return len; }
	if (*cursor > len) *cursor = len;
	at = NativeTextEdit_CursorLeft(buf, *cursor);
	n = *cursor - at;
	for (i = at; i + n < len; i++) buf[i] = buf[i + n];
	len -= n;
	buf[len] = '\0';
	*cursor = at;
	return len;
}

// Delete the codepoint under the cursor; the cursor stays. Returns the new
// length; a cursor at the end changes nothing.
static inline int NativeTextEdit_Delete(char *buf, int len, int cursor)
{
	int n, i;
	if ((buf == 0) || (cursor < 0) || (cursor >= len)) return len;
	n = NativeTextEdit_GlyphLen(buf, len, cursor);
	if (n < 1) return len;
	for (i = cursor; i + n < len; i++) buf[i] = buf[i + n];
	len -= n;
	buf[len] = '\0';
	return len;
}

#endif
