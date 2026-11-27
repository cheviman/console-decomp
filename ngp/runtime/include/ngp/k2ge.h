/**
 *    Name:        k2ge.h
 *    Description: the Neo Geo Pocket Color's video chip (K2GE): a line of the picture from the chip's memory,
 *                 its registers and its lines' timing
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The chip's 16 KB (the console's 008000-00BFFF: its registers, palettes, sprites, scroll planes and 2-bit
 *    characters) are read in place, where the console keeps them: nothing is copied or allocated, and a line
 *    is drawn on its own, as the console's time reaches it. What it draws is MAME 0.289's k1ge.cpp (the
 *    K2GE, and its K1GE-compatible mode), which ngpref runs as the reference.
 *
 *    A line is pens, as MAME's bitmap holds them: 0-191 the colour palettes' entries (sprites 00-3F, scroll
 *    plane 1 40-7F, scroll plane 2 80-BF), 192-215 the K1GE-compatible mode's (each through its register at
 *    100-117), the background's and the outside of the window's. A pen becomes a colour through the
 *    registers and palettes as they are when the picture is shown (ngp_k2ge_pen_colour), as MAME does at
 *    the end of a frame.
 *
 *    When (MAME's timer_callback): the window (WBA.H, WBA.V, WSI.H, WSI.V at 002-005) is taken at the start
 *    of line 0, and line N (0-151) is drawn at the start of line N + 1, from the memory as it is then.
 *
 *    The registers and the timing are MAME's too (k2ge_device::write, k1ge_device::read, timer_callback,
 *    hblank_on_timer_callback, device_reset). A frame is 199 lines of 515 dots (a dot a master clock,
 *    6.144 MHz): line 0 starts with the frame. At the start of every line the chip sets or clears the VBLANK
 *    status (bit 6 of 010) and calls its VBLANK line (the TMP95C061's INT4) and its HBLANK line (TI0), and
 *    on most lines it lowers HBLANK again at dot 480. A line is "called" as MAME calls its devcb: with a
 *    state, even when the line is at that state already, which the host passes on as MAME's ngp.cpp does.
 */
#ifndef NGP_K2GE_H
#define NGP_K2GE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NGP_K2GE_SIZE        0x4000
#define NGP_K2GE_WIDTH       160
#define NGP_K2GE_HEIGHT      152

#define NGP_K2GE_PEN_MONO    192       /* the first of the K1GE-compatible mode's 24 pens */
#define NGP_K2GE_PEN_BG      216       /* the background's */
#define NGP_K2GE_PEN_OUTSIDE 217       /* the outside of the window's */

/* The window as the chip took it at the start of line 0: its left column, top line, width and height */
typedef struct ngp_k2ge_window
{
	uint8_t left;
	uint8_t top;
	uint8_t width;
	uint8_t height;
} ngp_k2ge_window;

/* The window's registers (002-005), as the start of line 0 takes them */
void ngp_k2ge_take_window(const uint8_t *k2ge, ngp_k2ge_window *window);

/* Line 'line' (0-151) of the picture, NGP_K2GE_WIDTH pens, from the chip's memory as it is now */
void ngp_k2ge_draw_line(const uint8_t *k2ge, const ngp_k2ge_window *window, unsigned line, uint8_t *pens);

/* The colour a pen shows with the registers and palettes as they are now: 0x0BGR, 4 bits each */
uint16_t ngp_k2ge_pen_colour(const uint8_t *k2ge, uint8_t pen);

/* A colour as 0x00RRGGBB, each 4-bit level times 17 (MAME's pal4bit) */
uint32_t ngp_k2ge_rgb(uint16_t colour);

/* ---- the registers and the timing ---- */

#define NGP_K2GE_DOTS        515       /* dots (master clocks) a line */
#define NGP_K2GE_LINES       199       /* lines a frame */
#define NGP_K2GE_HBLANK_OFF  480       /* the dot of a line where HBLANK is lowered */
#define NGP_K2GE_NO_CALL     (-1)      /* a line not called */

/* What a line's start does to the chip's lines: each call's state, or NGP_K2GE_NO_CALL */
typedef struct ngp_k2ge_calls
{
	int8_t vblank;                     /* VBLANK (INT4): 1 on lines 152-198 and 0 on line 0, when 000 bit 7 */
	int8_t hblank;                     /* HBLANK (TI0): 1 on lines 0-150 and 198, when 000 bit 6 */
	uint8_t hblank_off;                /* 1: HBLANK is called with 0 at the line's dot 480 (lines 0-150, 198) */
} ngp_k2ge_calls;

/* The chip as MAME starts it: its memory zeroed, then device_reset's writes through ngp_k2ge_write. Returns
   the VBLANK call of its write to 000 (0) */
int ngp_k2ge_reset(uint8_t *k2ge);

/* The CPU writes 'data' at 'offset' (0000-3FFF), as MAME's k2ge_device::write: 100-117 keep 3 bits, the
   palettes' high bytes (odd offsets in 200-3FF) 4, 7E2 keeps bit 7 and only while 7F0 is AA (else the write
   is lost); 030 is kept whole (k1ge_device::write keeps its bit 7, then k2ge_device::write stores the byte
   again whole). Returns the VBLANK call a write to 000 makes (bit 7 of the data ? bit 6 of 010 : 0), else
   NGP_K2GE_NO_CALL */
int ngp_k2ge_write(uint8_t *k2ge, unsigned offset, uint8_t data);

/* The CPU reads 'offset' with the beam at 'line' (0-198) and 'dot' (0-514): the memory, but 008 (RAS.H) is
   dot / 4 and 009 (RAS.V) is the line */
uint8_t ngp_k2ge_read(const uint8_t *k2ge, unsigned offset, unsigned line, unsigned dot);

/* The start of line 'line' (0-198), MAME's timer_callback but its drawing (ngp_k2ge_draw_line, which the host
   calls after it): on lines 152-198 bit 6 of 010 set, on line 0 the window taken into 'window' and bit 6 of
   010 cleared, and the calls of 'calls' */
void ngp_k2ge_line_start(uint8_t *k2ge, unsigned line, ngp_k2ge_window *window, ngp_k2ge_calls *calls);

#ifdef __cplusplus
}
#endif

#endif
