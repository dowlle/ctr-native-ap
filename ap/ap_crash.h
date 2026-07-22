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
//     where the platform provides one (glibc backtrace on Linux; on Windows
//     the faulting thread's context registers plus an EBP-chain walk of the
//     faulting stack, with the fault address and each frame attributed to
//     its owning module+offset -- addresses are symbolized offline against
//     the same build).
//   ctr-ap.log -- the same block is appended so the crash sits inline with
//     the [AP HUB]/[AP CHECK] breadcrumbs that led up to it.
//
// The handlers write with raw fds and pre-formatted static buffers (no malloc,
// no stdio locks beyond what a dying process can stomach) and then re-raise /
// continue the search, so default OS crash behaviour is preserved.

// The header is consumed from C (unity build) and from the ap_net C++ scope
// (the std::terminate hook lives there; see ap_crash_terminate.cpp).
#ifdef __cplusplus
extern "C" {
#endif

// Install the platform handlers. Call once from main(), right after the
// base-dir chdir (handlers write relative paths).
void AP_CrashInstall(void);

// Cheap per-frame context cache read by the handlers (a crashed process may
// not be able to walk game structures safely, so the handler only reads these
// plain ints). Call every frame from AP_OnFrame.
void AP_CrashNoteFrame(int levelID, int connected);

// Write a crash block for a std::terminate (or equivalent C++-side death)
// through the same writer as the platform handlers: same fields, plus a
// best-effort stack of the calling thread. `what` describes the cause (e.g.
// "std::terminate (St13runtime_error: ...)"). Also marks the report as
// written so the SIGABRT path that follows the handler's abort() does not
// append a second, less specific block for the same death.
void AP_CrashReportTerminate(const char *what);

// Install the std::terminate hook. Defined in ap_crash_terminate.cpp (the
// ap_net C++ scope); called by AP_CrashInstall.
void AP_CrashInstallTerminate(void);

#ifdef __cplusplus
}
#endif

#endif // CTR_AP
#endif // AP_CRASH_H
