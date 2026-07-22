// Crash reporter (support-bundle feature). See ap_crash.h for the contract.
// Unity-included after ap_hooks.c, so AP_READ_LOG and CTR_AP_VERSION are in
// scope; version defines for the engine come from the build system.

#ifdef CTR_AP

#include "ap_crash.h"
#include "ap_version.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
// This file is part of the unity build, so whatever windows.h drags in lands in
// the SAME translation unit as the PSX prelude and the decompiled game. Without
// these trims, winuser.h's ACCEL collides with the game's ACCEL enum and the
// windef.h RECT family collides with the PSX RECT16, which then cascades errors
// through zGlobal_DATA.c and native_gpu.c. Only the process/debug APIs are
// needed here, so drop GDI and USER entirely.
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#define NOMINMAX
// psx_prelude.h ends with `#ifndef RECT / #define RECT RECT16`. RECT is a
// typedef rather than a macro in the Windows headers, so that guard passes and
// the alias is already active by the time we get here. windows.h's own
// `typedef struct tagRECT {...} RECT;` would then expand to `... RECT16;` and
// redefine RECT16 as struct tagRECT, which breaks every PSX RECT16 user
// downstream. Suspend the alias across the include and restore it after, so the
// Windows headers and the rest of the unity build each see the RECT they expect.
#pragma push_macro("RECT")
#undef RECT
#include <windows.h>
// Module enumeration for address attribution (issue #119). PSAPI_VERSION 2
// resolves the Enum/GetModule* calls to the K32* exports in kernel32 (Win7+),
// so no extra import library enters the link.
#define PSAPI_VERSION 2
#include <psapi.h>
#pragma pop_macro("RECT")
#include <signal.h>
#else
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif
#endif

#define AP_CRASH_FILE "ctr-ap-crash.txt"

// Context cached per frame by AP_CrashNoteFrame; plain ints only, so the
// handler never touches game structures in a corrupted process.
static volatile int ap_crash_level = -1;
static volatile int ap_crash_connected = 0;
static volatile unsigned ap_crash_frame = 0;

// Set by AP_CrashReportTerminate: the std::terminate hook ends in abort(),
// which the SIGABRT paths below also see. The flag stops them from appending
// a second, less specific block for the same death (issue #110).
static volatile int ap_crash_terminate_noted = 0;

void AP_CrashNoteFrame(int levelID, int connected)
{
	ap_crash_level = levelID;
	ap_crash_connected = connected;
	ap_crash_frame++;
}

// Format the crash block into buf. snprintf inside a crash handler is not
// strictly async-signal-safe, but it is the accepted trade-off for crash
// reporters (breakpad et al.): the alternative is no report at all.
static int ap_crash_format(char *buf, int cap, const char *what,
                           unsigned long long addr)
{
	return snprintf(buf, cap,
	                "\n==== CTR-AP CRASH ====\n"
	                "release: %s\n"
#ifdef CTR_NATIVE_VERSION
	                "engine: %s\n"
#endif
	                "what: %s\n"
	                "addr: 0x%llx\n"
	                "levelID: %d  frame: %u  ap-connected: %d\n",
	                CTR_AP_VERSION,
#ifdef CTR_NATIVE_VERSION
	                CTR_NATIVE_VERSION,
#endif
	                what, addr,
	                ap_crash_level, ap_crash_frame, ap_crash_connected);
}

#ifndef _WIN32

// Append `buf` (len bytes) to path with raw open/write (async-signal-safe).
static void ap_crash_write_file(const char *path, const char *buf, int len)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		return;
	// A short write in a dying process is still a report; ignore the result.
	(void)!write(fd, buf, len);
#if defined(__GLIBC__)
	{
		void *frames[32];
		int n = backtrace(frames, 32);
		if (n > 0)
			backtrace_symbols_fd(frames, n, fd);
	}
#endif
	close(fd);
}

static void ap_crash_handler(int sig, siginfo_t *si, void *uctx)
{
	static char buf[512];
	const char *what = "signal";
	switch (sig)
	{
	case SIGSEGV: what = "SIGSEGV (segmentation fault)"; break;
	case SIGABRT: what = "SIGABRT (abort)"; break;
	case SIGILL:  what = "SIGILL (illegal instruction)"; break;
	case SIGFPE:  what = "SIGFPE (arithmetic fault)"; break;
	case SIGBUS:  what = "SIGBUS (bus error)"; break;
	}
	// The abort raised by the terminate hook already wrote a richer block for
	// this death; skip the duplicate but keep the re-raise below.
	if (sig != SIGABRT || !ap_crash_terminate_noted)
	{
		int len = ap_crash_format(buf, sizeof buf, what,
		                          (unsigned long long)(uintptr_t)(si ? si->si_addr : 0));
		if (len > 0)
		{
			ap_crash_write_file(AP_CRASH_FILE, buf, len);
			ap_crash_write_file(AP_READ_LOG, buf, len);
		}
	}
	(void)uctx;
	// Restore default handling and re-raise, so the OS still gets its normal
	// core/exit path and a debugger attached to the process sees the fault.
	signal(sig, SIG_DFL);
	raise(sig);
}

void AP_CrashReportTerminate(const char *what)
{
	static char buf[512];
	ap_crash_terminate_noted = 1;
	int len = ap_crash_format(buf, sizeof buf, what, 0);
	if (len > 0)
	{
		// ap_crash_write_file appends the glibc backtrace of the calling
		// thread, which at std::terminate time is still the throwing stack.
		ap_crash_write_file(AP_CRASH_FILE, buf, len);
		ap_crash_write_file(AP_READ_LOG, buf, len);
	}
}

void AP_CrashInstall(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = ap_crash_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigaction(SIGSEGV, &sa, 0);
	sigaction(SIGABRT, &sa, 0);
	sigaction(SIGILL, &sa, 0);
	sigaction(SIGFPE, &sa, 0);
	sigaction(SIGBUS, &sa, 0);
	AP_CrashInstallTerminate();
}

#else // _WIN32

#define AP_CRASH_MAX_FRAMES 32
#define AP_CRASH_MAX_MODULES 128

// Module snapshot taken at report time so every address in the block can be
// attributed to its owning module (issue #119: a faulting address inside a
// graphics driver, a system DLL, or an injected overlay all looked the same).
// Static storage only; the handler never allocates.
static HMODULE ap_crash_mods[AP_CRASH_MAX_MODULES];
static DWORD ap_crash_mod_count = 0;

static void ap_crash_snap_modules(void)
{
	DWORD needed = 0;
	ap_crash_mod_count = 0;
	if (EnumProcessModules(GetCurrentProcess(), ap_crash_mods,
	                       sizeof ap_crash_mods, &needed))
	{
		ap_crash_mod_count = needed / sizeof(HMODULE);
		if (ap_crash_mod_count > AP_CRASH_MAX_MODULES)
			ap_crash_mod_count = AP_CRASH_MAX_MODULES;
	}
}

// snprintf returns the would-be length on truncation; clamp keeps `len` a
// valid offset so every later `buf + len` / `cap - len` stays in bounds.
static int ap_crash_clamp(int len, int cap)
{
	if (len < 0)
		return 0;
	if (len > cap - 1)
		return cap - 1;
	return len;
}

// Append "name.dll+0xOFFSET" (or the raw address when no module owns it) for
// addr into buf. Best-effort: any API failure degrades to the raw address.
static int ap_crash_append_resolved(char *buf, int len, int cap,
                                    unsigned long long addr)
{
	static char path[MAX_PATH];
	HANDLE proc = GetCurrentProcess();
	len = ap_crash_clamp(len, cap);
	for (DWORD i = 0; i < ap_crash_mod_count; i++)
	{
		MODULEINFO mi;
		if (!GetModuleInformation(proc, ap_crash_mods[i], &mi, sizeof mi))
			continue;
		unsigned long long base = (unsigned long long)(uintptr_t)mi.lpBaseOfDll;
		if (addr < base || addr - base >= (unsigned long long)mi.SizeOfImage)
			continue;
		path[0] = 0;
		if (!GetModuleFileNameExA(proc, ap_crash_mods[i], path, sizeof path))
			break;
		const char *name = path;
		for (const char *p = path; *p; p++)
			if (*p == '\\' || *p == '/')
				name = p + 1;
		return ap_crash_clamp(len + snprintf(buf + len, cap - len,
		                                     " %s+0x%llx", name, addr - base),
		                      cap);
	}
	return ap_crash_clamp(len + snprintf(buf + len, cap - len, " 0x%llx", addr),
	                      cap);
}

#if defined(__i386__) || defined(_M_IX86)
// Walk the faulting thread's EBP chain (issue #119): the previous
// CaptureStackBackTrace(0, ...) walked the HANDLER's stack, so the report
// described the reporter, not the crash. Frame reads go through
// ReadProcessMemory so a torn or omitted frame pointer ends the walk instead
// of re-faulting inside the handler. Best-effort by design: frames built with
// -fomit-frame-pointer will cut the chain short, but frame 0 (the faulting
// EIP) is always real.
static USHORT ap_crash_walk_x86(DWORD eip, DWORD ebp, DWORD esp,
                                void **frames, USHORT cap)
{
	USHORT n = 0;
	DWORD fp = ebp;
	if (n < cap)
		frames[n++] = (void *)(uintptr_t)eip;
	while (n < cap)
	{
		DWORD pair[2]; // [saved ebp][return address]
		SIZE_T got = 0;
		if (fp == 0 || (fp & 3) || fp < esp)
			break;
		if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)fp,
		                       pair, sizeof pair, &got) || got != sizeof pair)
			break;
		if (pair[1] == 0)
			break;
		frames[n++] = (void *)(uintptr_t)pair[1];
		if (pair[0] <= fp) // the chain must ascend or it is garbage
			break;
		fp = pair[0];
	}
	return n;
}
#endif

// Append the shared tail of a Windows crash block: module base, stack (raw
// addresses, the pre-existing field symbolized offline) and stack-modules
// (the same frames attributed to owning modules). New fields only add lines;
// the support bundle collects the file wholesale.
static int ap_crash_append_stack(char *buf, int len, int cap,
                                 void **frames, USHORT n)
{
	len = ap_crash_clamp(len, cap);
	len = ap_crash_clamp(len + snprintf(buf + len, cap - len,
	                                    "module-base: 0x%llx\nstack:",
	                                    (unsigned long long)(uintptr_t)GetModuleHandleA(NULL)),
	                     cap);
	for (USHORT i = 0; i < n && len < cap - 24; i++)
		len = ap_crash_clamp(len + snprintf(buf + len, cap - len, " 0x%llx",
		                                    (unsigned long long)(uintptr_t)frames[i]),
		                     cap);
	if (len < cap - 16)
		len = ap_crash_clamp(len + snprintf(buf + len, cap - len,
		                                    "\nstack-modules:"),
		                     cap);
	for (USHORT i = 0; i < n && len < cap - (MAX_PATH + 24); i++)
		len = ap_crash_append_resolved(buf, len, cap,
		                               (unsigned long long)(uintptr_t)frames[i]);
	if (len < cap - 2)
		len = ap_crash_clamp(len + snprintf(buf + len, cap - len, "\n"), cap);
	return len;
}

static void ap_crash_emit(const char *buf, int len)
{
	FILE *f = fopen(AP_CRASH_FILE, "a");
	if (f) { fwrite(buf, 1, len, f); fclose(f); }
	f = fopen(AP_READ_LOG, "a");
	if (f) { fwrite(buf, 1, len, f); fclose(f); }
}

static LONG WINAPI ap_crash_seh(EXCEPTION_POINTERS *ep)
{
	static char buf[4096];
	unsigned long long addr = 0;
	unsigned code = 0;
	if (ep && ep->ExceptionRecord)
	{
		addr = (unsigned long long)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;
		code = (unsigned)ep->ExceptionRecord->ExceptionCode;
	}
	char what[64];
	snprintf(what, sizeof what, "SEH exception 0x%08x", code);
	int len = ap_crash_format(buf, sizeof buf, what, addr);
	if (len > 0 && len < (int)sizeof buf)
	{
		void *frames[AP_CRASH_MAX_FRAMES];
		USHORT n = 0;
		ap_crash_snap_modules();
		// Attribute the faulting address itself, then record the faulting
		// thread's context and walk ITS stack (not the handler's).
		len = ap_crash_clamp(len + snprintf(buf + len, sizeof buf - len,
		                                    "fault-module:"),
		                     sizeof buf);
		len = ap_crash_append_resolved(buf, len, sizeof buf, addr);
		len = ap_crash_clamp(len + snprintf(buf + len, sizeof buf - len, "\n"),
		                     sizeof buf);
#if defined(__i386__) || defined(_M_IX86)
		if (ep && ep->ContextRecord)
		{
			const CONTEXT *c = ep->ContextRecord;
			len = ap_crash_clamp(len + snprintf(buf + len, sizeof buf - len,
			                                    "context: eip=0x%08lx esp=0x%08lx ebp=0x%08lx\n",
			                                    (unsigned long)c->Eip,
			                                    (unsigned long)c->Esp,
			                                    (unsigned long)c->Ebp),
			                     sizeof buf);
			n = ap_crash_walk_x86(c->Eip, c->Ebp, c->Esp,
			                      frames, AP_CRASH_MAX_FRAMES);
		}
#endif
		// No usable context (or non-x86 build): fall back to the old
		// handler-stack capture rather than reporting nothing.
		if (n == 0)
			n = CaptureStackBackTrace(0, AP_CRASH_MAX_FRAMES, frames, NULL);
		len = ap_crash_append_stack(buf, len, sizeof buf, frames, n);
		ap_crash_emit(buf, len);
	}
	// Keep default behaviour (WER / debugger) after recording.
	return EXCEPTION_CONTINUE_SEARCH;
}

// Shared writer for deaths reported from the dying thread itself
// (std::terminate, direct abort): same block, stack captured from the caller
// (skip 1 drops this writer frame; at std::terminate time the stack below the
// handler is still the throwing stack, since the unwinder has not run yet).
static void ap_crash_report_current(const char *what)
{
	static char buf[4096];
	int len = ap_crash_format(buf, sizeof buf, what, 0);
	if (len > 0 && len < (int)sizeof buf)
	{
		void *frames[AP_CRASH_MAX_FRAMES];
		ap_crash_snap_modules();
		USHORT n = CaptureStackBackTrace(1, AP_CRASH_MAX_FRAMES, frames, NULL);
		len = ap_crash_append_stack(buf, len, sizeof buf, frames, n);
		ap_crash_emit(buf, len);
	}
}

void AP_CrashReportTerminate(const char *what)
{
	ap_crash_terminate_noted = 1;
	ap_crash_report_current(what);
}

// abort() raises SIGABRT through the CRT and never reaches the SEH filter
// (issue #110). The CRT resets the handler to SIG_DFL before calling it, so
// returning lets abort() finish killing the process as usual.
static void ap_crash_abort_handler(int sig)
{
	(void)sig;
	if (!ap_crash_terminate_noted)
		ap_crash_report_current("SIGABRT (abort)");
}

void AP_CrashInstall(void)
{
	SetUnhandledExceptionFilter(ap_crash_seh);
	signal(SIGABRT, ap_crash_abort_handler);
	AP_CrashInstallTerminate();
}

#endif // _WIN32
#endif // CTR_AP
