#ifndef PROC_HITBOX_DISTANCE_H
#define PROC_HITBOX_DISTANCE_H

// Squared-distance test shared by PROC_CollideHitboxWithBucket and
// PROC_PerBspLeaf_CheckInstances (game/PROC.c). Pure, so the host harness
// tools/test-proc-hitbox-distance.c can pin it without the game.
//
// Retail squares each axis delta with MIPS `mult` and keeps only the low word,
// then rejects the axis when that word is above 0x0fffffff (|delta| > 16383).
// Once |delta| passes 46340 the true square no longer fits in a signed 32-bit
// word, the low word reads as negative, the reject test passes, and the
// negative term drags the sum under hitRadiusSquared. Retail tracks never
// separate two objects that far on one axis along the racing line, so retail
// never sees it. Cortex Vortex spans about 56000 units on X and Z: an
// explosion (bomb, missile, TNT, Nitro, potion) on one side of the map then
// "hits" a kart on the other side, with nothing in view (white flash, red
// icon, Wumpa loss). The native build squares in 64 bits so every axis beyond
// 16383 is rejected, exactly as retail intended; results for |delta| <= 46340
// are bit-identical to retail.

#include <stdint.h>

// Retail arithmetic, kept for the host harness and the non-native build.
static inline int32_t PROC_HitboxAxisSquare_Retail(int32_t value)
{
	return (int32_t)(uint32_t)((int64_t)value * (int64_t)value);
}

// Native arithmetic: a square that does not fit is clamped above the reject
// threshold instead of wrapping negative.
static inline int32_t PROC_HitboxAxisSquare_Native(int32_t value)
{
	int64_t square = (int64_t)value * (int64_t)value;

	if (square > 0x0fffffff)
	{
		return 0x10000000;
	}

	return (int32_t)square;
}

#if defined(CTR_NATIVE) || defined(PROC_HITBOX_FORCE_NATIVE)
#define PROC_HitboxAxisSquare PROC_HitboxAxisSquare_Native
#else
#define PROC_HitboxAxisSquare PROC_HitboxAxisSquare_Retail
#endif

// Returns 1 and writes the squared distance when the delta is inside
// hitRadiusSquared, 0 otherwise. Same order of tests as the retail loops.
static inline int PROC_HitboxWithin(int32_t (*square)(int32_t), int32_t distX, int32_t distY, int32_t distZ, int32_t hitRadiusSquared)
{
	int32_t dist;
	int32_t part;

	dist = square(distX);
	if (dist > 0x0fffffff)
	{
		return 0;
	}

	part = square(distY);
	dist = (int32_t)((uint32_t)dist + (uint32_t)part);
	if (part > 0x0fffffff)
	{
		return 0;
	}

	part = square(distZ);
	dist = (int32_t)((uint32_t)dist + (uint32_t)part);
	if (part > 0x0fffffff)
	{
		return 0;
	}

	return dist < hitRadiusSquared;
}

#endif
