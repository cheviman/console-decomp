/**
 *    Name:        test_k2ge.cpp
 *    Description: the check of the runtime's K2GE (ngp/runtime/src/k2ge.c) against MAME's, from dumps of the
 *                 chip's memory
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Each dump (the chip's 16 KB, as ngpref --dump writes FRAME_k2ge.bin) is written to MAME's K2GE through its
 *    write handler, as a program writes it, and the chip runs two frames: the second one's picture is drawn
 *    from that memory, as every line of the runtime's is. Every dump is tried as it is, with the scroll
 *    planes' order swapped (030 bit 7), in the K1GE-compatible mode (7F0 = AA, then 7E2 bit 7), with a window
 *    smaller than the screen and with the sprites moved right by 100 columns (past column 255 they wrap to the
 *    left edge: MAME's x is 8 bits). The pens of every line and the colour of every pixel must be the same.
 *
 *    usage: test_k2ge DUMP...      prints what differs and "score: -N" (N the lines that differ); exits 0 only
 *                                  when nothing does
 */
#include "k2gevideo.h"
#include "ngp/k2ge.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

constexpr uint64_t kFrame = 515 * 199;     // MAME's ngp.cpp: the screen's 515 dots by 199 lines

uint64_t g_present = 0;

uint64_t now()
{
	return g_present;
}

void pin(int)
{
}

// Runs MAME's K2GE on 'memory' and the runtime's on what MAME's chip then holds; the lines that differ
int compare(const std::vector<uint8_t> &memory, const std::string &name)
{
	// a chip of its own, started at time 0 (its first events are set from the present when it starts)
	g_present = 0;
	nv_attach(now, pin, pin);
	for (uint32_t offset = 0; offset < NGP_K2GE_SIZE; offset++)
		nv_write(offset, memory[offset]);
	// 7E2 takes a write only while 7F0 holds AA
	nv_write(0x7f0, 0xaa);
	nv_write(0x7e2, memory[0x7e2]);
	nv_write(0x7f0, memory[0x7f0]);
	for (uint64_t time = nv_next_event(); time <= 2 * kFrame; time = nv_next_event())
		nv_fire(time, &g_present);

	const uint8_t *chip = nv_memory();
	for (uint32_t offset = 0; offset < NGP_K2GE_SIZE; offset++)
		if (offset != 0x010 && chip[offset] != memory[offset])
		{
			std::printf("%s: MAME's chip holds %02X at %03X, the dump %02X (a write it masks)\n", name.c_str(),
				chip[offset], offset, memory[offset]);
			break;
		}

	std::vector<uint16_t> pens(NV_WIDTH * NV_HEIGHT);
	std::vector<uint8_t> rgb(NV_WIDTH * NV_HEIGHT * 3);
	nv_pens(pens.data());
	nv_picture(rgb.data());

	ngp_k2ge_window window;
	ngp_k2ge_take_window(chip, &window);
	int differ = 0;
	for (unsigned line = 0; line < NGP_K2GE_HEIGHT; line++)
	{
		uint8_t drawn[NGP_K2GE_WIDTH];
		ngp_k2ge_draw_line(chip, &window, line, drawn);
		bool same = true;
		for (unsigned x = 0; x < NGP_K2GE_WIDTH && same; x++)
		{
			const uint16_t pen = pens[line * NV_WIDTH + x];
			const uint8_t *pixel = &rgb[(line * NV_WIDTH + x) * 3];
			const uint32_t colour = ngp_k2ge_rgb(ngp_k2ge_pen_colour(chip, drawn[x]));
			const uint32_t expected = uint32_t(pixel[0]) << 16 | uint32_t(pixel[1]) << 8 | pixel[2];
			if (drawn[x] != pen)
			{
				std::printf("%s: line %u, x %u: MAME's pen %u, the runtime's %u\n", name.c_str(), line, x, pen,
					drawn[x]);
				same = false;
			}
			else if (colour != expected)
			{
				std::printf("%s: line %u, x %u, pen %u: MAME's colour %06X, the runtime's %06X\n", name.c_str(),
					line, x, pen, expected, colour);
				same = false;
			}
		}
		differ += same ? 0 : 1;
	}
	return differ;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: test_k2ge DUMP...\n");
		return 2;
	}
	int differ = 0, tried = 0;
	for (int index = 1; index < argc; index++)
	{
		std::ifstream in(argv[index], std::ios::binary);
		std::vector<uint8_t> memory((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (memory.size() != NGP_K2GE_SIZE)
		{
			std::fprintf(stderr, "%s: not a dump of the chip's 16 KB\n", argv[index]);
			return 2;
		}
		const std::string name = argv[index];

		differ += compare(memory, name);

		std::vector<uint8_t> swapped = memory;
		swapped[0x030] ^= 0x80;
		differ += compare(swapped, name + " (planes swapped)");

		std::vector<uint8_t> compatible = memory;
		compatible[0x7f0] = 0xaa;
		compatible[0x7e2] = 0x80;
		differ += compare(compatible, name + " (K1GE mode)");

		std::vector<uint8_t> window = memory;
		window[0x002] = 12;
		window[0x003] = 20;
		window[0x004] = 130;
		window[0x005] = 100;
		window[0x012] = 0x05;
		differ += compare(window, name + " (window)");

		// the sprites moved right by 100 columns: those past column 255 wrap to the left edge (an 8-bit x)
		std::vector<uint8_t> moved = memory;
		for (uint32_t sprite = 0; sprite < 64; sprite++)
			moved[0x802 + sprite * 4] = uint8_t(moved[0x802 + sprite * 4] + 100);
		differ += compare(moved, name + " (sprites moved)");
		tried += 5;
	}
	std::printf("%d pictures, %d lines that differ\nscore: %d\n", tried, differ, -differ);
	return differ ? 1 : 0;
}
