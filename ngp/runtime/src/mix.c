/**
 *    Name:        mix.c
 *    Description: the Neo Geo Pocket Color's two DACs and its speaker's mix in C: ngp/mix.h
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    MAME 0.289's DAC_8BIT_R2R (dac.cpp, smf) and ngp.cpp's routes to the speaker, in integers: 1.0 is 32768,
 *    so a T6W28 output (value / 32768) at a gain of 0.50 is value / 2, and a DAC's -1 + byte / 128 at 0.25 is
 *    (byte - 128) * 64.
 */

#include "ngp/mix.h"

/* One side: the T6W28's output (0-7FFF) halved, the odd value's half dropped, and the DAC's byte about its
   middle (80) a quarter of the way */
static int16_t mix_side(int16_t t6w28, uint8_t dac)
{
	return (int16_t)((t6w28 >> 1) + ((int32_t)dac - 128) * 64);
}

void ngp_mix(const int16_t *t6w28, unsigned count, uint8_t left_dac, uint8_t right_dac, int16_t *out)
{
	unsigned index;

	for (index = 0; index < count; index++)
	{
		const int16_t left = mix_side(t6w28[index * 2], left_dac);
		const int16_t right = mix_side(t6w28[index * 2 + 1], right_dac);

		out[index * 2] = left;
		out[index * 2 + 1] = right;
	}
}
