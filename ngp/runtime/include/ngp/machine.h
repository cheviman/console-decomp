/**
 *    Name:        machine.h
 *    Description: the Neo Geo Pocket Color as the TMP95C061 sees it: the memory map, the work RAM, the Z80's
 *                 RAM, the I/O at 000080-0000BF (the clock, the pad, the power button, the sound's ports, the
 *                 Z80's lines), the flash cartridge, and the NVRAM
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    What MAME 0.289's ngp.cpp does (BSD-3-Clause, Wilbert Pol) without MAME's device framework: its memory
 *    map, its io_r and io_w, its real-time clock, its flash cartridge (flash_w, load_ngp_cart, machine_start),
 *    TO3 on port A raising the Z80's IRQ, the power button's NMI and its NVRAM (ngpc.nv). The host (the
 *    reference, or the game) owns the processors, the K2GE and the sound chips, and the time: the board calls
 *    it back through ngp_hooks.
 *
 *    The TMP95C061's map: its own registers at 000000-00007F (the host's), the I/O at 000080-0000BF, the work
 *    RAM at 004000-006FFF, the Z80's RAM at 007000-007FFF (the Z80's 0000-0FFF), the K2GE at 008000-00BFFF
 *    (the host's), the cartridge's flash chips at 200000-3FFFFF and 800000-9FFFFF, the BIOS at FF0000-FFFFFF.
 *    The bus is 8 bits wide: a word or a long is its bytes in order, the lowest address first.
 *
 *    The cartridge is read in place and never written: the board keeps in its RAM what the program can
 *    change of it, NGP_SAVE_SIZE bytes from the place the host names (a game's save area and the flash after
 *    it: the BIOS programs bytes there too, as it starts; Sonic Pocket Adventure's 3F8000-3FBFFF is its own,
 *    and the BIOS wrote 3FE000 or 3FDF00, a byte of AA, depending on its NVRAM) and, for each chip,
 *    the 16 bytes MAME's flash_w overwrites with its IDs (4 at offsets 0, 7C000, FC000 and 1FC000 of the chip)
 *    and puts back from what they held at power on whenever the chip returns to reading. MAME's flash
 *    changes the whole cartridge; a program or an erase that MAME would have done outside the save area is
 *    counted in flash_lost instead, so a run says whether the board kept everything.
 */
#ifndef NGP_MACHINE_H
#define NGP_MACHINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MAME's ngp.cpp: the master clock, and the screen's set_raw (515 dots a line, 199 lines, VBLANK from 152) */
#define NGP_MASTER_CLOCK    6144000u
#define NGP_LINE_CLOCKS     515u
#define NGP_LINES           199u
#define NGP_VBLANK_LINE     152u
#define NGP_FRAME_CLOCKS    (NGP_LINE_CLOCKS * NGP_LINES)

#define NGP_CARTRIDGE_SPACE 0x400000u   /* both flash chips, 2 MB each (MAME's load_ngp_cart allocates 4 MB) */
#define NGP_CHIP_SIZE       0x200000u
#define NGP_SAVE_SIZE       0x8000u     /* the flash the board keeps in RAM, from the host's save_base */
#define NGP_NVRAM_SIZE      0x3020u     /* MAME's ngpc.nv: the work RAM, then the first 32 bytes of the I/O */

/* MAME's ngp.cpp, lines 122-133: the flash chip's command states */
enum
{
	NGP_FLASH_READ,                 /* xxxx F0 or 5555 AA 2AAA 55 5555 F0 */
	NGP_FLASH_PROG1,
	NGP_FLASH_PROG2,
	NGP_FLASH_COMMAND,
	NGP_FLASH_ID_READ,              /* 5555 AA 2AAA 55 5555 90 */
	NGP_FLASH_AUTO_PROGRAM,         /* 5555 AA 2AAA 55 5555 A0 address data */
	NGP_FLASH_AUTO_CHIP_ERASE,      /* 5555 AA 2AAA 55 5555 80 5555 AA 2AAA 55 5555 10 (never entered) */
	NGP_FLASH_AUTO_BLOCK_ERASE,     /* 5555 AA 2AAA 55 5555 80 5555 AA 2AAA 55 block_address 30 */
	NGP_FLASH_BLOCK_PROTECT         /* 5555 AA 2AAA 55 5555 9A 5555 AA 2AAA 55 5555 9A */
};

/* What the board asks of the host; a hook left NULL does nothing (a read gives 0) */
typedef struct ngp_hooks
{
	uint8_t (*internal_read)(uint32_t address);          /* the TMP95C061's own registers, 000000-00007F */
	void (*internal_write)(uint32_t address, uint8_t data);
	uint8_t (*k2ge_read)(uint32_t offset);               /* the K2GE, 008000-00BFFF (offset 0000-3FFF) */
	void (*k2ge_write)(uint32_t offset, uint8_t data);
	void (*psg_write)(uint8_t data);                     /* the T6W28's port 0 (I/O 20 and 21) */
	void (*psg_enable)(int on);                          /* I/O 38: 55 on, AA off */
	void (*dac_write)(unsigned which, uint8_t data);     /* 0 the right DAC (I/O 22), 1 the left (I/O 23) */
	void (*z80_run)(int run);                            /* I/O 39: 55 starts the Z80, AA stops it */
	void (*z80_nmi)(void);                               /* a write of I/O 3A */
	void (*z80_irq)(void);                               /* TO3 rising on port A raises the Z80's IRQ */
	void (*nmi)(int asserted);                           /* the TMP95C061's NMI line (the power button) */
	void (*data_read)(uint32_t address);                 /* (optional) a byte of the cartridge read */
} ngp_hooks;

/* A flash chip, as MAME's ngp_state keeps it, with what reads give at its 16 bytes of IDs */
typedef struct ngp_flash_chip
{
	uint8_t present;
	uint8_t manufacturer_id;
	uint8_t device_id;
	uint8_t state;                   /* NGP_FLASH_READ... */
	uint8_t command;                 /* MAME's command[0] */
	uint8_t org_data[16];            /* the 16 bytes at 0, 7C000, FC000 and 1FC000 (4 each) at power on */
	uint8_t id_data[16];             /* what reads give there now */
} ngp_flash_chip;

/* The real-time clock (MAME's device_rtc_interface as ngp.cpp uses it), in plain numbers */
typedef struct ngp_clock
{
	int year, month, day, hour, minute, second;
} ngp_clock;

typedef struct ngp_machine
{
	const uint8_t *cartridge;        /* the cartridge the host plugged in (ngp_insert), read in place */
	uint32_t cartridge_size;         /* 32 KB, 512 KB, 1, 2 or 4 MB; past it the cartridge's space reads 0 */
	uint32_t save_base;              /* where the save area starts in the cartridge's space (0-3FFFFF) */
	uint8_t save[NGP_SAVE_SIZE];     /* the save area and the flash after it, as the program left them */
	ngp_flash_chip flash[2];
	const uint8_t *bios;             /* the BIOS's 64 KB at FF0000 (the reference's), or NULL: it reads 0 */

	uint8_t main_ram[0x3000];        /* 004000-006FFF */
	uint8_t sound_ram[0x1000];       /* 007000-007FFF */
	uint8_t io[0x40];                /* 000080-0000BF as last written */

	uint8_t controls;                /* held: the pad's NGP_UP... and NGP_POWER (ngp/script.h) */
	uint8_t old_to3;                 /* port A's TO3 as last written */
	ngp_clock clock;

	uint64_t flash_writes;           /* statistics: writes to the cartridge's space */
	uint64_t flash_lost;             /* programs and erases MAME would have done outside the save area */

	ngp_hooks hooks;
} ngp_machine;

extern ngp_machine ngp_board;

/* The cartridge in the slot: 'size' bytes the host keeps while the board runs, its save area at 'save_base'
   (NGP_SAVE_SIZE bytes, in the cartridge's space) taken from the cartridge. The chips as MAME's
   load_ngp_cart sets them (chip 0, and chip 1 for 4 MB; Toshiba IDs by size) and their 16 bytes of IDs as
   machine_start takes them */
void ngp_insert(const uint8_t *cartridge, uint32_t size, uint32_t save_base);

/* Power on: the RAM and the I/O cleared, the flash chips reading, TO3 low (MAME's machine_reset); the
   cartridge, its save area, the BIOS, the clock, the controls and the hooks stay */
void ngp_power_on(void);

/* The TMP95C061's bus, a byte */
uint8_t ngp_read8(uint32_t address);
void ngp_write8(uint32_t address, uint8_t data);

/* What the program reads in the cartridge's space (0-3FFFFF: chip 0, then chip 1) now, without side effects */
uint8_t ngp_cartridge_byte(uint32_t offset);

/* The TMP95C061 writes port A: TO3 (bit 3) rising raises the Z80's IRQ (MAME's tlcs900_porta) */
void ngp_port_a(uint8_t data);

/* The controls held from now (NGP_UP... NGP_POWER): a change of the power button raises or lowers the
   TMP95C061's NMI when the BIOS lets it (I/O 33 bit 2, MAME's power_callback) */
void ngp_set_controls(uint8_t controls);

/* The Z80's side of the communication byte (I/O 3C) */
uint8_t ngp_comm_read(void);
void ngp_comm_write(uint8_t data);

/* A second of the real-time clock has passed (MAME's seconds_callback) */
void ngp_clock_second(void);

/* MAME's ngpc.nv (NGP_NVRAM_SIZE bytes): read into the work RAM and the I/O, or written from them */
void ngp_nvram_read(const uint8_t *data);
void ngp_nvram_write(uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif
