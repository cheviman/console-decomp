/**
 *    Name:        t6w28.h
 *    Description: the Neo Geo Pocket Color's sound chip (T6W28): its registers and its samples
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    What the chip does is MAME 0.289's t6w28.cpp (Wilbert Pol), which ngpref runs as the reference: two partial
 *    SN76489A cores behind two ports, port 0 (the Z80's 4000, and the TMP95C061's 0000A0 and 0000A1) and port 1
 *    (the Z80's 4001), each with the registers of three tones and a noise. In MAME's arithmetic the sound is made
 *    of port 1's three tones and port 0's noise (its period and mode, its shift register, and port 0's tone 2 for
 *    its "tone 2 output" rate); output 0 (the left side) takes port 1's four volumes, output 1 (the right side)
 *    port 0's. The chip is a struct the host keeps where it likes: nothing is allocated, integers only.
 *
 *    Samples come at the chip's rate, a sample every 16 of its clocks (3.072 MHz, half the master clock:
 *    192 kHz, a sample every 32 master clocks), each output 0-7FFF (MAME's stream takes it as value / 32768).
 *    ngp_t6w28_generate makes a run of them as one of MAME's stream updates does, and the run's length matters:
 *    a voice whose volume is off has its counter moved on by the whole run at once (sound_stream_update's
 *    first loop), so the same writes with the runs cut elsewhere give other samples. A host that must give
 *    MAME's samples generates up to the present before every write (MAME's write updates its stream first)
 *    and wherever MAME's stream is updated; ngp_t6w28_set_enable does not (MAME's set_enable does not either),
 *    so it takes effect from the start of the next run.
 */
#ifndef NGP_T6W28_H
#define NGP_T6W28_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NGP_T6W28_RATE       192000    /* samples a second */
#define NGP_T6W28_PERIOD     32        /* master clocks a sample */
#define NGP_T6W28_MAX_RUN    16383     /* the longest run ngp_t6w28_generate takes: with more, MAME's 32-bit
                                          counters of a voice whose volume is off would overflow */

/* The chip's state, as t6w28.cpp's members (MAME's names); index = port * 8 + register for the registers,
   port * 4 + voice (0-2 the tones, 3 the noise) for the voices */
typedef struct ngp_t6w28
{
	int32_t reg[16];                 /* m_register: the frequencies (10 bits) and the 4-bit volumes and noise */
	int32_t last_register[2];        /* m_last_register: each port's latched register (0-7) */
	int32_t volume[8];               /* m_volume: each voice's level, from the volume table */
	uint32_t rng[2];                 /* m_rng: each port's noise shift register */
	int32_t noise_mode[2];           /* m_noise_mode: 1 white noise, 0 periodic */
	int32_t feedback_mask;           /* m_feedback_mask */
	int32_t whitenoise_taps;         /* m_whitenoise_taps */
	int32_t whitenoise_invert;       /* m_whitenoise_invert */
	int32_t period[8];               /* m_period: each voice's half period, 65536 a sample */
	int32_t count[8];                /* m_count: what is left of it */
	int32_t output[8];               /* m_output: each voice's level, 0 or 1 */
	int32_t enabled;                 /* m_enabled: 0 silences both outputs */
} ngp_t6w28;

/* The chip as MAME starts it (device_start, on a device built in zeroed memory: port 1's registers 0, the noise
   modes periodic, the chip disabled) */
void ngp_t6w28_reset(ngp_t6w28 *chip);

/* A byte written to port 0 or 1 (MAME's write, offset & 1), without the update before it */
void ngp_t6w28_write(ngp_t6w28 *chip, unsigned port, uint8_t data);

/* 0000B8's 55 (enable 1) and AA (0), as MAME's set_enable */
void ngp_t6w28_set_enable(ngp_t6w28 *chip, int enable);

/* The next 'count' samples (1 to NGP_T6W28_MAX_RUN) as one run of MAME's sound_stream_update: 'out' takes
   2 * count values, output 0 and output 1 of each sample in turn */
void ngp_t6w28_generate(ngp_t6w28 *chip, unsigned count, int16_t *out);

#ifdef __cplusplus
}
#endif

#endif
