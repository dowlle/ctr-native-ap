#include <common.h>

#define CLAMP_LOW  0
#define CLAMP_HIGH 0xff00

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003f48c-0x8003f4c4
int Particle_BitwiseClampByte(int *value)
{
	if (*value < CLAMP_LOW)
		*value = CLAMP_LOW;
	else if (*value > CLAMP_HIGH)
		*value = CLAMP_HIGH;

	return *value >> 8;
}
