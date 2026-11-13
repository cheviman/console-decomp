/**
 *    Name:        mdsound.h
 *    Description: the reference's sound side of the Mega Drive: MAME's Z80, ymfm's YM2612 and MAME's
 *                 SN76489 (the Sega PSG), on the board's time
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Time is the board's: master clocks (md/machine.h). The Z80 runs a cycle every 15 of them, the YM2612
 *    makes a sample every 144 of its clocks (7 master clocks each: 1008), the PSG one every 2 of its clocks
 *    (15 each: 30), as MAME's devices do; sample n of a chip is the one it makes from time n x period on, so
 *    nothing drifts. The Z80 runs only while the board lets it (neither in reset nor stopped for the 68000);
 *    the YM2612's timers run all the time.
 */
#ifndef MEGADRIVE_REFERENCE_SOUND_H
#define MEGADRIVE_REFERENCE_SOUND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hooks the sound side to md_board (after md_power_on), at board time 'time' */
void ms_attach(uint64_t time);

/* Runs the Z80 and the chips up to board time 'time' */
void ms_run_until(uint64_t time);

/* Runs them up to 'time' and closes the frame's samples */
void ms_end_frame(uint64_t time);

/* FNV-1a hashes of the frame just closed: the chips' samples, and the Z80's RAM */
uint64_t ms_frame_samples_hash(void);
uint64_t ms_state_hash(void);

/* The frame's samples mixed as MAME's megadriv.cpp routes them (the YM2612's two outputs at 0.50 to their
   sides, the PSG at 0.50 to both), stereo at the YM2612's rate, the PSG averaged over each of its samples */
const int16_t *ms_frame_mix(size_t *count);
uint32_t ms_mix_rate(void);

/* The Z80's progress: the cycles it ran, and the addresses it executed (8 KB, address >> 3, bit address & 7) */
uint64_t ms_z80_cycles(void);
const uint8_t *ms_executed(void);

/* The address of the Z80's instruction running now */
uint16_t ms_z80_pc(void);

#ifdef __cplusplus
}
#endif

#endif
