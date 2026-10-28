/* Start pad activities from the in-game tracker (#375, 0.3.0).
 *
 * AP_PadActivities and AP_GarageActivities gather the facts the physical pad
 * and garage already ask and hand them to the pure decision in
 * ap_pad_activity_logic.h. A start (AP_TrackerLaunchBegin) resumes the hub,
 * loads the pad's hub if the player is in another one, and then the pad or
 * garage itself takes the kart (AH_WarpPad_ThTick, AH_Garage_ThTick) through
 * its own entry path. The request's lifecycle is ap_tracker_launch_logic.h. */
#include "ap_tracker.h"
#include "ap_tracker_launch_logic.h"

static ap_tl_request ap_tl;
static int ap_tl_ready;

static void AP_TLLog(const char *what, const ap_tl_request *r)
{
	char line[192];
	snprintf(line, sizeof line, "[AP TRACKER] start %s: hub=%d pad=%d dest=%d activity=%s mode=%d\n",
	         what, r->hub, r->physical, r->dest, AP_PadActivityName(r->activity), r->mode);
	AP_LogLine(line);
}

static ap_tl_request *AP_TLState(void)
{
	if (!ap_tl_ready)
	{
		AP_TLReset(&ap_tl);
		ap_tl.drop = AP_TL_DROP_NONE;
		ap_tl_ready = 1;
	}
	return &ap_tl;
}

int AP_TrackerLaunchEnabled(void)
{
	return ctr_cfg_active() && ctr_cfg.tracker_pad_launch;
}

static int AP_TLSingleActivity(int dest)
{
	if (dest >= 100 && dest < 105)
	{
#ifdef CTR_CUSTOM_TRACKS
		/* A legacy custom track in a cup slot asks Trophy or CTR itself. */
		if (AP_CustomPadOwnsDestination(&ctr_cfg, dest))
			return AP_PADACT_RACE;
#endif
		return AP_PADACT_CUP;
	}
	if (dest >= 18 && dest < 25)
		return AP_PADACT_CRYSTAL;
	if (dest == 16 || dest == 17)
		return AP_PADACT_RELIC;
	return AP_PADACT_RACE;
}

int AP_PadActivities(int hub, int physical, int dest, ap_padact_list *out)
{
	ap_padact_facts f;
	int admitted[AP_RS_COUNT];
	memset(&f, 0, sizeof f);
	out->count = 0;
	if (!ctr_cfg_active() || physical < 0)
		return 0;
	f.keysHave = AP_GateCount(AP_IDX_KEY);
	f.keysNeed = AP_PadActivityHubKeys(hub, physical);
	f.racerMet = ctr_cfg_racer_lock_met(physical);
	if (ap_content_plan_active())
	{
		const ctr_content_pad *pad = ap_content_plan_pad(physical);
		f.content = 1;
		f.padState = AP_PadState(physical, dest);
#ifdef CTR_CUSTOM_TRACKS
		f.contentReady = pad && pad->occupied && AP_ContentPadReady(physical);
#else
		f.contentReady = 0; /* this build has no content-plan entry route */
#endif
		f.stage1Met = AP_ContentPadOpen(pad);
		if (pad && pad->occupied && (pad->crystal || pad->cup))
		{
			f.node = AP_PADACT_NODE_SINGLE;
			f.single = pad->cup ? AP_PADACT_CUP : AP_PADACT_CRYSTAL;
		}
		else
		{
			ap_content_selector sel;
			int i;
			memset(&sel, 0, sizeof sel);
			if (pad && pad->occupied)
				AP_ContentSelectorBuild(pad, &sel);
			f.node = AP_PADACT_NODE_CONTENT;
			f.modeCount = sel.count < AP_PADACT_MAX_ROWS ? sel.count : AP_PADACT_MAX_ROWS;
			for (i = 0; i < f.modeCount; i++)
			{
				f.contentMode[i] = sel.mode[i];
				f.contentOpen[i] = sel.open[i];
				f.contentLeft[i] = sel.pending[i];
			}
		}
		AP_PadActivityDecide(&f, out);
		return 1;
	}
	f.padState = ctr_cfg_arena_pad_disabled(physical) || ctr_cfg_arena_pad_disabled(dest)
		? 0 : AP_PadState(physical, dest);
	f.stage1Met = AP_PadStage1Met(physical);
	f.replayable = AP_PadReplayable(physical, dest);
	f.contentReady = 1;
#ifdef CTR_CUSTOM_TRACKS
	if (AP_CustomContentRequired())
		f.contentReady = 0;
#endif
	if (dest == AP_CORTEX_DEST && !AP_CortexTrackEntryReady(0))
		f.contentReady = 0;
	{
		int got = AP_PadSelectorFacts(physical, dest, &f.rs, admitted, &f.trophyChecked, &f.stage2Met);
		if (got)
		{
			f.node = AP_PADACT_NODE_SELECTOR;
			f.structure = got == 2 ? AP_RS_STRUCTURE_REFUSED : ctr_cfg.adventure_structure;
			if (got == 2)
				f.rs.present[AP_RS_TROPHY] = 1; /* one row to carry the reason */
		}
		else
		{
			f.node = AP_PADACT_NODE_SINGLE;
			f.single = AP_TLSingleActivity(dest);
		}
	}
	AP_PadActivityDecide(&f, out);
	return 1;
}

int AP_GarageActivities(int hub, ap_padact_list *out)
{
	ap_padact_facts f;
	memset(&f, 0, sizeof f);
	out->count = 0;
	if (!ctr_cfg_active() || hub < 0 || hub > 4)
		return 0;
	f.node = AP_PADACT_NODE_GARAGE;
	f.keysHave = AP_GateCount(AP_IDX_KEY);
	f.keysNeed = AP_PadActivityHubKeys(hub, -1);
	/* The garage's own gates, the ones AH_Garage_ThTick asks. */
	f.garageOpen = hub == 0 ? AP_OxideGarageOpen() : AP_BossGarageOpen(hub - 1);
	AP_PadActivityDecide(&f, out);
	return 1;
}

static int AP_TLLevelHub(void)
{
	struct GameTracker *gt = sdata ? sdata->gGT : 0;
	int hub;
	if (!gt || !(gt->gameMode1 & ADVENTURE_ARENA) || !LOAD_IsOpen_AdvHub())
		return -1;
	hub = gt->levelID - GEM_STONE_VALLEY;
	return hub >= 0 && hub < 5 ? hub : -1;
}

/* Is the chosen row still open? Asked every frame until the pad takes the
 * kart, so a gate that closes on the way (a hub load, an item removed by a
 * reconnect) drops the start instead of entering out of logic. */
static int AP_TLGateOpen(const ap_tl_request *r)
{
	ap_padact_list list;
	if (!AP_TrackerLaunchEnabled())
		return 0;
	if (r->physical < 0)
		AP_GarageActivities(r->hub, &list);
	else
		AP_PadActivities(r->hub, r->physical, r->dest, &list);
	return AP_PadActivityStillOpen(&list, r->activity, r->mode);
}

int AP_TrackerLaunchBegin(int hub, int physical, int dest, int activity, int mode)
{
	ap_tl_request *r = AP_TLState();
	int here = AP_TLLevelHub(), begin;
	ap_tl_request probe;
	if (!AP_TrackerLaunchEnabled() || here < 0)
		return 0;
	AP_TLReset(&probe);
	probe.hub = hub; probe.physical = physical; probe.dest = dest;
	probe.activity = activity; probe.mode = mode;
	if (!AP_TLGateOpen(&probe))
	{
		AP_TLLog("refused (gate)", &probe);
		return 0;
	}
	begin = AP_TLBegin(r, here, hub, physical, dest, activity, mode);
	if (begin == AP_TL_BEGIN_REFUSED)
		return 0;
	MainFreeze_APResumeFromTracker();
	AP_TLLog(begin == AP_TL_BEGIN_LOAD_HUB ? "begins, the pad's hub loads first" : "begins", r);
	return 1;
}

void AP_TrackerLaunchFrame(struct GameTracker *gt)
{
	static const char *const why[] = {"", "dropped (timeout)", "dropped (left the hub)",
		"dropped (gate closed)", "dropped (pad refused)", "dropped (mode not offered)"};
	ap_tl_request *r = AP_TLState(), copy = *r;
	int levelHub, hold, drop;
	if (r->stage == AP_TL_IDLE)
		return;
	levelHub = AP_TLLevelHub();
	/* The same "genuinely idle hub" test the character swap waits for: no
	 * load, pause menu fully closed, no cutscene, door, hint or hub swap. */
	hold = sdata->Loading.stage != LOAD_IDLE || !gt || levelHub < 0 || !AP_HubSafeForRequest(gt);
	/* Once the pad holds the kart its own gates decide; before that the
	 * shared gate is asked again every frame. */
	drop = AP_TLFrame(r, levelHub, hold, hold || r->stage == AP_TL_TAKEN || AP_TLGateOpen(r));
	if (drop != AP_TL_DROP_NONE)
	{
		AP_TLLog(why[drop], &copy);
		return;
	}
	if (!hold && AP_TLWantsLoad(r))
	{
		AP_TLLog("requests the pad's hub", r);
		MainRaceTrack_RequestLoad((s16)(GEM_STONE_VALLEY + r->hub));
	}
}

int AP_TrackerLaunchPadArrives(int physical, int dest)
{
	return AP_TLPadArrives(AP_TLState(), AP_TLLevelHub(), physical, dest);
}

int AP_TrackerLaunchGarageArrives(void)
{
	return AP_TLGarageArrives(AP_TLState(), AP_TLLevelHub());
}

void AP_TrackerLaunchTaken(void)
{
	ap_tl_request *r = AP_TLState(), copy = *r;
	if (r->stage != AP_TL_ARRIVE)
		return;
	AP_TLTaken(r);
	AP_TLLog("taken by the pad", &copy);
}

/* The pad's menu asks for the chosen mode (warp frame 61). The shared gate is
 * asked once more first: a seed change during the warp must not apply a mode
 * the new room closes. Returns the mode, -1 without a tracker start for this
 * pad, or -2 when the gate closed (the caller releases the kart). */
int AP_TrackerLaunchTakeMode(int physical, int dest)
{
	ap_tl_request *r = AP_TLState(), copy = *r;
	int open = r->stage == AP_TL_TAKEN && r->physical == physical && r->dest == dest && AP_TLGateOpen(r);
	int mode = AP_TLTakeMode(r, physical, dest);
	if (mode < 0)
		return -1;
	if (!open)
	{
		AP_TLLog("dropped (gate closed during the warp)", &copy);
		return -2;
	}
	AP_TLLog("answers the pad menu", &copy);
	return mode;
}

/* The pad released the kart or did not offer the chosen mode. */
void AP_TrackerLaunchDrop(int why)
{
	ap_tl_request *r = AP_TLState(), copy = *r;
	if (r->stage == AP_TL_IDLE)
		return;
	AP_TLDrop(r, why);
	AP_TLLog(why == AP_TL_DROP_MODE ? "dropped (mode not offered)" : "dropped (pad refused)", &copy);
}

int AP_TrackerLaunchPending(void)
{
	return AP_TLState()->stage != AP_TL_IDLE;
}
