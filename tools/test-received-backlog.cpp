#include <chrono>
#include <cstdio>
#include <deque>
#include <list>
#include <set>

#include "../ap/ap_received_batch_logic.h"

struct TestItem
{
	long long item;
	int player;
	long long index;
	long long location;
	unsigned flags;
};

int main(void)
{
	static const int count = 10000;
	std::list<TestItem> items;
	std::deque<long long> itemQueue;
	std::deque<int> playerQueue;
	std::deque<long long> indexQueue;
	std::deque<long long> locationQueue;
	std::deque<unsigned> flagsQueue;
	std::set<long long> pending;
	int i;

	for (i = 0; i < count; i++)
	{
		items.push_back({35000000 + i, (i % 8) + 1, i,
		                 35010000 + i, (unsigned)(i % 5)});
		pending.insert(35010000 + i);
	}

	const auto start = std::chrono::steady_clock::now();
	const int appended = AP_ReceivedBatchAppend(
	    items, itemQueue, playerQueue, indexQueue, locationQueue, flagsQueue,
	    pending);
	const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
	    std::chrono::steady_clock::now() - start).count();

	const bool sizes = appended == count && itemQueue.size() == count &&
	                   playerQueue.size() == count && indexQueue.size() == count &&
	                   locationQueue.size() == count && flagsQueue.size() == count;
	const bool endpoints = itemQueue.front() == 35000000 &&
	                       itemQueue.back() == 35000000 + count - 1 &&
	                       indexQueue.front() == 0 &&
	                       indexQueue.back() == count - 1;
	const bool settled = pending.empty();

	std::printf("%s queue sizes stay aligned\n", sizes ? "PASS" : "FAIL");
	std::printf("%s batch order and endpoints survive\n", endpoints ? "PASS" : "FAIL");
	std::printf("%s pending checks settle\n", settled ? "PASS" : "FAIL");
	std::printf("INFO 10000-item container update: %lld us\n",
	            (long long)elapsed);
	return (sizes && endpoints && settled) ? 0 : 1;
}
