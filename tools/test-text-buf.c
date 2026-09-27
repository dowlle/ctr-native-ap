// Out-of-engine assertions for the in-memory ap-state.json builder and its
// skip-if-unchanged decision (ap/ap_text_buf_logic.h). No engine, no SDL.
//
//   cc -Wall -Wextra -I ap -o /tmp/test-text-buf tools/test-text-buf.c && /tmp/test-text-buf
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * printf/puts build exactly the text a FILE* would have received, across
//     many small appends and one append larger than the initial allocation
//   * growth keeps earlier text intact and stops at the cap with `failed` set,
//     and a failed text is never written
//   * reset keeps the allocation and starts an empty text
//   * the write decision: first dump writes, an identical dump is skipped, a
//     changed dump writes, an identical dump after a FAILED write is retried
//   * copy makes an equal, independent snapshot

#include <stdio.h>
#include <string.h>

#include "ap_text_buf_logic.h"

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

// A dump-shaped text: the same mix of fprintf/fputs calls AP_DumpState makes.
static void build_dump(AP_TextBuf *b, int pads, int position)
{
	int i;
	AP_TextBufReset(b);
	AP_TextBufPuts(b, "{\n");
	AP_TextBufPrintf(b, "  \"schema_active\": %d,\n", 1);
	AP_TextBufPrintf(b, "  \"live_position\": %d,\n", position);
	AP_TextBufPuts(b, "  \"pads\": [\n");
	for (i = 0; i < pads; i++)
		AP_TextBufPrintf(b,
		                 "    {\"pad\": %d, \"dest\": %d, \"req_type\": \"%s\", "
		                 "\"count\": %d, \"codes\": [%ld,%ld]}%s\n",
		                 i, i + 3, "trophy", i * 2, 35015156L + i, -1L,
		                 (i + 1 < pads) ? "," : "");
	AP_TextBufPuts(b, "  ]\n");
	AP_TextBufPuts(b, "}\n");
}

// The same text produced through stdio, for a byte-for-byte comparison.
static size_t build_dump_stdio(char *out, size_t cap, int pads, int position)
{
	FILE *f = tmpfile();
	size_t n;
	int i;
	if (f == NULL)
		return 0;
	fputs("{\n", f);
	fprintf(f, "  \"schema_active\": %d,\n", 1);
	fprintf(f, "  \"live_position\": %d,\n", position);
	fputs("  \"pads\": [\n", f);
	for (i = 0; i < pads; i++)
		fprintf(f,
		        "    {\"pad\": %d, \"dest\": %d, \"req_type\": \"%s\", "
		        "\"count\": %d, \"codes\": [%ld,%ld]}%s\n",
		        i, i + 3, "trophy", i * 2, 35015156L + i, -1L,
		        (i + 1 < pads) ? "," : "");
	fputs("  ]\n", f);
	fputs("}\n", f);
	rewind(f);
	n = fread(out, 1, cap, f);
	fclose(f);
	return n;
}

static void test_matches_stdio(void)
{
	static char expected[256 * 1024];
	AP_TextBuf b = {0};
	size_t n;

	// 28 pads: the real dump size class (about 8 KB), inside the first allocation.
	build_dump(&b, 28, 4);
	n = build_dump_stdio(expected, sizeof expected, 28, 4);
	expect(!b.failed && b.len == n && memcmp(b.data, expected, n) == 0,
	       "small dump is byte-identical to the stdio output");
	expect(b.data[b.len] == '\0', "text stays NUL-terminated");

	// 2000 pads: well past the initial 16 KB, forces several doublings mid-text.
	build_dump(&b, 2000, 7);
	n = build_dump_stdio(expected, sizeof expected, 2000, 7);
	expect(!b.failed && b.len == n && memcmp(b.data, expected, n) == 0,
	       "large dump is byte-identical after growth");
	expect(b.cap > AP_TEXT_BUF_INITIAL && b.cap <= AP_TEXT_BUF_MAX, "grew within the cap");

	// Reset keeps the allocation.
	{
		size_t cap = b.cap;
		AP_TextBufReset(&b);
		expect(b.len == 0 && b.cap == cap && b.data[0] == '\0', "reset keeps the allocation");
	}
	AP_TextBufFree(&b);
}

static void test_single_large_append(void)
{
	static char big[40000];
	AP_TextBuf b = {0};

	memset(big, 'x', sizeof big - 1);
	big[sizeof big - 1] = '\0';
	AP_TextBufPuts(&b, "head:");
	AP_TextBufPrintf(&b, "%s", big);
	AP_TextBufPuts(&b, ":tail");
	expect(!b.failed && b.len == 5 + (sizeof big - 1) + 5, "one append bigger than the buffer");
	expect(memcmp(b.data, "head:", 5) == 0 && memcmp(b.data + b.len - 5, ":tail", 5) == 0,
	       "text around the big append intact");
	AP_TextBufFree(&b);
}

static void test_cap(void)
{
	static char chunk[64 * 1024];
	AP_TextBuf b = {0};
	AP_TextBuf last = {0};
	int i;

	memset(chunk, 'y', sizeof chunk - 1);
	chunk[sizeof chunk - 1] = '\0';
	for (i = 0; i < 40; i++) // 40 x 64 KB > 1 MB cap
		AP_TextBufPrintf(&b, "%s", chunk);
	expect(b.failed, "passing the cap marks the text failed");
	expect(b.len < AP_TEXT_BUF_MAX, "length never passes the cap");
	expect(!AP_TextBufShouldWrite(&b, &last, 1), "a failed text is never written");
	AP_TextBufPuts(&b, "more");
	expect(b.failed, "failed stays failed until reset");
	AP_TextBufReset(&b);
	AP_TextBufPuts(&b, "ok");
	expect(!b.failed && b.len == 2, "reset clears the failure");
	AP_TextBufFree(&b);
	AP_TextBufFree(&last);
}

static void test_write_decision(void)
{
	AP_TextBuf now = {0};
	AP_TextBuf last = {0};

	build_dump(&now, 28, 4);
	expect(AP_TextBufShouldWrite(&now, &last, 1), "first dump is written");
	expect(AP_TextBufCopy(&last, &now) && AP_TextBufEquals(&last, &now), "snapshot equals");

	build_dump(&now, 28, 4);
	expect(!AP_TextBufShouldWrite(&now, &last, 1), "identical dump is skipped");

	build_dump(&now, 28, 5);
	expect(AP_TextBufShouldWrite(&now, &last, 1), "changed dump is written");
	AP_TextBufCopy(&last, &now);

	build_dump(&now, 28, 5);
	expect(AP_TextBufShouldWrite(&now, &last, 0),
	       "identical dump is retried when the last write failed");

	// The snapshot is independent of the builder.
	build_dump(&now, 28, 6);
	expect(!AP_TextBufEquals(&last, &now), "rebuilding does not change the snapshot");

	// Same length, different byte.
	build_dump(&now, 28, 5);
	now.data[now.len / 2] ^= 1;
	expect(!AP_TextBufEquals(&last, &now), "a one-byte change is seen");

	AP_TextBufFree(&now);
	AP_TextBufFree(&last);
}

int main(void)
{
	test_matches_stdio();
	test_single_large_append();
	test_cap();
	test_write_decision();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
