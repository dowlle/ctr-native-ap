// Behavioral harness for the pure one-click-connect parser (issue #334,
// implementation slice 1). It includes the production unit directly, so these
// assertions run against the code main.c compiles, not a copy.
//
//   cc -Wall -Wextra -I . -I include -o /tmp/test-launch-request tools/test-launch-request.c
//
// Exit 0 = every assertion held; failures are printed otherwise.
//
// Covers the slice's Tests heading: malformed escapes, duplicate keys, unknown
// keys (including the removed `password` key), fragments, authority confusion
// (userinfo, '@', path segments other than `connect`), IPv4, bracketed IPv6,
// DNS names, control characters, invalid UTF-8, overlong encodings, empty /
// zero / overflowing port, every length limit at limit and limit+1, output
// unchanged on failure, and diagnostics free of the URI and field values.

#include <stdio.h>
#include <string.h>

#include "platform/native_launch_request.c"

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

static NativeLaunchRequestStatus runParse(const char *uri, NativeLaunchRequest *out)
{
	NativeLaunchRequest scratch;
	NativeLaunchRequest *target = (out != NULL) ? out : &scratch;

	memset(target, 0xAB, sizeof(*target));
	return NativeLaunchRequest_Parse(uri, target);
}

static void expectStatus(const char *uri, NativeLaunchRequestStatus want, const char *name)
{
	NativeLaunchRequestStatus got = runParse(uri, NULL);

	g_checks++;
	if (got != want)
	{
		g_failures++;
		printf("FAIL: %s (want %s, got %s)\n", name,
		       NativeLaunchRequest_StatusText(want), NativeLaunchRequest_StatusText(got));
	}
}

static void expectOk(const char *uri, const char *host, unsigned int port,
                     const char *slot, const char *room, const char *name)
{
	NativeLaunchRequest request;
	NativeLaunchRequestStatus got = NativeLaunchRequest_Parse(uri, &request);
	int ok = got == NATIVE_LAUNCH_REQUEST_OK && strcmp(request.host, host) == 0 &&
	         request.port == port && strcmp(request.slot, slot) == 0 &&
	         strcmp(request.room, room) == 0;

	g_checks++;
	if (!ok)
	{
		g_failures++;
		printf("FAIL: %s (status %s)\n", name, NativeLaunchRequest_StatusText(got));
	}
}

static void TestValidRequests(void)
{
	expectOk("ctr-ap://connect?host=127.0.0.1&port=38281&slot=Player&room=abc123",
	         "127.0.0.1", 38281, "Player", "abc123", "canonical IPv4 request");
	expectOk("ctr-ap://connect/?host=127.0.0.1&port=38281&slot=Player&room=abc123",
	         "127.0.0.1", 38281, "Player", "abc123", "bare trailing slash accepted");
	expectOk("ctr-ap://connect?host=example.org&port=1&slot=P&room=r",
	         "example.org", 1, "P", "r", "minimum decimal port");
	expectOk("ctr-ap://connect?host=example.org&port=65535&slot=P&room=r",
	         "example.org", 65535, "P", "r", "maximum decimal port");
	expectOk("ctr-ap://connect?host=example.org&port=00080&slot=P&room=r",
	         "example.org", 80, "P", "r", "leading zeros are still decimal");
	expectOk("ctr-ap://connect?host=%65xample.org&port=38281&slot=Pla%79er&room=a%2Db",
	         "example.org", 38281, "Player", "a-b", "percent-encoded fields decode");
	expectOk("ctr-ap://connect?host=example.org&port=38281&slot=caf%C3%A9&room=%C3%A9",
	         "example.org", 38281, "caf\xC3\xA9", "\xC3\xA9", "valid multi-byte UTF-8 accepted");
	expectOk("ctr-ap://connect?host=localhost&port=38281&slot=a%20b&room=r",
	         "localhost", 38281, "a b", "r", "space inside slot accepted");
	expectOk("ctr-ap://connect?host=sub.domain.example.com&port=38281&slot=P&room=r",
	         "sub.domain.example.com", 38281, "P", "r", "multi-label DNS name accepted");
}

static void TestSchemeAndAuthority(void)
{
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_OK, "lowercase scheme accepted");
	expectStatus("CTR-AP://connect?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_SCHEME, "uppercase scheme rejected");
	expectStatus("http://connect?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_SCHEME, "foreign scheme rejected");
	expectStatus("ctr-ap:/connect?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_SCHEME, "one-slash scheme rejected");
	expectStatus("ctr-ap://connect",
	             NATIVE_LAUNCH_REQUEST_ERR_QUERY, "missing query rejected");
	expectStatus("ctr-ap://connect?",
	             NATIVE_LAUNCH_REQUEST_ERR_MISSING_KEY, "empty query rejected");
	expectStatus("ctr-ap://race?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_AUTHORITY, "wrong authority rejected");
	expectStatus("ctr-ap://user@connect?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_AUTHORITY, "userinfo in authority rejected");
	expectStatus("ctr-ap://connect/extra?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_AUTHORITY, "path segment after connect rejected");
	expectStatus("ctr-ap://foo/connect?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_AUTHORITY, "connect as a later path segment rejected");
	expectStatus("ctr-ap://con%6Eect?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_AUTHORITY, "encoded authority is not decoded to connect");
}

static void TestFragments(void)
{
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=c#frag",
	             NATIVE_LAUNCH_REQUEST_ERR_FRAGMENT, "trailing fragment rejected");
	expectStatus("ctr-ap://connect#f?host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_FRAGMENT, "fragment before query rejected");
	expectStatus("ctr-ap://connect?host=a%23b&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "encoded hash is a host byte, not a fragment");
}

static void TestEscapes(void)
{
	expectStatus("ctr-ap://connect?host=%&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_ESCAPE, "lone percent rejected");
	expectStatus("ctr-ap://connect?host=%2&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_ESCAPE, "one hex digit rejected");
	expectStatus("ctr-ap://connect?host=%zz&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_ESCAPE, "non-hex escape rejected");
	expectStatus("ctr-ap://connect?host=%2G&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_ESCAPE, "half-hex escape rejected");
	expectStatus("ctr-ap://connect?host=%G0&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_ESCAPE, "high nibble non-hex rejected");
	expectStatus("ctr-ap://connect?ho%st=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_ESCAPE, "malformed escape in key rejected");
}

static void TestDuplicateAndUnknownKeys(void)
{
	expectStatus("ctr-ap://connect?host=a&host=b&port=1&slot=c&room=d",
	             NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY, "duplicate host rejected");
	expectStatus("ctr-ap://connect?host=a&ho%73t=b&port=1&slot=c&room=d",
	             NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY, "percent-spelled duplicate key rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&port=2&slot=c&room=d",
	             NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY, "duplicate port rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=c&slot=d&room=e",
	             NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY, "duplicate slot rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=c&room=d&room=e",
	             NATIVE_LAUNCH_REQUEST_ERR_DUPLICATE_KEY, "duplicate room rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=c&password=secret",
	             NATIVE_LAUNCH_REQUEST_ERR_UNKNOWN_KEY, "password key rejected as unknown");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=c&game=Crash%20Team%20Racing",
	             NATIVE_LAUNCH_REQUEST_ERR_UNKNOWN_KEY, "game key rejected as unknown");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=c&x=1",
	             NATIVE_LAUNCH_REQUEST_ERR_UNKNOWN_KEY, "arbitrary unknown key rejected");
}

static void TestMissingAndMalformedQuery(void)
{
	expectStatus("ctr-ap://connect?port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_MISSING_KEY, "missing host rejected");
	expectStatus("ctr-ap://connect?host=a&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_MISSING_KEY, "missing port rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_MISSING_KEY, "missing slot rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b",
	             NATIVE_LAUNCH_REQUEST_ERR_MISSING_KEY, "missing room rejected");
	expectStatus("ctr-ap://connect?host",
	             NATIVE_LAUNCH_REQUEST_ERR_QUERY, "key without '=' rejected");
	expectStatus("ctr-ap://connect?=a&host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_QUERY, "empty key rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=c&",
	             NATIVE_LAUNCH_REQUEST_ERR_QUERY, "trailing ampersand rejected");
	expectStatus("ctr-ap://connect?&host=a&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_QUERY, "leading ampersand rejected");
}

static void TestEmptyFields(void)
{
	expectStatus("ctr-ap://connect?host=&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_EMPTY, "empty host rejected");
	expectStatus("ctr-ap://connect?host=a&port=&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "empty port rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_EMPTY, "empty slot rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=",
	             NATIVE_LAUNCH_REQUEST_ERR_EMPTY, "empty room rejected");
}

static void TestPorts(void)
{
	expectStatus("ctr-ap://connect?host=a&port=0&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "zero port rejected");
	expectStatus("ctr-ap://connect?host=a&port=65536&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "just-over-max port rejected");
	expectStatus("ctr-ap://connect?host=a&port=99999&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "five-digit overflow rejected");
	expectStatus("ctr-ap://connect?host=a&port=999999&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "long overflow rejected");
	expectStatus("ctr-ap://connect?host=a&port=-1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "negative port rejected");
	expectStatus("ctr-ap://connect?host=a&port=%2B80&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "plus-signed port rejected");
	expectStatus("ctr-ap://connect?host=a&port=%2080&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "leading whitespace port rejected");
	expectStatus("ctr-ap://connect?host=a&port=80%20&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "trailing whitespace port rejected");
	expectStatus("ctr-ap://connect?host=a&port=80x&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "non-digit port rejected");
	expectStatus("ctr-ap://connect?host=a&port=80.0&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_PORT, "decimal-point port rejected");
}

static void TestHosts(void)
{
	expectStatus("ctr-ap://connect?host=0.0.0.0&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_OK, "0.0.0.0 accepted as IPv4");
	expectStatus("ctr-ap://connect?host=255.255.255.255&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_OK, "broadcast IPv4 accepted");
	expectStatus("ctr-ap://connect?host=256.0.0.1&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "IPv4 octet over 255 rejected");
	expectStatus("ctr-ap://connect?host=1.2.3&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "short IPv4 rejected");
	expectStatus("ctr-ap://connect?host=1.2.3.4.5&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "long IPv4 rejected");

	expectStatus("ctr-ap://connect?host=%5B%3A%3A1%5D&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_OK, "bracketed IPv6 loopback accepted");
	expectStatus("ctr-ap://connect?host=%5B2001%3Adb8%3A%3A1%5D&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_OK, "bracketed IPv6 accepted");
	expectStatus("ctr-ap://connect?host=::1&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "unbracketed IPv6 rejected");
	expectStatus("ctr-ap://connect?host=%5B%3A%3A1%25eth0%5D&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "IPv6 zone identifier rejected");
	expectStatus("ctr-ap://connect?host=%5B%3A%3A1&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "unterminated IPv6 bracket rejected");
	expectStatus("ctr-ap://connect?host=%5Babc%5D&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "bracket without a colon rejected");

	expectStatus("ctr-ap://connect?host=ho%20st&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "space in host rejected");
	expectStatus("ctr-ap://connect?host=host%2Fpath&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "slash in host rejected");
	expectStatus("ctr-ap://connect?host=host%5Cpath&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "backslash in host rejected");
	expectStatus("ctr-ap://connect?host=user%40host&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "userinfo in host value rejected");
	expectStatus("ctr-ap://connect?host=host%3A38281&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "embedded port colon in host rejected");
	expectStatus("ctr-ap://connect?host=.leading&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "leading empty label rejected");
	expectStatus("ctr-ap://connect?host=double..dot&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_HOST, "empty interior label rejected");
}

static void TestControlsAndUtf8(void)
{
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%00&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_CONTROL, "NUL in slot rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%1F&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_CONTROL, "C0 control in slot rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%7F&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_CONTROL, "DEL in slot rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=%0A",
	             NATIVE_LAUNCH_REQUEST_ERR_CONTROL, "LF in room rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=%0D",
	             NATIVE_LAUNCH_REQUEST_ERR_CONTROL, "CR in room rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=%09",
	             NATIVE_LAUNCH_REQUEST_ERR_CONTROL, "tab in room rejected");
	expectStatus("ctr-ap://connect?host=%00&port=1&slot=b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_CONTROL, "NUL in host rejected");

	expectStatus("ctr-ap://connect?host=a&port=1&slot=%FF&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_UTF8, "0xFF is invalid UTF-8");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%80&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_UTF8, "lone continuation byte rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%C3&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_UTF8, "truncated two-byte sequence rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%E0%80&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_UTF8, "truncated three-byte sequence rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%C0%AF&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_UTF8, "overlong two-byte slash rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%E0%80%AF&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_UTF8, "overlong three-byte rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%F0%80%80%AF&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_UTF8, "overlong four-byte rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%ED%A0%80&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_UTF8, "UTF-16 surrogate rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=%F4%90%80%80&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_UTF8, "code point above U+10FFFF rejected");
}

static void TestFieldDelimiters(void)
{
	expectStatus("ctr-ap://connect?host=a&port=1&slot=a%2Fb&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_SLOT, "slash in slot rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=a%5Cb&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_SLOT, "backslash in slot rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=a%40b&room=c",
	             NATIVE_LAUNCH_REQUEST_ERR_SLOT, "at-sign in slot rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=a%2Fb",
	             NATIVE_LAUNCH_REQUEST_ERR_ROOM, "slash in room rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=a%5Cb",
	             NATIVE_LAUNCH_REQUEST_ERR_ROOM, "backslash in room rejected");
	expectStatus("ctr-ap://connect?host=a&port=1&slot=b&room=a%40b",
	             NATIVE_LAUNCH_REQUEST_ERR_ROOM, "at-sign in room rejected");
}

static void TestLengthBoundaries(void)
{
	char host[NATIVE_LAUNCH_REQUEST_HOST_MAX + 2];
	char slot[NATIVE_LAUNCH_REQUEST_SLOT_MAX + 2];
	char room[NATIVE_LAUNCH_REQUEST_ROOM_MAX + 2];
	char uri[4096];

	memset(host, 'a', sizeof(host));
	host[NATIVE_LAUNCH_REQUEST_HOST_MAX] = '\0';
	snprintf(uri, sizeof(uri), "ctr-ap://connect?host=%s&port=1&slot=b&room=c", host);
	expectStatus(uri, NATIVE_LAUNCH_REQUEST_OK, "host at byte limit accepted");
	host[NATIVE_LAUNCH_REQUEST_HOST_MAX] = 'a';
	host[NATIVE_LAUNCH_REQUEST_HOST_MAX + 1] = '\0';
	snprintf(uri, sizeof(uri), "ctr-ap://connect?host=%s&port=1&slot=b&room=c", host);
	expectStatus(uri, NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG, "host one byte over limit rejected");

	memset(slot, 's', sizeof(slot));
	slot[NATIVE_LAUNCH_REQUEST_SLOT_MAX] = '\0';
	snprintf(uri, sizeof(uri), "ctr-ap://connect?host=a&port=1&slot=%s&room=c", slot);
	expectStatus(uri, NATIVE_LAUNCH_REQUEST_OK, "slot at byte limit accepted");
	slot[NATIVE_LAUNCH_REQUEST_SLOT_MAX] = 's';
	slot[NATIVE_LAUNCH_REQUEST_SLOT_MAX + 1] = '\0';
	snprintf(uri, sizeof(uri), "ctr-ap://connect?host=a&port=1&slot=%s&room=c", slot);
	expectStatus(uri, NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG, "slot one byte over limit rejected");

	memset(room, 'r', sizeof(room));
	room[NATIVE_LAUNCH_REQUEST_ROOM_MAX] = '\0';
	snprintf(uri, sizeof(uri), "ctr-ap://connect?host=a&port=1&slot=b&room=%s", room);
	expectStatus(uri, NATIVE_LAUNCH_REQUEST_OK, "room at byte limit accepted");
	room[NATIVE_LAUNCH_REQUEST_ROOM_MAX] = 'r';
	room[NATIVE_LAUNCH_REQUEST_ROOM_MAX + 1] = '\0';
	snprintf(uri, sizeof(uri), "ctr-ap://connect?host=a&port=1&slot=b&room=%s", room);
	expectStatus(uri, NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG, "room one byte over limit rejected");

	{
		char longUri[NATIVE_LAUNCH_REQUEST_URI_MAX + 2];
		memset(longUri, 'a', sizeof(longUri));
		longUri[NATIVE_LAUNCH_REQUEST_URI_MAX + 1] = '\0';
		expectStatus(longUri, NATIVE_LAUNCH_REQUEST_ERR_TOO_LONG, "whole URI over limit rejected");
	}
}

static void TestOutputUnchangedOnFailure(void)
{
	NativeLaunchRequest out;
	NativeLaunchRequest before;

	memset(&out, 0xAB, sizeof(out));
	memset(&before, 0xAB, sizeof(before));
	expect(NativeLaunchRequest_Parse("ctr-ap://connect?host=%zz&port=1&slot=b&room=c", &out) !=
	           NATIVE_LAUNCH_REQUEST_OK &&
	           memcmp(&out, &before, sizeof(out)) == 0,
	       "output unchanged after an escape failure");

	memset(&out, 0xAB, sizeof(out));
	expect(NativeLaunchRequest_Parse("ctr-ap://connect?host=a&port=65536&slot=b&room=c", &out) !=
	           NATIVE_LAUNCH_REQUEST_OK &&
	           memcmp(&out, &before, sizeof(out)) == 0,
	       "output unchanged after a port failure");

	memset(&out, 0xAB, sizeof(out));
	expect(NativeLaunchRequest_Parse("ctr-ap://connect?host=a&port=1&slot=b&room=c&password=x", &out) !=
	           NATIVE_LAUNCH_REQUEST_OK &&
	           memcmp(&out, &before, sizeof(out)) == 0,
	       "output unchanged after an unknown-key failure");
}

static void TestDiagnosticsAreRedacted(void)
{
	const char *uri = "ctr-ap://connect?host=secret-host.example&port=38281"
	                  "&slot=secretSlot&room=secretRoom&password=secretPassword";
	NativeLaunchRequest out;
	NativeLaunchRequestStatus status;
	int i;

	memset(&out, 0xAB, sizeof(out));
	status = NativeLaunchRequest_Parse(uri, &out);
	expect(status == NATIVE_LAUNCH_REQUEST_ERR_UNKNOWN_KEY, "password request rejected");
	expect(strstr(NativeLaunchRequest_StatusText(status), "secret") == NULL,
	       "rejection reason names no field value");
	expect(strstr(NativeLaunchRequest_StatusText(status), uri) == NULL,
	       "rejection reason is not the URI");

	for (i = 0; i <= NATIVE_LAUNCH_REQUEST_ERR_ROOM; i++)
	{
		const char *text = NativeLaunchRequest_StatusText((NativeLaunchRequestStatus)i);

		expect(text != NULL && strstr(text, "secret") == NULL && strstr(text, uri) == NULL,
		       "every status text is credential-free");
	}
}

static void TestArgumentClassification(void)
{
	expect(NativeLaunchRequest_ClassifyArg("ctr-ap://connect?host=a&port=1&slot=b&room=c") ==
	           NATIVE_LAUNCH_REQUEST_ARG_BARE,
	       "bare ctr-ap URI classified as bare");
	expect(NativeLaunchRequest_ClassifyArg("connect") == NATIVE_LAUNCH_REQUEST_ARG_CONNECT,
	       "connect keyword classified as connect");
	expect(NativeLaunchRequest_ClassifyArg("--version") == NATIVE_LAUNCH_REQUEST_ARG_NONE,
	       "--version left alone");
	expect(NativeLaunchRequest_ClassifyArg("race") == NATIVE_LAUNCH_REQUEST_ARG_NONE,
	       "unrelated argument left alone");
	expect(NativeLaunchRequest_ClassifyArg(NULL) == NATIVE_LAUNCH_REQUEST_ARG_NONE,
	       "NULL argument left alone");
	expect(NativeLaunchRequest_ClassifyArg("CTR-AP://connect") == NATIVE_LAUNCH_REQUEST_ARG_NONE,
	       "uppercase scheme not classified");
}

int main(void)
{
	TestValidRequests();
	TestSchemeAndAuthority();
	TestFragments();
	TestEscapes();
	TestDuplicateAndUnknownKeys();
	TestMissingAndMalformedQuery();
	TestEmptyFields();
	TestPorts();
	TestHosts();
	TestControlsAndUtf8();
	TestFieldDelimiters();
	TestLengthBoundaries();
	TestOutputUnchangedOnFailure();
	TestDiagnosticsAreRedacted();
	TestArgumentClassification();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
