// Host assertions for the explosion hit test (game/PROC_HitboxDistance.h), the
// cause of the "invisible hits" on Cortex Vortex: a blast far across the map
// hit a kart because a squared axis delta wrapped negative in 32 bits.
//
//   cc -Wall -Wextra -DPROC_HITBOX_FORCE_NATIVE -I game -o /tmp/test-proc-hitbox-distance tools/test-proc-hitbox-distance.c && /tmp/test-proc-hitbox-distance
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
// The coordinates are Cortex Vortex checkpoint positions (LEV restart points
// 1 and 131 of cortex-vortex 1.1.3), 56432 units apart.

#include <stdio.h>
#include <stdint.h>

#include "PROC_HitboxDistance.h"

static int g_fail;

static void expect(int got, int want, const char *what)
{
	if (got != want)
	{
		printf("FAIL %s: got %d, want %d\n", what, got, want);
		g_fail = 1;
	}
}

// Radii used by RB_Burst_Init (missile, bomb, big bomb) and RB_Blowup.
static const int32_t kRadii[] = {0x4000, 0x19000, 0x40000};

int main(void)
{
	int32_t blast[3] = {5581, 21, 27287};
	int32_t kart[3] = {-10773, 143, -26723};
	int32_t dx = blast[0] - kart[0];
	int32_t dy = blast[1] - kart[1];
	int32_t dz = blast[2] - kart[2];

	// 1. The reported bug: retail arithmetic hits a kart 56432 units away.
	expect(PROC_HitboxWithin(PROC_HitboxAxisSquare_Retail, dx, dy, dz, 0x19000), 1, "retail wraps on the Cortex Vortex pair");
	expect(PROC_HitboxWithin(PROC_HitboxAxisSquare_Native, dx, dy, dz, 0x19000), 0, "native rejects the Cortex Vortex pair");

	// 2. The native build uses the fixed square.
	expect(PROC_HitboxAxisSquare(54010) == PROC_HitboxAxisSquare_Native(54010), 1, "native build selects the clamped square");

	// 3. No far delta may ever hit, on any axis, for any radius.
	for (int r = 0; r < 3; r++)
	{
		for (int32_t d = 16384; d <= 131072; d++)
		{
			if (PROC_HitboxWithin(PROC_HitboxAxisSquare_Native, d, 0, 0, kRadii[r]) ||
			    PROC_HitboxWithin(PROC_HitboxAxisSquare_Native, 0, -d, 0, kRadii[r]) ||
			    PROC_HitboxWithin(PROC_HitboxAxisSquare_Native, 0, 0, d, kRadii[r]) ||
			    PROC_HitboxWithin(PROC_HitboxAxisSquare_Native, d, 100, -d, kRadii[r]))
			{
				printf("FAIL far delta %d hits with radius^2 0x%x\n", d, kRadii[r]);
				g_fail = 1;
				break;
			}
		}
	}

	// 4. Retail parity where retail is correct: every |delta| up to 46340
	// gives the same square and the same verdict.
	for (int32_t d = -46340; d <= 46340; d++)
	{
		if (PROC_HitboxAxisSquare_Native(d) != PROC_HitboxAxisSquare_Retail(d) && PROC_HitboxAxisSquare_Retail(d) <= 0x0fffffff)
		{
			printf("FAIL square parity at %d\n", d);
			g_fail = 1;
			break;
		}
	}
	for (int32_t a = -700; a <= 700; a += 7)
	{
		for (int32_t b = -700; b <= 700; b += 11)
		{
			for (int r = 0; r < 3; r++)
			{
				int retail = PROC_HitboxWithin(PROC_HitboxAxisSquare_Retail, a, b / 2, b, kRadii[r]);
				int native = PROC_HitboxWithin(PROC_HitboxAxisSquare_Native, a, b / 2, b, kRadii[r]);
				if (retail != native)
				{
					printf("FAIL near parity at (%d,%d,%d) r2=0x%x\n", a, b / 2, b, kRadii[r]);
					g_fail = 1;
				}
			}
		}
	}

	// 5. Near hits still land: a kart 100 units from a missile blast.
	expect(PROC_HitboxWithin(PROC_HitboxAxisSquare_Native, 100, 0, 0, 0x4000), 1, "near missile blast hits");
	expect(PROC_HitboxWithin(PROC_HitboxAxisSquare_Native, 129, 0, 0, 0x4000), 0, "just outside missile radius misses");

	if (!g_fail)
	{
		printf("PASS proc hitbox distance\n");
	}
	return g_fail;
}
