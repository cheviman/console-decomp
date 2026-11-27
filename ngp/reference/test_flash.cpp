/**
 *    Name:        test_flash.cpp
 *    Description: the check of the board's flash (ngp/machine.h) against MAME's flash_w
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    This test drives both the runtime board's flash (ngp_write8 into the cartridge's space) and MAME's
 *    flash_w with the same sequence of operations: command sequences (5555 AA 2AAA 55 ...), programs
 *    (auto-program at a picked offset), block erases (the 0x30 after the 80 command), ID reads (90), and
 *    block protects (9A 9A). After every write it compares the flash state, the command byte, the 16 ID
 *    places, and the save area. Every 1000 steps and at the end it re-checks the whole save area. The
 *    board's flash_lost is checked one way: a change MAME makes outside what the board keeps (the save area
 *    and the ID bytes), to a byte MAME still held as the cartridge had it, must make it grow, because the
 *    board weighs a program against the cartridge's own byte. A case that produced no block erase, no
 *    program inside the save area, no ID read, or no block protect fails, so the test cannot pass by not
 *    testing.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "ngp/machine.h"

using offs_t = uint32_t;

/* MAME's ngp.cpp, lines 122-133 (the flash's command states) and the part of its ngp_state flash_w uses,
   as ngpref had them; flash.hxx is MAME's flash_w as it is. */
enum flash_state
{
	F_READ,                     /* xxxx F0 or 5555 AA 2AAA 55 5555 F0 */
	F_PROG1,
	F_PROG2,
	F_COMMAND,
	F_ID_READ,                  /* 5555 AA 2AAA 55 5555 90 */
	F_AUTO_PROGRAM,             /* 5555 AA 2AAA 55 5555 A0 address data */
	F_AUTO_CHIP_ERASE,          /* 5555 AA 2AAA 55 5555 80 5555 AA 2AAA 55 5555 10 */
	F_AUTO_BLOCK_ERASE,         /* 5555 AA 2AAA 55 5555 80 5555 AA 2AAA 55 block_address 30 */
	F_BLOCK_PROTECT             /* 5555 AA 2AAA 55 5555 9A 5555 AA 2AAA 55 5555 9A */
};

class ngp_state
{
public:
	struct {
		bool      present = false;
		uint8_t   manufacturer_id = 0;
		uint8_t   device_id = 0;
		uint8_t   *data = nullptr;
		uint8_t   org_data[16] = { };
		int32_t   state = F_READ;
		uint8_t   command[2] = { };
	} m_flash_chip[2];

	template <int Which> void flash_w(offs_t offset, uint8_t data);
};

#include "flash.hxx"

namespace
{

uint32_t xorshift32(uint32_t &x)
{
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

struct Counts
{
	int block_erases;
	int programs_in_save;
	int id_reads;
	int block_protects;
	uint64_t flash_lost;
};

struct CaseContext
{
	uint32_t size;
	uint32_t seed;
	int step;
};

bool is_id_place(offs_t offset)
{
	return (offset < 4)
		|| (offset >= 0x7C000 && offset < 0x7C004)
		|| (offset >= 0xFC000 && offset < 0xFC004)
		|| (offset >= 0x1FC000 && offset < 0x1FC004);
}

bool in_save_area(uint32_t abs_addr, uint32_t save_base)
{
	return abs_addr >= save_base && abs_addr < save_base + NGP_SAVE_SIZE;
}

offs_t picked_offset(uint32_t &x, int chip, uint32_t size, uint32_t save_base)
{
	uint32_t r = xorshift32(x) % 100;
	if (r < 50)
	{
		// inside the save area
		if (chip == 0)
			return save_base + xorshift32(x) % NGP_SAVE_SIZE;
		else
			return 0x1F8000 + xorshift32(x) % NGP_SAVE_SIZE;
	}
	else if (r < 70)
	{
		// an ID place
		static const offs_t bases[4] = { 0, 0x7C000, 0xFC000, 0x1FC000 };
		return bases[xorshift32(x) % 4] + xorshift32(x) % 4;
	}
	else if (r < 85)
	{
		// 0x5555 or 0x2AAA
		return (xorshift32(x) & 1) ? 0x2AAA : 0x5555;
	}
	else
	{
		// random below 0x200000
		return xorshift32(x) % 0x200000;
	}
}

void do_write(ngp_state &mame_state, int chip, offs_t offset, uint8_t data)
{
	if (chip == 0)
	{
		mame_state.flash_w<0>(offset, data);
		ngp_write8(0x200000 + offset, data);
	}
	else
	{
		mame_state.flash_w<1>(offset, data);
		ngp_write8(0x800000 + offset, data);
	}
}

bool same_byte(const std::vector<uint8_t> &mame, uint32_t address, const char *what,
	const CaseContext &ctx)
{
	uint8_t mb = mame[address];
	uint8_t bb = ngp_cartridge_byte(address);
	if (mb != bb)
	{
		printf("size %X seed %u step %d: %s at %06X: MAME %02X, the board %02X\n",
			ctx.size, ctx.seed, ctx.step, what, address, mb, bb);
		return false;
	}
	return true;
}

bool compare_chip(ngp_state &mame_state, int chip, uint32_t size, uint32_t seed, int step)
{
	if (mame_state.m_flash_chip[chip].state != ngp_board.flash[chip].state)
	{
		printf("size %X seed %u step %d: state mismatch chip %d (MAME %d, Board %d)\n",
			size, seed, step, chip, mame_state.m_flash_chip[chip].state, ngp_board.flash[chip].state);
		return false;
	}
	if (mame_state.m_flash_chip[chip].command[0] != ngp_board.flash[chip].command)
	{
		printf("size %X seed %u step %d: command mismatch chip %d (MAME %02x, Board %02x)\n",
			size, seed, step, chip, mame_state.m_flash_chip[chip].command[0], ngp_board.flash[chip].command);
		return false;
	}
	static const offs_t id_bases[4] = { 0, 0x7C000, 0xFC000, 0x1FC000 };
	for (int b = 0; b < 4; b++)
	{
		for (int i = 0; i < 4; i++)
		{
			offs_t off = id_bases[b] + i;
			uint8_t mb = mame_state.m_flash_chip[chip].data[off];
			uint8_t bb = ngp_cartridge_byte(chip * 0x200000 + off);
			if (mb != bb)
			{
				printf("size %X seed %u step %d: ID mismatch chip %d offset %05x (MAME %02x, Board %02x)\n",
					size, seed, step, chip, off, mb, bb);
				return false;
			}
		}
	}
	return true;
}

bool run_case(uint32_t size, uint32_t seed, int steps, Counts &counts)
{
	uint32_t x = seed;

	std::vector<uint8_t> image(size);
	for (uint32_t i = 0; i < size; i++)
		image[i] = (uint8_t)(xorshift32(x) & 0xFF);

	uint32_t chip0_end = (size < 0x200000) ? size : 0x200000;
	uint32_t save_base = chip0_end - NGP_SAVE_SIZE;
	for (uint32_t i = save_base; i < chip0_end; i++)
		image[i] = 0xFF;

	// MAME's side
	std::vector<uint8_t> mame(0x400000, 0);
	std::memcpy(mame.data(), image.data(), size);

	ngp_state mame_state;
	mame_state.m_flash_chip[0].present = true;
	mame_state.m_flash_chip[0].manufacturer_id = 0x98;
	if (size == 0x80000)
		mame_state.m_flash_chip[0].device_id = 0xAB;
	else if (size == 0x100000)
		mame_state.m_flash_chip[0].device_id = 0x2C;
	else
		mame_state.m_flash_chip[0].device_id = 0x2F;
	mame_state.m_flash_chip[0].state = F_READ;
	mame_state.m_flash_chip[0].data = mame.data();
	static const offs_t id_bases[4] = { 0, 0x7C000, 0xFC000, 0x1FC000 };
	for (int b = 0; b < 4; b++)
		for (int i = 0; i < 4; i++)
			mame_state.m_flash_chip[0].org_data[b * 4 + i] = mame_state.m_flash_chip[0].data[id_bases[b] + i];

	mame_state.m_flash_chip[1].present = (size == 0x400000);
	mame_state.m_flash_chip[1].manufacturer_id = 0x98;
	mame_state.m_flash_chip[1].device_id = 0x2F;
	mame_state.m_flash_chip[1].state = F_READ;
	mame_state.m_flash_chip[1].data = mame.data() + 0x200000;
	for (int b = 0; b < 4; b++)
		for (int i = 0; i < 4; i++)
			mame_state.m_flash_chip[1].org_data[b * 4 + i] = mame_state.m_flash_chip[1].data[id_bases[b] + i];

	// Board's side
	std::memset(&ngp_board.hooks, 0, sizeof ngp_board.hooks);
	ngp_insert(image.data(), size, save_base);
	ngp_power_on();

	Counts local = { 0, 0, 0, 0, 0 };
	uint64_t flash_lost_before = ngp_board.flash_lost;

	for (int step = 0; step < steps; step++)
	{
		CaseContext ctx = { size, seed, step };

		// Pick the chip
		int chip = 0;
		if (size == 0x400000)
		{
			if (xorshift32(x) % 4 == 0)
				chip = 1;
		}
		else
		{
			if (xorshift32(x) % 20 == 0)
				chip = 1;
		}

		// Determine the operation
		uint32_t op = xorshift32(x) % 100;
		offs_t offset = 0;
		uint8_t data = 0;
		bool is_command = false;

		if (op < 35)
		{
			// A command sequence
			is_command = true;
			do_write(mame_state, chip, 0x5555, 0xAA);
			do_write(mame_state, chip, 0x2AAA, 0x55);

			uint32_t cmd = xorshift32(x) % 8;
			switch (cmd)
			{
			case 0:
				offset = 0x5555; data = 0x80; break;
			case 1:
				offset = 0x5555; data = 0x90; break;
			case 2:
				offset = 0x5555; data = 0x9A; break;
			case 3:
				offset = 0x5555; data = 0xA0; break;
			case 4:
				offset = 0x5555; data = 0xF0; break;
			case 5:
				offset = 0x5555; data = 0x10; break;
			case 6:
				offset = picked_offset(x, chip, size, save_base);
				data = 0x30;
				break;
			default:
				offset = 0x5555;
				data = (uint8_t)(xorshift32(x) & 0xFF);
				break;
			}
		}
		else if (op < 60)
		{
			// A random byte at a picked offset (a program when the chip waits for one)
			offset = picked_offset(x, chip, size, save_base);
			data = (uint8_t)(xorshift32(x) & 0xFF);
		}
		else if (op < 75)
		{
			// 5555 AA alone, or 2AAA 55 alone
			if (xorshift32(x) & 1)
			{
				offset = 0x5555; data = 0xAA;
			}
			else
			{
				offset = 0x2AAA; data = 0x55;
			}
		}
		else
		{
			// A random byte at a random offset below 0x200000
			offset = xorshift32(x) % 0x200000;
			data = (uint8_t)(xorshift32(x) & 0xFF);
		}

		// Capture state before the write (for flash_lost check)
		int state_before = mame_state.m_flash_chip[chip].state;
		int cmd_before = mame_state.m_flash_chip[chip].command[0];
		uint8_t byte_before = mame_state.m_flash_chip[chip].data[offset];
		std::vector<uint8_t> block_before;
		bool block_erase_pending = (state_before == F_PROG2 && cmd_before == 0x80 && data == 0x30);
		if (block_erase_pending)
		{
			offs_t block_start = offset & ~0xFFFF;
			block_before.resize(0x10000);
			std::memcpy(block_before.data(), mame_state.m_flash_chip[chip].data + block_start, 0x10000);
		}

		// Do the write
		do_write(mame_state, chip, offset, data);

		// Update counts
		if (mame_state.m_flash_chip[chip].state == F_AUTO_BLOCK_ERASE)
			local.block_erases++;
		if (mame_state.m_flash_chip[chip].state == F_ID_READ)
			local.id_reads++;
		if (mame_state.m_flash_chip[chip].state == F_BLOCK_PROTECT)
			local.block_protects++;
		if (state_before == F_AUTO_PROGRAM)
		{
			uint32_t abs_addr = chip * 0x200000 + offset;
			if (in_save_area(abs_addr, save_base))
				local.programs_in_save++;
		}

		// Check flash_lost
		uint64_t flash_lost_after = ngp_board.flash_lost;
		bool flash_lost_grew = (flash_lost_after > flash_lost_before);
		flash_lost_before = flash_lost_after;

		if (state_before == F_AUTO_PROGRAM)
		{
			// The board weighs a program against the cartridge's own byte: it can only count one that changes
			// a byte MAME still held as the cartridge had it (an earlier lost erase may have changed it)
			uint8_t byte_after = mame_state.m_flash_chip[chip].data[offset];
			uint32_t original_at = chip * 0x200000 + offset;
			uint8_t original = original_at < size ? image[original_at] : 0;
			if (byte_before != byte_after && byte_before == original)
			{
				uint32_t abs_addr = chip * 0x200000 + offset;
				if (!in_save_area(abs_addr, save_base) && !is_id_place(offset))
				{
					if (!flash_lost_grew)
					{
						printf("size %X seed %u step %d: flash_lost did not grow for program at %06X\n",
							size, seed, step, abs_addr);
						return false;
					}
				}
			}
		}

		if (block_erase_pending)
		{
			offs_t block_start = offset & ~0xFFFF;
			bool changed_outside = false;
			for (uint32_t i = 0; i < 0x10000; i++)
			{
				uint8_t after = mame_state.m_flash_chip[chip].data[block_start + i];
				if (block_before[i] != after)
				{
					uint32_t abs_addr = chip * 0x200000 + block_start + i;
					if (!in_save_area(abs_addr, save_base) && !is_id_place(block_start + i))
						changed_outside = true;
				}
			}
			if (changed_outside && !flash_lost_grew)
			{
				printf("size %X seed %u step %d: flash_lost did not grow for block erase at %06X\n",
					size, seed, step, chip * 0x200000 + block_start);
				return false;
			}
		}

		// Compare states and IDs
		if (!compare_chip(mame_state, chip, size, seed, step))
			return false;

		// Compare the written offset if it is in the save area or at an ID place
		{
			uint32_t abs_addr = chip * 0x200000 + offset;
			if (in_save_area(abs_addr, save_base) || is_id_place(offset))
			{
				if (!same_byte(mame, abs_addr, "byte", ctx))
					return false;
			}
		}

		// Every 1000 steps and at the end: full save area and ID places
		if (step % 1000 == 0 || step == steps - 1)
		{
			for (uint32_t i = 0; i < NGP_SAVE_SIZE; i++)
			{
				uint32_t addr = save_base + i;
				if (!same_byte(mame, addr, "save area", ctx))
					return false;
			}
			for (int c = 0; c < 2; c++)
			{
				if (!mame_state.m_flash_chip[c].present)
					continue;
				for (int b = 0; b < 4; b++)
					for (int i = 0; i < 4; i++)
					{
						offs_t off = id_bases[b] + i;
						uint32_t addr = c * 0x200000 + off;
						if (!same_byte(mame, addr, "ID", ctx))
							return false;
					}
			}
		}
	}

	// Final full comparison
	for (int c = 0; c < 2; c++)
	{
		if (!mame_state.m_flash_chip[c].present)
			continue;
		if (mame_state.m_flash_chip[c].state != ngp_board.flash[c].state)
		{
			printf("size %X seed %u: final state mismatch chip %d\n", size, seed, c);
			return false;
		}
	}
	for (uint32_t i = 0; i < NGP_SAVE_SIZE; i++)
	{
		uint32_t addr = save_base + i;
		CaseContext ctx = { size, seed, -1 };
		if (!same_byte(mame, addr, "save area", ctx))
			return false;
	}
	for (int c = 0; c < 2; c++)
	{
		if (!mame_state.m_flash_chip[c].present)
			continue;
		for (int b = 0; b < 4; b++)
			for (int i = 0; i < 4; i++)
			{
				offs_t off = id_bases[b] + i;
				uint32_t addr = c * 0x200000 + off;
				CaseContext ctx = { size, seed, -1 };
				if (!same_byte(mame, addr, "ID", ctx))
					return false;
			}
	}

	counts = local;
	counts.flash_lost = ngp_board.flash_lost;
	return true;
}

} // namespace

int main()
{
	static const uint32_t sizes[4] = { 0x80000, 0x100000, 0x200000, 0x400000 };
	static const uint32_t seeds[2] = { 1, 2 };
	const int steps = 50000;

	for (int s = 0; s < 4; s++)
	{
		for (int i = 0; i < 2; i++)
		{
			Counts counts = { 0, 0, 0, 0, 0 };
			if (!run_case(sizes[s], seeds[i], steps, counts))
				return 1;

			if (counts.block_erases == 0 || counts.programs_in_save == 0 ||
				counts.id_reads == 0 || counts.block_protects == 0)
			{
				printf("size %X seed %u: missing operations (block_erases=%d, programs_in_save=%d, id_reads=%d, "
					"block_protects=%d)\n", sizes[s], seeds[i], counts.block_erases, counts.programs_in_save,
					counts.id_reads, counts.block_protects);
				return 1;
			}

			printf("size %X seed %u: OK (block_erases=%d, programs_in_save=%d, id_reads=%d, block_protects=%d, "
				"flash_lost=%llu)\n",
				sizes[s], seeds[i], counts.block_erases, counts.programs_in_save,
				counts.id_reads, counts.block_protects, (unsigned long long)counts.flash_lost);
		}
	}

	return 0;
}
