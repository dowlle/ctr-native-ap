// Archipelago keyboard shortcuts that the Controls page can move.
//
// Each one is a named action with a key stored in controls.ini under
// [AP Shortcuts] (platform/native_controls.c). The defaults are the keys the
// shortcuts had before they became bindable, so nothing changes for a player who
// never opens the page. A key is never bound to a game control and a shortcut at
// the same time: binding one clears the other (NativeControls_SetBinding and
// NativeControls_SetShortcutKey).
//
// Freestanding on purpose: the AP modules include this without pulling in SDL or
// the controls module, the same way they used to declare
// Platform_InputRawKeyDown themselves.

#ifndef PLATFORM_NATIVE_SHORTCUTS_H
#define PLATFORM_NATIVE_SHORTCUTS_H

enum NativeShortcut
{
	// Character picker in an adventure hub (ap/ap_charswap.c). Live for players.
	NATIVE_SHORTCUT_CHARACTER_PICKER = 0,

	// Test-mode cycles for the picker's stat rules. Only matter with no seed
	// connected (ap/ap_charswap.c).
	NATIVE_SHORTCUT_DEV_PROGRESSIVE_MODE,
	NATIVE_SHORTCUT_DEV_EDITABLE_MODE,

	// Trap test keys, live only with dev_keys=1 in ap-config.txt (ap/ap_traps.c).
	NATIVE_SHORTCUT_DEV_TRAP_ICY,
	NATIVE_SHORTCUT_DEV_TRAP_LOW_GRAVITY,
	NATIVE_SHORTCUT_DEV_TRAP_NO_BRAKES,
	NATIVE_SHORTCUT_DEV_TRAP_BOOST,
	NATIVE_SHORTCUT_DEV_TRAP_FIRST_PERSON,
	NATIVE_SHORTCUT_DEV_TRAP_ALL,

	// Shortcutless test keys, live only with dev_keys=1 (ap/ap_shortcut.c).
	NATIVE_SHORTCUT_DEV_SHORTCUTLESS,
	NATIVE_SHORTCUT_DEV_MARK_QUAD,

	// Demo camera toggle, live only with dev_keys=1 and debug_democam=1
	// (ap/ap_democam.c).
	NATIVE_SHORTCUT_DEV_DEMO_CAMERA,

	NATIVE_SHORTCUT_COUNT,
};

// 1 while the key bound to `shortcut` is held. Reads 0 when the shortcut is
// unbound, while a Controls page rebind is listening, and while a text field
// (the Connection page) has the keyboard, so typing never fires a shortcut.
// Defined in platform/native_input.c for the builds that have AP hotkeys.
int Platform_InputShortcutDown(int shortcut);

#endif
