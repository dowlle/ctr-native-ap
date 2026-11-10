#include <common.h>
#include <text_box.h>
#include <text_box_logic.h>

static TextBoxClock textBoxClocks[TEXT_SLOT_COUNT];

void DecalFont_ResetLineBox(int slot)
{
    if (slot >= 0 && slot < TEXT_SLOT_COUNT) textBoxClocks[slot].valid = 0;
}

int DecalFont_LineBoxOffset(const char *text, int width, s16 font, int slot)
{
    if (slot < 0 || slot >= TEXT_SLOT_COUNT) return 0;
    return TextBox_Offset(DecalFont_GetLineWidthStrlen((char *)text, -1, font),
        width, textBoxClocks[slot].ms);
}

/* Both environment packets are allocated before drawing any glyphs. If the
   arena is full, skip the line rather than leave an unpaired scissor behind. */
static int TextBox_Environments(uint32_t *ot, int x, int y, int w, int h, void **clip)
{
    struct GameTracker *gt = sdata->gGT;
    struct DB *db = gt->backBuffer;
    struct PushBuffer *pb = &gt->pushBuffer_UI;
    DRAWENV env;
    int l, t, r, b;
    unsigned char *p = db->primMem.cursor;
    if (p + 0x40 > (unsigned char *)db->primMem.guardEnd) return 0;
    for (u32 i = 0; i < sizeof env / 4; i++)
        CTR_WriteU32LE((u8 *)&env + i * 4, CTR_ReadU32LE((u8 *)&db->drawEnv + i * 4));
    env.clip.x += pb->rect.x; env.clip.y += pb->rect.y;
    env.clip.w = pb->rect.w; env.clip.h = pb->rect.h;
    env.ofs[0] += pb->rect.x; env.ofs[1] += pb->rect.y;
    env.isbg = 0;
    SetDrawEnv((void *)p, &env);
    AddPrim(ot, p); /* LIFO: restore is rendered after the glyphs. */
    TextBox_Clip(x, y, w, h, pb->rect.w, pb->rect.h, &l, &t, &r, &b);
    env.clip.x += l; env.clip.y += t;
    env.clip.w = r - l; env.clip.h = b - t;
    *clip = p + 0x40;
    SetDrawEnv(*clip, &env);
    db->primMem.cursor = p + 0x80;
    return 1;
}

void DecalFont_DrawLineBox(const char *text, int x, int y, int width,
    s16 font, int flags, int mode, int slot, uint32_t *ot)
{
    struct GameTracker *gt = sdata->gGT;
    struct PushBuffer *pb = &gt->pushBuffer_UI;
    uint32_t ms = 0;
    int len, textW;
    if (!text || width <= 0) return;
    if (!ot) ot = pb->ptrOT;
    len = (int)strlen(text);
    /* Strlen width avoids GetLineWidth's legacy signed 16-bit narrowing. */
    textW = DecalFont_GetLineWidthStrlen((char *)text, len, font);
    if (mode == TEXT_MARQUEE && slot >= 0 && slot < TEXT_SLOT_COUNT)
        ms = TextBox_Tick(&textBoxClocks[slot], TextBox_Key(text), gt->timer,
            gt->elapsedTimeMS, width, pb->rect.x, pb->rect.y, pb->rect.w,
            pb->rect.h, gt->numPlyrCurrGame);
    if (textW <= width)
    {
        DecalFont_DrawLineOT((char *)text, x, y, font, flags, ot);
        return;
    }
    /* For overflowing centered/right text x is still its original anchor. */
    if (flags & JUSTIFY_CENTER) x -= width / 2;
    else if (flags & JUSTIFY_RIGHT) x -= width;
    flags &= ~(JUSTIFY_CENTER | JUSTIFY_RIGHT);
    if (mode != TEXT_MARQUEE)
    {
        int n = 0, used = 0;
        int dots = DecalFont_GetLineWidthStrlen("...", 3, font);
        while (n < len)
        {
            int advance = DecalFont_GetLineWidthStrlen((char *)text + n, 1, font);
            if (used + advance + dots > width) break;
            used += advance; n++;
        }
        uint32_t *saved = pb->ptrOT;
        pb->ptrOT = ot;
        DecalFont_DrawLineStrlen((char *)text, n, x, y, font, flags);
        if (dots <= width) DecalFont_DrawLineOT("...", x + used, y, font, flags, ot);
        pb->ptrOT = saved;
        return;
    }
    int shift = TextBox_Offset(textW, width, ms);
    int pos = x - shift, first = -1, firstX = 0, count = 0;
    for (int i = 0; i < len; i++)
    {
        int advance = DecalFont_GetLineWidthStrlen((char *)text + i, 1, font);
        if (pos + advance > x && pos < x + width)
        {
            if (first < 0) { first = i; firstX = pos; }
            count = i - first + 1;
        }
        pos += advance;
    }
    void *clip;
    if (first < 0 || !TextBox_Environments(ot, x, y - 2, width,
        (font == FONT_BIG ? 17 : 12) + 4, &clip)) return;
    uint32_t *saved = pb->ptrOT;
    pb->ptrOT = ot;
    DecalFont_DrawLineStrlen((char *)text + first, count, firstX, y, font, flags);
    pb->ptrOT = saved;
    AddPrim(ot, clip); /* clip, glyphs, restore on the GPU. */
}
