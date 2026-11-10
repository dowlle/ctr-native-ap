#ifndef CTR_TEXT_BOX_LOGIC_H
#define CTR_TEXT_BOX_LOGIC_H

#include <stdint.h>

typedef struct {
    uint32_t key, ms, frame;
    uint32_t ticks, tickRemainder;
    int width, viewportX, viewportY, viewportW, viewportH, players, valid;
} TextBoxClock;

static inline uint32_t TextBox_Key(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static inline unsigned TextBox_Cycle(int textW, int boxW)
{
    return textW > boxW ? 2500u + (unsigned)(textW - boxW) * 25u : 0;
}

static inline int TextBox_Offset(int textW, int boxW, uint32_t ms)
{
    unsigned cycle = TextBox_Cycle(textW, boxW), t;
    int overflow = textW - boxW;
    if (!cycle) return 0;
    t = ms % cycle;
    if (t <= 1500) return 0;
    t = (t - 1500) / 25; /* 40 pixels per second. */
    return t < (unsigned)overflow ? (int)t : overflow;
}

/* Called once per drawn frame, even for fitting text. Never advances twice in
   one frame. A hidden frame, new text/box or viewport starts the first hold. */
static inline uint32_t TextBox_Tick(TextBoxClock *s, uint32_t key, uint32_t frame,
    int elapsed, int width, int vx, int vy, int vw, int vh, int players)
{
    if (!s->valid || s->key != key || frame - s->frame > 1 || s->width != width ||
        s->viewportX != vx || s->viewportY != vy || s->viewportW != vw ||
        s->viewportH != vh || s->players != players)
        s->ms = 0;
    else if (frame != s->frame)
        s->ms += (unsigned)(elapsed < 0 ? 0 : elapsed > 64 ? 64 : elapsed);
    s->valid = 1;
    s->key = key; s->frame = frame; s->width = width;
    s->viewportX = vx; s->viewportY = vy; s->viewportW = vw;
    s->viewportH = vh; s->players = players;
    return s->ms;
}

/* The NTSC presentation clock continues during pause and is reproduced by
   the native replay scheduler's VSync packets. Retain fractional ms so 30
   and 60 fps accumulate exactly the same time. UI frame identity is separate
   from the gameplay timer, which stops under PAUSE_ALL. */
static inline uint32_t TextBox_TickVBlank(TextBoxClock *s, uint32_t key, uint32_t frame,
    uint32_t ticks, int width, int vx, int vy, int vw, int vh, int players)
{
    unsigned elapsed = 0;
    if (!s->valid || frame != s->frame)
    {
        unsigned delta = ticks - s->ticks;
        if (!s->valid || frame - s->frame > 1) delta = s->tickRemainder = 0;
        if (delta > 4) delta = 4;
        unsigned units = delta * 1000 + s->tickRemainder;
        elapsed = units / 60;
        s->tickRemainder = units % 60;
        s->ticks = ticks;
    }
    return TextBox_Tick(s, key, frame, (int)elapsed, width, vx, vy, vw, vh, players);
}

/* Intersect with the UI viewport, including negative fly-in coordinates. */
static inline void TextBox_Clip(int x, int y, int w, int h, int vw, int vh,
    int *left, int *top, int *right, int *bottom)
{
    *left = x < 0 ? 0 : x > vw ? vw : x;
    *top = y < 0 ? 0 : y > vh ? vh : y;
    *right = x + w < *left ? *left : x + w > vw ? vw : x + w;
    *bottom = y + h < *top ? *top : y + h > vh ? vh : y + h;
}
#endif
