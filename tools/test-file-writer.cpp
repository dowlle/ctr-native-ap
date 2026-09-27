// Host assertions for the background file writer (ap/ap_file_writer.cpp) that
// takes ap-state.json and the replay-dedup file off the game thread.
//
// c++ -std=c++17 -Wall -Wextra -pthread -I ap -o /tmp/test-file-writer tools/test-file-writer.cpp ap/ap_file_writer.cpp
// /tmp/test-file-writer
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * submit returns without waiting for the job (the worker is held busy)
//   * latest snapshot wins per slot: jobs queued behind a busy worker collapse
//     into the newest one, and slots never replace each other
//   * the payload is copied at submit time
//   * flush waits for the queue to drain, and times out while a job is stuck
//   * last_ok reports the result of the last finished job per slot
//   * replace_file writes the exact bytes, replaces an existing file, and
//     leaves no .tmp behind

#include "ap_file_writer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

static int g_checks;
static int g_failures;

static void expect(bool condition, const char *name)
{
	g_checks++;
	if (!condition)
	{
		g_failures++;
		std::printf("FAIL: %s\n", name);
	}
}

static std::atomic<bool> g_gate{false};
static std::atomic<int>  g_blockerRuns{0};
static std::atomic<int>  g_fxRuns{0};
static std::string       g_fxLast;
static std::atomic<int>  g_result{1};

static int blocker_job(const void *, size_t)
{
	g_blockerRuns++;
	while (!g_gate.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	return 1;
}

static int record_job(const void *payload, size_t len)
{
	g_fxRuns++;
	g_fxLast.assign(static_cast<const char *>(payload), len);
	return g_result.load();
}

static std::string read_file(const char *path)
{
	std::string out;
	FILE *f = std::fopen(path, "rb");
	if (f == nullptr)
		return "<missing>";
	char buf[4096];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
		out.append(buf, n);
	std::fclose(f);
	return out;
}

int main()
{
	// Hold the worker inside a slot-0 job.
	expect(ap_file_writer_submit(AP_FILE_WRITER_SLOT_STATE, blocker_job, "", 0) == 1,
	       "submit queues on the worker");
	for (int i = 0; i < 1000 && g_blockerRuns.load() == 0; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	expect(g_blockerRuns.load() == 1, "worker picked up the blocking job");

	// Queue three snapshots behind it; the caller never waits.
	char snap[16];
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 1; i <= 3; i++)
	{
		std::snprintf(snap, sizeof snap, "snapshot-%d", i);
		ap_file_writer_submit(AP_FILE_WRITER_SLOT_FXSEEN, record_job, snap, std::strlen(snap));
		std::memset(snap, 'Z', sizeof snap - 1); // payload was copied
	}
	auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
	                  std::chrono::steady_clock::now() - t0).count();
	expect(waited < 100, "submitting behind a busy worker does not block");
	expect(ap_file_writer_flush(50) == 0, "flush times out while a job is stuck");

	g_gate = true;
	expect(ap_file_writer_flush(5000) == 1, "flush returns once the queue drained");
	expect(g_fxRuns.load() == 1, "three queued snapshots collapsed into one write");
	expect(g_fxLast == "snapshot-3", "the newest snapshot won, copied at submit time");
	expect(g_blockerRuns.load() == 1, "the other slot was not replaced or rerun");

	// last_ok follows the job result.
	g_result = 0;
	ap_file_writer_submit(AP_FILE_WRITER_SLOT_FXSEEN, record_job, "x", 1);
	ap_file_writer_flush(5000);
	expect(ap_file_writer_last_ok(AP_FILE_WRITER_SLOT_FXSEEN) == 0, "failed write reported");
	g_result = 1;
	ap_file_writer_submit(AP_FILE_WRITER_SLOT_FXSEEN, record_job, "y", 1);
	ap_file_writer_flush(5000);
	expect(ap_file_writer_last_ok(AP_FILE_WRITER_SLOT_FXSEEN) == 1, "later success reported");
	expect(ap_file_writer_submit(7, record_job, "z", 1) == 0, "bad slot refused");

	// replace_file.
	const char *path = "/tmp/ctr-test-file-writer.json";
	std::remove(path);
	const char a[] = "{\n  \"a\": 1\n}\n";
	const char b[] = "{\n  \"b\": 22\n}\n";
	expect(ap_file_writer_replace_file(path, a, sizeof a - 1) == 1, "first write");
	expect(read_file(path) == a, "exact bytes");
	expect(ap_file_writer_replace_file(path, b, sizeof b - 1) == 1, "replace existing");
	expect(read_file(path) == b, "replaced content");
	expect(read_file("/tmp/ctr-test-file-writer.json.tmp") == "<missing>", "no .tmp left behind");
	std::remove(path);

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
