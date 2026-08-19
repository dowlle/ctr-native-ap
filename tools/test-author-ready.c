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

	printf("%s: 5 readiness checks\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
