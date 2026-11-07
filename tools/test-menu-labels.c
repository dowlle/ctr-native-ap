// cc -m32 -DCTR_AP -I . -I include -I externals/SDL/include tools/test-menu-labels.c -o /tmp/test-menu-labels
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include <platform/native_config.h>
#include <platform/native_controls.h>

#include "platform/native_config.c"
static SDL_GamepadType padType;
static int assigned = 0, hasPad = 1;
int NativeControls_GetPlayerDevice(int player) { assert(player == 0); return assigned; }
void *NativeControls_GetGamepadHandle(int device) { assert(device == assigned); return hasPad ? (void *)1 : NULL; }
SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *pad) { assert(pad == (void *)1); return padType; }
#include "platform/native_menu.c"

int main(void)
{
    assert(g_config.deviceAwareLabels);
    char tmp[] = "/tmp/ctr-menu-labels-XXXXXX";
    assert(mkdtemp(tmp) && chdir(tmp) == 0);
    g_config.deviceAwareLabels = false; NativeConfig_Save();
    g_config.deviceAwareLabels = true; NativeConfig_Load();
    assert(!g_config.deviceAwareLabels);
    g_config.deviceAwareLabels = true; NativeConfig_Save();
    g_config.deviceAwareLabels = false; NativeConfig_Load();
    assert(g_config.deviceAwareLabels);
    assert(unlink("config.ini") == 0 && chdir("/") == 0 && rmdir(tmp) == 0);
    const char *hint = "*: OPEN  @: CONFIRM  [: HELP  ^: BACK";
    const char *letters = "A: OPEN  B: CONFIRM  X: HELP  Y: BACK";
    for (int type = SDL_GAMEPAD_TYPE_PS3; type <= SDL_GAMEPAD_TYPE_PS5; ++type) {
        padType = type;
        assert(!strcmp(NativeMenu_Labels(hint), hint));
    }
    const int types[] = {SDL_GAMEPAD_TYPE_XBOX360, SDL_GAMEPAD_TYPE_XBOXONE, SDL_GAMEPAD_TYPE_STANDARD};
    for (unsigned i = 0; i < sizeof types / sizeof *types; ++i) {
        padType = types[i];
        assert(!strcmp(NativeMenu_Labels(hint), letters));
        g_config.deviceAwareLabels = false;
        assert(!strcmp(NativeMenu_Labels(hint), hint));
        g_config.deviceAwareLabels = true;
    }
    /* Unknown pads and the keyboard keep the retail fallback. Hotplug and
       a changed P1 assignment take effect on the very next hint. */
    padType = SDL_GAMEPAD_TYPE_UNKNOWN;
    assert(!strcmp(NativeMenu_Labels(hint), hint));
    padType = SDL_GAMEPAD_TYPE_STANDARD; hasPad = 0;
    assert(!strcmp(NativeMenu_Labels(hint), hint));
    hasPad = 1; assigned = 3;
    assert(!strcmp(NativeMenu_Labels(hint), letters));
    char longHint[2048]; memset(longHint, '*', sizeof longHint - 1); longHint[2047] = 0;
    assert(strlen(NativeMenu_Labels(longHint)) == 1023);
    int armed = -1;
    assert(!NativeMenu_ResetPress(&armed, 0, true, false));
    assert(NativeMenu_ResetPress(&armed, 0, true, false));
    assert(!NativeMenu_ResetPress(&armed, 0, true, false));
    assert(!NativeMenu_ResetPress(&armed, 1, true, false));
    assert(!NativeMenu_ResetPress(&armed, 1, false, true) && armed == -1);
    puts("menu labels: PS3/4/5, Xbox 360/One, Deck standard, toggle, keyboard, hotplug, bounds and reset passed");
    return 0;
}
