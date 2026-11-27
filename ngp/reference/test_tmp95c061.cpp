/**
 *    Name:        test_tmp95c061.cpp
 *    Description: the check of the runtime's TMP95C061 registers and devices (ngp/tmp95c061.h) against
 *                 MAME 0.289's TMP95C061 (tmp95c061.h), both driven with the same random operations at the
 *                 same times
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The test drives the runtime's chip (ngp/runtime/src/tmp95c061.c) and MAME's chip (through
 *    tmp95c061.h on the tlcs900 stub, built in zeroed memory) with the same writes, reads, input changes,
 *    runs (mostly an instruction's cycles, now and then up to 300000), DMA checks, interrupt checks (the
 *    host's part done as the header says: the pushes, the IFF, the vector) and writes of the core's DMA
 *    registers, prescaler and check flag. After every operation it compares the chips' registers (read with
 *    no side effects), the core's timer and interrupt state, the port write records, the analogue read
 *    counts and the two memories, and the micro DMA's bus reads as they happen; a case stops at its first
 *    difference. It counts what the operations reached and fails when one is never reached: each of the 14
 *    interrupts MAME raises, the NMI, each of the 16 DMA modes, a DMA count's end, an A/D end with ADMOD
 *    bit 4 clear and set, each 8-bit timer's request, TO1 and TO3 changed by a timer, INT0 by an edge and by
 *    a level. 300 cases of 3000 operations by default (--cases N).
 */

#include "emu.h"
#include "tmp95c061.h"
#include "ngp/tmp95c061.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>
#include <vector>

namespace
{

/* MAME's line states, as the stub's emu.h defines them */
constexpr int LINE_ASSERT = 1;
constexpr int LINE_CLEAR = 0;

/* The memory the bus sees, 64 KB */
constexpr uint32_t MEM_SIZE = 0x10000;

/* The port write record: the port as ngp/tmp95c061.h numbers it and the data */
struct port_record
{
	unsigned port;
	uint8_t data;
};

/* The test's CPU: MAME's TMP95C061 with public access to the members and the protected virtuals the test
   drives directly */
class test_cpu : public tmp95c061_device
{
public:
	explicit test_cpu(const machine_config &config)
		: tmp95c061_device(config, "maincpu", nullptr, 6144000)
	{
	}

	/* The core's members the test reads or sets */
	uint32_t &timer_pre() { return m_timer_pre; }
	uint8_t *timer_8() { return m_timer_8; }
	int *timer_change() { return m_timer_change; }
	int &ad_cycles_left() { return m_ad_cycles_left; }
	int &nmi_state() { return m_nmi_state; }
	int *level() { return m_level; }
	int &check_irqs_flag() { return m_check_irqs; }
	PAIR &dmas(unsigned c) { return m_dmas[c]; }
	PAIR &dmad(unsigned c) { return m_dmad[c]; }
	PAIR &dmac(unsigned c) { return m_dmac[c]; }
	PAIR &dmam(unsigned c) { return m_dmam[c]; }
	PAIR &sr() { return m_sr; }
	PAIR &pc() { return m_pc; }
	PAIR &xssp() { return m_xssp; }
	int &cycles() { return m_cycles; }
	int &halted() { return m_halted; }

	/* The protected virtuals the test calls, as the core would */
	void handle_ad() { tlcs900_handle_ad(); }
	void handle_timers() { tlcs900_handle_timers(); }
	void check_hdma() { tlcs900_check_hdma(); }
	void check_irqs() { tlcs900_check_irqs(); }
	void set_input_line(int line, int state) { execute_set_input(line, state); }
};

/* The bus's memory, one per side */
uint8_t mem_a[MEM_SIZE];
uint8_t mem_b[MEM_SIZE];

/* MAME's side: the internal map's entries, 0x80 of them */
const address_map::entry *g_internal_a[0x80];

/* The port write records, one per side */
std::vector<port_record> g_port_writes_a;
std::vector<port_record> g_port_writes_b;

/* The analogue read counts, one per side */
unsigned g_an_reads_a[4] = { 0, 0, 0, 0 };
unsigned g_an_reads_b[4] = { 0, 0, 0, 0 };

/* The runtime's chip and its host hooks */
ngp_tmp95c061 g_rt;
ngp_tmp95c061_host g_rt_host;

/* The coverage, counted over all cases */
struct coverage
{
	uint64_t vector[22] = {};      /* each of the 22 vectors of MAME's table taken, in the table's order */
	uint64_t nmi = 0;
	uint64_t dma_mode[16] = {};    /* each of the 16 DMA modes of MAME's switch moving data */
	uint64_t dma_end = 0;          /* a DMA count reaching 0 */
	uint64_t ad_end_fixed = 0;     /* an A/D end with ADMOD bit 4 clear (one channel) */
	uint64_t ad_end_sweep = 0;     /* an A/D end with ADMOD bit 4 set (the channels swept) */
	uint64_t timer_match[4] = {};  /* a timer's request raised by a run */
	uint64_t to1_changed = 0;      /* TO1 changed by a run (a timer) */
	uint64_t to3_changed = 0;      /* TO3 changed by a run */
	uint64_t int0_edge = 0;        /* INT0's request raised with IIMC's edge mode */
	uint64_t int0_level = 0;       /* INT0's request raised with IIMC's level mode */
};
coverage g_cov;

/* MAME's interrupt table's vectors and its DMA switch's modes, in their order */
const uint8_t k_vectors[22] = { 0x80, 0x7c, 0x78, 0x74, 0x70, 0x6c, 0x68, 0x64, 0x60, 0x5c, 0x58,
	0x54, 0x50, 0x4c, 0x48, 0x44, 0x40, 0x38, 0x34, 0x30, 0x2c, 0x28 };
const uint8_t k_dma_modes[16] = { 0x00, 0x01, 0x02, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0a, 0x0c, 0x0d, 0x0e,
	0x10, 0x11, 0x12, 0x14 };

int index_of(const uint8_t *list, int count, unsigned value)
{
	for (int i = 0; i < count; i++)
		if (list[i] == value)
			return i;
	return -1;
}

int g_case = 0;
int g_op = 0;
int g_failures = 0;

/* While the micro DMAs run, the addresses each side's bus reads, in order: a read can have side effects (an
   internal register's) and MAME's reads are part of what it does (its modes 0C and 10 read 16 bits to write 8) */
bool g_record_reads = false;
std::vector<uint32_t> g_reads_a, g_reads_b;

/* The bus for MAME's side: an address below 0x80 goes to the internal map, the rest to memory A */
uint8_t bus_a_r8(offs_t address)
{
	if (g_record_reads)
		g_reads_a.push_back(uint32_t(address));
	if (address < 0x80)
	{
		const address_map::entry *entry = g_internal_a[address];
		return entry && entry->read ? entry->read(address - entry->start) : 0;
	}
	return mem_a[address & 0xffff];
}

void bus_a_w8(offs_t address, uint8_t data)
{
	if (address < 0x80)
	{
		const address_map::entry *entry = g_internal_a[address];
		if (entry && entry->write)
			entry->write(address - entry->start, data);
	}
	else
	{
		mem_a[address & 0xffff] = data;
	}
}

uint16_t bus_a_r16(offs_t address)
{
	const uint8_t low = bus_a_r8(address);
	return uint16_t(low | (bus_a_r8(address + 1) << 8));
}

uint32_t bus_a_r32(offs_t address)
{
	const uint16_t low = bus_a_r16(address);
	return uint32_t(low) | (uint32_t(bus_a_r16(address + 2)) << 16);
}

void bus_a_w16(offs_t address, uint16_t data)
{
	bus_a_w8(address, uint8_t(data));
	bus_a_w8(address + 1, uint8_t(data >> 8));
}

void bus_a_w32(offs_t address, uint32_t data)
{
	bus_a_w16(address, uint16_t(data));
	bus_a_w16(address + 2, uint16_t(data >> 16));
}

/* The bus for the runtime's side: an address below 0x80 goes to ngp_tmp95c061_read/write, the rest to
   memory B */
uint8_t bus_b_r8(uint32_t address)
{
	if (g_record_reads)
		g_reads_b.push_back(address);
	if (address < 0x80)
	{
		return ngp_tmp95c061_read(&g_rt, address);
	}
	return mem_b[address & 0xffff];
}

void bus_b_w8(uint32_t address, uint8_t data)
{
	if (address < 0x80)
	{
		ngp_tmp95c061_write(&g_rt, address, data);
	}
	else
	{
		mem_b[address & 0xffff] = data;
	}
}

uint16_t bus_b_r16(uint32_t address)
{
	const uint8_t low = bus_b_r8(address);
	return uint16_t(low | (bus_b_r8(address + 1) << 8));
}

uint32_t bus_b_r32(uint32_t address)
{
	const uint16_t low = bus_b_r16(address);
	return uint32_t(low) | (uint32_t(bus_b_r16(address + 2)) << 16);
}

void bus_b_w16(uint32_t address, uint16_t data)
{
	bus_b_w8(address, uint8_t(data));
	bus_b_w8(address + 1, uint8_t(data >> 8));
}

void bus_b_w32(uint32_t address, uint32_t data)
{
	bus_b_w16(address, uint16_t(data));
	bus_b_w16(address + 2, uint16_t(data >> 16));
}

/* MAME's port write hook: record the port and data */
void mame_port_write(unsigned port, uint8_t data)
{
	g_port_writes_a.push_back({ port, data });
}

/* MAME's port read hook: 0x40 plus the port's number */
uint8_t mame_port_read(unsigned port)
{
	return uint8_t(0x40 + port);
}

/* MAME's analogue read hook: (0x123 * (calls so far + 1) + 0x55 * channel) & 0x3ff */
uint16_t mame_an_read(unsigned channel)
{
	g_an_reads_a[channel]++;
	return uint16_t((0x123 * (g_an_reads_a[channel] + 1) + 0x55 * channel) & 0x3ff);
}

/* The runtime's port write hook: record the port and data */
void rt_port_write(unsigned port, uint8_t data)
{
	g_port_writes_b.push_back({ port, data });
}

/* The runtime's port read hook: 0x40 plus the port's number, as MAME's; ports 2 and 6 have no read callback in
   MAME's (port 9 has no write one): unbound, they read 0 */
uint8_t rt_port_read(unsigned port)
{
	if (port == NGP_TMP95C061_PORT_2 || port == NGP_TMP95C061_PORT_6)
		return 0;
	return uint8_t(0x40 + port);
}

/* The runtime's analogue read hook: (0x123 * (calls so far + 1) + 0x55 * channel) & 0x3ff */
uint16_t rt_an_read(unsigned channel)
{
	g_an_reads_b[channel]++;
	return uint16_t((0x123 * (g_an_reads_b[channel] + 1) + 0x55 * channel) & 0x3ff);
}

/* Fill the two memories with the same random bytes */
void fill_memories(std::mt19937 &rng)
{
	std::uniform_int_distribution<int> dist(0, 255);
	for (uint32_t i = 0; i < MEM_SIZE; i++)
	{
		uint8_t byte = uint8_t(dist(rng));
		mem_a[i] = byte;
		mem_b[i] = byte;
	}
}

/* Compare the two memories; a difference ends the test */
bool compare_memories()
{
	if (memcmp(mem_a, mem_b, MEM_SIZE) != 0)
	{
		for (uint32_t i = 0; i < MEM_SIZE; i++)
		{
			if (mem_a[i] != mem_b[i])
			{
				printf("FAIL: case %d op %d: memory mismatch at %04X (mame: %02X, rt: %02X)\n",
					g_case, g_op, i, mem_a[i], mem_b[i]);
				g_failures++;
				return false;
			}
		}
	}
	return true;
}

/* Compare the port write records; a difference ends the test. The records are cleared after the
   comparison */
bool compare_port_writes()
{
	if (g_port_writes_a.size() != g_port_writes_b.size())
	{
		printf("FAIL: case %d op %d: port write count mismatch (mame: %zu, rt: %zu)\n",
			g_case, g_op, g_port_writes_a.size(), g_port_writes_b.size());
		g_failures++;
		return false;
	}
	for (size_t i = 0; i < g_port_writes_a.size(); i++)
	{
		if (g_port_writes_a[i].port != g_port_writes_b[i].port ||
			g_port_writes_a[i].data != g_port_writes_b[i].data)
		{
			printf("FAIL: case %d op %d: port write mismatch at %zu (mame: port %u data %02X, rt: port %u data %02X)\n",
				g_case, g_op, i,
				g_port_writes_a[i].port, g_port_writes_a[i].data,
				g_port_writes_b[i].port, g_port_writes_b[i].data);
			g_failures++;
			return false;
		}
	}
	g_port_writes_a.clear();
	g_port_writes_b.clear();
	return true;
}

/* Compare the analogue read counts; a difference ends the test */
bool compare_an_reads()
{
	for (unsigned c = 0; c < 4; c++)
	{
		if (g_an_reads_a[c] != g_an_reads_b[c])
		{
			printf("FAIL: case %d op %d: an_read count mismatch channel %u (mame: %u, rt: %u)\n",
				g_case, g_op, c, g_an_reads_a[c], g_an_reads_b[c]);
			g_failures++;
			return false;
		}
	}
	return true;
}

/* Compare the registers: every offset 00-7F but 60-67 read on MAME's side with no side effects against
   ngp_tmp95c061_peek. A difference ends the test */
bool compare_registers(test_cpu &cpu)
{
	g_machine.m_side_effects_disabled = true;
	for (unsigned offset = 0; offset < 0x80; offset++)
	{
		if (offset >= 0x60 && offset <= 0x67)
			continue;

		uint8_t mame_val = 0;
		const address_map::entry *entry = g_internal_a[offset];
		if (entry && entry->read)
			mame_val = entry->read(offset - entry->start);

		uint8_t rt_val = ngp_tmp95c061_peek(&g_rt, offset);

		if (mame_val != rt_val)
		{
			printf("FAIL: case %d op %d: register %02X mismatch (mame: %02X, rt: %02X)\n",
				g_case, g_op, offset, mame_val, rt_val);
			g_failures++;
			g_machine.m_side_effects_disabled = false;
			return false;
		}
	}
	g_machine.m_side_effects_disabled = false;
	return true;
}

/* Compare the core's timer and interrupt state. A difference ends the test */
bool compare_core_state(test_cpu &cpu)
{
	if (cpu.timer_pre() != g_rt.timer_pre)
	{
		printf("FAIL: case %d op %d: timer_pre mismatch (mame: %08X, rt: %08X)\n",
			g_case, g_op, cpu.timer_pre(), g_rt.timer_pre);
		g_failures++;
		return false;
	}
	for (unsigned i = 0; i < 6; i++)
	{
		if (cpu.timer_8()[i] != g_rt.timer_8[i])
		{
			printf("FAIL: case %d op %d: timer_8[%u] mismatch (mame: %02X, rt: %02X)\n",
				g_case, g_op, i, cpu.timer_8()[i], g_rt.timer_8[i]);
			g_failures++;
			return false;
		}
	}
	for (unsigned i = 0; i < 4; i++)
	{
		if (cpu.timer_change()[i] != g_rt.timer_change[i])
		{
			printf("FAIL: case %d op %d: timer_change[%u] mismatch (mame: %d, rt: %d)\n",
				g_case, g_op, i, cpu.timer_change()[i], g_rt.timer_change[i]);
			g_failures++;
			return false;
		}
	}
	if (cpu.ad_cycles_left() != g_rt.ad_cycles_left)
	{
		printf("FAIL: case %d op %d: ad_cycles_left mismatch (mame: %d, rt: %d)\n",
			g_case, g_op, cpu.ad_cycles_left(), g_rt.ad_cycles_left);
		g_failures++;
		return false;
	}
	if (cpu.nmi_state() != g_rt.nmi_state)
	{
		printf("FAIL: case %d op %d: nmi_state mismatch (mame: %u, rt: %u)\n",
			g_case, g_op, cpu.nmi_state(), g_rt.nmi_state);
		g_failures++;
		return false;
	}
	/* The five lines: NMI, INT0, INT4, INT5, TIO */
	const int mame_lines[] = { TLCS900_NMI, TLCS900_INT0, TLCS900_INT4, TLCS900_INT5, TLCS900_TIO };
	const int rt_lines[] = { NGP_TMP95C061_NMI, NGP_TMP95C061_INT0, NGP_TMP95C061_INT4,
		NGP_TMP95C061_INT5, NGP_TMP95C061_TIO };
	for (unsigned i = 0; i < 5; i++)
	{
		int mame_level = cpu.level()[mame_lines[i]] == LINE_ASSERT ? 1 : 0;
		int rt_level = g_rt.level[rt_lines[i]];
		if (mame_level != rt_level)
		{
			printf("FAIL: case %d op %d: level[%u] mismatch (mame: %d, rt: %d)\n",
				g_case, g_op, i, mame_level, rt_level);
			g_failures++;
			return false;
		}
	}
	if ((cpu.check_irqs_flag() != 0) != (g_rt.check_irqs != 0))
	{
		printf("FAIL: case %d op %d: check_irqs mismatch (mame: %d, rt: %d)\n",
			g_case, g_op, cpu.check_irqs_flag() != 0, g_rt.check_irqs);
		g_failures++;
		return false;
	}
	for (unsigned c = 0; c < 4; c++)
	{
		if (cpu.dmas(c).d != g_rt.dmas[c])
		{
			printf("FAIL: case %d op %d: dmas[%u] mismatch (mame: %08X, rt: %08X)\n",
				g_case, g_op, c, cpu.dmas(c).d, g_rt.dmas[c]);
			g_failures++;
			return false;
		}
		if (cpu.dmad(c).d != g_rt.dmad[c])
		{
			printf("FAIL: case %d op %d: dmad[%u] mismatch (mame: %08X, rt: %08X)\n",
				g_case, g_op, c, cpu.dmad(c).d, g_rt.dmad[c]);
			g_failures++;
			return false;
		}
		if (cpu.dmac(c).w.l != g_rt.dmac[c])
		{
			printf("FAIL: case %d op %d: dmac[%u] mismatch (mame: %04X, rt: %04X)\n",
				g_case, g_op, c, cpu.dmac(c).w.l, g_rt.dmac[c]);
			g_failures++;
			return false;
		}
		if (cpu.dmam(c).b.l != g_rt.dmam[c])
		{
			printf("FAIL: case %d op %d: dmam[%u] mismatch (mame: %02X, rt: %02X)\n",
				g_case, g_op, c, cpu.dmam(c).b.l, g_rt.dmam[c]);
			g_failures++;
			return false;
		}
	}
	return true;
}

/* Do the full comparison after an operation; a difference ends the test */
bool compare_all(test_cpu &cpu)
{
	if (!compare_registers(cpu))
		return false;
	if (!compare_core_state(cpu))
		return false;
	if (!compare_port_writes())
		return false;
	if (!compare_an_reads())
		return false;
	if (!compare_memories())
		return false;
	return true;
}

/* Build MAME's CPU for a case: zero the buffer, placement new, set up the bus, ports, start, reset */
test_cpu *build_mame_cpu(std::mt19937 &rng)
{
	alignas(test_cpu) static unsigned char buffer[sizeof(test_cpu)];
	memset(buffer, 0, sizeof(buffer));

	test_cpu *cpu = new (buffer) test_cpu(machine_config());

	cpu->set_am8_16(1);
	cpu->space(AS_PROGRAM).r8 = bus_a_r8;
	cpu->space(AS_PROGRAM).r16 = bus_a_r16;
	cpu->space(AS_PROGRAM).r32 = bus_a_r32;
	cpu->space(AS_PROGRAM).w8 = bus_a_w8;
	cpu->space(AS_PROGRAM).w16 = bus_a_w16;
	cpu->space(AS_PROGRAM).w32 = bus_a_w32;
	cpu->space(AS_PROGRAM).width = 8;

	cpu->port1_write().set([](offs_t, uint8_t data) { mame_port_write(NGP_TMP95C061_PORT_1, data); });
	cpu->port2_write().set([](offs_t, uint8_t data) { mame_port_write(NGP_TMP95C061_PORT_2, data); });
	cpu->port5_write().set([](offs_t, uint8_t data) { mame_port_write(NGP_TMP95C061_PORT_5, data); });
	cpu->port6_write().set([](offs_t, uint8_t data) { mame_port_write(NGP_TMP95C061_PORT_6, data); });
	cpu->port7_write().set([](offs_t, uint8_t data) { mame_port_write(NGP_TMP95C061_PORT_7, data); });
	cpu->port8_write().set([](offs_t, uint8_t data) { mame_port_write(NGP_TMP95C061_PORT_8, data); });
	cpu->porta_write().set([](offs_t, uint8_t data) { mame_port_write(NGP_TMP95C061_PORT_A, data); });
	cpu->portb_write().set([](offs_t, uint8_t data) { mame_port_write(NGP_TMP95C061_PORT_B, data); });

	cpu->port1_read().set([](offs_t) { return mame_port_read(NGP_TMP95C061_PORT_1); });
	cpu->port5_read().set([](offs_t) { return mame_port_read(NGP_TMP95C061_PORT_5); });
	cpu->port7_read().set([](offs_t) { return mame_port_read(NGP_TMP95C061_PORT_7); });
	cpu->port8_read().set([](offs_t) { return mame_port_read(NGP_TMP95C061_PORT_8); });
	cpu->port9_read().set([](offs_t) { return mame_port_read(NGP_TMP95C061_PORT_9); });
	cpu->porta_read().set([](offs_t) { return mame_port_read(NGP_TMP95C061_PORT_A); });
	cpu->portb_read().set([](offs_t) { return mame_port_read(NGP_TMP95C061_PORT_B); });

	cpu->an_read<0>().set([]() { return mame_an_read(0); });
	cpu->an_read<1>().set([]() { return mame_an_read(1); });
	cpu->an_read<2>().set([]() { return mame_an_read(2); });
	cpu->an_read<3>().set([]() { return mame_an_read(3); });

	cpu->start();

	for (const address_map::entry &entry : cpu->internal_map().m_entries)
	{
		for (offs_t address = entry.start; address <= entry.end && address < 0x80; address++)
			g_internal_a[address] = &entry;
	}

	cpu->reset();

	return cpu;
}

/* Set up the runtime's host hooks, init the chip and reset it, as MAME's is */
void setup_runtime()
{
	g_rt_host.port_write = rt_port_write;
	g_rt_host.port_read = rt_port_read;
	g_rt_host.an_read = rt_an_read;
	g_rt_host.read8 = bus_b_r8;
	g_rt_host.read16 = bus_b_r16;
	g_rt_host.read32 = bus_b_r32;
	g_rt_host.write8 = bus_b_w8;
	g_rt_host.write16 = bus_b_w16;
	g_rt_host.write32 = bus_b_w32;

	ngp_tmp95c061_init(&g_rt, &g_rt_host);
	ngp_tmp95c061_reset(&g_rt);
}

/* The list of offsets that half of the time a write chooses from */
const unsigned write_offsets[] = {
	0x1E, 0x20, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
	0x2C, 0x2D, 0x2E, 0x50, 0x54, 0x6D,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
	0x7B, 0x7C, 0x7D, 0x7E, 0x7F
};
constexpr size_t WRITE_OFFSETS_COUNT = sizeof(write_offsets) / sizeof(write_offsets[0]);

/* Operation 1: a write */
void op_write(test_cpu &cpu, std::mt19937 &rng)
{
	std::uniform_int_distribution<int> offset_dist(0, 0x7F);
	std::uniform_int_distribution<int> data_dist(0, 255);
	std::uniform_int_distribution<int> choice_dist(0, 1);

	unsigned offset;
	uint8_t data;

	if (choice_dist(rng) == 0)
	{
		std::uniform_int_distribution<int> idx_dist(0, WRITE_OFFSETS_COUNT - 1);
		offset = write_offsets[idx_dist(rng)];
		data = uint8_t(data_dist(rng));
	}
	else
	{
		offset = unsigned(offset_dist(rng));
		data = uint8_t(data_dist(rng));
	}

	/* To reach the interrupts, a write to 70-7A often holds a priority 1-6 in bits 0-2 and 4-6 and the
	   request bits as they are */
	if (offset >= 0x70 && offset <= 0x7A)
	{
		if (choice_dist(rng) == 0)
		{
			std::uniform_int_distribution<int> prio_dist(1, 6);
			int prio = prio_dist(rng);
			/* Read the current value to keep the request bits */
			g_machine.m_side_effects_disabled = true;
			uint8_t current = 0;
			const address_map::entry *entry = g_internal_a[offset];
			if (entry && entry->read)
				current = entry->read(offset - entry->start);
			g_machine.m_side_effects_disabled = false;

			data = uint8_t((current & 0x88) | (prio << 4) | prio);
		}
	}

	/* A write to 7C-7F often names a valid vector ((v & 0x1f) << 2 in 0x28-0x70, not 0x3c) */
	if (offset >= 0x7C && offset <= 0x7F)
	{
		if (choice_dist(rng) == 0)
		{
			std::uniform_int_distribution<int> vec_dist(0x28, 0x70);
			int vec = vec_dist(rng) & ~3;
			if (vec == 0x3C)
				vec = 0x40;
			data = uint8_t((vec >> 2) | (data & 0xe0));     /* the register holds the vector / 4 */
		}
	}

	cpu.space(AS_PROGRAM).w8(offset, data);
	ngp_tmp95c061_write(&g_rt, offset, data);
}

/* Operation 2: a read */
void op_read(test_cpu &cpu, std::mt19937 &rng)
{
	std::uniform_int_distribution<int> offset_dist(0, 0x7F);
	unsigned offset = unsigned(offset_dist(rng));

	uint8_t mame_val = cpu.space(AS_PROGRAM).r8(offset);
	uint8_t rt_val = ngp_tmp95c061_read(&g_rt, offset);

	if (mame_val != rt_val)
	{
		printf("FAIL: case %d op %d: read %02X mismatch (mame: %02X, rt: %02X)\n",
			g_case, g_op, offset, mame_val, rt_val);
		g_failures++;
	}
}

/* Operation 3: an input change */
void op_input(test_cpu &cpu, std::mt19937 &rng)
{
	std::uniform_int_distribution<int> line_dist(0, 4);
	std::uniform_int_distribution<int> state_dist(0, 1);

	int line_idx = line_dist(rng);
	int state = state_dist(rng);

	/* MAME's lines */
	const int mame_lines[] = { TLCS900_NMI, TLCS900_INT0, TLCS900_INT4, TLCS900_INT5, TLCS900_TIO };
	int mame_line = mame_lines[line_idx];

	/* The runtime's lines */
	const int rt_lines[] = { NGP_TMP95C061_NMI, NGP_TMP95C061_INT0, NGP_TMP95C061_INT4,
		NGP_TMP95C061_INT5, NGP_TMP95C061_TIO };
	int rt_line = rt_lines[line_idx];

	const uint8_t int0_before = g_rt.int_reg[0] & 0x08;
	cpu.halted() = 1;
	cpu.set_input_line(mame_line, state ? LINE_ASSERT : LINE_CLEAR);
	int mame_halted = cpu.halted();

	int rt_result = ngp_tmp95c061_set_input(&g_rt, rt_line, state);

	if (rt_result != (mame_halted == 0 ? 1 : 0))
	{
		printf("FAIL: case %d op %d: input line %d state %d mismatch (mame halted: %d, rt result: %d)\n",
			g_case, g_op, line_idx, state, mame_halted, rt_result);
		g_failures++;
	}

	/* Coverage: INT0's request raised, in IIMC's edge mode or its level mode */
	if (line_idx == 1 && !int0_before && (g_rt.int_reg[0] & 0x08))
	{
		if (g_rt.iimc & 0x02)
			g_cov.int0_edge++;
		else
			g_cov.int0_level++;
	}
}

/* Operation 4: a run */
void op_run(test_cpu &cpu, std::mt19937 &rng)
{
	std::uniform_int_distribution<int> cycles_dist(1, 30);
	std::uniform_int_distribution<int> choice_dist(0, 19);

	/* Mostly an instruction's cycles; now and then a longer run, and rarely a very long one (T256 takes 32768
	   cycles a count) */
	int cycles;
	const int choice = choice_dist(rng);
	if (choice > 1)
	{
		cycles = cycles_dist(rng);
	}
	else if (choice == 1)
	{
		std::uniform_int_distribution<int> big_dist(31, 3000);
		cycles = big_dist(rng);
	}
	else
	{
		std::uniform_int_distribution<int> long_dist(3001, 300000);
		cycles = long_dist(rng);
	}

	const uint8_t to1 = g_rt.to1, to3 = g_rt.to3;
	const uint8_t requests[2] = { g_rt.int_reg[3], g_rt.int_reg[4] };
	const bool ad_ends = g_rt.ad_cycles_left > 0 && g_rt.ad_cycles_left <= cycles;
	const bool ad_sweep = (g_rt.ad_mode & 0x10) != 0;

	cpu.cycles() = cycles;
	cpu.handle_ad();
	cpu.handle_timers();

	ngp_tmp95c061_run(&g_rt, cycles);

	/* Coverage: the timers' requests raised (INTET10 bits 3 and 7, INTET32 bits 3 and 7), TO1 and TO3
	   changed, an A/D's end */
	for (int timer = 0; timer < 4; timer++)
	{
		const uint8_t flag = (timer & 1) ? 0x80 : 0x08;
		if (!(requests[timer >> 1] & flag) && (g_rt.int_reg[3 + (timer >> 1)] & flag))
			g_cov.timer_match[timer]++;
	}
	if (g_rt.to1 != to1)
		g_cov.to1_changed++;
	if (g_rt.to3 != to3)
		g_cov.to3_changed++;
	if (ad_ends)
		(ad_sweep ? g_cov.ad_end_sweep : g_cov.ad_end_fixed)++;
}

/* Operation 5: a DMA check */
void op_dma(test_cpu &cpu, std::mt19937 &rng)
{
	std::uniform_int_distribution<int> iff_dist(0, 7);
	std::uniform_int_distribution<int> choice_dist(0, 3);

	unsigned iff;
	if (choice_dist(rng) == 0)
	{
		iff = 7;
	}
	else
	{
		iff = unsigned(iff_dist(rng));
	}

	uint16_t counts[4];
	for (unsigned c = 0; c < 4; c++)
		counts[c] = g_rt.dmac[c];

	cpu.sr().w.l = uint16_t((iff << 12) | 0x8800);
	cpu.cycles() = 0;
	g_reads_a.clear();
	g_reads_b.clear();
	g_record_reads = true;
	cpu.check_hdma();
	int mame_cycles = cpu.cycles();

	int rt_cycles = ngp_tmp95c061_dma(&g_rt, iff);
	g_record_reads = false;

	if (mame_cycles != rt_cycles)
	{
		printf("FAIL: case %d op %d: dma cycles mismatch (mame: %d, rt: %d)\n",
			g_case, g_op, mame_cycles, rt_cycles);
		g_failures++;
	}
	if (g_reads_a != g_reads_b)
	{
		printf("FAIL: case %d op %d: the dma's bus reads differ (mame: %zu, rt: %zu)\n",
			g_case, g_op, g_reads_a.size(), g_reads_b.size());
		g_failures++;
	}

	/* Coverage: the channel that ran (its count moved), its mode when it is one of MAME's switch's, and the
	   count's end */
	for (unsigned c = 0; c < 4; c++)
	{
		if (g_rt.dmac[c] == counts[c])
			continue;
		const int mode = index_of(k_dma_modes, 16, g_rt.dmam[c] & 0x1f);
		if (mode >= 0 && rt_cycles > 0)
			g_cov.dma_mode[mode]++;
		if (g_rt.dmac[c] == 0)
			g_cov.dma_end++;
	}
}

/* Operation 6: an interrupt check */
void op_interrupt(test_cpu &cpu, std::mt19937 &rng)
{
	std::uniform_int_distribution<int> iff_dist(0, 7);
	std::uniform_int_distribution<int> pc_dist(0, 0xFFFF);
	std::uniform_int_distribution<int> xssp_dist(0x1000, 0xF000);

	unsigned iff = unsigned(iff_dist(rng));
	uint32_t pc = uint32_t(pc_dist(rng));
	uint32_t xssp = uint32_t(xssp_dist(rng) & ~1); /* even */

	cpu.sr().w.l = uint16_t((iff << 12) | 0x8800);
	cpu.pc().d = pc;
	cpu.xssp().d = xssp;
	cpu.cycles() = 0;
	cpu.halted() = 1;
	cpu.check_irqs();

	uint32_t mame_pc = cpu.pc().d;
	uint16_t mame_sr = cpu.sr().w.l;
	uint32_t mame_xssp = cpu.xssp().d;
	int mame_cycles = cpu.cycles();
	int mame_halted = cpu.halted();

	/* The runtime's side: what the host does */
	ngp_tmp95c061_irq irq;
	int taken = ngp_tmp95c061_pending(&g_rt, iff, &irq);

	uint32_t rt_pc = pc;
	uint16_t rt_sr = uint16_t((iff << 12) | 0x8800);
	uint32_t rt_xssp = xssp;
	int rt_cycles = 0;
	int rt_halted = 1;

	if (taken)
	{
		/* Push the PC (4 bytes), then the SR as it was (2 bytes), on XSSP; then the SR's new IFF */
		rt_xssp -= 4;
		bus_b_w32(rt_xssp, pc);
		rt_xssp -= 2;
		bus_b_w16(rt_xssp, rt_sr);
		rt_sr = uint16_t((rt_sr & 0x8FFF) | (irq.level == NGP_TMP95C061_KEEP_IFF ? (rt_sr & 0x7000) : (uint16_t(irq.level) << 12)));

		/* Read the PC from 0xFFFF00 + vector */
		rt_pc = bus_b_r32(0xFFFF00 + irq.vector);

		ngp_tmp95c061_acknowledge(&g_rt, &irq);
		rt_cycles = 18;
		rt_halted = 0;

		/* Coverage: the vector taken */
		if (irq.flag == 0)
			g_cov.nmi++;
		else if (index_of(k_vectors, 22, irq.vector) >= 0)
			g_cov.vector[index_of(k_vectors, 22, irq.vector)]++;
	}

	if (mame_pc != rt_pc)
	{
		printf("FAIL: case %d op %d: interrupt pc mismatch (mame: %08X, rt: %08X)\n",
			g_case, g_op, mame_pc, rt_pc);
		g_failures++;
	}
	if (mame_sr != rt_sr)
	{
		printf("FAIL: case %d op %d: interrupt sr mismatch (mame: %04X, rt: %04X)\n",
			g_case, g_op, mame_sr, rt_sr);
		g_failures++;
	}
	if (mame_xssp != rt_xssp)
	{
		printf("FAIL: case %d op %d: interrupt xssp mismatch (mame: %08X, rt: %08X)\n",
			g_case, g_op, mame_xssp, rt_xssp);
		g_failures++;
	}
	if (mame_cycles != rt_cycles)
	{
		printf("FAIL: case %d op %d: interrupt cycles mismatch (mame: %d, rt: %d)\n",
			g_case, g_op, mame_cycles, rt_cycles);
		g_failures++;
	}
	if (mame_halted != rt_halted)
	{
		printf("FAIL: case %d op %d: interrupt halted mismatch (mame: %d, rt: %d)\n",
			g_case, g_op, mame_halted, rt_halted);
		g_failures++;
	}
}

/* Operation 7: DMA registers */
void op_dma_registers(test_cpu &cpu, std::mt19937 &rng)
{
	std::uniform_int_distribution<int> channel_dist(0, 3);
	std::uniform_int_distribution<int> addr_dist(0x0080, 0xFFF0);
	std::uniform_int_distribution<int> count_dist(1, 4);
	std::uniform_int_distribution<int> mode_dist(0, 15);
	std::uniform_int_distribution<int> choice_dist(0, 1);

	unsigned c = unsigned(channel_dist(rng));
	uint32_t dmas = uint32_t(addr_dist(rng) & ~3);
	uint32_t dmad = uint32_t(addr_dist(rng) & ~3);
	uint16_t dmac = uint16_t(count_dist(rng));
	uint8_t dmam;

	if (choice_dist(rng) == 0)
	{
		const uint8_t modes[] = { 0x00, 0x01, 0x02, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0A, 0x0C, 0x0D, 0x0E, 0x10, 0x11, 0x12, 0x14 };
		std::uniform_int_distribution<int> idx_dist(0, 15);
		dmam = modes[idx_dist(rng)];
	}
	else
	{
		dmam = uint8_t(mode_dist(rng));
	}

	cpu.dmas(c).d = dmas;
	cpu.dmad(c).d = dmad;
	cpu.dmac(c).w.l = dmac;
	cpu.dmam(c).b.l = dmam;

	g_rt.dmas[c] = dmas;
	g_rt.dmad[c] = dmad;
	g_rt.dmac[c] = dmac;
	g_rt.dmam[c] = dmam;
}

/* Operation 8: clear check_irqs */
void op_clear(test_cpu &cpu)
{
	cpu.check_irqs_flag() = 0;
	g_rt.check_irqs = 0;
}

/* Operation 9: prescaler */
void op_prescaler(test_cpu &cpu, std::mt19937 &rng)
{
	std::uniform_int_distribution<int> pre_dist(0xFFF000, 0xFFFFFF);
	uint32_t pre = uint32_t(pre_dist(rng));

	cpu.timer_pre() = pre;
	g_rt.timer_pre = pre;
}

/* Operation 10: reset */
void op_reset(test_cpu &cpu)
{
	cpu.reset();
	ngp_tmp95c061_reset(&g_rt);
}

/* Choose and do an operation */
void do_operation(test_cpu &cpu, std::mt19937 &rng)
{
	/* Weighted, per thousand: writes and runs the most (the timers need TRUN to stay set a while), a reset
	   rarely (it stops every timer) */
	static std::discrete_distribution<int> op_dist({ 260, 100, 150, 250, 70, 80, 40, 30, 19, 1 });
	int op = op_dist(rng);

	switch (op)
	{
	case 0: op_write(cpu, rng); break;
	case 1: op_read(cpu, rng); break;
	case 2: op_input(cpu, rng); break;
	case 3: op_run(cpu, rng); break;
	case 4: op_dma(cpu, rng); break;
	case 5: op_interrupt(cpu, rng); break;
	case 6: op_dma_registers(cpu, rng); break;
	case 7: op_clear(cpu); break;
	case 8: op_prescaler(cpu, rng); break;
	case 9: op_reset(cpu); break;
	}
}

/* Check the coverage; a missing item ends the test */
bool check_coverage()
{
	bool ok = true;
	uint64_t vectors = 0, modes = 0, matches = 0;
	for (uint64_t count : g_cov.vector)
		vectors += count;
	for (uint64_t count : g_cov.dma_mode)
		modes += count;
	for (uint64_t count : g_cov.timer_match)
		matches += count;
	printf("covered: %llu interrupts taken, %llu NMIs, %llu DMA moves (%llu counts ended), %llu A/D ends "
		"(%llu swept), %llu timers' requests, TO1 changed %llu times, TO3 %llu, INT0 by edge %llu, by level %llu\n",
		(unsigned long long)vectors, (unsigned long long)g_cov.nmi, (unsigned long long)modes,
		(unsigned long long)g_cov.dma_end, (unsigned long long)(g_cov.ad_end_fixed + g_cov.ad_end_sweep),
		(unsigned long long)g_cov.ad_end_sweep, (unsigned long long)matches, (unsigned long long)g_cov.to1_changed,
		(unsigned long long)g_cov.to3_changed, (unsigned long long)g_cov.int0_edge, (unsigned long long)g_cov.int0_level);

	/* MAME raises 14 of the 22 requests: not the serial channels' receptions (68, 60), the 16-bit timers' (5C,
	   58, 54, 50: they are not run) nor INT6 and INT7 (38, 34: no input line), and a write of an interrupt
	   register keeps a request bit or clears it, never sets it */
	static const uint8_t never_raised[] = { 0x68, 0x60, 0x5c, 0x58, 0x54, 0x50, 0x38, 0x34 };
	for (int i = 0; i < 22; i++)
	{
		if (!g_cov.vector[i] && !std::count(std::begin(never_raised), std::end(never_raised), k_vectors[i]))
		{
			printf("FAIL: never vector %02X\n", k_vectors[i]);
			ok = false;
		}
	}
	if (!g_cov.nmi)
	{
		printf("FAIL: never NMI taken\n");
		ok = false;
	}
	for (int i = 0; i < 16; i++)
	{
		if (!g_cov.dma_mode[i])
		{
			printf("FAIL: never DMA mode %02X moving data\n", k_dma_modes[i]);
			ok = false;
		}
	}
	if (!g_cov.dma_end)
	{
		printf("FAIL: never DMA count end\n");
		ok = false;
	}
	if (!g_cov.ad_end_fixed)
	{
		printf("FAIL: never A/D end with ADMOD bit 4 clear\n");
		ok = false;
	}
	if (!g_cov.ad_end_sweep)
	{
		printf("FAIL: never A/D end with ADMOD bit 4 set\n");
		ok = false;
	}
	for (int i = 0; i < 4; i++)
	{
		if (!g_cov.timer_match[i])
		{
			printf("FAIL: never timer %d's request raised by a run\n", i);
			ok = false;
		}
	}
	if (!g_cov.to1_changed)
	{
		printf("FAIL: never TO1 changed by a timer\n");
		ok = false;
	}
	if (!g_cov.to3_changed)
	{
		printf("FAIL: never TO3 changed by a timer\n");
		ok = false;
	}
	if (!g_cov.int0_edge)
	{
		printf("FAIL: never INT0 taken by an edge\n");
		ok = false;
	}
	if (!g_cov.int0_level)
	{
		printf("FAIL: never INT0 taken by a level\n");
		ok = false;
	}

	return ok;
}

} /* anonymous namespace */

int main(int argc, char **argv)
{
	int cases = 300;
	if (argc > 1 && std::string(argv[1]) == "--cases" && argc > 2)
	{
		cases = std::atoi(argv[2]);
	}

	const int ops_per_case = 3000;

	for (int c = 0; c < cases; c++)
	{
		g_case = c;
		g_port_writes_a.clear();
		g_port_writes_b.clear();
		memset(g_an_reads_a, 0, sizeof(g_an_reads_a));
		memset(g_an_reads_b, 0, sizeof(g_an_reads_b));

		std::mt19937 rng(c);
		fill_memories(rng);

		test_cpu *cpu = build_mame_cpu(rng);
		setup_runtime();

		for (int o = 0; o < ops_per_case; o++)
		{
			g_op = o;

			do_operation(*cpu, rng);

			if (!compare_all(*cpu))
			{
				break;
			}

			if (g_failures >= 10)
			{
				break;
			}
		}

		cpu->~test_cpu();

		if (g_failures >= 10)
		{
			break;
		}
	}

	bool cov_ok = check_coverage();

	if (g_failures == 0 && cov_ok)
	{
		printf("all checks hold: %d cases, %d operations\n", cases, cases * ops_per_case);
		return 0;
	}

	printf("%d failures\n", g_failures);
	return 1;
}
