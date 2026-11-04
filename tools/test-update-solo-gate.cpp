// g++ -m32 -std=c++17 -Wall -Wextra -pthread -DASIO_STANDALONE -I . -I ap -I ap/vendor/json/include -I ap/vendor/asio/include -o /tmp/test-update-solo-gate tools/test-update-solo-gate.cpp ap/ap_update_core.cpp -lssl -lcrypto -lz
// /tmp/test-update-solo-gate
//
// The signed updater and Play Solo (issues #301 and #89). The real worker
// (ap/ap_update_net.cpp) and trust core run on a temporary install with a
// staged update; only Play Solo's two state functions are faked:
//
//   * a solo session in any active state, or a running solo job, refuses the
//     install: the result is -1, the status stays READY, and not one installed
//     or staged file changes, solo-runtime/ included
//   * a finished, failed or idle job and no session do not hold it back
//   * the install then replaces solo-runtime/ as one folder and leaves the
//     player's solo/ folder alone
#include "ap/ap_update_net.cpp"

#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>

static int g_session_active, g_job_state;
extern "C" int AP_Solo_SessionActive(void) { return g_session_active; }
extern "C" int AP_Solo_JobState(void) { return g_job_state; }

static int checks, failures;
static void expect(bool good, const char *why)
{
	++checks;
	if (!good)
	{
		++failures;
		std::printf("FAIL: %s\n", why);
	}
}

static void put(const std::string &path, const std::string &text)
{
	std::ofstream(path, std::ios::binary) << text;
}
static std::string get(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream all;
	all << in.rdbuf();
	return in ? all.str() : "<missing>";
}
static InstallFile file(const std::string &rel, const std::string &text, bool executable = false)
{
	InstallFile f;
	f.rel = rel;
	f.data.assign(text.begin(), text.end());
	f.sha256 = Sha256Hex(f.data);
	f.executable = executable;
	return f;
}

int main()
{
	char base[] = "/tmp/ctr-update-solo-XXXXXX";
	if (!mkdtemp(base))
		return 2;
	const std::string dir = base;
	const std::string exe = dir + "/ctr_native_ap";
	mkdir((dir + "/solo-runtime").c_str(), 0755);
	mkdir((dir + "/solo").c_str(), 0755);
	put(exe, "OLD EXE");
	put(dir + "/ctr.apworld", "OLD WORLD");
	put(dir + "/solo-runtime/RUNTIME.txt", "OLD ID");
	put(dir + "/solo-runtime/stale.py", "OLD RUNTIME FILE");
	put(dir + "/solo/room.archipelago", "PLAYER SEED");

	const std::vector<InstallFile> files = {
		file("ctr_native_ap", "NEW EXE", true),
		file("ctr.apworld", "NEW WORLD"),
		file("solo-runtime/RUNTIME.txt", "NEW ID"),
		file("solo-runtime/ctr_solo.py", "NEW RUNTIME FILE"),
	};
	std::vector<StagedFile> staged;
	const FsOps fs = RealFsOps();
	if (Stage(fs, exe, files, staged) != OK)
	{
		std::printf("FAIL: staging\n");
		return 1;
	}

	State &s = S();
	s.status = AP_UPD_READY;
	s.exe = exe;
	s.staged = staged;
	s.tag = "v9.9.9";

	const auto unchanged = [&]() {
		return get(exe) == "OLD EXE" && get(dir + "/ctr.apworld") == "OLD WORLD" &&
		       get(dir + "/solo-runtime/RUNTIME.txt") == "OLD ID" &&
		       get(dir + "/solo-runtime/stale.py") == "OLD RUNTIME FILE" &&
		       get(dir + "/solo-runtime.update/ctr_solo.py") == "NEW RUNTIME FILE" &&
		       get(dir + "/solo-runtime.old/RUNTIME.txt") == "<missing>" &&
		       get(exe + ".old") == "<missing>";
	};

	expect(unchanged(), "the staged update changed nothing installed");

	g_session_active = 1;
	g_job_state = AP_SOLO_JOB_IDLE;
	expect(ap_update_solo_busy() == 1, "a solo session is busy");
	expect(ap_update_apply() == -1, "a solo session refuses the install");
	expect(s.status == AP_UPD_READY && unchanged(), "the refusal keeps READY and every file");

	g_session_active = 0;
	g_job_state = AP_SOLO_JOB_RUNNING;
	expect(ap_update_solo_busy() == 1, "a solo job is busy");
	expect(ap_update_apply() == -1, "a solo job refuses the install");
	expect(s.status == AP_UPD_READY && unchanged(), "the refusal keeps READY and every file");

	for (int job : {(int)AP_SOLO_JOB_IDLE, (int)AP_SOLO_JOB_DONE, (int)AP_SOLO_JOB_FAILED})
	{
		g_job_state = job;
		expect(ap_update_solo_busy() == 0, "no session and no running job is not busy");
	}

	g_job_state = AP_SOLO_JOB_DONE;
	expect(ap_update_apply() == 1, "the install runs once Play Solo stopped");
	expect(s.status == AP_UPD_INSTALLED, "the status says installed");
	expect(get(exe) == "NEW EXE" && get(dir + "/ctr.apworld") == "NEW WORLD", "the new files are in place");
	expect(get(dir + "/solo-runtime/RUNTIME.txt") == "NEW ID" &&
	           get(dir + "/solo-runtime/ctr_solo.py") == "NEW RUNTIME FILE",
	       "the new runtime is in place");
	expect(get(dir + "/solo-runtime/stale.py") == "<missing>", "no file of the older runtime survives");
	expect(get(dir + "/solo-runtime.old/stale.py") == "OLD RUNTIME FILE", "the old runtime waits as .old");
	expect(get(dir + "/solo/room.archipelago") == "PLAYER SEED", "the player's solo folder is untouched");

	std::string rm = "rm -rf '" + dir + "'";
	if (std::system(rm.c_str()) != 0)
		std::printf("note: could not remove %s\n", dir.c_str());
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
