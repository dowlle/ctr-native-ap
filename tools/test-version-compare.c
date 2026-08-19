// Out-of-engine assertions for the pair-version comparator behind the update
// notice (issue #150). Compiles the REAL function -- ap/ap_version_cmp.h is
// freestanding by design, so this harness links nothing from the game.
//
//   cc -Wall -Wextra -o /tmp/test-version-compare tools/test-version-compare.c && /tmp/test-version-compare
//
// Exit 0 = every assertion held; the failing case is printed otherwise.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_version_cmp.h"
#include "../ap/ap_version.h"

static int g_failures = 0;

static void expect(const char *seed, const char *client, int want, const char *why)
{
	int got = AP_VersionNewer(seed, client);
	printf("%-4s AP_VersionNewer(\"%s\", \"%s\") = %d  [%s]\n",
	       got == want ? "ok" : "FAIL", seed, client, got, why);
	if (got != want)
		g_failures++;
}

int main(void)
{
	expect("0.2.0", CTR_AP_COMPAT_VERSION, 0, "compiled Alpha 2 compatibility identity matches");
	// Ordering: the cases the release trains actually produce.
	expect("0.1.5",   "0.1.4",   1, "patch bump is newer");
	expect("0.1.4.1", "0.1.4",   1, "hotfix component beats a missing one (D=0)");
	expect("0.1.5",   "0.1.4.1", 1, "patch bump beats a hotfix on the older patch");
	expect("0.2.0",   "0.1.9",   1, "minor bump is newer");
	expect("0.2.0",   "v0.2.0",  0, "matched 0.2.0 pair is silent");
	expect("0.2.0",   "v0.1.5",  1, "0.2.0 seed warns the previous stable client");
	expect("1.0.0",   "0.9.9",   1, "major bump is newer");

	// Equal and older: silent.
	expect("0.1.4",   "0.1.4",   0, "equal is silent");
	expect("0.1.4.0", "0.1.4",   0, "explicit .0 equals the missing component");
	expect("0.1.4",   "0.1.5",   0, "older seed is silent");
	expect("0.1.4",   "0.1.4.1", 0, "seed older than a client hotfix is silent");

	// The "v" prefix is stripped on either side, in any combination.
	expect("v0.1.5",  "v0.1.4",  1, "v-prefix on both sides");
	expect("0.1.5",   "v0.1.4",  1, "v-prefix on the client only (the real shape)");
	expect("v0.1.4",  "0.1.4",   0, "v-prefix does not change equality");

	// Unparseable on either side -> cannot tell -> silent.
	expect("",         "v0.1.4",  0, "empty seed version (absent key) is silent");
	expect("v0.1.4",   "",        0, "empty client version is silent");
	expect("garbage",  "v0.1.4",  0, "junk seed version is silent");
	expect("0.1",      "v0.1.4",  0, "two-component version is unparseable");
	expect("0.1.5.1.2","v0.1.4",  0, "five-component version is unparseable");
	expect("0.1.5-rc1","v0.1.4",  0, "prerelease tail is unparseable");
	expect("0.1.5 ",   "v0.1.4",  0, "trailing whitespace is unparseable");
	expect("v",        "v0.1.4",  0, "bare v is unparseable");
	expect("..",       "v0.1.4",  0, "empty components are unparseable");
	expect("0.1.x",    "v0.1.4",  0, "non-numeric component is unparseable");
	expect("999999999999.0.0", "v0.1.4", 1, "absurd component saturates, does not wrap");

	// AP_VersionDigits: what the notice strings substitute.
	if (strcmp(AP_VersionDigits("v0.1.4.1"), "0.1.4.1") != 0 ||
	    strcmp(AP_VersionDigits("0.1.5"), "0.1.5") != 0)
	{
		printf("FAIL AP_VersionDigits did not strip exactly the leading v\n");
		g_failures++;
	}
	else
	{
		printf("ok   AP_VersionDigits strips a leading v and nothing else\n");
	}

	printf("\n%s\n", g_failures ? "FAILURES PRESENT" : "all assertions passed");
	return g_failures != 0;
}
