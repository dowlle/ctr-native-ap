#ifndef AP_CONTENT_PLAN_H
#define AP_CONTENT_PLAN_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Standalone track entries with their Trophy Race, CTR Challenge and Relic
 * Race modes (#133 PR 3), per-track check families (#133 PR 3b), Cortex
 * Vortex as a track and Relic Race Perfect checks (#133 PR 3c), character
 * unlocks and racer-locked pads (#133 PR 4b), and Crystal Challenge entries. Stored records own
 * their text; callers copy a record before crossing a load boundary. A
 * physical pad is never recovered by inverting its borrowed engine level. */
#define AP_CONTENT_MODE_TROPHY 0
#define AP_CONTENT_MODE_CTR 1
#define AP_CONTENT_MODE_RELIC 2
#define AP_CONTENT_MODE_COUNT 3
/* One leg of a content-plan cup (#133 PR 5). */
#define AP_CONTENT_CUP_LEGS 4
typedef struct ctr_content_leg {
    int retail_id;     /* engine track 0..15, or -1 */
    int cortex;        /* 1 for Cortex Vortex */
    int package_index; /* custom package, or -1 */
    int custom_slot, named_block, laps;
    int64_t victory;   /* `<Track>: Cup Leg Victory`, 0 when none */
    char leg_id[65], track_id[65];
} ctr_content_leg;
typedef struct ctr_content_pad {
    int physical, hub, keys, occupied;
    /* Manual stage-1 rule (#463), the resolved `manual_pad_requirements_v1`
     * record for this pad. rule_mask has bit i set for item 35010000 + i; the
     * rule is met when the received counts of the masked items sum to at least
     * rule_count (one bit is a concrete item, several are an Any rule over the
     * items the seed has as base items). has_rule 0 is a free pad: only the hub
     * Key floor (`keys`) applies. */
    int has_rule, rule_count;
    unsigned rule_mask;
    int retail_id; /* -1 custom or Cortex Vortex, otherwise engine track/arena LevelID */
    /* Cortex Vortex (#133 PR 3c): the bundled pair under its own identity,
     * raced on AP_CONTENT_CORTEX_HOST with no package row and no custom slot
     * (package_index -1, custom_slot 0). */
    int cortex;
    int custom_slot, package_index, laps;
    int crystal, target, time_limit_ticks, crystal_count;
    int64_t crystal_locations[20];
    int named_block; /* -1 generic slot ids, otherwise 0..9999 (#306) */
    /* Crystal Challenge completion code, or the Trophy Race code of a track
     * entry with a Trophy mode; 0 for a track entry without one. */
    int64_t trophy_location;
    /* Track modes in the pad's resolved order (#133 PR 3): modes[0] opens with
     * the pad (stage 1); a later mode is stage 2 when stages[i] == 2, and then
     * also needs the pad's second-stage rule (stage2_mask/stage2_count, same
     * encoding as rule_mask). mode_count is 0 for a Crystal Challenge. */
    int mode_count, modes[AP_CONTENT_MODE_COUNT], stages[AP_CONTENT_MODE_COUNT];
    int has_stage2, stage2_count;
    unsigned stage2_mask;
    /* `pad_requirements_v1` (one generator row 3): a later mode counts only
     * after the pad's first race is won, ap_content_first_race_won. */
    int first_race_rule;
    int64_t ctr_location;         /* CTR Token Challenge, 0 without the mode */
    int64_t relic_locations[3];   /* Sapphire, Gold, Platinum Time Trial, 0 where the seed has no such check */
    /* The frozen Sapphire code of a Relic Race (one generator row 2). It names
     * the race's route when tier checks follow the item counts and the track's
     * Sapphire check, or every tier check, is not in the seed. Never sent. */
    int64_t relic_route;
    /* Per-track check families (#133 PR 3b), 0 when the seed or track has
     * none. rung_locations: the podium rungs of the Trophy Race in SLOT_ORDER
     * (held 1st, 3rd, 5th, finish on podium, finish any). wumpa_location: the
     * track's own Reach 10 Wumpa check (`wumpa: per_track`). wumpa_modes: bit
     * per mode with a Wumpa route (Trophy and CTR, on a track that can reach
     * ten fruit), also for `wumpa: global`. letter_locations: the C/T/R
     * letter checks of the CTR Challenge (Lettersanity modes 1 and 2).
     * letter_items: the C/T/R letter items the seed pools for it (modes 2 and
     * 3); the CTR Challenge then pays only with the pooled letters received. */
    int64_t rung_locations[5];
    int64_t wumpa_location;
    unsigned wumpa_modes;
    int64_t letter_locations[3];
    int64_t letter_items[3];
    /* Relic Race Perfect (#133 PR 3c, #453): the check for breaking every time
     * crate in this pad's Relic Race; 0 when the seed or entry has none. */
    int64_t perfect_location;
    /* ctr_first (#208 on content plans, J11): 1 when the plan's
     * adventure_structure is ctr_first and this track entry runs it. The entry
     * then has no Trophy mode (modes are CTR, then Relic), trophy_location is
     * still the track's Trophy Race code, paid by a qualifying CTR win, and
     * rung_locations are earned by finishing place in the CTR Challenge. */
    int ctr_first;
    /* Racer lock (#133 PR 4b): the engine character id this pad requires, or
     * -1. ANDed onto the pad's gates by AP_ContentPadOpen; the parser also
     * mirrors it into ctr_cfg.racer_lock / gem_cup_racer_lock, so the pad's
     * racer line and the warp-commit seating read it like a normal room's. */
    int racer_lock;
    /* Capability packs (#133 PR 4c): the Progressive Boost ranks this pad's
     * Trophy Race and CTR Token Challenge need (the plan checks' `boost`), 0
     * when none. Bound to racer_lock when it is set. */
    int trophy_boost, ctr_boost;
    /* Item Box Locations (#133 PR 4e): the track's box checks, by 0-based
     * slot (the name is "Item Box <slot + 1>"), 0 where the slot has none.
     * Retail tracks keep their frozen codes (35014000 block), a named-block
     * custom track uses offsets 20..34 for its first box_count slots, and a
     * generic slot or Cortex Vortex has none. box_count is the highest slot
     * with a code. A custom track's boxes stand on its own weapon crates
     * (ap_box_seat_logic.h); box_crates is the package's measured weapon
     * crate count those seats are taken from (0 when it has no boxes). */
    int box_count, box_crates;
    int64_t box_locations[15];
    char entry_id[65], track_id[65], display_name[129];
    char room_title[129]; /* what the room and Universal Tracker call this track */
    /* Cups (#133 PR 5): a four-leg cup entry. cup_index is its name in the
     * 27-name pool; trophy_location is then `<Cup>: Win` (trophy_boost its
     * rank), sweep_location `<Cup>: Win All Legs` or 0, and track_id the
     * check owner `cup:<index>`. mode_count is 0. Each leg names the track it
     * races (as a standalone pad would: retail_id, cortex, or package_index
     * with custom_slot and named_block), its laps and its `Cup Leg Victory`
     * code, 0 when the track has a standalone Trophy Race.
     * gem_cup (one generator row 2): the engine LevelID of an original Gem Cup,
     * 100 (Red) to 104 (Purple), else 0. cup_index is then -1, trophy_location
     * `<Colour> Gem Cup: Gem`, sweep_location its Win All Legs, track_id
     * `gem_cup:<colour>`. The cup still runs on engine cup 0. */
    int cup, cup_index, gem_cup, sweep_boost;
    int64_t sweep_location;
    ctr_content_leg legs[AP_CONTENT_CUP_LEGS];
} ctr_content_pad;

/* Open admission (#133 PR 2): any complete descriptor, not only compiled
 * revisions. Bytes are located and verified on load, and a claimed capability
 * the bytes do not have refuses the package (ap_content_capability.h). */
#define AP_CONTENT_MAX_PACKAGES 32
/* Descriptor capability flags (worlds/ctr/custom_packages.py CAPABILITIES). */
#define AP_CONTENT_CAP_TIME_TRIAL 0x01u
#define AP_CONTENT_CAP_ARCADE 0x02u
#define AP_CONTENT_CAP_RELIC 0x04u
#define AP_CONTENT_CAP_CTR_TOKENS 0x08u
#define AP_CONTENT_CAP_WUMPA 0x10u
#define AP_CONTENT_CAP_ALL 0x1fu
/* Optional `super_turbo_pads` (#133 PR 4c): not one of the mode flags above.
 * The package states whether its LEV has a super turbo pad trigger quad;
 * native measures the bytes and refuses a stated value they disagree with. */
#define AP_CONTENT_SUPER_PADS_UNSTATED (-1)
#define AP_CONTENT_RETAIL_LAPS 3 /* retail tracks race their authored three laps */
/* Engine hosts of non-retail track entries: a custom package borrows Roo's
 * Tubes; Cortex Vortex keeps the Oxide Station slot its bundled pair is
 * served on by the legacy pad track and the Oxide 2 venue. */
/* Boss cups (#133 PR 6b): the cup pad of ordinary garage g is
 * AP_CONTENT_GARAGE_CUP_PAD + g, above every physical pad id (pad ids are
 * engine pad LevelIDs, the Cup room's up to 104). */
#define AP_CONTENT_GARAGE_CUP_PAD 1000
#define AP_CONTENT_CUSTOM_HOST 6
#define AP_CONTENT_CORTEX_HOST 13
#define AP_CONTENT_CORTEX_LAPS 3
typedef struct ctr_content_package {
    char id[65], content_id[65], uuid[37], revision[65];
    char display_name[129], author[129], lev_sha256[65], vrm_sha256[65];
    uint32_t lev_bytes, vrm_bytes;
    int laps;              /* authored laps, 1..7 */
    unsigned capabilities; /* AP_CONTENT_CAP_* bits the descriptor claims */
    int has_relic_targets; /* relic targets in ticks (960 per second) */
    int relic_targets[3];  /* sapphire, gold, platinum */
    int super_turbo_pads;  /* AP_CONTENT_SUPER_PADS_UNSTATED, 0 none, 1 at least one */
    /* Optional measured `weapon_crates` (#133 PR 4e): how many weapon crates
     * (PU_RANDOM_CRATE instances) the LEV holds, or
     * AP_CONTENT_WEAPON_CRATES_UNSTATED. Native counts them on load and
     * refuses a stated count the bytes disagree with. */
    int weapon_crates;
    /* Optional measured `checkpoints` and `ai_nav` (#133 PR 6b): the LEV's
     * restart point count (1..255) and whether it has AI navigation paths,
     * or AP_CONTENT_UNSTATED. Native measures both on load and refuses a
     * stated value the bytes disagree with. With both a track may be a
     * boss venue under Boss Track Shuffle. */
    int checkpoints, ai_nav;
} ctr_content_package;
#define AP_CONTENT_UNSTATED (-1)
#define AP_CONTENT_WEAPON_CRATES_UNSTATED (-1)
#define AP_CONTENT_WEAPON_CRATES_MAX 65536 /* the verifier's instance-table ceiling */

/* Capability packs (#133 PR 4c): the plan's `capability` record. `present`
 * is 0 when Progressive Boost and Progressive Stats are both off. Modes are
 * 0 off, 1 shared, 2 per racer; difficulty and knowledge 0 easy to 2 hard. */
typedef struct ctr_content_capability {
    int present, boost_mode, boost_blue_fire, stats_mode, logic_difficulty, shortcut_knowledge;
} ctr_content_capability;

/* Item Box Locations (#133 PR 4e): the plan's `item_boxes` record. `present`
 * is 0 when they are off; `shortcut_knowledge` (0 easy to 2 hard) decides
 * which retail box slots exist. */
typedef struct ctr_content_item_boxes {
    int present, shortcut_knowledge;
} ctr_content_item_boxes;

/* Boss garages (#133 PR 6): the plan's `bosses` record. `present` is 0 when
 * the garages are empty (bosses off); then oxide_goal is 3 (disabled) and
 * bosses_required 0. venues[g] is the engine LevelID ordinary garage g races
 * on (its home track unless track_shuffle); final_track is
 * CTR_CFG_OXIDE_FINAL_CORTEX_VORTEX (0) or _OXIDE_STATION (1), -1 when Oxide
 * is disabled. The parser drives the normal room's garage, Oxide and goal
 * configuration from it (ctr_cfg.boss_req from the garage roll, goal_oxide, goal_bosses,
 * oxide_final_*, ctr_boss_encounters). */
typedef struct ctr_content_bosses {
    int present, oxide_goal, bosses_required, oxide_1_optional;
    int final_unlock, final_count, track_shuffle, repeats;
    int venues[4], final_track;
    /* #133 PR 6b: the mixed-item garage roll. requirement_level 0 standard,
     * 1 high, 2 very_high, 3 chaotic (the apworld draws; native checks the
     * result). garage_req_* is each ordinary garage's requirement as a
     * ctr_req (type, count, colour) for AP_BossReqMet. */
    int requirement_level;
    int garage_req_type[4], garage_req_count[4], garage_req_colour[4];
    /* #133 PR 6b: a garage venue on custom bytes. venue_package[g] is the
     * package index (-1 none), venue_cortex[g] 1 for Cortex Vortex; then
     * venues[g] is the garage's home LevelID, the host the bytes are served
     * on, and venue_points[g] the certified restart point count. */
    int venue_package[4], venue_cortex[4], venue_points[4];
    /* #133 PR 6b: boss cups. cup[g] 1 when ordinary garage g holds a boss
     * cup (its legs are the cup pad ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD
     * + g)); cup_points[g][n] each leg's certified restart point count. */
    int cup[4], cup_points[4][4];
    /* J11 (decisions 7 and 9): boss opponent and kit shuffle on a content
     * plan. `extended` is 1 when the record carries the opponent and kit keys
     * (any of opponent_shuffle, kit_shuffle, oxide_attack_profile
     * all_compatible). opponent[g] is the engine character id garage g races
     * (its own boss without opponent_shuffle), final_opponent the one racing
     * both N. Oxide encounters (15 unless drawn, -1 when Oxide is disabled).
     * kit_intensity 0 relaxed .. 3 mayhem (1 without the kit switch),
     * kit_assignment 0 mixed_loadouts, 1 signature_profiles. home_venues 1
     * with track_assignment character_home_track. The composed schedules are
     * filled into ctr_boss_encounters exactly as an ordinary version 2 room's. */
    int extended, opponent_shuffle, oxide_in_pool, kit_shuffle, gauntlet, oxide_profile_all, home_venues;
    int kit_assignment, kit_intensity, loss_easing;
    int opponent[4], final_opponent;
} ctr_content_bosses;

/* Wumpa Shop and Wumpa goal on a content plan (J11). The plan's `wumpa_shop`
 * record and its `wumpa_banked` goal arm; the parser also fills the ordinary
 * ctr_wumpa_shop configuration from them, so the runtime reads that. */
typedef struct ctr_content_wumpa {
    int shop_slots;  /* per hub, 0 without the record */
    int prices;      /* wumpa_shop_prices percent, 25..400 */
    int excluded;    /* progression "excluded" */
    int hints;
    int goal;        /* the wumpa_banked arm's count, 0 without one */
} ctr_content_wumpa;

typedef struct ctr_content_item {
    int base, extra, start_from_pool, start_additional, remaining, receipt_cap;
} ctr_content_item;

/* The plan's per-track check settings (#133 PR 3b): `lettersanity` 0 off,
 * 1 locations, 2 locations and items, 3 items; `wumpa` 0 off, 1 global,
 * 2 per track, with the global check's code. `relic_perfect` 1 when Relic
 * Race Perfect checks are on (#133 PR 3c). */
typedef struct ctr_content_checks {
    int podium, lettersanity, letters_per_track, wumpa;
    int64_t wumpa_global;
    int relic_perfect;
} ctr_content_checks;

/* The engine LevelID a track entry races on. */
static inline int ap_content_pad_host(const struct ctr_content_pad *pad)
{
    return pad->retail_id >= 0 ? pad->retail_id : pad->cortex ? AP_CONTENT_CORTEX_HOST : AP_CONTENT_CUSTOM_HOST;
}
/* The engine LevelID a cup leg races on (#133 PR 5). */
static inline int ap_content_leg_host(const struct ctr_content_leg *leg)
{
    return leg->retail_id >= 0 ? leg->retail_id : leg->cortex ? AP_CONTENT_CORTEX_HOST : AP_CONTENT_CUSTOM_HOST;
}

/* The single pad-entry evaluator for the content-plan profile. Display, pad
 * state, capture, the final load gate and the tracker launch all call it, so
 * the answers cannot diverge. `counts` is indexed by item offset (the 15
 * AP_IDX_* tallies). */
static inline int ap_content_rule_owned(const ctr_content_pad *pad, const int *counts)
{
    int i, owned = 0;
    for (i = 0; i < 15; ++i)
        if (pad->rule_mask & (1u << i)) owned += counts[i];
    return owned;
}
static inline int ap_content_pad_open(const ctr_content_pad *pad, const int *counts)
{
    return pad && pad->occupied && counts[14] >= pad->keys &&
           (!pad->has_rule || ap_content_rule_owned(pad, counts) >= pad->rule_count);
}
/* Position of a mode in the pad's order, or -1 when the entry lacks it. */
static inline int ap_content_mode_slot(const ctr_content_pad *pad, int mode)
{
    int i;
    for (i = 0; pad && i < pad->mode_count && i < AP_CONTENT_MODE_COUNT; ++i)
        if (pad->modes[i] == mode) return i;
    return -1;
}
static inline int ap_content_stage2_owned(const ctr_content_pad *pad, const int *counts)
{
    int i, owned = 0;
    for (i = 0; i < 15; ++i)
        if (pad->stage2_mask & (1u << i)) owned += counts[i];
    return owned;
}
/* The check codes a mode owns on this pad (Trophy 1, CTR 1, Relic 3); 0 when
 * the entry lacks the mode. */
static inline int ap_content_mode_codes(const ctr_content_pad *pad, int mode, int64_t out[3])
{
    if (ap_content_mode_slot(pad, mode) < 0) return 0;
    if (mode == AP_CONTENT_MODE_TROPHY) { out[0] = pad->trophy_location; return 1; }
    if (mode == AP_CONTENT_MODE_CTR) { out[0] = pad->ctr_location; return 1; }
    out[0] = pad->relic_locations[0]; out[1] = pad->relic_locations[1]; out[2] = pad->relic_locations[2];
    return 3;
}
/* Server checked-state of a content-plan code. */
typedef int (*ap_content_checked_fn)(int64_t code);
/* The pad's first race is won: a check of its first mode is checked, as v0.2.x
 * reads the Trophy Race. A first race with no check of its own (a Relic Race
 * with no tier check) proves nothing, so then only the items count, as in the
 * apworld's logic (content_plan.create_regions). A ctr_first entry's win is its
 * Trophy Race check: the CTR Token check needs the letters on top. */
static inline int ap_content_first_race_won(const ctr_content_pad *pad, ap_content_checked_fn checked)
{
    int64_t codes[3] = {0, 0, 0};
    int i, n, any = 0;
    if (!pad->first_race_rule || pad->mode_count < 2) return 1;
    if (pad->ctr_first && pad->trophy_location > 0) return checked(pad->trophy_location);
    n = ap_content_mode_codes(pad, pad->modes[0], codes);
    for (i = 0; i < n; ++i) {
        if (codes[i] <= 0) continue;
        any = 1;
        if (checked(codes[i])) return 1;
    }
    return !any;
}
/* The single mode gate: the pad is open, a later mode's pad has won its first
 * race (ruled 2026-10-30), and a stage-2 mode's second rule is met. Pad entry,
 * the mode selector, the race-start gate and the tracker all call this. */
static inline int ap_content_mode_open(const ctr_content_pad *pad, int mode, const int *counts,
                                       ap_content_checked_fn checked)
{
    const int i = ap_content_mode_slot(pad, mode);
    if (i < 0 || !ap_content_pad_open(pad, counts)) return 0;
    if (i && !ap_content_first_race_won(pad, checked)) return 0;
    return pad->stages[i] == 1 || (pad->has_stage2 && ap_content_stage2_owned(pad, counts) >= pad->stage2_count);
}

/* The code that names a mode's route in its race context: its first check,
 * and for a Relic Race the track's frozen Sapphire code whichever tier checks
 * the seed holds. Never a check to send on its own for a Relic Race. */
static inline int64_t ap_content_mode_route(const ctr_content_pad *pad, int mode)
{
    int64_t codes[3] = {0, 0, 0};
    if (ap_content_mode_codes(pad, mode, codes) < 1) return 0;
    return mode == AP_CONTENT_MODE_RELIC && pad->relic_route > 0 ? pad->relic_route : codes[0];
}

/* Parsed != admitted. Net admission must bind the exact room location union
 * and identity before any gameplay consumer can obtain a pad or package. */
int ap_content_plan_present(void);
int ap_content_plan_active(void);
uint64_t ap_content_plan_epoch(void);
const ctr_content_pad *ap_content_plan_pad(int physical);
const ctr_content_package *ap_content_plan_package(int index);
/* 1 when the active plan has a Cortex Vortex track entry (#133 PR 3c). */
int ap_content_plan_cortex(void);
const ctr_content_item *ap_content_plan_item(int64_t item);
const ctr_content_checks *ap_content_plan_checks(void);
/* Capability packs (#133 PR 4c). The record (all zero without one); the
 * from-pool plus additional starting copies of any ledger item; and the boost
 * rank a check needs (0 when none or not a plan check). */
const ctr_content_capability *ap_content_plan_capability(void);
int ap_content_plan_start_count(int64_t item);
int ap_content_plan_check_boost(int64_t location);
/* Item Box Locations (#133 PR 4e): the record (all zero without one). */
const ctr_content_item_boxes *ap_content_plan_item_boxes(void);
int ap_content_plan_goal(const int received_gems[5]);
/* The plan's Gem arm (#133 PR 6): how many of the goal's distinct base Gem
 * colours are held, the count the Oxide garage's companion term compares
 * with ctr_cfg.goal_gems. */
int ap_content_plan_gems_held(const int received_gems[5]);
/* Boss garages (#133 PR 6): the record (all zero without one). */
const ctr_content_bosses *ap_content_plan_bosses(void);
/* J11: the Wumpa Shop and goal record (all zero without one), and 1 when the
 * plan's adventure_structure is ctr_first. */
const ctr_content_wumpa *ap_content_plan_wumpa(void);
int ap_content_plan_ctr_first(void);
/* Completability sweep (#133 PR 8, ap_content_verify.hpp): the plan's own
 * logic graph evaluated to a fixed point from the receipts that are not this
 * world's own location rewards (other players, starting inventory, server
 * grants), banking each reached own location's scouted item. `foreign`
 * returns that receipt count for an item code; `scout` returns 0 for a
 * location the room does not have, else 1 with *own_item the item it holds for
 * this slot (0 when the item belongs to another player); `checked` 1 for a
 * checked location; `won`, when set, 1 for an encounter (by its completion
 * location) the player personally won, which a checked location alone does
 * not prove (Collect and Release check it too); `unreached`, when set, is
 * called for every location of the room the sweep does not reach. Returns 0 with no verdict when no plan is
 * active or the model could not be built. */
#define AP_CONTENT_VERIFY_LISTED 16
typedef struct ap_content_verify_io {
    void *ctx;
    int (*foreign)(void *ctx, int64_t item);
    int (*scout)(void *ctx, int64_t location, int64_t *own_item);
    int (*checked)(void *ctx, int64_t location);
    int (*won)(void *ctx, int64_t completion_location);
    void (*unreached)(void *ctx, int64_t location);
} ap_content_verify_io;
typedef struct ap_content_verify_result {
    int goal, total, reachable, keys, unreachable_count;
    int64_t unreachable[AP_CONTENT_VERIFY_LISTED];
} ap_content_verify_result;
int ap_content_plan_verify(const ap_content_verify_io *io, ap_content_verify_result *out);
int ap_content_plan_bind(const char *seed, int team, int slot,
                         const int64_t *locations, size_t count);

#ifdef __cplusplus
}
#endif
#endif
