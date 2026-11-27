/**
 *    Name:        ngpref.cpp
 *    Description: the reference: a Neo Geo Pocket Color game's original program, with the console's BIOS, on
 *                 MAME's cores (TMP95C061, Z80, T6W28, K2GE), frame by frame from a controls script
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The console is the runtime's board (ngp/machine.h): MAME 0.289's ngp.cpp (mame/, BSD-3-Clause, Wilbert
 *    Pol) in C, its memory map, its I/O at 000080-0000BF, its real-time clock and its flash cartridge, with
 *    the cartridge read in place and its save area (game/data.h's GAME_SAVE_BASE) in the board's RAM. Here it
 *    wires the chips as ngp.cpp does: the TMP95C061 (tlcs900/mame) runs the BIOS and the game and keeps its
 *    own registers, the K2GE (video/k2gevideo) raises INT4 at VBLANK and pulses TI0 at every HBLANK, the Z80,
 *    the T6W28 and the DACs are the sound side (sound/ngpsound), and timer 3's output (TO3, port A) raises the
 *    Z80's IRQ. The run ends saying how many of the flash's programs and erases fell outside the save area,
 *    where the board cannot keep them (MAME's flash would have).
 *
 *    Time is the master clock (6.144 MHz): a TMP95C061 cycle, a dot of the screen; a frame is 199 lines of
 *    515 dots. As MAME's scheduler does, the TMP95C061 runs up to the next event of the console (a line's
 *    start, the end of an HBLANK, a second of the clock), the Z80 then runs up to it, and the event fires; when
 *    the TMP95C061 changes a line of the Z80, the Z80 is first brought up to that moment. A frame ends when its
 *    VBLANK starts (line 152), after the K2GE has drawn its last line: the controls script is applied there,
 *    and the hashes and the pictures are taken.
 *
 *    What MAME takes from the computer it runs on is fixed here, so that two runs are the same: the clock
 *    starts at --rtc's date, and the NVRAM (the work RAM and the first 32 I/O bytes, as MAME's ngpc.nv) is the
 *    file --nvram names, or none (the BIOS then starts as a console that has never been set up).
 *
 *    Over a run: --trace writes the addresses executed; --jumps where each JP or CALL through a register
 *    went when it was taken ("FROM TO"); --rom-reads the cartridge's bytes the TMP95C061 reads as data, under
 *    the instruction that read them, as "PC FIRST-LAST" ranges (mdref's and kref's format, which arcade-decomp's
 *    coverage.py reads). The core reads its instructions through the same space as its data: its RDOP marks
 *    them (tlcs900.cpp's one change), and they are left out.
 *
 *    Beside MAME's K2GE, the runtime's (ngp/k2ge.h) takes the window and draws every line when MAME's does,
 *    from the same memory; at the end of every frame their pens are compared, and the run ends saying how
 *    many lines differed.
 */
#include "emu.h"
#include "tmp95c061.h"

#include "game/data.h"
#include "game/extract.h"
#include "ngp/script.h"
#include "ngp/machine.h"
#include "ngpsound.h"
#include "k2gevideo.h"
#include "ngp/k2ge.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

// ---- the console's timing (MAME's ngp.cpp: 6.144 MHz, the screen's set_raw 515 x 199) ----

constexpr uint64_t kClock = NGP_MASTER_CLOCK;
constexpr uint64_t kLine = NGP_LINE_CLOCKS;
constexpr uint64_t kFrame = NGP_FRAME_CLOCKS;
constexpr uint64_t kVblankLine = NGP_VBLANK_LINE;

// ---- the TMP95C061, with what the reference needs of it ----

class reference_cpu : public tmp95c061_device
{
public:
	explicit reference_cpu(const machine_config &config) : tmp95c061_device(config, "maincpu", nullptr, uint32_t(kClock)) {}

	uint32_t pc() const { return m_pc.d; }
	uint32_t sp() const { return m_xssp.d; }

	// Whether the core is reading its instruction stream (RDOP), not data
	bool fetching() const { return m_fetching; }

	// Where MAME's ngp.cpp starts the BIOS when the NVRAM was loaded (machine_reset: set_state_int(TLCS900_PC))
	void set_pc(uint32_t address)
	{
		m_pc.d = address;
		m_prefetch_clear = true;
	}

	// The instruction at 'pc', as MAME's dasm900 writes it, read from the bus without side effects; its length
	// in bytes goes to 'length' when one is given
	std::string disassemble(uint32_t pc, uint8_t (*read)(offs_t), uint32_t *length = nullptr)
	{
		if (!m_disassembler)
			m_disassembler = create_disassembler();
		uint8_t window[32];
		g_machine.m_side_effects_disabled = true;
		for (uint32_t index = 0; index < 16; index++)
			window[(pc + index) & 31] = read(pc + index);
		g_machine.m_side_effects_disabled = false;
		const util::disasm_interface::data_buffer bytes(window, 31);
		std::ostringstream text;
		const offs_t result = m_disassembler->disassemble(text, pc, bytes, bytes);
		if (length)
			*length = result & util::disasm_interface::LENGTHMASK;
		return text.str();
	}

private:
	std::unique_ptr<util::disasm_interface> m_disassembler;
};

machine_config g_config;
reference_cpu *g_cpu = nullptr;

// ---- the console (the runtime's board, ngp_board) ----

uint8_t g_controls = 0;                // the script's: NGP_UP... and NGP_POWER

// The internal registers' handlers, by address (000000-00007F)
const address_map::entry *g_internal[0x80] = {};

// ---- time ----

uint64_t g_cpu_time = 0;               // the master clock the TMP95C061 has reached
uint64_t g_slice_start = 0;
int g_slice_budget = 0;
bool g_in_slice = false;
uint64_t g_event_time = 0;             // while an event fires, its own time
bool g_in_event = false;

uint64_t now()
{
	if (g_in_event)
		return g_event_time;
	if (g_in_slice)
		return g_slice_start + uint64_t(g_slice_budget - g_cpu->cycles_left());
	return g_cpu_time;
}

// ---- the real-time clock: the board keeps it, and a second passes every kClock ----

uint64_t g_next_second = kClock;

// The days of a month, for --rtc's check (the board's clock counts them as MAME's device_rtc_interface does)
int days_in_month(int month, int year)
{
	static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	return month == 2 && leap ? 29 : days[(month - 1) % 12];
}

// ---- the TMP95C061's bus: the board's map (MAME's ngp_state::main_mem; an 8-bit bus: set_am8_16(1)) ----

std::vector<uint8_t> g_executed(size_t(1) << 21, 0);  // 16 MB of addresses, a bit each
uint64_t g_instructions = 0;
uint32_t g_instruction_pc = 0;        // the instruction running: the last one the hook saw begin

// --rom-reads: (instruction, address) pairs of the cartridge's bytes the TMP95C061 reads as data; not its
// instruction fetches (RDOP marks them) nor the reference's own reads (the disassembler's, the stack's)
std::set<uint64_t> g_rom_reads;
bool g_record_rom_reads = false;

// The board's data_read hook: a byte of the cartridge read
void record_rom_read(uint32_t address)
{
	if (g_record_rom_reads && !g_cpu->fetching() && !g_machine.m_side_effects_disabled)
		g_rom_reads.insert(uint64_t(g_instruction_pc) << 24 | address);
}

uint8_t bus_r8(offs_t address)
{
	return ngp_read8(address);
}

void bus_w8(offs_t address, uint8_t data)
{
	ngp_write8(address, data);
}

// Words and longs on the 8-bit bus: their bytes in order, the lowest address first
uint16_t bus_r16(offs_t address)
{
	const uint8_t low = bus_r8(address);
	return uint16_t(low | (bus_r8(address + 1) << 8));
}

uint32_t bus_r32(offs_t address)
{
	const uint16_t low = bus_r16(address);
	return uint32_t(low) | (uint32_t(bus_r16(address + 2)) << 16);
}

void bus_w16(offs_t address, uint16_t data)
{
	bus_w8(address, uint8_t(data));
	bus_w8(address + 1, uint8_t(data >> 8));
}

void bus_w32(offs_t address, uint32_t data)
{
	bus_w16(address, uint16_t(data));
	bus_w16(address + 2, uint16_t(data >> 16));
}

// ---- what the board asks of the chips (its hooks) ----

// The TMP95C061's own registers (000000-00007F), through its internal map
uint8_t internal_read(uint32_t address)
{
	const address_map::entry *entry = g_internal[address];
	return entry && entry->read ? entry->read(address - entry->start) : 0;
}

void internal_write(uint32_t address, uint8_t data)
{
	const address_map::entry *entry = g_internal[address];
	if (entry && entry->write)
		entry->write(address - entry->start, data);
}

// The sound side, at the present
void psg_write(uint8_t data)
{
	ns_psg_write(now(), data);
}

void psg_enable(int on)
{
	ns_psg_enable(now(), on);
}

void dac_write(unsigned which, uint8_t data)
{
	ns_dac_write(now(), int(which), data);
}

void z80_run(int run)
{
	if (run)
		ns_z80_start(now());
	else
		ns_z80_stop(now());
}

void z80_nmi()
{
	ns_z80_nmi(now());
}

void z80_irq()
{
	ns_z80_irq(now());
}

// The power button's NMI, when the board lets it through (MAME's ngp_state::power_callback)
void power_nmi(int asserted)
{
	g_cpu->set_input_line(TLCS900_NMI, asserted ? ASSERT_LINE : CLEAR_LINE);
}

// ---- the lines between the chips ----

void vblank_pin(int state)
{
	g_cpu->set_input_line(TLCS900_INT4, state ? ASSERT_LINE : CLEAR_LINE);
}

void hblank_pin(int state)
{
	g_cpu->set_input_line(TLCS900_TIO, state ? ASSERT_LINE : CLEAR_LINE);
}

void int5(int state)
{
	g_cpu->set_input_line(TLCS900_INT5, state ? ASSERT_LINE : CLEAR_LINE);
}

void script_poke(void *context, uint32_t address, uint8_t value)
{
	(void)context;
	if (address >= 0x4000 && address < 0x7000)
		ngp_board.main_ram[address - 0x4000] = value;
	else if (address >= 0x7000 && address < 0x8000)
		ngp_board.sound_ram[address - 0x7000] = value;
}

// ---- running ----

std::ofstream g_log;                  // --log: every instruction of the frames --log-frames names
uint64_t g_log_first = 0, g_log_last = 0;
uint64_t g_frame = 0;

// --jumps: where each JP or CALL through a register went, when it was taken. Those are the ones whose
// memory operand is a register's (the prefixes B0-BF and F3-F5: (r), (r+d8), (r+d16), (r+r8), (r+r16),
// (-r), (r+)); with an address in it (F0-F2) the target is in the instruction.
std::set<std::pair<uint32_t, uint32_t>> g_jumps;
bool g_record_jumps = false;

struct pending_jump
{
	bool waiting = false;
	uint32_t from = 0;        // the jump's address
	uint32_t next = 0;        // the instruction after it, where it goes when it is not taken
	uint32_t sp = 0;          // XSP before it
};
pending_jump g_pending_jump;

// By address, in the cartridge and the BIOS (RAM's code can change): a JP or CALL through a register's
// length, or 0 for any other instruction
std::unordered_map<uint32_t, uint32_t> g_jump_lengths;

uint32_t computed_jump_length(uint32_t pc)
{
	g_machine.m_side_effects_disabled = true;
	const uint8_t first = bus_r8(pc);
	g_machine.m_side_effects_disabled = false;
	if ((first < 0xb0 || first > 0xbf) && (first < 0xf3 || first > 0xf5))
		return 0;
	const bool lasting = pc >= 0x200000;
	if (lasting)
	{
		const auto found = g_jump_lengths.find(pc);
		if (found != g_jump_lengths.end())
			return found->second;
	}
	uint32_t length = 0;
	const std::string text = g_cpu->disassemble(pc, bus_r8, &length);
	if (text.rfind("jp ", 0) != 0 && text.rfind("call ", 0) != 0)
		length = 0;
	if (lasting)
		g_jump_lengths[pc] = length;
	return length;
}

// The jump waiting has ended at 'pc'. An interrupt taken between the two pushed the PC and SR (6 bytes, on
// top of the 4 a CALL pushes; tmp95c061.cpp's tlcs900_check_irqs) and 'pc' is its handler's: the PC it
// pushed is where the jump went.
void end_jump(uint32_t pc)
{
	g_pending_jump.waiting = false;
	const uint32_t sp = g_cpu->sp();
	uint32_t to = pc;
	if (int64_t(g_pending_jump.sp) - int64_t(sp) >= 6)
	{
		g_machine.m_side_effects_disabled = true;
		to = bus_r32(sp + 2) & 0xffffff;
		g_machine.m_side_effects_disabled = false;	}
	if (to != g_pending_jump.next)
		g_jumps.emplace(g_pending_jump.from, to);
}

void on_instruction(offs_t pc)
{
	pc &= 0xffffff;
	g_executed[pc >> 3] |= uint8_t(1u << (pc & 7));
	g_instructions++;
	g_instruction_pc = pc;
	if (g_pending_jump.waiting)
		end_jump(pc);
	if (g_record_jumps)
		if (const uint32_t length = computed_jump_length(pc))
			g_pending_jump = { true, pc, (pc + length) & 0xffffff, g_cpu->sp() };
	if (g_log.is_open() && g_frame >= g_log_first && g_frame <= g_log_last)
	{
		char text[48];
		std::snprintf(text, sizeof(text), "%" PRIu64 " %06X  ", g_frame, pc);
		g_log << text << g_cpu->disassemble(pc, bus_r8) << '\n';
	}
}

// Runs the TMP95C061 until it reaches 'time' (it ends past it by what is left of its last instruction)
void run_cpu_until(uint64_t time)
{
	if (g_cpu_time >= time)
		return;
	g_slice_start = g_cpu_time;
	g_slice_budget = int(time - g_cpu_time);
	g_in_slice = true;
	const int ran = g_cpu->run(g_slice_budget);
	g_in_slice = false;
	g_cpu_time = g_slice_start + uint64_t(ran);
}

// Runs the console up to 'time': the TMP95C061 and the Z80 up to each event before it, and the event
// The runtime's K2GE (ngp/k2ge.h) beside MAME's: it takes the window and draws each line when MAME's does, at
// the start of the line after it, from the same memory; the frames' pens are compared when they end
ngp_k2ge_window g_window = {};
std::vector<uint8_t> g_runtime_pens(size_t(NV_WIDTH) * NV_HEIGHT);
uint64_t g_runtime_lines_unlike = 0;
std::string g_runtime_first_unlike;

void runtime_line(uint64_t time)
{
	const unsigned line = unsigned((time % kFrame) / kLine);
	if (line == 0)
		ngp_k2ge_take_window(nv_memory(), &g_window);
	else if (line <= NGP_K2GE_HEIGHT)
		ngp_k2ge_draw_line(nv_memory(), &g_window, line - 1, &g_runtime_pens[size_t(line - 1) * NV_WIDTH]);
}

void compare_runtime_picture(uint64_t frame)
{
	std::vector<uint16_t> pens(size_t(NV_WIDTH) * NV_HEIGHT);
	nv_pens(pens.data());
	for (unsigned line = 0; line < NV_HEIGHT; line++)
		for (unsigned x = 0; x < NV_WIDTH; x++)
			if (pens[line * NV_WIDTH + x] != g_runtime_pens[line * NV_WIDTH + x])
			{
				if (!g_runtime_lines_unlike++)
					g_runtime_first_unlike = "frame " + std::to_string(frame) + ", line " + std::to_string(line)
						+ ", x " + std::to_string(x);
				break;
			}
}

void run_until(uint64_t time)
{
	for (;;)
	{
		const uint64_t next = std::min({ nv_next_event(), g_next_second, time });
		const bool line_starts = nv_next_event() == next && next % kLine == 0;
		run_cpu_until(next);
		ns_run_until(next);
		g_in_event = true;
		nv_fire(next, &g_event_time);
		if (line_starts)
			runtime_line(next);
		if (g_next_second <= next)
		{
			g_event_time = g_next_second;
			ngp_clock_second();
			g_next_second += kClock;
		}
		g_in_event = false;
		if (next >= time)
			return;
	}
}

// ---- the outputs (mdref's forms) ----

uint64_t fnv(uint64_t hash, const void *data, size_t size)
{
	const uint8_t *bytes = static_cast<const uint8_t *>(data);
	for (size_t index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 0x100000001b3ull;
	return hash;
}

// The console's state: the TMP95C061's registers, its memories, the K2GE's, the I/O, the game's save area
// (the board's first 16 KB from GAME_SAVE_BASE; not the BIOS's bytes after it, as the hashes always were)
uint64_t state_hash()
{
	static const int registers[] = { TLCS900_PC, TLCS900_SR, TLCS900_XWA0, TLCS900_XBC0, TLCS900_XDE0,
		TLCS900_XHL0, TLCS900_XWA1, TLCS900_XBC1, TLCS900_XDE1, TLCS900_XHL1, TLCS900_XWA2, TLCS900_XBC2,
		TLCS900_XDE2, TLCS900_XHL2, TLCS900_XWA3, TLCS900_XBC3, TLCS900_XDE3, TLCS900_XHL3, TLCS900_XIX,
		TLCS900_XIY, TLCS900_XIZ, TLCS900_XNSP, TLCS900_XSSP };
	uint64_t hash = 0xcbf29ce484222325ull;
	for (int index : registers)
	{
		const uint32_t value = g_cpu->state_value(index);
		hash = fnv(hash, &value, sizeof(value));
	}
	hash = fnv(hash, ngp_board.main_ram, sizeof(ngp_board.main_ram));
	hash = fnv(hash, ngp_board.sound_ram, sizeof(ngp_board.sound_ram));
	hash = fnv(hash, ngp_board.io, sizeof(ngp_board.io));
	hash = fnv(hash, nv_memory(), 0x4000);
	hash = fnv(hash, ngp_board.save, 0x4000);
	hash = fnv(hash, &ngp_board.flash_writes, sizeof(ngp_board.flash_writes));
	return hash;
}

std::vector<uint8_t> g_picture(size_t(NV_WIDTH) * NV_HEIGHT * 3);

void write_bmp(const std::string &path)
{
	const int row = (NV_WIDTH * 3 + 3) & ~3;
	const uint32_t size = 54 + uint32_t(row) * NV_HEIGHT;
	std::vector<uint8_t> file(size, 0);
	auto put32 = [&](size_t at, uint32_t value) { for (int byte = 0; byte < 4; byte++) file[at + byte] = uint8_t(value >> (8 * byte)); };
	file[0] = 'B';
	file[1] = 'M';
	put32(2, size);
	put32(10, 54);
	put32(14, 40);
	put32(18, uint32_t(NV_WIDTH));
	put32(22, uint32_t(NV_HEIGHT));
	file[26] = 1;
	file[28] = 24;
	for (int y = 0; y < NV_HEIGHT; y++)
		for (int x = 0; x < NV_WIDTH; x++)
		{
			const uint8_t *pixel = &g_picture[(size_t(y) * NV_WIDTH + size_t(x)) * 3];
			uint8_t *out = &file[54 + size_t(NV_HEIGHT - 1 - y) * size_t(row) + size_t(x) * 3];
			out[0] = pixel[2];
			out[1] = pixel[1];
			out[2] = pixel[0];
		}
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(file.data()), std::streamsize(file.size()));
}

void write_wav(const std::string &path, const std::vector<int16_t> &samples, uint32_t rate)
{
	std::vector<uint8_t> file(44 + samples.size() * 2, 0);
	auto put32 = [&](size_t at, uint32_t value) { for (int byte = 0; byte < 4; byte++) file[at + byte] = uint8_t(value >> (8 * byte)); };
	auto put16 = [&](size_t at, uint16_t value) { file[at] = uint8_t(value); file[at + 1] = uint8_t(value >> 8); };
	std::memcpy(&file[0], "RIFF", 4);
	put32(4, uint32_t(file.size() - 8));
	std::memcpy(&file[8], "WAVEfmt ", 8);
	put32(16, 16);
	put16(20, 1);
	put16(22, 2);
	put32(24, rate);
	put32(28, rate * 4);
	put16(32, 4);
	put16(34, 16);
	std::memcpy(&file[36], "data", 4);
	put32(40, uint32_t(samples.size() * 2));
	for (size_t index = 0; index < samples.size(); index++)
		put16(44 + index * 2, uint16_t(samples[index]));
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(file.data()), std::streamsize(file.size()));
}

void write_addresses(const std::string &path, const std::vector<uint8_t> &bits, uint32_t count, int digits, const char *title)
{
	std::ofstream out(path);
	out << "# " << title << ", one per line\n";
	for (uint32_t address = 0; address < count; address++)
		if (bits[address >> 3] & (1u << (address & 7)))
		{
			char line[16];
			std::snprintf(line, sizeof(line), "%0*X\n", digits, address);
			out << line;
		}
}

// The reads as "PC FIRST-LAST" lines: by instruction, runs of consecutive bytes (mdref's write_reads)
void write_reads(const std::string &path, const std::set<uint64_t> &reads, const char *title)
{
	std::ofstream out(path);
	out << "# " << title << ": PC FIRST-LAST\n";
	uint32_t pc = 0, first = 0, last = 0;
	bool open = false;
	auto flush = [&]() {
		char text[40];
		std::snprintf(text, sizeof(text), "%06X %06X-%06X\n", pc, first, last);
		out << text;
	};
	for (const uint64_t pair : reads)
	{
		const uint32_t here_pc = uint32_t(pair >> 24), here = uint32_t(pair & 0xffffff);
		if (open && here_pc == pc && here == last + 1)
		{
			last = here;
			continue;
		}
		if (open)
			flush();
		pc = here_pc;
		first = last = here;
		open = true;
	}
	if (open)
		flush();
}

void write_jumps(const std::string &path)
{
	std::ofstream out(path);
	out << "# where each TMP95C061 JP or CALL through a register went, by ngpref: FROM TO\n";
	for (const auto &jump : g_jumps)
	{
		char text[24];
		std::snprintf(text, sizeof(text), "%06X %06X\n", jump.first, jump.second);
		out << text;
	}
}

// The console's memories at the end of a frame, a file each: DIR/FRAME_main_ram.bin...
void dump_frame(const std::string &folder, uint64_t frame)
{
	const struct { const char *name; const void *data; size_t size; } memories[] = {
		{ "main_ram", ngp_board.main_ram, sizeof(ngp_board.main_ram) },
		{ "sound_ram", ngp_board.sound_ram, sizeof(ngp_board.sound_ram) },
		{ "io", ngp_board.io, sizeof(ngp_board.io) },
		{ "k2ge", nv_memory(), 0x4000 },
	};
	for (const auto &memory : memories)
	{
		std::ofstream out(folder + "/" + std::to_string(frame) + "_" + memory.name + ".bin", std::ios::binary);
		out.write(reinterpret_cast<const char *>(memory.data), std::streamsize(memory.size));
	}
}

// MAME's ngpc.nv: the work RAM (004000-006FFF) and the first 32 I/O bytes (000080-00009F)
constexpr size_t kNvramSize = NGP_NVRAM_SIZE;

bool read_nvram(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	std::vector<uint8_t> data(kNvramSize);
	if (!in.read(reinterpret_cast<char *>(data.data()), std::streamsize(kNvramSize)))
		return false;
	ngp_nvram_read(data.data());
	return true;
}

void write_nvram(const std::string &path)
{
	std::vector<uint8_t> data(kNvramSize);
	ngp_nvram_write(data.data());
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(kNvramSize));
}

// YYYYMMDDhhmmss
bool parse_date(const char *text, ngp_clock &clock)
{
	if (std::strlen(text) != 14)
		return false;
	int fields[6];
	const int widths[6] = { 4, 2, 2, 2, 2, 2 };
	for (int field = 0, at = 0; field < 6; at += widths[field], field++)
	{
		const std::string part(text + at, size_t(widths[field]));
		if (part.find_first_not_of("0123456789") != std::string::npos)
			return false;
		fields[field] = std::atoi(part.c_str());
	}
	clock = { fields[0], fields[1], fields[2], fields[3], fields[4], fields[5] };
	return clock.month >= 1 && clock.month <= 12 && clock.day >= 1 && clock.day <= days_in_month(clock.month, clock.year)
		&& clock.hour < 24 && clock.minute < 60 && clock.second < 60;
}

} // namespace

int main(int argc, char **argv)
{
	const char *usage =
		"usage: ngpref [--frames N] [--script FILE] [--rom FOLDER] [--bios FOLDER] [--nvram FILE] [--save-nvram FILE]"
		" [--rtc YYYYMMDDhhmmss] [--hashes FILE] [--trace FILE] [--jumps FILE] [--rom-reads FILE]"
		" [--dump FRAME ... --dump-dir DIR]"
		" [--video-hashes FILE] [--video-dump FRAME ... --video-dir DIR] [--sound-hashes FILE] [--sound-wav FILE]"
		" [--sound-trace FILE] [--log FILE [--log-frames FIRST LAST]]\n";
	std::string rom_folder = "rom", bios_folder, nvram_path, save_nvram_path, script_path;
	std::string hashes_path, trace_path, dump_folder = ".", video_hashes_path, video_folder = ".";
	std::string sound_hashes_path, sound_wav_path, sound_trace_path, log_path, jumps_path, rom_reads_path;
	std::set<uint64_t> dump_frames, video_dump_frames;
	uint64_t frames = 600;
	bool frames_given = false;
	ngp_board.clock = { 2000, 1, 1, 0, 0, 0 };     // --rtc's, or MAME's own date when it has none

	for (int index = 1; index < argc; index++)
	{
		const std::string option = argv[index];
		const bool has_value = index + 1 < argc;
		auto frame_list = [&](std::set<uint64_t> &list) {
			while (index + 1 < argc && argv[index + 1][0] != '-')
				list.insert(std::strtoull(argv[++index], nullptr, 10));
		};
		if (option == "--frames" && has_value)
		{
			frames = std::strtoull(argv[++index], nullptr, 10);
			frames_given = true;
		}
		else if (option == "--script" && has_value)
			script_path = argv[++index];
		else if (option == "--rom" && has_value)
			rom_folder = argv[++index];
		else if (option == "--bios" && has_value)
			bios_folder = argv[++index];
		else if (option == "--nvram" && has_value)
			nvram_path = argv[++index];
		else if (option == "--save-nvram" && has_value)
			save_nvram_path = argv[++index];
		else if (option == "--rtc" && has_value)
		{
			if (!parse_date(argv[++index], ngp_board.clock))
			{
				std::cerr << "--rtc takes a date and time as YYYYMMDDhhmmss\n";
				return 1;
			}
		}
		else if (option == "--hashes" && has_value)
			hashes_path = argv[++index];
		else if (option == "--trace" && has_value)
			trace_path = argv[++index];
		else if (option == "--jumps" && has_value)
			jumps_path = argv[++index];
		else if (option == "--rom-reads" && has_value)
			rom_reads_path = argv[++index];
		else if (option == "--dump")
			frame_list(dump_frames);
		else if (option == "--dump-dir" && has_value)
			dump_folder = argv[++index];
		else if (option == "--video-hashes" && has_value)
			video_hashes_path = argv[++index];
		else if (option == "--video-dump")
			frame_list(video_dump_frames);
		else if (option == "--video-dir" && has_value)
			video_folder = argv[++index];
		else if (option == "--sound-hashes" && has_value)
			sound_hashes_path = argv[++index];
		else if (option == "--sound-wav" && has_value)
			sound_wav_path = argv[++index];
		else if (option == "--sound-trace" && has_value)
			sound_trace_path = argv[++index];
		else if (option == "--log" && has_value)
			log_path = argv[++index];
		else if (option == "--log-frames" && index + 2 < argc)
		{
			g_log_first = std::strtoull(argv[++index], nullptr, 10);
			g_log_last = std::strtoull(argv[++index], nullptr, 10);
		}
		else
		{
			std::cerr << usage;
			return 1;
		}
	}
	if (bios_folder.empty())
		bios_folder = rom_folder;
	g_record_jumps = !jumps_path.empty();
	g_record_rom_reads = !rom_reads_path.empty();

	if (const char *problem = game_rom_set_load(rom_folder.c_str()))
	{
		std::cerr << "The cartridge in " << rom_folder << " cannot be read: " << problem << '\n';
		return 1;
	}
	if (const char *problem = game_bios_load(bios_folder.c_str()))
	{
		std::cerr << "The BIOS in " << bios_folder << " cannot be read: " << problem << '\n';
		return 1;
	}

	// The cartridge in the board (MAME's load_ngp_cart and machine_start: a 2 MB one is flash chip 0, a Toshiba
	// 16 Mbit), its save area in the board's RAM, and the BIOS
	ngp_insert(game_program, GAME_PROGRAM_SIZE, GAME_SAVE_BASE);
	ngp_board.bios = game_bios;
	ngp_power_on();

	const bool nvram_loaded = !nvram_path.empty();
	if (nvram_loaded && !read_nvram(nvram_path))
	{
		std::cerr << "The NVRAM " << nvram_path << " cannot be read (" << kNvramSize << " bytes)\n";
		return 1;
	}

	ngp_script *script = nullptr;
	if (!script_path.empty())
	{
		char error[256];
		script = ngp_script_load(script_path.c_str(), error, sizeof(error));
		if (!script)
		{
			std::cerr << error << '\n';
			return 1;
		}
		if (!frames_given && ngp_script_frames(script))
			frames = ngp_script_frames(script);
		ngp_script_apply(script, 0, &g_controls, script_poke, nullptr);
	}
	ngp_board.controls = g_controls;               // held at power on: no NMI for them

	// The TMP95C061 (MAME's ngp_common): an 8-bit bus, port A to the Z80's IRQ, the main battery full
	reference_cpu cpu(g_config);
	g_cpu = &cpu;
	cpu.set_am8_16(1);
	cpu.space(AS_PROGRAM).r8 = bus_r8;
	cpu.space(AS_PROGRAM).r16 = bus_r16;
	cpu.space(AS_PROGRAM).r32 = bus_r32;
	cpu.space(AS_PROGRAM).w8 = bus_w8;
	cpu.space(AS_PROGRAM).w16 = bus_w16;
	cpu.space(AS_PROGRAM).w32 = bus_w32;
	cpu.space(AS_PROGRAM).width = 8;
	cpu.porta_write().set([](offs_t, uint8_t data) { ngp_port_a(data); });
	cpu.an_read<0>().set([] { return uint16_t(0x3ff); });
	cpu.instruction_hook = on_instruction;
	cpu.log_handler = [](const std::string &message) { std::cerr << message; };
	cpu.start();
	for (const address_map::entry &entry : cpu.internal_map().m_entries)
		for (offs_t address = entry.start; address <= entry.end && address < 0x80; address++)
			g_internal[address] = &entry;

	// What the board asks of the chips
	ngp_board.hooks.internal_read = internal_read;
	ngp_board.hooks.internal_write = internal_write;
	ngp_board.hooks.k2ge_read = nv_read;
	ngp_board.hooks.k2ge_write = nv_write;
	ngp_board.hooks.psg_write = psg_write;
	ngp_board.hooks.psg_enable = psg_enable;
	ngp_board.hooks.dac_write = dac_write;
	ngp_board.hooks.z80_run = z80_run;
	ngp_board.hooks.z80_nmi = z80_nmi;
	ngp_board.hooks.z80_irq = z80_irq;
	ngp_board.hooks.nmi = power_nmi;
	ngp_board.hooks.data_read = record_rom_read;

	static const ns_host sound_host = { ngp_comm_read, ngp_comm_write, int5 };
	ns_attach(0, ngp_board.sound_ram, &sound_host);
	nv_attach(now, vblank_pin, hblank_pin);

	// MAME's machine_reset (TO3 low: ngp_power_on): the CPU from its reset vector, or the BIOS's restart when the
	// NVRAM was loaded
	cpu.reset();
	if (nvram_loaded)
		cpu.set_pc(0xff1800);

	if (!log_path.empty())
	{
		g_log.open(log_path);
		if (!g_log_last)
			g_log_first = 1, g_log_last = 1;
	}
	std::ofstream hashes, video_hashes, sound_hashes;
	if (!hashes_path.empty())
		hashes.open(hashes_path);
	if (!video_hashes_path.empty())
		video_hashes.open(video_hashes_path);
	if (!sound_hashes_path.empty())
		sound_hashes.open(sound_hashes_path);
	std::vector<int16_t> sound_mix;

	// VBLANK: the frame ends
	auto end_frame = [&](uint64_t frame, uint64_t time) {
		ngp_script_apply(script, frame, &g_controls, script_poke, nullptr);
		ngp_set_controls(g_controls);
		ns_end_frame(time);
		compare_runtime_picture(frame);
		if (sound_hashes.is_open())
		{
			char text[64];
			std::snprintf(text, sizeof(text), "%" PRIu64 " %016" PRIx64 " %016" PRIx64 "\n", frame,
				ns_frame_samples_hash(), ns_state_hash());
			sound_hashes << text;
		}
		if (!sound_wav_path.empty())
		{
			size_t count = 0;
			const int16_t *mix = ns_frame_mix(&count);
			sound_mix.insert(sound_mix.end(), mix, mix + count * 2);
		}
		if (hashes.is_open())
		{
			char text[48];
			std::snprintf(text, sizeof(text), "%" PRIu64 " %016" PRIx64 "\n", frame, state_hash());
			hashes << text;
		}
		if (dump_frames.count(frame))
			dump_frame(dump_folder, frame);
		if (video_hashes.is_open() || video_dump_frames.count(frame))
		{
			nv_picture(g_picture.data());
			if (video_hashes.is_open())
			{
				char text[48];
				std::snprintf(text, sizeof(text), "%" PRIu64 " %016" PRIx64 "\n", frame,
					fnv(0xcbf29ce484222325ull, g_picture.data(), g_picture.size()));
				video_hashes << text;
			}
			if (video_dump_frames.count(frame))
				write_bmp(video_folder + "/frame_" + std::to_string(frame) + ".bmp");
		}
	};

	int status = 0;
	uint64_t frame = 0;
	try
	{
		for (frame = 1; frame <= frames; frame++)
		{
			g_frame = frame;
			const uint64_t vblank = (frame - 1) * kFrame + kVblankLine * kLine;
			run_until(vblank);
			end_frame(frame, vblank);
		}
		frame--;
	}
	catch (const std::exception &error)
	{
		std::cerr << "TMP95C061 stopped at PC " << std::hex << cpu.pc() << std::dec << " in frame " << frame << ": "
			<< error.what() << '\n';
		status = 2;
	}

	size_t unique = 0;
	for (uint8_t bits : g_executed)
		for (; bits; bits &= uint8_t(bits - 1))
			unique++;
	std::cout << "frames:            " << frame << '\n'
		<< "instructions:      " << g_instructions << '\n'
		<< "unique addresses:  " << unique << '\n'
		<< "final PC:          " << std::hex << cpu.pc() << std::dec << '\n'
		<< "flash writes:      " << ngp_board.flash_writes << '\n'
		<< "flash outside the save area: " << ngp_board.flash_lost << '\n'
		<< "Z80 cycles:        " << ns_z80_cycles() << '\n'
		<< "runtime K2GE lines unlike MAME's: " << g_runtime_lines_unlike;
	if (g_runtime_lines_unlike)
		std::cout << " (the first in " << g_runtime_first_unlike << ')';
	std::cout << '\n';
	if (!trace_path.empty())
		write_addresses(trace_path, g_executed, 1u << 24, 6, "TMP95C061 addresses executed by ngpref");
	if (!jumps_path.empty())
		write_jumps(jumps_path);
	if (!rom_reads_path.empty())
		write_reads(rom_reads_path, g_rom_reads, "cartridge bytes the TMP95C061 read as data, by ngpref");
	if (!sound_wav_path.empty())
		write_wav(sound_wav_path, sound_mix, ns_mix_rate());
	if (!sound_trace_path.empty())
	{
		std::vector<uint8_t> bits(ns_executed(), ns_executed() + 0x2000);
		write_addresses(sound_trace_path, bits, 0x10000, 4, "Z80 addresses executed by ngpref");
	}
	if (!save_nvram_path.empty())
		write_nvram(save_nvram_path);
	ngp_script_free(script);
	return status;
}
