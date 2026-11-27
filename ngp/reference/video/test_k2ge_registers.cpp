/**
 *    Name:        test_k2ge_registers.cpp
 *    Description: the check of the runtime's K2GE registers and timing (ngp/k2ge.h) against MAME's K2GE
 *                 (k2gevideo.h), both driven with the same random operations at the same times
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The test drives the runtime's K2GE (ngp/runtime/src/k2ge.c) and MAME's K2GE (through k2gevideo.h) with
 *    the same writes, reads, and time advances. After every operation it compares the chips' memory, the
 *    results of any read, and the lists of VBLANK and HBLANK calls (time and state) that each chip has made
 *    so far, and a case stops at its first difference. It also tracks coverage of specific register
 *    behaviours (such as the loss of a write to 7E2 when 7F0 is not AA, or the read of the RAS registers at
 *    specific times) to ensure that the random operations actually exercised the code paths they are meant
 *    to test.
 */

#include "k2gevideo.h"
#include "ngp/k2ge.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace
{
struct call
{
	uint64_t time;
	int state;
};

uint64_t g_present = 0;
bool g_firing = false;                  // while MAME's events fire: the calls of a line's start
std::vector<call> g_mame_vblank;
std::vector<call> g_mame_hblank;
std::vector<call> g_rt_vblank;
std::vector<call> g_rt_hblank;

uint8_t g_memory[NGP_K2GE_SIZE];
ngp_k2ge_window g_window;

uint64_t g_next_line_time = NGP_K2GE_DOTS;
uint64_t g_next_hblank_off_time = UINT64_MAX;

int g_case = 0;
int g_op = 0;
int g_failures = 0;
uint64_t g_total_vblank = 0;
uint64_t g_total_hblank = 0;

struct coverage
{
	bool vblank_line_1 = false;
	bool vblank_line_0 = false;
	bool vblank_write_1 = false;
	bool vblank_write_0 = false;
	bool hblank_1 = false;
	bool hblank_0 = false;
	bool write_7e2_lost = false;
	bool write_7e2_kept = false;
	bool write_030_low = false;
	bool read_009[NGP_K2GE_LINES] = { false };
	bool read_008[129] = { false };
};

coverage g_cov;

uint64_t now()
{
	return g_present;
}

void mame_vblank_cb(int state)
{
	g_mame_vblank.push_back({ g_present, state });
	if (g_firing && state == 1)
		g_cov.vblank_line_1 = true;
	else if (g_firing)
		g_cov.vblank_line_0 = true;
}

void mame_hblank_cb(int state)
{
	g_mame_hblank.push_back({ g_present, state });
	if (state == 1) g_cov.hblank_1 = true;
	else g_cov.hblank_0 = true;
}

void advance_mame(uint64_t t)
{
	while (nv_next_event() <= t)
	{
		g_firing = true;
		nv_fire(nv_next_event(), &g_present);
		g_firing = false;
	}
	g_present = t;
}

void advance_runtime(uint64_t t)
{
	while (true)
	{
		uint64_t next_event = g_next_line_time;
		if (g_next_hblank_off_time < next_event)
		{
			next_event = g_next_hblank_off_time;
		}
		if (next_event > t)
		{
			break;
		}

		g_present = next_event;

		if (next_event == g_next_hblank_off_time)
		{
			g_rt_hblank.push_back({ g_present, 0 });
			g_next_hblank_off_time = UINT64_MAX;
		}

		if (next_event == g_next_line_time)
		{
			unsigned line = (unsigned)(g_present / NGP_K2GE_DOTS) % NGP_K2GE_LINES;
			ngp_k2ge_calls calls;
			ngp_k2ge_line_start(g_memory, line, &g_window, &calls);

			if (calls.vblank != NGP_K2GE_NO_CALL)
			{
				g_rt_vblank.push_back({ g_present, calls.vblank });
			}
			if (calls.hblank != NGP_K2GE_NO_CALL)
			{
				g_rt_hblank.push_back({ g_present, calls.hblank });
			}
			if (calls.hblank_off)
			{
				g_next_hblank_off_time = g_present + NGP_K2GE_HBLANK_OFF;
			}

			g_next_line_time += NGP_K2GE_DOTS;
		}
	}
	g_present = t;
}

bool check_calls(const char *name, const std::vector<call> &a, const std::vector<call> &b, size_t &idx)
{
	if (g_failures >= 10)
	{
		return false;
	}

	while (idx < a.size() && idx < b.size())
	{
		if (a[idx].time != b[idx].time || a[idx].state != b[idx].state)
		{
			printf("FAIL: case %d op %d time %llu: %s mismatch at %zu (rt: %llu %d, mame: %llu %d)\n",
				g_case, g_op, (unsigned long long)a[idx].time, name, idx,
				(unsigned long long)a[idx].time, a[idx].state,
				(unsigned long long)b[idx].time, b[idx].state);
			g_failures++;
			return false;
		}
		idx++;
	}

	if (a.size() != b.size())
	{
		printf("FAIL: case %d op %d time %llu: %s length mismatch (rt: %zu, mame: %zu)\n",
			g_case, g_op, (unsigned long long)g_present, name, a.size(), b.size());
		g_failures++;
		return false;
	}

	return true;
}
}

int main()
{
	const int cases = 200;
	const int ops_per_case = 4000;

	for (int c = 0; c < cases; c++)
	{
		g_case = c;
		g_present = 0;
		g_mame_vblank.clear();
		g_mame_hblank.clear();
		g_rt_vblank.clear();
		g_rt_hblank.clear();
		g_next_line_time = NGP_K2GE_DOTS;
		g_next_hblank_off_time = UINT64_MAX;

		nv_attach(now, mame_vblank_cb, mame_hblank_cb);

		int rt_vblank = ngp_k2ge_reset(g_memory);
		if (rt_vblank != NGP_K2GE_NO_CALL)
		{
			g_rt_vblank.push_back({ 0, rt_vblank });
		}

		std::mt19937 rng(c);
		std::uniform_int_distribution<int> step_dist(1, 600);
		std::uniform_int_distribution<int> op_dist(0, 99);
		std::uniform_int_distribution<int> offset_dist(0, 16);
		std::uniform_int_distribution<int> data_dist(0, 255);

		size_t v_idx = 0;
		size_t h_idx = 0;

		for (int o = 0; o < ops_per_case; o++)
		{
			g_op = o;

			int step = step_dist(rng);
			if (rng() % 20 == 0)
			{
				step = 3 * NGP_K2GE_DOTS * NGP_K2GE_LINES;
			}
			g_present += step;

			advance_mame(g_present);
			advance_runtime(g_present);

			int op_type = op_dist(rng);
			int read_val_rt = -1;
			int read_val_mame = -1;

			if (op_type < 55)
			{
				int off_type = offset_dist(rng);
				uint32_t offset;
				uint8_t data;

				if (off_type == 0)
				{
					offset = 0x000;
					data = data_dist(rng);
				}
				else if (off_type >= 1 && off_type <= 4)
				{
					offset = 0x002 + off_type - 1;
					data = data_dist(rng);
				}
				else if (off_type == 5)
				{
					offset = 0x008;
					data = data_dist(rng);
				}
				else if (off_type == 6)
				{
					offset = 0x009;
					data = data_dist(rng);
				}
				else if (off_type == 7)
				{
					offset = 0x010;
					data = data_dist(rng);
				}
				else if (off_type == 8)
				{
					offset = 0x012;
					data = data_dist(rng);
				}
				else if (off_type == 9)
				{
					offset = 0x030;
					data = data_dist(rng);
					if (data & 0x7F)
					{
						g_cov.write_030_low = true;
					}
				}
				else if (off_type >= 10 && off_type <= 12)
				{
					offset = 0x100 + rng() % 0x19;
					data = data_dist(rng);
				}
				else if (off_type == 13)
				{
					offset = 0x200 + (data_dist(rng) & 0x1FF);
					data = data_dist(rng);
				}
				else if (off_type == 14)
				{
					offset = 0x7E2;
					data = data_dist(rng);
					if (g_memory[0x7F0] != 0xAA)
					{
						g_cov.write_7e2_lost = true;
					}
					else
					{
						g_cov.write_7e2_kept = true;
					}
				}
				else if (off_type == 15)
				{
					offset = 0x7F0;
					data = (rng() % 2 == 0) ? 0xAA : data_dist(rng);
				}
				else
				{
					offset = rng() % NGP_K2GE_SIZE;
					data = data_dist(rng);
				}

				nv_write(offset, data);
				int rt_vb = ngp_k2ge_write(g_memory, offset, data);
				if (rt_vb != NGP_K2GE_NO_CALL)
				{
					g_rt_vblank.push_back({ g_present, rt_vb });
					if (rt_vb == 1) g_cov.vblank_write_1 = true;
					else g_cov.vblank_write_0 = true;
				}
			}
			else if (op_type < 85)
			{
				int off_type = offset_dist(rng);
				uint32_t offset;
				if (off_type == 0) offset = 0x008;
				else if (off_type == 1) offset = 0x009;
				else if (off_type == 2) offset = 0x010;
				else if (off_type == 3) offset = 0x000;
				else offset = rng() % NGP_K2GE_SIZE;

				read_val_mame = nv_read(offset);
				unsigned line = (unsigned)(g_present / NGP_K2GE_DOTS) % NGP_K2GE_LINES;
				unsigned dot = (unsigned)(g_present % NGP_K2GE_DOTS);
				read_val_rt = ngp_k2ge_read(g_memory, offset, line, dot);

				if (offset == 0x009)
				{
					g_cov.read_009[line] = true;
				}
				if (offset == 0x008 && read_val_rt <= 128)
				{
					g_cov.read_008[read_val_rt] = true;
				}
			}

			if (memcmp(nv_memory(), g_memory, NGP_K2GE_SIZE) != 0)
			{
				for (size_t i = 0; i < NGP_K2GE_SIZE; i++)
				{
					if (nv_memory()[i] != g_memory[i])
					{
						printf("FAIL: case %d op %d time %llu: memory mismatch at %zu (rt: %02X, mame: %02X)\n",
							g_case, g_op, (unsigned long long)g_present, i, g_memory[i], nv_memory()[i]);
						g_failures++;
						break;
					}
				}
				break;
			}

			if (read_val_rt != -1 && read_val_rt != read_val_mame)
			{
				printf("FAIL: case %d op %d time %llu: read mismatch (rt: %d, mame: %d)\n",
					g_case, g_op, (unsigned long long)g_present, read_val_rt, read_val_mame);
				g_failures++;
				break;
			}

			bool v_ok = check_calls("VBLANK", g_rt_vblank, g_mame_vblank, v_idx);
			bool h_ok = check_calls("HBLANK", g_rt_hblank, g_mame_hblank, h_idx);
			if (!v_ok || !h_ok)
			{
				break;
			}

			if (g_failures >= 10)
			{
				break;
			}
		}

		g_total_vblank += g_mame_vblank.size();
		g_total_hblank += g_mame_hblank.size();

		if (g_failures >= 10)
		{
			break;
		}
	}

	bool cov_ok = true;
	if (!g_cov.vblank_line_1) { printf("FAIL: never vblank line 1\n"); cov_ok = false; }
	if (!g_cov.vblank_line_0) { printf("FAIL: never vblank line 0\n"); cov_ok = false; }
	if (!g_cov.vblank_write_1) { printf("FAIL: never vblank write 1\n"); cov_ok = false; }
	if (!g_cov.vblank_write_0) { printf("FAIL: never vblank write 0\n"); cov_ok = false; }
	if (!g_cov.hblank_1) { printf("FAIL: never hblank 1\n"); cov_ok = false; }
	if (!g_cov.hblank_0) { printf("FAIL: never hblank 0\n"); cov_ok = false; }
	if (!g_cov.write_7e2_lost) { printf("FAIL: never write 7e2 lost\n"); cov_ok = false; }
	if (!g_cov.write_7e2_kept) { printf("FAIL: never write 7e2 kept\n"); cov_ok = false; }
	if (!g_cov.write_030_low) { printf("FAIL: never write 030 low\n"); cov_ok = false; }
	for (int i = 0; i < NGP_K2GE_LINES; i++)
	{
		if (!g_cov.read_009[i]) { printf("FAIL: never read 009 line %d\n", i); cov_ok = false; }
	}
	for (int i = 0; i <= 128; i++)
	{
		if (!g_cov.read_008[i]) { printf("FAIL: never read 008 val %d\n", i); cov_ok = false; }
	}

	if (g_failures == 0 && cov_ok)
	{
		printf("all checks hold: %d cases, %d operations, %llu VBLANK calls, %llu HBLANK calls\n",
			cases, cases * ops_per_case, (unsigned long long)g_total_vblank, (unsigned long long)g_total_hblank);
		return 0;
	}

	printf("%d failures\n", g_failures);
	return 1;
}
