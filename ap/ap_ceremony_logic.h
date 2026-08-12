#ifndef AP_CEREMONY_LOGIC_H
#define AP_CEREMONY_LOGIC_H

// Pure ceremony timing and geometry helpers for issue #55. Keeping these
// engine-independent lets the host harness prove the two display invariants:
// every entry receives a frame before fly-out, and a centered wrapped block is
// moved completely beyond the logical screen edge.

#define AP_CEREMONY_DEFAULT_DWELL 90

static int AP_CeremonyCycleIndex(int entryCount, int elapsedFrames,
                                 int visibleFrames)
{
	int dwell;
	int index;

	if (entryCount <= 1)
		return 0;
	if (elapsedFrames < 0)
		elapsedFrames = 0;
	if (visibleFrames < entryCount)
		visibleFrames = entryCount;

	dwell = visibleFrames / entryCount;
	if (dwell > AP_CEREMONY_DEFAULT_DWELL)
		dwell = AP_CEREMONY_DEFAULT_DWELL;
	if (dwell < 1)
		dwell = 1;

	index = elapsedFrames / dwell;
	if (index >= entryCount)
		index = entryCount - 1;
	return index;
}

static int AP_CeremonyOffscreenCenterX(int logicalWidth, int wrapWidth)
{
	// DrawMultiLine is centered on x and can occupy the full wrap width. One
	// extra pixel makes the left edge strictly greater than the screen edge.
	return logicalWidth + (wrapWidth + 1) / 2 + 1;
}

#endif
