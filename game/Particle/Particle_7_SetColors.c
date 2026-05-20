#include <common.h>

#define COLOR_FLAG_R 0x80
#define COLOR_FLAG_G 0x100
#define COLOR_FLAG_B 0x200

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003f4c4-0x8003f590
u32 Particle_SetColors(u32 flagColors, u32 flagAlpha, struct Particle *p)
{
	u32 color = 0;

	if (flagColors & COLOR_FLAG_R)
	{
		color = (u32)Particle_BitwiseClampByte(&p->axis[7].startVal);

		if (flagColors & COLOR_FLAG_G)
			color |= (u32)Particle_BitwiseClampByte(&p->axis[8].startVal) << 8;
		else
			color |= color << 8;

		if (flagColors & COLOR_FLAG_B)
			color |= (u32)Particle_BitwiseClampByte(&p->axis[9].startVal) << 16;
		else
			color |= (color & 0xff) << 16;
	}
	else
	{
		color = 0x01000000;
	}

	if (flagAlpha & 0x80)
		color |= 0x2000000;

	return color;
}
