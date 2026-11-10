/* Layout harnesses record the initial, width-bounded text. The production
   GPU packet and moving partial-glyph path is exercised by
   test-text-box-production.py. MM_CustomText.c must be included first. */
void DecalFont_DrawLineBox(const char *text, int x, int y, int width,
    s16 font, int flags, int mode, int slot, uint32_t *ot)
{
    (void)mode; (void)slot;
    MM_CustomText_DrawFont(text, x, y, width, 1, font, flags, ot);
}
void DecalFont_ResetLineBox(int slot) { (void)slot; }
