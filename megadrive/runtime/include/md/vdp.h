/**
 *    Name:        vdp.h
 *    Description: the Mega Drive's VDP (Sega 315-5313) as the program sees it: its ports, registers and
 *                 memories, its DMA, its interrupts and its counters, with the time its transfers take;
 *                 and its picture, a line at a time
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    What the ports do follows MAME's sega315_5313_device (src/devices/video/315_5313.cpp, BSD-3-Clause,
 *    David Haywood): the command words, the codes, the auto-increment, the fill that writes one more byte
 *    than its length, the copy, the 68000's DMA and the registers it leaves. MAME's DMA is instant; here it
 *    takes the time Charles MacDonald's table gives (bytes per line, by width and by whether the line is
 *    displayed), and the 68000 waits for it: the 68000 stands still for a 68000-to-VDP transfer, and its
 *    writes to the data port wait while the four-entry FIFO is full. A fill or a copy runs while the 68000
 *    goes on, and the status says DMA busy until it ends.
 */
#ifndef MD_VDP_H
#define MD_VDP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MD_VDP_WIDTH        320    /* H40; H32 draws the first 256 */
#define MD_VDP_HEIGHT       224    /* V28 */

typedef struct md_vdp
{
	uint8_t vram[0x10000];
	uint16_t cram[64];
	uint16_t vsram[64];
	uint16_t sprite_cache[0x200];    /* the VDP's own copy of what is written to the sprite table */
	uint8_t regs[32];

	uint16_t address;                /* the port's address and code (CD5-CD0) */
	uint8_t code;
	uint8_t pending;                 /* the first word of a command was written */
	uint16_t part1, part2;
	uint8_t fill_pending;            /* the next data write starts a fill */
	uint16_t fill_length;

	uint8_t vint_pending, hint_pending;
	int hint_counter;
	uint8_t odd_frame;

	uint64_t busy_until;             /* a fill or a copy runs until this master clock */
	uint64_t fifo[4];                /* when each of the last four writes leaves the FIFO */
	unsigned fifo_next;

	uint64_t dma_words;              /* statistics: words the 68000's DMA moved, and its waits */
	uint64_t dma_stall_clocks;
	uint64_t fifo_stall_clocks;
} md_vdp;

void md_vdp_reset(md_vdp *vdp);

/* The ports at C00000-C0001F: 'offset' is the address's low 5 bits, MAME's mem_mask for the bytes */
uint16_t md_vdp_read(md_vdp *vdp, unsigned offset, uint16_t mem_mask);
void md_vdp_write(md_vdp *vdp, unsigned offset, uint16_t data, uint16_t mem_mask);

/* The start of line 'line' (0 the first displayed line): the H interrupt's counter, the V interrupt */
void md_vdp_line(md_vdp *vdp, unsigned line);

/* The interrupt level the VDP asks the 68000 for (6, 4 or 0), and its acknowledge */
int md_vdp_irq_level(const md_vdp *vdp);
void md_vdp_acknowledge(md_vdp *vdp, int level);

/* Line 'line' of the picture as it is now, MD_VDP_WIDTH pixels 0x00RRGGBB (MAME's levels: pal3bit) */
void md_vdp_render_line(const md_vdp *vdp, unsigned line, uint32_t *pixels);

/* The colour of a CRAM word as 0x00RRGGBB */
uint32_t md_vdp_colour(uint16_t word);

#ifdef __cplusplus
}
#endif

#endif
