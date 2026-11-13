/**
 *    Name:        machine.h
 *    Description: the Mega Drive (Genesis) as the 68000 and the Z80 see it: the memory maps, the work RAM,
 *                 the Z80's RAM and its bus, the I/O ports and the pads, the VDP, the interrupts, and the
 *                 time the 68000 loses to the board
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    An NTSC console of the first models, without TMSS (its version register says 0) and without an
 *    expansion unit, with two three-button pads. The board counts time in master clocks (53.693175 MHz):
 *    the 68000 runs at a seventh of it, the Z80 at a fifteenth, a line is 3420 clocks and a frame 262 lines,
 *    224 of them displayed. The host (the reference, or the game) owns the processors and the sound chips:
 *    it tells the board the time and runs the lines, and the board calls the host back through md_hooks.
 *
 *    The time the 68000 loses: a 68000-to-VDP DMA stops it (vdp.h); its writes to the VDP's data port wait
 *    while the FIFO is full; with 'refresh' on, the DRAM refresh costs it 2 cycles every 128 on its accesses
 *    to the cartridge and the work RAM; and each access of the Z80 to the 68000's bus (its bank window) takes
 *    8 cycles from the 68000 and 3 from the Z80. The refresh and the Z80's accesses are BlastEm's figures
 *    (genesis.c: REFRESH_INTERVAL, REFRESH_DELAY, z80_read_bank; its own notes call the 8 an estimate): to be
 *    measured against the console. The Z80 runs behind the 68000, so the 68000 pays for the Z80's accesses
 *    as a debt (z80_bus_debt) the host takes from its next slice.
 */
#ifndef MD_MACHINE_H
#define MD_MACHINE_H

#include "md/vdp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MD_MASTER_CLOCK     53693175u
#define MD_CPU_DIVIDER      7u
#define MD_Z80_DIVIDER      15u
#define MD_CPU_CLOCK        (MD_MASTER_CLOCK / MD_CPU_DIVIDER)
#define MD_Z80_CLOCK        (MD_MASTER_CLOCK / MD_Z80_DIVIDER)
#define MD_LINE_CLOCKS      3420u
#define MD_LINES            262u
#define MD_ACTIVE_LINES     224u
#define MD_FRAME_CLOCKS     (MD_LINE_CLOCKS * MD_LINES)

#define MD_REFRESH_INTERVAL 128u    /* 68000 cycles between DRAM refreshes */
#define MD_REFRESH_WAIT     2u      /* 68000 cycles each one costs */
#define MD_Z80_BUS_CPU_WAIT 8u      /* 68000 cycles a Z80 access to the 68000's bus takes from it */
#define MD_Z80_BUS_Z80_WAIT 3u      /* Z80 cycles the same access costs the Z80 (the arbitration) */

/* A pad's buttons as the board keeps them: 1 = held */
enum
{
	MD_UP = 0x01, MD_DOWN = 0x02, MD_LEFT = 0x04, MD_RIGHT = 0x08,
	MD_B = 0x10, MD_C = 0x20, MD_A = 0x40, MD_START = 0x80
};

/* What the board asks of the host */
typedef struct md_hooks
{
	uint64_t (*now)(void);                       /* the present, in master clocks */
	void (*stall)(uint64_t until);               /* the 68000 holds still until this master clock */
	void (*sound_sync)(void);                    /* the Z80 and the sound chips catch up with the present */
	uint8_t (*fm_read)(unsigned port);           /* the YM2612 (ports 0-3) */
	void (*fm_write)(unsigned port, uint8_t data);
	void (*psg_write)(uint8_t data);             /* the SN76489 */
	void (*z80_reset)(int held);                 /* the Z80 (and the YM2612) held in reset, or let go */
	void (*z80_irq)(int raised);                 /* the Z80's INT line */
	void (*data_read)(int z80, uint32_t address);   /* (optional) a byte of the 68000's bus read as data by a
	                                                   VDP DMA (z80 0) or by the Z80 through its window (z80 1) */
} md_hooks;

typedef struct md_machine
{
	const uint8_t *cartridge;        /* the cartridge the host plugged in (md_insert), big endian */
	uint32_t cartridge_size;         /* a power of two: the cartridge's space mirrors it */

	uint8_t work_ram[0x10000];
	uint8_t z80_ram[0x2000];
	md_vdp vdp;

	uint8_t pads[2];                 /* the buttons held on pads 1 and 2 (MD_UP...) */
	uint8_t io_data[3];              /* the I/O ports' data and control registers (pad 1, pad 2, expansion) */
	uint8_t io_control[3];

	uint8_t z80_busreq;              /* the 68000 asked for the Z80's bus */
	uint8_t z80_reset;               /* the Z80 is held in reset */
	uint32_t z80_bank;               /* the 32 KB of the 68000's bus the Z80 sees at 8000-FFFF */

	uint64_t frame;                  /* frames begun (line 0s) */
	uint64_t frame_start;            /* master clock of the current frame's line 0 */
	unsigned line;                   /* the line md_line_start began last */

	int refresh;                     /* 1: the DRAM refresh costs the 68000 its wait states */
	uint64_t refresh_next;           /* the 68000 cycle of the next refresh */
	uint64_t refresh_cycles;         /* statistics: 68000 cycles lost to it */
	uint64_t z80_bus_accesses;       /* statistics: the Z80's accesses to the 68000's bus */
	uint32_t z80_bus_debt;           /* 68000 cycles those accesses took that the host has not charged yet */
	uint32_t z80_bus_wait;           /* Z80 cycles the last of them took from the Z80, for the host to charge */

	md_hooks hooks;
} md_machine;

extern md_machine md_board;

/* The cartridge in the slot: 'size' bytes (a power of two) the host keeps while the board runs */
void md_insert(const uint8_t *cartridge, uint32_t size);

/* Power on: memories cleared, the VDP and the I/O as they come up, the Z80 held in reset; the cartridge
   and the hooks stay */
void md_power_on(void);

/* The 68000's bus: a 16-bit word at an even address, MAME's mem_mask for the bytes */
uint16_t md_read16(uint32_t address, uint16_t mem_mask);
void md_write16(uint32_t address, uint16_t data, uint16_t mem_mask);
/* A word of the cartridge or the work RAM without side effects or waits (the DMA's source, tools) */
uint16_t md_peek16(uint32_t address);

/* The Z80's bus */
uint8_t md_z80_read(uint16_t address);
void md_z80_write(uint16_t address, uint8_t data);
/* Whether the Z80 runs now (neither held in reset nor stopped for the 68000) */
int md_z80_running(void);

/* The 68000's interrupt level, and its acknowledge of one */
int md_irq_level(void);
void md_irq_acknowledge(int level);

/* The start of line 'line' of the frame (0-261), at the present: line 0 begins a frame */
void md_line_start(unsigned line);

/* The line and the master clock within it at the present */
unsigned md_current_line(void);

/* A hash of the program's state: the work RAM, the Z80's RAM, the VDP's memories and registers */
uint64_t md_state_hash(void);

#ifdef __cplusplus
}
#endif

#endif
