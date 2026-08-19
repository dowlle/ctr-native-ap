#ifndef CTR_AP_HELD_CHECKS_H
#define CTR_AP_HELD_CHECKS_H

#include <cstdint>
#include <set>
#include <string>

struct APHeldCheckFlush
{
	int attempted = 0;
	int sent = 0;
	int rearmed = 0;
	int settled = 0;
	int discarded = 0;
};

class APHeldChecks
{
public:
	void hold(int64_t code) { checks_.insert(code); }
	int size() const { return (int)checks_.size(); }
	bool empty() const { return checks_.empty(); }

	template <typename IsSettled, typename Send>
	APHeldCheckFlush onConnected(const std::string &seed, const std::string &slot,
	                              IsSettled isSettled, Send send)
	{
		APHeldCheckFlush result;

		if (!checks_.empty())
		{
			if (seed == seed_ && slot == slot_)
			{
				std::set<int64_t> retry;
				retry.swap(checks_);
				for (int64_t code : retry)
				{
					if (isSettled(code))
					{
						result.settled++;
						continue;
					}
					result.attempted++;
					if (send(code))
						result.sent++;
					else
					{
						checks_.insert(code);
						result.rearmed++;
					}
				}
			}
			else
			{
				result.discarded = (int)checks_.size();
				checks_.clear();
			}
		}

		seed_ = seed;
		slot_ = slot;
		return result;
	}

private:
	std::set<int64_t> checks_;
	std::string seed_;
	std::string slot_;
};

#endif
