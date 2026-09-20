#ifndef NATIVE_FOCUS_MUTE_H
#define NATIVE_FOCUS_MUTE_H

// Freestanding decision logic for the "mute when the window is not focused"
// option (community request: running several randomizer clients on one PC and
// alt-tabbing between them, only the game on screen should make noise).
// Pulled out of platform/native_audio.c so a host harness can pin the
// option + focus -> output-gain decision without linking SDL, exactly the way
// native_vsync.h and native_window_geometry.h split decision from gather.
//
// NOTHING here touches SDL: the caller
// (NativeAudio_UpdateFocusMuteState, platform/native_audio.c) supplies the
// option value and the window's focus state and applies the resulting gain.
// Both sides agree on the same header, so the harness and the game cannot
// drift.
//
// Persisted field (platform/native_config.h, [Video & QoL] section,
// g_config.muteWhenUnfocused, CFG_BOOL):
//   mute_when_unfocused   false = never mute (default, today's behaviour
//                         exactly), true = silence the audio output while the
//                         game window does not have input focus.
//
// The gain is applied to the SDL output stream, AFTER the SPU mix
// (NativeAudio_RenderFrames) has produced its samples. Nothing about the
// emulated SPU, the XA player, the game clock or the deterministic replay
// path changes: voices keep running, music keeps its position, and unmuting
// resumes mid-note instead of restarting anything. That is also why muting is
// not done by stopping voices or by writing the SPU master volume -- those are
// game state, they are captured in savestates and they would desynchronise a
// deterministic replay.

#ifdef __cplusplus
extern "C" {
#endif

// Output-stream gain (a linear multiplier, SDL_SetAudioStreamGain's unit).
// "Full" is the pass-through value the stream is opened with, so the option
// being off never changes a sample.
#define NATIVE_FOCUS_MUTE_GAIN_FULL  1.0f
#define NATIVE_FOCUS_MUTE_GAIN_MUTED 0.0f

// The gain the output stream should carry right now. Muted only when the
// option is on AND the window has lost input focus; every other combination
// is full gain, including "option off while unfocused" (the default single
// game setup must never be touched) and "option turned off while the window
// is still unfocused" (the next call restores full gain without waiting for a
// focus event).
//
// windowFocused is a tri-state so the caller can say "not known yet": a
// negative value means the focus state has not been read from SDL, and is
// treated as focused, so a start-up frame before the window flags are read
// can never silence the game by accident. The caller seeds the real value
// from the window's SDL_WINDOW_INPUT_FOCUS flag on the first frame, so an
// app that starts unfocused mutes from that frame on.
static inline float NativeFocusMute_GainPure(int muteWhenUnfocused, int windowFocused)
{
	if (muteWhenUnfocused == 0)
		return NATIVE_FOCUS_MUTE_GAIN_FULL;
	if (windowFocused != 0)
		return NATIVE_FOCUS_MUTE_GAIN_FULL;
	return NATIVE_FOCUS_MUTE_GAIN_MUTED;
}

// Whether the caller has to push the gain into SDL this frame. The decision
// runs every frame, but the SDL call is only made when the wanted gain
// differs from what was last applied, or when the stream it was applied to is
// gone (the audio device was reopened, so the new stream starts at its
// default gain and has to be told again).
//
// appliedGain is only meaningful when streamUnchanged is non-zero; a
// reopened stream always re-applies.
static inline int NativeFocusMute_NeedsApplyPure(float wantedGain, float appliedGain, int streamUnchanged)
{
	if (streamUnchanged == 0)
		return 1;
	return (wantedGain != appliedGain) ? 1 : 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NATIVE_FOCUS_MUTE_H
