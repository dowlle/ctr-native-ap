#include <stdio.h>

#include "../ap/ap_author_ready.h"

static int failures;

static void expect(const char *name, int got, int want)
{
	printf("%s %s\n", got == want ? "PASS" : "FAIL", name);
	if (got != want)
		failures++;
}

int main(void)
{
	expect("missing state stands down", AP_AuthorRuntimeReady(0, 1, 1, 1), 0);
	expect("active load stands down", AP_AuthorRuntimeReady(1, 0, 1, 1), 0);
	expect("missing driver stands down", AP_AuthorRuntimeReady(1, 1, 0, 0), 0);
	expect("driver without instance stands down", AP_AuthorRuntimeReady(1, 1, 1, 0), 0);
	expect("idle level with born driver runs", AP_AuthorRuntimeReady(1, 1, 1, 1), 1);

	expect("boss race stands down (#194)", AP_AuthorRaceAllowsAuthoring(1), 0);
	expect("ordinary race stays authorable", AP_AuthorRaceAllowsAuthoring(0), 1);

	printf("%s: 7 readiness checks\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
