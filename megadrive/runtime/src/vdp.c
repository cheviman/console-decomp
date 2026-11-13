/**
 *    Name:        vdp.c
 *    Description: the Mega Drive's VDP (Sega 315-5313): ports, registers, memories, DMA with its timing,
 *                 interrupts, counters, and the picture a line at a time (md/vdp.h)
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The ports follow MAME's sega315_5313_device (src/devices/video/315_5313.cpp, BSD-3-Clause, David
 *    Haywood) operation for operation; MAME's own notes say its DMA timing is not the console's, so the
 *    time a transfer takes comes from Charles MacDonald's table instead (the one MAME quotes): units a line,
 *    by the kind of transfer, the width (H32/H40) and whether the line is displayed. Where MAME reports a
 *    status bit as a constant (FIFO empty, DMA busy) the status here reports the model's.
 *
 *    The picture is the mode 5 one: the two planes with their scroll modes, the window, the sprites (their
 *    Y, size and link from the VDP's own copy of the table), the priorities, the backdrop, the left column
 *    blanking. Shadow and highlight are not drawn (the program does not turn them on: to check).
 */
#include "md/vdp.h"

#include "md/machine.h"

#include <string.h>

enum { TRANSFER_68K, TRANSFER_FILL, TRANSFER_COPY };

/* Charles MacDonald's units a line (bytes for VRAM; a word of CRAM or VSRAM takes one), by kind, by H32 or
   H40, by displayed or blank */
static const unsigned transfer_rates[3][2][2] = {
	{ { 16, 167 }, { 18, 205 } },    /* 68000 to VDP */
	{ { 15, 166 }, { 17, 204 } },    /* VRAM fill */
	{ { 8, 83 }, { 9, 102 } }        /* VRAM copy */
};

static uint64_t present(void)
{
	return md_board.hooks.now ? md_board.hooks.now() : md_board.frame_start;
}

static int is_h40(const md_vdp *vdp)
{
	return (vdp->regs[12] & 0x81) != 0;
}

static int display_enabled(const md_vdp *vdp)
{
	return (vdp->regs[1] & 0x40) != 0;
}

/* Whether a line gives the VDP's access slots of the blanking (the transfer rates' second column): with the
   display off, every line (Sega, Genesis Software Manual, p. 34); with it on, the 36 lines of V-Blank of V28
   NTSC the same page counts, 225-260: line 224 keeps the displayed lines' slots and line 261 prepares line 0's
   sprites with them (BlastEm vdp.c: check_switch_inactive, PREPARING at line 0x1FF) */
static int blank_slots(const md_vdp *vdp, unsigned line)
{
	return !display_enabled(vdp) || (line > MD_ACTIVE_LINES && line < MD_LINES - 1);
}

/* The line (0-261) master clock 'time' falls in, the frames being regular from the current one's start */
static unsigned line_at(uint64_t time)
{
	if (time < md_board.frame_start)
		return 0;
	return (unsigned)(((time - md_board.frame_start) / MD_LINE_CLOCKS) % MD_LINES);
}

/* The master clock a transfer of 'units' that begins at 'start' ends */
static uint64_t transfer_end(const md_vdp *vdp, uint64_t start, uint64_t units, int kind)
{
	double time = (double)start, left = (double)units;

	while (left > 0.0)
	{
		const uint64_t at = (uint64_t)time;
		const int blank = blank_slots(vdp, line_at(at));
		const double per_clock = (double)transfer_rates[kind][is_h40(vdp)][blank] / (double)MD_LINE_CLOCKS;
		const uint64_t into = at >= md_board.frame_start ? (at - md_board.frame_start) % MD_LINE_CLOCKS : 0;
		const double line_end = (double)(at - into + MD_LINE_CLOCKS);
		const double can = (line_end - time) * per_clock;

		if (can >= left)
		{
			time += left / per_clock;
			left = 0.0;
		}
		else
		{
			left -= can;
			time = line_end;
		}
	}
	return (uint64_t)time + 1;
}

/* The 68000 holds still until 'until' */
static void stall_until(uint64_t until)
{
	if (md_board.hooks.stall)
		md_board.hooks.stall(until);
}

/* A write into the FIFO from the 68000: it waits while the FIFO holds four, and the write leaves it when
   its access slots come ('slots': 2 for a VRAM word, 1 for CRAM or VSRAM) */
static void fifo_write(md_vdp *vdp, unsigned slots)
{
	uint64_t now = present();
	const uint64_t oldest = vdp->fifo[vdp->fifo_next];
	const uint64_t last = vdp->fifo[(vdp->fifo_next + 3) & 3];

	if (oldest > now)
	{
		vdp->fifo_stall_clocks += oldest - now;
		stall_until(oldest);
		now = oldest;
	}
	vdp->fifo[vdp->fifo_next] = transfer_end(vdp, last > now ? last : now, slots, TRANSFER_68K);
	vdp->fifo_next = (vdp->fifo_next + 1) & 3;
}

/* The 68000 waits until the FIFO is empty (a read of the data port) */
static void fifo_drain(md_vdp *vdp)
{
	const uint64_t now = present();
	uint64_t latest = 0;
	int entry;

	for (entry = 0; entry < 4; entry++)
		if (vdp->fifo[entry] > latest)
			latest = vdp->fifo[entry];
	if (latest > now)
	{
		vdp->fifo_stall_clocks += latest - now;
		stall_until(latest);
	}
}

void md_vdp_reset(md_vdp *vdp)
{
	memset(vdp, 0, sizeof(*vdp));
	vdp->hint_counter = 0xFF;
}

static void address_increment(md_vdp *vdp)
{
	vdp->address = (uint16_t)(vdp->address + vdp->regs[15]);
}

/* A word into VRAM at the port's address (a byte-swapped one at an odd address), and into the VDP's copy
   of the sprite table when it falls inside it */
static void vram_word(md_vdp *vdp, uint16_t data)
{
	const int h40 = (vdp->regs[12] & 0x01) != 0;    /* MAME's RS1 */
	const unsigned base = h40 ? (unsigned)(vdp->regs[5] & 0x7E) << 9 : (unsigned)(vdp->regs[5] & 0x7F) << 9;
	const unsigned size = h40 ? 0x400u : 0x200u;
	const unsigned address = vdp->address;

	if (address & 1)
		data = (uint16_t)(data << 8 | data >> 8);
	vdp->vram[address & 0xFFFE] = (uint8_t)(data >> 8);
	vdp->vram[(address & 0xFFFE) + 1] = (uint8_t)data;
	if (address >= base && address < base + size)
		vdp->sprite_cache[(address & (size - 1)) >> 1] = data;
	address_increment(vdp);
}

static void cram_word(md_vdp *vdp, uint16_t data)
{
	vdp->cram[(vdp->address & 0x7E) >> 1] = data;
	address_increment(vdp);
}

static void vsram_word(md_vdp *vdp, uint16_t data)
{
	vdp->vsram[(vdp->address & 0x7E) >> 1] = data;
	address_increment(vdp);
}

static uint16_t source_word(uint32_t source)
{
	if (md_board.hooks.data_read)
	{
		md_board.hooks.data_read(0, source & 0xFFFFFE);
		md_board.hooks.data_read(0, (source & 0xFFFFFE) + 1);
	}
	return md_peek16(source);
}

/* A 68000-to-VDP transfer: its words land at once, and the 68000 stands still for the time it takes */
static void dma_from_68000(md_vdp *vdp)
{
	uint32_t source = ((uint32_t)vdp->regs[21] | (uint32_t)vdp->regs[22] << 8 | (uint32_t)(vdp->regs[23] & 0x7F) << 16) << 1;
	uint16_t length = (uint16_t)(((unsigned)vdp->regs[19] | (unsigned)vdp->regs[20] << 8) << 1);
	const unsigned target = vdp->code & 0x0F;
	const uint64_t start = present();
	unsigned count, words;

	if (target != 0x1 && target != 0x3 && target != 0x5)
		return;    /* (MAME: an invalid mode) */
	if (length == 0)
		length = 0xFFFF;
	words = length >> 1;
	for (count = 0; count < words; count++)
	{
		if (target == 0x5 && vdp->address >= 0x80)
			break;    /* (MAME abandons a VSRAM transfer past its end) */
		if (target == 0x1)
			vram_word(vdp, source_word(source));
		else if (target == 0x3)
			cram_word(vdp, source_word(source));
		else
			vsram_word(vdp, source_word(source));
		source += 2;
		if (source > 0xFFFFFF)
			source = target == 0x1 ? 0xE00000 : 0xFE0000;
	}
	vdp->regs[19] = 0;
	vdp->regs[20] = 0;
	vdp->regs[21] = (uint8_t)(source >> 1);
	vdp->regs[22] = (uint8_t)(source >> 9);
	vdp->regs[23] = (uint8_t)(source >> 17);
	vdp->dma_words += words;
	{
		const uint64_t end = transfer_end(vdp, start, target == 0x1 ? (uint64_t)words * 2 : words, TRANSFER_68K);

		vdp->dma_stall_clocks += end - start;
		stall_until(end);
	}
}

static void vram_copy(md_vdp *vdp)
{
	uint32_t source = (uint32_t)vdp->regs[21] | (uint32_t)vdp->regs[22] << 8;
	const uint16_t length = (uint16_t)((unsigned)vdp->regs[19] | (unsigned)vdp->regs[20] << 8);
	unsigned count;

	for (count = 0; count < length; count++)
	{
		vdp->vram[vdp->address] = vdp->vram[source & 0xFFFF];
		source++;
		address_increment(vdp);
	}
	vdp->busy_until = transfer_end(vdp, present(), length, TRANSFER_COPY);
}

static void handle_dma(md_vdp *vdp)
{
	const unsigned type = vdp->regs[23] >> 6;
	const unsigned target = vdp->code & 0x0F;

	if (!(vdp->regs[1] & 0x10))
		return;
	if (type == 0 || type == 1)
		dma_from_68000(vdp);
	else if (type == 2)
	{
		if (target == 0x1 || target == 0x3 || target == 0x5)
		{
			vdp->fill_pending = 1;
			vdp->fill_length = (uint16_t)((unsigned)vdp->regs[19] | (unsigned)vdp->regs[20] << 8);
		}
	}
	else if ((vdp->code & 0x10) || target == 0x1)
		vram_copy(vdp);
}

static void set_register(md_vdp *vdp, unsigned number, uint8_t value)
{
	if (!(vdp->regs[1] & 0x04) && number > 10)
		return;    /* (mode 4 ignores the registers past 10) */
	vdp->regs[number] = value;
}

static void update_code_and_address(md_vdp *vdp)
{
	vdp->code = (uint8_t)(((vdp->part1 & 0xC000) >> 14) | ((vdp->part2 & 0x00F0) >> 2));
	vdp->address = (uint16_t)((vdp->part1 & 0x3FFF) | ((vdp->part2 & 0x0003) << 14));
}

static void control_write(md_vdp *vdp, uint16_t data)
{
	vdp->fill_pending = 0;
	if (vdp->pending)
	{
		vdp->pending = 0;
		vdp->part2 = data;
		update_code_and_address(vdp);
		if (vdp->code & 0x20)
			handle_dma(vdp);
	}
	else if ((data & 0xC000) == 0x8000)
	{
		set_register(vdp, (data >> 8) & 0x1F, (uint8_t)data);
		vdp->code = 0;
		vdp->address = 0;
	}
	else
	{
		vdp->pending = 1;
		vdp->part1 = data;
		update_code_and_address(vdp);
	}
}

static void data_write(md_vdp *vdp, uint16_t data)
{
	vdp->pending = 0;
	if (vdp->fill_pending)
	{
		unsigned count;

		vdp->vram[vdp->address] = (uint8_t)data;
		for (count = 0; count <= vdp->fill_length; count++)
		{
			vdp->vram[vdp->address ^ 1] = (uint8_t)(data >> 8);
			address_increment(vdp);
		}
		vdp->regs[19] = 0;
		vdp->regs[20] = 0;
		vdp->busy_until = transfer_end(vdp, present(), (uint64_t)vdp->fill_length + 1, TRANSFER_FILL);
		return;
	}
	switch (vdp->code & 0x0F)
	{
	case 0x1:
		fifo_write(vdp, 2);
		vram_word(vdp, data);
		break;
	case 0x3:
		fifo_write(vdp, 1);
		cram_word(vdp, data);
		break;
	case 0x5:
		fifo_write(vdp, 1);
		vsram_word(vdp, data);
		break;
	default:
		break;    /* (a read code: MAME ignores the write) */
	}
}

static uint16_t data_read(md_vdp *vdp)
{
	uint16_t value = 0;

	vdp->pending = 0;
	fifo_drain(vdp);
	switch (vdp->code & 0x0F)
	{
	case 0x0:
		value = (uint16_t)(vdp->vram[vdp->address & 0xFFFE] << 8 | vdp->vram[(vdp->address & 0xFFFE) + 1]);
		address_increment(vdp);
		break;
	case 0x4:
		value = vdp->vsram[(vdp->address & 0x7E) >> 1];
		address_increment(vdp);
		break;
	case 0x8:
		value = vdp->cram[(vdp->address & 0x7E) >> 1];
		address_increment(vdp);
		break;
	default:
		break;
	}
	return value;
}

static uint16_t status_read(md_vdp *vdp)
{
	const uint64_t now = present();
	const unsigned line = line_at(now);
	const uint64_t into = now >= md_board.frame_start ? (now - md_board.frame_start) % MD_LINE_CLOCKS : 0;
	unsigned waiting = 0;
	int entry, vblank, hblank;

	for (entry = 0; entry < 4; entry++)
		if (vdp->fifo[entry] > now)
			waiting++;
	vblank = (line >= MD_ACTIVE_LINES && line < MD_LINES - 1) || !display_enabled(vdp);
	hblank = into >= MD_LINE_CLOCKS * 7 / 8;
	vdp->pending = 0;
	return (uint16_t)(0x3400
		| (waiting == 0 ? 0x0200 : 0) | (waiting == 4 ? 0x0100 : 0)
		| (vdp->vint_pending ? 0x0080 : 0)
		| (vblank ? 0x0008 : 0) | (hblank ? 0x0004 : 0)
		| (vdp->busy_until > now ? 0x0002 : 0));
}

/* The H and V counters: V 00-EA then E5-FF (NTSC, V28); H in H40 00-B6 then E4-FF, in H32 00-93 then E9-FF */
static uint16_t hv_read(const md_vdp *vdp)
{
	const uint64_t now = present();
	const unsigned line = line_at(now);
	const unsigned into = (unsigned)(now >= md_board.frame_start ? (now - md_board.frame_start) % MD_LINE_CLOCKS : 0);
	unsigned v = line <= 0xEA ? line : line - 6, h;

	if (is_h40(vdp))
	{
		h = into * 210 / MD_LINE_CLOCKS;
		h = h <= 0xB6 ? h : h + (0xE4 - 0xB7);
	}
	else
	{
		h = into * 171 / MD_LINE_CLOCKS;
		h = h <= 0x93 ? h : h + (0xE9 - 0x94);
	}
	return (uint16_t)((v & 0xFF) << 8 | (h & 0xFF));
}

uint16_t md_vdp_read(md_vdp *vdp, unsigned offset, uint16_t mem_mask)
{
	(void)mem_mask;
	switch (offset & 0x1E)
	{
	case 0x00:
	case 0x02:
		return data_read(vdp);
	case 0x04:
	case 0x06:
		return status_read(vdp);
	case 0x08:
	case 0x0A:
	case 0x0C:
	case 0x0E:
		return hv_read(vdp);
	default:
		return 0;
	}
}

void md_vdp_write(md_vdp *vdp, unsigned offset, uint16_t data, uint16_t mem_mask)
{
	switch (offset & 0x1E)
	{
	case 0x00:
	case 0x02:
		if (mem_mask == 0x00FF)
			data = (uint16_t)((data & 0x00FF) | data << 8);
		else if (mem_mask == 0xFF00)
			data = (uint16_t)((data & 0xFF00) | data >> 8);
		data_write(vdp, data);
		break;
	case 0x04:
	case 0x06:
		control_write(vdp, data);
		break;
	default:
		break;    /* (the counters; the PSG is the machine's) */
	}
}

void md_vdp_line(md_vdp *vdp, unsigned line)
{
	if (line <= MD_ACTIVE_LINES)
	{
		if (--vdp->hint_counter < 0)
		{
			vdp->hint_counter = vdp->regs[10];
			if (vdp->regs[0] & 0x10)
				vdp->hint_pending = 1;
		}
	}
	else
		vdp->hint_counter = vdp->regs[10];
	if (line == MD_ACTIVE_LINES)
		vdp->vint_pending = 1;
}

int md_vdp_irq_level(const md_vdp *vdp)
{
	if (vdp->vint_pending && (vdp->regs[1] & 0x20))
		return 6;
	if (vdp->hint_pending && (vdp->regs[0] & 0x10))
		return 4;
	return 0;
}

void md_vdp_acknowledge(md_vdp *vdp, int level)
{
	(void)level;
	if (vdp->vint_pending && (vdp->regs[1] & 0x20))
		vdp->vint_pending = 0;
	else if (vdp->hint_pending && (vdp->regs[0] & 0x10))
		vdp->hint_pending = 0;
}

/* ---- the picture ---- */

static uint32_t level3(unsigned value)
{
	return (value << 5) | (value << 2) | (value >> 1);
}

uint32_t md_vdp_colour(uint16_t word)
{
	return level3((word >> 1) & 7) << 16 | level3((word >> 5) & 7) << 8 | level3((word >> 9) & 7);
}

static unsigned plane_cells(unsigned bits)
{
	return bits == 0 ? 32 : bits == 1 ? 64 : bits == 3 ? 128 : 32;
}

/* The colour (palette << 4 | index, 0 = transparent) of pixel (x, y) of tile 'entry' (a name table word) */
static unsigned tile_pixel(const md_vdp *vdp, uint16_t entry, unsigned x, unsigned y)
{
	const unsigned tile = entry & 0x7FF;
	unsigned byte, index;

	if (entry & 0x0800)
		x = 7 - x;
	if (entry & 0x1000)
		y = 7 - y;
	byte = vdp->vram[(tile * 32 + y * 4 + x / 2) & 0xFFFF];
	index = (x & 1) ? (byte & 0x0F) : (byte >> 4);
	return index ? ((entry >> 13) & 3) << 4 | index : 0;
}

static uint16_t vram_word_at(const md_vdp *vdp, unsigned address)
{
	address &= 0xFFFE;
	return (uint16_t)(vdp->vram[address] << 8 | vdp->vram[address + 1]);
}

/* A plane's pixel: colour and priority (bit 8) */
static unsigned plane_pixel(const md_vdp *vdp, unsigned base, unsigned width, unsigned height, unsigned x, unsigned y)
{
	const unsigned column = (x / 8) & (width - 1), row = (y / 8) & (height - 1);
	const uint16_t entry = vram_word_at(vdp, base + (row * width + column) * 2);
	const unsigned colour = tile_pixel(vdp, entry, x & 7, y & 7);

	return colour | ((entry & 0x8000) ? 0x100 : 0);
}

void md_vdp_render_line(const md_vdp *vdp, unsigned line, uint32_t *pixels)
{
	const int h40 = is_h40(vdp);
	const unsigned width = h40 ? 320 : 256;
	const uint32_t backdrop = md_vdp_colour(vdp->cram[vdp->regs[7] & 0x3F]);
	const unsigned plane_width = plane_cells(vdp->regs[16] & 3), plane_height = plane_cells((vdp->regs[16] >> 4) & 3);
	const unsigned base_a = (unsigned)(vdp->regs[2] & 0x38) << 10, base_b = (unsigned)(vdp->regs[4] & 0x07) << 13;
	const unsigned base_window = h40 ? (unsigned)(vdp->regs[3] & 0x3C) << 10 : (unsigned)(vdp->regs[3] & 0x3E) << 10;
	const unsigned window_width = h40 ? 64 : 32;
	const unsigned hscroll_base = (unsigned)(vdp->regs[13] & 0x3F) << 10;
	const unsigned hscroll_mode = vdp->regs[11] & 3;
	const int column_vscroll = (vdp->regs[11] & 0x04) != 0;
	const unsigned window_x = (vdp->regs[17] & 0x1F) * 16, window_y = (vdp->regs[18] & 0x1F) * 8;
	const int window_right = (vdp->regs[17] & 0x80) != 0, window_down = (vdp->regs[18] & 0x80) != 0;
	const int window_line = window_down ? line >= window_y : line < window_y;
	unsigned sprite_line[320];
	unsigned hscroll_address, hscroll_a, hscroll_b, x;

	if (!display_enabled(vdp) || line >= MD_VDP_HEIGHT)
	{
		for (x = 0; x < MD_VDP_WIDTH; x++)
			pixels[x] = backdrop;
		return;
	}

	hscroll_address = hscroll_base + (hscroll_mode == 0 ? 0 : hscroll_mode == 1 ? (line & 7) * 4
		: hscroll_mode == 2 ? (line & ~7u) * 4 : line * 4);
	hscroll_a = vram_word_at(vdp, hscroll_address) & 0x3FF;
	hscroll_b = vram_word_at(vdp, hscroll_address + 2) & 0x3FF;

	/* the sprites of this line, the first in the list in front: colour | priority << 8, 0 = none */
	memset(sprite_line, 0, sizeof(sprite_line));
	{
		const unsigned sat = h40 ? (unsigned)(vdp->regs[5] & 0x7E) << 9 : (unsigned)(vdp->regs[5] & 0x7F) << 9;
		const unsigned most = h40 ? 80 : 64, per_line = h40 ? 20 : 16, dots_most = width;
		unsigned index = 0, seen = 0, on_line = 0, dots = 0;
		int masked = 0, nonzero_x = 0;

		while (seen < most)
		{
			const uint16_t y_word = vdp->sprite_cache[index * 4], size_word = vdp->sprite_cache[index * 4 + 1];
			const uint16_t attributes = vram_word_at(vdp, sat + index * 8 + 4);
			const unsigned x_raw = vram_word_at(vdp, sat + index * 8 + 6) & 0x1FF;
			const int top = (int)(y_word & 0x3FF) - 128;
			const unsigned cells_w = ((size_word >> 10) & 3) + 1, cells_h = ((size_word >> 8) & 3) + 1;
			const unsigned link = size_word & 0x7F;

			if ((int)line >= top && (int)line < top + (int)cells_h * 8)
			{
				if (on_line == per_line)
					break;
				on_line++;
				if (x_raw == 0 && nonzero_x)
					masked = 1;
				if (x_raw != 0)
					nonzero_x = 1;
				if (!masked)
				{
					unsigned row = line - (unsigned)top, cell_x;

					if (attributes & 0x1000)
						row = cells_h * 8 - 1 - row;
					for (cell_x = 0; cell_x < cells_w * 8; cell_x++)
					{
						const int screen_x = (int)x_raw - 128 + (int)cell_x;
						unsigned column = (attributes & 0x0800) ? cells_w * 8 - 1 - cell_x : cell_x;
						const unsigned tile = (attributes & 0x7FF) + (column / 8) * cells_h + row / 8;
						unsigned byte, colour;

						if (dots >= dots_most)
							break;
						dots++;
						if (screen_x < 0 || screen_x >= (int)width || sprite_line[screen_x])
							continue;
						byte = vdp->vram[((tile & 0x7FF) * 32 + (row & 7) * 4 + (column & 7) / 2) & 0xFFFF];
						colour = (column & 1) ? (byte & 0x0F) : (byte >> 4);
						if (colour)
							sprite_line[screen_x] = (((attributes >> 13) & 3) << 4 | colour)
								| ((attributes & 0x8000) ? 0x100 : 0) | 0x200;
					}
				}
			}
			seen++;
			if (link == 0 || link >= most)
				break;
			index = link;
		}
	}

	for (x = 0; x < MD_VDP_WIDTH; x++)
	{
		unsigned a, b, s, colour = 0;
		unsigned column = x / 16, vscroll_a, vscroll_b;
		int in_window;

		if (x >= width || ((vdp->regs[0] & 0x20) && x < 8))
		{
			pixels[x] = backdrop;
			continue;
		}
		vscroll_a = (column_vscroll ? vdp->vsram[(column * 2) & 63] : vdp->vsram[0]) & 0x3FF;
		vscroll_b = (column_vscroll ? vdp->vsram[(column * 2 + 1) & 63] : vdp->vsram[1]) & 0x3FF;
		b = plane_pixel(vdp, base_b, plane_width, plane_height, (x - hscroll_b) & 0x3FF, line + vscroll_b);
		in_window = window_line || (window_right ? x >= window_x : x < window_x);
		if (in_window)
			a = plane_pixel(vdp, base_window, window_width, 32, x, line);
		else
			a = plane_pixel(vdp, base_a, plane_width, plane_height, (x - hscroll_a) & 0x3FF, line + vscroll_a);
		s = sprite_line[x];

		/* in front: the high-priority layers (sprites, A, B), then the low ones, then the backdrop */
		if ((s & 0x200) && (s & 0x100) && (s & 0x3F) & 0x0F)
			colour = s & 0x3F;
		else if ((a & 0x100) && (a & 0x0F))
			colour = a & 0x3F;
		else if ((b & 0x100) && (b & 0x0F))
			colour = b & 0x3F;
		else if ((s & 0x200) && (s & 0x0F))
			colour = s & 0x3F;
		else if (a & 0x0F)
			colour = a & 0x3F;
		else if (b & 0x0F)
			colour = b & 0x3F;
		pixels[x] = colour ? md_vdp_colour(vdp->cram[colour]) : backdrop;
	}
}
