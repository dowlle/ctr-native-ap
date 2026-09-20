// Out-of-engine assertions for the "Mute When Unfocused" option (issue 348:
// running several Archipelago clients on one PC, only the game on screen
// should make noise). Exercises the freestanding option + focus -> output-gain
// decision in include/platform/native_focus_mute.h directly -- no SDL, no
// engine.
//
//   cc -Wall -Wextra -I include -o /tmp/test-focus-mute tools/test-focus-mute.c && /tmp/test-focus-mute
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * the full gain/mute matrix: the option only ever mutes while it is on AND
//     the window has lost focus, so the default (option off) never touches a
//     sample whatever the focus state does
//   * the "focus not read from SDL yet" tri-state counting as focused, so a
//     start-up frame cannot silence the game by accident
//   * when the caller has to push the gain into SDL: only on a change, or when
//     the audio device was reopened and the new stream is back at its default

#include <stdio.h>

#include "platform/native_focus_mute.h"

#define FOCUS_UNKNOWN (-1)
#define UNFOCUSED     0
#define FOCUSED       1

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

static void TestOptionOffNeverMutes(void)
{
	expect(NativeFocusMute_GainPure(0, FOCUSED) == NATIVE_FOCUS_MUTE_GAIN_FULL, "option off, focused: full gain");
	expect(NativeFocusMute_GainPure(0, UNFOCUSED) == NATIVE_FOCUS_MUTE_GAIN_FULL, "option off, unfocused: full gain, the default is untouched");
	expect(NativeFocusMute_GainPure(0, FOCUS_UNKNOWN) == NATIVE_FOCUS_MUTE_GAIN_FULL, "option off, focus unknown: full gain");
}

static void TestOptionOnMutesOnlyWhenUnfocused(void)
{
	expect(NativeFocusMute_GainPure(1, FOCUSED) == NATIVE_FOCUS_MUTE_GAIN_FULL, "option on, focused: full gain");
	expect(NativeFocusMute_GainPure(1, UNFOCUSED) == NATIVE_FOCUS_MUTE_GAIN_MUTED, "option on, unfocused: muted");
	expect(NativeFocusMute_GainPure(1, FOCUS_UNKNOWN) == NATIVE_FOCUS_MUTE_GAIN_FULL,
	       "option on, focus not read from SDL yet: full gain, never silence by accident");
}

static void TestToggledWhileUnfocused(void)
{
	// The option is read every frame, so turning it off while the window is
	// still unfocused restores full gain without waiting for a focus event,
	// and turning it on while unfocused mutes on the very next frame.
	expect(NativeFocusMute_GainPure(1, UNFOCUSED) == NATIVE_FOCUS_MUTE_GAIN_MUTED, "turned on while unfocused: muted on the next frame");
	expect(NativeFocusMute_GainPure(0, UNFOCUSED) == NATIVE_FOCUS_MUTE_GAIN_FULL, "turned off while still unfocused: full gain without a focus event");
}

static void TestWindowStartsUnfocused(void)
{
	// The caller seeds the focus state from the window flags on the first
	// frame, so a client launched behind another window mutes from the start
	// instead of after the first alt-tab.
	float seeded = NativeFocusMute_GainPure(1, UNFOCUSED);
	expect(seeded == NATIVE_FOCUS_MUTE_GAIN_MUTED, "window that starts unfocused is muted from the seeded state");
}

static void TestNeedsApply(void)
{
	const int SAME_STREAM = 1;
	const int REOPENED = 0;

	expect(NativeFocusMute_NeedsApplyPure(NATIVE_FOCUS_MUTE_GAIN_FULL, NATIVE_FOCUS_MUTE_GAIN_FULL, SAME_STREAM) == 0,
	       "unchanged gain on the same stream: no SDL call");
	expect(NativeFocusMute_NeedsApplyPure(NATIVE_FOCUS_MUTE_GAIN_MUTED, NATIVE_FOCUS_MUTE_GAIN_MUTED, SAME_STREAM) == 0,
	       "still muted on the same stream: no SDL call");
	expect(NativeFocusMute_NeedsApplyPure(NATIVE_FOCUS_MUTE_GAIN_MUTED, NATIVE_FOCUS_MUTE_GAIN_FULL, SAME_STREAM) == 1, "focus lost: apply the mute once");
	expect(NativeFocusMute_NeedsApplyPure(NATIVE_FOCUS_MUTE_GAIN_FULL, NATIVE_FOCUS_MUTE_GAIN_MUTED, SAME_STREAM) == 1, "focus gained: restore full gain once");
	expect(NativeFocusMute_NeedsApplyPure(NATIVE_FOCUS_MUTE_GAIN_MUTED, NATIVE_FOCUS_MUTE_GAIN_MUTED, REOPENED) == 1,
	       "reopened audio device: re-apply even though the wanted gain did not change");
	expect(NativeFocusMute_NeedsApplyPure(NATIVE_FOCUS_MUTE_GAIN_FULL, NATIVE_FOCUS_MUTE_GAIN_FULL, REOPENED) == 1,
	       "reopened audio device at full gain: re-apply, the new stream was never told");
}

int main(void)
{
	TestOptionOffNeverMutes();
	TestOptionOnMutesOnlyWhenUnfocused();
	TestToggledWhileUnfocused();
	TestWindowStartsUnfocused();
	TestNeedsApply();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures != 0;
}
