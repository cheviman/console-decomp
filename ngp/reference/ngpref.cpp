/**
 *    Name:        ngpref.cpp
 *    Description: the reference: a Neo Geo Pocket Color game's original program, with the console's BIOS, on
 *                 MAME's cores (TMP95C061, Z80, T6W28, K2GE), frame by frame from a controls script
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    26-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The console is MAME 0.289's ngp.cpp (mame/, BSD-3-Clause, Wilbert Pol) without MAME's device framework:
 *    its memory map, its I/O at 000080-0000BF, its real-time clock, its flash cartridge (flash.hxx, MAME's
 *    flash_w as it is) and the way it wires the chips: the TMP95C061 (tlcs900/mame) runs the BIOS and the
 *    game, the K2GE (video/k2gevideo) raises INT4 at VBLANK and pulses TI0 at every HBLANK, the Z80, the T6W28
 *    and the DACs are the sound side (sound/ngpsound), and timer 3's output (TO3, port A) raises the Z80's IRQ.
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
 */
#include "emu.h"
#include "tmp95c061.h"

#include "game/data.h"
#include "game/extract.h"
#include "ngp/script.h"
#include "ngpsound.h"
#include "k2gevideo.h"

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
#include <vector>

namespace
{

// MAME's ngp.cpp, lines 122-133: the flash's command states, as flash.hxx names them
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

// What of MAME's ngp_state flash_w uses
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

// ---- the console's timing (MAME's ngp.cpp: 6.144 MHz, the screen's set_raw 515 x 199) ----

constexpr uint64_t kClock = 6144000;
constexpr uint64_t kLine = 515;
constexpr uint64_t kFrame = kLine * 199;
constexpr uint64_t kVblankLine = 152;
constexpr uint64_t kCartridgeSpace = 0x400000;     // MAME's ngp.cpp allocates 4 MB for both flash chips

// ---- the TMP95C061, with what the reference needs of it ----

class reference_cpu : public tmp95c061_device
{
public:
	explicit reference_cpu(const machine_config &config) : tmp95c061_device(config, "maincpu", nullptr, uint32_t(kClock)) {}

	uint32_t pc() const { return m_pc.d; }

	// Where MAME's ngp.cpp starts the BIOS when the NVRAM was loaded (machine_reset: set_state_int(TLCS900_PC))
	void set_pc(uint32_t address)
	{
		m_pc.d = address;
		m_prefetch_clear = true;
	}

	// The instruction at 'pc', as MAME's dasm900 writes it, read from the bus without side effects
	std::string disassemble(uint32_t pc, uint8_t (*read)(offs_t))
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
		m_disassembler->disassemble(text, pc, bytes, bytes);
		return text.str();
	}

private:
	std::unique_ptr<util::disasm_interface> m_disassembler;
};

machine_config g_config;
reference_cpu *g_cpu = nullptr;

// ---- the console's memories and I/O (MAME's ngp_state) ----

uint8_t g_main_ram[0x3000];            // 004000-006FFF
uint8_t g_sound_ram[0x1000];           // 007000-007FFF, the Z80's 0000-0FFF
uint8_t g_io_reg[0x40];                // 000080-0000BF
std::vector<uint8_t> g_cartridge(kCartridgeSpace, 0);
ngp_state g_flash;
uint64_t g_flash_writes = 0;
uint8_t g_old_to3 = 0;
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

// ---- the real-time clock (MAME's device_rtc_interface as ngp.cpp uses it) ----

struct rtc_clock
{
	int year = 2000, month = 1, day = 1, hour = 0, minute = 0, second = 0;
} g_rtc;
uint64_t g_next_second = kClock;

uint8_t to_bcd(int value)
{
	return uint8_t(((value / 10) % 10) << 4 | (value % 10));
}

int from_bcd(uint8_t value)
{
	return (value >> 4) * 10 + (value & 0x0f);
}

int days_in_month(int month, int year)
{
	static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	return month == 2 && leap ? 29 : days[(month - 1) % 12];
}

void advance_second()
{
	if (++g_rtc.second < 60)
		return;
	g_rtc.second = 0;
	if (++g_rtc.minute < 60)
		return;
	g_rtc.minute = 0;
	if (++g_rtc.hour < 24)
		return;
	g_rtc.hour = 0;
	if (++g_rtc.day <= days_in_month(g_rtc.month, g_rtc.year))
		return;
	g_rtc.day = 1;
	if (++g_rtc.month <= 12)
		return;
	g_rtc.month = 1;
	g_rtc.year++;
}

// ---- 000080-0000BF (MAME's ngp_state::io_r, io_w) ----

uint8_t io_r(offs_t offset)
{
	uint8_t data = g_io_reg[offset];
	switch (offset)
	{
	case 0x11: return to_bcd(g_rtc.year % 100);
	case 0x12: return to_bcd(g_rtc.month);
	case 0x13: return to_bcd(g_rtc.day);
	case 0x14: return to_bcd(g_rtc.hour);
	case 0x15: return to_bcd(g_rtc.minute);
	case 0x16: return to_bcd(g_rtc.second);
	case 0x30:                                          // the pad
		return uint8_t(g_controls & 0x7f);
	case 0x31:                                          // the power button, and the sub-battery always fine
		data = (g_controls & NGP_POWER) ? 0x01 : 0x00;
		data |= 0x02;
		break;
	}
	return data;
}

void io_w(offs_t offset, uint8_t data)
{
	switch (offset)
	{
	case 0x11: g_rtc.year = from_bcd(data); break;
	case 0x12: g_rtc.month = from_bcd(data); break;
	case 0x13: g_rtc.day = from_bcd(data); break;
	case 0x14: g_rtc.hour = from_bcd(data); break;
	case 0x15: g_rtc.minute = from_bcd(data); break;
	case 0x16: g_rtc.second = from_bcd(data); break;
	case 0x20:                                          // the T6W28, "right" and "left": MAME writes both to port 0
	case 0x21:
		if (g_io_reg[0x38] == 0x55 && g_io_reg[0x39] == 0xAA)
			ns_psg_write(now(), data);
		break;
	case 0x22:
		ns_dac_write(now(), 0, data);
		break;
	case 0x23:
		ns_dac_write(now(), 1, data);
		break;
	case 0x38:                                          // sound on (55) or off (AA)
		if (data == 0x55)
			ns_psg_enable(now(), 1);
		else if (data == 0xAA)
			ns_psg_enable(now(), 0);
		break;
	case 0x39:                                          // the Z80 on (55) or off (AA)
		if (data == 0x55)
			ns_z80_start(now());
		else if (data == 0xAA)
			ns_z80_stop(now());
		break;
	case 0x3a:
		ns_z80_nmi(now());
		break;
	}
	g_io_reg[offset] = data;
}

// ---- the TMP95C061's bus (MAME's ngp_state::main_mem; an 8-bit bus: set_am8_16(1)) ----

std::vector<uint8_t> g_executed(size_t(1) << 21, 0);  // 16 MB of addresses, a bit each
uint64_t g_instructions = 0;

uint8_t bus_r8(offs_t address)
{
	address &= 0xffffff;
	if (address < 0x80)
	{
		const address_map::entry *entry = g_internal[address];
		return entry && entry->read ? entry->read(address - entry->start) : 0;
	}
	if (address < 0xc0)
		return io_r(address - 0x80);
	if (address >= 0x4000 && address < 0x7000)
		return g_main_ram[address - 0x4000];
	if (address >= 0x7000 && address < 0x8000)
		return g_sound_ram[address - 0x7000];
	if (address >= 0x8000 && address < 0xc000)
		return nv_read(address - 0x8000);
	if (address >= 0x200000 && address < 0x400000)
		return g_cartridge[address - 0x200000];
	if (address >= 0x800000 && address < 0xa00000)
		return g_cartridge[address - 0x800000 + 0x200000];
	if (address >= 0xff0000)
		return game_bios[address - 0xff0000];
	return 0;
}

void bus_w8(offs_t address, uint8_t data)
{
	address &= 0xffffff;
	if (address < 0x80)
	{
		const address_map::entry *entry = g_internal[address];
		if (entry && entry->write)
			entry->write(address - entry->start, data);
	}
	else if (address < 0xc0)
		io_w(address - 0x80, data);
	else if (address >= 0x4000 && address < 0x7000)
		g_main_ram[address - 0x4000] = data;
	else if (address >= 0x7000 && address < 0x8000)
		g_sound_ram[address - 0x7000] = data;
	else if (address >= 0x8000 && address < 0xc000)
		nv_write(address - 0x8000, data);
	else if (address >= 0x200000 && address < 0x400000)
	{
		g_flash_writes++;
		g_flash.flash_w<0>(address - 0x200000, data);
	}
	else if (address >= 0x800000 && address < 0xa00000)
	{
		g_flash_writes++;
		g_flash.flash_w<1>(address - 0x800000, data);
	}
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

// ---- the lines between the chips ----

void vblank_pin(int state)
{
	g_cpu->set_input_line(TLCS900_INT4, state ? ASSERT_LINE : CLEAR_LINE);
}

void hblank_pin(int state)
{
	g_cpu->set_input_line(TLCS900_TIO, state ? ASSERT_LINE : CLEAR_LINE);
}

// Port A: TO3 rising raises the Z80's IRQ (MAME's ngp_state::tlcs900_porta)
void port_a(uint8_t data)
{
	const uint8_t to3 = BIT(data, 3);
	if (to3 && !g_old_to3)
		ns_z80_irq(now());
	g_old_to3 = to3;
}

// The power button: an NMI when the BIOS lets it (MAME's ngp_state::power_callback)
void power_changed(bool pressed)
{
	if (BIT(g_io_reg[0x33], 2))
		g_cpu->set_input_line(TLCS900_NMI, pressed ? CLEAR_LINE : ASSERT_LINE);
}

uint8_t comm_read()
{
	return g_io_reg[0x3c];
}

void comm_write(uint8_t data)
{
	g_io_reg[0x3c] = data;
}

void int5(int state)
{
	g_cpu->set_input_line(TLCS900_INT5, state ? ASSERT_LINE : CLEAR_LINE);
}

void script_poke(void *context, uint32_t address, uint8_t value)
{
	(void)context;
	if (address >= 0x4000 && address < 0x7000)
		g_main_ram[address - 0x4000] = value;
	else if (address >= 0x7000 && address < 0x8000)
		g_sound_ram[address - 0x7000] = value;
}

// ---- running ----

std::ofstream g_log;                  // --log: every instruction of the frames --log-frames names
uint64_t g_log_first = 0, g_log_last = 0;
uint64_t g_frame = 0;

void on_instruction(offs_t pc)
{
	pc &= 0xffffff;
	g_executed[pc >> 3] |= uint8_t(1u << (pc & 7));
	g_instructions++;
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
void run_until(uint64_t time)
{
	for (;;)
	{
		const uint64_t next = std::min({ nv_next_event(), g_next_second, time });
		run_cpu_until(next);
		ns_run_until(next);
		g_in_event = true;
		nv_fire(next, &g_event_time);
		if (g_next_second <= next)
		{
			g_event_time = g_next_second;
			advance_second();
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

// The console's state: the TMP95C061's registers, its memories, the K2GE's, the I/O, the flash's save area
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
	hash = fnv(hash, g_main_ram, sizeof(g_main_ram));
	hash = fnv(hash, g_sound_ram, sizeof(g_sound_ram));
	hash = fnv(hash, g_io_reg, sizeof(g_io_reg));
	hash = fnv(hash, nv_memory(), 0x4000);
	hash = fnv(hash, &g_cartridge[0x1f8000], 0x4000);
	hash = fnv(hash, &g_flash_writes, sizeof(g_flash_writes));
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

// The console's memories at the end of a frame, a file each: DIR/FRAME_main_ram.bin...
void dump_frame(const std::string &folder, uint64_t frame)
{
	const struct { const char *name; const void *data; size_t size; } memories[] = {
		{ "main_ram", g_main_ram, sizeof(g_main_ram) },
		{ "sound_ram", g_sound_ram, sizeof(g_sound_ram) },
		{ "io", g_io_reg, sizeof(g_io_reg) },
		{ "k2ge", nv_memory(), 0x4000 },
	};
	for (const auto &memory : memories)
	{
		std::ofstream out(folder + "/" + std::to_string(frame) + "_" + memory.name + ".bin", std::ios::binary);
		out.write(reinterpret_cast<const char *>(memory.data), std::streamsize(memory.size));
	}
}

// MAME's ngpc.nv: the work RAM (004000-006FFF) and the first 32 I/O bytes (000080-00009F)
constexpr size_t kNvramSize = 0x3000 + 0x20;

bool read_nvram(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	std::vector<char> data(kNvramSize);
	if (!in.read(data.data(), std::streamsize(kNvramSize)))
		return false;
	std::memcpy(g_main_ram, data.data(), 0x3000);
	std::memcpy(g_io_reg, data.data() + 0x3000, 0x20);
	return true;
}

void write_nvram(const std::string &path)
{
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(g_main_ram), 0x3000);
	out.write(reinterpret_cast<const char *>(g_io_reg), 0x20);
}

// YYYYMMDDhhmmss
bool parse_date(const char *text, rtc_clock &clock)
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
		" [--rtc YYYYMMDDhhmmss] [--hashes FILE] [--trace FILE] [--dump FRAME ... --dump-dir DIR]"
		" [--video-hashes FILE] [--video-dump FRAME ... --video-dir DIR] [--sound-hashes FILE] [--sound-wav FILE]"
		" [--sound-trace FILE] [--log FILE [--log-frames FIRST LAST]]\n";
	std::string rom_folder = "rom", bios_folder, nvram_path, save_nvram_path, script_path;
	std::string hashes_path, trace_path, dump_folder = ".", video_hashes_path, video_folder = ".";
	std::string sound_hashes_path, sound_wav_path, sound_trace_path, log_path;
	std::set<uint64_t> dump_frames, video_dump_frames;
	uint64_t frames = 600;
	bool frames_given = false;

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
			if (!parse_date(argv[++index], g_rtc))
			{
				std::cerr << "--rtc takes a date and time as YYYYMMDDhhmmss\n";
				return 1;
			}
		}
		else if (option == "--hashes" && has_value)
			hashes_path = argv[++index];
		else if (option == "--trace" && has_value)
			trace_path = argv[++index];
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

	// The cartridge (MAME's load_ngp_cart and machine_start): a 2 MB one is flash chip 0, a Toshiba 16 Mbit
	std::memcpy(g_cartridge.data(), game_program, GAME_PROGRAM_SIZE);
	{
		auto &chip = g_flash.m_flash_chip[0];
		chip.present = true;
		chip.manufacturer_id = 0x98;
		chip.device_id = GAME_PROGRAM_SIZE <= 0x80000 ? 0xab : GAME_PROGRAM_SIZE == 0x100000 ? 0x2c : 0x2f;
		chip.state = F_READ;
		for (int chip_index = 0; chip_index < 2; chip_index++)
		{
			auto &each = g_flash.m_flash_chip[chip_index];
			each.data = g_cartridge.data() + size_t(chip_index) * 0x200000;
			const size_t places[4] = { 0, 0x7c000, 0xfc000, 0x1fc000 };
			for (int place = 0; place < 4; place++)
				for (int byte = 0; byte < 4; byte++)
					each.org_data[place * 4 + byte] = each.data[places[place] + size_t(byte)];
		}
	}

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
	cpu.porta_write().set([](offs_t, uint8_t data) { port_a(data); });
	cpu.an_read<0>().set([] { return uint16_t(0x3ff); });
	cpu.instruction_hook = on_instruction;
	cpu.log_handler = [](const std::string &message) { std::cerr << message; };
	cpu.start();
	for (const address_map::entry &entry : cpu.internal_map().m_entries)
		for (offs_t address = entry.start; address <= entry.end && address < 0x80; address++)
			g_internal[address] = &entry;

	static const ns_host sound_host = { comm_read, comm_write, int5 };
	ns_attach(0, g_sound_ram, &sound_host);
	nv_attach(now, vblank_pin, hblank_pin);

	// MAME's machine_reset: the CPU from its reset vector, or the BIOS's restart when the NVRAM was loaded
	cpu.reset();
	g_old_to3 = 0;
	if (nvram_loaded)
		cpu.set_pc(0xff1800);
	bool power = (g_controls & NGP_POWER) != 0;

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
		const bool pressed = (g_controls & NGP_POWER) != 0;
		if (pressed != power)
		{
			power = pressed;
			power_changed(pressed);
		}
		ns_end_frame(time);
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
		<< "flash writes:      " << g_flash_writes << '\n'
		<< "Z80 cycles:        " << ns_z80_cycles() << '\n';
	if (!trace_path.empty())
		write_addresses(trace_path, g_executed, 1u << 24, 6, "TMP95C061 addresses executed by ngpref");
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
