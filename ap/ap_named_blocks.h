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

/* 1 when `code` is a Letter C/T/R item of any named block (item offsets 0..2).
 * Seed independent: used to present the item, never to grant it. */
static inline int ap_named_item_is_letter(int64_t code)
{
    if (code < AP_NAMED_ITEM_BASE || code >= AP_NAMED_ITEM_BASE + (int64_t)AP_NAMED_BLOCK_LIMIT * AP_NAMED_ITEM_BUDGET)
        return 0;
    return (code - AP_NAMED_ITEM_BASE) % AP_NAMED_ITEM_BUDGET <= 2;
}

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

/* Fold a UTF-8 room title to plain ASCII for in-game display (ruling of
 * 2026-10-13). The game font has no accents or other scripts, so Latin
 * letters lose their marks (Cafe for Cafe with an acute), common punctuation
 * becomes its ASCII twin, and anything else becomes '?'. Universal Tracker
 * and the room keep the full title; only the in-game text is folded. Writes
 * at most cap-1 bytes plus a NUL and returns the folded length. */
static inline size_t ap_room_title_fold_ascii(const char *utf8, char *out, size_t cap)
{
    static const char *const latin1[64] = {
        "A","A","A","A","A","A","AE","C","E","E","E","E","I","I","I","I",
        "D","N","O","O","O","O","O","x","O","U","U","U","U","Y","Th","ss",
        "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",
        "d","n","o","o","o","o","o","/","o","u","u","u","u","y","th","y"};
    static const char ext_a[] =
        "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
    size_t n = 0;
    if (!out || cap == 0)
        return 0;
    const unsigned char *p = (const unsigned char *)(utf8 ? utf8 : "");
    while (*p && n + 1 < cap) {
        uint32_t cp = *p;
        int len = 1;
        if (cp >= 0xF0 && cp < 0xF8) { cp &= 0x07; len = 4; }
        else if (cp >= 0xE0) { cp &= 0x0F; len = 3; }
        else if (cp >= 0xC0) { cp &= 0x1F; len = 2; }
        else if (cp >= 0x80) { cp = 0xFFFD; }
        int ok = 1;
        for (int i = 1; i < len; ++i) {
            if ((p[i] & 0xC0) != 0x80) { ok = 0; len = i; break; }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        p += len;
        if (!ok)
            cp = 0xFFFD;
        const char *rep = 0;
        char single[2] = {0, 0};
        if (cp < 0x80) { single[0] = (char)cp; rep = single; }
        else if (cp >= 0xC0 && cp <= 0xFF) rep = latin1[cp - 0xC0];
        else if (cp >= 0x100 && cp <= 0x17F) { single[0] = ext_a[cp - 0x100]; rep = single; }
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0x2032) rep = "'";
        else if (cp == 0x201C || cp == 0x201D) rep = "\"";
        else if (cp >= 0x2010 && cp <= 0x2015) rep = "-";
        else if (cp == 0x2026) rep = "...";
        else if (cp == 0x00A0) rep = " ";
        else rep = "?";
        for (const char *r = rep; *r && n + 1 < cap; ++r)
            out[n++] = *r;
    }
    out[n] = '\0';
    return n;
}

#endif
