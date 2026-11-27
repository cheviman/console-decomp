/**
 *    Name:        mix.h
 *    Description: the Neo Geo Pocket Color's two DACs and its speaker's mix, as ngp.cpp routes them
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    What the console's output is in MAME 0.289's ngp.cpp, which ngpref runs as the reference: a stereo speaker
 *    that takes the T6W28's output 0 and the left DAC (I/O 23) on the left side, its output 1 and the right DAC
 *    (I/O 22) on the right, the T6W28 at a gain of 0.50 and each DAC at 0.25. The DACs are DAC_8BIT_R2R
 *    (dac.cpp): with no inputs routed to them, a byte gives -1 + byte / 256 * 2, so 80 is the middle and 00 the
 *    bottom, which is where both start (m_curval 0). A T6W28 output is MAME's value / 32768.
 *
 *    In MAME's floats the sum is exact: t6w28 / 65536 + (byte - 128) / 512. ngp_mix gives it with 1.0 as 32768,
 *    t6w28 / 2 + (byte - 128) * 64, the half of an odd T6W28 value dropped (MAME's sum taken down to a whole
 *    number): -8192 to 24511, so it needs no clamp. It is at the T6W28's rate, each sample with the DACs' bytes
 *    as they are at its time; a host cuts its runs where a DAC is written, as MAME's DAC brings its stream up
 *    to the present first, so that a run has one byte for each DAC. Integers only, nothing kept.
 */
#ifndef NGP_MIX_H
#define NGP_MIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The speaker's 'count' stereo samples for a run of the T6W28's (ngp_t6w28_generate's 'count' pairs, output 0
   and output 1 in turn, each 0-7FFF) with the left and right DACs at the bytes given: 'out' takes 2 * count
   values, left and right in turn, and may be 't6w28' itself */
void ngp_mix(const int16_t *t6w28, unsigned count, uint8_t left_dac, uint8_t right_dac, int16_t *out);

#ifdef __cplusplus
}
#endif

#endif
