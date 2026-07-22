// std::terminate hook for the crash reporter (issue #110).
//
// A C++ exception that escapes to std::terminate (or an explicit terminate
// call) dies through abort(), a path that never reaches the SEH filter or the
// fault signals, so v0.1.3 exited with no ctr-ap-crash.txt for this crash
// class. This hook routes that death through the same writer as the platform
// handlers (AP_CrashReportTerminate in ap_crash.c) and then aborts, keeping
// the default exit behaviour.
//
// It lives here rather than in ap_crash.c because std::set_terminate needs
// C++, and ap_net is the only C++ scope in the build (the unity game build
// stays C). Compiled into the ap_net static library; AP_CrashInstall calls
// AP_CrashInstallTerminate through the C linkage declared in ap_crash.h.

#ifdef CTR_AP

#include "ap_crash.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <typeinfo>

static void ap_terminate_handler()
{
	// Re-entry guard: if describing the exception itself terminates again
	// (or a second thread races in), go straight to abort instead of
	// recursing through the reporter.
	static volatile bool entered = false;
	if (entered)
		std::abort();
	entered = true;

	// Name the active exception when that is cheap: rethrow the already
	// allocated exception object and catch it by reference. typeid().name()
	// yields the mangled name (e.g. St13runtime_error); demangling would
	// allocate, so it is left for offline reading.
	static char what[256];
	const char *msg = "std::terminate";
	if (std::exception_ptr p = std::current_exception())
	{
		try
		{
			std::rethrow_exception(p);
		}
		catch (const std::exception &e)
		{
			snprintf(what, sizeof what, "std::terminate (%s: %s)",
			         typeid(e).name(), e.what());
			msg = what;
		}
		catch (...)
		{
			msg = "std::terminate (non-std exception)";
		}
	}
	AP_CrashReportTerminate(msg);
	std::abort();
}

extern "C" void AP_CrashInstallTerminate(void)
{
	std::set_terminate(ap_terminate_handler);
}

#endif // CTR_AP
