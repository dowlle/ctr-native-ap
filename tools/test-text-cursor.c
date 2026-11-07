// cc -std=c99 -Wall -Wextra -o /tmp/ttextcursor tools/test-text-cursor.c && /tmp/ttextcursor
// Host harness for the pure NativeText cursor editing rules
// (include/platform/native_text_edit_logic.h, 0.3.0-alpha3 tester wish:
// scroll through the track rename name instead of deleting to the start).
// Same shape as tools/test-trap-scheduler.c: no engine, no build system, one
// translation unit, compile and run.
//
// Cases: cursor movement at the start, middle and end, insert and delete at
// the start, middle and end, the field length cap, UTF-8 codepoints in a
// starting value (moved over and deleted whole, broken sequences never trap
// the cursor), and the commit flow the rename screen runs: edit with a moved
// cursor, then the saved name is the edited text.

#include <stdio.h>
#include <string.h>

#include "../include/platform/native_text_edit_logic.h"

static int failures;
static int checks;

static void expect(const char *name, int got, int want)
{
	checks++;
	if (got != want)
	{
		printf("FAIL %-56s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

static void expect_str(const char *name, const char *got, const char *want)
{
	checks++;
	if (strcmp(got, want) != 0)
	{
		printf("FAIL %-56s got=\"%s\" want=\"%s\"\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

// A field in the state the platform layer keeps it: buffer, capacity, length,
// cursor. Begin measures the starting text and opens the cursor at the end,
// exactly what NativeText_Begin does.
struct Field
{
	char buf[16];
	int cap;
	int len;
	int cursor;
};

static void field_begin(struct Field *f, const char *text)
{
	snprintf(f->buf, sizeof f->buf, "%s", text);
	f->cap = (int)sizeof f->buf;
	f->len = (int)strlen(f->buf);
	f->cursor = f->len;
}

int main(void)
{
	struct Field f;

	// Movement: right at the end stays, left walks to the start and stops.
	field_begin(&f, "ABCDE");
	expect("right at end stays", NativeTextEdit_CursorRight(f.buf, f.len, f.cursor), 5);
	expect("left from end", NativeTextEdit_CursorLeft(f.buf, 5), 4);
	expect("left in middle", NativeTextEdit_CursorLeft(f.buf, 2), 1);
	expect("left at start stays", NativeTextEdit_CursorLeft(f.buf, 0), 0);
	expect("right in middle", NativeTextEdit_CursorRight(f.buf, f.len, 2), 3);
	expect("right from start", NativeTextEdit_CursorRight(f.buf, f.len, 0), 1);

	// Insert at the end: the old append-only behaviour.
	field_begin(&f, "AB");
	f.len = NativeTextEdit_Insert(f.buf, f.cap, f.len, &f.cursor, 'C');
	expect_str("insert at end text", f.buf, "ABC");
	expect("insert at end len", f.len, 3);
	expect("insert at end cursor", f.cursor, 3);

	// Insert at the start and in the middle.
	field_begin(&f, "BC");
	f.cursor = 0;
	f.len = NativeTextEdit_Insert(f.buf, f.cap, f.len, &f.cursor, 'A');
	expect_str("insert at start text", f.buf, "ABC");
	expect("insert at start cursor", f.cursor, 1);
	f.cursor = 2;
	f.len = NativeTextEdit_Insert(f.buf, f.cap, f.len, &f.cursor, 'x');
	expect_str("insert in middle text", f.buf, "ABxC");
	expect("insert in middle cursor", f.cursor, 3);

	// Backspace at the end: the old behaviour; at start and middle with cursor.
	field_begin(&f, "ABC");
	f.len = NativeTextEdit_Backspace(f.buf, f.len, &f.cursor);
	expect_str("backspace at end text", f.buf, "AB");
	expect("backspace at end cursor", f.cursor, 2);
	field_begin(&f, "ABC");
	f.cursor = 0;
	f.len = NativeTextEdit_Backspace(f.buf, f.len, &f.cursor);
	expect_str("backspace at start text", f.buf, "ABC");
	expect("backspace at start cursor", f.cursor, 0);
	field_begin(&f, "ABC");
	f.cursor = 2;
	f.len = NativeTextEdit_Backspace(f.buf, f.len, &f.cursor);
	expect_str("backspace in middle text", f.buf, "AC");
	expect("backspace in middle cursor", f.cursor, 1);

	// Delete under the cursor: end, start, middle.
	field_begin(&f, "ABC");
	f.len = NativeTextEdit_Delete(f.buf, f.len, f.cursor);
	expect_str("delete at end text", f.buf, "ABC");
	field_begin(&f, "ABC");
	f.len = NativeTextEdit_Delete(f.buf, f.len, 0);
	expect_str("delete at start text", f.buf, "BC");
	field_begin(&f, "ABC");
	f.len = NativeTextEdit_Delete(f.buf, f.len, 1);
	expect_str("delete in middle text", f.buf, "AC");

	// Length cap: the field is full at cap - 1, inserts are refused.
	field_begin(&f, "123456789012345");
	expect("field starts full", f.len, 15);
	f.len = NativeTextEdit_Insert(f.buf, f.cap, f.len, &f.cursor, 'X');
	expect("full field refuses insert", f.len, 15);
	expect_str("full field text unchanged", f.buf, "123456789012345");
	f.cursor = 0;
	f.len = NativeTextEdit_Insert(f.buf, f.cap, f.len, &f.cursor, 'X');
	expect("full field refuses insert at start", f.len, 15);
	// One short of full takes exactly one more, wherever the cursor is.
	field_begin(&f, "12345678901234");
	f.cursor = 7;
	f.len = NativeTextEdit_Insert(f.buf, f.cap, f.len, &f.cursor, 'X');
	expect("one short of full inserts", f.len, 15);
	expect_str("one short of full text", f.buf, "1234567X8901234");
	f.len = NativeTextEdit_Insert(f.buf, f.cap, f.len, &f.cursor, 'Y');
	expect("now full refuses", f.len, 15);

	// UTF-8 in a starting value (a track title from a pack descriptor):
	// "AéB" is A, 0xC3 0xA9, B. Movement steps over the pair as one.
	field_begin(&f, "A\xC3\xA9" "B");
	expect("utf8 length is bytes", f.len, 4);
	expect("utf8 left from end", NativeTextEdit_CursorLeft(f.buf, 4), 3);
	expect("utf8 left over the pair", NativeTextEdit_CursorLeft(f.buf, 3), 1);
	expect("utf8 right over the pair", NativeTextEdit_CursorRight(f.buf, f.len, 1), 3);
	// Backspace removes the whole codepoint, not one byte of it.
	f.cursor = 3;
	f.len = NativeTextEdit_Backspace(f.buf, f.len, &f.cursor);
	expect_str("utf8 backspace removes the pair", f.buf, "AB");
	expect("utf8 backspace cursor", f.cursor, 1);
	// A broken sequence counts as one byte and never traps the cursor.
	field_begin(&f, "A\xC3" "B");
	expect("broken seq left lands on it", NativeTextEdit_CursorLeft(f.buf, 2), 1);
	expect("broken seq left past it", NativeTextEdit_CursorLeft(f.buf, 1), 0);
	f.cursor = 2;
	f.len = NativeTextEdit_Backspace(f.buf, f.len, &f.cursor);
	expect_str("broken seq backspace one byte", f.buf, "AB");

	// The rename screen's commit flow: a track title edited with a moved
	// cursor is the text the menu hands to SetItemTitle on confirm.
	field_begin(&f, "Sunse Raceway");
	f.cursor = 5; // walked left from the end into the gap after "Sunse"
	f.len = NativeTextEdit_Insert(f.buf, f.cap, f.len, &f.cursor, 't');
	expect_str("rename flow fixes the typo", f.buf, "Sunset Raceway");
	// Leading spaces are trimmed by the menu on commit, not by the editor.
	field_begin(&f, "  Track");
	expect_str("editor keeps leading spaces", f.buf, "  Track");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
