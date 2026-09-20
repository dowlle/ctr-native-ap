/*
 * Host harness for the pure boss/pad reward sentence builder (issue #330).
 *
 *   cc -Wall -Wextra -DCTR_AP -I ap -I . -I include -o /tmp/test-reward-text \
 *      tools/test-reward-text.c && /tmp/test-reward-text
 *
 * Asserts the wording, the single display bound (AP_REWARD_TEXT_MAX), exact
 * fit vs per-component truncation, uppercase normalization, whitespace
 * collapse/trim, UTF-8 and control-byte rejection, the "Unknown" placeholder,
 * unsupported glyphs that must not pass through, and the production fallback
 * wiring that keeps the retail string when no scout is known.
 *
 * Issue #330 correction: the foreign line must always keep a recognizable
 * recipient AND item joined by the possessive separator. A long recipient may
 * no longer cut the item away, so the regression below (100-character player,
 * "HOOKSHOT") is the difference between the rejected sentence-level cut and
 * the component-wise formatter.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ap/ap_reward_text.h"

static int g_failures = 0;

static void check(int condition, const char *message)
{
	if (!condition)
	{
		fprintf(stderr, "FAIL: %s\n", message);
		g_failures++;
	}
}

static void expect_build(const char *name, const char *item,
                         const char *player, int own, const char *want)
{
	char out[64];
	int ok = AP_RewardTextBuild(out, (int)sizeof out, item, player, own);
	if (!ok)
	{
		fprintf(stderr, "FAIL %s: expected \"%s\", got no sentence\n", name, want);
		g_failures++;
		return;
	}
	if (strcmp(out, want) != 0)
	{
		fprintf(stderr, "FAIL %s: expected \"%s\", got \"%s\"\n", name, want, out);
		g_failures++;
		return;
	}
	if (strlen(out) > AP_REWARD_TEXT_MAX)
	{
		fprintf(stderr, "FAIL %s: \"%s\" is %d chars, exceeds the %d-char bound\n",
		        name, out, (int)strlen(out), AP_REWARD_TEXT_MAX);
		g_failures++;
	}
}

static void expect_fallback(const char *name, const char *item,
                            const char *player, int own)
{
	char out[64];
	out[0] = '\0';
	int ok = AP_RewardTextBuild(out, (int)sizeof out, item, player, own);
	if (ok)
	{
		fprintf(stderr, "FAIL %s: expected retail fallback, got \"%s\"\n", name, out);
		g_failures++;
	}
}

static int file_contains(const char *path, const char *needle)
{
	static char buf[1u << 20];
	FILE *f = fopen(path, "rb");
	size_t n;

	if (f == NULL)
		return -1; // cannot inspect (run from elsewhere); caller reports
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return strstr(buf, needle) != NULL;
}

// The production accessor is the only place the "no scout / disconnected"
// fallback can live; verify its guard clauses and the Roo-only hook are intact
// so the pure tests above are known to be the fallback path's source.
static void test_production_wiring(void)
{
	static const struct { const char *path, *needle; } guards[] = {
		{"ap/ap_hooks.c", "if (!retail || !ctr_cfg_active())"},
		{"ap/ap_hooks.c", "!ap_net_scout_known(code, &itemId, &player, NULL)"},
		{"ap/ap_hooks.c", "!ap_net_scout_text(code, itemRaw, (int)sizeof itemRaw,"},
		{"ap/ap_hooks.c", "if (own && itemId == AP_ITEM_BASE + AP_IDX_KEY)"},
		{"ap/ap_hooks.c", "AP_RewardTextBuild(line, (int)sizeof line, alias, playerRaw, own)"},
		{"ap/ap_hooks.h", "char *AP_RewardSubtitleForBit(int rewardBit, char *retail);"},
		{"game/233/CS_Thread.c", "cs->Subtitles.lngIndex == LNG_HAVE_A_KEY"},
		{"game/233/CS_Thread.c", "AP_RewardSubtitleForBit("},
		{"game/233/CS_Thread.c", "ADV_REWARD_FIRST_BOSS_KEY, subtitleText"},
	};
	size_t i;
	int inspected = 0;

	for (i = 0; i < sizeof guards / sizeof guards[0]; i++)
	{
		int r = file_contains(guards[i].path, guards[i].needle);
		if (r < 0)
			continue; // cwd is not the repo root; skip rather than false-fail
		inspected = 1;
		check(r == 1, guards[i].path);
		if (r != 1)
			fprintf(stderr, "  missing in %s: %s\n", guards[i].path, guards[i].needle);
	}
	if (!inspected)
		fprintf(stderr, "note: source wiring guard skipped (repo root not the cwd)\n");
}

// Build "HAVE <n copies of c>..." style expectations without hand-counting.
static void repeat_char(char *dst, char c, int n)
{
	memset(dst, c, (size_t)n);
	dst[n] = '\0';
}

int main(void)
{
	char out[64];
	char longItem[101];
	char longPlayer[101];
	char exact27[28];
	char exact21[22];
	char long28[29];
	char want[128];
	int i;

	// ── own item ────────────────────────────────────────────────────────────
	expect_build("own item", "Progressive Boost", "Local", 1,
	             "HAVE A PROGRESSIVE BOOST.");
	expect_build("own uppercase", "moon pearl", "Local", 1,
	             "HAVE A MOON PEARL.");
	expect_build("own apostrophe", "Papu's Pyramid", "Local", 1,
	             "HAVE A PAPU'S PYRAMID.");
	expect_build("own whitespace trim/collapse", "  MOON   PEARL  ", "Local", 1,
	             "HAVE A MOON PEARL.");

	// ── own item exact fit is untouched; one over cuts the item only ─────────
	// Own: "HAVE A " (7) + item + "." (1) == 35, so item is exactly 27.
	for (i = 0; i < 26; i++)
		exact27[i] = (char)('A' + i); // A..Z
	exact27[26] = '1';
	exact27[27] = '\0';
	expect_build("own exact fit", exact27, "Local", 1,
	             "HAVE A ABCDEFGHIJKLMNOPQRSTUVWXYZ1.");
	check(AP_RewardTextBuild(out, (int)sizeof out, exact27, "Local", 1) &&
	      strlen(out) == AP_REWARD_TEXT_MAX &&
	      strcmp(out + (AP_REWARD_TEXT_MAX - 3), "Z1.") == 0,
	      "own exact fit keeps its single full stop");

	memset(long28, 'x', 28);
	long28[28] = '\0';
	check(AP_RewardTextBuild(out, (int)sizeof out, long28, "Local", 1) &&
	      strlen(out) == AP_REWARD_TEXT_MAX &&
	      strcmp(out + (AP_REWARD_TEXT_MAX - 3), "...") == 0,
	      "own one-over truncates with an explicit ellipsis at the bound");

	// ── short foreign item keeps the plain retail-shaped wording ────────────
	expect_build("foreign item", "Moon Pearl", "Alice", 0,
	             "HAVE ALICE'S MOON PEARL.");
	expect_build("foreign uppercase", "dragon mines", "bob", 0,
	             "HAVE BOB'S DRAGON MINES.");

	// Foreign: "HAVE " (5) + player + "'S " (3) + item + "." (1) == 35, so
	// player + item == 26.
	for (i = 0; i < 21; i++)
		exact21[i] = (char)('A' + (i % 26));
	exact21[21] = '\0';
	expect_build("foreign exact fit", exact21, "ALICE", 0,
	             "HAVE ALICE'S ABCDEFGHIJKLMNOPQRSTU.");

	// ── regression: a 100-character recipient must not remove the item ──────
	// This is the rejected 2522bc0f behavior: the assembled sentence was cut
	// from the left, yielding only "HAVE <PLAYER>..." and dropping the reward.
	memset(longPlayer, 'p', sizeof longPlayer - 1);
	longPlayer[sizeof longPlayer - 1] = '\0';

	check(AP_RewardTextBuild(out, (int)sizeof out, "HOOKSHOT", longPlayer, 0),
	      "long recipient with a short item builds");
	check(strstr(out, "HOOKSHOT") != NULL,
	      "long recipient keeps the complete short item");
	check(strstr(out, "'S ") != NULL,
	      "long recipient keeps the possessive separator");
	{
		char wantHook[128];
		char p15[16];
		repeat_char(p15, 'P', 15);
		snprintf(wantHook, sizeof wantHook, "HAVE %s...'S HOOKSHOT.", p15);
		check(strcmp(out, wantHook) == 0,
		      "long recipient is shortened to a recognizable prefix");
	}

	// ── long item / short recipient: keep the recipient, cut the item ───────
	memset(longItem, 'x', sizeof longItem - 1);
	longItem[sizeof longItem - 1] = '\0';
	check(AP_RewardTextBuild(out, (int)sizeof out, longItem, "ALICE", 0),
	      "short recipient with a long item builds");
	{
		char x19[20];
		repeat_char(x19, 'X', 19);
		snprintf(want, sizeof want, "HAVE ALICE'S %s...", x19);
		check(strcmp(out, want) == 0,
		      "long item keeps the short recipient and a marked prefix");
	}

	// ── both long: each side keeps an identifiable prefix and an ellipsis ────
	check(AP_RewardTextBuild(out, (int)sizeof out, longItem, longPlayer, 0),
	      "two long names build");
	{
		char p10[11];
		char x11[12];
		repeat_char(p10, 'P', 10);
		repeat_char(x11, 'X', 11);
		snprintf(want, sizeof want, "HAVE %s...'S %s...", p10, x11);
		check(strcmp(out, want) == 0,
		      "two long names split the bound and keep both prefixes");
	}
	check(strlen(out) <= AP_REWARD_TEXT_MAX,
	      "both-long line stays inside the display bound");
	check(strncmp(out, "HAVE ", 5) == 0,
	      "both-long line never collapses to a bare player prefix");

	// ── own 100-character item stays inside the single display bound ────────
	check(AP_RewardTextBuild(out, (int)sizeof out, longItem, longPlayer, 1),
	      "100-character own item builds");
	check(strlen(out) == AP_REWARD_TEXT_MAX, "own long line hits the bound");
	check(out[AP_REWARD_TEXT_MAX - 1] == '.' &&
	      out[AP_REWARD_TEXT_MAX - 2] == '.' &&
	      out[AP_REWARD_TEXT_MAX - 3] == '.',
	      "own long line ends in a readable three-period cut");

	// ── fallbacks: empty, Unknown, unsupported, control-only ─────────────────
	expect_fallback("empty item", "", "Local", 1);
	expect_fallback("null item", NULL, "Local", 1);
	expect_fallback("Unknown item", "Unknown", "Local", 1);
	expect_fallback("unknown lowercase item", "unknown", "Local", 1);
	expect_fallback("Unknown foreign player", "Moon Pearl", "Unknown", 0);
	expect_fallback("empty foreign player", "Moon Pearl", "", 0);
	expect_fallback("UTF-8 only item", "\xc3\xa9\xc3\xa9", "Local", 1);
	expect_fallback("control-only item", "\x01\x02\x03", "Local", 1);
	expect_fallback("UTF-8 only foreign player", "Moon Pearl", "\xe2\x98\x83", 0);

	// ── unsupported bytes/control chars become collapsed spaces, never gaps ──
	expect_build("UTF-8 byte folds to a space", "A\xc3\xa9" "B", "Local", 1,
	             "HAVE A A B.");
	expect_build("control bytes fold to spaces", "A\x01\x02\x03" "B", "Local", 1,
	             "HAVE A A B.");
	expect_build("reserved button glyph folds to a space", "A@B", "Local", 1,
	             "HAVE A A B.");
	expect_build("colon and comparison glyphs survive", "A:B=C", "Local", 1,
	             "HAVE A A:B=C.");

	// ── storage-bound guard ────────────────────────────────────────────────
	// The production buffer is AP_REWARD_TEXT_MAX + 1; a smaller buffer must be
	// refused rather than overflowed.
	check(!AP_RewardTextBuild(out, AP_REWARD_TEXT_MAX, "Moon Pearl", "Local", 1),
	      "undersized output buffer is refused");
	check(!AP_RewardTextName("Moon Pearl", out, 0), "zero-capacity name refused");

	test_production_wiring();

	if (g_failures)
	{
		printf("reward text: FAIL (%d)\n", g_failures);
		return 1;
	}
	puts("reward text: PASS");
	return 0;
}
