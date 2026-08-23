#ifndef AP_FIRSTKEY_FREEZE_H
#define AP_FIRSTKEY_FREEZE_H

// The retail first-boss return briefly sees the locally awarded Key bit before
// AP_ApplyItems removes it. In AP that cosmetic bit is not inventory and must
// not arm a door freeze which only a received Key can release.
//
// Keep this pure so the zero-Key, pre-received-Key and ordinary retail-shaped
// inputs can be pinned without engine state.
static inline int AP_FirstKeyFreezeShouldArm(int profileKeys, int receivedKeys)
{
	return profileKeys == 1 && receivedKeys > 0;
}

#endif // AP_FIRSTKEY_FREEZE_H
