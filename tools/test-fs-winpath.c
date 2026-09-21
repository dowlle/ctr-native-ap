// Behavioral harness for the freestanding Win32 path classification and prefix
// logic (issue #334, slice 5). The logic lives in
// include/platform/native_fs_winpath.h and operates on UTF-16 code unit arrays,
// so it is exercised here on the host without any Windows API:
//
//   * bare drive, drive-relative, absolute drive
//   * UNC and extended-length prefixes
//   * already-prefixed paths left unchanged
//   * forward slashes converted before the prefix is added
//   * trailing separator
//   * one and two character inputs (no out-of-bounds read)
//   * a path at and above 260 code units
//
//   cc -Wall -Wextra -I . -I include -o /tmp/test-fs-winpath tools/test-fs-winpath.c
//
// Exit 0 = every assertion held; failures are printed otherwise.

#include <stdio.h>
#include <string.h>

#include "platform/native_fs_winpath.h"

static int g_checks;
static int g_failures;

static void expect(int condition, const char *name)
{
	g_checks++;
	if (!condition)
	{
		g_failures++;
		printf("FAIL: %s\n", name);
	}
}

// ASCII string to UTF-16 code units. Returns the unit count (excluding NUL).
static size_t toWide(const char *ascii, NativeWinPathUnit *out, size_t outCount)
{
	size_t i = 0;

	while ((ascii[i] != '\0') && (i + 1u < outCount))
	{
		out[i] = (NativeWinPathUnit)(unsigned char)ascii[i];
		i++;
	}

	out[i] = 0;
	return i;
}

// Prefix an ASCII input and return the ASCII result in a static buffer.
static const char *prefixAscii(const char *ascii)
{
	static char result[2048];
	NativeWinPathUnit in[1024];
	NativeWinPathUnit out[1200];
	size_t len;
	size_t i;
	size_t outLen;

	len = toWide(ascii, in, sizeof(in) / sizeof(in[0]));
	expect(NativeWinPath_Prefix(in, len, out, sizeof(out) / sizeof(out[0])) == 1, "prefix: succeeds");
	outLen = NativeWinPath_PrefixedLength(in, len);

	for (i = 0; (i < outLen) && (i + 1u < sizeof(result)); i++)
		result[i] = (char)(unsigned char)out[i];
	result[i] = '\0';
	return result;
}

static void TestClassification(void)
{
	NativeWinPathUnit one[8];
	NativeWinPathUnit two[8];
	size_t len;

	// One character: can never be absolute or a drive.
	len = toWide("C", one, sizeof(one) / sizeof(one[0]));
	expect(len == 1, "classify: one-character input length");
	expect(NativeWinPath_Classify(one, len) == NATIVE_WIN_PATH_RELATIVE, "classify: single letter is relative");

	// Two characters: a bare drive is drive-relative, never absolute.
	len = toWide("C:", two, sizeof(two) / sizeof(two[0]));
	expect(NativeWinPath_Classify(two, len) == NATIVE_WIN_PATH_DRIVE_RELATIVE, "classify: bare drive is drive-relative");
	expect(NativeWinPath_Classify(NULL, 0) == NATIVE_WIN_PATH_EMPTY, "classify: NULL/empty");

	expect(NativeWinPath_Classify((const NativeWinPathUnit[]){'C', ':', 'n', 'a', 'm', 'e'}, 6) == NATIVE_WIN_PATH_DRIVE_RELATIVE,
	       "classify: X:name is drive-relative");
	expect(NativeWinPath_Classify((const NativeWinPathUnit[]){'C', ':', '\\', 'x'}, 4) == NATIVE_WIN_PATH_DRIVE_ABSOLUTE,
	       "classify: X:\\x is absolute");
	expect(NativeWinPath_Classify((const NativeWinPathUnit[]){'C', ':', '/'}, 3) == NATIVE_WIN_PATH_DRIVE_ABSOLUTE,
	       "classify: X:/ is absolute");
	expect(NativeWinPath_Classify((const NativeWinPathUnit[]){'\\', '\\', 's', 'r', 'v'}, 5) == NATIVE_WIN_PATH_UNC,
	       "classify: two slashes is UNC");
	expect(NativeWinPath_Classify((const NativeWinPathUnit[]){'\\', '\\', '?', '\\', 'x'}, 5) == NATIVE_WIN_PATH_EXTENDED,
	       "classify: \\\\?\\ is extended");
	expect(NativeWinPath_Classify((const NativeWinPathUnit[]){'\\', '\\', '.', '\\', 'x'}, 5) == NATIVE_WIN_PATH_EXTENDED,
	       "classify: \\\\.\\ is extended");
	expect(NativeWinPath_Classify((const NativeWinPathUnit[]){'/', '/', 's', 'r', 'v'}, 5) == NATIVE_WIN_PATH_UNC,
	       "classify: //srv is UNC");
}

static void TestPrefixing(void)
{
	// Bare drive and drive-relative get no prefix.
	expect(strcmp(prefixAscii("C:"), "C:") == 0, "prefix: bare drive unchanged");
	expect(strcmp(prefixAscii("C:name"), "C:name") == 0, "prefix: drive-relative unchanged");
	expect(strcmp(prefixAscii("relative\\path"), "relative\\path") == 0, "prefix: relative unchanged");

	// Absolute drive gets \\?\.
	expect(strcmp(prefixAscii("C:\\Games\\ctr-u.bin"), "\\\\?\\C:\\Games\\ctr-u.bin") == 0, "prefix: absolute drive prefixed");
	expect(strcmp(prefixAscii("C:/Games/ctr.bin"), "\\\\?\\C:\\Games\\ctr.bin") == 0, "prefix: forward slashes converted and prefixed");
	expect(strcmp(prefixAscii("D:\\"), "\\\\?\\D:\\") == 0, "prefix: trailing separator kept");

	// UNC becomes \\?\UNC\.
	expect(strcmp(prefixAscii("\\\\server\\share\\ctr.bin"), "\\\\?\\UNC\\server\\share\\ctr.bin") == 0, "prefix: UNC prefixed");
	expect(strcmp(prefixAscii("//server/share/ctr.bin"), "\\\\?\\UNC\\server\\share\\ctr.bin") == 0, "prefix: forward-slash UNC prefixed");

	// Already-prefixed paths are unchanged, including forward slashes.
	expect(strcmp(prefixAscii("\\\\?\\C:\\already"), "\\\\?\\C:\\already") == 0, "prefix: \\\\?\\ unchanged");
	expect(strcmp(prefixAscii("\\\\.\\COM1"), "\\\\.\\COM1") == 0, "prefix: \\\\.\\ unchanged");
	expect(strcmp(prefixAscii("\\\\?\\UNC\\server\\share"), "\\\\?\\UNC\\server\\share") == 0, "prefix: prefixed UNC unchanged");

	// One and two characters, no out-of-bounds read.
	expect(strcmp(prefixAscii("C"), "C") == 0, "prefix: single letter unchanged");
	expect(strcmp(prefixAscii("C:"), "C:") == 0, "prefix: two-character drive unchanged");

	// Empty.
	expect(strcmp(prefixAscii(""), "") == 0, "prefix: empty stays empty");
}

static void TestLongPaths(void)
{
	char input[600];
	char expected[700];
	NativeWinPathUnit in[600];
	NativeWinPathUnit out[700];
	size_t len;
	size_t i;
	size_t prefixedLength;
	char result[700];

	// A 260-unit path: the old magic number, and the boundary beyond which an
	// extended prefix is required.
	memset(input, 'a', 260);
	snprintf(input, 4, "C:\\");
	memset(input + 3, 'a', 257);
	input[260] = '\0';

	len = toWide(input, in, sizeof(in) / sizeof(in[0]));
	expect(len == 260, "long: 260-unit input");
	prefixedLength = NativeWinPath_PrefixedLength(in, len);
	expect(prefixedLength == 264, "long: 260-unit prefixed length is 264");

	expect(NativeWinPath_Prefix(in, len, out, sizeof(out) / sizeof(out[0])) == 1, "long: 260-unit prefix succeeds");
	for (i = 0; i < prefixedLength; i++)
		result[i] = (char)(unsigned char)out[i];
	result[i] = '\0';
	snprintf(expected, sizeof(expected), "\\\\?\\%s", input);
	expect(strcmp(result, expected) == 0, "long: 260-unit path prefixed");

	// Above 260: same shape, longer.
	memset(input, 'b', 400);
	snprintf(input, 4, "C:\\");
	memset(input + 3, 'b', 397);
	input[400] = '\0';
	len = toWide(input, in, sizeof(in) / sizeof(in[0]));
	prefixedLength = NativeWinPath_PrefixedLength(in, len);
	expect(prefixedLength == 404, "long: 400-unit prefixed length is 404");
	expect(NativeWinPath_Prefix(in, len, out, sizeof(out) / sizeof(out[0])) == 1, "long: 400-unit prefix succeeds");
	expect((out[0] == '\\') && (out[1] == '\\') && (out[2] == '?') && (out[3] == '\\') && (out[4] == 'C') && (out[5] == ':'),
	       "long: 400-unit path has the extended prefix");

	// A too-small output buffer is refused, not overflowed.
	expect(NativeWinPath_Prefix(in, len, out, 3) == 0, "long: too-small output refused");
}

int main(void)
{
	TestClassification();
	TestPrefixing();
	TestLongPaths();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
