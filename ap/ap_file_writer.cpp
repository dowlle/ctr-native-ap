// Background file writer. See ap_file_writer.h for the contract.

#include "ap_file_writer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{

struct Slot
{
	ap_file_writer_job job = nullptr;
	std::vector<char>  payload;
	bool               pending = false;
};

struct Writer
{
	std::mutex              m;
	std::condition_variable wake;  // a job was queued
	std::condition_variable idle;  // a job finished
	Slot                    slots[AP_FILE_WRITER_SLOT__COUNT];
	int                     running = 0; // jobs taken off a slot, not finished yet
	std::atomic<int>        lastOk[AP_FILE_WRITER_SLOT__COUNT];
};

// Created once and never destroyed: the worker is detached and may still be
// inside a job while the process runs its exit-time destructors, so the state it
// uses must not have one.
Writer          *g_writer = nullptr;
bool             g_threaded = false;
std::once_flag   g_once;

bool any_pending(const Writer *w)
{
	for (int i = 0; i < AP_FILE_WRITER_SLOT__COUNT; i++)
		if (w->slots[i].pending)
			return true;
	return false;
}

void worker_main(Writer *w)
{
	std::unique_lock<std::mutex> lk(w->m);
	for (;;)
	{
		w->wake.wait(lk, [w] { return any_pending(w); });
		for (int i = 0; i < AP_FILE_WRITER_SLOT__COUNT; i++)
		{
			Slot &s = w->slots[i];
			if (!s.pending)
				continue;
			ap_file_writer_job job = s.job;
			std::vector<char> payload;
			payload.swap(s.payload);
			s.pending = false;
			w->running++;
			lk.unlock();
			const int ok = job(payload.data(), payload.size());
			lk.lock();
			w->lastOk[i].store(ok ? 1 : 0);
			w->running--;
		}
		w->idle.notify_all();
	}
}

void init_once()
{
	Writer *w = new (std::nothrow) Writer();
	if (w == nullptr)
		return;
	for (int i = 0; i < AP_FILE_WRITER_SLOT__COUNT; i++)
		w->lastOk[i].store(1);
	g_writer = w;
	try
	{
		std::thread(worker_main, w).detach();
		g_threaded = true;
	}
	catch (...)
	{
		g_threaded = false; // jobs run synchronously
	}
}

} // namespace

extern "C" int ap_file_writer_submit(int slot, ap_file_writer_job job, const void *payload, size_t len)
{
	if (slot < 0 || slot >= AP_FILE_WRITER_SLOT__COUNT || job == nullptr)
		return 0;
	std::call_once(g_once, init_once);

	if (g_writer != nullptr && g_threaded)
	{
		try
		{
			std::lock_guard<std::mutex> lk(g_writer->m);
			Slot &s = g_writer->slots[slot];
			const char *p = static_cast<const char *>(payload);
			s.payload.assign(p, p + len);
			s.job = job;
			s.pending = true;
			g_writer->wake.notify_one();
			return 1;
		}
		catch (...)
		{
			// Out of memory for the copy: fall through to a synchronous write.
		}
	}

	const int ok = job(payload, len);
	if (g_writer != nullptr)
		g_writer->lastOk[slot].store(ok ? 1 : 0);
	return 0;
}

extern "C" int ap_file_writer_flush(unsigned timeout_ms)
{
	if (g_writer == nullptr || !g_threaded)
		return 1;
	std::unique_lock<std::mutex> lk(g_writer->m);
	return g_writer->idle.wait_for(lk, std::chrono::milliseconds(timeout_ms), [] {
		return !any_pending(g_writer) && g_writer->running == 0;
	}) ? 1 : 0;
}

extern "C" int ap_file_writer_last_ok(int slot)
{
	if (g_writer == nullptr || slot < 0 || slot >= AP_FILE_WRITER_SLOT__COUNT)
		return 1;
	return g_writer->lastOk[slot].load();
}

static int write_whole(const char *path, const void *data, size_t len)
{
	FILE *f = std::fopen(path, "w");
	if (f == nullptr)
		return 0;
	int ok = std::fwrite(data, 1, len, f) == len;
	if (std::fclose(f) != 0)
		ok = 0;
	return ok;
}

extern "C" int ap_file_writer_replace_file(const char *path, const void *data, size_t len)
{
	if (path == nullptr || (data == nullptr && len > 0))
		return 0;
	const std::string tmp = std::string(path) + ".tmp";
	if (write_whole(tmp.c_str(), data, len))
	{
#ifdef _WIN32
		if (MoveFileExA(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING))
			return 1;
#else
		if (std::rename(tmp.c_str(), path) == 0)
			return 1;
#endif
	}
	std::remove(tmp.c_str());
	return write_whole(path, data, len);
}
