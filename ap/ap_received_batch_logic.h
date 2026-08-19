#ifndef AP_RECEIVED_BATCH_LOGIC_H
#define AP_RECEIVED_BATCH_LOGIC_H

// Pure container update used by the inline apclientpp ReceivedItems callback.
// Keeping all five positional queues in one helper makes backlog behavior
// directly benchmarkable without a websocket or game runtime.
template <typename ItemRange, typename ItemQueue, typename PlayerQueue,
          typename IndexQueue, typename LocationQueue, typename FlagsQueue,
          typename PendingSet>
static inline int AP_ReceivedBatchAppend(const ItemRange &items,
                                         ItemQueue &itemQueue,
                                         PlayerQueue &playerQueue,
                                         IndexQueue &indexQueue,
                                         LocationQueue &locationQueue,
                                         FlagsQueue &flagsQueue,
                                         PendingSet &pendingChecks)
{
	int n = 0;
	for (const auto &it : items)
	{
		itemQueue.push_back((long long)it.item);
		playerQueue.push_back((int)it.player);
		indexQueue.push_back((long long)it.index);
		locationQueue.push_back((long long)it.location);
		flagsQueue.push_back(it.flags);
		pendingChecks.erase(it.location);
		n++;
	}
	return n;
}

#endif
