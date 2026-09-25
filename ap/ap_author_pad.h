#ifndef AP_AUTHOR_PAD_H
#define AP_AUTHOR_PAD_H

// Controller keys for Box Author Mode (box authoring build only). The numpad
// keys (ap_author.h) stay; this adds the same three actions on player 1's pad:
//
//   Select            drop a box where the kart is (on release)
//   hold Select + L1  remove the newest box on this track
//   hold Select + R1  save the file and list this track's boxes to the log
//
// Select alone acts on RELEASE, so a chord never also drops a box. Retail
// racing gives Select no job (the only Select use in the port is the paused
// adventure-hub tracker map, ap_tracker.c, which is never live here), but L1
// and R1 hop and powerslide. So while author input is live the caller strips
// Select from player 1's pad, and strips L1 and R1 from the moment Select is
// held until each is released: the chord never makes the kart hop, and a
// shoulder still held when Select comes up does not read as a fresh tap.
//
// Freestanding (no engine headers), so tools/test-author-pad.c checks exactly
// what the engine runs. The masks are the engine's BTN_* values
// (include/namespace_Gamepad.h), passed in by the caller.

enum
{
	AP_AUTHOR_PAD_NONE = 0,
	AP_AUTHOR_PAD_DROP = 1,
	AP_AUTHOR_PAD_UNDO = 2,
	AP_AUTHOR_PAD_LIST = 3,
};

typedef struct
{
	int selectDown;        // Select was held on the last live frame
	int chorded;           // L1 or R1 was pressed during this Select hold
	unsigned int swallow;  // shoulder bits stripped until they are released
	unsigned int prevHeld; // last frame's unstripped buttons, for our own edges
} AP_AuthorPadState;

typedef struct
{
	unsigned int select, l1, r1;
} AP_AuthorPadButtons;

static inline void AP_AuthorPad_Reset(AP_AuthorPadState *s)
{
	s->selectDown = 0;
	s->chorded = 0;
	s->swallow = 0;
	s->prevHeld = 0;
}

// One frame. live: author input is live (mode on, race ready, not a boss race,
// not paused, an authorable track). held: player 1's buttons this frame before
// stripping. Presses are edges of held against the previous frame's unstripped
// buttons, not the engine's tapped mask: the engine derives taps from the
// stripped mask, so a swallowed shoulder would look freshly pressed every
// frame. *strip receives the bits the caller removes from player 1's held,
// tapped and released masks. Returns the action to run, if any.
static inline int AP_AuthorPad_Step(AP_AuthorPadState *s, const AP_AuthorPadButtons *b, int live,
                                    unsigned int held, unsigned int *strip)
{
	int action = AP_AUTHOR_PAD_NONE;
	unsigned int shoulders = b->l1 | b->r1;
	unsigned int tapped = held & ~s->prevHeld;

	if (!live)
	{
		// Leaving the live context drops a half-made gesture; nothing fires.
		AP_AuthorPad_Reset(s);
		s->prevHeld = held; // a button already down is not a press later
		*strip = 0;
		return AP_AUTHOR_PAD_NONE;
	}
	s->prevHeld = held;

	// A gesture starts with a press of Select, so a Select already held when
	// author input became live does nothing when it comes up.
	if (tapped & b->select)
	{
		s->selectDown = 1;
		s->chorded = 0;
	}
	if (s->selectDown && (held & b->select))
	{
		if ((tapped & shoulders) == shoulders)
			s->chorded = 1; // both at once: ambiguous, do neither
		else if (tapped & b->l1)
		{
			s->chorded = 1;
			action = AP_AUTHOR_PAD_UNDO;
		}
		else if (tapped & b->r1)
		{
			s->chorded = 1;
			action = AP_AUTHOR_PAD_LIST;
		}
		s->swallow |= held & shoulders;
	}
	else if (s->selectDown)
	{
		if (!s->chorded)
			action = AP_AUTHOR_PAD_DROP;
		s->selectDown = 0;
		s->chorded = 0;
	}

	s->swallow &= held;
	*strip = b->select | s->swallow;
	return action;
}

#endif
