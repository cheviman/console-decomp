/**
 *    Name:        ngpsound.cpp
 *    Description: the reference's sound side of the Neo Geo Pocket Color: MAME's Z80, MAME's T6W28 and the two
 *                 DACs, on the console's time (ngpsound.h)
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Built as arcade-decomp's konami/reference/sound/ksound.cpp is (the shared core, tools/core): MAME's
 *    files are included inside a namespace of their own (ns) after that folder's stand-in for MAME's emu.h,
 *    the Z80 core is system32/reference/mame/z80's (MAME 0.289's), the T6W28 and the DACs are ngp/reference/
 *    mame's copies of MAME 0.289's t6w28.cpp (BSD-3-Clause, Wilbert Pol) and dac.cpp (BSD-3-Clause, smf). As
 *    MAME's ngp.cpp wires them: the Z80's memory is the console's (shared RAM, the T6W28, the comm byte, the
 *    INT5 trigger), its INT line is the timer's, its NMI line is the console's; the Z80 runs at half the
 *    master clock, and the T6W28 at half the master clock too, making a sample every 16 of its clocks. The two
 *    DACs are DAC_8BIT_R2R, with nothing routed into them: each gives -1 + byte / 128 until its next write.
 *    The speaker sums 0.50 of each T6W28 output and 0.25 of its side's DAC (output 0 and the DAC at I/O 23 on
 *    the left); the sum is exact in MAME's floats, and the mix written out (ns_frame_mix) is it taken down to
 *    a whole number with 1.0 as 32768, at the T6W28's rate, each sample with the DACs' levels at its time.
 *
 *    The Z80 runs in slices that end where the console's time asks, so that a write to the T6W28 or a DAC
 *    happens at the right moment; the T6W28 is brought up to the present before each write. The Z80 starts
 *    stopped and runs only while the console lets it.
 *
 *    Two things of MAME's that are not obvious: the T6W28 takes its tones' frequencies from port 1 only (the
 *    Z80's 4001), and port 0 gives the right side's volumes; MAME's io_w writes the TMP95C061's 0000A0 and
 *    0000A1 both to port 0, so the TMP95C061 alone can only change those volumes. And t6w28.cpp's
 *    device_start leaves port 1's registers (m_register[8..15]) as the memory had them: the chips are built
 *    in zeroed memory, so that they start at 0 and a run does not depend on the one before it.
 *    test_ngpsound.cpp is the check.
 *
 *    Beside MAME's T6W28, the runtime's (ngp/t6w28.h) takes the same writes and makes the same runs of
 *    samples, and every value is compared with MAME's (ns_t6w28_unlike); the runtime's mix (ngp/mix.h) mixes
 *    each run of its samples with the DACs' bytes, and every value is compared with MAME's sum (ns_mix_unlike).
 */
#include "ngpsound.h"
#include "ngp/mix.h"
#include "ngp/t6w28.h"

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

namespace ns
{
#include "emu.h"
#include "logmacro.h"
#include "z80.cpp"
#include "z80dasm.cpp"
#include "t6w28.cpp"
#include "dac.cpp"
}

namespace
{

constexpr uint64_t kZ80Period = 2;           // master clocks a Z80 cycle (6.144 MHz / 2 = 3.072 MHz)
constexpr uint64_t kT6w28Period = 32;        // master clocks a T6W28 sample (3.072 MHz / 16 = 192 kHz)

// how many samples of a chip that makes one every 'period' start before board time 'time'
uint64_t samples_before(uint64_t time, uint64_t period)
{
	return (time + period - 1) / period;
}

// ---- time: the console's, and while the Z80 runs, where its slice started and how long it is ----

uint64_t g_time = 0;                        // master clock the sound side has reached
bool g_running = false;
uint64_t g_slice_start = 0;
int g_slice_budget = 0;                     // Z80 cycles
uint64_t g_z80_cycles = 0;
bool g_z80_enabled = false;                 // the Z80 runs only when the console lets it

class sound_cpu : public ns::z80_device
{
public:
	// built in zeroed memory, as the T6W28 is (below)
	static void *operator new(size_t size)
	{
		void *memory = ::operator new(size);
		std::memset(memory, 0, size);
		return memory;
	}
	static void operator delete(void *memory) { ::operator delete(memory); }

	explicit sound_cpu(const ns::machine_config &config) : ns::z80_device(config, "audiocpu", nullptr, 3072000) {}

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
	int run_slice(int cycles)
	{
		m_cut = 0;
		const int done = run(cycles);
		return done - m_cut;
	}

	// the Z80's registers, for the state hash: PC, SP, AF (F as the flags are now), BC, DE, HL, IX, IY
	void registers(uint16_t *out)
	{
		const ns::PAIR16 *pairs[] = { &m_pc, &m_sp, &m_af, &m_bc, &m_de, &m_hl, &m_ix, &m_iy };
		for (int index = 0; index < 8; index++)
			out[index] = pairs[index]->w;
		out[2] = uint16_t((m_af.w & 0xff00) | get_f());
	}

private:
	int m_cut = 0;
};

ns::machine_config g_config;
std::unique_ptr<sound_cpu> g_z80;
uint8_t g_executed[0x10000 / 8];
uint8_t *g_shared_ram = nullptr;            // the 4 KB both CPUs see, for the state hash

uint64_t now()
{
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

// the T6W28's samples and the DACs' levels at each sample's time, and the DACs' last written bytes
samples g_t6w28_samples(2);
std::vector<uint8_t> g_dac_left;
std::vector<uint8_t> g_dac_right;
uint8_t g_dac_left_value = 0;
uint8_t g_dac_right_value = 0;

// the runtime's T6W28, its samples of the run in hand, and the values where it gave another sample than MAME's
ngp_t6w28 g_runtime_t6w28;
int16_t g_runtime_samples[2 * NGP_T6W28_MAX_RUN];
uint64_t g_runtime_unlike = 0;

// MAME's DACs (I/O 23 the left, 22 the right); a write is preceded by a catch_up (ns_dac_write), which is
// what the DAC's own update before it does in MAME, so their streams are not hooked
std::unique_ptr<ns::dac_8bit_r2r_device> g_left_dac;
std::unique_ptr<ns::dac_8bit_r2r_device> g_right_dac;

// the speaker's samples not yet taken, as MAME sums them, and the runtime's mix of the run in hand and the
// values where it gave another
std::vector<int16_t> g_mix_samples;
int16_t g_runtime_mix[2 * NGP_T6W28_MAX_RUN];
uint64_t g_mix_unlike = 0;

// ---- the T6W28 ----

class t6w28_chip : public ns::t6w28_device
{
public:
	explicit t6w28_chip(const ns::machine_config &config) : ns::t6w28_device(config, "t6w28", nullptr, 3072000)
	{
		stream().m_before_change = [this]() { catch_up(now()); };
	}

	// MAME leaves some members to whatever memory a device is built in (the T6W28's port 1 registers,
	// m_register[8..15], which its device_start does not set): built in zeroed memory, they start at 0 in
	// every run, and a run does not depend on the one before it
	static void *operator new(size_t size)
	{
		void *memory = ::operator new(size);
		std::memset(memory, 0, size);
		return memory;
	}
	static void operator delete(void *memory) { ::operator delete(memory); }
	void catch_up(uint64_t time)
	{
		const uint64_t due = samples_before(time, kT6w28Period);
		if (due <= g_t6w28_samples.made)
			return;
		const int count = int(due - g_t6w28_samples.made);
		generate(count);
		for (int index = 0; index < count; index++)
		{
			const float values[2] = { stream().m_outputs[0][size_t(index)], stream().m_outputs[1][size_t(index)] };
			g_t6w28_samples.push(values);
			g_dac_left.push_back(g_dac_left_value);
			g_dac_right.push_back(g_dac_right_value);
		}

		// The speaker's samples: the DACs' levels over the run (each the same until its next write, which
		// comes after a catch_up), and 0.50 of each T6W28 output with 0.25 of its side's DAC, as MAME sums
		// them, exact in floats, taken down to a whole number with 1.0 as 32768
		g_left_dac->generate(count);
		g_right_dac->generate(count);
		for (int index = 0; index < count; index++)
		{
			const float left = 0.50f * stream().m_outputs[0][size_t(index)]
				+ 0.25f * g_left_dac->stream().m_outputs[0][size_t(index)];
			const float right = 0.50f * stream().m_outputs[1][size_t(index)]
				+ 0.25f * g_right_dac->stream().m_outputs[0][size_t(index)];
			g_mix_samples.push_back(int16_t(std::floor(left * 32768.0f)));
			g_mix_samples.push_back(int16_t(std::floor(right * 32768.0f)));
		}

		// The runtime's chip makes the same run, each value compared as MAME's stream holds it (put_int's
		// value / 32768), and the runtime's mix of it with the DACs' bytes is compared with MAME's sums; a run
		// longer than the chip takes counts as unlike, every value of it
		if (count > NGP_T6W28_MAX_RUN)
		{
			g_runtime_unlike += uint64_t(count) * 2;
			g_mix_unlike += uint64_t(count) * 2;
			return;
		}
		ngp_t6w28_generate(&g_runtime_t6w28, unsigned(count), g_runtime_samples);
		for (int index = 0; index < count; index++)
			for (int output = 0; output < 2; output++)
				if (stream().m_outputs[output][size_t(index)] != float(g_runtime_samples[index * 2 + output]) * (1.0f / 32768.0f))
					g_runtime_unlike++;
		ngp_mix(g_runtime_samples, unsigned(count), g_dac_left_value, g_dac_right_value, g_runtime_mix);
		const size_t first = g_mix_samples.size() - size_t(count) * 2;
		for (size_t index = 0; index < size_t(count) * 2; index++)
			if (g_runtime_mix[index] != g_mix_samples[first + index])
				g_mix_unlike++;
	}
	void write(unsigned offset, uint8_t data)
	{
		catch_up(now());
		ns::t6w28_device::write(offset, data);
		ngp_t6w28_write(&g_runtime_t6w28, offset, data);
	}
	void set_enable(bool enable)
	{
		ns::t6w28_device::set_enable(enable);
		ngp_t6w28_set_enable(&g_runtime_t6w28, enable ? 1 : 0);
	}
};

std::unique_ptr<t6w28_chip> g_t6w28;

uint16_t g_z80_pc = 0;

void on_instruction(ns::offs_t address)
{
	const uint16_t pc = uint16_t(address);
	g_z80_pc = pc;
	g_executed[pc >> 3] |= uint8_t(1u << (pc & 7));
}

// the Z80 (while the console lets it run) up to board time 'until'
void run_to(uint64_t until)
{
	while (g_time < until)
	{
		if (!g_z80_enabled)
		{
			g_time = until;
			break;
		}
		g_slice_start = g_time;
		g_slice_budget = int(std::max<uint64_t>((until - g_time + kZ80Period - 1) / kZ80Period, 1));
		g_running = true;
		const int done = g_z80->run_slice(g_slice_budget);
		g_running = false;
		g_time += uint64_t(std::max(done, 0)) * kZ80Period;
		g_z80_cycles += uint64_t(std::max(done, 0));
	}
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

extern "C" void ns_attach(uint64_t time, uint8_t *shared_ram, const ns_host *host)
{
	g_z80 = std::make_unique<sound_cpu>(g_config);
	g_z80->space(ns::AS_PROGRAM).read = [shared_ram, host](ns::offs_t address) -> uint8_t {
		if (address < 0x1000)
			return shared_ram[address];
		if (address == 0x8000)
			return host->comm_read();
		return uint8_t(0);
	};
	g_z80->space(ns::AS_PROGRAM).write = [shared_ram, host](ns::offs_t address, uint8_t data) {
		if (address < 0x1000)
		{
			shared_ram[address] = data;
			return;
		}
		if (address >= 0x4000 && address <= 0x4001)
		{
			g_t6w28->write(address & 1, data);
			return;
		}
		if (address == 0x8000)
		{
			host->comm_write(data);
			return;
		}
		if (address == 0xc000)
		{
			host->int5(1);
			return;
		}
	};
	g_z80->space(ns::AS_IO).read = [](ns::offs_t) -> uint8_t { return uint8_t(0); };
	g_z80->space(ns::AS_IO).write = [host](ns::offs_t, uint8_t) {
		g_z80->set_input_line(ns::INPUT_LINE_IRQ0, ns::CLEAR_LINE);
		host->int5(0);
	};
	g_z80->irq_vector = []() { return 0xff; };
	g_z80->instruction_hook = on_instruction;
	g_z80->start();
	g_z80->reset();

	g_t6w28 = std::make_unique<t6w28_chip>(g_config);
	ngp_t6w28_reset(&g_runtime_t6w28);
	g_runtime_unlike = 0;
	g_t6w28->start();
	g_t6w28->reset();
	g_t6w28->set_enable(false);

	g_left_dac = std::make_unique<ns::dac_8bit_r2r_device>(g_config, "ldac", nullptr, 0);
	g_right_dac = std::make_unique<ns::dac_8bit_r2r_device>(g_config, "rdac", nullptr, 0);
	g_left_dac->start();
	g_right_dac->start();
	g_mix_samples.clear();
	g_mix_unlike = 0;

	g_time = time;
	g_running = false;
	g_slice_start = 0;
	g_slice_budget = 0;
	g_t6w28_samples.data.clear();
	g_t6w28_samples.first = g_t6w28_samples.made = samples_before(time, kT6w28Period);
	g_dac_left.clear();
	g_dac_right.clear();
	g_dac_left_value = 0;
	g_dac_right_value = 0;
	g_z80_enabled = false;
	g_z80_cycles = 0;
	g_z80_pc = 0;
	g_frame_samples_hash = 0;
	g_shared_ram = shared_ram;
	std::memset(g_executed, 0, sizeof(g_executed));
	g_mix.clear();
}

extern "C" void ns_run_until(uint64_t time)
{
	run_to(time);
}

extern "C" void ns_z80_start(uint64_t time)
{
	run_to(time);
	g_z80->reset();
	g_z80->set_input_line(ns::INPUT_LINE_IRQ0, ns::CLEAR_LINE);
	g_z80_enabled = true;
}

extern "C" void ns_z80_stop(uint64_t time)
{
	run_to(time);
	g_z80_enabled = false;
}

extern "C" void ns_z80_nmi(uint64_t time)
{
	run_to(time);
	g_z80->set_input_line(ns::INPUT_LINE_NMI, ns::ASSERT_LINE);
	g_z80->set_input_line(ns::INPUT_LINE_NMI, ns::CLEAR_LINE);
}

extern "C" void ns_z80_irq(uint64_t time)
{
	run_to(time);
	g_z80->set_input_line(ns::INPUT_LINE_IRQ0, ns::ASSERT_LINE);
}

extern "C" void ns_psg_write(uint64_t time, uint8_t data)
{
	run_to(time);
	g_t6w28->write(0, data);
}

extern "C" void ns_psg_enable(uint64_t time, int enable)
{
	run_to(time);
	g_t6w28->set_enable(enable != 0);
}

extern "C" void ns_dac_write(uint64_t time, int left, uint8_t data)
{
	run_to(time);
	g_t6w28->catch_up(time);
	if (left)
	{
		g_dac_left_value = data;
		g_left_dac->write(data);
	}
	else
	{
		g_dac_right_value = data;
		g_right_dac->write(data);
	}
}

extern "C" void ns_end_frame(uint64_t time)
{
	run_to(time);
	g_t6w28->catch_up(time);

	std::vector<float> t6w28;
	uint64_t hash = 0xcbf29ce484222325ull;
	hash = take(g_t6w28_samples, samples_before(time, kT6w28Period), hash, t6w28);

	// hash the DACs' bytes at each sample's time
	const size_t sample_count = t6w28.size() / 2;
	for (size_t index = 0; index < sample_count; index++)
	{
		hash = hash_bytes(&g_dac_left[index], 1, hash);
		hash = hash_bytes(&g_dac_right[index], 1, hash);
	}
	g_frame_samples_hash = hash;

	// the speaker's samples of the same samples, as catch_up summed them
	g_mix.assign(g_mix_samples.begin(), g_mix_samples.begin() + ptrdiff_t(sample_count * 2));
	g_mix_samples.erase(g_mix_samples.begin(), g_mix_samples.begin() + ptrdiff_t(sample_count * 2));
	g_dac_left.erase(g_dac_left.begin(), g_dac_left.begin() + ptrdiff_t(sample_count));
	g_dac_right.erase(g_dac_right.begin(), g_dac_right.begin() + ptrdiff_t(sample_count));
}

extern "C" uint64_t ns_frame_samples_hash(void)
{
	return g_frame_samples_hash;
}

extern "C" uint64_t ns_state_hash(void)
{
	uint64_t hash = 0xcbf29ce484222325ull;
	// the Z80's registers
	uint16_t registers[8];
	g_z80->registers(registers);
	hash = hash_bytes(registers, sizeof(registers), hash);
	// the 4 KB shared RAM
	hash = hash_bytes(g_shared_ram, 0x1000, hash);
	return hash;
}

extern "C" const int16_t *ns_frame_mix(size_t *count)
{
	*count = g_mix.size() / 2;
	return g_mix.data();
}

extern "C" uint32_t ns_mix_rate(void)
{
	return 192000;
}

extern "C" uint64_t ns_z80_cycles(void)
{
	return g_z80_cycles;
}

extern "C" const uint8_t *ns_executed(void)
{
	return g_executed;
}

extern "C" uint16_t ns_z80_pc(void)
{
	return g_z80_pc;
}

extern "C" uint64_t ns_t6w28_unlike(void)
{
	return g_runtime_unlike;
}

extern "C" uint64_t ns_mix_unlike(void)
{
	return g_mix_unlike;
}
