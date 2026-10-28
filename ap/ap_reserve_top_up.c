#ifdef CTR_AP
#include <limits.h>
#include <common.h>
#include "ap_reserve_top_up.h"
#include "ap_fxmarker_logic.h"
#include "ap_net.h"
#include "ap_seedcfg.h"

// Received count is rebuilt by authoritative replay on every connect. Only
// applied copies advance the room-backed spent marker. The generic network
// store is scoped by room, team and slot, like Turbo Grant's fired marker.
static int ap_rtu_received;
static int ap_rtu_countdown_seen;
static AP_FxMarker ap_rtu_spent;
static unsigned ap_rtu_revision;

void AP_ReserveTopUpReset(void)
{
	ap_rtu_received = 0;
	ap_rtu_countdown_seen = 0;
	ap_rtu_revision = 0;
	AP_FxMarkerReset(&ap_rtu_spent, 0);
}

void AP_ReserveTopUpReceive(void)
{
	if (ap_rtu_received < INT_MAX)
		ap_rtu_received++;
}

static void AP_ReserveTopUpSync(void)
{
	long long value = 0;
	unsigned revision = 0;
	int state = ap_net_fx_store(AP_NET_FX_RESERVE_SPENT, &value, &revision);
	if (!ap_rtu_spent.known)
		AP_FxMarkerResolve(&ap_rtu_spent, state, value, 0, 0);
	else if (state == AP_FXM_STORE_VALID && revision != ap_rtu_revision)
		AP_FxMarkerServerValue(&ap_rtu_spent, value);
	ap_rtu_revision = revision;
}

void AP_ReserveTopUpTick(struct GameTracker *gGT)
{
	struct Driver *local;
	int onTrack, sum, state;
	if (!gGT)
		return;
	onTrack = LOAD_IsOpen_RacingOrBattle();
	if (!onTrack || (gGT->gameMode1 & END_OF_RACE))
		ap_rtu_countdown_seen = 0;
	else if (gGT->trafficLightsTimer >= 1)
		ap_rtu_countdown_seen = 1;
	if (!ap_net_is_connected())
		return;
	AP_ReserveTopUpSync();
	if (!ctr_cfg_active() || !ctr_cfg.reserve_top_up || !ap_rtu_spent.known ||
	    ap_rtu_spent.value >= ap_rtu_received)
		return;
	if (!onTrack || !ap_rtu_countdown_seen || gGT->trafficLightsTimer >= 1 ||
	    (gGT->gameMode1 & (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)))
		return;
	local = gGT->drivers[0];
	if (!local || (local->actionsFlagSet & ACTION_BOT) || local->reserves <= 0)
		return;
	// Convert explicitly rather than relying on out-of-range signed narrowing.
	// The only legal sum is 984..33750, so it crosses at most one signed rail
	// and cannot wrap through zero. Negative Saffi reserves never reach here.
	sum = (int)local->reserves + AP_RESERVE_TOP_UP_AMOUNT;
	local->reserves = (short)(sum > 0x7fff ? sum - 0x10000 : sum);
	// Direct reserve addition preserves the current fire cap and bypasses the
	// separate Progressive Boost grant clamp, as ruled for this filler item.
	AP_FxMarkerIncrement(&ap_rtu_spent);
	state = ap_net_fx_store(AP_NET_FX_RESERVE_SPENT, NULL, NULL);
	ap_net_fx_store_write(AP_NET_FX_RESERVE_SPENT, ap_rtu_spent.value,
	                      AP_FxMarkerWriteOp(&ap_rtu_spent, state));
}
#endif
