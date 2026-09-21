#ifndef NATIVE_FS_WINPATH_H
#define NATIVE_FS_WINPATH_H

#include <stddef.h>

// Freestanding Win32 path classification and extended-prefix logic for the
// UTF-8 filesystem layer (issue #334, slice 5). It operates on UTF-16 code unit
// arrays and therefore compiles and is tested on POSIX too, where no Windows API
// or <wchar.h> is available. The Windows translation unit feeds it the result of
// MultiByteToWideChar; the host harness feeds it literal arrays.
//
// Rules, corrected from the first slice:
//   * Only "X:\" or "X:/" followed by a separator is an absolute drive path.
//     A bare "X:" and a drive-relative "X:name" are not absolute and never get
//     an extended-length prefix.
//   * An already-prefixed path ("\\?\" or "\\.\") is left unchanged.
//   * Forward slashes are converted to backslashes before a prefix is added.
//   * A two-slash UNC path "\\server\share" becomes "\\?\UNC\server\share".
//
// Calling code must have at least 4 code units before looking at the prefix, so
// this header's checks never read past the array it is given.

typedef unsigned short NativeWinPathUnit; // one UTF-16 code unit

typedef enum
{
	NATIVE_WIN_PATH_EMPTY = 0,
	NATIVE_WIN_PATH_DRIVE_ABSOLUTE,
	NATIVE_WIN_PATH_DRIVE_RELATIVE,
	NATIVE_WIN_PATH_UNC,
	NATIVE_WIN_PATH_EXTENDED,
	NATIVE_WIN_PATH_RELATIVE
} NativeWinPathKind;

static inline int NativeWinPath_IsDriveLetter(NativeWinPathUnit c)
{
	return ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
}

static inline int NativeWinPath_IsSeparator(NativeWinPathUnit c)
{
	return (c == '\\') || (c == '/');
}

// True when the path already carries a Win32 extended-length prefix.
static inline int NativeWinPath_IsPrefixed(const NativeWinPathUnit *path, size_t len)
{
	if ((path == NULL) || (len < 4))
		return 0;

	return (path[0] == '\\') && (path[1] == '\\') && ((path[2] == '?') || (path[2] == '.')) && (path[3] == '\\');
}

static inline NativeWinPathKind NativeWinPath_Classify(const NativeWinPathUnit *path, size_t len)
{
	if ((path == NULL) || (len == 0))
		return NATIVE_WIN_PATH_EMPTY;

	if (NativeWinPath_IsPrefixed(path, len))
		return NATIVE_WIN_PATH_EXTENDED;

	if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/'))
		return NATIVE_WIN_PATH_UNC;

	if ((len >= 2) && NativeWinPath_IsDriveLetter(path[0]) && (path[1] == ':'))
	{
		// "X:\..." or "X:/..." is absolute; a bare "X:" or "X:name" is not.
		if ((len >= 3) && NativeWinPath_IsSeparator(path[2]))
			return NATIVE_WIN_PATH_DRIVE_ABSOLUTE;

		return NATIVE_WIN_PATH_DRIVE_RELATIVE;
	}

	return NATIVE_WIN_PATH_RELATIVE;
}

// Length in code units (excluding the terminating NUL) of the prefixed form, or
// 0 when the input is empty or NULL.
static inline size_t NativeWinPath_PrefixedLength(const NativeWinPathUnit *path, size_t len)
{
	NativeWinPathKind kind = NativeWinPath_Classify(path, len);

	switch (kind)
	{
	case NATIVE_WIN_PATH_EXTENDED:
		return len;
	case NATIVE_WIN_PATH_UNC:
		return 8u + (len - 2u); // "\\?\UNC\" + the part after the two leading slashes
	case NATIVE_WIN_PATH_DRIVE_ABSOLUTE:
		return 4u + len; // "\\?\" + the path
	default:
		return len;
	}
}

// Produce the prefixed, slash-normalized path in out. Returns 1 on success and
// 0 when out is too small or an argument is missing; the caller can size out
// from NativeWinPath_PrefixedLength. Already-prefixed input is copied verbatim
// (no slash conversion). Never reads past in[0..len-1].
static inline int NativeWinPath_Prefix(const NativeWinPathUnit *in, size_t len, NativeWinPathUnit *out, size_t outCount)
{
	size_t i;
	size_t needed;

	if ((in == NULL) || (out == NULL) || (outCount == 0))
		return 0;

	// Already prefixed: unchanged.
	if (NativeWinPath_IsPrefixed(in, len))
	{
		if (outCount < len + 1u)
			return 0;

		for (i = 0; i < len; i++)
			out[i] = in[i];
		out[len] = 0;
		return 1;
	}

	// Slash-normalize first, so the replacement and prefix steps see a single
	// separator form.
	if (outCount < len + 1u)
		return 0;

	for (i = 0; i < len; i++)
		out[i] = (in[i] == '/') ? '\\' : in[i];
	out[len] = 0;

	switch (NativeWinPath_Classify(out, len))
	{
	case NATIVE_WIN_PATH_UNC:
	{
		static const NativeWinPathUnit prefix[] = {'\\', '\\', '?', '\\', 'U', 'N', 'C', '\\'};

		needed = 8u + (len - 2u) + 1u;
		if (outCount < needed)
			return 0;

		// Move the "server\share" part up, then write "\\?\UNC\" in front.
		// Copy backwards so the move does not clobber unread source units.
		for (i = len - 2u; i > 0u; i--)
			out[8u + (i - 1u)] = out[2u + (i - 1u)];
		for (i = 0; i < 8u; i++)
			out[i] = prefix[i];
		out[8u + (len - 2u)] = 0;
		return 1;
	}
	case NATIVE_WIN_PATH_DRIVE_ABSOLUTE:
	{
		static const NativeWinPathUnit prefix[] = {'\\', '\\', '?', '\\'};

		needed = 4u + len + 1u;
		if (outCount < needed)
			return 0;

		// Copy backwards so the shift does not clobber unread source units.
		for (i = len + 1u; i > 0u; i--)
			out[4u + (i - 1u)] = out[i - 1u];
		for (i = 0; i < 4u; i++)
			out[i] = prefix[i];
		return 1;
	}
	default:
		return 1;
	}
}

#endif // NATIVE_FS_WINPATH_H
