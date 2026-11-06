#pragma once
#include <platform/native_menu.h>
/* Renderer-only harnesses select the pad family without linking SDL. The
   dedicated menu-label harness exercises the real SDL/config adapter. */
static bool menuTestLetters;
char *NativeMenu_Labels(const char *hint)
{
    static char text[8][1024];
    static unsigned next;
    char *out = text[next++ % 8];
    size_t i = 0;
    for (; hint[i] && i + 1 < sizeof text[0]; ++i)
        out[i] = NativeMenu_LabelGlyph(hint[i], menuTestLetters);
    out[i] = 0;
    return out;
}
