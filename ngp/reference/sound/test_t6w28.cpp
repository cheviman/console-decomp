/**
 *    Name:        test_t6w28.cpp
 *    Description: the check of the runtime's T6W28 (ngp/t6w28.h) against MAME 0.289's t6w28.cpp
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Both chips take the same random operations: bytes written to ports 0 and 1, enables, and runs of samples
 *    of random lengths up to NGP_T6W28_MAX_RUN, a short run before every write, since the runs' lengths change
 *    what a voice that is off does. Every value is compared as MAME's stream holds it (put_int's value / 32768);
 *    a case ends at its first run that differs, since its chips have parted. MAME's chip is made with its
 *    zeroed operator new, as ngpsound.cpp makes it: its device_start leaves port 1's registers and the noise
 *    modes as the memory had them. So that the test cannot pass by not testing, it fails when the cases never
 *    wrote one of port 0's noise modes or one of the 16 volumes on either port, never heard either output,
 *    never ran disabled, never moved the noise's shift register in white and in periodic mode, or never made
 *    the longest run. It prints each failure and exits 0 only when all hold.
 */

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
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ns
{
#include "emu.h"
#include "logmacro.h"
#include "t6w28.cpp"
}

namespace
{

ns::machine_config g_config;

class mame_chip : public ns::t6w28_device
{
public:
	explicit mame_chip(const ns::machine_config &config) : ns::t6w28_device(config, "t6w28", nullptr, 3072000)
	{
	}

	static void *operator new(size_t size)
	{
		void *memory = ::operator new(size);
		std::memset(memory, 0, size);
		return memory;
	}
	static void operator delete(void *memory) { ::operator delete(memory); }
};

int g_failures = 0;
uint64_t g_values_compared = 0;
int g_fail_printed = 0;

/* Coverage tracking */
bool g_noise_written[8] = { false };
bool g_volume_written[2][16] = { { false }, { false } };
bool g_output0_nonzero = false;
bool g_output1_nonzero = false;
bool g_disabled_sample = false;
bool g_white_noise_rng_changed = false;
bool g_periodic_noise_rng_changed = false;
bool g_max_run_done = false;

void check_coverage()
{
	for (int i = 0; i < 8; i++)
	{
		if (!g_noise_written[i])
		{
			std::printf("FAIL: never wrote noise register value %d\n", i);
			g_failures++;
		}
	}
	for (int port = 0; port < 2; port++)
	{
		for (int i = 0; i < 16; i++)
		{
			if (!g_volume_written[port][i])
			{
				std::printf("FAIL: never wrote volume value %d on port %d\n", i, port);
				g_failures++;
			}
		}
	}
	if (!g_output0_nonzero)
	{
		std::printf("FAIL: never had output 0 non-zero\n");
		g_failures++;
	}
	if (!g_output1_nonzero)
	{
		std::printf("FAIL: never had output 1 non-zero\n");
		g_failures++;
	}
	if (!g_disabled_sample)
	{
		std::printf("FAIL: never had a sample with enabled 0\n");
		g_failures++;
	}
	if (!g_white_noise_rng_changed)
	{
		std::printf("FAIL: never had white noise RNG change\n");
		g_failures++;
	}
	if (!g_periodic_noise_rng_changed)
	{
		std::printf("FAIL: never had periodic noise RNG change\n");
		g_failures++;
	}
	if (!g_max_run_done)
	{
		std::printf("FAIL: never did a max run\n");
		g_failures++;
	}
}

/* Compares a run's values, MAME's stream against the runtime's buffer; false when any differed */
bool compare_samples(int case_num, int step, mame_chip &mame, int count, const int16_t *buffer)
{
	bool matched = true;
	for (int i = 0; i < count; i++)
	{
		for (int o = 0; o < 2; o++)
		{
			float mame_val = mame.stream().m_outputs[o][i];
			float runtime_val = float(buffer[2 * i + o]) * (1.0f / 32768.0f);
			g_values_compared++;

			if (mame_val != runtime_val)
			{
				if (g_fail_printed < 10)
				{
					std::printf("FAIL: case %d step %d sample %d output %d: MAME %d, runtime %d\n",
						case_num, step, i, o,
						int(mame_val * 32768.0f),
						buffer[2 * i + o]);
					g_fail_printed++;
				}
				g_failures++;
				matched = false;
			}
		}
	}
	return matched;
}

bool generate_and_compare(int case_num, int step, mame_chip &mame, ngp_t6w28 &runtime, int count, int16_t *buffer)
{
	mame.generate(count);
	ngp_t6w28_generate(&runtime, count, buffer);
	return compare_samples(case_num, step, mame, count, buffer);
}

void write_both(mame_chip &mame, ngp_t6w28 &runtime, int port, uint8_t data)
{
	mame.write(port, data);
	ngp_t6w28_write(&runtime, port, data);

	/* Track coverage */
	if (data & 0x80)
	{
		int reg = (data >> 4) & 0x07;
		int value = data & 0x0f;

		if (port == 0 && reg == 6)
		{
			g_noise_written[value & 7] = true;
		}

		/* Volume registers are odd: 1, 3, 5, 7 */
		if (reg % 2 == 1)
		{
			g_volume_written[port][value] = true;
		}
	}
}

void run_case(int case_num)
{
	std::mt19937 rng(case_num);
	static int16_t buffer[2 * NGP_T6W28_MAX_RUN];

	auto mame = std::make_unique<mame_chip>(g_config);
	ngp_t6w28 runtime;
	ngp_t6w28_reset(&runtime);

	mame->start();

	/* The setup: random tone periods, every volume loud, and a random noise on port 0; then a run before any
	   enable, which both chips must make silent (MAME's device_start leaves m_enabled as zeroed memory had it),
	   and the chips enabled */
	for (int port = 0; port < 2; port++)
	{
		/* Set random tone periods for voices 0-2 */
		for (int t = 0; t < 3; t++)
		{
			int p = int(rng() % 1024);
			/* The period's low 4 bits in the latch byte, its high 6 in the data byte after it */
			uint8_t latch = 0x80 | ((2 * t) << 4) | (p & 0x0f);
			write_both(*mame, runtime, port, latch);

			uint8_t data = (p >> 4) & 0x3f;
			write_both(*mame, runtime, port, data);
		}

		/* Set volumes to 0 (loud) for voices 0-3 */
		for (int v = 0; v < 4; v++)
		{
			uint8_t latch = 0x80 | ((2 * v + 1) << 4) | 0;
			write_both(*mame, runtime, port, latch);
		}

		/* On port 0, set the noise */
		if (port == 0)
		{
			int noise_val = int(rng() % 8);
			uint8_t latch = 0x80 | (6 << 4) | noise_val;
			write_both(*mame, runtime, port, latch);
		}
	}
	if (!generate_and_compare(case_num, -1, *mame, runtime, 64, buffer))
		return;
	mame->set_enable(true);
	ngp_t6w28_set_enable(&runtime, 1);

	for (int step = 0; step < 400; step++)
	{
		int op = int(rng() % 100);

		if (op < 60)
		{
			/* Write operation */
			int port = int(rng() % 2);
			uint8_t data;
			int write_type = int(rng() % 4);

			if (write_type == 0)
			{
				/* Latch byte: 0x80 | (register << 4) | value */
				int reg = int(rng() % 8);
				int value = int(rng() % 16);
				data = 0x80 | (reg << 4) | value;
			}
			else if (write_type == 1)
			{
				/* Data byte 0x00-0x7F */
				data = uint8_t(rng() % 128);
			}
			else
			{
				/* Any byte 0x00-0xFF */
				data = uint8_t(rng() % 256);
			}

			/* Generate a short run before the write */
			int run_len = 1 + int(rng() % 64);
			if (!generate_and_compare(case_num, step, *mame, runtime, run_len, buffer))
			{
				return;
			}

			/* Perform the write */
			write_both(*mame, runtime, port, data);
		}
		else if (op < 70)
		{
			/* set_enable */
			int enable = int(rng() % 2);
			mame->set_enable(enable);
			ngp_t6w28_set_enable(&runtime, enable);
		}
		else
		{
			/* Sample run */
			int run_len;
			if (rng() % 50 == 0)
			{
				run_len = NGP_T6W28_MAX_RUN;
				g_max_run_done = true;
			}
			else
			{
				run_len = 1 + int(rng() % 4000);
			}

			/* Track RNG changes for noise */
			uint32_t rng0_before = runtime.rng[0];
			int noise_mode0 = runtime.noise_mode[0];

			if (!generate_and_compare(case_num, step, *mame, runtime, run_len, buffer))
			{
				return;
			}

			uint32_t rng0_after = runtime.rng[0];
			if (rng0_before != rng0_after)
			{
				if (noise_mode0 == 1)
					g_white_noise_rng_changed = true;
				else
					g_periodic_noise_rng_changed = true;
			}

			/* Check outputs and enabled state */
			for (int i = 0; i < run_len; i++)
			{
				if (buffer[2 * i] != 0)
					g_output0_nonzero = true;
				if (buffer[2 * i + 1] != 0)
					g_output1_nonzero = true;
				if (!runtime.enabled)
					g_disabled_sample = true;
			}
		}
	}
}

} // anonymous namespace

int main()
{
	int num_cases = 400;
	for (int i = 0; i < num_cases; i++)
	{
		run_case(i);
	}

	check_coverage();

	if (g_failures == 0)
	{
		std::printf("all checks hold: %d cases, %llu values compared\n", num_cases,
			(unsigned long long)g_values_compared);
		return 0;
	}
	else
	{
		std::printf("%d failures\n", g_failures);
		return 1;
	}
}
