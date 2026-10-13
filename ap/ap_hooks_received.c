// ap_hooks_received.c -- part of the ap_hooks unit (issue #305).
// Received-item state: per-category and per-type counts, and the room-scoped
// one-shot effect markers (#299), including the Turbo Grant fired count.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// ---------------------------------------------------------------------------
// NETWORK glue: connect once, pump every frame, surface received items.
// ---------------------------------------------------------------------------

static int ap_net_started = 0;

// AI-difficulty option sync: 1 once this connection's override has been pulled
// into the local config (one-shot per connect; re-armed on each fresh connect).
static int ap_diff_pulled = 0;

// Count of received items per bit-pool category (index by AP_ItemCat 0..COUNT-1).
// Kept ONLY to drive the cosmetic AdvProgress bits (podium / profile % / hub-map
// icons) in AP_ApplyItems. The adventure GATES no longer read these bits.
static int ap_item_count[AP_CAT_COUNT] = {0};

// Per-item-TYPE received count (Option B). This is the AUTHORITATIVE gate input:
// the boss garages, hub doors, gem cups, Turbo Track, Slide Coliseum, and trophy
// warp pads read these via AP_GateCount* (see game/232/AH_*.c, #ifdef CTR_AP).
// Keyed by raw item index 0..14 (id - AP_ITEM_BASE). Decoupled from location
// bits, so generic item fill never collides with the game's own location grants.
static int ap_recv_count[AP_ITEM_INDEX_COUNT] = {0};

// Foreign-only split of ap_recv_count (issue #85): receipts that are NOT an
// own-world location check -- multiworld items + starting inventory. The seed
// verifier seeds its simulated tally from this and banks own items from the scout
// cache as it opens each location, so the own component follows checked-state
// synchronously and cannot race the ReceivedItems drain. See AP_GateCountForeign.
static int ap_recv_count_foreign[AP_ITEM_INDEX_COUNT] = {0};

// Complete 0.2.0 inventory projection for ap_verify.c. The gameplay counters
// above intentionally remain the compact legacy 0..14 gate table; widening
// this separate verifier tally avoids changing any live reward semantics.
//
// Sized from the HIGHEST index in the apworld table, which is no longer the Turbo
// Grant: the 0.2.0 trap wave added four identities at 190..193, so the table is
// 194 long. Anything past the end is dropped by the
// bounds check at the receive site rather than counted, so an undersized array
// silently loses foreign receipts from the verifier's view.
// Includes the additive trial letters at indexes 194..199, not only traps.
#define AP_VERIFY_ITEM_INDEX_COUNT 200
CTR_STATIC_ASSERT(AP_VERIFY_ITEM_INDEX_COUNT > AP_TRAP_ITEM_INDEX_MAX);
CTR_STATIC_ASSERT(AP_VERIFY_ITEM_INDEX_COUNT > AP_TURBOGRANT_ITEM_INDEX);
static int ap_verify_recv_foreign[AP_VERIFY_ITEM_INDEX_COUNT] = {0};
static unsigned char ap_verify_custom_letter_foreign[AP_CUSTOM_LETTER_VERIFY_COUNT] = {0};
static unsigned char ap_letter_received[CTR_CFG_LETTER_TRACK_COUNT][CTR_CFG_LETTER_COUNT] = {{0}};
static unsigned char ap_custom_letter_received[AP_CUSTOM_LETTER_SLOT_COUNT][CTR_CFG_LETTER_COUNT] = {{0}};

// ── One-shot effect replay dedup (traps + Wumpa; #299 room-scoped) ──
// The server resends the FULL ReceivedItems list on every (re)connect. Gate
// COUNTS rebuild idempotently from it, but one-shot EFFECTS must not re-fire
// (live hits: a crash-restore first-person trap re-trigger and a replayed trap
// on a 3-player seed). The marker is the highest server item index whose
// effect has been handled. Since #299 it lives in the ROOM's DataStorage
// (ap_net.h AP_NET_FX_EFFECT, key ctr_fx_<team>_<slot>), so a fresh room made
// from the same seed starts empty and a reconnect to the same room reads it
// back. The rules (hold until the Get reply, max-only writes, one-time
// migration, no timeout) are in ap_fxmarker_logic.h. The Turbo Grant fired
// count uses the same machinery under AP_NET_FX_TURBO_FIRED.
//
// ctr-ap-fxseen.txt (endpoint<TAB>seed<TAB>slot<TAB>max) and
// ctr-ap-turbogrant.txt (seed<TAB>slot<TAB>fired) are the 0.2.1 local ledgers.
// They are only READ, once, when this room has no key yet, to migrate an
// ongoing run; the migrated row is then deleted on the background writer so a
// later fresh room cannot inherit it. Nothing writes a marker to them anymore.
#define AP_FXSEEN_FILE "ctr-ap-fxseen.txt"
#define AP_TURBOGRANT_FILE "ctr-ap-turbogrant.txt"
// Log once when the Get reply is this many ticks late (about 10 s at 60 Hz).
#define AP_FX_OVERDUE_TICKS 600

static AP_FxMarker ap_fx_marker;    // effect marker (empty = -1)
static AP_FxMarker ap_tg_marker;    // Turbo Grant fired count (empty = 0)
static unsigned    ap_fx_rev[AP_NET_FX_COUNT];
static int         ap_fx_wait_ticks = 0;
static char ap_fx_seed[128] = "";
static char ap_fx_slot[64] = "";
static char ap_fx_endpoint[192] = "";

// Effect items drained while the marker is pending: the raw item id and its
// server index, released in arrival order once the marker is known.
typedef struct AP_FxHeld
{
	long long item;
	long long srvIdx;
} AP_FxHeld;
static AP_FxHeld *ap_fx_held = NULL;
static int        ap_fx_held_n = 0;
static int        ap_fx_held_cap = 0;

// Legacy 0.2.1 row for this room (endpoint+seed+slot). Game thread, read only,
// and only reached when the room has no key. A removal still queued on the
// writer must land first or a fresh room could read a row that is being
// deleted.
static int AP_FxLegacyEffectRow(long long *max)
{
	FILE *f;
	char line[320];
	int found = 0;

	if (ap_fx_endpoint[0] == '\0' || ap_fx_seed[0] == '\0')
		return 0;
	ap_file_writer_flush(2000);
	f = fopen(AP_FXSEEN_FILE, "r");
	if (f == NULL)
		return 0;
	while (fgets(line, sizeof line, f))
	{
		AP_FxSeenRow row;
		if (AP_FxSeenParseRow(line, &row) &&
		    AP_FxSeenRowMatches(&row, ap_fx_endpoint, ap_fx_seed, ap_fx_slot))
		{
			*max = row.max;
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

// Legacy 0.2.1 Turbo Grant row. That file never had an endpoint column, so
// seed+slot is the whole identity (the wider migration ambiguity is noted in
// ap_fxmarker_logic.h).
static int AP_FxLegacyTurboRow(long long *fired)
{
	FILE *f;
	char line[320];
	int found = 0;

	if (ap_fx_seed[0] == '\0')
		return 0;
	ap_file_writer_flush(2000);
	f = fopen(AP_TURBOGRANT_FILE, "r");
	if (f == NULL)
		return 0;
	while (fgets(line, sizeof line, f))
	{
		char seed[128], slot[64];
		int n;
		if (sscanf(line, "%127[^\t]\t%63[^\t]\t%d", seed, slot, &n) == 3 &&
		    !strcmp(seed, ap_fx_seed) && !strcmp(slot, ap_fx_slot))
		{
			*fired = n < 0 ? 0 : n;
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

// Background-writer job: drop one migrated row from a legacy ledger, keeping
// every other row as it is. The payload is a snapshot of the identity.
typedef struct AP_FxLegacyRemoveJob
{
	int  turbo; // 0 = ctr-ap-fxseen.txt, 1 = ctr-ap-turbogrant.txt
	char endpoint[sizeof ap_fx_endpoint];
	char seed[sizeof ap_fx_seed];
	char slot[sizeof ap_fx_slot];
} AP_FxLegacyRemoveJob;

static int AP_FxLegacyRemoveWriteJob(const void *payload, size_t len)
{
	const AP_FxLegacyRemoveJob *j = (const AP_FxLegacyRemoveJob *)payload;
	static char line[512];            // writer thread only
	static char out[64 * 512];        // writer thread only
	const char *path;
	size_t used = 0;
	int dropped = 0;
	FILE *f;

	if (len != sizeof *j)
		return 0;
	path = j->turbo ? AP_TURBOGRANT_FILE : AP_FXSEEN_FILE;
	f = fopen(path, "r");
	if (f == NULL)
		return 1; // nothing to remove
	while (fgets(line, sizeof line, f))
	{
		size_t rl;
		int match;
		if (j->turbo)
		{
			char seed[128], slot[64];
			int n;
			match = sscanf(line, "%127[^\t]\t%63[^\t]\t%d", seed, slot, &n) == 3 &&
			        !strcmp(seed, j->seed) && !strcmp(slot, j->slot);
		}
		else
		{
			AP_FxSeenRow row;
			match = AP_FxSeenParseRow(line, &row) &&
			        AP_FxSeenRowMatches(&row, j->endpoint, j->seed, j->slot);
		}
		if (match)
		{
			dropped = 1;
			continue;
		}
		rl = strlen(line);
		if (used + rl > sizeof out)
			break; // keep what fits; the file was always small and bounded
		memcpy(out + used, line, rl);
		used += rl;
	}
	fclose(f);
	if (!dropped)
		return 1;
	return ap_file_writer_replace_file(path, out, used);
}

static void AP_FxLegacyRemove(int turbo)
{
	AP_FxLegacyRemoveJob j;

	memset(&j, 0, sizeof j);
	j.turbo = turbo;
	snprintf(j.endpoint, sizeof j.endpoint, "%s", ap_fx_endpoint);
	snprintf(j.seed, sizeof j.seed, "%s", ap_fx_seed);
	snprintf(j.slot, sizeof j.slot, "%s", ap_fx_slot);
	ap_file_writer_submit(turbo ? AP_FILE_WRITER_SLOT_TURBOGRANT : AP_FILE_WRITER_SLOT_FXSEEN,
	                      AP_FxLegacyRemoveWriteJob, &j, sizeof j);
}

// Run the one-shot effect of one received item. `item` is the raw AP item id.
static void AP_FxApply(long long item)
{
	long long idx = item - AP_ITEM_BASE;

	if (AP_TrapItemIndexIsTrap(idx))
		AP_TrapReceive(AP_TrapEffectForItemIndex(idx));
	else if (idx == AP_WUMPA_SMALL_BUNDLE_ITEM_INDEX)
		AP_WumpaReceive(3);
	else if (idx == AP_WUMPA_BIG_BUNDLE_ITEM_INDEX)
		AP_WumpaReceive(10);
	else if (AP_ItemCategory(item) == AP_CAT_WUMPA)
		AP_WumpaReceive(1);
}

// The drain's single entry for a one-shot effect item: apply it, skip it as
// already handled in this room, or hold it until the marker is known.
static void AP_FxOffer(long long item, long long srvIdx)
{
	int decision = AP_FxMarkerDecide(&ap_fx_marker, srvIdx);

	if (decision == AP_FXM_DECIDE_APPLY)
	{
		AP_FxApply(item);
		return;
	}
	if (decision == AP_FXM_DECIDE_SKIP)
	{
		if (AP_TrapItemIndexIsTrap(item - AP_ITEM_BASE))
		{
			char skipmsg[96];
			snprintf(skipmsg, sizeof skipmsg,
			         "[AP TRAP] replay dedup: trap at index %lld skipped\n", srvIdx);
			AP_LogLine(skipmsg);
		}
		return;
	}
	if (ap_fx_held_n == ap_fx_held_cap)
	{
		int cap = ap_fx_held_cap ? ap_fx_held_cap * 2 : 64;
		AP_FxHeld *grown = (AP_FxHeld *)realloc(ap_fx_held, (size_t)cap * sizeof *grown);
		if (grown == NULL)
		{
			// Out of memory: drop the hold, not the effect. The next connect
			// replays the full item list and offers it again.
			AP_LogLine("[AP FX] could not hold an effect item (out of memory)\n");
			return;
		}
		ap_fx_held = grown;
		ap_fx_held_cap = cap;
	}
	ap_fx_held[ap_fx_held_n].item = item;
	ap_fx_held[ap_fx_held_n].srvIdx = srvIdx;
	ap_fx_held_n++;
}

// Fresh connect: both markers go back to pending, the held list empties (the
// server is about to resend everything anyway), and the identity used for the
// one-time legacy migration is captured.
static void AP_FxConnectReset(void)
{
	int k;

	AP_FxMarkerReset(&ap_fx_marker, -1);
	AP_FxMarkerReset(&ap_tg_marker, 0);
	ap_fx_held_n = 0;
	ap_fx_wait_ticks = 0;
	for (k = 0; k < AP_NET_FX_COUNT; k++)
		ap_fx_rev[k] = 0;
	if (!ap_net_seed_name(ap_fx_seed, sizeof ap_fx_seed))
		ap_fx_seed[0] = '\0';
	if (!ap_net_slot_name(ap_fx_slot, sizeof ap_fx_slot))
		ap_fx_slot[0] = '\0';
	if (!ap_net_room_endpoint(ap_fx_endpoint, sizeof ap_fx_endpoint))
		ap_fx_endpoint[0] = '\0';
}

static void AP_FxWrite(int which, const AP_FxMarker *m)
{
	int storeState = ap_net_fx_store(which, NULL, NULL);
	ap_net_fx_store_write(which, m->value, AP_FxMarkerWriteOp(m, storeState));
}

// Resolve one marker from its Get reply. Returns 1 on the tick it resolved.
static int AP_FxResolveOne(int which, AP_FxMarker *m)
{
	long long v = 0, local = 0;
	unsigned rev = 0;
	int state = ap_net_fx_store(which, &v, &rev);
	int haveLocal = 0;

	if (m->known || state == AP_FXM_STORE_PENDING)
		return 0;
	if (state != AP_FXM_STORE_VALID)
		haveLocal = which == AP_NET_FX_EFFECT ? AP_FxLegacyEffectRow(&local)
		                                      : AP_FxLegacyTurboRow(&local);
	AP_FxMarkerResolve(m, state, v, haveLocal, local);
	ap_fx_rev[which] = rev;
	if (which == AP_NET_FX_EFFECT)
	{
		// Release what was held, in arrival order, against the resolved marker.
		int k;
		for (k = 0; k < ap_fx_held_n; k++)
			if (AP_FxMarkerDecide(m, ap_fx_held[k].srvIdx) == AP_FXM_DECIDE_APPLY)
				AP_FxApply(ap_fx_held[k].item);
		ap_fx_held_n = 0;
	}
	if (AP_FxMarkerFinishResolve(m))
		AP_FxWrite(which, m);
	if (m->source == AP_FXM_SRC_MIGRATED && !m->migrate_row)
		AP_FxLegacyRemove(which == AP_NET_FX_TURBO_FIRED);
	return 1;
}

// A later server value for a resolved marker (our own max write coming back, or
// another client of this slot). Only ever raises it.
static void AP_FxSyncOne(int which, AP_FxMarker *m)
{
	long long v = 0;
	unsigned rev = 0;
	int state = ap_net_fx_store(which, &v, &rev);

	if (!m->known || rev == ap_fx_rev[which])
		return;
	ap_fx_rev[which] = rev;
	if (state == AP_FXM_STORE_VALID && AP_FxMarkerServerValue(m, v))
		AP_FxLegacyRemove(which == AP_NET_FX_TURBO_FIRED);
}

// Per tick, after the connect reset and before the item drain.
static void AP_FxStorageTick(void)
{
	int fxNow, tgNow;

	if (!ap_net_is_connected())
		return;
	fxNow = AP_FxResolveOne(AP_NET_FX_EFFECT, &ap_fx_marker);
	tgNow = AP_FxResolveOne(AP_NET_FX_TURBO_FIRED, &ap_tg_marker);
	if (fxNow || tgNow)
	{
		// The one line per connect that says where the markers came from. Both
		// keys ride one Get, so they normally resolve on the same tick.
		char msg[200];
		snprintf(msg, sizeof msg,
		         "[AP FX] one-shot effect marker: %s (index %lld); "
		         "Turbo Grants fired: %s (%lld)\n",
		         AP_FxMarkerSourceName(ap_fx_marker.source), ap_fx_marker.value,
		         AP_FxMarkerSourceName(ap_tg_marker.source), ap_tg_marker.value);
		AP_LogLine(msg);
	}
	AP_FxSyncOne(AP_NET_FX_EFFECT, &ap_fx_marker);
	AP_FxSyncOne(AP_NET_FX_TURBO_FIRED, &ap_tg_marker);
	if (!ap_fx_marker.known || !ap_tg_marker.known)
	{
		if (++ap_fx_wait_ticks == AP_FX_OVERDUE_TICKS)
			AP_LogLine("[AP FX] server has not answered the effect marker request; "
			           "traps, Wumpa and Turbo Grants stay held until it does "
			           "(nothing is lost: a reconnect offers them again)\n");
	}
}

// End of a drained batch: raise the effect marker past everything drained and
// write the rise to the room with `max`.
static void AP_FxBatchEnd(long long batchMax)
{
	if (AP_FxMarkerNoteDrained(&ap_fx_marker, batchMax))
		AP_FxWrite(AP_NET_FX_EFFECT, &ap_fx_marker);
}

// Turbo Grant accounting seam (ap_turbogrant.c, same unity build).
int AP_FxTurboFiredKnown(void)
{
	return ap_tg_marker.known;
}

int AP_FxTurboFired(void)
{
	long long v = ap_tg_marker.value;
	return v < 0 ? 0 : v > 0x7fffffff ? 0x7fffffff : (int)v;
}

void AP_FxTurboFiredIncrement(void)
{
	if (AP_FxMarkerIncrement(&ap_tg_marker))
		AP_FxWrite(AP_NET_FX_TURBO_FIRED, &ap_tg_marker);
}

#endif // CTR_AP
