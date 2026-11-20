/**
 *    Name:        frames.h
 *    Description: frame timing: the board's lines as the translated and the decompiled program run, and as
 *                 mdref --timing frames runs the original: instructions take no time, and the board moves on
 *                 to its next frame only where the program waits for it
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *    Modified:    20-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The program runs at one moment of the frame: the start of line 224, where VBLANK begins and the V interrupt
 *    is raised (at power on, the start of frame 1's line 0). The game's frame waits are the branches back of the
 *    loops where it waits until an interrupt changes what they read: a loop that only reads and tests, or one the
 *    game knows goes round with nothing to do (md_game_frame_idle). When the program takes one with nothing to
 *    do, the board runs the lines from the next one up to the next line 224, each as
 *    md_line_start begins it, and the frame ends there: the host takes its hashes and applies its controls, and
 *    the program takes the interrupt after the loop's first instruction (MAME's core takes an interrupt raised
 *    before an instruction after it). So a frame comes where the program has finished its work, as on a console
 *    fast enough never to lag. The present the hooks see (md_frames_now) is the start
 *    of the line begun last; the 68000 never waits (no DMA wait, no FIFO, no refresh).
 */
#ifndef MD_FRAMES_H
#define MD_FRAMES_H

#include "md/machine.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's (a source of its own, which mdref and the program link): its frame waits, the branches back of
   the loops that wait for an interrupt; and, when the program has just taken the one at 'pc', whether it has
   nothing to do (not 0; always, for a loop that only reads and tests) */
extern const uint32_t md_game_frame_waits[];
extern const unsigned md_game_frame_wait_count;
int md_game_frame_idle(uint32_t pc);

/* Whether 'pc' is one of the game's frame waits */
int md_frame_wait_at(uint32_t pc);

/* After md_power_on: the present at 0, frame 1's line 0 the next to begin */
void md_frames_reset(void);

/* The present in master clocks: the board's hooks.now with frame timing */
uint64_t md_frames_now(void);

/* The board's lines from the next one up to the next line 224, each begun (md_line_start) and then passed to
   'line' (the host's: the interrupt line, the picture); 'vblank' after line 224 begins: the frame ends (the
   host's hashes and controls, md_board.frame the frame's number). Either may be NULL */
void md_frames_next(void (*line)(unsigned line), void (*vblank)(void));

#ifdef __cplusplus
}
#endif

#endif
