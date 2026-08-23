// ap_transition_diag.h -- the `transition.diag` block of ap-state.json.
//
// Diagnostics only. Nothing in the game reads these values back; they exist so
// a support bundle taken while the kart will not move can say WHICH hold the
// driver is under. The two Alpha 3 hub-lock reports (#269 zero-Key boss return,
// and the character-swap lock in the 2026-08-23 bundle inspection) could not be
// told apart from the fields the transition block carried at that point, and
// the swap lock could not be diagnosed at all: freeze bits clear, driver present,
// and no record of the kart state, the driver's INIT table, the pause and
// RectMenu state, the mask-hint state, the picker's own state machine or the
// trap scheduler.
//
// The formatter is freestanding (no game headers) so tools/test-transition-diag.c
// can characterize the exact text, and so the JSON shape is fixed in one place.
// AP_DumpState fills AP_TransitionDiag from live state and calls
// AP_TransitionDiagFormat; the harness calls it with synthetic values.
#ifndef AP_TRANSITION_DIAG_H
#define AP_TRANSITION_DIAG_H

#include <stdio.h>

// Which function sits in the driver's DRIVER_FUNC_INIT slot. The picker and
// the first-key door freeze both swap this slot (ap/ap_charfreeze.h), so its
// identity says who last took or gave back the kart.
enum AP_DiagInitFunc
{
	AP_DIAG_INIT_NO_DRIVER = 0, // drivers[0] is NULL
	AP_DIAG_INIT_NULL = 1,      // slot holds NULL
	AP_DIAG_INIT_DRIVING = 2,   // VehPhysProc_Driving_Init (normal driving)
	AP_DIAG_INIT_FREEZE_END = 3, // VehPhysProc_FreezeEndEvent_Init (a modal hold)
	AP_DIAG_INIT_OTHER = 4      // anything else (engine-owned sequence)
};

typedef struct
{
	int kartState;     // struct Driver kartState (KS_*), -1 when no driver
	int initFunc;      // enum AP_DiagInitFunc
	int pauseState;    // sdata->pause_state
	int activeMenu;    // sdata->ptrActiveMenu != NULL
	int akuHintState;  // sdata->AkuAkuHintState
	int loadingStage;  // sdata->Loading.stage (LOAD_IDLE == 0)
	int pickerOpen;    // ap_charswap: picker owns the screen
	int pickerPending; // ap_charswap: swap reload requested
	int pickerRestore; // ap_charswap: waiting to put the kart back after reload
	int trapsArmed;    // scheduler slots in ARMED
	int trapsWarning;  // scheduler slots in WARNING
	int trapsActive;   // scheduler slots in ACTIVE
	int trapsSuspended; // ACTIVE slots frozen by a scripted sequence
	int receivedKeys;  // AP_GateCount(AP_IDX_KEY): the authoritative Key count
	int profileKeys;   // currAdvProfile.numKeys: the cosmetic count rebuilt from bits
} AP_TransitionDiag;

static const char *AP_TransitionDiagKartStateName(int kartState)
{
	switch (kartState)
	{
	case -1: return "no_driver";
	case 0: return "normal";
	case 1: return "crashing";
	case 2: return "drifting";
	case 3: return "spinning";
	case 4: return "engine_revving";
	case 5: return "mask_grabbed";
	case 6: return "blasted";
	case 9: return "antivshift";
	case 10: return "warp_pad";
	case 11: return "freeze";
	default: return "unknown";
	}
}

static const char *AP_TransitionDiagInitFuncName(int initFunc)
{
	switch (initFunc)
	{
	case AP_DIAG_INIT_NO_DRIVER: return "no_driver";
	case AP_DIAG_INIT_NULL: return "null";
	case AP_DIAG_INIT_DRIVING: return "driving_init";
	case AP_DIAG_INIT_FREEZE_END: return "freeze_end_event_init";
	default: return "other";
	}
}

// Writes one JSON object (no trailing comma or newline) for the diag block.
// Returns the number of characters snprintf would have written, so a caller
// with a fixed buffer can detect truncation the usual way.
static int AP_TransitionDiagFormat(char *out, int cap, const AP_TransitionDiag *d)
{
	return snprintf(out, (size_t)cap,
	                "{\"kart_state\": %d, \"kart_state_name\": \"%s\", "
	                "\"init_func\": \"%s\", "
	                "\"pause_state\": %d, \"active_menu\": %d, "
	                "\"aku_hint_state\": %d, \"loading_stage\": %d, "
	                "\"picker\": {\"open\": %d, \"pending_swap\": %d, \"restore_pos\": %d}, "
	                "\"traps\": {\"armed\": %d, \"warning\": %d, \"active\": %d, \"suspended\": %d}, "
	                "\"received_keys\": %d, \"profile_keys\": %d}",
	                d->kartState, AP_TransitionDiagKartStateName(d->kartState),
	                AP_TransitionDiagInitFuncName(d->initFunc),
	                d->pauseState, d->activeMenu ? 1 : 0,
	                d->akuHintState, d->loadingStage,
	                d->pickerOpen ? 1 : 0, d->pickerPending ? 1 : 0, d->pickerRestore ? 1 : 0,
	                d->trapsArmed, d->trapsWarning, d->trapsActive, d->trapsSuspended,
	                d->receivedKeys, d->profileKeys);
}

#endif // AP_TRANSITION_DIAG_H
