#ifndef AP_NAME_ENTRY_HELD_LOGIC_H
#define AP_NAME_ENTRY_HELD_LOGIC_H

// Name entry ignores a confirm or cancel input that was already held when it
// opened, until that input has been released once (A4-87b).
//
// Name entry reads the keyboard Enter as a held key, and a name from an earlier
// session puts its cursor on SAVE, so an Enter still down from the screen
// before submitted that name on the first frame, before it was ever seen. A
// pad button held through the open frame, or pressed on it, could do the same.
// Every caller opens name entry through SubmitName_RestoreName (Time Trial,
// the garage, the garage skip, the hub backdrop), so that is where the guard
// arms, with the input state of that moment.
//
// Per input, not all or nothing: only the inputs held at the open are ignored,
// each until its own release. A button pressed after the open, or one released
// and pressed again, works at once. Typing, the arrow keys and the grid are
// never guarded.
//
// Freestanding so tools/test-hub-name-entry.c pins it out of engine;
// tools/test-hub-name-entry-production.py checks the wiring.

struct AP_NEH_Guard
{
	unsigned pad; // pad confirm and cancel bits still held from before the open
	int enter;    // keyboard Enter still held from before the open
	int esc;      // keyboard Escape still held from before the open
};

// padMask: the pad bits that confirm or cancel (SAVE, CANCEL, backspace, Start).
static inline void AP_NEH_Arm(struct AP_NEH_Guard *g, unsigned padHeld, unsigned padMask, int enterHeld, int escHeld)
{
	g->pad = padHeld & padMask;
	g->enter = enterHeld != 0;
	g->esc = escHeld != 0;
}

// One frame of name entry. Drops from the guard every input that is no longer
// held, removes the guarded pad bits from the tap bits, and returns which
// keyboard inputs to ignore this frame: bit 0 Enter, bit 1 Escape.
static inline int AP_NEH_Filter(struct AP_NEH_Guard *g, unsigned padHeld, int enterHeld, int escHeld, unsigned *tap)
{
	g->pad &= padHeld;
	g->enter = g->enter && enterHeld;
	g->esc = g->esc && escHeld;
	*tap &= ~g->pad;
	return (g->enter ? 1 : 0) | (g->esc ? 2 : 0);
}

#endif
