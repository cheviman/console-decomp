/**
 *    Name:        mdsound.cpp
 *    Description: the reference's sound side of the Mega Drive: MAME's Z80, ymfm's YM2612 and MAME's Sega
 *                 PSG (SN76489), on the board's time (mdsound.h)
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Built as arcade-decomp's konami/reference/sound/ksound.cpp is (the shared core, tools/core): MAME's
 *    files are included inside a namespace of their own (ms) after that folder's stand-in for MAME's emu.h,
 *    the Z80 core is system32/reference/mame/z80's (MAME 0.289's), the PSG is this folder's copy of MAME
 *    0.289's sn76496.cpp (BSD-3-Clause, Nicola Salmoria), the YM2612 is ymfm's (system32/reference/ymfm,
 *    BSD-3-Clause, Aaron Giles). As MAME's megadriv.cpp wires them: the Z80's memory is the board's
 *    (md_z80_read and md_z80_write), its INT line is the VDP's, its reset line also resets the YM2612.
 *
 *    The Z80 runs in slices that end where the board's time asks, or at the YM2612's timers, so that a timer
 *    runs out when its time comes; the PSG and the YM2612 are brought up to the present before each write.
 */
#include "mdsound.h"

#include "md/machine.h"

#include "ymfm_opn.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ms
{
#include "emu.h"
#include "logmacro.h"
#include "z80.cpp"
#include "z80dasm.cpp"
#include "sn76496.cpp"
}

namespace
{

constexpr uint64_t kZ80Period = MD_Z80_DIVIDER;             // master clocks a Z80 cycle
constexpr uint64_t kFmClockPeriod = MD_CPU_DIVIDER;         // master clocks a YM2612 clock
constexpr uint64_t kFmPeriod = MD_CPU_DIVIDER * 144;        // master clocks a YM2612 sample (6 x 24 clocks)
constexpr uint64_t kPsgPeriod = MD_Z80_DIVIDER * 2;         // master clocks a PSG sample (MAME's clock / 2)

// how many samples of a chip that makes one every 'period' start before board time 'time'
uint64_t samples_before(uint64_t time, uint64_t period)
{
	return (time + period - 1) / period;
}

// ---- time: the board's, and while the Z80 runs, where its slice started and how long it is ----

uint64_t g_time = 0;                        // master clock the sound side has reached
bool g_running = false;
uint64_t g_slice_start = 0;
int g_slice_budget = 0;                     // Z80 cycles
uint64_t g_now_override = UINT64_MAX;       // while a timer fires, its own time
uint64_t g_z80_cycles = 0;

class sound_cpu : public ms::z80_device
{
public:
	explicit sound_cpu(const ms::machine_config &config) : ms::z80_device(config, "audiocpu", nullptr, MD_Z80_CLOCK) {}

	// how many of the slice's cycles have gone by
	int cycles_done() const { return g_slice_budget - cycles_left(); }

	// end the running slice early, with no more than 'left' cycles to go: how many it took off
	int cut_slice(int left)
	{
		const int taken = cycles_left() - left;
		if (taken <= 0)
			return 0;
		m_icount -= taken;
		m_cut += taken;
		return taken;
	}
	// the Z80 loses 'cycles' within its slice (an access to the 68000's bus)
	void lose(int cycles) { m_icount -= cycles; }
	int run_slice(int cycles)
	{
		m_cut = 0;
		const int done = run(cycles);
		return done - m_cut;
	}

private:
	int m_cut = 0;
};

ms::machine_config g_config;
std::unique_ptr<sound_cpu> g_z80;
uint8_t g_executed[0x10000 / 8];

uint64_t now()
{
	if (g_now_override != UINT64_MAX)
		return g_now_override;
	return g_running ? g_slice_start + uint64_t(g_z80->cycles_done()) * kZ80Period : g_time;
}

// ---- a chip's samples: 'outputs' channels interleaved, from sample number 'first' on ----

struct samples
{
	int outputs = 1;
	std::vector<float> data;
	uint64_t first = 0;
	uint64_t made = 0;                // samples made so far

	explicit samples(int count) : outputs(count) {}
	void push(const float *values)
	{
		data.insert(data.end(), values, values + outputs);
		made++;
	}
};

samples g_fm_samples(2);
samples g_psg_samples(1);

// ---- the YM2612 (its IRQ goes nowhere on this board) ----

class fm_chip : public ymfm::ymfm_interface
{
public:
	fm_chip() : m_chip(*this) { m_chip.reset(); }

	// every sample that starts before board time 'time' (x / 32768, as ksound takes ymfm's)
	void catch_up(uint64_t time)
	{
		const uint64_t due = samples_before(time, kFmPeriod);
		ymfm::ym2612::output_data output;
		while (g_fm_samples.made < due)
		{
			m_chip.generate(&output);
			const float values[2] = { float(output.data[0]) * (1.0f / 32768.0f), float(output.data[1]) * (1.0f / 32768.0f) };
			g_fm_samples.push(values);
		}
	}
	uint8_t read(unsigned offset)
	{
		catch_up(now());
		return m_chip.read(offset);
	}
	void write(unsigned offset, uint8_t data)
	{
		catch_up(now());
		m_chip.write(offset, data);
	}
	void reset()
	{
		catch_up(now());
		m_chip.reset();
		m_deadline[0] = m_deadline[1] = UINT64_MAX;
	}
	uint64_t next_timer() const { return std::min(m_deadline[0], m_deadline[1]); }
	// the timers due by board time 'time', in the order they run out
	void fire(uint64_t time)
	{
		while (next_timer() <= time)
		{
			const int number = m_deadline[0] <= m_deadline[1] ? 0 : 1;
			const uint64_t when = m_deadline[number];
			m_deadline[number] = UINT64_MAX;
			catch_up(when);
			g_now_override = when;
			m_engine->engine_timer_expired(uint32_t(number));
			g_now_override = UINT64_MAX;
		}
	}

	// ymfm_interface
	void ymfm_set_timer(uint32_t number, int32_t clocks) override
	{
		if (clocks < 0)
		{
			m_deadline[number] = UINT64_MAX;
			return;
		}
		m_deadline[number] = now() + uint64_t(clocks) * kFmClockPeriod;
		// a running Z80 stops by then, for the timer to run out on time
		if (g_running && g_now_override == UINT64_MAX
			&& m_deadline[number] < g_slice_start + uint64_t(g_slice_budget) * kZ80Period)
		{
			const uint64_t at = g_slice_start + uint64_t(g_z80->cycles_done()) * kZ80Period;
			const int left = m_deadline[number] > at ? int((m_deadline[number] - at + kZ80Period - 1) / kZ80Period) : 0;
			g_slice_budget -= g_z80->cut_slice(left);
		}
	}
	void ymfm_set_busy_end(uint32_t clocks) override
	{
		m_busy_until = now() + uint64_t(clocks) * kFmClockPeriod;
	}
	bool ymfm_is_busy() override
	{
		return now() < m_busy_until;
	}

private:
	ymfm::ym2612 m_chip;
	uint64_t m_deadline[2] = { UINT64_MAX, UINT64_MAX };
	uint64_t m_busy_until = 0;
};

std::unique_ptr<fm_chip> g_fm;

// ---- the PSG: MAME's, its stream pulled up to the present before each write ----

class psg_chip : public ms::segapsg_device
{
public:
	explicit psg_chip(const ms::machine_config &config) : ms::segapsg_device(config, "psg", nullptr, MD_Z80_CLOCK)
	{
		stream().m_before_change = [this]() { catch_up(now()); };
	}
	void catch_up(uint64_t time)
	{
		const uint64_t due = samples_before(time, kPsgPeriod);
		if (due <= g_psg_samples.made)
			return;
		const int count = int(due - g_psg_samples.made);
		generate(count);
		for (int index = 0; index < count; index++)
		{
			const float value = stream().m_outputs[0][size_t(index)];
			g_psg_samples.push(&value);
		}
	}
};

std::unique_ptr<psg_chip> g_psg;

uint16_t g_z80_pc = 0;

void on_instruction(ms::offs_t address)
{
	const uint16_t pc = uint16_t(address);
	g_z80_pc = pc;
	g_executed[pc >> 3] |= uint8_t(1u << (pc & 7));
}

// the Z80 (while the board lets it run) and the YM2612's timers up to board time 'until'
void run_to(uint64_t until)
{
	while (g_time < until)
	{
		g_fm->fire(g_time);
		const uint64_t end = std::min(until, std::max(g_fm->next_timer(), g_time + 1));
		if (!md_z80_running())
		{
			g_time = end;
			continue;
		}
		g_slice_start = g_time;
		g_slice_budget = int(std::max<uint64_t>((end - g_time + kZ80Period - 1) / kZ80Period, 1));
		g_running = true;
		const int done = g_z80->run_slice(g_slice_budget);
		g_running = false;
		g_time += uint64_t(std::max(done, 0)) * kZ80Period;
		g_z80_cycles += uint64_t(std::max(done, 0));
	}
	g_fm->fire(g_time);
}

// ---- recording ----

uint64_t g_frame_samples_hash = 0;
std::vector<int16_t> g_mix;

uint64_t hash_bytes(const void *data, size_t size, uint64_t hash)
{
	const uint8_t *bytes = static_cast<const uint8_t *>(data);
	for (size_t index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 0x100000001b3ull;
	return hash;
}

// the chip's samples that start before 'due' out of it, into the hash
uint64_t take(samples &source, uint64_t due, uint64_t hash, std::vector<float> &out)
{
	const size_t count = size_t(std::min<uint64_t>(due - source.first, source.data.size() / size_t(source.outputs))) * size_t(source.outputs);
	out.assign(source.data.begin(), source.data.begin() + ptrdiff_t(count));
	source.data.erase(source.data.begin(), source.data.begin() + ptrdiff_t(count));
	source.first += count / size_t(source.outputs);
	return hash_bytes(out.data(), out.size() * sizeof(float), hash);
}

} // namespace

extern "C" void ms_attach(uint64_t time)
{
	g_z80 = std::make_unique<sound_cpu>(g_config);
	// an access to the 68000's bus costs the Z80 the arbitration's cycles (md/machine.h)
	g_z80->space(ms::AS_PROGRAM).read = [](ms::offs_t address) {
		const uint8_t value = md_z80_read(uint16_t(address));
		g_z80->lose(int(std::exchange(md_board.z80_bus_wait, 0u)));
		return value;
	};
	g_z80->space(ms::AS_PROGRAM).write = [](ms::offs_t address, uint8_t data) {
		md_z80_write(uint16_t(address), data);
		g_z80->lose(int(std::exchange(md_board.z80_bus_wait, 0u)));
	};
	g_z80->space(ms::AS_IO).read = [](ms::offs_t) { return uint8_t(0xff); };
	g_z80->space(ms::AS_IO).write = [](ms::offs_t, uint8_t) {};
	g_z80->irq_vector = []() { return 0xff; };    // (interrupt mode 1 reads no vector; the line is the VDP's)
	g_z80->instruction_hook = on_instruction;
	g_z80->start();
	g_z80->reset();

	g_fm = std::make_unique<fm_chip>();
	g_psg = std::make_unique<psg_chip>(g_config);
	g_psg->start();
	g_psg->reset();

	md_board.hooks.fm_read = [](unsigned port) { return g_fm->read(port); };
	md_board.hooks.fm_write = [](unsigned port, uint8_t data) { g_fm->write(port, data); };
	md_board.hooks.psg_write = [](uint8_t data) { g_psg->write(data); };
	md_board.hooks.z80_reset = [](int held) {
		if (held)
		{
			g_fm->reset();
			g_z80->reset();
		}
	};
	md_board.hooks.z80_irq = [](int raised) {
		g_z80->set_input_line(ms::INPUT_LINE_IRQ0, raised ? ms::ASSERT_LINE : ms::CLEAR_LINE);
	};
	g_time = time;
	g_fm_samples.first = g_fm_samples.made = samples_before(time, kFmPeriod);
	g_psg_samples.first = g_psg_samples.made = samples_before(time, kPsgPeriod);
}

extern "C" void ms_run_until(uint64_t time)
{
	run_to(time);
}

extern "C" void ms_end_frame(uint64_t time)
{
	run_to(time);
	g_fm->catch_up(time);
	g_psg->catch_up(time);

	const uint64_t fm_first = g_fm_samples.first, psg_first = g_psg_samples.first;
	std::vector<float> fm, psg;
	uint64_t hash = 0xcbf29ce484222325ull;
	hash = take(g_fm_samples, samples_before(time, kFmPeriod), hash, fm);
	g_frame_samples_hash = take(g_psg_samples, samples_before(time, kPsgPeriod), hash, psg);

	// the mix at the YM2612's rate: each PSG sample that starts within a YM2612 sample's period, averaged
	g_mix.clear();
	for (size_t index = 0; index < fm.size() / 2; index++)
	{
		const uint64_t start = (fm_first + index) * kFmPeriod, end = start + kFmPeriod;
		const uint64_t from = std::max(samples_before(start, kPsgPeriod), psg_first);
		const uint64_t to = std::min(samples_before(end, kPsgPeriod), psg_first + psg.size());
		float level = 0.0f;
		if (to > from)
		{
			for (uint64_t sample = from; sample < to; sample++)
				level += psg[size_t(sample - psg_first)];
			level /= float(to - from);
		}
		for (int side = 0; side < 2; side++)
		{
			const float mixed = 0.50f * fm[index * 2 + size_t(side)] + 0.50f * level;
			g_mix.push_back(int16_t(std::clamp(int(mixed * 32768.0f), -32768, 32767)));
		}
	}
}

extern "C" uint64_t ms_frame_samples_hash(void)
{
	return g_frame_samples_hash;
}

extern "C" uint64_t ms_state_hash(void)
{
	return hash_bytes(md_board.z80_ram, sizeof(md_board.z80_ram), 0xcbf29ce484222325ull);
}

extern "C" const int16_t *ms_frame_mix(size_t *count)
{
	*count = g_mix.size();
	return g_mix.data();
}

extern "C" uint32_t ms_mix_rate(void)
{
	return uint32_t(MD_MASTER_CLOCK / kFmPeriod);
}

extern "C" uint64_t ms_z80_cycles(void)
{
	return g_z80_cycles;
}

extern "C" const uint8_t *ms_executed(void)
{
	return g_executed;
}

extern "C" uint16_t ms_z80_pc(void)
{
	return g_z80_pc;
}
