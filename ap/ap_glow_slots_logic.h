#ifndef AP_GLOW_SLOTS_LOGIC_H
#define AP_GLOW_SLOTS_LOGIC_H

typedef int (*AP_GlowSlotGroupFn)(int bit);

// Select the advertised reward bit for each of the three physical prize slots.
// A negative output hides that slot. `byRewardType` assigns group 0/1/2 to the
// corresponding slot; otherwise all rewards share one three-wide window.
static inline void AP_GlowSlots_Select(const int *bits, int n, int phase,
                                       int byRewardType,
                                       AP_GlowSlotGroupFn groupFn,
                                       int *outSlot3)
{
	int i;

	if (outSlot3 == 0)
		return;
	outSlot3[0] = -1;
	outSlot3[1] = -1;
	outSlot3[2] = -1;
	if (bits == 0 || n <= 0)
		return;
	if (phase < 0)
		phase = 0;

	if (byRewardType && groupFn != 0)
	{
		for (i = 0; i < 3; i++)
		{
			int inGroup = 0;
			int nth;
			int j;
			for (j = 0; j < n; j++)
				if (groupFn(bits[j]) == i)
					inGroup++;
			if (inGroup == 0)
				continue;
			nth = phase % inGroup;
			for (j = 0; j < n; j++)
				if (groupFn(bits[j]) == i && nth-- == 0)
				{
					outSlot3[i] = bits[j];
					break;
				}
		}
		return;
	}

	{
		int base = (n > 3) ? phase * 3 : 0;
		for (i = 0; i < 3; i++)
			outSlot3[i] = (i < n) ? bits[(base + i) % n] : -1;
	}
}

#endif
