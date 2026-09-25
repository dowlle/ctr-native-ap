// Minimal SDL3 audio surface for host harnesses that compile
// platform/native_audio.c without SDL (tools/test-spu-memory.c). Opening a
// device succeeds with a dummy stream; nothing is ever played.
#ifndef CTR_HARNESS_SDL3_STUB_H
#define CTR_HARNESS_SDL3_STUB_H
#include <stddef.h>
#include <stdint.h>

#define SDLCALL
#define SDL_INIT_AUDIO 0x10u
#define SDL_HINT_AUDIO_DRIVER "SDL_AUDIO_DRIVER"
#define SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES "SDL_AUDIO_DEVICE_SAMPLE_FRAMES"
#define SDL_AUDIO_S16 0x8010
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ((SDL_AudioDeviceID)0xffffffffu)

typedef uint32_t SDL_AudioDeviceID;
typedef int SDL_AudioFormat;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef struct SDL_AudioSpec
{
	SDL_AudioFormat format;
	int channels;
	int freq;
} SDL_AudioSpec;
typedef void (SDLCALL *SDL_AudioStreamCallback)(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount);

static char s_sdlStubStream;

static inline const char *SDL_GetError(void) { return "stub"; }
static inline const char *SDL_GetHint(const char *name) { (void)name; return "stub"; }
static inline int SDL_SetHint(const char *name, const char *value) { (void)name; (void)value; return 1; }
static inline int SDL_InitSubSystem(uint32_t flags) { (void)flags; return 1; }
static inline void SDL_QuitSubSystem(uint32_t flags) { (void)flags; }
static inline const char *SDL_GetCurrentAudioDriver(void) { return "stub"; }
static inline SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
                                                         SDL_AudioStreamCallback callback, void *userdata)
{
	(void)device; (void)spec; (void)callback; (void)userdata;
	return (SDL_AudioStream *)&s_sdlStubStream;
}
static inline SDL_AudioDeviceID SDL_GetAudioStreamDevice(SDL_AudioStream *stream) { (void)stream; return 1; }
static inline int SDL_GetAudioDeviceFormat(SDL_AudioDeviceID device, SDL_AudioSpec *spec, int *frames)
{
	(void)device;
	spec->format = SDL_AUDIO_S16; spec->channels = 2; spec->freq = 44100;
	if (frames) *frames = 1024;
	return 1;
}
static inline int SDL_GetAudioStreamFormat(SDL_AudioStream *stream, SDL_AudioSpec *src, SDL_AudioSpec *dst)
{
	(void)stream;
	if (src) { src->format = SDL_AUDIO_S16; src->channels = 2; src->freq = 44100; }
	if (dst) { dst->format = SDL_AUDIO_S16; dst->channels = 2; dst->freq = 44100; }
	return 1;
}
static inline int SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream) { (void)stream; return 1; }
static inline void SDL_DestroyAudioStream(SDL_AudioStream *stream) { (void)stream; }
static inline int SDL_LockAudioStream(SDL_AudioStream *stream) { (void)stream; return 1; }
static inline int SDL_UnlockAudioStream(SDL_AudioStream *stream) { (void)stream; return 1; }
static inline int SDL_ClearAudioStream(SDL_AudioStream *stream) { (void)stream; return 1; }
static inline int SDL_GetAudioStreamQueued(SDL_AudioStream *stream) { (void)stream; return 0; }
static inline int SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buf, int len)
{
	(void)stream; (void)buf; (void)len;
	return 1;
}
static inline int SDL_SetAudioStreamGain(SDL_AudioStream *stream, float gain) { (void)stream; (void)gain; return 1; }

#endif
