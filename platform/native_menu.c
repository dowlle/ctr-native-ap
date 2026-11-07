#include <SDL3/SDL.h>
#include <platform/native_menu.h>
#include <platform/native_config.h>
#include <platform/native_controls.h>

/* Steam Input exposes Deck's controller as STANDARD or Xbox. */
static bool NativeMenu_GamepadUsesLetters(SDL_GamepadType type)
{
    return type == SDL_GAMEPAD_TYPE_STANDARD || type == SDL_GAMEPAD_TYPE_XBOX360 ||
           type == SDL_GAMEPAD_TYPE_XBOXONE;
}

char *NativeMenu_Labels(const char *hint)
{
    static char buffers[8][1024];
    static unsigned next;
    char *out = buffers[next++ % 8];
    bool letters = false;
#ifdef CTR_AP
    if (g_config.deviceAwareLabels) {
        SDL_Gamepad *pad = NativeControls_GetGamepadHandle(NativeControls_GetPlayerDevice(0));
        if (pad) letters = NativeMenu_GamepadUsesLetters(SDL_GetGamepadType(pad));
    }
#endif
    size_t i = 0;
    for (; hint[i] && i + 1 < sizeof buffers[0]; ++i)
        out[i] = NativeMenu_LabelGlyph(hint[i], letters);
    out[i] = 0;
    return out;
}
