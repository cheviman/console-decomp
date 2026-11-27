/**
 *    Name:        test_mix.cpp
 *    Description: the check of the runtime's mix (ngp/mix.h) against MAME 0.289's dac.cpp and ngp.cpp's routes
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Both sides take the same operations: a run of T6W28 samples (output 0 and output 1 in turn, each 0-7FFF)
 *    with the left and right DACs at the bytes given, and the speaker's sum compared value by value: MAME's
 *    DAC_8BIT_R2R (dac.cpp) with ngp.cpp's gains (T6W28 at 0.50, each DAC at 0.25) against ngp_mix's integers
 *    (1.0 as 32768, the half of an odd T6W28 value dropped). The DACs are read through their own stream
 *    (generate once per write, the level is constant until the next), never by a formula of the test's.
 *    Exhaustive over every byte on each DAC against a sweep of every T6W28 value on both outputs, then random
 *    runs, half in place, with a guard past the run's end. It fails when a case differs, and when the cases
 *    never wrote a byte on a DAC, never had a T6W28 value on an output, never made the extremes -8192 or 24511
 *    on a side, never ran in place, never ran of count 0 or 1, or never ran the longest. It prints each
 *    failure and exits 0 only when all hold.
 */

#include "ngp/mix.h"

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
#include "dac.cpp"
}

namespace
{

ns::machine_config g_config;

int g_failures = 0;
uint64_t g_values_compared = 0;
int g_fail_printed = 0;
int g_exhaustive_runs = 0;
int g_random_runs = 0;
int g_random_in_place = 0;

/* Coverage tracking */
bool g_left_byte[256] = { false };
bool g_right_byte[256] = { false };
bool g_t6w28_output0[32768] = { false };
bool g_t6w28_output1[32768] = { false };
bool g_left_extreme_low = false;
bool g_left_extreme_high = false;
bool g_right_extreme_low = false;
bool g_right_extreme_high = false;
bool g_in_place_run = false;
bool g_count_0_run = false;
bool g_count_1_run = false;
bool g_max_run_done = false;

/* MAME's DAC, built as ngpsound.cpp builds it: its constructor sets every member, so no zeroed memory */
using mame_dac = ns::dac_8bit_r2r_device;

/* The speaker's sum for one side, in MAME's floats: the T6W28 output (value / 32768) at 0.50 and the DAC's
   level at 0.25 */
float mame_side(int16_t t6w28, float dac_level)
{
	return 0.50f * (float(t6w28) * (1.0f / 32768.0f)) + 0.25f * dac_level;
}

/* One side of the mix compared: MAME's sum against ngp_mix's integer; false when it differs */
bool compare_side(int case_num, int step, int side, int16_t t6w28, uint8_t byte, float mame_sum,
	int16_t runtime_val)
{
	int expected = int(std::floor(mame_sum * 32768.0f));
	g_values_compared++;

	if (expected != runtime_val)
	{
		if (g_fail_printed < 20)
		{
			std::printf("FAIL: case %d step %d side %d: T6W28 %d, byte %d, MAME %d, runtime %d\n",
				case_num, step, side, int(t6w28), int(byte), expected, int(runtime_val));
			g_fail_printed++;
		}
		g_failures++;
		return false;
	}
	return true;
}

/* The DAC's level for a byte: written, then one sample generated (the level is constant until the next write) */
float dac_level(ns::dac_8bit_r2r_device &dac, uint8_t byte)
{
	dac.write(byte);
	dac.generate(1);
	return dac.stream().m_outputs[0][0];
}

/* A run of the mix compared value by value; false when any differed */
bool compare_run(int case_num, int step, const int16_t *t6w28, unsigned count,
	uint8_t left_byte, uint8_t right_byte,
	float left_level, float right_level,
	const int16_t *out)
{
	bool matched = true;

	for (unsigned i = 0; i < count; i++)
	{
		int16_t t0 = t6w28[i * 2];
		int16_t t1 = t6w28[i * 2 + 1];

		g_t6w28_output0[t0 & 0x7FFF] = true;
		g_t6w28_output1[t1 & 0x7FFF] = true;

		float left_sum = mame_side(t0, left_level);
		float right_sum = mame_side(t1, right_level);

		if (!compare_side(case_num, step, 0, t0, left_byte, left_sum, out[i * 2]))
			matched = false;
		if (!compare_side(case_num, step, 1, t1, right_byte, right_sum, out[i * 2 + 1]))
			matched = false;

		if (out[i * 2] == -8192)
			g_left_extreme_low = true;
		if (out[i * 2] == 24511)
			g_left_extreme_high = true;
		if (out[i * 2 + 1] == -8192)
			g_right_extreme_low = true;
		if (out[i * 2 + 1] == 24511)
			g_right_extreme_high = true;
	}
	return matched;
}

void check_coverage()
{
	for (int b = 0; b < 256; b++)
	{
		if (!g_left_byte[b])
		{
			std::printf("FAIL: never wrote byte %d to the left DAC\n", b);
			g_failures++;
		}
		if (!g_right_byte[b])
		{
			std::printf("FAIL: never wrote byte %d to the right DAC\n", b);
			g_failures++;
		}
	}
	for (int v = 0; v < 32768; v++)
	{
		if (!g_t6w28_output0[v])
		{
			std::printf("FAIL: never had T6W28 output 0 value %d\n", v);
			g_failures++;
		}
		if (!g_t6w28_output1[v])
		{
			std::printf("FAIL: never had T6W28 output 1 value %d\n", v);
			g_failures++;
		}
	}
	if (!g_left_extreme_low)
	{
		std::printf("FAIL: never had the left side at -8192\n");
		g_failures++;
	}
	if (!g_left_extreme_high)
	{
		std::printf("FAIL: never had the left side at 24511\n");
		g_failures++;
	}
	if (!g_right_extreme_low)
	{
		std::printf("FAIL: never had the right side at -8192\n");
		g_failures++;
	}
	if (!g_right_extreme_high)
	{
		std::printf("FAIL: never had the right side at 24511\n");
		g_failures++;
	}
	if (!g_in_place_run)
	{
		std::printf("FAIL: never ran in place\n");
		g_failures++;
	}
	if (!g_count_0_run)
	{
		std::printf("FAIL: never ran of count 0\n");
		g_failures++;
	}
	if (!g_count_1_run)
	{
		std::printf("FAIL: never ran of count 1\n");
		g_failures++;
	}
	if (!g_max_run_done)
	{
		std::printf("FAIL: never ran the longest\n");
		g_failures++;
	}
}

/* 1. A DAC never written gives the level of byte 0 */
void check_unwritten_dac()
{
	auto fresh = std::make_unique<mame_dac>(g_config, "fresh", nullptr, 0);
	fresh->start();
	fresh->generate(1);
	float fresh_level = fresh->stream().m_outputs[0][0];

	auto written = std::make_unique<mame_dac>(g_config, "written0", nullptr, 0);
	written->start();
	float written_level = dac_level(*written, 0);

	if (fresh_level != written_level)
	{
		std::printf("FAIL: a DAC never written does not give the level of byte 0\n");
		g_failures++;
	}
}

/* 2. Exhaustive: every byte on each DAC against a sweep of every T6W28 value on both outputs */
void check_exhaustive()
{
	static int16_t t6w28[2 * 32768];
	static int16_t out[2 * 32768];

	for (unsigned v = 0; v < 32768; v++)
	{
		t6w28[v * 2] = int16_t(v);
		t6w28[v * 2 + 1] = int16_t(32767 - v);
	}

	auto ldac = std::make_unique<mame_dac>(g_config, "ldac", nullptr, 0);
	auto rdac = std::make_unique<mame_dac>(g_config, "rdac", nullptr, 0);
	ldac->start();
	rdac->start();

	for (int b = 0; b < 256; b++)
	{
		uint8_t left_byte = uint8_t(b);
		uint8_t right_byte = uint8_t(255 - b);

		float left_level = dac_level(*ldac, left_byte);
		float right_level = dac_level(*rdac, right_byte);

		g_left_byte[b] = true;
		g_right_byte[255 - b] = true;

		ngp_mix(t6w28, 32768, left_byte, right_byte, out);
		g_exhaustive_runs++;

		if (!compare_run(1, b, t6w28, 32768, left_byte, right_byte, left_level, right_level, out))
			return;
	}
}

/* 3. Random runs */
void check_random()
{
	std::mt19937 rng(12345);
	static int16_t t6w28[2 * 4096 + 16];
	static int16_t input[2 * 4096];
	static int16_t out[2 * 4096 + 16];

	auto ldac = std::make_unique<mame_dac>(g_config, "ldac", nullptr, 0);
	auto rdac = std::make_unique<mame_dac>(g_config, "rdac", nullptr, 0);
	ldac->start();
	rdac->start();

	for (int case_num = 0; case_num < 20000; case_num++)
	{
		unsigned count;
		if (rng() % 100 == 0)
			count = 4096;
		else
			count = unsigned(rng() % 4097);

		if (count == 0)
			g_count_0_run = true;
		if (count == 1)
			g_count_1_run = true;
		if (count == 4096)
			g_max_run_done = true;

		for (unsigned i = 0; i < count * 2; i++)
			t6w28[i] = int16_t(rng() % 32768);

		uint8_t left_byte = uint8_t(rng() % 256);
		uint8_t right_byte = uint8_t(rng() % 256);

		g_left_byte[left_byte] = true;
		g_right_byte[right_byte] = true;

		float left_level = dac_level(*ldac, left_byte);
		float right_level = dac_level(*rdac, right_byte);

		bool in_place = (rng() % 2) == 0;
		if (in_place)
			g_in_place_run = true;

		int16_t *out_ptr;
		if (in_place)
		{
			for (unsigned i = count * 2; i < count * 2 + 16; i++)
				t6w28[i] = int16_t(0x5A5A);
			out_ptr = t6w28;
		}
		else
		{
			std::memset(out, 0x5A, sizeof(out));
			out_ptr = out;
		}

		std::memcpy(input, t6w28, count * 2 * sizeof(int16_t));

		ngp_mix(t6w28, count, left_byte, right_byte, out_ptr);
		g_random_runs++;
		if (in_place)
			g_random_in_place++;

		if (!compare_run(2, case_num, input, count, left_byte, right_byte, left_level, right_level, out_ptr))
			return;

		/* The guard past the run's end must be intact */
		if (in_place)
		{
			for (unsigned i = count * 2; i < count * 2 + 16; i++)
			{
				if (out_ptr[i] != int16_t(0x5A5A))
				{
					std::printf("FAIL: case %d: guard past the run's end at %u is %d, not 0x5A5A\n",
						case_num, i, int(out_ptr[i]));
					g_failures++;
					return;
				}
			}
		}
		else
		{
			for (unsigned i = count * 2; i < count * 2 + 16; i++)
			{
				if (out[i] != int16_t(0x5A5A))
				{
					std::printf("FAIL: case %d: guard past the run's end at %u is %d, not 0x5A5A\n",
						case_num, i, int(out[i]));
					g_failures++;
					return;
				}
			}
		}
	}
}

} // anonymous namespace

int main()
{
	check_unwritten_dac();
	check_exhaustive();
	check_random();

	check_coverage();

	std::printf("values compared: %llu (%d exhaustive runs of 32768 samples, %d random runs, %d in place)\n",
		(unsigned long long)g_values_compared, g_exhaustive_runs, g_random_runs, g_random_in_place);

	if (g_failures == 0)
	{
		std::printf("test_mix: all checks pass\n");
		return 0;
	}
	else
	{
		std::printf("%d failures\n", g_failures);
		return 1;
	}
}
