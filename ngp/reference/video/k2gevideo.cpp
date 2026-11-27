/**
 *    Name:        k2gevideo.cpp
 *    Description: the reference's video side of the Neo Geo Pocket Color: MAME's K2GE on the console's time
 *                 (k2gevideo.h)
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    26-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    MAME 0.289's k1ge.cpp (../mame, BSD-3-Clause, Wilbert Pol), unchanged, included inside a namespace of its
 *    own (nv) after the stand-in for MAME's emu.h in stub/, as arcade-decomp's konami/reference/sound/ksound.cpp
 *    does with its chips. As MAME's ngp.cpp wires it (ngpc: K2GE at 6.144 MHz on the screen it gives): its
 *    VBLANK pin is the CPU's INT4 and its HBLANK pin the CPU's TI0, which the host takes (nv_attach).
 */
#include "k2gevideo.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace nv
{
#include "emu.h"
#include "k1ge.cpp"
}

namespace
{

// The K2GE, with what the host reads of it
class video : public nv::k2ge_device
{
public:
	explicit video(const nv::machine_config &config) : nv::k2ge_device(config, "k1ge", nullptr, 6144000) {}

	const nv::u8 *memory() const { return m_vram.get(); }
	const nv::bitmap_ind16 &bitmap() const { return m_bitmap; }
};

nv::machine_config g_config;
std::unique_ptr<video> g_video;

} // namespace

void nv_attach(uint64_t (*now)(void), void (*vblank)(int state), void (*hblank)(int state))
{
	nv::g_now = now;
	nv::g_timers.clear();
	g_video = std::make_unique<video>(g_config);
	g_video->vblank_callback().set(vblank);
	g_video->hblank_callback().set(hblank);
	g_video->start();
	g_video->reset();
}

uint8_t nv_read(uint32_t offset)
{
	return g_video->read(offset & 0x3fff);
}

void nv_write(uint32_t offset, uint8_t data)
{
	g_video->write(offset & 0x3fff, data);
}

uint64_t nv_next_event(void)
{
	return nv::next_timer();
}

void nv_fire(uint64_t time, uint64_t *present)
{
	nv::fire_timers(time, *present);
}

void nv_picture(uint8_t *rgb)
{
	const nv::bitmap_ind16 &bitmap = g_video->bitmap();
	for (int y = 0; y < NV_HEIGHT; y++)
		for (int x = 0; x < NV_WIDTH; x++)
		{
			const nv::rgb_t color = g_video->pen_color(bitmap.pix(y, x));
			uint8_t *pixel = rgb + (size_t(y) * NV_WIDTH + size_t(x)) * 3;
			pixel[0] = color.r;
			pixel[1] = color.g;
			pixel[2] = color.b;
		}
}

const uint8_t *nv_memory(void)
{
	return g_video->memory();
}
