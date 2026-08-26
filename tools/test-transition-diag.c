// cc -std=c11 -Wall -Wextra -Werror -o /tmp/test-transition-diag tools/test-transition-diag.c
//
// Characterization of the ap-state.json `transition.diag` block (2026-08-23
// Alpha 3 bundle inspection). The block exists so a support bundle taken while
// the kart will not move identifies the hold. Every case below pins the exact
// text the formatter emits, so a field renamed or dropped by a later edit fails
// here rather than in the next tester's bundle, and so tools/check-ap-state.py
// and this harness agree on the key names.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_transition_diag.h"

static int failures;

static void expect_text(const char *actual, const char *wanted, const char *name)
{
	int ok = (strcmp(actual, wanted) == 0);
	printf("%s  %-52s\n", ok ? "ok  " : "FAIL", name);
	if (!ok)
	{
		printf("      got:  %s\n      want: %s\n", actual, wanted);
		failures++;
	}
}

static void expect_int(int actual, int wanted, const char *name)
{
	int ok = (actual == wanted);
	printf("%s  %-52s %d\n", ok ? "ok  " : "FAIL", name, actual);
	if (!ok)
	{
		printf("      want: %d\n", wanted);
		failures++;
	}
}

// A healthy hub kart: the shape every ordinary bundle should show.
static void case_healthy_hub(void)
{
	AP_TransitionDiag d = {0};
	char out[512];
	d.kartState = 0;
	d.initFunc = AP_DIAG_INIT_DRIVING;
	d.receivedKeys = 2;
	d.profileKeys = 2;
	AP_TransitionDiagFormat(out, sizeof out, &d);
	expect_text(out,
	            "{\"kart_state\": 0, \"kart_state_name\": \"normal\", "
	            "\"init_func\": \"driving_init\", "
	            "\"pause_state\": 0, \"active_menu\": 0, "
	            "\"aku_hint_state\": 0, \"loading_stage\": 0, "
	            "\"picker\": {\"open\": 0, \"pending_swap\": 0, \"restore_pos\": 0}, "
	            "\"traps\": {\"armed\": 0, \"warning\": 0, \"active\": 0, \"suspended\": 0}, "
	            "\"received_keys\": 2, \"profile_keys\": 2}",
	            "healthy hub kart");
}

// The #269 shape the Blue Eyes bundle implies: the door freeze took the kart
// (KS_FREEZE, FreezeEndEvent INIT), zero received Keys, and the profile count
// already reconciled back to zero by AP_ApplyItems.
static void case_zero_key_door_freeze(void)
{
	AP_TransitionDiag d = {0};
	char out[512];
	d.kartState = 11;
	d.initFunc = AP_DIAG_INIT_FREEZE_END;
	d.receivedKeys = 0;
	d.profileKeys = 0;
	AP_TransitionDiagFormat(out, sizeof out, &d);
	expect_text(out,
	            "{\"kart_state\": 11, \"kart_state_name\": \"freeze\", "
	            "\"init_func\": \"freeze_end_event_init\", "
	            "\"pause_state\": 0, \"active_menu\": 0, "
	            "\"aku_hint_state\": 0, \"loading_stage\": 0, "
	            "\"picker\": {\"open\": 0, \"pending_swap\": 0, \"restore_pos\": 0}, "
	            "\"traps\": {\"armed\": 0, \"warning\": 0, \"active\": 0, \"suspended\": 0}, "
	            "\"received_keys\": 0, \"profile_keys\": 0}",
	            "zero-Key door freeze (#269 shape)");
}

// The swap-lock candidates the Kitkat bundle could not distinguish: a picker
// state machine still busy after the reload, a RectMenu still owning the screen,
// and a trap left suspended with nothing to resume it. Non-zero flags must
// print as 1, not as their raw truthy value.
static void case_swap_lock_candidates(void)
{
	AP_TransitionDiag d = {0};
	char out[512];
	d.kartState = 0;
	d.initFunc = AP_DIAG_INIT_OTHER;
	d.pauseState = 3;
	d.activeMenu = 7; // pointer-ish truthy value
	d.akuHintState = 2;
	d.loadingStage = 0;
	d.pickerOpen = 0;
	d.pickerPending = 1;
	d.pickerRestore = 5;
	d.trapsArmed = 1;
	d.trapsActive = 1;
	d.trapsSuspended = 1;
	d.receivedKeys = 2;
	d.profileKeys = 2;
	AP_TransitionDiagFormat(out, sizeof out, &d);
	expect_text(out,
	            "{\"kart_state\": 0, \"kart_state_name\": \"normal\", "
	            "\"init_func\": \"other\", "
	            "\"pause_state\": 3, \"active_menu\": 1, "
	            "\"aku_hint_state\": 2, \"loading_stage\": 0, "
	            "\"picker\": {\"open\": 0, \"pending_swap\": 1, \"restore_pos\": 1}, "
	            "\"traps\": {\"armed\": 1, \"warning\": 0, \"active\": 1, \"suspended\": 1}, "
	            "\"received_keys\": 2, \"profile_keys\": 2}",
	            "swap-lock candidates, flags normalised");
}

// No driver at all (title screen, mid-load): the names say so instead of
// printing a stale number.
static void case_no_driver(void)
{
	AP_TransitionDiag d = {0};
	char out[512];
	d.kartState = -1;
	d.initFunc = AP_DIAG_INIT_NO_DRIVER;
	d.loadingStage = 4;
	AP_TransitionDiagFormat(out, sizeof out, &d);
	expect_text(out,
	            "{\"kart_state\": -1, \"kart_state_name\": \"no_driver\", "
	            "\"init_func\": \"no_driver\", "
	            "\"pause_state\": 0, \"active_menu\": 0, "
	            "\"aku_hint_state\": 0, \"loading_stage\": 4, "
	            "\"picker\": {\"open\": 0, \"pending_swap\": 0, \"restore_pos\": 0}, "
	            "\"traps\": {\"armed\": 0, \"warning\": 0, \"active\": 0, \"suspended\": 0}, "
	            "\"received_keys\": 0, \"profile_keys\": 0}",
	            "no driver during a load");
}

// Every KartState the engine defines has a name; gaps and out-of-range values
// say "unknown" rather than aliasing a real state.
static void case_kart_state_names(void)
{
	expect_text(AP_TransitionDiagKartStateName(5), "mask_grabbed", "KS_MASK_GRABBED name");
	expect_text(AP_TransitionDiagKartStateName(10), "warp_pad", "KS_WARP_PAD name");
	expect_text(AP_TransitionDiagKartStateName(7), "unknown", "undefined state 7");
	expect_text(AP_TransitionDiagKartStateName(99), "unknown", "out-of-range state");
}

// The dump writes into a fixed buffer; the formatter must report the full
// length so truncation is detectable, and the widest plausible record must fit
// in the 512 bytes AP_DumpState gives it.
static void case_length_budget(void)
{
	AP_TransitionDiag d = {0};
	char out[512];
	int n;
	d.kartState = -2147483647 - 1;
	d.initFunc = AP_DIAG_INIT_FREEZE_END;
	d.pauseState = -2147483647 - 1;
	d.activeMenu = 1;
	d.akuHintState = -2147483647 - 1;
	d.loadingStage = -2147483647 - 1;
	d.pickerOpen = d.pickerPending = d.pickerRestore = 1;
	d.trapsArmed = d.trapsWarning = d.trapsActive = d.trapsSuspended = 2147483647;
	d.receivedKeys = -2147483647 - 1;
	d.profileKeys = -2147483647 - 1;
	n = AP_TransitionDiagFormat(out, sizeof out, &d);
	expect_int(n < (int)sizeof out, 1, "widest record fits the 512-byte buffer");
	expect_int((int)strlen(out) == n, 1, "reported length matches the text");
}

int main(void)
{
	case_healthy_hub();
	case_zero_key_door_freeze();
	case_swap_lock_candidates();
	case_no_driver();
	case_kart_state_names();
	case_length_budget();
	printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
