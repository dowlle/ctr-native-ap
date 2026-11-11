// Ordinary pickup checks. Included by game_unity after the race route helpers.
#ifdef CTR_AP

#include "ap_sanity_colour.h"

static struct InstDef *ap_sanity_defs;
static int ap_sanity_defs_count, ap_sanity_ready, ap_sanity_size;
static uint64_t ap_sanity_epoch;
static struct {
    struct Instance *inst;
    long code;
    unsigned color;
    short alpha;
    unsigned char family;
    unsigned short recolour;
} ap_sanity_objects[512];
static long ap_sanity_sent[16384];
static int ap_sanity_sent_count, ap_sanity_feed[3], ap_sanity_feed_ticks;

void AP_SanityLevelInstances(struct InstDef *defs, int count)
{
    ap_sanity_defs = defs;
    ap_sanity_defs_count = count;
    ap_sanity_ready = ap_sanity_size = 0;
}

static const ctr_content_sanity *AP_SanityRoute(struct GameTracker *gt)
{
#ifdef CTR_CUSTOM_TRACKS
    const struct CustomTrackRaceContext *c;
    const ctr_content_pad *cup;
    if (!gt || !ctr_cfg_active() || !ap_content_plan_active() || !(gt->gameMode1 & ADVENTURE_MODE) ||
        gt->boolDemoMode || (gt->gameMode1 & (ADVENTURE_ARENA | ARCADE_MODE | BATTLE_MODE | CRYSTAL_CHALLENGE | MAIN_MENU | RELIC_RACE | TIME_TRIAL)))
        return 0;
#ifdef CTR_CUSTOM_PACKAGES
    if (MainRaceTrack_OfflineCustomLoad()) return 0;
#endif
    c = CustomTrack_StandaloneContext(gt->levelID);
    if (c && c->cup) {
        if (c->seedEpoch != ap_content_plan_epoch()) return 0;
        cup = AP_ContentCupPad();
        if (!cup || c->leg < 0 || c->leg >= 4) return 0;
        return ap_content_plan_sanity(cup->legs[c->leg].track_id);
    }
    // A boss venue owns its pickups even when a standalone context exists
    // on the same borrowed host, as for the shared lap and Wumpa routes.
    if (IS_BOSS_RACE(gt->gameMode1)) return ap_content_plan_sanity_boss(gt->bossID);
    if (c && c->seedEpoch == ap_content_plan_epoch()) {
        if (AP_StandaloneRoutePad(c) && (c->retail || CustomTrack_ServingLoad(gt->levelID, 0, 0)))
            return ap_content_plan_sanity(c->trackID);
        return 0;
    }
#else
    (void)gt;
#endif
    return 0;
}

static int AP_SanitySentIndex(long code)
{
    int low = 0, high = ap_sanity_sent_count;
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (ap_sanity_sent[mid] < code) low = mid + 1; else high = mid;
    }
    return low;
}

static int AP_SanityAlreadySent(long code)
{
    int i = AP_SanitySentIndex(code);
    if (i < ap_sanity_sent_count && ap_sanity_sent[i] == code) return 1;
    if (!ap_net_location_checked(code)) return 0;
    // Keep observed server checks for this seed too. A stopped retry cycle
    // discards network caches, but must not make a checked object open again.
    if (ap_sanity_sent_count < 16384) {
        memmove(&ap_sanity_sent[i + 1], &ap_sanity_sent[i], (ap_sanity_sent_count - i) * sizeof(long));
        ap_sanity_sent[i] = code;
        ++ap_sanity_sent_count;
    }
    return 1;
}

static int AP_SanityEnsure(struct GameTracker *gt)
{
    const ctr_content_sanity *row = AP_SanityRoute(gt);
    int counts[3] = {0, 0, 0}, i, f;
    uint64_t epoch = ap_content_plan_epoch();
    if (epoch != ap_sanity_epoch) {
        if (ap_sanity_ready > 0) for (i = 0; i < ap_sanity_size; ++i) {
            ap_sanity_objects[i].inst->colorRGBA = ap_sanity_objects[i].color;
            ap_sanity_objects[i].inst->alphaScale = ap_sanity_objects[i].alpha;
        }
        ap_sanity_epoch = epoch;
        ap_sanity_ready = ap_sanity_sent_count = 0;
        memset(ap_sanity_feed, 0, sizeof ap_sanity_feed);
        ap_sanity_feed_ticks = 0;
    }
    if (!row || !ap_sanity_defs || ap_sanity_defs_count < 0 || ap_sanity_defs_count > 512) return 0;
    if (ap_sanity_ready) return ap_sanity_ready > 0;
    ap_sanity_size = 0;
    for (i = 0; i < ap_sanity_defs_count; ++i) {
        struct InstDef *d = &ap_sanity_defs[i];
        if (!d->model) continue;
        f = d->model->id == PU_RANDOM_CRATE ? 0 : d->model->id == PU_FRUIT_CRATE ? 1 : d->model->id == PU_WUMPA_FRUIT ? 2 : -1;
        if (f < 0) continue;
        ++counts[f];
        if (counts[f] > (f == 0 ? 80 : f == 1 ? 16 : 32) || !d->ptrInstance) continue;
        if (row->locations[f][counts[f] - 1] > 0) {
            int n = ap_sanity_size++;
            ap_sanity_objects[n].inst = d->ptrInstance;
            ap_sanity_objects[n].code = (long)row->locations[f][counts[f] - 1];
            ap_sanity_objects[n].family = (unsigned char)f;
            ap_sanity_objects[n].color = d->ptrInstance->colorRGBA;
            ap_sanity_objects[n].alpha = d->ptrInstance->alphaScale;
        }
    }
    if (memcmp(counts, row->counts, sizeof counts)) {
        AP_AppendLog("[AP SANITY] pickup counts differ from the room; sanity disabled on this track\n");
        ap_sanity_size = 0; ap_sanity_ready = -1; return 0;
    }
    ap_sanity_ready = 1;
    return 1;
}

// Returns 0 only when a bot must pass through an open loose-fruit check.
int AP_SanityPickup(struct Instance *inst, struct Driver *driver)
{
    int i, bot;
    struct GameTracker *gt = sdata ? sdata->gGT : 0;
    if (!driver || !AP_SanityEnsure(gt)) return 1;
    bot = (driver->actionsFlagSet & ACTION_BOT) != 0;
    for (i = 0; i < ap_sanity_size; ++i) if (ap_sanity_objects[i].inst == inst) {
        long code = ap_sanity_objects[i].code;
        // The bound plan already verified this code against the room. A
        // stopped socket clears network membership, but offline pickups still
        // belong in the same room's held-check queue.
        if (AP_SanityAlreadySent(code)) return 1;
        if (bot) return ap_sanity_objects[i].family != 2;
        if (driver->driverID != 0 || driver != gt->drivers[0] || ap_sanity_sent_count >= 16384) return 1;
        if (AP_EmitClassCheck(code, 0, -1, -1, 0, "[AP CHECK] sanity location=%ld\n", code)) {
            int at = AP_SanitySentIndex(code);
            memmove(&ap_sanity_sent[at + 1], &ap_sanity_sent[at], (ap_sanity_sent_count - at) * sizeof(long));
            ap_sanity_sent[at] = code;
            ++ap_sanity_sent_count;
            ++ap_sanity_feed[ap_sanity_objects[i].family];
            if (!ap_sanity_feed_ticks) ap_sanity_feed_ticks = 120;
            inst->colorRGBA = ap_sanity_objects[i].color;
            inst->alphaScale = ap_sanity_objects[i].alpha;
            ap_sanity_objects[i].recolour = AP_SanityTag(AP_PAD_LOOK_DONE, AP_SANITY_VISIBILITY);
        }
        return 1;
    }
    return 1;
}

void AP_SanityTick(struct GameTracker *gt)
{
    int i, active = AP_SanityEnsure(gt);
    if (active) for (i = 0; i < ap_sanity_size; ++i) {
        int open = !AP_SanityAlreadySent(ap_sanity_objects[i].code);
        int look = AP_SanityLook(!open, AP_LocationLogicState(ap_sanity_objects[i].code));
        ap_sanity_objects[i].recolour = AP_SanityTag(look, AP_SANITY_VISIBILITY);
    }
    if (ap_sanity_feed_ticks > 0 && --ap_sanity_feed_ticks == 0) {
        static const char *const labels[] = {"WEAPON CRATES", "WUMPA CRATES", "WUMPA FRUIT"};
        for (i = 0; i < 3; ++i) if (ap_sanity_feed[i]) {
            char line[64];
            snprintf(line, sizeof line, "%d %s CHECKED", ap_sanity_feed[i], labels[i]);
            AP_FeedNoticeLine(line);
            ap_sanity_feed[i] = 0;
        }
    }
}

unsigned AP_SanityRecolourTag(struct Instance *inst)
{
    int i;
    struct GameTracker *gt = sdata ? sdata->gGT : 0;
    if (ap_sanity_ready <= 0 || !AP_SanityRoute(gt)) return 0;
    for (i = 0; i < ap_sanity_size; ++i)
        if (ap_sanity_objects[i].inst == inst) return ap_sanity_objects[i].recolour;
    return 0;
}

int AP_SanityPadPending(int physical, int family)
{
    int64_t codes[320];
    int i, n = ap_content_plan_sanity_pad(physical, family, codes, 320), pending = 0;
    for (i = 0; i < n; ++i) pending += !AP_ContentCodeChecked(codes[i]);
    return pending;
}
#endif // CTR_AP
