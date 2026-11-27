/**
 *    Name:        machine.c
 *    Description: the Neo Geo Pocket Color as the TMP95C061 sees it (ngp/machine.h)
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The TMP95C061's map: its own registers at 000000-00007F (the host's), the I/O at 000080-0000BF, the
 *    work RAM at 004000-006FFF, the Z80's RAM at 007000-007FFF, the K2GE at 008000-00BFFF (the host's), the
 *    cartridge's flash chips at 200000-3FFFFF and 800000-9FFFFF, and the BIOS at FF0000-FFFFFF. The I/O
 *    holds the real-time clock, the pad, the power button, the sound's ports, and the Z80's lines. The
 *    cartridge is read in place; the board keeps in its RAM the save area and the flash after it (NGP_SAVE_SIZE
 *    bytes from save_base) and the 16 ID bytes of each chip (at offsets 0, 7C000, FC000 and 1FC000 of the chip),
 *    restoring the ID bytes to what they held at power on whenever the chip returns to reading.
 *    MAME 0.289's ngp.cpp (BSD-3-Clause, Wilbert Pol) provides the behaviour: its memory map, its io_r and
 *    io_w, its real-time clock, its flash cartridge, and its NVRAM.
 */
#include "ngp/machine.h"
#include "ngp/script.h"

#include <string.h>

ngp_machine ngp_board;

/* The four places in a chip where the flash ID bytes live (MAME's flash_w overwrites these 16 bytes) */
static const uint32_t id_places[4] = { 0x000000, 0x07C000, 0x0FC000, 0x1FC000 };

/* ---- the ID bytes, the clock's numbers, and the block erase ---- */

/* The index of a chip's offset among its 16 ID bytes, or -1. The four places are multiples of 0x4000, so an
   offset whose bits 2-13 are not all 0 is none of them: a quick answer for every read of the cartridge */
static int id_index(uint32_t o)
{
	int i;

	if (o & 0x3FFC)
		return -1;
	for (i = 0; i < 16; i++)
	{
		if (o == id_places[i >> 2] + (i & 3))
			return i;
	}
	return -1;
}

/* A byte of the cartridge's space as the board keeps it, the ID bytes aside: the save area's in RAM, the
   cartridge's read in place, and 0 past the cartridge */
static uint8_t stored_byte(uint32_t offset)
{
	const uint32_t base = ngp_board.save_base;

	if (offset >= base && offset - base < NGP_SAVE_SIZE)
		return ngp_board.save[offset - base];
	if (offset < ngp_board.cartridge_size)
		return ngp_board.cartridge[offset];
	return 0;
}

/* Converts a number to BCD for the real-time clock's I/O bytes */
static uint8_t to_bcd(int value)
{
	return (uint8_t)(((value / 10) % 10) << 4 | (value % 10));
}

/* Converts a BCD byte from the real-time clock's I/O to a number */
static int from_bcd(uint8_t value)
{
	return (value >> 4) * 10 + (value & 0x0f);
}

/* The number of days in a month, with February's leap year */
static int days_in_month(int month, int year)
{
	static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	const int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;

	return month == 2 && leap ? 29 : days[(month - 1) % 12];
}

/* MAME's block erase (flash_w's memset of FF) on what the board keeps: the ID bytes and the save area in the
   block, and a count of what it cannot keep */
static void erase_block(unsigned chip, uint32_t start, uint32_t size)
{
	const uint32_t block_start = chip * NGP_CHIP_SIZE + start;
	const uint32_t block_end = block_start + size;
	const uint32_t save_start = ngp_board.save_base;
	const uint32_t save_end = save_start + NGP_SAVE_SIZE;
	int i;

	/* MAME's erase sets the ID bytes in the block to FF; they read so until the chip returns to reading, when
	   they are put back */
	for (i = 0; i < 16; i++)
	{
		uint32_t o = id_places[i >> 2] + (i & 3);

		if (o >= start && o - start < size)
			ngp_board.flash[chip].id_data[i] = 0xFF;
	}

	/* The part of the save area that lies in the block reads FF */
	if (block_start < save_end && block_end > save_start)
	{
		uint32_t lo = block_start > save_start ? block_start : save_start;
		uint32_t hi = block_end < save_end ? block_end : save_end;
		uint32_t offset = lo - save_start;

		memset(&ngp_board.save[offset], 0xFF, hi - lo);
	}

	/* A block that reaches past the save area: MAME's flash would have changed bytes the board does not keep,
	   so it is counted, and a run says whether the board kept everything */
	if (block_start < save_start || block_end > save_end)
		ngp_board.flash_lost++;
}

/* The block bounds for the device's erase command, as MAME's flash_w computes them */
static void erase_bounds(uint8_t device_id, uint32_t o, uint32_t *start, uint32_t *size)
{
	switch (device_id)
	{
	case 0xAB:
		if (o < 0x70000)
			*start = o & 0x70000;
		else
		{
			if (o & 0x8000)
			{
				if (o & 0x4000)
				{
					*start = o & 0x7C000;
					*size = 0x4000;
				}
				else
				{
					*start = o & 0x7E000;
					*size = 0x2000;
				}
			}
			else
			{
				*start = o & 0x78000;
				*size = 0x8000;
			}
		}
		break;
	case 0x2C:
		if (o < 0xF0000)
			*start = o & 0xF0000;
		else
		{
			if (o & 0x8000)
			{
				if (o & 0x4000)
				{
					*start = o & 0xFC000;
					*size = 0x4000;
				}
				else
				{
					*start = o & 0xFE000;
					*size = 0x2000;
				}
			}
			else
			{
				*start = o & 0xF8000;
				*size = 0x8000;
			}
		}
		break;
	case 0x2F:
		if (o < 0x1F0000)
			*start = o & 0x1F0000;
		else
		{
			if (o & 0x8000)
			{
				if (o & 0x4000)
				{
					*start = o & 0x1FC000;
					*size = 0x4000;
				}
				else
				{
					*start = o & 0x1FE000;
					*size = 0x2000;
				}
			}
			else
			{
				*start = o & 0x1F8000;
				*size = 0x8000;
			}
		}
		break;
	}
}

/* ---- the I/O ---- */

/* MAME's io_r: the clock in BCD, the pad, the power button, and the I/O bytes as last written */
static uint8_t io_read(uint32_t offset)
{
	uint8_t data = ngp_board.io[offset];

	switch (offset)
	{
	case 0x11: return to_bcd(ngp_board.clock.year % 100);
	case 0x12: return to_bcd(ngp_board.clock.month);
	case 0x13: return to_bcd(ngp_board.clock.day);
	case 0x14: return to_bcd(ngp_board.clock.hour);
	case 0x15: return to_bcd(ngp_board.clock.minute);
	case 0x16: return to_bcd(ngp_board.clock.second);
	case 0x30:
		return (uint8_t)(ngp_board.controls & 0x7F);
	case 0x31:
		data = (ngp_board.controls & NGP_POWER) ? 0x01 : 0x00;
		data |= 0x02;
		break;
	}
	return data;
}

/* MAME's io_w: the clock in BCD, the sound's ports, the Z80's lines, and the I/O bytes as last written */
static void io_write(uint32_t offset, uint8_t data)
{
	switch (offset)
	{
	case 0x11: ngp_board.clock.year = from_bcd(data); break;
	case 0x12: ngp_board.clock.month = from_bcd(data); break;
	case 0x13: ngp_board.clock.day = from_bcd(data); break;
	case 0x14: ngp_board.clock.hour = from_bcd(data); break;
	case 0x15: ngp_board.clock.minute = from_bcd(data); break;
	case 0x16: ngp_board.clock.second = from_bcd(data); break;
	case 0x20:
	case 0x21:
		if (ngp_board.io[0x38] == 0x55 && ngp_board.io[0x39] == 0xAA)
		{
			if (ngp_board.hooks.psg_write)
				ngp_board.hooks.psg_write(data);
		}
		break;
	case 0x22:
		if (ngp_board.hooks.dac_write)
			ngp_board.hooks.dac_write(0, data);
		break;
	case 0x23:
		if (ngp_board.hooks.dac_write)
			ngp_board.hooks.dac_write(1, data);
		break;
	case 0x38:
		if ((data == 0x55 || data == 0xAA) && ngp_board.hooks.psg_enable)
			ngp_board.hooks.psg_enable(data == 0x55);
		break;
	case 0x39:
		if ((data == 0x55 || data == 0xAA) && ngp_board.hooks.z80_run)
			ngp_board.hooks.z80_run(data == 0x55);
		break;
	case 0x3A:
		if (ngp_board.hooks.z80_nmi)
			ngp_board.hooks.z80_nmi();
		break;
	}
	ngp_board.io[offset] = data;
}

/* ---- the flash cartridge ---- */

/* MAME's flash_w: the command sequence, the program, the erase, and the ID bytes */
static void flash_write(unsigned chip, uint32_t o, uint8_t data)
{
	ngp_flash_chip *fc = &ngp_board.flash[chip];

	if (!fc->present)
		return;

	switch (fc->state)
	{
	case NGP_FLASH_READ:
		if (o == 0x5555 && data == 0xAA)
			fc->state = NGP_FLASH_PROG1;
		fc->command = 0;
		break;
	case NGP_FLASH_PROG1:
		if (o == 0x2AAA && data == 0x55)
			fc->state = NGP_FLASH_PROG2;
		else
			fc->state = NGP_FLASH_READ;
		break;
	case NGP_FLASH_PROG2:
		if (data == 0x30)
		{
			if (fc->command == 0x80)
			{
				uint32_t size = 0x10000;
				uint32_t start = 0;

				fc->state = NGP_FLASH_AUTO_BLOCK_ERASE;
				erase_bounds(fc->device_id, o, &start, &size);
				erase_block(chip, start, size);
			}
			else
				fc->state = NGP_FLASH_READ;
		}
		else if (o == 0x5555)
		{
			switch (data)
			{
			case 0x80:
				fc->command = 0x80;
				fc->state = NGP_FLASH_COMMAND;
				break;
			case 0x90:
			{
				int i;

				for (i = 0; i < 16; i++)
				{
					fc->id_data[i] = (i & 3) == 0 ? fc->manufacturer_id
						: (i & 3) == 1 ? fc->device_id
						: (i & 3) == 2 ? 0x02
						: 0x80;
				}
				fc->state = NGP_FLASH_ID_READ;
				break;
			}
			case 0x9A:
				if (fc->command == 0x9A)
					fc->state = NGP_FLASH_BLOCK_PROTECT;
				else
				{
					fc->command = 0x9A;
					fc->state = NGP_FLASH_COMMAND;
				}
				break;
			case 0xA0:
				fc->state = NGP_FLASH_AUTO_PROGRAM;
				break;
			case 0xF0:
			default:
				fc->state = NGP_FLASH_READ;
				break;
			}
		}
		else
			fc->state = NGP_FLASH_READ;
		break;
	case NGP_FLASH_COMMAND:
		if (o == 0x5555 && data == 0xAA)
			fc->state = NGP_FLASH_PROG1;
		else
			fc->state = NGP_FLASH_READ;
		break;
	case NGP_FLASH_ID_READ:
		if (o == 0x5555 && data == 0xAA)
			fc->state = NGP_FLASH_PROG1;
		else
			fc->state = NGP_FLASH_READ;
		fc->command = 0;
		break;
	case NGP_FLASH_AUTO_PROGRAM:
	{
		int idx = id_index(o);

		if (idx >= 0)
			fc->id_data[idx] &= data;
		else
		{
			uint32_t offset = chip * NGP_CHIP_SIZE + o;
			uint32_t base = ngp_board.save_base;

			if (offset >= base && offset - base < NGP_SAVE_SIZE)
				ngp_board.save[offset - base] &= data;
			else
			{
				uint8_t current;

				if (offset < ngp_board.cartridge_size)
					current = ngp_board.cartridge[offset];
				else
					current = 0;
				if ((current & data) != current)
					ngp_board.flash_lost++;
			}
		}
		fc->state = NGP_FLASH_READ;
		break;
	}
	case NGP_FLASH_AUTO_CHIP_ERASE:
		fc->state = NGP_FLASH_READ;
		break;
	case NGP_FLASH_AUTO_BLOCK_ERASE:
		fc->state = NGP_FLASH_READ;
		break;
	case NGP_FLASH_BLOCK_PROTECT:
		fc->state = NGP_FLASH_READ;
		break;
	}

	if (fc->state == NGP_FLASH_READ)
	{
		memcpy(fc->id_data, fc->org_data, 16);
		fc->command = 0;
	}
}

/* ---- what the program reads of the cartridge ---- */

/* What the program reads in the cartridge's space now, without side effects */
uint8_t ngp_cartridge_byte(uint32_t offset)
{
	uint32_t chip = offset >> 21;
	uint32_t o = offset & 0x1FFFFF;
	int idx = id_index(o);

	if (idx >= 0)
		return ngp_board.flash[chip].id_data[idx];
	return stored_byte(offset);
}

/* ---- the TMP95C061's bus ---- */

/* A byte read from the TMP95C061's bus, as MAME's memory map gives it */
uint8_t ngp_read8(uint32_t address)
{
	address &= 0xFFFFFF;

	if (address < 0x80)
	{
		if (ngp_board.hooks.internal_read)
			return ngp_board.hooks.internal_read(address);
		return 0;
	}
	if (address < 0xC0)
		return io_read(address - 0x80);
	if (address >= 0x4000 && address < 0x7000)
		return ngp_board.main_ram[address - 0x4000];
	if (address >= 0x7000 && address < 0x8000)
		return ngp_board.sound_ram[address - 0x7000];
	if (address >= 0x8000 && address < 0xC000)
	{
		if (ngp_board.hooks.k2ge_read)
			return ngp_board.hooks.k2ge_read(address - 0x8000);
		return 0;
	}
	if (address >= 0x200000 && address < 0x400000)
	{
		if (ngp_board.hooks.data_read)
			ngp_board.hooks.data_read(address);
		return ngp_cartridge_byte(address - 0x200000);
	}
	if (address >= 0x800000 && address < 0xA00000)
	{
		if (ngp_board.hooks.data_read)
			ngp_board.hooks.data_read(address);
		return ngp_cartridge_byte(address - 0x800000 + 0x200000);
	}
	if (address >= 0xFF0000)
	{
		if (ngp_board.bios)
			return ngp_board.bios[address - 0xFF0000];
		return 0;
	}
	return 0;
}

/* A byte write to the TMP95C061's bus, as MAME's memory map gives it */
void ngp_write8(uint32_t address, uint8_t data)
{
	address &= 0xFFFFFF;

	if (address < 0x80)
	{
		if (ngp_board.hooks.internal_write)
			ngp_board.hooks.internal_write(address, data);
	}
	else if (address < 0xC0)
		io_write(address - 0x80, data);
	else if (address >= 0x4000 && address < 0x7000)
		ngp_board.main_ram[address - 0x4000] = data;
	else if (address >= 0x7000 && address < 0x8000)
		ngp_board.sound_ram[address - 0x7000] = data;
	else if (address >= 0x8000 && address < 0xC000)
	{
		if (ngp_board.hooks.k2ge_write)
			ngp_board.hooks.k2ge_write(address - 0x8000, data);
	}
	else if (address >= 0x200000 && address < 0x400000)
	{
		ngp_board.flash_writes++;
		flash_write(0, address - 0x200000, data);
	}
	else if (address >= 0x800000 && address < 0xA00000)
	{
		ngp_board.flash_writes++;
		flash_write(1, address - 0x800000, data);
	}
}

/* ---- the cartridge in the slot, and power on ---- */

/* The cartridge goes in: the chips as MAME's load_ngp_cart sets them (Toshiba IDs by size, chip 1 for 4 MB),
   the save area taken from the cartridge, and their 16 ID bytes as machine_start takes them */
void ngp_insert(const uint8_t *cartridge, uint32_t size, uint32_t save_base)
{
	uint32_t i;
	unsigned chip;

	ngp_board.cartridge = cartridge;
	ngp_board.cartridge_size = size;
	ngp_board.save_base = save_base;

	for (i = 0; i < NGP_SAVE_SIZE; i++)
	{
		if (save_base + i < size)
			ngp_board.save[i] = cartridge[save_base + i];
		else
			ngp_board.save[i] = 0;
	}

	memset(ngp_board.flash, 0, sizeof ngp_board.flash);

	ngp_board.flash[0].present = 1;
	ngp_board.flash[0].manufacturer_id = 0x98;
	ngp_board.flash[0].state = NGP_FLASH_READ;

	switch (size)
	{
	case 0x8000:
	case 0x80000:
		ngp_board.flash[0].device_id = 0xAB;
		break;
	case 0x100000:
		ngp_board.flash[0].device_id = 0x2C;
		break;
	case 0x200000:
	case 0x400000:
		ngp_board.flash[0].device_id = 0x2F;
		break;
	}

	if (size == 0x400000)
	{
		ngp_board.flash[1].present = 1;
		ngp_board.flash[1].manufacturer_id = 0x98;
		ngp_board.flash[1].device_id = 0x2F;
		ngp_board.flash[1].state = NGP_FLASH_READ;
	}

	/* MAME's machine_start takes what the 16 bytes hold as the cartridge goes in */
	for (chip = 0; chip < 2; chip++)
	{
		for (i = 0; i < 16; i++)
		{
			uint32_t o = id_places[i >> 2] + (i & 3);
			uint32_t offset = chip * NGP_CHIP_SIZE + o;

			ngp_board.flash[chip].org_data[i] = stored_byte(offset);
			ngp_board.flash[chip].id_data[i] = ngp_board.flash[chip].org_data[i];
		}
	}
}

/* Power on: the RAM and the I/O cleared as a new console's, the flash chips reading, and TO3 low (MAME's
   machine_reset); what the slot holds, the clock, the controls and the hooks stay */
void ngp_power_on(void)
{
	memset(ngp_board.main_ram, 0, sizeof ngp_board.main_ram);
	memset(ngp_board.sound_ram, 0, sizeof ngp_board.sound_ram);
	memset(ngp_board.io, 0, sizeof ngp_board.io);
	ngp_board.old_to3 = 0;
	ngp_board.flash_writes = 0;
	ngp_board.flash_lost = 0;

	ngp_board.flash[0].state = NGP_FLASH_READ;
	ngp_board.flash[0].command = 0;
	memcpy(ngp_board.flash[0].id_data, ngp_board.flash[0].org_data, 16);

	ngp_board.flash[1].state = NGP_FLASH_READ;
	ngp_board.flash[1].command = 0;
	memcpy(ngp_board.flash[1].id_data, ngp_board.flash[1].org_data, 16);
}

/* ---- the lines and the clock ---- */

/* MAME's tlcs900_porta: TO3 (bit 3) rising raises the Z80's IRQ */
void ngp_port_a(uint8_t data)
{
	const uint8_t to3 = (uint8_t)((data & 0x08) ? 1 : 0);

	if (to3 && !ngp_board.old_to3)
	{
		if (ngp_board.hooks.z80_irq)
			ngp_board.hooks.z80_irq();
	}
	ngp_board.old_to3 = to3;
}

/* MAME's power_callback: a change of the power button raises or lowers the TMP95C061's NMI when the BIOS lets it */
void ngp_set_controls(uint8_t controls)
{
	const uint8_t old = ngp_board.controls;
	const int old_power = (old & NGP_POWER) ? 1 : 0;
	const int new_power = (controls & NGP_POWER) ? 1 : 0;

	if (old_power != new_power)
	{
		if (ngp_board.io[0x33] & 0x04)
		{
			if (ngp_board.hooks.nmi)
				ngp_board.hooks.nmi(new_power ? 0 : 1);
		}
	}
	ngp_board.controls = controls;
}

/* The Z80 reads the communication byte (I/O 3C) */
uint8_t ngp_comm_read(void)
{
	return ngp_board.io[0x3C];
}

/* The Z80 writes the communication byte */
void ngp_comm_write(uint8_t data)
{
	ngp_board.io[0x3C] = data;
}

/* MAME's seconds_callback: a second of the real-time clock has passed */
void ngp_clock_second(void)
{
	ngp_clock *c = &ngp_board.clock;

	if (++c->second < 60)
		return;
	c->second = 0;
	if (++c->minute < 60)
		return;
	c->minute = 0;
	if (++c->hour < 24)
		return;
	c->hour = 0;
	if (++c->day <= days_in_month(c->month, c->year))
		return;
	c->day = 1;
	if (++c->month <= 12)
		return;
	c->month = 1;
	c->year++;
}

/* MAME's nvram_read: read into the work RAM and the I/O */
void ngp_nvram_read(const uint8_t *data)
{
	memcpy(ngp_board.main_ram, data, 0x3000);
	memcpy(ngp_board.io, data + 0x3000, 0x20);
}

/* MAME's nvram_write: written from the work RAM and the I/O */
void ngp_nvram_write(uint8_t *data)
{
	memcpy(data, ngp_board.main_ram, 0x3000);
	memcpy(data + 0x3000, ngp_board.io, 0x20);
}
