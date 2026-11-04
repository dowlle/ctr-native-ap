// Hint filters (A2-23): the pure decisions in ap/ap_hint_filters_logic.h.
// The wiring is pinned by test-hint-filters-production.py.
#include <cstdio>
#include "../ap/ap_hint_filters_logic.h"

static int checks, failures;
static void expect(const char *name, long long got, long long want)
{
	checks++;
	if (got != want) { std::printf("FAIL %s: got %lld, want %lld\n", name, got, want); failures++; }
}

int main()
{
	// Pad hints wait only when asked AND the seed uses Spoiler Items AND the preview is missing.
	for (int wait = 0; wait <= 1; ++wait)
		for (int spoiler = 0; spoiler <= 1; ++spoiler)
			for (int shown = 0; shown <= 1; ++shown)
			{
				char name[48];
				std::snprintf(name, sizeof name, "waits w%d s%d p%d", wait, spoiler, shown);
				expect(name, AP_HintFilter_PadWaits(wait, spoiler, shown), wait && spoiler && !shown);
			}
	// Option off: every location keeps, known or not, whatever its flags.
	for (unsigned flags = 0; flags < 8; ++flags)
		for (int known = 0; known <= 1; ++known)
			expect("off keeps", AP_HintFilter_Progression(0, known, flags), AP_HINT_KEEP);
	// Option on: progression keeps, useful (2), filler (0) and trap (4) drop, unknown waits.
	expect("progression", AP_HintFilter_Progression(1, 1, 1), AP_HINT_KEEP);
	expect("progression+useful", AP_HintFilter_Progression(1, 1, 3), AP_HINT_KEEP);
	expect("useful", AP_HintFilter_Progression(1, 1, 2), AP_HINT_DROP);
	expect("filler", AP_HintFilter_Progression(1, 1, 0), AP_HINT_DROP);
	expect("trap", AP_HintFilter_Progression(1, 1, 4), AP_HINT_DROP);
	expect("unknown waits", AP_HintFilter_Progression(1, 0, 0), AP_HINT_LATER);
	expect("unknown waits even with flags", AP_HintFilter_Progression(1, 0, 1), AP_HINT_LATER);
	std::printf("hint filters: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
