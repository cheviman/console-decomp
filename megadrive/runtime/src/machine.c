/**
 *    Name:        machine.c
 *    Description: the Mega Drive (Genesis) as the 68000 and the Z80 see it (md/machine.h)
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The 68000's map: the cartridge at 000000-3FFFFF, the Z80's space at A00000-A0FFFF (its RAM, the
 *    YM2612, its bank register, the PSG), the I/O ports at A10000-A1001F, the Z80's bus request at A11100
 *    and reset at A11200, the VDP at C00000-C0001F (the PSG at C00011), the work RAM at E00000-FFFFFF (64 KB
 *    mirrored). The Z80's: its RAM at 0000-1FFF (mirrored to 3FFF), the YM2612 at 4000-5FFF, its bank
 *    register at 6000, the PSG at 7F11, and 32 KB of the 68000's bus at 8000-FFFF. As MAME's megadriv.cpp
 *    and sega_md_ioport have them (BSD-3-Clause); where they differ from the console's documents (a word
 *    read of the Z80's RAM), the documents.
 */
#include "md/machine.h"

#include <string.h>

md_machine md_board;

static uint64_t present(void)
{
	return md_board.hooks.now ? md_board.hooks.now() : md_board.frame_start;
}

static void sound_sync(void)
{
	if (md_board.hooks.sound_sync)
		md_board.hooks.sound_sync();
}

/* The DRAM refresh: an access to the cartridge or the work RAM after one is due waits its cycles */
static void refresh_wait(void)
{
	uint64_t cycle;

	if (!md_board.refresh || !md_board.hooks.stall)
		return;
	cycle = present() / MD_CPU_DIVIDER;
	if (cycle < md_board.refresh_next)
		return;
	md_board.refresh_next = cycle - (cycle - md_board.refresh_next) % MD_REFRESH_INTERVAL + MD_REFRESH_INTERVAL;
	md_board.refresh_cycles += MD_REFRESH_WAIT;
	md_board.hooks.stall(present() + MD_REFRESH_WAIT * MD_CPU_DIVIDER);
}

void md_insert(const uint8_t *cartridge, uint32_t size)
{
	md_board.cartridge = cartridge;
	md_board.cartridge_size = size;
}

void md_power_on(void)
{
	const md_hooks hooks = md_board.hooks;
	const uint8_t *cartridge = md_board.cartridge;
	const uint32_t cartridge_size = md_board.cartridge_size;

	memset(&md_board, 0, sizeof(md_board));
	md_board.hooks = hooks;
	md_insert(cartridge, cartridge_size);
	md_vdp_reset(&md_board.vdp);
	md_board.io_data[0] = md_board.io_data[1] = md_board.io_data[2] = 0x7F;
	md_board.z80_reset = 1;
	md_board.refresh = 1;
	md_board.refresh_next = MD_REFRESH_INTERVAL;
}

int md_z80_running(void)
{
	return !md_board.z80_reset && !md_board.z80_busreq;
}

/* ---- the I/O ports ---- */

/* A pad's port: TH high gives C B Right Left Down Up, TH low Start A 0 0 Down Up (0 = pressed); the pins
   set as outputs read back the data register */
static uint8_t port_read(unsigned port)
{
	const uint8_t control = md_board.io_control[port], data = md_board.io_data[port];
	const uint8_t buttons = port < 2 ? md_board.pads[port] : 0;
	const int th = (control & 0x40) ? (data & 0x40) != 0 : 1;
	uint8_t in;

	if (th)
		in = (uint8_t)(0x40 | (~buttons & 0x3F));
	else
		in = (uint8_t)((~(buttons >> 2) & 0x30) | (~buttons & 0x03));
	return (uint8_t)((in & ~control & 0x7F) | (data & control & 0x7F) | (data & 0x80));
}

static uint8_t io_read(unsigned reg)
{
	switch (reg)
	{
	case 0:
		return 0xA0;    /* overseas, NTSC, no expansion unit, version 0 (no TMSS) */
	case 1:
	case 2:
	case 3:
		return port_read(reg - 1);
	case 4:
	case 5:
	case 6:
		return md_board.io_control[reg - 4];
	default:
		return 0;
	}
}

static void io_write(unsigned reg, uint8_t data)
{
	if (reg >= 1 && reg <= 3)
		md_board.io_data[reg - 1] = data;
	else if (reg >= 4 && reg <= 6)
		md_board.io_control[reg - 4] = data;
}

/* ---- the Z80's space, from either side ---- */

/* The Z80 takes the 68000's bus for an access: what it costs each of them (machine.h) */
static void z80_bus_access(void)
{
	md_board.z80_bus_accesses++;
	md_board.z80_bus_debt += MD_Z80_BUS_CPU_WAIT;
	md_board.z80_bus_wait = MD_Z80_BUS_Z80_WAIT;
}

static uint8_t bus_peek8(uint32_t address)
{
	const uint16_t word = md_peek16(address & ~1u);

	return (address & 1) ? (uint8_t)word : (uint8_t)(word >> 8);
}

uint8_t md_z80_read(uint16_t address)
{
	if (address < 0x4000)
		return md_board.z80_ram[address & 0x1FFF];
	if (address < 0x6000)
		return md_board.hooks.fm_read ? md_board.hooks.fm_read(address & 3) : 0;
	if (address < 0x8000)
		return 0xFF;
	z80_bus_access();
	if (md_board.hooks.data_read)
		md_board.hooks.data_read(1, md_board.z80_bank | (address & 0x7FFF));
	return bus_peek8(md_board.z80_bank | (address & 0x7FFF));
}

void md_z80_write(uint16_t address, uint8_t data)
{
	if (address < 0x4000)
		md_board.z80_ram[address & 0x1FFF] = data;
	else if (address < 0x6000)
	{
		if (md_board.hooks.fm_write)
			md_board.hooks.fm_write(address & 3, data);
	}
	else if (address < 0x6100)
		md_board.z80_bank = ((md_board.z80_bank >> 1) | ((uint32_t)(data & 1) << 23)) & 0xFF8000u;
	else if (address == 0x7F11 || address == 0x7F13 || address == 0x7F15 || address == 0x7F17)
	{
		if (md_board.hooks.psg_write)
			md_board.hooks.psg_write(data);
	}
	else if (address >= 0x8000)
	{
		const uint32_t target = md_board.z80_bank | (address & 0x7FFF);

		z80_bus_access();

		if (target >= 0xE00000)
			md_board.work_ram[target & 0xFFFF] = data;
	}
}

/* The 68000's byte at 'address' of the Z80's space (A00000-A0FFFF) */
static uint8_t z80_space_read(uint32_t address)
{
	address &= 0xFFFF;
	if (address >= 0x8000)
		return 0xFF;
	if (address >= 0x6000 && address < 0x8000)
		return 0xFF;
	return md_z80_read((uint16_t)address);
}

static void z80_space_write(uint32_t address, uint8_t data)
{
	address &= 0xFFFF;
	if (address < 0x8000)
		md_z80_write((uint16_t)address, data);
}

/* ---- the 68000's bus ---- */

uint16_t md_peek16(uint32_t address)
{
	address &= 0xFFFFFE;
	if (address < 0x400000)
	{
		if (!md_board.cartridge)
			return 0xFFFF;
		address &= md_board.cartridge_size - 1;
		return (uint16_t)(md_board.cartridge[address] << 8 | md_board.cartridge[address + 1]);
	}
	if (address >= 0xE00000)
	{
		address &= 0xFFFF;
		return (uint16_t)(md_board.work_ram[address] << 8 | md_board.work_ram[address + 1]);
	}
	return 0;
}

uint16_t md_read16(uint32_t address, uint16_t mem_mask)
{
	address &= 0xFFFFFF;
	if (address < 0x400000 || address >= 0xE00000)
	{
		refresh_wait();
		return md_peek16(address);
	}
	if (address >= 0xC00000 && address < 0xE00000)
	{
		if ((address & 0x07FFE0) == 0)
			return md_vdp_read(&md_board.vdp, address & 0x1F, mem_mask);
		return 0;
	}
	if (address < 0xA10000 && address >= 0xA00000)
	{
		uint8_t high, low;

		sound_sync();
		high = z80_space_read(address & ~1u);
		low = mem_mask == 0x00FF ? z80_space_read(address | 1) : high;
		return (uint16_t)(high << 8 | low);
	}
	if (address < 0xA10020 && address >= 0xA10000)
	{
		const uint8_t value = io_read((address & 0x1F) >> 1);

		return (uint16_t)(value << 8 | value);
	}
	if ((address & 0xFFFF00) == 0xA11100)
		return md_board.z80_busreq ? 0x0000 : 0x0101;
	return 0;
}

void md_write16(uint32_t address, uint16_t data, uint16_t mem_mask)
{
	address &= 0xFFFFFF;
	if (address >= 0xE00000)
	{
		refresh_wait();
		address &= 0xFFFE;
		if (mem_mask & 0xFF00)
			md_board.work_ram[address] = (uint8_t)(data >> 8);
		if (mem_mask & 0x00FF)
			md_board.work_ram[address + 1] = (uint8_t)data;
		return;
	}
	if (address < 0x400000)
	{
		refresh_wait();
		return;    /* (the cartridge has no RAM) */
	}
	if (address >= 0xC00000 && address < 0xE00000)
	{
		const unsigned offset = address & 0x1F;

		if ((address & 0x07FFE0) != 0)
			return;
		if (offset >= 0x10 && offset < 0x18)
		{
			if ((mem_mask & 0x00FF) && md_board.hooks.psg_write)
			{
				sound_sync();
				md_board.hooks.psg_write((uint8_t)data);
			}
			return;
		}
		md_vdp_write(&md_board.vdp, offset, data, mem_mask);
		return;
	}
	if (address < 0xA10000 && address >= 0xA00000)
	{
		sound_sync();
		if (mem_mask == 0x00FF)
			z80_space_write(address | 1, (uint8_t)data);
		else
			z80_space_write(address & ~1u, (uint8_t)(data >> 8));
		return;
	}
	if (address < 0xA10020 && address >= 0xA10000)
	{
		io_write((address & 0x1F) >> 1, (uint8_t)data);
		return;
	}
	if ((address & 0xFFFF00) == 0xA11100)
	{
		const uint8_t request = (uint8_t)(((mem_mask & 0xFF00) ? data >> 8 : data) & 1);

		sound_sync();
		md_board.z80_busreq = request;
		return;
	}
	if ((address & 0xFFFF00) == 0xA11200)
	{
		const uint8_t held = (uint8_t)!(((mem_mask & 0xFF00) ? data >> 8 : data) & 1);

		sound_sync();
		if (held != md_board.z80_reset && md_board.hooks.z80_reset)
			md_board.hooks.z80_reset(held);
		md_board.z80_reset = held;
		return;
	}
	/* (A14000, the TMSS register, and anything else: nothing) */
}

/* ---- interrupts and time ---- */

int md_irq_level(void)
{
	return md_vdp_irq_level(&md_board.vdp);
}

void md_irq_acknowledge(int level)
{
	md_vdp_acknowledge(&md_board.vdp, level);
}

void md_line_start(unsigned line)
{
	md_board.line = line;
	if (line == 0)
	{
		md_board.frame_start = present();
		md_board.frame++;
	}
	md_vdp_line(&md_board.vdp, line);
	if (line == MD_ACTIVE_LINES && md_board.hooks.z80_irq)
		md_board.hooks.z80_irq(1);
	else if (line == MD_ACTIVE_LINES + 1 && md_board.hooks.z80_irq)
		md_board.hooks.z80_irq(0);
}

unsigned md_current_line(void)
{
	const uint64_t now = present();

	if (now < md_board.frame_start)
		return 0;
	return (unsigned)(((now - md_board.frame_start) / MD_LINE_CLOCKS) % MD_LINES);
}

static uint64_t fnv1a(uint64_t hash, const void *data, size_t size)
{
	const uint8_t *bytes = (const uint8_t *)data;
	size_t at;

	for (at = 0; at < size; at++)
		hash = (hash ^ bytes[at]) * 0x100000001B3ull;
	return hash;
}

uint64_t md_state_hash(void)
{
	uint64_t hash = 0xCBF29CE484222325ull;

	hash = fnv1a(hash, md_board.work_ram, sizeof(md_board.work_ram));
	hash = fnv1a(hash, md_board.z80_ram, sizeof(md_board.z80_ram));
	hash = fnv1a(hash, md_board.vdp.vram, sizeof(md_board.vdp.vram));
	hash = fnv1a(hash, md_board.vdp.cram, sizeof(md_board.vdp.cram));
	hash = fnv1a(hash, md_board.vdp.vsram, sizeof(md_board.vdp.vsram));
	hash = fnv1a(hash, md_board.vdp.regs, sizeof(md_board.vdp.regs));
	return hash;
}
