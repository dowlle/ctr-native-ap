#ifndef AP_GARAGESKIP_H
#define AP_GARAGESKIP_H

// ---------------------------------------------------------------------------
// The adventure-start garage skip's session latch (#54/#209).
//
// WHY A LATCH EXISTS AT ALL. When the seed owns the racer, CS_Garage_MenuProc
// commits it and hands off to the name-entry OSK. That handoff is a request:
// sdata->ptrDesiredMenu takes a frame or two to become the active menu, and the
// garage funcPtr keeps running meanwhile. The two writes are idempotent, but
// SubmitName_RestoreName and the confirm sound are not things to repeat every
// frame, so the commit fires once per garage session.
//
// WHY IT IS PER SESSION AND NOT PER PROCESS. The first cut used a plain
// function-local static that was only cleared when the seed stopped owning the
// racer, which on a character-phase seed is never. That soft-locked the game in
// two reachable ways, both needing a restart, because menuGarage's state is
// DISABLE_INPUT_ALLOW_FUNCPTRS | EXECUTE_FUNCPTR: its funcPtr is the ONLY thing
// driving the screen, so a funcPtr that returns early leaves nothing at all.
//
//   1. Cancelling the adventure name-entry OSK. Retail's CANCEL points
//      ptrDesiredMenu back at the garage (game/SubmitName.c:514-517), the garage
//      funcPtr runs again, the latch is still set, and the skip returns before
//      re-issuing the handoff. Dead screen.
//   2. Starting a SECOND new adventure in the same process. The static outlives
//      the garage level load, so the same thing happens on the fresh garage.
//
// The re-arm signal is CS_Garage_ZoomOut, which is exactly "a garage session
// begins" and has precisely two callers, one for each path above:
// CS_Garage_Init (zoomState 0, a fresh garage load, game/233/CS_Garage.c) and
// the OSK CANCEL branch (zoomState 1, game/SubmitName.c:516). Nothing else in
// the tree calls it, so the lifecycle is closed rather than best-effort.
//
// The decision lives here, as a pure function over a tiny struct, so
// tools/test-character-persistence.cpp can drive the frame sequences that
// produced both soft-locks. The engine half is then only "call NewSession from
// ZoomOut, call ShouldCommit from MenuProc", which is the part a reader can
// check by eye.
// ---------------------------------------------------------------------------

struct AP_GarageSkipState
{
	// 1 once this garage session has already committed and handed off.
	int committed;
};

// A garage session begins: a fresh garage load, or a return from a cancelled
// name entry. Re-arms the commit unconditionally.
static inline void AP_GarageSkip_NewSession(struct AP_GarageSkipState *s)
{
	if (s == 0)
		return;
	s->committed = 0;
}

// Should this frame perform the commit and the OSK handoff?
//
// `apRacer` is AP_CharSwap_GarageRacer(): a racer id when the seed owns the
// choice, or negative when it does not and the retail garage should run.
//
// Returns 1 at most once per session. A negative racer also re-arms, so a seed
// that stops owning the racer mid-session (a disconnect) leaves the latch ready
// rather than stale; that path returns 0, because the caller must then run the
// retail garage rather than commit anything.
static inline int AP_GarageSkip_ShouldCommit(struct AP_GarageSkipState *s, int apRacer)
{
	if (s == 0)
		return 0;

	if (apRacer < 0)
	{
		s->committed = 0;
		return 0;
	}

	if (s->committed)
		return 0;

	s->committed = 1;
	return 1;
}

// Does the caller own the screen this frame (skip the retail garage entirely)?
// True whenever the seed owns the racer, whether or not this frame is the one
// that commits: the frames after the commit still must not run the retail
// picker while the handoff settles.
static inline int AP_GarageSkip_Owns(int apRacer)
{
	return apRacer >= 0;
}

#endif // AP_GARAGESKIP_H
