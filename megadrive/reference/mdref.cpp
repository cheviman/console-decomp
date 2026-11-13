/**
 *    Name:        mdref.cpp
 *    Description: development reference of a Mega Drive game: runs the original 68000 program on MAME's
 *                 68000 core against the runtime's model of the console, to find code, record state per
 *                 frame, and measure how much of each frame the program takes
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Not part of the game. Usage:
 *      mdref [--frames N] [--script controls.txt] [--rom FOLDER] [--hashes FILE] [--trace FILE] [--calls FILE]
 *            [--jumps FILE] [--rom-reads FILE] [--z80-rom-reads FILE] [--dump FRAME ... --dump-dir DIR]
 *            [--video-hashes FILE]
 *            [--video-dump FRAME ... --video-dir DIR] [--sound-hashes FILE] [--sound-wav FILE]
 *            [--sound-trace FILE] [--step ADDRESS --idle ADDRESS [--steps FILE]]
 *            [--profile FILE [--profile-from FRAME]] [--no-refresh]
 *
 *    It runs the original program, so it reads the player's cartridge when it starts, as MAME does (the
 *    game's game/extract.h: rom/ by default, a ROM file, a .zip or a folder).
 *
 *    Time is the board's, in master clocks (md/machine.h): the 68000 runs a line at a time at most, and each
 *    line begins when its time comes (md_line_start: the H interrupt's counter, the V interrupt at line 224,
 *    the Z80's INT). A frame ends when VBLANK begins, at line 224: its state hash is taken there, and the
 *    control script applied. The 68000 loses the time the board says (md/machine.h: the DMA, the FIFO, the
 *    refresh) by giving up cycles of its slice.
 *
 *    --hashes writes "FRAME HASH" per frame (md_state_hash); --trace and --calls the addresses executed and
 *    the ones reached by jsr or bsr; --jumps where each jmp or jsr through a register went ("FROM TO");
 *    --rom-reads the cartridge's bytes the 68000 reads as data (not its instruction fetches), a DMA's
 *    under the instruction that started it, as "PC FIRST-LAST" ranges (kref's format, which the core's
 *    coverage.py reads); --z80-rom-reads the ones the Z80 reads through its window, under its PC (it runs
 *    the sound side);
 *    --dump writes the board's memories at the end of those frames, a file each.
 *
 *    --video-hashes and --video-dump draw the frames with the runtime's VDP (md_vdp_render_line), a line at
 *    a time as each line begins: the hash of every frame's pixels, and frames as BMP (DIR/frame_N.bmp).
 *    (To do: MAME's 315-5313 drawing the same lines, as the reference's picture.)
 *
 *    --sound-hashes, --sound-wav and --sound-trace run the sound side (sound/mdsound.cpp: MAME's Z80 and PSG,
 *    ymfm's YM2612): per frame the hash of the chips' samples and of the Z80's RAM; the mix as a WAV; the Z80
 *    addresses executed. Without them the Z80 stands still (and a program that waits on it may hang).
 *
 *    The load: --step names the address where the program's main loop begins a step (a frame's work) and
 *    --idle the one where it starts waiting for the next frame. A step that is still working when VBLANK
 *    comes means the game falls behind: it slows down. --steps writes "START_FRAME FRAMES WORK" per step
 *    (the frame it began in, the frames until the next one began, its 68000 cycles from step to idle, the
 *    interrupts it took included); the summary counts the frames lost (a step that took N frames lost N-1)
 *    and the steps whose work is more than a frame. --profile writes the 68000 cycles spent in each function (from the call it
 *    was entered by: jsr, bsr, or an interrupt) after frame --profile-from: "ADDRESS SELF INCLUSIVE CALLS",
 *    the most expensive first.
 */
#include "emu.h"
#include "m68000.h"

#include "game/data.h"
#include "game/extract.h"
#include "md/machine.h"
#include "md/script.h"
#include "mdsound.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

class reference_cpu : public m68000_device
{
public:
	reference_cpu() : m68000_device(m_config, "maincpu", nullptr, MD_CPU_CLOCK) {}
	u32 pc() const { return m_ipc; }
	// the 68000 loses 'cycles' within its slice (the board holds its bus)
	void lose(int cycles) { m_icount -= cycles; }

private:
	static inline machine_config m_config;
};

reference_cpu *g_cpu = nullptr;
int g_irq_level = 0;
int g_acknowledged = 0;                     // the level the 68000 just took, for the profile's call stack
std::vector<uint8_t> g_executed(1 << 21);  // one bit per address of the 24-bit bus
std::vector<uint8_t> g_called(1 << 21);
uint32_t g_previous_pc = 0xFFFFFFFF;
uint64_t g_instructions = 0;

// ---- time: the 68000's, in master clocks; while it runs, where its slice started and how long it is ----

uint64_t g_cpu_time = 0;
uint64_t g_slice_start = 0;
int g_slice_budget = 0;
bool g_in_slice = false;

uint64_t now()
{
	return g_in_slice ? g_slice_start + uint64_t(g_slice_budget - g_cpu->cycles_left()) * MD_CPU_DIVIDER : g_cpu_time;
}

void stall(uint64_t until)
{
	const uint64_t at = now();
	if (until <= at)
		return;
	const int cycles = int((until - at + MD_CPU_DIVIDER - 1) / MD_CPU_DIVIDER);
	if (g_in_slice)
		g_cpu->lose(cycles);
	else
		g_cpu_time += uint64_t(cycles) * MD_CPU_DIVIDER;
}

// The board's interrupt line as the 68000 sees it, after anything that can change it
void sync_irq()
{
	const int level = md_irq_level();
	if (level == g_irq_level)
		return;
	if (g_irq_level)
		g_cpu->set_input_line(g_irq_level, CLEAR_LINE);
	if (level)
		g_cpu->set_input_line(level, ASSERT_LINE);
	g_irq_level = level;
}

// --rom-reads and --z80-rom-reads: (instruction, cartridge address) pairs of the data read from the cartridge
std::set<uint64_t> g_rom_reads, g_z80_rom_reads;
bool g_record_rom_reads = false;

void record_read(std::set<uint64_t> &reads, uint32_t pc, uint32_t address)
{
	if (g_record_rom_reads && address < GAME_PROGRAM_SIZE)
		reads.insert(uint64_t(pc) << 24 | address);
}

void data_read(int z80, uint32_t address)
{
	if (z80)
		record_read(g_z80_rom_reads, ms_z80_pc(), address & 0xFFFFFF);
	else
		record_read(g_rom_reads, g_cpu->pc(), address & 0xFFFFFF);
}

u16 bus_read(offs_t address, u16 mem_mask)
{
	if (g_record_rom_reads && !g_reading_instructions)
	{
		if (mem_mask & 0xFF00)
			record_read(g_rom_reads, g_cpu->pc(), address & 0xFFFFFE);
		if (mem_mask & 0x00FF)
			record_read(g_rom_reads, g_cpu->pc(), (address & 0xFFFFFE) + 1);
	}
	const u16 value = md_read16(address, mem_mask);
	sync_irq();
	return value;
}

void bus_write(offs_t address, u16 data, u16 mem_mask)
{
	md_write16(address, data, mem_mask);
	sync_irq();
}

// The interrupt acknowledge: the VDP takes it back, and the level takes its autovector
u16 acknowledge(offs_t address, u16 mem_mask)
{
	(void)mem_mask;
	const int level = int((address >> 1) & 7);
	md_irq_acknowledge(level);
	g_acknowledged = level;
	sync_irq();
	return u16(0x18 + level);
}

// The 68000 runs until the board's clock reaches 'time', a line at a time at most
void run_until(reference_cpu &cpu, uint64_t time)
{
	while (g_cpu_time < time)
	{
		// the cycles the Z80's accesses to the 68000's bus took from it, charged before it goes on
		g_cpu_time += uint64_t(std::exchange(md_board.z80_bus_debt, 0u)) * MD_CPU_DIVIDER;
		if (g_cpu_time >= time)
			break;
		const uint64_t cycles = std::min<uint64_t>((time - g_cpu_time + MD_CPU_DIVIDER - 1) / MD_CPU_DIVIDER,
			MD_LINE_CLOCKS / MD_CPU_DIVIDER + 1);
		g_slice_start = g_cpu_time;
		g_slice_budget = int(cycles);
		g_in_slice = true;
		g_cpu_time += uint64_t(cpu.run(int(cycles))) * MD_CPU_DIVIDER;
		g_in_slice = false;
	}
}

bool g_sound = false;

void sound_sync()
{
	if (g_sound)
		ms_run_until(now());
}

// ---- what ran ----

void mark(std::vector<uint8_t> &bits, uint32_t address)
{
	address &= 0xFFFFFF;
	bits[address >> 3] |= uint8_t(1u << (address & 7));
}

uint16_t opcode_at(uint32_t pc)
{
	return pc + 1 < GAME_PROGRAM_SIZE ? uint16_t(game_program[pc] << 8 | game_program[pc + 1]) : 0;
}

bool is_call(uint32_t pc)
{
	const uint16_t opcode = opcode_at(pc);
	return (opcode & 0xFFC0) == 0x4E80 || (opcode & 0xFF00) == 0x6100;    // jsr, bsr
}

bool is_return(uint32_t pc)
{
	const uint16_t opcode = opcode_at(pc);
	return opcode == 0x4E75 || opcode == 0x4E77 || opcode == 0x4E73;      // rts, rtr, rte
}

// A jmp or jsr through a register or a register's memory: (An), (d16,An), (d8,An,Xn), (d8,PC,Xn)
bool is_computed_jump(uint32_t pc)
{
	const uint16_t opcode = opcode_at(pc);
	const int mode = (opcode >> 3) & 7;
	return ((opcode & 0xFFC0) == 0x4E80 || (opcode & 0xFFC0) == 0x4EC0)
		&& (mode == 2 || mode == 5 || mode == 6 || (opcode & 0x3F) == 0x3B);
}

std::set<std::pair<uint32_t, uint32_t>> g_jumps;    // --jumps: where each computed jump went

// The reads as "PC FIRST-LAST" lines: by instruction, runs of consecutive bytes
void write_reads(const std::string &path, const std::set<uint64_t> &reads, const char *title, int pc_digits)
{
	std::ofstream out(path);
	out << "# " << title << ": PC FIRST-LAST\n";
	uint32_t pc = 0, first = 0, last = 0;
	bool open = false;
	auto flush = [&]() {
		char text[40];
		std::snprintf(text, sizeof(text), "%0*X %06X-%06X\n", pc_digits, pc, first, last);
		out << text;
	};
	for (const uint64_t pair : reads)
	{
		const uint32_t here_pc = uint32_t(pair >> 24), here = uint32_t(pair & 0xFFFFFF);
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

// ---- the load: the main loop's steps, and the cycles of each function ----

uint32_t g_step_address = 0xFFFFFFFF, g_idle_address = 0xFFFFFFFF;
bool g_working = false;                     // a step is running (not waiting for the next frame)
uint64_t g_step_start = 0;                  // master clock the running step began
struct step_record
{
	uint64_t frame;                         // the frame it began in
	uint64_t frames;                        // frames until the next step began
	uint64_t work;                          // 68000 cycles from step to idle
};
std::vector<step_record> g_steps;
uint64_t g_frame = 0;                       // the frame being run (1 the first)
uint64_t g_overruns = 0;                    // VBLANKs that came while a step was working

struct function_cost
{
	uint64_t self = 0, inclusive = 0, calls = 0;
};
bool g_profiling = false;
std::map<uint32_t, function_cost> g_costs;
std::vector<uint32_t> g_stack;              // the functions entered and not yet left
uint64_t g_last_instruction = 0;            // master clock the previous instruction began

void profile(uint32_t pc)
{
	const uint64_t at = now();
	const uint64_t spent = at - g_last_instruction;
	g_last_instruction = at;
	if (g_profiling)
	{
		const uint32_t current = g_stack.empty() ? 0 : g_stack.back();
		g_costs[current].self += spent;
		std::set<uint32_t> counted;
		for (uint32_t function : g_stack)
			if (counted.insert(function).second)
				g_costs[function].inclusive += spent;
		if (g_stack.empty())
			g_costs[0].inclusive += spent;
	}
	// how the previous instruction left its function
	if (g_acknowledged)
	{
		g_acknowledged = 0;
		g_stack.push_back(pc);
		if (g_profiling)
			g_costs[pc].calls++;
	}
	else if (g_previous_pc != 0xFFFFFFFF && is_call(g_previous_pc))
	{
		g_stack.push_back(pc);
		if (g_profiling)
			g_costs[pc].calls++;
	}
	else if (g_previous_pc != 0xFFFFFFFF && is_return(g_previous_pc) && !g_stack.empty())
		g_stack.pop_back();
	if (g_stack.size() > 256)
		g_stack.erase(g_stack.begin());    // (a stack the program left without returning)
}

void on_instruction(offs_t pc)
{
	g_instructions++;
	mark(g_executed, pc);
	if (g_previous_pc != 0xFFFFFFFF && is_call(g_previous_pc))
		mark(g_called, pc);
	if (g_previous_pc != 0xFFFFFFFF && is_computed_jump(g_previous_pc))
		g_jumps.emplace(g_previous_pc, pc & 0xFFFFFF);
	profile(pc);
	if (pc == g_step_address)
	{
		if (!g_steps.empty() && g_steps.back().frames == 0)
			g_steps.back().frames = g_frame - g_steps.back().frame;
		g_steps.push_back({ g_frame, 0, 0 });
		g_step_start = now();
		g_working = true;
	}
	else if (pc == g_idle_address && g_working)
	{
		g_steps.back().work = (now() - g_step_start) / MD_CPU_DIVIDER;
		g_working = false;
	}
	g_previous_pc = pc;
}

// ---- output ----

void write_addresses(const std::string &path, const std::vector<uint8_t> &bits, const char *title)
{
	std::ofstream out(path);
	out << "# " << title << ", one per line\n";
	for (uint32_t address = 0; address < (1u << 24); address++)
		if (bits[address >> 3] & (1u << (address & 7)))
		{
			char line[16];
			std::snprintf(line, sizeof(line), "%06X\n", address);
			out << line;
		}
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

std::vector<uint32_t> g_picture(size_t(MD_VDP_WIDTH) * MD_VDP_HEIGHT);

void write_bmp(const std::string &path)
{
	const int row = (MD_VDP_WIDTH * 3 + 3) & ~3;
	const uint32_t size = 54 + uint32_t(row) * MD_VDP_HEIGHT;
	std::vector<uint8_t> file(size, 0);
	auto put32 = [&](size_t at, uint32_t value) { for (int byte = 0; byte < 4; byte++) file[at + byte] = uint8_t(value >> (8 * byte)); };
	file[0] = 'B';
	file[1] = 'M';
	put32(2, size);
	put32(10, 54);
	put32(14, 40);
	put32(18, uint32_t(MD_VDP_WIDTH));
	put32(22, uint32_t(MD_VDP_HEIGHT));
	file[26] = 1;
	file[28] = 24;
	for (int y = 0; y < MD_VDP_HEIGHT; y++)
		for (int x = 0; x < MD_VDP_WIDTH; x++)
		{
			const uint32_t pixel = g_picture[size_t(y) * MD_VDP_WIDTH + size_t(x)];
			uint8_t *out = &file[54 + size_t(MD_VDP_HEIGHT - 1 - y) * size_t(row) + size_t(x) * 3];
			out[0] = uint8_t(pixel);
			out[1] = uint8_t(pixel >> 8);
			out[2] = uint8_t(pixel >> 16);
		}
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(file.data()), std::streamsize(file.size()));
}

uint64_t picture_hash()
{
	uint64_t hash = 0xcbf29ce484222325ull;
	const uint8_t *bytes = reinterpret_cast<const uint8_t *>(g_picture.data());
	for (size_t index = 0; index < g_picture.size() * sizeof(uint32_t); index++)
		hash = (hash ^ bytes[index]) * 0x100000001b3ull;
	return hash;
}

// The board's memories at the end of a frame, a file each: DIR/FRAME_work_ram.bin...
void dump_frame(const std::string &folder, uint64_t frame)
{
	const struct { const char *name; const void *data; size_t size; } memories[] = {
		{ "work_ram", md_board.work_ram, sizeof(md_board.work_ram) },
		{ "z80_ram", md_board.z80_ram, sizeof(md_board.z80_ram) },
		{ "vram", md_board.vdp.vram, sizeof(md_board.vdp.vram) },
		{ "cram", md_board.vdp.cram, sizeof(md_board.vdp.cram) },
		{ "vsram", md_board.vdp.vsram, sizeof(md_board.vdp.vsram) },
		{ "vdp_regs", md_board.vdp.regs, sizeof(md_board.vdp.regs) },
	};
	for (const auto &memory : memories)
	{
		std::ofstream out(folder + "/" + std::to_string(frame) + "_" + memory.name + ".bin", std::ios::binary);
		out.write(reinterpret_cast<const char *>(memory.data), std::streamsize(memory.size));
	}
}

void write_profile(const std::string &path)
{
	std::vector<std::pair<uint32_t, function_cost>> rows(g_costs.begin(), g_costs.end());
	std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.second.self > b.second.self; });
	std::ofstream out(path);
	out << "# 68000 cycles per function by mdref, the most expensive first: ADDRESS SELF INCLUSIVE CALLS\n"
		<< "# (000000: outside any function the profile saw entered)\n";
	for (const auto &row : rows)
	{
		char line[96];
		std::snprintf(line, sizeof(line), "%06X %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", row.first,
			row.second.self / MD_CPU_DIVIDER, row.second.inclusive / MD_CPU_DIVIDER, row.second.calls);
		out << line;
	}
}

uint32_t parse_address(const char *text)
{
	return uint32_t(std::strtoul(text, nullptr, 16)) & 0xFFFFFF;
}

} // namespace

int main(int argc, char **argv)
{
	const char *usage =
		"usage: mdref [--frames N] [--script FILE] [--rom FOLDER] [--hashes FILE] [--trace FILE] [--calls FILE]"
		" [--jumps FILE] [--rom-reads FILE] [--z80-rom-reads FILE] [--dump FRAME ... --dump-dir DIR] [--video-hashes FILE] [--video-dump FRAME ... --video-dir DIR]"
		" [--sound-hashes FILE] [--sound-wav FILE] [--sound-trace FILE] [--step ADDRESS --idle ADDRESS [--steps FILE]]"
		" [--profile FILE [--profile-from FRAME]] [--no-refresh]\n";
	std::string rom_folder = "rom";
	std::string hashes_path, trace_path, calls_path, jumps_path, dump_folder = ".", video_hashes_path, video_folder = ".";
	std::string sound_hashes_path, sound_wav_path, sound_trace_path, steps_path, profile_path, script_path;
	std::string rom_reads_path, z80_rom_reads_path;
	std::set<uint64_t> dump_frames, video_dump_frames;
	uint64_t frames = 600, profile_from = 0;
	bool frames_given = false, refresh = true;

	for (int index = 1; index < argc; index++)
	{
		const std::string argument = argv[index];
		const bool has_value = index + 1 < argc;
		if (argument == "--frames" && has_value)
		{
			frames = std::strtoull(argv[++index], nullptr, 10);
			frames_given = true;
		}
		else if (argument == "--script" && has_value)
			script_path = argv[++index];
		else if (argument == "--rom" && has_value)
			rom_folder = argv[++index];
		else if (argument == "--hashes" && has_value)
			hashes_path = argv[++index];
		else if (argument == "--trace" && has_value)
			trace_path = argv[++index];
		else if (argument == "--calls" && has_value)
			calls_path = argv[++index];
		else if (argument == "--jumps" && has_value)
			jumps_path = argv[++index];
		else if (argument == "--rom-reads" && has_value)
			rom_reads_path = argv[++index];
		else if (argument == "--z80-rom-reads" && has_value)
			z80_rom_reads_path = argv[++index];
		else if (argument == "--dump" && has_value)
			while (index + 1 < argc && argv[index + 1][0] != '-')
				dump_frames.insert(std::strtoull(argv[++index], nullptr, 10));
		else if (argument == "--dump-dir" && has_value)
			dump_folder = argv[++index];
		else if (argument == "--video-hashes" && has_value)
			video_hashes_path = argv[++index];
		else if (argument == "--video-dump" && has_value)
			while (index + 1 < argc && argv[index + 1][0] != '-')
				video_dump_frames.insert(std::strtoull(argv[++index], nullptr, 10));
		else if (argument == "--video-dir" && has_value)
			video_folder = argv[++index];
		else if (argument == "--sound-hashes" && has_value)
			sound_hashes_path = argv[++index];
		else if (argument == "--sound-wav" && has_value)
			sound_wav_path = argv[++index];
		else if (argument == "--sound-trace" && has_value)
			sound_trace_path = argv[++index];
		else if (argument == "--step" && has_value)
			g_step_address = parse_address(argv[++index]);
		else if (argument == "--idle" && has_value)
			g_idle_address = parse_address(argv[++index]);
		else if (argument == "--steps" && has_value)
			steps_path = argv[++index];
		else if (argument == "--profile" && has_value)
			profile_path = argv[++index];
		else if (argument == "--profile-from" && has_value)
			profile_from = std::strtoull(argv[++index], nullptr, 10);
		else if (argument == "--no-refresh")
			refresh = false;
		else
		{
			std::cerr << usage;
			return 1;
		}
	}

	if (const char *problem = game_rom_set_load(rom_folder.c_str()))
	{
		std::cerr << "The cartridge in " << rom_folder << " cannot be read: " << problem << '\n';
		return 1;
	}
	md_insert(game_program, GAME_PROGRAM_SIZE);
	md_board.hooks.now = now;
	md_board.hooks.stall = stall;
	md_board.hooks.sound_sync = sound_sync;
	md_board.hooks.data_read = data_read;
	md_power_on();
	md_board.refresh = refresh ? 1 : 0;

	md_script *script = nullptr;
	if (!script_path.empty())
	{
		char error[256];
		script = md_script_load(script_path.c_str(), error, sizeof(error));
		if (!script)
		{
			std::cerr << error << '\n';
			return 1;
		}
		if (!frames_given && md_script_frames(script))
			frames = md_script_frames(script);
		md_script_apply(script, 0);
	}

	reference_cpu cpu;
	g_cpu = &cpu;
	for (int space : { int(AS_PROGRAM), int(AS_OPCODES) })
	{
		cpu.space(space).read16 = bus_read;
		cpu.space(space).write16 = bus_write;
	}
	cpu.space(AS_OPCODES).instructions = true;
	cpu.space(m68000_base_device::AS_CPU_SPACE).read16 = acknowledge;
	cpu.space(m68000_base_device::AS_CPU_SPACE).write16 = bus_write;
	cpu.instruction_hook = on_instruction;
	cpu.log_handler = [](const std::string &message) { std::cerr << message; };
	cpu.start();
	cpu.reset();

	std::ofstream hashes, video_hashes, sound_hashes;
	if (!hashes_path.empty())
		hashes.open(hashes_path);
	if (!video_hashes_path.empty())
		video_hashes.open(video_hashes_path);
	const bool video = video_hashes.is_open() || !video_dump_frames.empty();
	if (!sound_hashes_path.empty())
		sound_hashes.open(sound_hashes_path);
	std::vector<int16_t> sound_mix;
	g_sound = sound_hashes.is_open() || !sound_wav_path.empty() || !sound_trace_path.empty() || !z80_rom_reads_path.empty();
	g_record_rom_reads = !rom_reads_path.empty() || !z80_rom_reads_path.empty();
	if (g_sound)
		ms_attach(0);

	int status = 0;
	uint64_t frame = 0;
	try
	{
		for (frame = 1; frame <= frames; frame++)
		{
			const uint64_t base = (frame - 1) * MD_FRAME_CLOCKS;
			g_frame = frame;
			g_profiling = !profile_path.empty() && frame > profile_from;
			for (unsigned line = 0; line < MD_LINES; line++)
			{
				const uint64_t time = base + uint64_t(line) * MD_LINE_CLOCKS;
				run_until(cpu, time);
				if (g_sound)
					ms_run_until(time);
				md_line_start(line);
				sync_irq();
				if (video && line < MD_VDP_HEIGHT)
					md_vdp_render_line(&md_board.vdp, line, &g_picture[size_t(line) * MD_VDP_WIDTH]);
				if (line != MD_ACTIVE_LINES)
					continue;

				// VBLANK: the frame ends
				if (g_working)
					g_overruns++;
				md_script_apply(script, frame);
				if (g_sound)
				{
					ms_end_frame(time);
					if (sound_hashes.is_open())
					{
						char text[64];
						std::snprintf(text, sizeof(text), "%" PRIu64 " %016" PRIx64 " %016" PRIx64 "\n", frame,
							ms_frame_samples_hash(), ms_state_hash());
						sound_hashes << text;
					}
					if (!sound_wav_path.empty())
					{
						size_t count = 0;
						const int16_t *mix = ms_frame_mix(&count);
						sound_mix.insert(sound_mix.end(), mix, mix + count);
					}
				}
				if (hashes.is_open())
				{
					char text[48];
					std::snprintf(text, sizeof(text), "%" PRIu64 " %016" PRIx64 "\n", frame, md_state_hash());
					hashes << text;
				}
				if (dump_frames.count(frame))
					dump_frame(dump_folder, frame);
				if (video_hashes.is_open())
				{
					char text[48];
					std::snprintf(text, sizeof(text), "%" PRIu64 " %016" PRIx64 "\n", frame, picture_hash());
					video_hashes << text;
				}
				if (video_dump_frames.count(frame))
					write_bmp(video_folder + "/frame_" + std::to_string(frame) + ".bmp");
			}
		}
		frame--;
	}
	catch (const std::exception &error)
	{
		std::cerr << "68000 stopped at PC " << std::hex << cpu.pc() << std::dec << " in frame " << frame << ": "
			<< error.what() << '\n';
		status = 2;
	}

	size_t unique = 0;
	for (uint8_t bits : g_executed)
		for (; bits; bits &= uint8_t(bits - 1))
			unique++;
	const double frame_cycles = double(MD_FRAME_CLOCKS) / MD_CPU_DIVIDER;
	std::cout << "frames:            " << frame << '\n'
		<< "instructions:      " << g_instructions << '\n'
		<< "unique addresses:  " << unique << '\n'
		<< "final PC:          " << std::hex << cpu.pc() << std::dec << '\n'
		<< "DMA stall cycles:  " << md_board.vdp.dma_stall_clocks / MD_CPU_DIVIDER << '\n'
		<< "FIFO stall cycles: " << md_board.vdp.fifo_stall_clocks / MD_CPU_DIVIDER << '\n'
		<< "refresh cycles:    " << md_board.refresh_cycles << '\n'
		<< "Z80 bus accesses:  " << md_board.z80_bus_accesses << '\n';
	if (g_step_address != 0xFFFFFFFF)
	{
		// a step that took up to kGameplaySpan frames is the game's; a longer one is a screen with a loop of
		// its own (a menu) called from the main loop
		constexpr uint64_t kGameplaySpan = 8;
		uint64_t spans[6] = {}, lost = 0, most = 0, over = 0;
		for (const step_record &step : g_steps)
		{
			if (step.frames > kGameplaySpan)
				continue;
			spans[std::min<uint64_t>(step.frames, 5)]++;
			lost += step.frames > 1 ? step.frames - 1 : 0;
			most = std::max(most, step.work);
			over += step.work > uint64_t(frame_cycles) ? 1 : 0;
		}
		std::cout << "steps:             " << g_steps.size() << '\n'
			<< "steps by frames until the next (0/1/2/3/4/5-8): " << spans[0] << '/' << spans[1] << '/' << spans[2]
			<< '/' << spans[3] << '/' << spans[4] << '/' << spans[5] << '\n'
			<< "frames lost:       " << lost << " (frames the game did not advance)\n"
			<< "steps over a frame: " << over << '\n'
			<< "VBLANKs mid-step:  " << g_overruns << '\n'
			<< "heaviest step:     " << most << " cycles (" << int(100.0 * double(most) / frame_cycles + 0.5)
			<< "% of a frame)\n";
		if (!steps_path.empty())
		{
			std::ofstream out(steps_path);
			out << "# the main loop's steps by mdref: START_FRAME FRAMES WORK (68000 cycles; a frame is "
				<< int(frame_cycles) << ")\n";
			for (const step_record &step : g_steps)
				out << step.frame << ' ' << step.frames << ' ' << step.work << '\n';
		}
	}
	if (!profile_path.empty())
		write_profile(profile_path);
	if (!trace_path.empty())
		write_addresses(trace_path, g_executed, "68000 addresses executed by mdref");
	if (!rom_reads_path.empty())
		write_reads(rom_reads_path, g_rom_reads, "68000 cartridge bytes read as data by mdref", 6);
	if (!z80_rom_reads_path.empty())
		write_reads(z80_rom_reads_path, g_z80_rom_reads, "cartridge bytes the Z80 read as data by mdref (Z80 PCs)", 4);
	if (!calls_path.empty())
		write_addresses(calls_path, g_called, "68000 addresses reached by jsr or bsr");
	if (!jumps_path.empty())
	{
		std::ofstream out(jumps_path);
		out << "# where each 68000 jmp or jsr through a register went, by mdref: FROM TO\n";
		for (const auto &jump : g_jumps)
		{
			char text[24];
			std::snprintf(text, sizeof(text), "%06X %06X\n", jump.first, jump.second);
			out << text;
		}
	}
	if (g_sound)
		std::cout << "Z80 cycles:        " << ms_z80_cycles() << '\n';
	if (!sound_wav_path.empty())
		write_wav(sound_wav_path, sound_mix, ms_mix_rate());
	if (!sound_trace_path.empty())
	{
		std::ofstream out(sound_trace_path);
		out << "# Z80 addresses executed by mdref, one per line\n";
		for (uint32_t address = 0; address < 0x10000; address++)
			if (ms_executed()[address >> 3] & (1u << (address & 7)))
			{
				char text[16];
				std::snprintf(text, sizeof(text), "%04X\n", address);
				out << text;
			}
	}
	md_script_free(script);
	return status;
}
