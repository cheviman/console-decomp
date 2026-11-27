/**
 *    Name:        emu.h
 *    Description: minimal stand-in for MAME's emu.h, enough to build MAME's K1GE/K2GE (k1ge.cpp) alone
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    26-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    k2gevideo.cpp includes it, and k1ge.cpp after it, inside a namespace of their own (nv), as arcade-decomp's
 *    konami/reference/sound/ksound.cpp does with its chips. With MAME's meaning: time is the console's master
 *    clock (6.144 MHz, a dot of the screen), counted from the start of frame 0; the screen is MAME's ngp.cpp's
 *    (set_raw: 515 dots a line, 199 lines, 160 x 152 seen), and where its beam is comes from the present the
 *    host gives (screen_device::now); a timer is a time it expires at, which the host's loop fires
 *    (fire_timers); the palette is MAME's indirect one (pens that point to colours); no-ops for saving state.
 */
#pragma once

#define ATTR_COLD
#define NAME(x) x, #x
#define FUNC(x) &x, #x
#define DECLARE_DEVICE_TYPE(Type, Class) extern const device_type Type;
#define DEFINE_DEVICE_TYPE(Type, Class, ShortName, FullName) const device_type Type = ShortName;
#define TIMER_CALLBACK_MEMBER(name) void name(s32 param)

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using offs_t = uint32_t;

// BIT(x, n) from MAME's coretmpl.h
template <typename T, typename U>
constexpr T BIT(T x, U n) noexcept
{
	return (x >> n) & T(1);
}

// pal3bit, pal4bit from MAME's palette.h: an n-bit level spread over 8 bits
constexpr u8 pal3bit(u8 bits) { bits &= 7; return u8((bits << 5) | (bits << 2) | (bits >> 1)); }
constexpr u8 pal4bit(u8 bits) { bits &= 0xf; return u8((bits << 4) | bits); }

template <typename T>
std::unique_ptr<T> make_unique_clear(size_t count)
{
	using element = std::remove_extent_t<T>;
	std::unique_ptr<T> made(new element[count]);
	std::memset(made.get(), 0, count * sizeof(element));
	return made;
}

// The screen's timing (MAME's ngp.cpp, set_raw(6.144_MHz_XTAL, 515, 0, 160, 199, 0, 152))
constexpr u64 kDotsPerLine = 515;
constexpr u64 kLinesPerFrame = 199;
constexpr u64 kDotsPerFrame = kDotsPerLine * kLinesPerFrame;
constexpr int kVisibleWidth = 160;
constexpr int kVisibleHeight = 152;

// A span of time, in master clocks
struct attotime
{
	u64 ticks = 0;
	static attotime never() { return attotime{ UINT64_MAX }; }
};

class machine_config {};
class device_t;
using device_type = const char *;

struct rgb_t
{
	u8 r = 0, g = 0, b = 0;
	rgb_t() = default;
	rgb_t(u8 red, u8 green, u8 blue) : r(red), g(green), b(blue) {}
	static rgb_t black() { return rgb_t(0, 0, 0); }
};

struct rectangle
{
	int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
};

class bitmap_ind16
{
public:
	void allocate(int width, int height)
	{
		m_width = width;
		m_height = height;
		m_pixels.assign(size_t(width) * size_t(height), 0);
	}
	u16 &pix(int y, int x = 0) { return m_pixels[size_t(y) * size_t(m_width) + size_t(x)]; }
	const u16 &pix(int y, int x = 0) const { return m_pixels[size_t(y) * size_t(m_width) + size_t(x)]; }
	int width() const { return m_width; }
	int height() const { return m_height; }

private:
	int m_width = 0;
	int m_height = 0;
	std::vector<u16> m_pixels;
};

inline void copybitmap(bitmap_ind16 &dest, const bitmap_ind16 &source, int flipx, int flipy, s32 destx, s32 desty,
		const rectangle &cliprect)
{
	(void)flipx; (void)flipy;
	for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
		for (int x = cliprect.min_x; x <= cliprect.max_x; x++)
			dest.pix(y + desty, x + destx) = source.pix(y, x);
}

// A timer: the time it expires at, and what it calls then. The host's loop fires it (fire_timers)
class emu_timer
{
public:
	void adjust(attotime delay, s32 param = 0);
	bool enabled() const { return m_expire != UINT64_MAX; }
	u64 expire() const { return m_expire; }

	std::function<void(s32)> m_callback;
	u64 m_expire = UINT64_MAX;
	s32 m_param = 0;
};

// The present, in master clocks: the host's (the time the CPU that is running has reached)
inline u64 (*g_now)() = nullptr;
inline std::vector<std::unique_ptr<emu_timer>> g_timers;

inline void emu_timer::adjust(attotime delay, s32 param)
{
	m_expire = delay.ticks == UINT64_MAX ? UINT64_MAX : g_now() + delay.ticks;
	m_param = param;
}

// The earliest time a timer expires at (UINT64_MAX: none is set)
inline u64 next_timer()
{
	u64 next = UINT64_MAX;
	for (const auto &timer : g_timers)
		next = std::min(next, timer->m_expire);
	return next;
}

// Fires, in the order of their times, the timers that expire at or before 'time'; the host has brought the
// present up to each one's time first (g_now gives it while its callback runs)
inline void fire_timers(u64 time, u64 &firing_time)
{
	for (;;)
	{
		emu_timer *earliest = nullptr;
		for (const auto &timer : g_timers)
			if (timer->m_expire <= time && (!earliest || timer->m_expire < earliest->m_expire))
				earliest = timer.get();
		if (!earliest)
			return;
		firing_time = earliest->m_expire;
		earliest->m_expire = UINT64_MAX;
		earliest->m_callback(earliest->m_param);
	}
}

class screen_device
{
public:
	// The beam: the line and the dot the present is at
	int vpos() const { return int((g_now() / kDotsPerLine) % kLinesPerFrame); }
	int hpos() const { return int(g_now() % kDotsPerLine); }

	// The time until the beam is next at (vpos, hpos): a whole frame when it is there now, as MAME's
	// screen_device::time_until_pos gives
	attotime time_until_pos(int vpos, int hpos = 0) const
	{
		const u64 now = g_now() % kDotsPerFrame;
		const u64 target = u64(vpos) * kDotsPerLine + u64(hpos);
		u64 delta = (target + kDotsPerFrame - now) % kDotsPerFrame;
		if (delta == 0)
			delta = kDotsPerFrame;
		return attotime{ delta };
	}

	void register_screen_bitmap(bitmap_ind16 &bitmap) { bitmap.allocate(int(kDotsPerLine), int(kLinesPerFrame)); }
};

inline screen_device g_screen;

class devcb_write_line
{
public:
	struct binder
	{
		devcb_write_line *target;
		void set(std::function<void(int)> function) { target->m_function = std::move(function); }
	};

	explicit devcb_write_line(device_t &owner) { (void)owner; }
	binder bind() { return binder{ this }; }
	bool isunset() const { return !m_function; }
	void operator()(int state) { if (m_function) m_function(state); }

private:
	std::function<void(int)> m_function;
};

class device_t
{
public:
	device_t(const machine_config &config, device_type type, const char *tag, device_t *owner, u32 clock)
		: m_clock(clock)
	{
		(void)config; (void)type; (void)tag; (void)owner;
	}
	virtual ~device_t() = default;

	u32 clock() const { return m_clock; }

	// Host API
	void start() { device_start(); }
	void reset() { device_reset(); }

protected:
	virtual void device_start() {}
	virtual void device_reset() {}

	template <typename C>
	emu_timer *timer_alloc(void (C::*callback)(s32), const char *name, C *object)
	{
		(void)name;
		g_timers.push_back(std::make_unique<emu_timer>());
		g_timers.back()->m_callback = [callback, object](s32 param) { (object->*callback)(param); };
		return g_timers.back().get();
	}

	template <typename T> void save_item(T &item, const char *name) { (void)item; (void)name; }
	template <typename T> void save_pointer(T &item, const char *name, size_t count) { (void)item; (void)name; (void)count; }

private:
	u32 m_clock;
};

class device_video_interface
{
public:
	device_video_interface(const machine_config &config, device_t &device) { (void)config; (void)device; }
	template <typename T> void set_screen(T &&tag) { (void)tag; }
	screen_device &screen() const { return g_screen; }
};

// MAME's indirect palette: a pen points to one of the indirect colours
class device_palette_interface
{
public:
	device_palette_interface(const machine_config &config, device_t &device) { (void)config; (void)device; }
	virtual ~device_palette_interface() = default;

	void set_indirect_color(int index, rgb_t color)
	{
		size_indirect();
		m_indirect[size_t(index)] = color;
	}
	void set_pen_indirect(u32 pen, u16 indirect)
	{
		size_indirect();
		m_pens[pen] = indirect;
	}

	// Host API: the colour a pen shows now
	rgb_t pen_color(u32 pen) const { return m_indirect[m_pens[pen]]; }
	u32 entries() const { return palette_entries(); }

protected:
	virtual u32 palette_entries() const noexcept = 0;
	virtual u32 palette_indirect_entries() const noexcept = 0;

private:
	void size_indirect()
	{
		if (m_pens.empty())
		{
			m_pens.assign(palette_entries(), 0);
			m_indirect.assign(palette_indirect_entries(), rgb_t::black());
		}
	}

	std::vector<u16> m_pens;
	std::vector<rgb_t> m_indirect;
};
