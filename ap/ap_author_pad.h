#ifndef AP_AUTHOR_PAD_H
#define AP_AUTHOR_PAD_H

// Controller keys for Box Author Mode. The numpad keys (ap_author.h) stay;
// this puts every authoring action on player 1's pad, so a Steam Deck needs no
// keyboard:
//
//   Select                  drop a box where the kart is (on release)
//   hold Select + L1        delete the selected box (the newest when none is)
//   hold Select + R1        save the file and list this track's boxes to the log
//   hold Select + Square    select the previous box on this track
//   hold Select + Circle    select the next box on this track
//   hold Select + Cross     move the selected box (move mode, below)
//   hold Select + Triangle  export submission files (ap_author_export.h)
//
// Move mode, until Select is pressed again:
//
//   D-pad                   slide the selected box along the world X and Z axes
//   L1 / R1                 lower / raise it
//
// Select alone acts on RELEASE, so a chord never also drops a box. Retail
// racing gives Select no job (the only Select use in the port is the paused
// adventure-hub tracker map, ap_tracker.c, which is never live here), but L1
// and R1 hop and powerslide, Cross accelerates, Square brakes, Circle fires
// and Triangle looks back. So while author input is live the caller strips
// Select from player 1's pad, and strips every chord button pressed while
// Select is held until that button is released: the chord never makes the
// kart hop, fire or brake, and a button still held when Select comes up does
// not read as a fresh tap. Move mode also strips the D-pad and the shoulders,
// so the kart does not steer or hop while the box moves.
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
	AP_AUTHOR_PAD_PREV = 4,
	AP_AUTHOR_PAD_NEXT = 5,
	AP_AUTHOR_PAD_MOVE = 6,      // ask to enter move mode (the caller confirms)
	AP_AUTHOR_PAD_MOVE_DONE = 7, // move mode ended by the player
	AP_AUTHOR_PAD_EXPORT = 8,
	AP_AUTHOR_PAD_NUDGE_XNEG = 9,
	AP_AUTHOR_PAD_NUDGE_XPOS = 10,
	AP_AUTHOR_PAD_NUDGE_ZPOS = 11,
	AP_AUTHOR_PAD_NUDGE_ZNEG = 12,
	AP_AUTHOR_PAD_LOWER = 13,
	AP_AUTHOR_PAD_RAISE = 14,
};

typedef struct
{
	int selectDown;        // Select was held on the last live frame
	int chorded;           // a chord button was pressed during this Select hold
	int moving;            // move mode; set by the caller via AP_AuthorPad_SetMoving
	unsigned int swallow;  // chord bits stripped until they are released
	unsigned int prevHeld; // last frame's unstripped buttons, for our own edges
} AP_AuthorPadState;

// Zero is a valid "not mapped" for every field after r1, so an initialiser
// that names only the first three keeps the original three-action layout.
typedef struct
{
	unsigned int select, l1, r1;
	unsigned int square, circle, cross, triangle;
	unsigned int up, down, left, right;
} AP_AuthorPadButtons;

static inline void AP_AuthorPad_Reset(AP_AuthorPadState *s)
{
	s->selectDown = 0;
	s->chorded = 0;
	s->moving = 0;
	s->swallow = 0;
	s->prevHeld = 0;
}

// The caller answers AP_AUTHOR_PAD_MOVE (a box is selected: 1) and ends move
// mode when the box goes away (0).
static inline void AP_AuthorPad_SetMoving(AP_AuthorPadState *s, int moving)
{
	s->moving = moving ? 1 : 0;
}

// One frame. live: author input is live (mode on, race ready, not a boss race,
// not paused, an authorable track). held: player 1's buttons this frame before
// stripping. Presses are edges of held against the previous frame's unstripped
// buttons, not the engine's tapped mask: the engine derives taps from the
// stripped mask, so a swallowed button would look freshly pressed every
// frame. *strip receives the bits the caller removes from player 1's held,
// tapped and released masks. Returns the action to run, if any.
static inline int AP_AuthorPad_Step(AP_AuthorPadState *s, const AP_AuthorPadButtons *b, int live,
                                    unsigned int held, unsigned int *strip)
{
	int action = AP_AUTHOR_PAD_NONE;
	unsigned int shoulders = b->l1 | b->r1;
	unsigned int faces = b->square | b->circle | b->cross | b->triangle;
	unsigned int dpad = b->up | b->down | b->left | b->right;
	unsigned int tapped = held & ~s->prevHeld;

	if (!live)
	{
		// Leaving the live context drops a half-made gesture and move mode;
		// nothing fires.
		AP_AuthorPad_Reset(s);
		s->prevHeld = held; // a button already down is not a press later
		*strip = 0;
		return AP_AUTHOR_PAD_NONE;
	}
	s->prevHeld = held;

	if (s->moving)
	{
		// Select ends move mode. It counts as a finished chord, so its release
		// does not also drop a box.
		if (tapped & b->select)
		{
			s->moving = 0;
			s->selectDown = 1;
			s->chorded = 1;
			action = AP_AUTHOR_PAD_MOVE_DONE;
		}
		else if (tapped & b->left)
			action = AP_AUTHOR_PAD_NUDGE_XNEG;
		else if (tapped & b->right)
			action = AP_AUTHOR_PAD_NUDGE_XPOS;
		else if (tapped & b->up)
			action = AP_AUTHOR_PAD_NUDGE_ZPOS;
		else if (tapped & b->down)
			action = AP_AUTHOR_PAD_NUDGE_ZNEG;
		else if ((tapped & shoulders) == shoulders && shoulders != 0)
			; // both at once: ambiguous, neither
		else if (tapped & b->l1)
			action = AP_AUTHOR_PAD_LOWER;
		else if (tapped & b->r1)
			action = AP_AUTHOR_PAD_RAISE;
		// A shoulder or D-pad direction held when move mode ends stays away
		// from the game until it is released.
		s->swallow = (s->swallow | (held & (shoulders | dpad))) & held;
		if (s->moving)
		{
			*strip = b->select | shoulders | dpad | s->swallow;
			return action;
		}
		*strip = b->select | s->swallow;
		return action;
	}

	// A gesture starts with a press of Select, so a Select already held when
	// author input became live does nothing when it comes up.
	int heldBefore = s->selectDown; // Select was already down on the last frame
	if (tapped & b->select)
	{
		s->selectDown = 1;
		s->chorded = 0;
		heldBefore = 0;
	}
	if (s->selectDown && (held & b->select))
	{
		const unsigned int chord[6] = {b->l1, b->r1, b->square, b->circle, b->cross, b->triangle};
		// A face button pressed in the same frame as Select is driving, not a
		// chord (Cross with Select still drops on release). The shoulders keep
		// their original rule and count from the first frame.
		unsigned int chordTaps = tapped & (shoulders | (heldBefore ? faces : 0));
		int count = 0, i;

		// Cross and Square each span two engine bits (BTN_CROSS, BTN_SQUARE),
		// so count buttons, not bits.
		for (i = 0; i < 6; i++)
		{
			if (chord[i] != 0 && (chordTaps & chord[i]) != 0)
				count++;
		}
		if (count > 1)
			s->chorded = 1; // two at once: ambiguous, do neither
		else if (count == 1)
		{
			s->chorded = 1;
			if (chordTaps & b->l1)
				action = AP_AUTHOR_PAD_UNDO;
			else if (chordTaps & b->r1)
				action = AP_AUTHOR_PAD_LIST;
			else if (chordTaps & b->square)
				action = AP_AUTHOR_PAD_PREV;
			else if (chordTaps & b->circle)
				action = AP_AUTHOR_PAD_NEXT;
			else if (chordTaps & b->cross)
				action = AP_AUTHOR_PAD_MOVE;
			else if (chordTaps & b->triangle)
				action = AP_AUTHOR_PAD_EXPORT;
		}
		// Shoulders held during the hold are taken (the original rule: a
		// powerslide held when Select goes down is part of the chord). Face
		// buttons are taken only when pressed during the hold, so a held Cross
		// keeps accelerating while Select drops a box.
		s->swallow |= (held & shoulders) | chordTaps;
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

// ── the selection and move rules, shared by retail and custom tracks ────────

// Steps in world units: a nudge moves the box a seventh of its hit radius
// (AP_BOX_HIT_RADIUS 0x6e), a height step half that.
#define AP_AUTHOR_NUDGE_STEP  16
#define AP_AUTHOR_HEIGHT_STEP 8

// 1-based position of row `sel` among this track's rows idx[0..n), 0 when it
// is not one of them.
static inline int AP_AuthorSel_Ordinal(const int *idx, int n, int sel)
{
	int i;

	for (i = 0; i < n; i++)
		if (idx[i] == sel)
			return i + 1;
	return 0;
}

// The next selection. dir < 0 is the previous box, dir > 0 the next, wrapping.
// With nothing selected on this track either direction starts at the newest
// box, the one Select + L1 deletes. -1 when the track has none.
static inline int AP_AuthorSel_Step(const int *idx, int n, int sel, int dir)
{
	int at = AP_AuthorSel_Ordinal(idx, n, sel);

	if (n <= 0)
		return -1;
	if (at == 0)
		return idx[n - 1];
	at--;
	at = dir < 0 ? (at + n - 1) % n : (at + 1) % n;
	return idx[at];
}

// One move step on a stored row, kept inside the signed 16-bit range the LEV
// uses. Returns 1 when the row changed.
static inline int AP_AuthorNudge_Axis(short *v, int delta)
{
	int next = (int)*v + delta;

	if (next > 32767)
		next = 32767;
	if (next < -32768)
		next = -32768;
	if (next == (int)*v)
		return 0;
	*v = (short)next;
	return 1;
}

static inline int AP_AuthorNudge_Apply(short *x, short *y, short *z, int dx, int dy, int dz)
{
	int changed = AP_AuthorNudge_Axis(x, dx);
	changed |= AP_AuthorNudge_Axis(y, dy);
	changed |= AP_AuthorNudge_Axis(z, dz);
	return changed;
}

// The world step for a nudge action: *dx, *dy, *dz. Returns 0 for anything
// that is not a nudge. Height is +Y, as in the stored rows: the engine's Y
// axis points up (AP_Placement_FromKartAxis keeps the kart's Y as it is).
static inline int AP_AuthorNudge_Delta(int action, int *dx, int *dy, int *dz)
{
	*dx = *dy = *dz = 0;
	switch (action)
	{
	case AP_AUTHOR_PAD_NUDGE_XNEG: *dx = -AP_AUTHOR_NUDGE_STEP; return 1;
	case AP_AUTHOR_PAD_NUDGE_XPOS: *dx = AP_AUTHOR_NUDGE_STEP; return 1;
	case AP_AUTHOR_PAD_NUDGE_ZPOS: *dz = AP_AUTHOR_NUDGE_STEP; return 1;
	case AP_AUTHOR_PAD_NUDGE_ZNEG: *dz = -AP_AUTHOR_NUDGE_STEP; return 1;
	case AP_AUTHOR_PAD_LOWER: *dy = -AP_AUTHOR_HEIGHT_STEP; return 1;
	case AP_AUTHOR_PAD_RAISE: *dy = AP_AUTHOR_HEIGHT_STEP; return 1;
	default: return 0;
	}
}

#endif
