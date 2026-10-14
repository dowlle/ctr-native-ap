#ifndef PLATFORM_NATIVE_INPUT_H
#define PLATFORM_NATIVE_INPUT_H

#include <macros.h>

#define PLATFORM_INPUT_PAD_COUNT 4

// Analog value a control has to reach to count as held. Shared by the input layer and
// the controls module's capture so both agree on what counts as pressed.
#define PLATFORM_INPUT_PRESS_THRESHOLD 16384

struct PlatformInputPadSnapshot
{
	u8 status;
	u8 id;
	u8 buttons[2];
	u8 analog[4];
	u8 connected;
	u8 reserved[3];
};

int Platform_InputInit(void);
void Platform_InputShutdown(void);
void Platform_InputUpdate(void);
void Platform_InputControllerAdded(int deviceIndex);
void Platform_InputControllerRemoved(int instanceId);

// Logs the player-to-device assignment resolved by platform/native_controls.c.
// Replaces the F4/F6 slot cycling: the Options -> Controls page owns assignment.
void Platform_InputLogAssignment(void);

#if defined(CTR_AP) || defined(CTR_CUSTOM_PACKAGES) || defined(CTR_BOX_AUTHORING)
// Raw keyboard probe (SDL_SCANCODE_*) for AP debug/QoL hotkeys. 1 if down, 0
// otherwise. Mapping-independent; not part of the PSX pad bus. CTR_AP builds,
// and the authoring client (Track Manager, Box Author Mode keys) without it.
int Platform_InputRawKeyDown(int scancode);

// Buttons held on physical gamepads only, active-high in the RAW_BTN_* layout.
// Excludes the keyboard device, so it stays usable while a NativeText
// session is capturing keystrokes. Read through each pad's Controls page
// bindings; not part of the PSX pad bus. Same builds as above.
int Platform_InputRawGamepadButtons(void);
#endif

void Platform_InputPadInit(int slot, unsigned char *padData);
int Platform_InputPadGetState(int port);
void Platform_InputPadVibrate(int port, unsigned char *table, int len);
int Platform_InputCapturePadSnapshots(struct PlatformInputPadSnapshot *dst, int count);
int Platform_InputInstallPadSnapshots(const struct PlatformInputPadSnapshot *src, int count);
void Platform_InputClearInstalledPadSnapshots(void);
int Platform_InputGetStateSize(void);
int Platform_InputCaptureState(void *dst, int dstSize);
int Platform_InputRestoreState(const void *src, int srcSize);

#endif
