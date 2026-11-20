/**
 *    Name:        frames.c
 *    Description: frame timing: the board's lines from one frame wait to the next VBLANK (md/frames.h)
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *    Modified:    20-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 */
#include "md/frames.h"

static uint64_t present;        /* the start of the line begun last */
static uint64_t frame_base;     /* the master clock of the next line 0 to come, or of the frame under way */
static unsigned next_line;      /* the next line to begin */

int md_frame_wait_at(uint32_t pc)
{
	unsigned index;

	pc &= 0xFFFFFFu;
	for (index = 0; index < md_game_frame_wait_count; index++)
		if (md_game_frame_waits[index] == pc)
			return 1;
	return 0;
}

void md_frames_reset(void)
{
	present = 0;
	frame_base = 0;
	next_line = 0;
}

uint64_t md_frames_now(void)
{
	return present;
}

void md_frames_next(void (*line)(unsigned line), void (*vblank)(void))
{
	for (;;)
	{
		const unsigned begun = next_line;

		present = frame_base + (uint64_t)begun * MD_LINE_CLOCKS;
		md_line_start(begun);
		if (line)
			line(begun);
		if (++next_line == MD_LINES)
		{
			next_line = 0;
			frame_base += MD_FRAME_CLOCKS;
		}
		if (begun == MD_ACTIVE_LINES)
		{
			if (vblank)
				vblank();
			return;
		}
	}
}
