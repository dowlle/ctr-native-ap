#ifndef NATIVE_WIN32_H
#define NATIVE_WIN32_H

#if defined(_WIN32) || defined(__CYGWIN__)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#ifndef NOUSER
#define NOUSER 1
#endif

/*
 * psx_prelude.h ends with `#define RECT RECT16`. If that alias is already
 * active when this header is first included (the authoring client's unity
 * build reaches it from ap_navrec.c, after the prelude), windows.h's
 * `typedef struct tagRECT {...} RECT;` would redefine RECT16. Suspend the
 * alias across the include, as ap/ap_crash.c does; where it is not defined
 * yet this is a no-op.
 */
#pragma push_macro("RECT")
#undef RECT
#include <windows.h>
#pragma pop_macro("RECT")

/*
 * This project is built as one large C translation unit. Keep Win32's ANSI
 * name-selection macros from rewriting PS1 SDK/game function names later.
 */
#ifdef OpenEvent
#undef OpenEvent
#endif

#ifdef LoadImage
#undef LoadImage
#endif

#endif

#endif
