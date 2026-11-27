/**
 *    Name:        test_ngpsound.cpp
 *    Description: the check of ngpsound.cpp: a Z80 program in the shared RAM against what ngpsound.h says
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    26-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The program plays a tone on the T6W28, passes bytes through 8000 and raises and clears INT5; the test
 *    compares every effect, two runs' hashes, the samples a frame holds, and the TMP95C061's own writes to the
 *    T6W28 and the DACs. Built with ngpsound.cpp and the sound side's include folders (reference.cmake's
 *    ngp_sound_reference), it prints each failure and exits 0 only when all hold.
 */
#include "ngpsound.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int failures;
static uint8_t comm = 0x3C;
static int comm_writes, int5_raised, int5_cleared;

static uint8_t comm_read() { return comm; }
static void comm_write(uint8_t data) { comm = data; comm_writes++; }
static void int5(int state) { if (state) int5_raised++; else int5_cleared++; }

static void expect(bool condition, const char *what)
{
	if (!condition)
	{
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

static const uint8_t program[] =
{
	0xF3,                   // 0000 di
	0x31, 0x00, 0x0F,       // 0001 ld sp,0F00
	0x3E, 0x90,             // 0004 ld a,90       tone 0 at full volume
	0x32, 0x00, 0x40,       // 0006 ld (4000),a   right
	0x32, 0x01, 0x40,       // 0009 ld (4001),a   left
	0x3E, 0x8A,             // 000C ld a,8A       tone 0's period, low 4 bits
	0x32, 0x00, 0x40,       // 000E ld (4000),a
	0x32, 0x01, 0x40,       // 0011 ld (4001),a
	0x3E, 0x04,             // 0014 ld a,04       and its high 6 bits
	0x32, 0x00, 0x40,       // 0016 ld (4000),a
	0x32, 0x01, 0x40,       // 0019 ld (4001),a
	0x3A, 0x00, 0x80,       // 001C ld a,(8000)   the byte the TLCS-900/H passes
	0x32, 0x00, 0x01,       // 001F ld (0100),a
	0x3E, 0x55,             // 0022 ld a,55
	0x32, 0x00, 0x80,       // 0024 ld (8000),a
	0x32, 0x00, 0xC0,       // 0027 ld (C000),a   INT5 raised
	0xD3, 0x00,             // 002A out (00),a    INT5 cleared
	0x18, 0xEE,             // 002C jr 001C
};

struct run
{
	uint64_t samples_hash = 0;
	uint64_t state_hash = 0;
	std::vector<int16_t> mix;
};

static constexpr uint64_t kFrame = 515 * 199;   // a frame in master clocks

// One frame: the Z80 started at 'start' (never when 0), the frame closed at kFrame
static run one_frame(uint8_t *ram, uint64_t start)
{
	static const ns_host host = { comm_read, comm_write, int5 };
	std::memset(ram, 0xEE, 0x1000);
	std::memcpy(ram, program, sizeof(program));
	comm = 0x3C;
	comm_writes = int5_raised = int5_cleared = 0;
	ns_attach(0, ram, &host);
	ns_psg_enable(0, 1);
	ns_run_until(start ? start : 1000);
	expect(ns_z80_cycles() == 0, "the Z80 ran before it was started");
	if (start)
		ns_z80_start(start);
	ns_end_frame(kFrame);
	run result;
	result.samples_hash = ns_frame_samples_hash();
	result.state_hash = ns_state_hash();
	size_t count = 0;
	const int16_t *mix = ns_frame_mix(&count);
	if (count > 4000)
	{
		std::printf("FAIL: ns_frame_mix gave %zu stereo samples for a frame (3203 expected): a count of int16 values?\n", count);
		std::exit(1);
	}
	result.mix.assign(mix, mix + count * 2);
	return result;
}

int main()
{
	static uint8_t ram[0x1000];

	expect(ns_mix_rate() == 192000, "the mix rate should be 192000 Hz");

	// The Z80 started at 1000: it runs (kFrame - 1000) / 2 cycles, give or take an instruction
	run played = one_frame(ram, 1000);
	const uint64_t cycles = ns_z80_cycles();
	const uint64_t expected = (kFrame - 1000) / 2;
	if (cycles + 23 < expected || cycles > expected + 23)
	{
		std::printf("FAIL: the Z80 ran %llu cycles, expected about %llu\n", (unsigned long long)cycles, (unsigned long long)expected);
		failures++;
	}
	expect(ram[0x100] == 0x3C || ram[0x100] == 0x55, "the Z80 did not store the byte it read at 8000");
	expect(comm == 0x55 && comm_writes > 100, "the Z80's writes to 8000 did not reach the host");
	expect(int5_raised > 100 && int5_cleared > 100, "C000 and OUT did not raise and clear INT5");
	expect(ns_executed()[0] & 1, "address 0000 not marked as executed");
	expect(ns_executed()[0x2C >> 3] & (1 << (0x2C & 7)), "address 002C not marked as executed");
	expect(!(ns_executed()[0x40 >> 3] & (1 << (0x40 & 7))), "address 0040 marked as executed");
	expect(ns_z80_pc() >= 0x1C && ns_z80_pc() <= 0x2C, "the Z80's PC is not in its loop");

	// 3203 samples start before kFrame (one every 32 master clocks from time 0)
	if (played.mix.size() != 3203 * 2)
	{
		std::printf("FAIL: %zu mixed values, expected 3203 stereo samples (6406 values)\n", played.mix.size());
		failures++;
	}
	bool sound = false;
	for (int16_t value : played.mix)
		if (value != played.mix[0])
			sound = true;
	expect(sound, "the tone gave no sound");

	// The same run again gives the same hashes; with the Z80 never started, others
	run again = one_frame(ram, 1000);
	expect(again.samples_hash == played.samples_hash && again.state_hash == played.state_hash,
		"the same run gave other hashes");
	run silent = one_frame(ram, 0);
	expect(ns_z80_cycles() == 0, "the Z80 ran though it was never started");
	expect(ram[0x100] == 0xEE, "the stopped Z80 wrote the RAM");
	expect(silent.samples_hash != played.samples_hash, "silence and the tone gave the same samples' hash");
	expect(silent.state_hash != played.state_hash, "the Z80's state hash did not change with what it did");

	// Stopped again, it runs no more
	one_frame(ram, 1000);
	ns_z80_stop(kFrame + 100);
	const uint64_t stopped_at = ns_z80_cycles();
	ns_end_frame(2 * kFrame);
	expect(ns_z80_cycles() == stopped_at, "the Z80 ran after it was stopped");

	// The TLCS-900/H's own writes: the T6W28 from the main side, then a DAC on the left only
	static const ns_host host = { comm_read, comm_write, int5 };
	ns_attach(0, ram, &host);
	ns_psg_enable(0, 1);
	ns_psg_write(100, 0x90);
	ns_psg_write(100, 0x8A);
	ns_psg_write(100, 0x04);
	ns_end_frame(kFrame);
	size_t count = 0;
	const int16_t *mix = ns_frame_mix(&count);
	bool main_tone = false;
	for (size_t i = 0; i < count * 2; i++)
		if (mix[i] != mix[0])
			main_tone = true;
	expect(main_tone, "the main CPU's T6W28 writes gave no sound");

	ns_attach(0, ram, &host);
	ns_dac_write(kFrame / 2, 1, 0xFF);
	ns_end_frame(kFrame);
	mix = ns_frame_mix(&count);
	expect(count == 3203, "a frame of the DACs alone should have 3203 samples");
	if (count == 3203)
	{
		expect(mix[0] == mix[1], "left and right differ before the DAC write");
		expect(mix[3000 * 2] != mix[3000 * 2 + 1], "the left DAC did not move the left side only");
		expect(mix[3000 * 2] != mix[0], "the left DAC did not change the left side");
	}

	if (failures)
		std::printf("%d failures\n", failures);
	else
		std::printf("all checks hold\n");
	return failures ? 1 : 0;
}
