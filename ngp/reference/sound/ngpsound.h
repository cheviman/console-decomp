/**
 *    Name:        ngpsound.h
 *    Description: the reference's sound side of the Neo Geo Pocket Color: MAME's Z80, MAME's T6W28 and the two
 *                 DACs, on the console's time
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Time is the console's master clock (6.144 MHz, a TLCS-900/H cycle), from the start of frame 0. The Z80
 *    runs at half of it (MAME's ngp.cpp: 6.144_MHz_XTAL/2), the T6W28 too (a sample every 16 of its clocks,
 *    every 32 master clocks). Every call that takes a time first runs the Z80 and the chips up to it, then
 *    does what it says, as MAME's scheduler brings the Z80 up to the present before a line of it changes; time
 *    never goes back. The Z80 starts stopped (MAME's machine_reset) and runs only while the TLCS-900/H lets it
 *    (0000B9: 55 starts it, AA stops it).
 *
 *    As MAME's ngp.cpp maps the Z80: 0000-0FFF the RAM it shares with the TLCS-900/H (007000-007FFF there),
 *    4000-4001 the T6W28 (written), 8000 the byte the two CPUs pass (the host's 0000BC), C000 a write raises
 *    the TLCS-900/H's INT5; a write to any of its I/O ports clears its own IRQ and the TLCS-900/H's INT5.
 */
#ifndef NGP_REFERENCE_SOUND_H
#define NGP_REFERENCE_SOUND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the sound side needs of the console: the byte the CPUs pass, and the TLCS-900/H's INT5 */
typedef struct ns_host
{
	uint8_t (*comm_read)(void);              /* the Z80 reads 8000 */
	void (*comm_write)(uint8_t data);        /* the Z80 writes 8000 */
	void (*int5)(int state);                 /* 1: a write to C000; 0: a write to an I/O port */
} ns_host;

/* Hooks the sound side to the console at time 'time': 'shared_ram' is the 4 KB both CPUs see (the host's
   007000-007FFF), 'host' what the Z80 reaches of the console. The Z80 is stopped, the T6W28 and the DACs
   are as MAME starts them */
void ns_attach(uint64_t time, uint8_t *shared_ram, const ns_host *host);

/* Runs the Z80 and the chips up to 'time' */
void ns_run_until(uint64_t time);

/* The TLCS-900/H's writes to the sound side, at 'time': */
void ns_z80_start(uint64_t time);                    /* 0000B9 = 55: resumed, reset, its IRQ cleared */
void ns_z80_stop(uint64_t time);                     /* 0000B9 = AA: stopped (suspended) */
void ns_z80_nmi(uint64_t time);                      /* 0000BA: an NMI pulse */
void ns_z80_irq(uint64_t time);                      /* timer 3's output (TO3) rose: the Z80's IRQ raised */
void ns_psg_write(uint64_t time, uint8_t data);      /* 0000A0 or 0000A1, while the TLCS-900/H drives it: the
                                                        T6W28's port 0, as MAME's io_w writes both (the right
                                                        side's volumes: the frequencies are port 1's) */
void ns_psg_enable(uint64_t time, int enable);       /* 0000B8 = 55 (1) or AA (0) */
void ns_dac_write(uint64_t time, int left, uint8_t data);   /* 0000A2 (right, left = 0) or 0000A3 (left = 1) */

/* Runs them up to 'time' and closes the frame's samples */
void ns_end_frame(uint64_t time);

/* FNV-1a hashes of the frame just closed: the T6W28's samples (its two outputs) and the DACs' levels at
   every one of those samples; and the Z80's state (its registers and the shared RAM) */
uint64_t ns_frame_samples_hash(void);
uint64_t ns_state_hash(void);

/* The frame's samples mixed as MAME's ngp.cpp routes them (the T6W28's outputs 0 and 1 at 0.50 to the left
   and the right, the left DAC at 0.25 to the left, the right DAC at 0.25 to the right, each DAC MAME's
   DAC_8BIT_R2R: -1 + byte / 128), the sums taken down to a whole number with 1.0 as 32768, stereo, 16-bit, at
   the T6W28's rate (ns_mix_rate: 192000 Hz) */
const int16_t *ns_frame_mix(size_t *count);
uint32_t ns_mix_rate(void);

/* The Z80's progress: the cycles it ran, and the addresses it executed (8 KB, address >> 3, bit address & 7) */
uint64_t ns_z80_cycles(void);
const uint8_t *ns_executed(void);

/* The address of the Z80's instruction running now */
uint16_t ns_z80_pc(void);

/* The runtime's T6W28 (ngp/t6w28.h), run beside MAME's with the same writes and the same runs of samples: the
   values (two a sample) where it gave another than MAME's since ns_attach */
uint64_t ns_t6w28_unlike(void);

/* The runtime's mix (ngp/mix.h) of the runtime's T6W28 samples with the DACs' bytes, beside MAME's sums: the
   values (two a sample) where it gave another since ns_attach */
uint64_t ns_mix_unlike(void);

#ifdef __cplusplus
}
#endif

#endif
