/**
 *    Name:        k2gevideo.h
 *    Description: the reference's video side of the Neo Geo Pocket Color: MAME's K2GE on the console's time
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    26-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Time is the console's master clock (6.144 MHz, a dot), from the start of frame 0, and the host gives the
 *    present. The chip's own events (the start of every line, where it draws the line before and raises or
 *    lowers VBLANK, and the end of HBLANK at dot 480) are timers the host fires at their times: it runs the
 *    CPUs up to nv_next_event and then calls nv_fire.
 */
#ifndef NGP_REFERENCE_K2GEVIDEO_H
#define NGP_REFERENCE_K2GEVIDEO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NV_WIDTH   160
#define NV_HEIGHT  152

/* Creates the K2GE, wired to the host's present and to the console's VBLANK (INT4) and HBLANK (TI0) lines, and
   starts and resets it as MAME does at power on */
void nv_attach(uint64_t (*now)(void), void (*vblank)(int state), void (*hblank)(int state));

/* The chip's 16 KB at 008000-00BFFF, as the CPU reads and writes them (offset 0000-3FFF) */
uint8_t nv_read(uint32_t offset);
void nv_write(uint32_t offset, uint8_t data);

/* The time of the chip's next event (UINT64_MAX: none) */
uint64_t nv_next_event(void);

/* Fires the events due by 'time', in order; '*present' is set to each one's time while it runs (the host's
   'now' reads it) */
void nv_fire(uint64_t time, uint64_t *present);

/* The picture as drawn so far: 160 x 152 pixels, 3 bytes each (R, G, B), the pens through the palette as it
   is now */
void nv_picture(uint8_t *rgb);

/* The chip's memory (16 KB), for the state's hash */
const uint8_t *nv_memory(void);

#ifdef __cplusplus
}
#endif

#endif
