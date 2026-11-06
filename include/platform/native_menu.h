#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Only pass authored button hints, never names, paths or other user text.
   Returned storage lasts for eight calls. Measure the returned text too. */
char *NativeMenu_Labels(const char *hint);

static inline char NativeMenu_LabelGlyph(char c, bool letters)
{
    if (!letters) return c;
    switch (c) {
    case '*': return 'A';
    case '@': return 'B';
    case '[': return 'X';
    case '^': return 'Y';
    default: return c;
    }
}

/* A changed row, value, page or any other action disarms a pending reset. */
static inline bool NativeMenu_ResetPress(int *armedRow, int row, bool reset, bool changed)
{
    if (changed || *armedRow != row) *armedRow = -1;
    if (!reset) return false;
    if (*armedRow == row) { *armedRow = -1; return true; }
    *armedRow = row;
    return false;
}
