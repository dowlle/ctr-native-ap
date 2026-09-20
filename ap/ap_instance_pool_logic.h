#ifndef AP_INSTANCE_POOL_LOGIC_H
#define AP_INSTANCE_POOL_LOGIC_H

/* Authored LEV instances leave the free list without entering taken.
 * Custom tracks must reserve their full table as well as the normal runtime
 * budget. Retail loads retain their existing memory budget. */
static inline int AP_InstancePoolCapacity(int runtimeBudget, int customServing,
                                        unsigned int authoredInstances)
{
	return runtimeBudget + (customServing ? (int)authoredInstances : 0);
}

#endif
