/* Pixel-bounded text for the native custom-content screens. Retail small
   glyphs advance 13 pixels, not the 6-8 pixels common in desktop UI fonts. */
static void MM_CustomText_DrawFont(const char *text, int x, int y, int width,
                               size_t lines, int font, int flags, uint32_t *ot)
{
    char line[512];
    size_t drawn;
    for (drawn = 0; *text && drawn < lines; drawn++)
    {
        size_t n = 0, lastSpace = 0;
        while (text[n] && text[n] != '\n' && n + 4 < sizeof line)
        {
            line[n] = text[n];
            line[n + 1] = 0;
            if (DecalFont_GetLineWidth(line, font) > width) break;
            if (text[n] == ' ') lastSpace = n;
            n++;
        }
        if (!n) break;
        if (text[n] && text[n] != '\n' && lastSpace) n = lastSpace;
        line[n] = 0;
        text += n;
        while (*text == ' ' || *text == '\n') text++;
        if (*text && drawn + 1 == lines)
        {
            for (;;)
            {
                strcpy(line + n, "...");
                if (DecalFont_GetLineWidth(line, font) <= width || !n) break;
                n--;
            }
        }
        DecalFont_DrawLineOT(line, x, y + (int)drawn * (font == FONT_BIG ? 18 : 12), font, flags, ot);
    }
}

static void MM_CustomText_Draw(const char *text, int x, int y, int width,
                               size_t lines, int flags, uint32_t *ot)
{
    MM_CustomText_DrawFont(text, x, y, width, lines, FONT_SMALL, flags, ot);
}
