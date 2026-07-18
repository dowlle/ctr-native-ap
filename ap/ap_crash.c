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
#include <windows.h>
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
	int len = ap_crash_format(buf, sizeof buf, what,
	                          (unsigned long long)(uintptr_t)(si ? si->si_addr : 0));
	if (len > 0)
	{
		ap_crash_write_file(AP_CRASH_FILE, buf, len);
		ap_crash_write_file(AP_READ_LOG, buf, len);
	}
	(void)uctx;
	// Restore default handling and re-raise, so the OS still gets its normal
	// core/exit path and a debugger attached to the process sees the fault.
	signal(sig, SIG_DFL);
	raise(sig);
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
}

#else // _WIN32

static LONG WINAPI ap_crash_seh(EXCEPTION_POINTERS *ep)
{
	static char buf[1024];
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
		// Module base + raw frame addresses: offsets against the shipped exe
		// are symbolized offline against the same build's map/objects.
		void *frames[32];
		USHORT n = CaptureStackBacktrace(0, 32, frames, NULL);
		len += snprintf(buf + len, sizeof buf - len, "module-base: 0x%llx\nstack:",
		                (unsigned long long)(uintptr_t)GetModuleHandleA(NULL));
		for (USHORT i = 0; i < n && len < (int)sizeof buf - 24; i++)
			len += snprintf(buf + len, sizeof buf - len, " 0x%llx",
			                (unsigned long long)(uintptr_t)frames[i]);
		if (len < (int)sizeof buf - 2)
			len += snprintf(buf + len, sizeof buf - len, "\n");
		FILE *f = fopen(AP_CRASH_FILE, "a");
		if (f) { fwrite(buf, 1, len, f); fclose(f); }
		f = fopen(AP_READ_LOG, "a");
		if (f) { fwrite(buf, 1, len, f); fclose(f); }
	}
	// Keep default behaviour (WER / debugger) after recording.
	return EXCEPTION_CONTINUE_SEARCH;
}

void AP_CrashInstall(void)
{
	SetUnhandledExceptionFilter(ap_crash_seh);
}

#endif // _WIN32
#endif // CTR_AP
