// Unit tests for platform/native_controls.c and the parts of platform/native_input.c
// that read it.
//
// Ported from CTR Native Expanded by thecodingbob
// (https://github.com/thecodingbob/ctr-native, tools/controls/test_native_controls.c
// at commit 45d74d619, PR 54 and PR 70), GPL-3.0. See THIRD_PARTY_NOTICES.md.
// Changes for CTR Archipelago: controls.ini is cwd-relative here, so the scratch
// directory is entered with chdir instead of redirecting NativeAssets_GetBaseDir;
// the omni-controller hack and native_config.c are not part of this build; added
// checks for the AP raw keyboard probe and physical-pad-only button mask, for
// refusing the pre-remap input snapshot layout, and for the bindable AP shortcuts
// ([AP Shortcuts] and the one-key-one-action rule).
//
// The module is a .c in the unity build, so the test includes it directly and
// drives the device table by hand. That keeps the tests focused on the logic that
// is easy to get wrong: assignment order, the anti-softlock rule, hotplug
// recovery, and controls.ini round-tripping.
//
// It links a host build of the vendored SDL3. tools/controls/run-tests.sh builds
// that library once (into $HARNESS_OUT, default /tmp/ctr-harness) and then this
// file; CI runs it through the `cmd` line in tools/ci/harnesses.txt:
//   bash tools/controls/run-tests.sh
//
// The directory it is started in does not matter: controls.ini is read and written in a
// scratch directory that is removed again on the way out.

// SDL must come before macros.h: the project defines `internal` as `static`, which
// would otherwise rewrite SDL's `SDL_DisplayModeData.internal` field. The unity
// build gets this ordering for free because main.c includes SDL first.
#include <SDL3/SDL.h>

#include <macros.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The pad-bus replay mirrors the game's pad structures.
#include <namespace_Gamepad.h>
#include <psx/libpad.h>

// PadTypeID values from PSn00bSDK's psxpad.h. That header also carries the PadButton
// enum namespace_Gamepad.h copies, so it cannot be included next to it here.
enum
{
	TEST_PAD_ID_ANALOG_STICK = 0x5,
	TEST_PAD_ID_ANALOG = 0x7,
	TEST_PAD_ID_MULTITAP = 0x8,
};
#define PAD_ID_ANALOG_STICK TEST_PAD_ID_ANALOG_STICK
#define PAD_ID_ANALOG       TEST_PAD_ID_ANALOG
#define PAD_ID_MULTITAP     TEST_PAD_ID_MULTITAP

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

// controls.ini is resolved against the working directory, like config.ini. Left there it
// would land wherever the binary was started, which is normally the repository root, and a
// real controls.ini there would be overwritten by whatever the last test wrote.
// TestUseScratchDir creates a disposable directory and changes into it instead.
static char s_testBaseDir[512] = "./";

// Owned by the game layer; native_input.c gates its update on it.
s32 g_padCommEnable = 1;

void Platform_Log(const char *fmt, ...)
{
	(void)fmt;
}

void Platform_LogWarn(const char *fmt, ...)
{
	(void)fmt;
}

// native_platform.c owns text entry; the shortcut tests flip this to stand in for an
// open Connection page field.
static int s_testTextActive;

int NativeText_Active(void)
{
	return s_testTextActive;
}

// native_controls.c comes first: the input layer reads the assignment from it.
#include <platform/native_controls.c>
#include <platform/native_input.c>

// Real storage, not pointers into SetupDevices' locals.
static char s_testDeviceKeys[NATIVE_CONTROLS_MAX_DEVICES][64];
static char s_testDeviceNames[NATIVE_CONTROLS_MAX_DEVICES][32];

#define TEST_KEYBOARD_NAME "Keyboard"

static int s_failures;
static int s_checks;

static const char *TestPadName(int index)
{
	static const char *names[4] = {"Pad A", "Pad B", "Pad C", "Pad D"};

	return (index >= 0 && index < 4) ? names[index] : "?";
}

static void Check(bool condition, const char *what)
{
	s_checks++;

	if (!condition)
	{
		s_failures++;
		printf("  FAIL: %s\n", what);
	}
}

// expectedName of NULL means the player must have no device.
static void CheckDevice(int player, const char *expectedName, const char *what)
{
	const int device = NativeControls_GetPlayerDevice(player);

	if (expectedName == NULL)
	{
		Check(device == NATIVE_CONTROLS_NO_DEVICE, what);
		return;
	}

	if (device == NATIVE_CONTROLS_NO_DEVICE)
	{
		Check(false, what);
		return;
	}

	Check(NativeControls_StrEqual(NativeControls_GetDeviceName(device), expectedName), what);
}

// Mirrors how the module itself builds its path, so the tests exercise the same
// resolution the game uses.
static void TestFilePath(char *dst, size_t dstSize)
{
	snprintf(dst, dstSize, "%s%s", s_testBaseDir, NATIVE_CONTROLS_FILE_NAME);
}

// Removes the scratch directory and everything in it. Built from the pref path rather
// than from s_testBaseDir, so this also works before the base directory is pointed at it.
static void TestRemoveScratchDir(void)
{
	char *path = SDL_GetPrefPath("ctr-native", "controls-tests");

	if (path == NULL)
	{
		return;
	}

	// SDL_RemovePath only removes an empty directory, so the file goes first. The pref
	// path already ends in a separator.
	char file[sizeof(s_testBaseDir)];
	snprintf(file, sizeof(file), "%s%s", path, NATIVE_CONTROLS_FILE_NAME);
	SDL_RemovePath(file);
	SDL_RemovePath(path);
	SDL_free(path);
}

// Redirects s_testBaseDir into a scratch directory under the SDL pref path. Refuses to
// run rather than falling back to the working directory, because that fallback is exactly
// the file it exists to protect.
static void TestUseScratchDir(void)
{
	char *path = SDL_GetPrefPath("ctr-native", "controls-tests");

	if ((path == NULL) || (strlen(path) >= sizeof(s_testBaseDir)))
	{
		printf("could not resolve a scratch directory path\n");
		exit(1);
	}

	// SDL_CreateDirectory fails on a directory that is already there, which a run killed
	// part way through would leave behind, so clear it out first.
	TestRemoveScratchDir();

	if (!SDL_CreateDirectory(path))
	{
		printf("could not create a scratch directory at %s\n", path);
		exit(1);
	}

	snprintf(s_testBaseDir, sizeof(s_testBaseDir), "%s", path);
	SDL_free(path);

	if (chdir(s_testBaseDir) != 0)
	{
		printf("could not enter the scratch directory %s\n", s_testBaseDir);
		exit(1);
	}
}

static void ResetModule(void)
{
	memset(s_profiles, 0, sizeof(s_profiles));
	memset(s_devices, 0, sizeof(s_devices));
	memset(s_deviceOwner, 0, sizeof(s_deviceOwner));
	memset(s_playerPref, 0, sizeof(s_playerPref));
	s_deviceCount = 0;
	s_controlsGeneration = 0;

	// The input layer caches the generation it last synced against and skips the
	// projection when it matches. Reset both together, or a test can silently skip a
	// sync because a previous test left the counter on the same value.
	s_syncedGeneration = 0;

	s_refreshed = false;
	s_capturePhase = NATIVE_CONTROLS_CAPTURE_IDLE;
	s_captureDevice = NATIVE_CONTROLS_NO_DEVICE;
	s_captureBinding = -1;
	s_captureShortcut = -1;
	s_captureInstanceId = -1;
	NativeControls_ResetShortcuts();
	s_testTextActive = 0;
	s_devKeysOn = 0;
	s_demoCamOn = 0;

	for (int player = 0; player < PLATFORM_INPUT_PAD_COUNT; player++)
	{
		s_playerPref[player].kind = NATIVE_CONTROLS_PREF_AUTO;
		s_playerPref[player].deviceKey[0] = '\0';
		s_playerDevice[player] = NATIVE_CONTROLS_NO_DEVICE;
	}
}

// Builds a device table of `gamepadCount` fake gamepads followed by the keyboard,
// mirroring the order NativeControls_EnumerateDevices produces. Device indices for
// the fake gamepads go to gamepadDevice; the keyboard's index to keyboardDevice.
static void SetupDevices(int gamepadCount, int *gamepadDevice, int *keyboardDevice)
{
	memset(s_devices, 0, sizeof(s_devices));
	memset(s_deviceOwner, 0, sizeof(s_deviceOwner));
	s_deviceCount = 0;

	for (int i = 0; i < gamepadCount; i++)
	{
		snprintf(s_testDeviceKeys[i], sizeof(s_testDeviceKeys[i]), "test-guid-%d", i);
		snprintf(s_testDeviceNames[i], sizeof(s_testDeviceNames[i]), "%s", TestPadName(i));

		struct NativeControlsDevice *device = &s_devices[s_deviceCount];
		device->used = true;
		device->kind = NATIVE_CONTROLS_DEVICE_GAMEPAD;
		device->instanceId = 100 + i;
		device->profile =
		    NativeControls_ProfileForDevice(NATIVE_CONTROLS_DEVICE_GAMEPAD, s_testDeviceKeys[i], s_testDeviceNames[i]);
		snprintf(device->name, sizeof(device->name), "%s", s_testDeviceNames[i]);
		device->gamepad = NULL;

		gamepadDevice[i] = s_deviceCount;
		s_deviceCount++;
	}

	struct NativeControlsDevice *keyboard = &s_devices[s_deviceCount];
	keyboard->used = true;
	keyboard->kind = NATIVE_CONTROLS_DEVICE_KEYBOARD;
	keyboard->instanceId = -1;
	keyboard->profile =
	    NativeControls_ProfileForDevice(NATIVE_CONTROLS_DEVICE_KEYBOARD, NATIVE_CONTROLS_KEYBOARD_KEY, TEST_KEYBOARD_NAME);
	snprintf(keyboard->name, sizeof(keyboard->name), "%s", TEST_KEYBOARD_NAME);
	keyboard->gamepad = NULL;

	*keyboardDevice = s_deviceCount;
	s_deviceCount++;
}

static void SetPref(int player, int kind, const char *deviceKey)
{
	s_playerPref[player].kind = kind;
	snprintf(s_playerPref[player].deviceKey, sizeof(s_playerPref[player].deviceKey), "%s",
	         (deviceKey != NULL) ? deviceKey : "");
}

// Regression: the first refresh once reported "no change" and skipped enumeration, leaving
// a keyboard-only machine with no devices at all. Asserted against whatever the host has
// plugged in, since a pad cannot be assumed absent: what must hold either way is that the
// first refresh enumerates and leaves player 1 holding something.
static void TestFirstRefreshAlwaysEnumeratesTheKeyboard(void)
{
	printf("refresh: the first refresh always enumerates and keeps a keyboard\n");

	ResetModule();
	s_refreshed = false;

	NativeControls_Refresh();

	const int keyboardDevice = NativeControls_FindKeyboardDevice();
	Check(keyboardDevice != NATIVE_CONTROLS_NO_DEVICE, "the keyboard is always enumerated");
	Check(s_refreshed, "the refresh is recorded as done");

	// Regression: the keyboard entry used to leave its name unset, so the menu drew an
	// empty value for the player holding it. Asserted directly, because with a pad
	// attached player 1 does not hold the keyboard and the name would go unchecked.
	Check(strlen(NativeControls_GetDeviceName(keyboardDevice)) != 0, "the keyboard has a name to display");
	Check(NativeControls_StrEqual(NativeControls_GetDeviceName(keyboardDevice), TEST_KEYBOARD_NAME),
	      "the keyboard is named Keyboard");

	// Player 1 must never be left without a device, whatever the host has plugged in.
	Check(NativeControls_GetPlayerDevice(0) != NATIVE_CONTROLS_NO_DEVICE, "player 1 holds a device");

	// The keyboard is the last fallback, so it only lands on player 1 when nothing
	// else is available.
	if (NativeControls_GetDeviceKind(NativeControls_GetPlayerDevice(0)) == NATIVE_CONTROLS_DEVICE_KEYBOARD)
	{
		CheckDevice(0, TEST_KEYBOARD_NAME, "player 1 has the keyboard when there is no pad");
	}

	// A second refresh with nothing changing must not disturb the assignment.
	const int generationBefore = (int)NativeControls_GetGeneration();
	NativeControls_Refresh();
	Check((int)NativeControls_GetGeneration() == generationBefore, "an unchanged refresh bumps no generation");
	CheckDevice(0, (NativeControls_GetDeviceKind(NativeControls_GetPlayerDevice(0)) == NATIVE_CONTROLS_DEVICE_GAMEPAD)
	                       ? NativeControls_GetDeviceName(NativeControls_GetPlayerDevice(0))
	                       : TEST_KEYBOARD_NAME,
	           "player 1 keeps its device across an unchanged refresh");
}

static void TestDisabledPlayerCanComeBack(void)
{
	printf("assignment: a disabled player can be given a device again\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	// P1 takes the pad, the keyboard falls through to P2.
	NativeControls_ResolveAssignment();
	CheckDevice(0, TestPadName(0), "P1 starts on the pad");
	CheckDevice(1, TEST_KEYBOARD_NAME, "P2 starts on the keyboard");

	// Disable P2 the way the menu does. Its device falls to the next automatic
	// player, so nothing is free and no named device can be picked any more.
	NativeControls_SetPlayerPref(1, NATIVE_CONTROLS_PREF_DISABLED, NATIVE_CONTROLS_NO_DEVICE);
	CheckDevice(1, NULL, "P2 is disabled");
	CheckDevice(2, TEST_KEYBOARD_NAME, "the keyboard moved to P3");

	int available[NATIVE_CONTROLS_MAX_DEVICES];
	Check(NativeControls_GetAvailableDevices(1, available, NATIVE_CONTROLS_MAX_DEVICES) == 0,
	      "P2 has no device available to pick, only the way out of disabled");

	// Returning to automatic is what gets the keyboard back off P3.
	NativeControls_SetPlayerPref(1, NATIVE_CONTROLS_PREF_AUTO, NATIVE_CONTROLS_NO_DEVICE);
	CheckDevice(1, TEST_KEYBOARD_NAME, "P2 gets the keyboard back");
	CheckDevice(2, NULL, "P3 gives it up");
}

static void DeclareDevicesForBus(int gamepadCount, int *gamepadDevice, int *keyboardDevice);

// === Rebind timeout ===

// A pending capture must not leave a binding stranded, or a player who walks away
// mid-rebind can end up with that control dead. BeginCapture blanks the binding so the
// pending state is visible, and the timeout puts the original value back.
static void TestCaptureTimeoutRestoresTheBinding(void)
{
	printf("capture: timing out restores the previous binding\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(1, pads, &keyboard);

	const int device = keyboard;
	const int binding = NATIVE_CONTROLS_BIND_CIRCLE;
	const s32 original = NativeControls_GetBinding(device, binding);

	Check(original == SDL_SCANCODE_V, "the circle binding starts at its default");

	NativeControls_BeginCapture(device, binding);
	Check(NativeControls_IsCapturing(), "a capture is pending");
	Check(NativeControls_GetBinding(device, binding) == NATIVE_CONTROLS_BINDING_NONE,
	      "the binding reads unbound while pending, so the old value is not in effect");

	// Poll without any input. The device table is unchanged, so the capture cannot be
	// lost to hotplug and every poll is a genuine timeout tick.
	int result = 0;
	int timedOut = 0;

	for (int frame = 0; frame < NATIVE_CONTROLS_CAPTURE_TIMEOUT_FRAMES + 8; frame++)
	{
		result = NativeControls_PollCapture();

		if (result == 2)
		{
			timedOut = frame;
			break;
		}
	}

	Check(timedOut > 0, "the capture times out rather than waiting forever");
	Check(!NativeControls_IsCapturing(), "no capture is pending after the timeout");
	Check(NativeControls_GetBinding(device, binding) == original, "the previous binding is restored");
	Check(result == 2, "the timeout is reported distinctly from a captured value");
}

static void TestCancelRestoresTheBinding(void)
{
	printf("capture: cancelling restores the previous binding\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(1, pads, &keyboard);

	const int binding = NATIVE_CONTROLS_BIND_CIRCLE;
	const s32 original = NativeControls_GetBinding(keyboard, binding);

	NativeControls_BeginCapture(keyboard, binding);
	NativeControls_CancelCapture();

	Check(!NativeControls_IsCapturing(), "no capture is pending after cancelling");
	Check(NativeControls_GetBinding(keyboard, binding) == original, "the previous binding is restored");
}

// A capture that committed must not have its new value stomped by a later cancel or
// timeout path, which is why NativeControls_RestoreCaptureValue only writes while the
// binding still holds the placeholder.
static void TestCommittedCaptureSurvivesACancel(void)
{
	printf("capture: a committed value is not reverted by a later cancel\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(1, pads, &keyboard);

	NativeControls_BeginCapture(keyboard, NATIVE_CONTROLS_BIND_CIRCLE);

	// Commit directly the way a landed press does, then let the normal end-of-capture
	// cleanup run.
	s32 *bindings = NativeControls_BindingsOfDevice(keyboard);
	Check(bindings != NULL, "the keyboard has a bindings table");

	if (bindings != NULL)
	{
		bindings[NATIVE_CONTROLS_BIND_CIRCLE] = SDL_SCANCODE_Q;
	}

	NativeControls_CancelCapture();

	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CIRCLE) == SDL_SCANCODE_Q,
	      "the committed value survives");
}

// A chosen key is a proposal, not a commit: it lands in the bindings table so the menu
// can show it, but only survives if Cross confirms it on the same device.
static void TestChosenKeyWaitsForConfirmation(void)
{
	printf("capture: a chosen key is not committed until Cross confirms\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(1, pads, &keyboard);

	const int binding = NATIVE_CONTROLS_BIND_CIRCLE;
	const s32 original = NativeControls_GetBinding(keyboard, binding);

	NativeControls_BeginCapture(keyboard, binding);
	Check(NativeControls_GetBinding(keyboard, binding) == NATIVE_CONTROLS_BINDING_NONE,
	      "the binding is blanked while listening");
	Check(!NativeControls_IsConfirming(), "not confirming before a key is chosen");

	// Stand in for a key press landing, which the host would otherwise supply.
	s_capturePendingValue = SDL_SCANCODE_Q;
	s32 *bindings = NativeControls_BindingsOfDevice(keyboard);

	if (bindings != NULL)
	{
		bindings[binding] = SDL_SCANCODE_Q;
	}
	s_capturePhase = NATIVE_CONTROLS_CAPTURE_CONFIRM_WAIT_RELEASE;

	Check(NativeControls_IsConfirming(), "the capture is waiting for confirmation");
	Check(NativeControls_GetBinding(keyboard, binding) == SDL_SCANCODE_Q, "the proposal is visible");

	// Abandoning it puts the original back.
	NativeControls_CancelCapture();
	Check(NativeControls_GetBinding(keyboard, binding) == original,
	      "an unconfirmed proposal is reverted on cancel");
}

// The proposal must not be treated as committed: an abandoned confirm step restores the
// previous value rather than leaving the proposal in place.
static void TestUnconfirmedProposalIsRevertedOnTimeout(void)
{
	printf("capture: an unconfirmed proposal reverts when the window expires\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(1, pads, &keyboard);

	const int binding = NATIVE_CONTROLS_BIND_CIRCLE;
	const s32 original = NativeControls_GetBinding(keyboard, binding);

	NativeControls_BeginCapture(keyboard, binding);
	s_capturePendingValue = SDL_SCANCODE_Q;

	s32 *bindings = NativeControls_BindingsOfDevice(keyboard);

	if (bindings != NULL)
	{
		bindings[binding] = SDL_SCANCODE_Q;
	}
	s_capturePhase = NATIVE_CONTROLS_CAPTURE_CONFIRM;

	int result = 0;
	for (int frame = 0; frame < NATIVE_CONTROLS_CAPTURE_TIMEOUT_FRAMES + 8; frame++)
	{
		result = NativeControls_PollCapture();

		if (result != 0)
		{
			break;
		}
	}

	Check(result == 2, "the unconfirmed capture times out");
	Check(NativeControls_GetBinding(keyboard, binding) == original, "the original binding is restored");
}

// Confirming reads whatever control Cross is bound to, not a physical Cross, so that
// rebinding Cross itself cannot leave the device without a way to confirm.
static void TestConfirmUsesTheCurrentCrossBinding(void)
{
	printf("capture: confirming uses the device's current Cross binding\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(1, pads, &keyboard);

	// The keyboard has no active keys under test, so no Cross binding reports held.
	Check(!NativeControls_IsCrossHeld(keyboard), "Cross is not held with no key down");

	// An unbound Cross can never be pressed, so an unreachable proposal cannot be
	// confirmed and the capture reverts instead of locking the device out.
	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_CROSS, NATIVE_CONTROLS_BINDING_NONE);
	Check(!NativeControls_IsCrossHeld(keyboard), "an unbound Cross never reports held");

	// A gamepad handle is a marker here and must never be dereferenced, so only the
	// null-handle path is safe to exercise.
	Check(!NativeControls_IsCrossHeld(NATIVE_CONTROLS_NO_DEVICE),
	      "an invalid device never reports Cross held");
}

// Regression: leaving the menu after a confirmed rebind rolled the binding back in memory
// while the file kept the new value, so the change vanished on the next save.
static void TestLeavingAfterACommitKeepsTheBinding(void)
{
	printf("capture: leaving after a commit does not roll the binding back\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);
	NativeControls_ResolveAssignment();

	const int binding = NATIVE_CONTROLS_BIND_CIRCLE;
	const s32 original = NativeControls_GetBinding(keyboard, binding);

	NativeControls_BeginCapture(keyboard, binding);
	s_capturePendingValue = SDL_SCANCODE_Q;

	s32 *bindings = NativeControls_BindingsOfDevice(keyboard);

	if (bindings != NULL)
	{
		bindings[binding] = SDL_SCANCODE_Q;
	}

	// Stand in for the confirming Cross press landing on a device with no keys down.
	s_capturePhase = NATIVE_CONTROLS_CAPTURE_CONFIRM;
	Check(NativeControls_CommitCapture() == 1, "the commit reports success");

	Check(NativeControls_GetBinding(keyboard, binding) == SDL_SCANCODE_Q, "the commit took effect");

	// This is the back press in MM_ConfigMenu, which runs after a confirmed rebind.
	NativeControls_CancelCapture();
	Check(NativeControls_GetBinding(keyboard, binding) == SDL_SCANCODE_Q,
	      "the committed binding survives a later cancel");
	Check(NativeControls_GetBinding(keyboard, binding) != original,
	      "the pre-capture value did not come back");
	Check(!NativeControls_IsCapturing(), "the capture is fully ended");

	// The user-visible symptom: any later save must still write the new value out.
	NativeControls_Save();
	ResetModule();
	NativeControls_Load();

	int reloadedPads[4];
	int reloadedKeyboard;
	SetupDevices(1, reloadedPads, &reloadedKeyboard);

	Check(NativeControls_GetBinding(reloadedKeyboard, NATIVE_CONTROLS_BIND_CIRCLE) == SDL_SCANCODE_Q,
	      "the committed binding is still on disk after the cancel and a later save");
}

// The timeout is a softlock failsafe, so the window has to be long enough to be usable
// and short enough that an abandoned rebind clears itself.
static void TestCaptureTimeoutWindowIsReasonable(void)
{
	printf("capture: the timeout window is usable but bounded\n");

	Check(NATIVE_CONTROLS_CAPTURE_TIMEOUT_FRAMES >= 120, "at least two seconds at 60Hz");
	Check(NATIVE_CONTROLS_CAPTURE_TIMEOUT_FRAMES <= 600, "no more than ten seconds at 60Hz");
}

// === Pad bus layout ===
//
// These drive the real NativeInput_WritePadBus and replay the game's own
// GAMEPAD_GetNumConnected walk over the written bytes, so the player-to-slot mapping
// is verified end to end rather than inferred from the assignment table.

// Declares `gamepadCount` pads followed by the keyboard, in the same order
// NativeControls_EnumerateDevices produces. Pad handles are non-NULL markers that
// nothing here dereferences, so the bus layout can be exercised without hardware.
static void DeclareDevicesForBus(int gamepadCount, int *gamepadDevice, int *keyboardDevice)
{
	memset(s_devices, 0, sizeof(s_devices));
	memset(s_deviceOwner, 0, sizeof(s_deviceOwner));
	s_deviceCount = 0;

	for (int i = 0; i < gamepadCount; i++)
	{
		char key[64];
		snprintf(key, sizeof(key), "bus-guid-%d", i);

		struct NativeControlsDevice *device = &s_devices[s_deviceCount];
		device->used = true;
		device->kind = NATIVE_CONTROLS_DEVICE_GAMEPAD;
		device->instanceId = 300 + i;
		device->profile = NativeControls_ProfileForDevice(NATIVE_CONTROLS_DEVICE_GAMEPAD, key, "Bus Pad");
		snprintf(device->name, sizeof(device->name), "%s", "Bus Pad");
		device->gamepad = (SDL_Gamepad *)(uintptr_t)(1 + i);

		gamepadDevice[i] = s_deviceCount;
		s_deviceCount++;
	}

	struct NativeControlsDevice *keyboard = &s_devices[s_deviceCount];
	keyboard->used = true;
	keyboard->kind = NATIVE_CONTROLS_DEVICE_KEYBOARD;
	keyboard->instanceId = -1;
	keyboard->profile =
	    NativeControls_ProfileForDevice(NATIVE_CONTROLS_DEVICE_KEYBOARD, NATIVE_CONTROLS_KEYBOARD_KEY, TEST_KEYBOARD_NAME);
	snprintf(keyboard->name, sizeof(keyboard->name), "%s", TEST_KEYBOARD_NAME);
	keyboard->gamepad = NULL;

	*keyboardDevice = s_deviceCount;
	s_deviceCount++;
}

// Mirrors struct MultitapPacket on the game side, which is what the pad buffers hold.
struct TestPadSlot
{
	u8 plugged;
	u8 controllerData;
	struct ControllerPacket controllers[4];
};

// Replays the walk in GAMEPAD_GetNumConnected, filling in per-player connection state.
// Returns how many pads the game finds on port 0: four for a multitap, one for a bare
// port, so a caller can tell which layout was actually on the bus.
static int GamePadBusView(u8 *port0, int *connectedPerPlayer)
{
	struct TestPadSlot *slot = (struct TestPadSlot *)port0;

	int numSlots = 2;
	int numPorts = 1;
	if ((slot->plugged == PLUGGED) && (slot->controllerData == (PAD_ID_MULTITAP << 4)))
	{
		numSlots = 1;
		numPorts = 4;
	}

	int padIndex = 0;
	for (int s = 0; s < numSlots; s++)
	{
		for (int p = 0; p < numPorts; p++)
		{
			struct ControllerPacket *packet = (struct ControllerPacket *)&slot[s];
			int isConnected = 0;

			if (slot[s].plugged == PLUGGED)
			{
				if (slot[s].controllerData == (PAD_ID_MULTITAP << 4))
				{
					packet = &slot[s].controllers[p];
				}

				if (packet->plugged == PLUGGED)
				{
					isConnected = 1;
				}
			}

			if (padIndex < 4)
			{
				connectedPerPlayer[padIndex] = isConnected;
			}
			padIndex++;
		}
	}

	return numPorts;
}

// Builds the slot table the way Platform_InputInit does, so every bus test below starts
// from the real created default rather than an assumed one.
static void ResetBusState(void)
{
	memset(s_controllers, 0, sizeof(s_controllers));
	for (int player = 0; player < PLATFORM_INPUT_PAD_COUNT; player++)
	{
		s_playerPref[player].kind = NATIVE_CONTROLS_PREF_AUTO;
		s_playerPref[player].deviceKey[0] = '\0';
		s_playerDevice[player] = NATIVE_CONTROLS_NO_DEVICE;
		NativeInput_ResetController(player);
	}
	s_refreshed = true;
	s_controlsGeneration++;
}

// Projects the resolved assignment onto pad slots and writes the bus, returning how many
// pads the game sees on port 0. Pads are marked connected without reading SDL, which is
// all the layout depends on.
static int RunBusLayout(int gamepadCount, int *connectedPerPlayer)
{
	int pads[4];
	int keyboard;

	ResetBusState();
	DeclareDevicesForBus(gamepadCount, pads, &keyboard);
	NativeControls_ResolveAssignment();

	for (int slot = 0; slot < PLATFORM_INPUT_PAD_COUNT; slot++)
	{
		s_controllers[slot].device = NativeControls_GetPlayerDevice(slot);
		NativeInput_ResetSnapshot(slot);

		if (s_controllers[slot].device != NATIVE_CONTROLS_NO_DEVICE)
		{
			s_controllers[slot].snapshot.connected = 1;
			s_controllers[slot].snapshot.status = 0;
			s_controllers[slot].snapshot.id = NATIVE_INPUT_PAD_ANALOG;
		}
	}

	u8 port0[NATIVE_INPUT_MULTITAP_HEADER + (NATIVE_INPUT_MAX_CONTROLLERS * NATIVE_INPUT_PAD_PACKET_BYTES)];
	u8 port1[NATIVE_INPUT_PAD_PACKET_BYTES];
	memset(port0, 0, sizeof(port0));
	memset(port1, 0, sizeof(port1));

	// Point the bus at the test buffers instead of the game's pad slots.
	s_padSlotData[0] = port0;
	s_padSlotData[1] = port1;
	NativeInput_WritePadBus();

	return GamePadBusView(port0, connectedPerPlayer);
}

static void TestBusIsAlwaysMultitap(void)
{
	printf("pad bus: always multitap, so player index matches pad slot\n");
	ResetModule();

	int perPlayer[4];
	int ports = RunBusLayout(1, perPlayer);

	Check(ports == 4, "the game walks four pads on port 0");
	Check(perPlayer[0] == 1, "player 1 is connected on the pad");
	Check(perPlayer[1] == 1, "player 2 is connected on the keyboard");
	Check(perPlayer[2] == 0, "player 3 has no device");
}

// Regression: a conditional multitap layout used to put a keyboard on player 2 in the wrong
// pad slot, so the game read player 2 as disconnected.
static void TestKeyboardOnPlayerTwoIsVisibleToTheGame(void)
{
	printf("pad bus: keyboard on player 2 does not displace player 2\n");
	ResetModule();

	int perPlayer[4];
	RunBusLayout(1, perPlayer);

	int connected = 0;
	for (int p = 0; p < 4; p++)
	{
		connected += perPlayer[p];
	}

	Check(connected == 2, "exactly two players are connected");
	Check(perPlayer[1] == 1, "player 2 reads its own device, not an empty slot");
}

static void TestAllFourPlayersAreReachable(void)
{
	printf("pad bus: four players are reachable\n");
	ResetModule();

	int perPlayer[4];
	RunBusLayout(3, perPlayer);

	Check(perPlayer[0] == 1, "player 1 has a pad");
	Check(perPlayer[1] == 1, "player 2 has a pad");
	Check(perPlayer[2] == 1, "player 3 has a pad");
	Check(perPlayer[3] == 1, "player 4 has the keyboard");
}

// Regression: a device change from the menu updated the assignment table but never published
// it, so the pad slots kept serving the old device and the change appeared to do nothing.
static void TestChangingAPlayerDeviceReachesThePadBus(void)
{
	printf("assignment: a menu device change reaches the pad bus\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(1, pads, &keyboard);

	NativeControls_ResolveAssignment();
	for (int slot = 0; slot < PLATFORM_INPUT_PAD_COUNT; slot++)
	{
		s_controllers[slot].device = NativeControls_GetPlayerDevice(slot);
	}

	CheckDevice(0, "Bus Pad", "P1 starts on the pad");
	CheckDevice(1, TEST_KEYBOARD_NAME, "P2 starts on the keyboard");

	// Move P1 onto the keyboard, which pushes the pad to P2.
	const u32 before = NativeControls_GetGeneration();
	Check(NativeControls_SetPlayerPref(0, NATIVE_CONTROLS_PREF_PINNED, keyboard) == 1, "the change is accepted");
	Check(NativeControls_GetGeneration() != before, "a device change bumps the generation");

	// The input layer keys off exactly this counter.
	NativeInput_SyncDevices();
	for (int slot = 0; slot < PLATFORM_INPUT_PAD_COUNT; slot++)
	{
		NativeInput_ResetSnapshot(slot);
		if (s_controllers[slot].device != NATIVE_CONTROLS_NO_DEVICE)
		{
			s_controllers[slot].snapshot.connected = 1;
			s_controllers[slot].snapshot.status = 0;
			s_controllers[slot].snapshot.id = NATIVE_INPUT_PAD_ANALOG;
		}
	}

	u8 port0[NATIVE_INPUT_MULTITAP_HEADER + (NATIVE_INPUT_MAX_CONTROLLERS * NATIVE_INPUT_PAD_PACKET_BYTES)];
	u8 port1[NATIVE_INPUT_PAD_PACKET_BYTES];
	memset(port0, 0, sizeof(port0));
	memset(port1, 0, sizeof(port1));
	s_padSlotData[0] = port0;
	s_padSlotData[1] = port1;
	NativeInput_WritePadBus();

	int perPlayer[4];
	GamePadBusView(port0, perPlayer);

	Check(s_controllers[0].device == keyboard, "pad slot 0 now follows the keyboard");
	Check(perPlayer[0] == 1, "player 1 still reads a connected device after the change");
	CheckDevice(1, "Bus Pad", "the pad moved to P2");
}

static void TestDisablingAPlayerReachesThePadBus(void)
{
	printf("assignment: disabling a player reaches the pad bus\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(1, pads, &keyboard);

	NativeControls_ResolveAssignment();
	NativeControls_SetPlayerPref(1, NATIVE_CONTROLS_PREF_DISABLED, NATIVE_CONTROLS_NO_DEVICE);
	NativeInput_SyncDevices();

	Check(s_controllers[1].device == NATIVE_CONTROLS_NO_DEVICE, "pad slot 1 was released");
}

static void TestDisabledPlayerLeavesAHole(void)
{
	printf("pad bus: a disabled player reports disconnected without shifting others\n");
	ResetModule();

	int pads[4];
	int keyboard;
	int perPlayer[4];

	ResetBusState();
	DeclareDevicesForBus(1, pads, &keyboard);

	NativeControls_SetPlayerPref(0, NATIVE_CONTROLS_PREF_PINNED, pads[0]);
	NativeControls_SetPlayerPref(1, NATIVE_CONTROLS_PREF_DISABLED, NATIVE_CONTROLS_NO_DEVICE);
	NativeControls_ResolveAssignment();

	for (int slot = 0; slot < PLATFORM_INPUT_PAD_COUNT; slot++)
	{
		s_controllers[slot].device = NativeControls_GetPlayerDevice(slot);
		NativeInput_ResetSnapshot(slot);

		if (s_controllers[slot].device != NATIVE_CONTROLS_NO_DEVICE)
		{
			s_controllers[slot].snapshot.connected = 1;
			s_controllers[slot].snapshot.status = 0;
			s_controllers[slot].snapshot.id = NATIVE_INPUT_PAD_ANALOG;
		}
	}

	u8 port0[NATIVE_INPUT_MULTITAP_HEADER + (NATIVE_INPUT_MAX_CONTROLLERS * NATIVE_INPUT_PAD_PACKET_BYTES)];
	u8 port1[NATIVE_INPUT_PAD_PACKET_BYTES];
	memset(port0, 0, sizeof(port0));
	memset(port1, 0, sizeof(port1));

	s_padSlotData[0] = port0;
	s_padSlotData[1] = port1;
	NativeInput_WritePadBus();
	int ports = GamePadBusView(port0, perPlayer);

	Check(ports == 4, "the bus stays in multitap with a hole in it");
	Check(perPlayer[0] == 1, "player 1 keeps the pad");
	Check(perPlayer[1] == 0, "the disabled player reports disconnected");
}

// Stand-in for SDL's keyboard array, so the real NativeInput_ReadKeyboard path runs
// without a window.
static bool s_testKeyboardState[SDL_SCANCODE_COUNT];

// Reads one pad's button word off the multitap bus. The byte order is the game's own,
// from GAMEPAD_ProcessHold.
static u16 PadBusButtons(u8 *port0, int player)
{
	struct TestPadSlot *slot = (struct TestPadSlot *)port0;
	struct ControllerPacket *packet = &slot->controllers[player];

	return (u16)((packet->controllerInput2 << 8) | packet->controllerInput1);
}

// Whether the game would see a raw button mask held on a pad. Buttons are active low, and
// gamepadMapBtn carries every raw bit in both byte positions, so both orders count.
static int PadBusButtonDown(u8 *port0, int player, u16 rawMask)
{
	u16 word = PadBusButtons(port0, player);
	u16 swapped = (u16)((word >> 8) | (word << 8));

	return ((word & rawMask) == 0) || ((swapped & rawMask) == 0);
}

static int PadBusConnected(u8 *port0, int player)
{
	struct TestPadSlot *slot = (struct TestPadSlot *)port0;

	return slot->controllers[player].plugged == PLUGGED;
}

// === Analog mode ===

// The id byte a pad's packet carries on the bus.
static u8 PadBusControllerData(u8 *port0, int player)
{
	struct TestPadSlot *slot = (struct TestPadSlot *)port0;

	return slot->controllers[player].controllerData;
}

// Mirrors GAMEPAD_ProcessSticks_IsAnalogLike, the predicate deciding whether the game reads
// a pad's analog bytes or discards them.
static int GameReadsAnalogAxes(u8 controllerData)
{
	return controllerData == ((PAD_ID_ANALOG_STICK << 4) | 3) || controllerData == ((PAD_ID_ANALOG << 4) | 3);
}

// Nothing distinguishes the keyboard on the bus. Its axes are unbound, so they read as the
// centre value the game takes for neutral, and the controls screen keeps the stick rows off
// it so there is nothing to misbind.
static void TestTheKeyboardReportsAPadLikeAnyOtherDevice(void)
{
	printf("analog: the keyboard reports a pad like any other device\n");
	ResetModule();
	ResetBusState();

	// Keyboard only: ApplyController dereferences a gamepad handle, so a frame cannot be
	// driven while the fake pad handles are in the table.
	int pads[4];
	int keyboard;
	DeclareDevicesForBus(0, pads, &keyboard);
	NativeControls_ResolveAssignment();

	u8 port0[NATIVE_INPUT_MULTITAP_HEADER + (NATIVE_INPUT_MAX_CONTROLLERS * NATIVE_INPUT_PAD_PACKET_BYTES)];
	u8 port1[NATIVE_INPUT_PAD_PACKET_BYTES];
	memset(port0, 0, sizeof(port0));
	memset(port1, 0, sizeof(port1));
	s_padSlotData[0] = port0;
	s_padSlotData[1] = port1;

	memset(s_testKeyboardState, 0, sizeof(s_testKeyboardState));
	s_keyboardState = s_testKeyboardState;
	s_inputInitialized = 1;

	NativeInput_SyncDevices();
	Platform_InputUpdate();

	Check(PadBusConnected(port0, 0), "the keyboard is connected");
	Check(PadBusControllerData(port0, 0) == NATIVE_INPUT_PAD_ANALOG, "the keyboard is reported as an analog pad");
	Check(GameReadsAnalogAxes(PadBusControllerData(port0, 0)), "the game reads the packet as a pad with axes");

	// Nothing is bound to the axes, so they must read as centre rather than as a
	// deflection the game would steer with.
	const struct ControllerPacket *packet = (const struct ControllerPacket *)&((struct TestPadSlot *)port0)->controllers[0];
	Check(packet->analog.leftX == 0x80, "the left X axis reads as centre");
	Check(packet->analog.leftY == 0x80, "the left Y axis reads as centre");
	Check(packet->analog.rightX == 0x80, "the right X axis reads as centre");
	Check(packet->analog.rightY == 0x80, "the right Y axis reads as centre");

	s_inputInitialized = 0;
	s_keyboardState = NULL;
}

// === Deadzone ===

// The deadzone is what replaces the old digital mode as the answer to a drifting stick, so
// what it has to guarantee is that a stick pushed to either end still reaches the end.
// Trimming without stretching back would quietly cap steering.
static void TestDeadzoneTrimsCentreAndKeepsFullTravel(void)
{
	printf("deadzone: trims the centre and still reaches full travel\n");

	Check(NativeInput_ApplyDeadzone(0, 0) == 0, "a centred stick is centred at 0%");
	Check(NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MAX, 0) == SDL_JOYSTICK_AXIS_MAX,
	      "a full-deflection stick is untouched at 0%");
	Check(NativeInput_ApplyDeadzone(-SDL_JOYSTICK_AXIS_MAX - 1, 0) == -SDL_JOYSTICK_AXIS_MAX - 1,
	      "the negative end is untouched at 0%");

	// A stick resting slightly off centre reads as centred.
	Check(NativeInput_ApplyDeadzone(500, 10) == 0, "a small positive drift is trimmed away");
	Check(NativeInput_ApplyDeadzone(-500, 10) == 0, "a small negative drift is trimmed away");
	Check(NativeInput_ApplyDeadzone(0, 10) == 0, "a centred stick stays centred");

	// Just past the deadzone the axis has to have moved, not still read as the raw value.
	Check(NativeInput_ApplyDeadzone(7000, 10) < 7000, "travel just past the deadzone is pulled toward centre");

	// Pushed all the way, either way, the packet byte must still reach its extreme, or
	// trimming the centre would quietly cost full throttle. Checked on the byte because
	// that is what the game reads, and it is the level the guarantee has to hold at.
	Check(NativeInput_AxisToByte(NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MAX, 25)) == 0xff,
	      "full positive travel still reaches the end");
	Check(NativeInput_AxisToByte(NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MIN, 25)) == 0x00,
	      "full negative travel still reaches the end");
	Check(NativeInput_AxisToByte(NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MAX, NATIVE_CONTROLS_DEADZONE_MAX)) == 0xff,
	      "the widest allowed deadzone still reaches the positive end");
	Check(NativeInput_AxisToByte(NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MIN, NATIVE_CONTROLS_DEADZONE_MAX)) == 0x00,
	      "the widest allowed deadzone still reaches the negative end");

	// Just past the deadzone the axis has to move the right way, not back toward centre.
	Check(NativeInput_ApplyDeadzone(9000, 25) > 0, "pushing right past the deadzone reads positive");
	Check(NativeInput_ApplyDeadzone(-9000, 25) < 0, "pushing left past the deadzone reads negative");

	// A deadzone wide enough to cover the whole range must trim everything and stop there.
	// The negative end of an SDL axis is one unit past the positive one, so it is the only
	// value that reaches the rescale with nothing left to stretch, which is what turned the
	// denominator into a division by zero.
	Check(NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MIN, 100) == 0, "a full-width deadzone trims the far negative end");
	Check(NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MAX, 100) == 0, "a full-width deadzone trims the far positive end");

	// The widest the menu allows still leaves travel, so nothing is trimmed away for good.
	Check(NativeInput_AxisToByte(NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MIN, NATIVE_CONTROLS_DEADZONE_MAX)) == 0x00,
	      "the widest allowed deadzone still reports the far negative end");

	// Nothing at or above the top of the range may divide by zero, whatever it is asked for.
	for (s32 percent = NATIVE_CONTROLS_DEADZONE_MAX; percent <= 100; percent++)
	{
		NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MIN, percent);
		NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MAX, percent);
	}

	Check(NATIVE_CONTROLS_DEADZONE_MAX < 100, "the maximum leaves some travel at each end");

	// Monotonic: more stick must never report less, or the steering would reverse.
	s32 previous = 0;
	for (s32 axis = 0; axis <= SDL_JOYSTICK_AXIS_MAX; axis += 512)
	{
		const s32 applied = NativeInput_ApplyDeadzone(axis, 20);
		Check(applied >= previous, "the trimmed axis never moves backwards");
		previous = applied;
	}
}

// Restoring defaults has to take the deadzone with it, or a pad the player reset would
// keep trimming input it no longer shows a figure for.
static void TestRestoreDefaultsClearsTheDeadzone(void)
{
	printf("deadzone: restoring defaults clears it\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	NativeControls_SetDeadzone(pads[0], 30);
	Check(NativeControls_GetDeadzone(pads[0]) == 30, "the deadzone takes");

	NativeControls_RestoreDeviceDefaults(pads[0]);
	Check(NativeControls_GetDeadzone(pads[0]) == NATIVE_CONTROLS_DEADZONE_DEFAULT, "the default is back");
}

// The value is clamped at both ends however it arrives, so a hand-edited file cannot
// produce a deadzone that trims the entire range.
static void TestDeadzoneIsClamped(void)
{
	printf("deadzone: values are clamped to the allowed range\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	NativeControls_SetDeadzone(pads[0], 5000);
	Check(NativeControls_GetDeadzone(pads[0]) == NATIVE_CONTROLS_DEADZONE_MAX, "too high clamps to the maximum");

	NativeControls_SetDeadzone(pads[0], -5000);
	Check(NativeControls_GetDeadzone(pads[0]) == NATIVE_CONTROLS_DEADZONE_MIN, "too low clamps to the minimum");

	Check(NativeControls_GetDeadzone(NATIVE_CONTROLS_NO_DEVICE) == NATIVE_CONTROLS_DEADZONE_DEFAULT,
	      "no device reads the default");
}

// A deadzone belongs to the stick it trims, so it is saved beside that device's bindings
// and must come back for the same device and no other.
static void TestDeadzoneRoundTripsPerDevice(void)
{
	printf("deadzone: saved per device and read back\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(2, pads, &keyboard);
	NativeControls_ResolveAssignment();

	NativeControls_SetDeadzone(pads[0], 15);
	NativeControls_SetDeadzone(pads[1], 40);
	Check(NativeControls_GetDeadzone(pads[0]) == 15, "the first pad keeps its own figure");

	// The menu's flush is what reaches the file.
	NativeControls_Save();

	ResetModule();
	NativeControls_Load();

	int reloadedPads[4];
	int reloadedKeyboard;
	SetupDevices(2, reloadedPads, &reloadedKeyboard);

	Check(NativeControls_GetDeadzone(reloadedPads[0]) == 15, "the first pad's figure survived");
	Check(NativeControls_GetDeadzone(reloadedPads[1]) == 40, "the second pad's figure survived");
	Check(NativeControls_GetDeadzone(reloadedKeyboard) == NATIVE_CONTROLS_DEADZONE_DEFAULT,
	      "the keyboard has no figure of its own");
}

// A hand-edited file cannot set a deadzone past the configured maximum, so the value the
// input layer is handed is always one it can actually rescale.
static void TestAHandEditedDeadzoneCannotExceedTheMaximum(void)
{
	printf("deadzone: a hand-edited figure past the maximum is clamped\n");

	char path[512];
	TestFilePath(path, sizeof(path));

	FILE *file = fopen(path, "w");
	fprintf(file, "[%s%stest-guid-0]\n", NATIVE_CONTROLS_SECTION_PREFIX, "");
	fprintf(file, "%s = 100\n", NATIVE_CONTROLS_KEY_DEADZONE);
	fclose(file);

	ResetModule();
	NativeControls_Load();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	Check(NativeControls_GetDeadzone(pads[0]) == NATIVE_CONTROLS_DEADZONE_MAX, "it clamps to the maximum");

	// And the clamped value is one the input layer can actually rescale.
	Check(NativeInput_AxisToByte(NativeInput_ApplyDeadzone(SDL_JOYSTICK_AXIS_MIN, NativeControls_GetDeadzone(pads[0]))) == 0x00,
	      "the clamped value keeps the far negative end rather than dividing by zero");
}

// An unreadable figure must leave the default rather than trim input nobody asked to trim.
static void TestAnUnreadableDeadzoneKeepsTheDefault(void)
{
	printf("deadzone: an unreadable figure keeps the default\n");

	char path[512];
	TestFilePath(path, sizeof(path));

	FILE *file = fopen(path, "w");
	fprintf(file, "[%s%stest-guid-0]\n", NATIVE_CONTROLS_SECTION_PREFIX, "");
	fprintf(file, "%s = not-a-number\n", NATIVE_CONTROLS_KEY_DEADZONE);
	fprintf(file, "%s = 25\n", NATIVE_CONTROLS_KEY_DEADZONE);
	fclose(file);

	ResetModule();
	NativeControls_Load();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	Check(NativeControls_GetDeadzone(pads[0]) == 25, "a good figure later in the file still applies");
}

// A pad with no figure at all must read as the console's behaviour rather than something
// the profile table happened to be zeroed to by accident.
static void TestDeadzoneDefaultsToNone(void)
{
	printf("deadzone: an unconfigured pad trims nothing\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	Check(NativeControls_GetDeadzone(pads[0]) == 0, "the default trims nothing");
	Check(NATIVE_CONTROLS_DEADZONE_DEFAULT == 0, "the default is the console's behaviour");
}

// The snapshot version in effect while the digital/analog toggle still existed. Its layout
// carried two extra fields per slot, so a snapshot from that build is bigger than the
// struct now and the size check in Platform_InputRestoreState does not catch it: the
// version guard is the only thing standing between the two layouts.
#define TEST_INPUT_STATE_VERSION_WITH_TOGGLE 2

static void TestAnOlderInputSnapshotIsRefused(void)
{
	printf("state: an input snapshot from before the toggle is refused\n");
	ResetBusState();

	Check(NATIVE_INPUT_STATE_VERSION != TEST_INPUT_STATE_VERSION_WITH_TOGGLE,
	      "the version was bumped past the layout that had the toggle fields");

	const int size = Platform_InputGetStateSize();
	u8 *buffer = calloc(1, (size_t)size);
	Check(buffer != NULL, "the snapshot buffer is allocated");

	if (buffer == NULL)
	{
		return;
	}

	// A successful restore writes the pad bus, so point it at this test's own buffers
	// rather than inheriting a previous test's, which is out of scope by now.
	u8 port0[NATIVE_INPUT_MULTITAP_HEADER + (NATIVE_INPUT_MAX_CONTROLLERS * NATIVE_INPUT_PAD_PACKET_BYTES)];
	u8 port1[NATIVE_INPUT_PAD_PACKET_BYTES];
	memset(port0, 0, sizeof(port0));
	memset(port1, 0, sizeof(port1));
	s_padSlotData[0] = port0;
	s_padSlotData[1] = port1;

	Check(Platform_InputCaptureState(buffer, size) == 1, "a snapshot is captured");

	struct NativeInputStateSnapshot *snapshot = (struct NativeInputStateSnapshot *)buffer;
	Check(snapshot->version == NATIVE_INPUT_STATE_VERSION, "the capture carries the current version");

	// Stands in for a snapshot read from a checkpoint written by that older build.
	snapshot->version = TEST_INPUT_STATE_VERSION_WITH_TOGGLE;
	Check(Platform_InputRestoreState(buffer, size) == 0, "an older snapshot is refused");

	// CTR Archipelago's layout before the remap port (version 1) carried the keyboard
	// slot and the slot-cycling state, which the controls module now owns.
	snapshot->version = 1;
	Check(Platform_InputRestoreState(buffer, size) == 0, "the pre-remap CTR-AP snapshot is refused");

	free(buffer);
}

// Regression: Alt was suppressed by skipping the keyboard's whole contribution in
// Platform_InputUpdate, which also skipped the pad's connection flag, so holding Alt
// made the game announce the player as unplugged.
static void TestAltKeepsTheKeyboardPadConnected(void)
{
	printf("keyboard: Alt suppresses buttons without dropping the pad\n");
	ResetBusState();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(0, pads, &keyboard);
	NativeControls_ResolveAssignment();

	u8 port0[NATIVE_INPUT_MULTITAP_HEADER + (NATIVE_INPUT_MAX_CONTROLLERS * NATIVE_INPUT_PAD_PACKET_BYTES)];
	u8 port1[NATIVE_INPUT_PAD_PACKET_BYTES];
	memset(port0, 0, sizeof(port0));
	memset(port1, 0, sizeof(port1));
	s_padSlotData[0] = port0;
	s_padSlotData[1] = port1;

	memset(s_testKeyboardState, 0, sizeof(s_testKeyboardState));
	s_keyboardState = s_testKeyboardState;

	// Drive the real frame function: the guard that dropped the pad used to live there.
	s_inputInitialized = 1;
	NativeInput_SyncDevices();

	Check(NativeControls_GetDeviceKind(s_controllers[0].device) == NATIVE_CONTROLS_DEVICE_KEYBOARD,
		"pad slot 0 follows the keyboard");

	const u16 crossMask = s_bindingButtonMask[NATIVE_CONTROLS_BIND_CROSS];

	Platform_InputUpdate();
	Check(PadBusConnected(port0, 0), "an idle keyboard is connected");
	Check(PadBusButtons(port0, 0) == 0xffff, "an idle keyboard reads no buttons");

	// A key on its own reaches the game.
	s_testKeyboardState[SDL_SCANCODE_C] = true;
	Platform_InputUpdate();
	Check(PadBusButtonDown(port0, 0, crossMask) == 1, "a keyboard cross press reaches the pad");

	// Alt belongs to the window manager: the button goes away, the pad does not.
	s_testKeyboardState[SDL_SCANCODE_LALT] = true;
	Platform_InputUpdate();
	Check(PadBusButtonDown(port0, 0, crossMask) == 0, "Alt suppresses the keyboard's buttons");
	Check(PadBusConnected(port0, 0), "the pad stays connected while Alt is held");

	s_inputInitialized = 0;
	s_keyboardState = NULL;
}

static void TestAutoAssignmentPutsFirstGamepadOnPlayerOne(void)
{
	printf("auto assignment: first gamepad on player 1\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(2, pads, &keyboard);

	NativeControls_ResolveAssignment();

	CheckDevice(0, TestPadName(0), "P1 takes the first gamepad");
	CheckDevice(1, TestPadName(1), "P2 takes the second gamepad");
	CheckDevice(2, TEST_KEYBOARD_NAME, "P3 takes the keyboard");
	CheckDevice(3, NULL, "P4 has no device");
}

static void TestKeyboardIsTheLastFallback(void)
{
	printf("auto assignment: keyboard is the last fallback\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(0, pads, &keyboard);

	NativeControls_ResolveAssignment();

	// No gamepads at all: the keyboard is the only device, so P1 must get it.
	CheckDevice(0, TEST_KEYBOARD_NAME, "P1 takes the only device, the keyboard");
	CheckDevice(1, NULL, "P2 has no device");
}

static void TestDisabledPlayerIsSkipped(void)
{
	printf("auto assignment: disabled players are skipped\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(3, pads, &keyboard);

	SetPref(1, NATIVE_CONTROLS_PREF_DISABLED, NULL);
	NativeControls_ResolveAssignment();

	CheckDevice(0, TestPadName(0), "P1 takes the first gamepad");
	CheckDevice(1, NULL, "P2 stays disabled");
	CheckDevice(2, TestPadName(1), "P3 takes the second gamepad, not P2's share");
	CheckDevice(3, TestPadName(2), "P4 takes the third gamepad");
}

static void TestPinnedPreferencesWinOverAuto(void)
{
	printf("pinned preferences: honoured and ordered by player\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(2, pads, &keyboard);

	// P2 pins the first pad, which P1 would otherwise take.
	SetPref(1, NATIVE_CONTROLS_PREF_PINNED, s_testDeviceKeys[0]);
	NativeControls_ResolveAssignment();

	CheckDevice(0, TestPadName(1), "P1 falls through to the second pad");
	CheckDevice(1, TestPadName(0), "P2 holds the pad it pinned");
}

static void TestConflictingPinsResolveToLowerPlayer(void)
{
	printf("pinned preferences: a conflict resolves to the lower player\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	SetPref(0, NATIVE_CONTROLS_PREF_PINNED, s_testDeviceKeys[0]);
	SetPref(1, NATIVE_CONTROLS_PREF_PINNED, s_testDeviceKeys[0]);
	NativeControls_ResolveAssignment();

	CheckDevice(0, TestPadName(0), "P1 keeps the contested pad");
	CheckDevice(1, NULL, "P2 gets nothing rather than stealing it");
}

static void TestSoftlockGuardMovesKeyboardToPlayerOne(void)
{
	printf("softlock: the keyboard moves to player 1\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(0, pads, &keyboard);

	// P2 pins the keyboard, which would leave P1 with nothing.
	SetPref(1, NATIVE_CONTROLS_PREF_PINNED, NATIVE_CONTROLS_KEYBOARD_KEY);
	NativeControls_ResolveAssignment();

	CheckDevice(0, TEST_KEYBOARD_NAME, "P1 takes the keyboard");
	CheckDevice(1, NULL, "P2 lost the keyboard");
	Check(s_playerPref[1].kind == NATIVE_CONTROLS_PREF_AUTO, "P2's pin was dropped so it cannot be stolen back");
}

static void TestHotplugGivesKeyboardToPlayerOne(void)
{
	printf("softlock: player 1's pad is unplugged mid-session\n");
	ResetModule();

	int pads[4];
	int keyboard;

	// One gamepad, so P1 takes it and the keyboard falls through to P2.
	SetupDevices(1, pads, &keyboard);

	NativeControls_ResolveAssignment();
	CheckDevice(0, TestPadName(0), "P1 starts on the gamepad");
	CheckDevice(1, TEST_KEYBOARD_NAME, "P2 starts on the keyboard");

	// The pad under P1 goes away: rebuild the table without it.
	SetupDevices(0, pads, &keyboard);
	NativeControls_ResolveAssignment();

	CheckDevice(0, TEST_KEYBOARD_NAME, "P1 falls back to the keyboard, taking it from P2");
	CheckDevice(1, NULL, "P2 ends up without a device");
}

static void TestReplugRestoresPinnedDevice(void)
{
	printf("hotplug: a pinned device is reclaimed when it returns\n");
	ResetModule();

	int pads[4];
	int keyboard;

	// Pin P1 to the second pad while only one pad is connected.
	SetupDevices(1, pads, &keyboard);
	SetPref(0, NATIVE_CONTROLS_PREF_PINNED, "test-guid-1");
	NativeControls_ResolveAssignment();

	CheckDevice(0, TEST_KEYBOARD_NAME, "P1 falls back to the keyboard while the pad is away");

	SetupDevices(2, pads, &keyboard);
	NativeControls_ResolveAssignment();

	CheckDevice(0, TestPadName(1), "P1 takes its pinned pad back on replug");
	CheckDevice(1, TestPadName(0), "the other pad went to P2");
	CheckDevice(2, TEST_KEYBOARD_NAME, "the keyboard stayed the last fallback");
}

static void TestAvailableDevicesHidesOtherPlayersDevices(void)
{
	printf("device picker: another player's device is not offered\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(2, pads, &keyboard);

	NativeControls_ResolveAssignment();

	int available[NATIVE_CONTROLS_MAX_DEVICES];
	int count = NativeControls_GetAvailableDevices(0, available, NATIVE_CONTROLS_MAX_DEVICES);
	Check(count == 1, "P1 sees only its own device");
	Check((count == 1) && (available[0] == pads[0]), "P1 sees its own device");

	// P4 owns nothing, but auto-assignment is greedy and always claims every device,
// so it is offered nothing rather than another player's pad.
	count = NativeControls_GetAvailableDevices(3, available, NATIVE_CONTROLS_MAX_DEVICES);
	Check(count == 0, "P4 is offered nothing while every device is taken");

	// Unplug P1's pad: P1 drops to the keyboard, P2 loses its device, and the pad is
	// gone entirely. Still nothing free for P4.
	SetupDevices(0, pads, &keyboard);
	NativeControls_ResolveAssignment();
	count = NativeControls_GetAvailableDevices(3, available, NATIVE_CONTROLS_MAX_DEVICES);
	Check(count == 0, "P4 is still offered nothing once only the keyboard remains");
}

static void TestPlayerOneCannotBeDisabled(void)
{
	printf("player 1 cannot be disabled\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);
	NativeControls_ResolveAssignment();

	Check(NativeControls_SetPlayerPref(0, NATIVE_CONTROLS_PREF_DISABLED, NATIVE_CONTROLS_NO_DEVICE) == 0,
	      "the request is refused");
	CheckDevice(0, TestPadName(0), "P1 keeps its device");
}

static void TestIniRoundTrip(void)
{
	printf("controls.ini: write then read back\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);
	NativeControls_ResolveAssignment();

	// Rebind cross on the keyboard and L2 on the pad, then pin P2 to the pad.
	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_CROSS, SDL_SCANCODE_SPACE);
	NativeControls_SetBinding(pads[0], NATIVE_CONTROLS_BIND_L2, SDL_GAMEPAD_BUTTON_DPAD_UP);
	NativeControls_SetBinding(pads[0], NATIVE_CONTROLS_BIND_AXIS_LEFT_Y,
	                          SDL_GAMEPAD_AXIS_RIGHTY | NATIVE_CONTROLS_BINDING_FLAG_AXIS);
	SetPref(1, NATIVE_CONTROLS_PREF_PINNED, s_testDeviceKeys[0]);
	NativeControls_Save();

	// Wipe everything and reload from disk.
	ResetModule();
	NativeControls_Load();

	int reloadedPads[4];
	int reloadedKeyboard;
	SetupDevices(1, reloadedPads, &reloadedKeyboard);

	Check(NativeControls_GetBinding(reloadedKeyboard, NATIVE_CONTROLS_BIND_CROSS) == SDL_SCANCODE_SPACE,
	      "keyboard cross came back");
	Check(NativeControls_GetBinding(reloadedPads[0], NATIVE_CONTROLS_BIND_L2) == SDL_GAMEPAD_BUTTON_DPAD_UP,
	      "gamepad L2 came back as a button");
	Check(NativeControls_GetBinding(reloadedPads[0], NATIVE_CONTROLS_BIND_AXIS_LEFT_Y) ==
	          (SDL_GAMEPAD_AXIS_RIGHTY | NATIVE_CONTROLS_BINDING_FLAG_AXIS),
	      "gamepad axis came back with its axis flag");

	NativeControls_ResolveAssignment();
	Check(s_playerPref[1].kind == NATIVE_CONTROLS_PREF_PINNED, "P2's pin survived the round trip");
	Check(strcmp(s_playerPref[1].deviceKey, "test-guid-0") == 0, "P2's pin names the same device");
}

// SDL names a printable key by the character it types. The retail font repurposes four
// characters as button artwork and has no glyph for several more, so a display name taken
// straight from SDL reads as the wrong icon or as nothing at all. The file keeps SDL's
// name; only the label shown on screen is spelled out.
static void TestDisplayTextAvoidsTheFontCollisions(void)
{
	printf("labels: keys the font cannot show are spelled out\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	// These are the default keyboard bindings that used to render badly: L3 is "[",
	// which DecalFont draws as the square button.
	Check(NativeControls_StrEqual(SDL_GetScancodeName(SDL_SCANCODE_LEFTBRACKET), "["),
	      "SDL still names the left bracket key with the raw character");
	Check(NativeControls_StrEqual(NativeControls_ScancodeName(SDL_SCANCODE_LEFTBRACKET), "Left Bracket"),
	      "the display name spells the left bracket out");

	const SDL_Scancode spellouts[] = {
	    SDL_SCANCODE_LEFTBRACKET,  SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_BACKSLASH,
	    SDL_SCANCODE_NONUSHASH,    SDL_SCANCODE_SEMICOLON,    SDL_SCANCODE_GRAVE,
	};

	for (unsigned int i = 0; i < SDL_arraysize(spellouts); i++)
	{
		const char *display = NativeControls_ScancodeName(spellouts[i]);
		bool clean = (display != NULL) && (display[0] != '\0');

		for (const char *c = display; clean && (*c != '\0'); c++)
		{
			// Anything from the reserved button set, or a character the font has no
			// glyph for, would render as an icon or a gap.
			if ((*c == '@') || (*c == '[') || (*c == '^') || (*c == '*') || (*c == ']') || (*c == '#') || (*c == ';') ||
			    (*c == '\\') || (*c == '`'))
			{
				clean = false;
			}
		}

		Check(clean, "no display name contains a character the font cannot show");
	}

	// A binding on the keyboard reaches the label through the same path.
	char label[64];
	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_L3, SDL_SCANCODE_LEFTBRACKET);
	NativeControls_GetBindingLabel(keyboard, NATIVE_CONTROLS_BIND_L3, label, sizeof(label));
	Check(NativeControls_StrEqual(label, "Left Bracket"), "the L3 row shows a name, not a square icon");

	// The file must keep the character, or the value would not survive a reload.
	char value[64];
	NativeControls_BindingToText(NATIVE_CONTROLS_DEVICE_KEYBOARD, SDL_SCANCODE_LEFTBRACKET, value, sizeof(value),
	                             NATIVE_CONTROLS_NAME_FILE);
	Check(NativeControls_StrEqual(value, "["), "the file still stores the raw scancode name");
}

static void TestIniDefaultsAreRestored(void)
{
	printf("controls.ini: unedited defaults come back unchanged\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(0, pads, &keyboard);
	NativeControls_ResolveAssignment();
	NativeControls_Save();

	ResetModule();
	NativeControls_Load();

	int reloadedPads[4];
	int reloadedKeyboard;
	SetupDevices(0, reloadedPads, &reloadedKeyboard);

	Check(NativeControls_GetBinding(reloadedKeyboard, NATIVE_CONTROLS_BIND_CROSS) == SDL_SCANCODE_C,
	      "keyboard cross is back to the default");
	Check(NativeControls_GetBinding(reloadedKeyboard, NATIVE_CONTROLS_BIND_L2) == SDL_SCANCODE_LCTRL,
	      "keyboard L2 is back to the default");
	Check(NativeControls_GetBinding(reloadedKeyboard, NATIVE_CONTROLS_BIND_AXIS_LEFT_X) == NATIVE_CONTROLS_BINDING_NONE,
	      "keyboard axes stay unbound");
}

static void TestRestoreDeviceDefaults(void)
{
	printf("restore defaults: resets one device only\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);
	NativeControls_ResolveAssignment();

	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_CROSS, SDL_SCANCODE_SPACE);
	NativeControls_SetBinding(pads[0], NATIVE_CONTROLS_BIND_CROSS, SDL_GAMEPAD_BUTTON_DPAD_DOWN);

	NativeControls_RestoreDeviceDefaults(keyboard);

	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CROSS) == SDL_SCANCODE_C, "keyboard was reset");
	Check(NativeControls_GetBinding(pads[0], NATIVE_CONTROLS_BIND_CROSS) == SDL_GAMEPAD_BUTTON_DPAD_DOWN,
	      "the gamepad was left alone");
}

static void TestUnknownNamesKeepDefaults(void)
{
	printf("controls.ini: an unreadable value keeps its default\n");

	char path[512];
	TestFilePath(path, sizeof(path));

	FILE *file = fopen(path, "w");
	fprintf(file, "[%s%s]\n", NATIVE_CONTROLS_SECTION_PREFIX, NATIVE_CONTROLS_KEYBOARD_KEY);
	fprintf(file, "cross = not-a-real-key\n");
	fprintf(file, "circle = V\n");
	fprintf(file, "[%stest-guid-0]\n", NATIVE_CONTROLS_SECTION_PREFIX);
	fprintf(file, "l2 = ~lefty\n");
	fclose(file);

	ResetModule();
	NativeControls_Load();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CROSS) == SDL_SCANCODE_C,
	      "a bad key name falls back to the default");
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CIRCLE) == SDL_SCANCODE_V,
	      "a good key name in the same file still applies");

	// An axis inversion marker from an older file is now just an unrecognised name, so
	// it falls back like any other rather than quietly doing nothing.
	Check(NativeControls_GetBinding(pads[0], NATIVE_CONTROLS_BIND_L2) ==
	          (SDL_GAMEPAD_AXIS_LEFT_TRIGGER | NATIVE_CONTROLS_BINDING_FLAG_AXIS),
	      "an axis inversion marker no longer names a control, so the default stands");
}

static void TestFileRefusesToDisablePlayerOne(void)
{
	printf("controls.ini: player1 = disabled is refused\n");

	char path[512];
	TestFilePath(path, sizeof(path));

	FILE *file = fopen(path, "w");
	fprintf(file, "[%s]\n", NATIVE_CONTROLS_SECTION_ASSIGNMENT);
	fprintf(file, "player1 = disabled\n");
	fprintf(file, "player2 = disabled\n");
	fclose(file);

	ResetModule();
	NativeControls_Load();

	Check(s_playerPref[0].kind == NATIVE_CONTROLS_PREF_AUTO, "player1 was left on auto");
	Check(s_playerPref[1].kind == NATIVE_CONTROLS_PREF_DISABLED, "player2 was disabled as asked");
}

#if defined(CTR_AP)
// The AP connection manager commits and cancels text entry from physical pads only,
// because typing on the keyboard reads as pad buttons. With the keyboard now a device
// like any other, its presses must still never reach that mask.
static void TestKeyboardNeverReachesTheRawGamepadMask(void)
{
	printf("ap: keyboard presses stay out of the raw gamepad mask\n");
	ResetModule();
	ResetBusState();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(0, pads, &keyboard);
	NativeControls_ResolveAssignment();

	u8 port0[NATIVE_INPUT_MULTITAP_HEADER + (NATIVE_INPUT_MAX_CONTROLLERS * NATIVE_INPUT_PAD_PACKET_BYTES)];
	u8 port1[NATIVE_INPUT_PAD_PACKET_BYTES];
	memset(port0, 0, sizeof(port0));
	memset(port1, 0, sizeof(port1));
	s_padSlotData[0] = port0;
	s_padSlotData[1] = port1;

	memset(s_testKeyboardState, 0, sizeof(s_testKeyboardState));
	s_keyboardState = s_testKeyboardState;
	s_inputInitialized = 1;
	NativeInput_SyncDevices();

	// Cross, Triangle and Start on the default keyboard layout: the three the
	// connection manager listens for.
	s_testKeyboardState[SDL_SCANCODE_C] = true;
	s_testKeyboardState[SDL_SCANCODE_Z] = true;
	s_testKeyboardState[SDL_SCANCODE_RETURN] = true;
	Platform_InputUpdate();

	Check(PadBusButtonDown(port0, 0, s_bindingButtonMask[NATIVE_CONTROLS_BIND_CROSS]) == 1,
	      "the keyboard drives player 1's pad");
	Check(Platform_InputRawGamepadButtons() == 0, "the raw gamepad mask stays empty while typing");

	s_inputInitialized = 0;
	s_keyboardState = NULL;
}

// AP hotkeys read physical keys. A rebind moves a game control, never a hotkey.
static void TestRawKeyProbeIgnoresBindings(void)
{
	printf("ap: the raw key probe ignores the Controls bindings\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(0, pads, &keyboard);

	memset(s_testKeyboardState, 0, sizeof(s_testKeyboardState));
	s_keyboardState = s_testKeyboardState;

	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_CROSS, SDL_SCANCODE_M);
	s_testKeyboardState[SDL_SCANCODE_M] = true;
	Check(Platform_InputRawKeyDown(SDL_SCANCODE_M) == 1, "a rebound key still reads as itself");
	Check(Platform_InputRawKeyDown(SDL_SCANCODE_C) == 0, "the old key does not read as held");
	Check(Platform_InputRawKeyDown(-1) == 0, "an out-of-range scancode reads as up");
	Check(Platform_InputRawKeyDown(SDL_SCANCODE_COUNT) == 0, "the scancode count reads as up");

	s_keyboardState = NULL;
}

// === AP shortcuts ===

// The keys the shortcuts were hardcoded to before they became bindable. An install that
// never opens the page must keep every one of them.
static void TestShortcutDefaultsAreTheOldKeys(void)
{
	printf("shortcuts: defaults are the keys the AP build always used\n");
	ResetModule();

	static const struct
	{
		int shortcut;
		s32 scancode;
	} expected[] = {
	    {NATIVE_SHORTCUT_CHARACTER_PICKER, 97},     // Numpad 9
	    {NATIVE_SHORTCUT_DEV_PROGRESSIVE_MODE, 98}, // Numpad 0
	    {NATIVE_SHORTCUT_DEV_EDITABLE_MODE, 99},    // Numpad .
	    {NATIVE_SHORTCUT_DEV_TRAP_ICY, 89},         // Numpad 1
	    {NATIVE_SHORTCUT_DEV_TRAP_LOW_GRAVITY, 90}, // Numpad 2
	    {NATIVE_SHORTCUT_DEV_TRAP_NO_BRAKES, 91},   // Numpad 3
	    {NATIVE_SHORTCUT_DEV_TRAP_BOOST, 92},       // Numpad 4
	    {NATIVE_SHORTCUT_DEV_TRAP_FIRST_PERSON, 93},// Numpad 5
	    {NATIVE_SHORTCUT_DEV_TRAP_ALL, 94},         // Numpad 6
	    {NATIVE_SHORTCUT_DEV_SHORTCUTLESS, 95},     // Numpad 7
	    {NATIVE_SHORTCUT_DEV_MARK_QUAD, 96},        // Numpad 8
	    {NATIVE_SHORTCUT_DEV_DEMO_CAMERA, 87},      // Numpad +
	};

	Check(sizeof(expected) / sizeof(expected[0]) == NATIVE_SHORTCUT_COUNT, "every shortcut has an expected key");

	for (unsigned int i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
	{
		Check(NativeControls_GetShortcutKey(expected[i].shortcut) == expected[i].scancode,
		      NativeControls_GetShortcutName(expected[i].shortcut));
	}

	// The names the file uses (and default_controls.ini lists) read back as the same key.
	for (int shortcut = 0; shortcut < NATIVE_SHORTCUT_COUNT; shortcut++)
	{
		char name[64];
		NativeControls_BindingToText(NATIVE_CONTROLS_DEVICE_KEYBOARD, s_shortcutInfo[shortcut].defaultKey, name,
		                             sizeof(name), NATIVE_CONTROLS_NAME_FILE);
		Check((s32)SDL_GetScancodeFromName(name) == s_shortcutInfo[shortcut].defaultKey,
		      "a default shortcut key survives the file round trip");
	}

	// None of them may sit on a default keyboard game control, or the defaults would
	// break the one-key-one-action rule before anyone touched them.
	for (int shortcut = 0; shortcut < NATIVE_SHORTCUT_COUNT; shortcut++)
	{
		for (int binding = 0; binding < NATIVE_CONTROLS_BIND_COUNT; binding++)
		{
			Check(s_defaultKeyboard[binding] != NativeControls_GetShortcutKey(shortcut),
			      "a shortcut default is not also a keyboard game default");
		}
	}
}

static void TestShortcutFollowsItsRebind(void)
{
	printf("shortcuts: a rebound shortcut fires on its new key only\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(0, pads, &keyboard);

	memset(s_testKeyboardState, 0, sizeof(s_testKeyboardState));
	s_keyboardState = s_testKeyboardState;

	s_testKeyboardState[SDL_SCANCODE_KP_9] = true;
	Check(Platform_InputShortcutDown(NATIVE_SHORTCUT_CHARACTER_PICKER) == 1, "the default key fires the picker");

	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER, SDL_SCANCODE_F);
	Check(Platform_InputShortcutDown(NATIVE_SHORTCUT_CHARACTER_PICKER) == 0, "the old key no longer fires it");
	Check(Platform_InputRawKeyDown(SDL_SCANCODE_KP_9) == 1, "the raw probe still reads the physical key");

	s_testKeyboardState[SDL_SCANCODE_KP_9] = false;
	s_testKeyboardState[SDL_SCANCODE_F] = true;
	Check(Platform_InputShortcutDown(NATIVE_SHORTCUT_CHARACTER_PICKER) == 1, "the new key fires it");

	NativeControls_SetShortcutKey(-2, SDL_SCANCODE_G);
	for (int shortcut = 0; shortcut < NATIVE_SHORTCUT_COUNT; shortcut++)
	{
		Check(NativeControls_GetShortcutKey(shortcut) != SDL_SCANCODE_G, "an invalid shortcut index changes nothing");
	}
	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER, NATIVE_CONTROLS_BINDING_NONE);
	Check(Platform_InputShortcutDown(NATIVE_SHORTCUT_CHARACTER_PICKER) == 0, "an unbound shortcut never fires");
	Check(Platform_InputShortcutDown(-1) == 0, "an out-of-range shortcut never fires");
	Check(Platform_InputShortcutDown(NATIVE_SHORTCUT_COUNT) == 0, "the shortcut count never fires");

	s_keyboardState = NULL;
}

// Most recent wins, in all three directions the rule covers.
static void TestTheNewerBindingClearsTheOlder(void)
{
	printf("shortcuts: one key never does two things\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	// A shortcut takes a key from a keyboard game control.
	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER, SDL_SCANCODE_V);
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER) == SDL_SCANCODE_V, "the shortcut has V");
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CIRCLE) == NATIVE_CONTROLS_BINDING_NONE,
	      "Circle lost V and reads as unbound");

	// A keyboard game control takes a key from a shortcut.
	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_SQUARE, SDL_SCANCODE_KP_1);
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_SQUARE) == SDL_SCANCODE_KP_1, "Square has Numpad 1");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_ICY) == NATIVE_CONTROLS_BINDING_NONE,
	      "the icy trap key lost Numpad 1");

	// A shortcut takes a key from another shortcut.
	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_BOOST, SDL_SCANCODE_KP_7);
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_SHORTCUTLESS) == NATIVE_CONTROLS_BINDING_NONE,
	      "Shortcutless lost Numpad 7");

	// A gamepad binding is a different device and never touches the keyboard keys.
	NativeControls_SetBinding(pads[0], NATIVE_CONTROLS_BIND_CROSS, SDL_SCANCODE_KP_8);
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_MARK_QUAD) == SDL_SCANCODE_KP_8,
	      "a gamepad value that equals a scancode is not a conflict");

	// Two game controls may still share a key, as they always could.
	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_L3, SDL_SCANCODE_X);
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_L3) == SDL_SCANCODE_X, "L3 has X");
}

// The rebind screen names what a confirm would clear, and the confirm clears it.
static void TestShortcutCaptureWarnsAndReplaces(void)
{
	printf("shortcuts: the capture warns about a taken key and the confirm clears it\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(1, pads, &keyboard);

	char holder[64];

	NativeControls_BeginShortcutCapture(NATIVE_SHORTCUT_CHARACTER_PICKER);
	Check(NativeControls_IsCapturing(), "the shortcut capture is pending");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER) == NATIVE_CONTROLS_BINDING_NONE,
	      "the shortcut reads unbound while listening");
	Check(NativeControls_GetCaptureConflict(holder, sizeof(holder)) == 0, "no warning before a key is chosen");

	// Stand in for V being pressed and released.
	s_capturePendingValue = SDL_SCANCODE_V;
	s_shortcutKeys[NATIVE_SHORTCUT_CHARACTER_PICKER] = SDL_SCANCODE_V;
	s_capturePhase = NATIVE_CONTROLS_CAPTURE_CONFIRM;

	Check(NativeControls_GetCaptureConflict(holder, sizeof(holder)) == 1, "a taken key is reported");
	Check(NativeControls_StrEqual(holder, "Circle"), "the warning names Circle");
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CIRCLE) == SDL_SCANCODE_V,
	      "nothing is cleared before the confirm");

	NativeControls_CommitCapture();
	Check(!NativeControls_IsCapturing(), "the capture ended");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER) == SDL_SCANCODE_V, "the picker has V");
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CIRCLE) == NATIVE_CONTROLS_BINDING_NONE,
	      "Circle was cleared by the newer binding");

	// A keyboard game-control capture warns about a shortcut the same way.
	NativeControls_BeginCapture(keyboard, NATIVE_CONTROLS_BIND_SQUARE);
	s_capturePendingValue = SDL_SCANCODE_KP_2;
	NativeControls_BindingsOfDevice(keyboard)[NATIVE_CONTROLS_BIND_SQUARE] = SDL_SCANCODE_KP_2;
	s_capturePhase = NATIVE_CONTROLS_CAPTURE_CONFIRM;
	Check(NativeControls_GetCaptureConflict(holder, sizeof(holder)) == 1, "a key a shortcut holds is reported");
	Check(NativeControls_StrEqual(holder, "Dev Trap Low Gravity"), "the warning names the shortcut");
	NativeControls_CommitCapture();
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_LOW_GRAVITY) == NATIVE_CONTROLS_BINDING_NONE,
	      "the shortcut was cleared by the newer binding");

	// A cancelled shortcut capture puts the old key back and clears nothing.
	NativeControls_BeginShortcutCapture(NATIVE_SHORTCUT_DEV_TRAP_ICY);
	s_capturePendingValue = SDL_SCANCODE_Z;
	s_shortcutKeys[NATIVE_SHORTCUT_DEV_TRAP_ICY] = SDL_SCANCODE_Z;
	s_capturePhase = NATIVE_CONTROLS_CAPTURE_CONFIRM;
	NativeControls_CancelCapture();
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_ICY) == SDL_SCANCODE_KP_1, "the old key came back");
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_TRIANGLE) == SDL_SCANCODE_Z,
	      "Triangle kept its key");
}

static void TestRestoreDefaultsRestoresBoth(void)
{
	printf("shortcuts: Restore Defaults puts shortcuts and keyboard controls back together\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER, SDL_SCANCODE_V);
	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_SQUARE, SDL_SCANCODE_KP_1);
	NativeControls_SetBinding(pads[0], NATIVE_CONTROLS_BIND_CROSS, SDL_GAMEPAD_BUTTON_DPAD_DOWN);

	NativeControls_RestoreShortcutDefaults();
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER) == SDL_SCANCODE_KP_9, "the picker is back");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_ICY) == SDL_SCANCODE_KP_1, "the icy key is back");
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CIRCLE) == SDL_SCANCODE_V, "Circle is back");
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_SQUARE) == SDL_SCANCODE_X, "Square is back");
	Check(NativeControls_GetBinding(pads[0], NATIVE_CONTROLS_BIND_CROSS) == SDL_GAMEPAD_BUTTON_DPAD_DOWN,
	      "the gamepad was left alone");

	// The keyboard's own Restore Defaults brings the shortcuts back too.
	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER, SDL_SCANCODE_V);
	NativeControls_RestoreDeviceDefaults(keyboard);
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER) == SDL_SCANCODE_KP_9,
	      "the keyboard restore brings the picker back");
	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CIRCLE) == SDL_SCANCODE_V, "and Circle");

	// A gamepad restore leaves the shortcuts alone.
	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER, SDL_SCANCODE_F);
	NativeControls_RestoreDeviceDefaults(pads[0]);
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER) == SDL_SCANCODE_F,
	      "a gamepad restore keeps the shortcuts");
}

static bool ListHas(const int *list, int count, int shortcut)
{
	for (int i = 0; i < count; i++)
	{
		if (list[i] == shortcut)
		{
			return true;
		}
	}
	return false;
}

static void TestDevRowsFollowDevKeys(void)
{
	printf("shortcuts: Dev rows are listed only with dev_keys=1, the demo camera also with debug_democam=1\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);

	int list[NATIVE_SHORTCUT_COUNT];
	const int devRows[] = {
	    NATIVE_SHORTCUT_DEV_TRAP_ICY,        NATIVE_SHORTCUT_DEV_TRAP_LOW_GRAVITY, NATIVE_SHORTCUT_DEV_TRAP_NO_BRAKES,
	    NATIVE_SHORTCUT_DEV_TRAP_BOOST,      NATIVE_SHORTCUT_DEV_TRAP_FIRST_PERSON, NATIVE_SHORTCUT_DEV_TRAP_ALL,
	    NATIVE_SHORTCUT_DEV_SHORTCUTLESS,    NATIVE_SHORTCUT_DEV_MARK_QUAD,         NATIVE_SHORTCUT_DEV_DEMO_CAMERA,
	};
	const int devCount = (int)(sizeof(devRows) / sizeof(devRows[0]));

	// dev_keys off: the player rows stay, every Dev trap, Shortcutless and camera row goes.
	NativeControls_SetDevShortcutsVisible(0, 0);
	int count = NativeControls_ListVisibleShortcuts(list, NATIVE_SHORTCUT_COUNT);
	Check(count == NATIVE_SHORTCUT_COUNT - devCount, "dev_keys off lists only the non-dev rows");
	Check(ListHas(list, count, NATIVE_SHORTCUT_CHARACTER_PICKER), "the character picker is listed");
	for (int i = 0; i < devCount; i++)
	{
		Check(!ListHas(list, count, devRows[i]), "a dev row is hidden with dev_keys off");
	}

	// debug_democam alone changes nothing without dev_keys.
	NativeControls_SetDevShortcutsVisible(0, 1);
	Check(NativeControls_ListVisibleShortcuts(list, NATIVE_SHORTCUT_COUNT) == count, "democam alone shows nothing");

	// dev_keys on: all but the camera.
	NativeControls_SetDevShortcutsVisible(1, 0);
	count = NativeControls_ListVisibleShortcuts(list, NATIVE_SHORTCUT_COUNT);
	Check(count == NATIVE_SHORTCUT_COUNT - 1, "dev_keys on lists everything but the camera");
	Check(!ListHas(list, count, NATIVE_SHORTCUT_DEV_DEMO_CAMERA), "the camera row needs debug_democam");

	// both on: everything, in enum order, each index once.
	NativeControls_SetDevShortcutsVisible(1, 1);
	count = NativeControls_ListVisibleShortcuts(list, NATIVE_SHORTCUT_COUNT);
	Check(count == NATIVE_SHORTCUT_COUNT, "both on lists every shortcut");
	for (int i = 0; i < count; i++)
	{
		Check(list[i] == i, "rows come in enum order");
	}

	// The list is the menu's packed row list, so a cursor stepping through it never
	// meets a hidden row. A small max truncates instead of overflowing.
	NativeControls_SetDevShortcutsVisible(0, 0);
	count = NativeControls_ListVisibleShortcuts(list, NATIVE_SHORTCUT_COUNT);
	for (int i = 0; i < count; i++)
	{
		Check(NativeControls_ShortcutVisible(list[i]) == 1, "every listed row is visible");
	}
	Check(NativeControls_ListVisibleShortcuts(list, 2) == 2, "the list honours max");
	Check(NativeControls_ShortcutVisible(-1) == 0, "an invalid index is not visible");
	Check(NativeControls_ShortcutVisible(NATIVE_SHORTCUT_COUNT) == 0, "the count is not visible");
}

static void TestHiddenDevRowsStillTakePartInConflicts(void)
{
	printf("shortcuts: hidden Dev rows keep keys, are cleared by conflicts and are restored\n");
	ResetModule();

	int pads[4];
	int keyboard;
	SetupDevices(1, pads, &keyboard);
	NativeControls_SetDevShortcutsVisible(0, 0);

	// Hidden, but the key is still there.
	Check(NativeControls_ShortcutVisible(NATIVE_SHORTCUT_DEV_TRAP_ICY) == 0, "Dev Trap Icy is hidden");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_ICY) == SDL_SCANCODE_KP_1, "it keeps Numpad 1");

	// A game control taking the key clears the hidden shortcut.
	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_SQUARE, SDL_SCANCODE_KP_1);
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_ICY) == NATIVE_CONTROLS_BINDING_NONE,
	      "a game control clears a hidden dev key");

	// A visible shortcut taking a hidden one's key clears it too.
	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER, SDL_SCANCODE_KP_7);
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_SHORTCUTLESS) == NATIVE_CONTROLS_BINDING_NONE,
	      "a visible shortcut clears a hidden dev key");

	// Restore Defaults brings hidden rows back as well, whatever the visibility.
	NativeControls_RestoreShortcutDefaults();
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_ICY) == SDL_SCANCODE_KP_1, "hidden icy key restored");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_SHORTCUTLESS) == SDL_SCANCODE_KP_7,
	      "hidden Shortcutless key restored");

	// Same outcome with dev_keys on: visibility never changes the rules.
	NativeControls_SetDevShortcutsVisible(1, 1);
	NativeControls_SetBinding(keyboard, NATIVE_CONTROLS_BIND_SQUARE, SDL_SCANCODE_KP_2);
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_LOW_GRAVITY) == NATIVE_CONTROLS_BINDING_NONE,
	      "a game control clears a visible dev key too");
	NativeControls_RestoreShortcutDefaults();
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_LOW_GRAVITY) == SDL_SCANCODE_KP_2,
	      "and Restore Defaults brings it back");
}

static void TestShortcutsRoundTripAndOldFilesLoad(void)
{
	printf("shortcuts: controls.ini round trip, and a file without the section\n");

	char path[512];
	TestFilePath(path, sizeof(path));

	// A file written before the shortcuts were bindable.
	FILE *file = fopen(path, "w");
	fprintf(file, "[%s]\nplayer1 = auto\n", NATIVE_CONTROLS_SECTION_ASSIGNMENT);
	fprintf(file, "[%s%s]\nname = Keyboard\ncross = C\ncircle = B\n", NATIVE_CONTROLS_SECTION_PREFIX,
	        NATIVE_CONTROLS_KEYBOARD_KEY);
	fclose(file);

	ResetModule();
	NativeControls_Load();

	int pads[4];
	int keyboard;
	SetupDevices(0, pads, &keyboard);

	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CIRCLE) == SDL_SCANCODE_B, "the old file applied");
	for (int shortcut = 0; shortcut < NATIVE_SHORTCUT_COUNT; shortcut++)
	{
		Check(NativeControls_GetShortcutKey(shortcut) == s_shortcutInfo[shortcut].defaultKey,
		      "a file without the section leaves every shortcut on its default");
	}

	// The section is never mistaken for a device.
	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER, SDL_SCANCODE_F);
	NativeControls_SetShortcutKey(NATIVE_SHORTCUT_DEV_DEMO_CAMERA, NATIVE_CONTROLS_BINDING_NONE);

	ResetModule();
	NativeControls_Load();
	SetupDevices(0, pads, &keyboard);

	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER) == SDL_SCANCODE_F, "the picker came back");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_DEMO_CAMERA) == NATIVE_CONTROLS_BINDING_NONE,
	      "an unbound shortcut came back unbound");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_ALL) == SDL_SCANCODE_KP_6,
	      "an untouched shortcut came back on its default");

	int shortcutProfiles = 0;
	for (int i = 0; i < NATIVE_CONTROLS_MAX_PROFILES; i++)
	{
		if (s_profiles[i].used && (strstr(s_profiles[i].key, "Shortcuts") != NULL))
		{
			shortcutProfiles++;
		}
	}
	Check(shortcutProfiles == 0, "the shortcuts section did not become a device profile");

	// A hand edit that puts one key on a game control and a shortcut keeps the game
	// control, and an unreadable key keeps the default.
	file = fopen(path, "w");
	fprintf(file, "[%s%s]\ncross = Keypad 9\n", NATIVE_CONTROLS_SECTION_PREFIX, NATIVE_CONTROLS_KEYBOARD_KEY);
	fprintf(file, "[%s]\ncharacter_picker = Keypad 9\ndev_trap_icy = not-a-real-key\n",
	        NATIVE_CONTROLS_SECTION_SHORTCUTS);
	fprintf(file, "dev_trap_boost = Keypad 3\n");
	fclose(file);

	ResetModule();
	NativeControls_Load();
	SetupDevices(0, pads, &keyboard);

	Check(NativeControls_GetBinding(keyboard, NATIVE_CONTROLS_BIND_CROSS) == SDL_SCANCODE_KP_9, "Cross kept Numpad 9");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_CHARACTER_PICKER) == NATIVE_CONTROLS_BINDING_NONE,
	      "the picker sharing it was unbound");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_ICY) == SDL_SCANCODE_KP_1,
	      "an unreadable key kept the default");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_NO_BRAKES) == SDL_SCANCODE_KP_3,
	      "of two shortcuts on one key the first in list order keeps it");
	Check(NativeControls_GetShortcutKey(NATIVE_SHORTCUT_DEV_TRAP_BOOST) == NATIVE_CONTROLS_BINDING_NONE,
	      "and the later one is unbound");
}

// Typing on the Connection page and listening for a rebind must never fire a shortcut,
// while the raw probe the connection manager and the Track Manager use keeps reading
// the physical keys.
static void TestShortcutsStayQuietWhileTypingOrRebinding(void)
{
	printf("shortcuts: quiet during text entry and rebinds; the raw probe is unchanged\n");
	ResetModule();

	int pads[4];
	int keyboard;
	DeclareDevicesForBus(0, pads, &keyboard);

	memset(s_testKeyboardState, 0, sizeof(s_testKeyboardState));
	s_keyboardState = s_testKeyboardState;
	s_testKeyboardState[SDL_SCANCODE_KP_7] = true;

	Check(Platform_InputShortcutDown(NATIVE_SHORTCUT_DEV_SHORTCUTLESS) == 1, "the key fires with no text field open");

	s_testTextActive = 1;
	Check(Platform_InputShortcutDown(NATIVE_SHORTCUT_DEV_SHORTCUTLESS) == 0, "typing in a text field fires nothing");
	Check(Platform_InputRawKeyDown(SDL_SCANCODE_KP_7) == 1, "the raw probe still reads the key while typing");
	Check(Platform_InputRawGamepadButtons() == 0, "the raw gamepad mask is untouched by typing");
	s_testTextActive = 0;

	NativeControls_BeginCapture(keyboard, NATIVE_CONTROLS_BIND_CIRCLE);
	Check(Platform_InputShortcutDown(NATIVE_SHORTCUT_DEV_SHORTCUTLESS) == 0, "a listening rebind fires nothing");
	NativeControls_CancelCapture();
	Check(Platform_InputShortcutDown(NATIVE_SHORTCUT_DEV_SHORTCUTLESS) == 1, "the key works again afterwards");

	s_keyboardState = NULL;
}
#endif

int main(void)
{
	// NativeControls_Refresh queries the real gamepad list, so the subsystem has to
	// be up for the refresh tests to mean anything.
	SDL_Init(SDL_INIT_GAMEPAD);

	// Before anything can write the file, and regardless of the directory this was
	// started in.
	TestUseScratchDir();

	printf("native_controls tests\n");

	TestCaptureTimeoutRestoresTheBinding();
	TestCancelRestoresTheBinding();
	TestCommittedCaptureSurvivesACancel();
	TestChosenKeyWaitsForConfirmation();
	TestUnconfirmedProposalIsRevertedOnTimeout();
	TestConfirmUsesTheCurrentCrossBinding();
	TestLeavingAfterACommitKeepsTheBinding();
	TestDisplayTextAvoidsTheFontCollisions();
	TestCaptureTimeoutWindowIsReasonable();
	TestFirstRefreshAlwaysEnumeratesTheKeyboard();
	TestDisabledPlayerCanComeBack();
	TestBusIsAlwaysMultitap();
	TestKeyboardOnPlayerTwoIsVisibleToTheGame();
	TestAllFourPlayersAreReachable();
	TestDisabledPlayerLeavesAHole();
	TestTheKeyboardReportsAPadLikeAnyOtherDevice();
	TestDeadzoneTrimsCentreAndKeepsFullTravel();
	TestRestoreDefaultsClearsTheDeadzone();
	TestDeadzoneIsClamped();
	TestDeadzoneRoundTripsPerDevice();
	TestAnUnreadableDeadzoneKeepsTheDefault();
	TestAHandEditedDeadzoneCannotExceedTheMaximum();
	TestDeadzoneDefaultsToNone();
	TestAnOlderInputSnapshotIsRefused();
	TestAltKeepsTheKeyboardPadConnected();
	TestChangingAPlayerDeviceReachesThePadBus();
	TestDisablingAPlayerReachesThePadBus();
	TestAutoAssignmentPutsFirstGamepadOnPlayerOne();
	TestKeyboardIsTheLastFallback();
	TestDisabledPlayerIsSkipped();
	TestPinnedPreferencesWinOverAuto();
	TestConflictingPinsResolveToLowerPlayer();
	TestSoftlockGuardMovesKeyboardToPlayerOne();
	TestHotplugGivesKeyboardToPlayerOne();
	TestReplugRestoresPinnedDevice();
	TestAvailableDevicesHidesOtherPlayersDevices();
	TestPlayerOneCannotBeDisabled();
	TestIniRoundTrip();
	TestIniDefaultsAreRestored();
	TestRestoreDeviceDefaults();
	TestUnknownNamesKeepDefaults();
	TestFileRefusesToDisablePlayerOne();
#if defined(CTR_AP)
	TestKeyboardNeverReachesTheRawGamepadMask();
	TestRawKeyProbeIgnoresBindings();
	TestShortcutDefaultsAreTheOldKeys();
	TestShortcutFollowsItsRebind();
	TestTheNewerBindingClearsTheOlder();
	TestShortcutCaptureWarnsAndReplaces();
	TestRestoreDefaultsRestoresBoth();
	TestDevRowsFollowDevKeys();
	TestHiddenDevRowsStillTakePartInConflicts();
	TestShortcutsRoundTripAndOldFilesLoad();
	TestShortcutsStayQuietWhileTypingOrRebinding();
#endif

	printf("\n%d checks, %d failures\n", s_checks, s_failures);

	const int result = (s_failures == 0) ? 0 : 1;

	TestRemoveScratchDir();
	return result;
}
