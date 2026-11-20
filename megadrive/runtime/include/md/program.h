/**
 *    Name:        program.h
 *    Description: a Mega Drive game's translated 68000 program (tools/recompile.py's C) as a port runs it: its
 *                 reset, its run on a stack of its own, the frames it makes
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *    Modified:    20-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The program runs with frame timing (md/frames.h): it takes no time, and the board moves on to its next
 *    VBLANK where the program has nothing to do. Each line the board begins goes to program_line (the picture),
 *    and when VBLANK begins program_frame_done runs: the port takes the frame's hashes, applies its controls,
 *    and calls program_stop when it has had enough. The cartridge is in the slot (md_insert) and the board on
 *    (md_power_on, md_frames_reset, hooks.now = md_frames_now) before program_reset.
 */
#ifndef MD_PROGRAM_H
#define MD_PROGRAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The CPU as the 68000's reset leaves it, from the program's vectors */
void program_reset(void);

/* Runs the program, on a stack of its own, until program_stop */
void program_run(void);

/* Called by program_frame_done: program_run returns */
void program_stop(void);

/* The port's: after each line begins, and when VBLANK begins (the frame is done) */
extern void (*program_line)(unsigned line);
extern void (*program_frame_done)(void);

/* The frames so far, and the interrupts the program took */
uint64_t program_frames(void);
uint32_t program_interrupts(void);

#ifdef __cplusplus
}
#endif

#endif
