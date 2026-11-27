/**
 *    Name:        k2ge.c
 *    Description: the Neo Geo Pocket Color's video chip (K2GE) in C, a line at a time: ngp/k2ge.h
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    MAME 0.289's k1ge.cpp drawing translated to C: the chip's memory is read in place, no allocation,
 *    and a line is drawn on its own, as the console's time reaches it.
 */
#include "ngp/k2ge.h"

#include <stdbool.h>
#include <stdint.h>

/* The address of a tile from its map entry: 0x2000 plus the tile number times 16 */
static void tile_address(uint16_t data, bool *hflip, uint16_t *tile_addr)
{
	*hflip = (data & 0x8000) != 0;
	*tile_addr = (uint16_t)(0x2000 + ((data & 0x1ff) * 16));
}

/* The 2-bit colour of the next pixel in a tile, advancing the tile data */
static uint16_t tile_pixel(bool hflip, uint16_t *tile_data)
{
	uint16_t col;
	if (hflip)
	{
		col = *tile_data & 0x0003;
		*tile_data >>= 2;
	}
	else
	{
		col = *tile_data >> 14;
		*tile_data <<= 2;
	}
	return col;
}

/* The pen code of a tile from its map entry and palette base */
static uint16_t tile_pen_code(uint16_t map_data, int pal_base, bool compat)
{
	if (!compat)
		return (uint16_t)(pal_base + ((map_data & 0x1e00) >> 7));
	else
		return (uint16_t)(NGP_K2GE_PEN_MONO + pal_base + (map_data & 0x2000 ? 4 : 0));
}

/* The tile data for a scroll plane's map entry at a given offset and line */
static void get_tile_data(int offset_x, uint16_t base, int line, int scroll_y, int pal_base, bool compat,
	uint16_t *pcode, bool *hflip, uint16_t *tile_addr, uint16_t *tile_data, const uint8_t *k2ge)
{
	uint16_t const map_data = (uint16_t)(k2ge[base + offset_x] | (k2ge[base + offset_x + 1] << 8));
	*pcode = tile_pen_code(map_data, pal_base, compat);
	tile_address(map_data, hflip, tile_addr);
	if (map_data & 0x4000)
		*tile_addr += (uint16_t)((7 - ((scroll_y + line) & 0x07)) * 2);
	else
		*tile_addr += (uint16_t)(((scroll_y + line) & 0x07) * 2);
	*tile_data = (uint16_t)(k2ge[*tile_addr] | (k2ge[*tile_addr + 1] << 8));
}

/* A scroll plane's 160 pixels, writing to pens where the tile colour is non-zero */
static void draw_scroll_plane(uint8_t *pens, uint16_t base, int line, int scroll_x, int scroll_y, int pal_base,
	bool compat, const uint8_t *k2ge)
{
	int offset_x = (scroll_x >> 3) * 2;
	int px = scroll_x & 0x07;

	base = (uint16_t)(base + ((((scroll_y + line) >> 3) & 0x1f) * 0x0040));

	uint16_t pcode, tile_addr, tile_data;
	bool hflip;
	get_tile_data(offset_x, base, line, scroll_y, pal_base, compat, &pcode, &hflip, &tile_addr, &tile_data, k2ge);
	if (hflip)
		tile_data >>= 2 * (scroll_x & 0x07);
	else
		tile_data <<= 2 * (scroll_x & 0x07);

	for (int i = 0; i < NGP_K2GE_WIDTH; i++)
	{
		uint16_t const col = tile_pixel(hflip, &tile_data);
		if (col)
		{
			pens[i] = (uint8_t)(pcode + col);
		}

		px++;
		if (px >= 8)
		{
			offset_x = (offset_x + 2) & 0x3f;
			get_tile_data(offset_x, base, line, scroll_y, pal_base, compat, &pcode, &hflip, &tile_addr, &tile_data, k2ge);
			px = 0;
		}
	}
}

/* The pen code of a sprite from its data and index */
static uint16_t sprite_pen_code(uint16_t spr_data, uint8_t spr_index, bool compat, const uint8_t *k2ge)
{
	if (!compat)
		return (uint16_t)((k2ge[0x0c00 + spr_index] & 0x0f) << 2);
	else
		return (uint16_t)(NGP_K2GE_PEN_MONO + (spr_data & 0x2000 ? 4 : 0));
}

/* A sprite plane's 160 pixels, drawing sprites in reverse order of selection */
static void draw_sprite_plane(uint8_t *pens, uint16_t priority, int line, int scroll_x, int scroll_y,
	bool compat, const uint8_t *k2ge)
{
	uint16_t spr_data[64];
	uint8_t spr_x[64];
	uint8_t spr_y[64];
	uint8_t spr_index[64];
	int num_sprites = 0;
	uint8_t spr_x_acc = 0;
	uint8_t spr_y_acc = 0;

	priority <<= 11;

	for (int i = 0; i < 256; i += 4)
	{
		uint16_t const data = (uint16_t)(k2ge[0x800 + i] | (k2ge[0x801 + i] << 8));
		uint8_t const x = k2ge[0x802 + i];
		uint8_t const y = k2ge[0x803 + i];

		spr_x_acc = (data & 0x0400) ? (uint8_t)(spr_x_acc + x) : (uint8_t)(scroll_x + x);
		spr_y_acc = (data & 0x0200) ? (uint8_t)(spr_y_acc + y) : (uint8_t)(scroll_y + y);

		if ((data & 0x1800) == priority)
		{
			if ((line >= spr_y_acc || spr_y_acc > 0xf8) && line < ((spr_y_acc + 8) & 0xff))
			{
				spr_data[num_sprites] = data;
				spr_y[num_sprites] = spr_y_acc;
				spr_x[num_sprites] = spr_x_acc;
				spr_index[num_sprites] = (uint8_t)(i >> 2);
				num_sprites++;
			}
		}
	}

	for (int i = num_sprites - 1; i >= 0; i--)
	{
		uint16_t const pcode = sprite_pen_code(spr_data[i], spr_index[i], compat, k2ge);

		bool hflip;
		uint16_t tile_addr;
		tile_address(spr_data[i], &hflip, &tile_addr);
		if (spr_data[i] & 0x4000)
			tile_addr += (uint16_t)((7 - ((line - spr_y[i]) & 0x07)) * 2);
		else
			tile_addr += (uint16_t)(((line - spr_y[i]) & 0x07) * 2);
		uint16_t tile_data = (uint16_t)(k2ge[tile_addr] | (k2ge[tile_addr + 1] << 8));

		for (int j = 0; j < 8; j++)
		{
			uint8_t const px = (uint8_t)(spr_x[i] + j);   /* 8 bits, as MAME's: past 255 it wraps */

			uint16_t const col = tile_pixel(hflip, &tile_data);
			if (px < NGP_K2GE_WIDTH && col)
			{
				pens[px] = (uint8_t)(pcode + col);
			}
		}
	}
}

void ngp_k2ge_take_window(const uint8_t *k2ge, ngp_k2ge_window *window)
{
	window->left = k2ge[0x002];
	window->top = k2ge[0x003];
	window->width = k2ge[0x004];
	window->height = k2ge[0x005];
}

void ngp_k2ge_draw_line(const uint8_t *k2ge, const ngp_k2ge_window *window, unsigned line, uint8_t *pens)
{
	bool const compat = (k2ge[0x7e2] & 0x80) != 0;

	if ((int)line < window->top || (int)line >= window->top + window->height)
	{
		for (int i = 0; i < NGP_K2GE_WIDTH; i++)
		{
			pens[i] = NGP_K2GE_PEN_OUTSIDE;
		}
	}
	else
	{
		for (int i = 0; i < NGP_K2GE_WIDTH; i++)
			pens[i] = NGP_K2GE_PEN_BG;

		if (k2ge[0x030] & 0x80)
		{
			draw_sprite_plane(pens, 1, (int)line, k2ge[0x020], k2ge[0x021], compat, k2ge);
			draw_scroll_plane(pens, 0x1000, (int)line, k2ge[0x032], k2ge[0x033], compat ? 0x08 : 0x40, compat, k2ge);
			draw_sprite_plane(pens, 2, (int)line, k2ge[0x020], k2ge[0x021], compat, k2ge);
			draw_scroll_plane(pens, 0x1800, (int)line, k2ge[0x034], k2ge[0x035], compat ? 0x10 : 0x80, compat, k2ge);
			draw_sprite_plane(pens, 3, (int)line, k2ge[0x020], k2ge[0x021], compat, k2ge);
		}
		else
		{
			draw_sprite_plane(pens, 1, (int)line, k2ge[0x020], k2ge[0x021], compat, k2ge);
			draw_scroll_plane(pens, 0x1800, (int)line, k2ge[0x034], k2ge[0x035], compat ? 0x10 : 0x80, compat, k2ge);
			draw_sprite_plane(pens, 2, (int)line, k2ge[0x020], k2ge[0x021], compat, k2ge);
			draw_scroll_plane(pens, 0x1000, (int)line, k2ge[0x032], k2ge[0x033], compat ? 0x08 : 0x40, compat, k2ge);
			draw_sprite_plane(pens, 3, (int)line, k2ge[0x020], k2ge[0x021], compat, k2ge);
		}

		for (int i = 0; i < window->left; i++)
		{
			pens[i] = NGP_K2GE_PEN_OUTSIDE;
		}

		for (int i = window->left + window->width; i < NGP_K2GE_WIDTH; i++)
		{
			pens[i] = NGP_K2GE_PEN_OUTSIDE;
		}
	}
}

uint16_t ngp_k2ge_pen_colour(const uint8_t *k2ge, uint8_t pen)
{
	uint8_t entry;
	if (pen < NGP_K2GE_PEN_MONO)
	{
		entry = pen;
	}
	else if (pen < NGP_K2GE_PEN_BG)
	{
		unsigned const o = 0x100u + (unsigned)(pen - NGP_K2GE_PEN_MONO);
		entry = (uint8_t)(NGP_K2GE_PEN_MONO + ((o & 0x1c) << 1) + (k2ge[o] & 7));
	}
	else if (pen == NGP_K2GE_PEN_BG)
	{
		entry = (uint8_t)(0xf0 + (((k2ge[0x118] & 0xc0) == 0x80) ? (k2ge[0x118] & 7) : 0));
	}
	else
	{
		entry = (uint8_t)(0xf8 + (k2ge[0x012] & 7));
	}
	return (uint16_t)((k2ge[0x200 + 2 * entry] | (k2ge[0x201 + 2 * entry] << 8)) & 0x0fff);
}

uint32_t ngp_k2ge_rgb(uint16_t colour)
{
	uint32_t const r = (colour & 0x000f) * 17;
	uint32_t const g = ((colour >> 4) & 0x000f) * 17;
	uint32_t const b = ((colour >> 8) & 0x000f) * 17;
	return (r << 16) | (g << 8) | b;
}
