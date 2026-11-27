/**
 *    Name:        k2ge.h
 *    Description: the Neo Geo Pocket Color's video chip (K2GE): a line of the picture from the chip's memory
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

#ifdef __cplusplus
}
#endif

#endif
