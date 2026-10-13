#ifndef AP_NAMED_BLOCKS_H
#define AP_NAMED_BLOCKS_H
/* Named custom-track id blocks and room titles (#306).
 *
 * A custom track the apworld knows has its own permanent id block, the way
 * Cortex Vortex has one. The apworld computes every code from the block index
 * and sends it on the wire; native recomputes the code from the index it
 * receives and refuses a check whose code disagrees. Native never needs the
 * frozen name itself: names live in the datapackage.
 *
 *   location = AP_NAMED_LOCATION_BASE + block * AP_NAMED_LOCATION_BUDGET + offset
 *   item     = AP_NAMED_ITEM_BASE     + block * AP_NAMED_ITEM_BUDGET     + offset
 *
 * Offsets are append only and match the apworld's named_tracks.LOCATION_OFFSETS.
 *
 * A room title is the text the room and Universal Tracker show for a custom
 * track's checks. The apworld sanitizes it; native only checks it is plain,
 * bounded text before storing it. */
#include <stdint.h>
#include <string.h>

#define AP_NAMED_LOCATION_BASE   35200000LL
#define AP_NAMED_LOCATION_BUDGET 200
#define AP_NAMED_ITEM_BASE       35100000LL
#define AP_NAMED_ITEM_BUDGET     5
#define AP_NAMED_BLOCK_LIMIT     10000

/* Location offsets inside a named block (apworld named_tracks.LOCATION_OFFSETS). */
enum {
    AP_NAMED_OFF_TROPHY = 0,
    AP_NAMED_OFF_HELD_1ST = 1,
    AP_NAMED_OFF_HELD_3RD = 2,
    AP_NAMED_OFF_HELD_5TH = 3,
    AP_NAMED_OFF_FINISH_PODIUM = 4,
    AP_NAMED_OFF_FINISH_ANY = 5,
    AP_NAMED_OFF_RELIC_SAPPHIRE = 6,
    AP_NAMED_OFF_RELIC_GOLD = 7,
    AP_NAMED_OFF_RELIC_PLATINUM = 8,
    AP_NAMED_OFF_CTR_TOKEN = 9,
    AP_NAMED_OFF_RELIC_PERFECT = 10,
    AP_NAMED_OFF_LETTER_C = 11,
    AP_NAMED_OFF_LETTER_T = 12,
    AP_NAMED_OFF_LETTER_R = 13,
    AP_NAMED_OFF_WUMPA = 14
};

/* Longest room title in UTF-8 bytes (apworld ROOM_TITLE_MAX_BYTES), plus NUL. */
#define AP_ROOM_TITLE_MAX_BYTES 105
#define AP_ROOM_TITLE_CAP       (AP_ROOM_TITLE_MAX_BYTES + 1)

/* -1 when block or offset is outside the named ranges. */
static inline int64_t ap_named_location_code(int64_t block, int offset)
{
    if (block < 0 || block >= AP_NAMED_BLOCK_LIMIT || offset < 0 || offset >= AP_NAMED_LOCATION_BUDGET)
        return -1;
    return AP_NAMED_LOCATION_BASE + block * AP_NAMED_LOCATION_BUDGET + offset;
}

static inline int64_t ap_named_item_code(int64_t block, int offset)
{
    if (block < 0 || block >= AP_NAMED_BLOCK_LIMIT || offset < 0 || offset >= AP_NAMED_ITEM_BUDGET)
        return -1;
    return AP_NAMED_ITEM_BASE + block * AP_NAMED_ITEM_BUDGET + offset;
}

/* 1 when `text` is a storable room title: 1..105 bytes, no ASCII control
 * character, no square bracket (the apworld turns brackets into parentheses
 * so Kivy markup in Universal Tracker cannot be injected). */
static inline int ap_room_title_ok(const char *text, size_t length)
{
    if (!text || length == 0 || length > AP_ROOM_TITLE_MAX_BYTES)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c < 32 || c == 127 || c == '[' || c == ']')
            return 0;
    }
    return text[0] != ' ' && text[length - 1] != ' ';
}

#endif
