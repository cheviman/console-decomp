/**
 *    Name:        t6w28.c
 *    Description: the Neo Geo Pocket Color's sound chip (T6W28) in C: ngp/t6w28.h
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    MAME 0.289's t6w28.cpp (Wilbert Pol) in C, its arithmetic kept: the 32-bit counters, and the sums of
 *    products as unsigned 32-bit values, which wrap as MAME's int sums do. The chip is the host's struct:
 *    nothing is allocated, integers only. ngp_t6w28_generate is one run of MAME's sound_stream_update.
 */

#include <string.h>
#include "ngp/t6w28.h"

#define STEP        0x10000
#define MAX_OUTPUT  0x7fff

/* The volume table as MAME's set_gain(0) builds it: MAX_OUTPUT / 3 (10922) divided by 1.258925412 (10 ^ (2/20))
   for each step, truncated to an integer, the last entry 0 */
static const int32_t vol_table[16] =
{
	10922, 8675, 6891, 5473, 4348, 3453, 2743, 2179,
	1731, 1374, 1092, 867, 689, 547, 434, 0
};

/* The square wave of one voice for one sample: its count and output move on by a sample, and the function
   returns how long the wave stayed at 1 in the sample (MAME's vol[voice]). The half period is added twice in
   each turn of the loop so that the wave ends a turn as it started. */
static int32_t square_wave(ngp_t6w28 *chip, int voice)
{
	int32_t vol = 0;

	if (chip->output[voice])
	{
		vol += chip->count[voice];
	}
	chip->count[voice] -= STEP;
	while (chip->count[voice] <= 0)
	{
		chip->count[voice] += chip->period[voice];
		if (chip->count[voice] > 0)
		{
			chip->output[voice] ^= 1;
			if (chip->output[voice])
			{
				vol += chip->period[voice];
			}
			break;
		}
		chip->count[voice] += chip->period[voice];
		vol += chip->period[voice];
	}
	if (chip->output[voice])
	{
		vol -= chip->count[voice];
	}

	return vol;
}

/* The noise for one sample: port 0's noise (its period, its mode noise_mode[0] and its shift register rng[0]),
   the only noise either output hears. It returns MAME's vol[3]. */
static int32_t noise(ngp_t6w28 *chip)
{
	int32_t vol = 0;
	int32_t left = STEP;

	do
	{
		int32_t nextevent;

		if (chip->count[3] < left)
		{
			nextevent = chip->count[3];
		}
		else
		{
			nextevent = left;
		}

		if (chip->output[3])
		{
			vol += chip->count[3];
		}
		chip->count[3] -= nextevent;

		if (chip->count[3] <= 0)
		{
			if (chip->noise_mode[0] == 1) /* White Noise Mode */
			{
				/* The taps' bits differ: the bit shifted in is 1 */
				const uint32_t taps = chip->rng[0] & (uint32_t)chip->whitenoise_taps;
				if (taps != (uint32_t)chip->whitenoise_taps && taps != 0u)
				{
					chip->rng[0] >>= 1;
					chip->rng[0] |= (uint32_t)chip->feedback_mask;
				}
				else
				{
					chip->rng[0] >>= 1;
				}
				chip->output[3] = chip->whitenoise_invert ? (int32_t)(!(chip->rng[0] & 1u))
					: (int32_t)(chip->rng[0] & 1u);
			}
			else /* Periodic noise mode */
			{
				if (chip->rng[0] & 1u)
				{
					chip->rng[0] >>= 1;
					chip->rng[0] |= (uint32_t)chip->feedback_mask;
				}
				else
				{
					chip->rng[0] >>= 1;
				}
				chip->output[3] = (int32_t)(chip->rng[0] & 1u);
			}
			chip->count[3] += chip->period[3];
			if (chip->output[3])
			{
				vol += chip->period[3];
			}
		}

		if (chip->output[3])
		{
			vol -= chip->count[3];
		}

		left -= nextevent;
	} while (left > 0);

	return vol;
}

void ngp_t6w28_reset(ngp_t6w28 *chip)
{
	int i;

	/* The memset gives what MAME's device_start sets to 0, and what it leaves as zeroed memory had it: port 1's
	   registers, the noise modes, and enabled */
	memset(chip, 0, sizeof(*chip));

	for (i = 0; i < 8; i += 2)
	{
		chip->reg[i] = 0;
		chip->reg[i + 1] = 0x0f;   /* volume = 0 */
	}

	for (i = 0; i < 8; i++)
	{
		chip->period[i] = STEP;
		chip->count[i] = STEP;
	}

	/* Default is SN76489 non-A */
	chip->feedback_mask = 0x4000;     /* mask for feedback */
	chip->whitenoise_taps = 0x03;     /* mask for white noise taps */
	chip->whitenoise_invert = 1;      /* white noise invert flag */

	chip->rng[0] = (uint32_t)chip->feedback_mask;
	chip->rng[1] = (uint32_t)chip->feedback_mask;
	chip->output[3] = (int32_t)(chip->rng[0] & 1u);

	/* values from sn76489a */
	chip->feedback_mask = 0x8000;
	chip->whitenoise_taps = 0x06;
	chip->whitenoise_invert = 0;
}

void ngp_t6w28_write(ngp_t6w28 *chip, unsigned port, uint8_t data)
{
	int n, r, c;

	port &= 1u;

	if (data & 0x80)
	{
		r = (data & 0x70) >> 4;
		chip->last_register[port] = r;
		chip->reg[port * 8 + r] = (chip->reg[port * 8 + r] & 0x3f0) | (int32_t)(data & 0x0f);
	}
	else
	{
		r = chip->last_register[port];
	}

	c = r / 2;

	switch (r)
	{
		case 0: /* tone 0 : frequency */
		case 2: /* tone 1 : frequency */
		case 4: /* tone 2 : frequency */
			if ((data & 0x80) == 0)
			{
				chip->reg[port * 8 + r] = (chip->reg[port * 8 + r] & 0x0f) | ((int32_t)(data & 0x3f) << 4);
			}
			chip->period[port * 4 + c] = STEP * chip->reg[port * 8 + r];
			if (chip->period[port * 4 + c] == 0)
			{
				chip->period[port * 4 + c] = STEP;
			}
			if (r == 4)
			{
				/* In its "tone 2" mode the noise's rate follows tone 2's */
				if ((chip->reg[port * 8 + 6] & 0x03) == 0x03)
				{
					chip->period[port * 4 + 3] = 2 * chip->period[port * 4 + 2];
				}
			}
			break;

		case 1: /* tone 0 : volume */
		case 3: /* tone 1 : volume */
		case 5: /* tone 2 : volume */
		case 7: /* noise  : volume */
			chip->volume[port * 4 + c] = vol_table[data & 0x0f];
			if ((data & 0x80) == 0)
			{
				chip->reg[port * 8 + r] = (chip->reg[port * 8 + r] & 0x3f0) | (int32_t)(data & 0x0f);
			}
			break;

		case 6: /* noise  : frequency, mode */
			{
				if ((data & 0x80) == 0)
				{
					chip->reg[port * 8 + r] = (chip->reg[port * 8 + r] & 0x3f0) | (int32_t)(data & 0x0f);
				}
				n = chip->reg[port * 8 + 6];
				chip->noise_mode[port] = (n & 4) ? 1 : 0;
				/* The rate: N/512, N/1024, N/2048, or tone 2's */
				chip->period[port * 4 + 3] = ((n & 3) == 3) ? 2 * chip->period[port * 4 + 2] : (STEP << (5 + (n & 3)));
				/* The shift register starts again (as SMS Power's document has it, MAME says) */
				chip->rng[port] = (uint32_t)chip->feedback_mask;
				chip->output[port * 4 + 3] = (int32_t)(chip->rng[port] & 1u);
			}
			break;
	}
}

void ngp_t6w28_set_enable(ngp_t6w28 *chip, int enable)
{
	chip->enabled = (enable != 0) ? 1 : 0;
}

void ngp_t6w28_generate(ngp_t6w28 *chip, unsigned count, int16_t *out)
{
	int32_t samples = (int32_t)count;
	int32_t i;
	unsigned sampindex;

	/* The counter of a voice that is off (its volume register 15, level 0) moves on by the whole run at once:
	   count += samples * STEP rather than count = samples + 1, which MAME says could interfere when a program
	   changes the volume quickly */
	for (i = 0; i < 8; i++)
	{
		if (chip->volume[i] == 0)
		{
			if (chip->count[i] <= samples * STEP)
			{
				chip->count[i] += samples * STEP;
			}
		}
	}

	for (sampindex = 0; sampindex < count; sampindex++)
	{
		uint32_t tone0, tone1, tone2, noise_high;
		uint32_t out0, out1;

		/* Port 0's tone 2 runs as MAME runs it, though nothing heard reads its wave: only its period, which its
		   writes set, is the noise's rate in the "tone 2" mode (register 6 & 3 is 3). It keeps the chip's
		   state MAME's */
		(void)square_wave(chip, 2);

		/* How long each wave stayed at 1 in the sample: port 1's three tones, then port 0's noise */
		tone0 = (uint32_t)square_wave(chip, 4);
		tone1 = (uint32_t)square_wave(chip, 5);
		tone2 = (uint32_t)square_wave(chip, 6);
		noise_high = (uint32_t)noise(chip);

		/* Output 0 (the left side) at port 1's volumes, output 1 (the right side) at port 0's */
		if (chip->enabled)
		{
			out0 = tone0 * (uint32_t)chip->volume[4] + tone1 * (uint32_t)chip->volume[5]
				+ tone2 * (uint32_t)chip->volume[6] + noise_high * (uint32_t)chip->volume[7];
			out1 = tone0 * (uint32_t)chip->volume[0] + tone1 * (uint32_t)chip->volume[1]
				+ tone2 * (uint32_t)chip->volume[2] + noise_high * (uint32_t)chip->volume[3];
		}
		else
		{
			out0 = 0;
			out1 = 0;
		}

		if (out0 > (uint32_t)MAX_OUTPUT * (uint32_t)STEP)
		{
			out0 = (uint32_t)MAX_OUTPUT * (uint32_t)STEP;
		}
		if (out1 > (uint32_t)MAX_OUTPUT * (uint32_t)STEP)
		{
			out1 = (uint32_t)MAX_OUTPUT * (uint32_t)STEP;
		}

		out[2 * sampindex] = (int16_t)(out0 / (uint32_t)STEP);
		out[2 * sampindex + 1] = (int16_t)(out1 / (uint32_t)STEP);
	}
}