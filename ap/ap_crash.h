#ifndef AP_CRASH_H
#define AP_CRASH_H
#ifdef CTR_AP

// Crash reporter for the AP build (support-bundle feature).
//
// Community reports arrive as "it crashed mid-race" with nothing attached --
// the game dies silently and no evidence survives. This module makes every
// crash leave two artifacts next to the exe, both collected by the
// support-bundle scripts:
//
//   ctr-ap-crash.txt -- one appended block per crash: release + engine
//     version, the signal/exception + fault address, the cached game context
//     (levelID, frame counter, AP connection state), and a raw stack trace
//     where the platform provides one (glibc backtrace on Linux,
//     CaptureStackBacktrace + module base on Windows -- addresses are
//     symbolized offline against the same build).
//   ctr-ap.log -- the same block is appended so the crash sits inline with
//     the [AP HUB]/[AP CHECK] breadcrumbs that led up to it.
//
// The handlers write with raw fds and pre-formatted static buffers (no malloc,
// no stdio locks beyond what a dying process can stomach) and then re-raise /
// continue the search, so default OS crash behaviour is preserved.

// Install the platform handlers. Call once from main(), right after the
// base-dir chdir (handlers write relative paths).
void AP_CrashInstall(void);

// Cheap per-frame context cache read by the handlers (a crashed process may
// not be able to walk game structures safely, so the handler only reads these
// plain ints). Call every frame from AP_OnFrame.
void AP_CrashNoteFrame(int levelID, int connected);

#endif // CTR_AP
#endif // AP_CRASH_H
