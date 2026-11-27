/**
 *    Name:        emu.h
 *    Description: minimal stand-in for MAME's emu.h, enough to build MAME's TLCS-900/H core, its TMP95C061
 *                 and its disassembler alone
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    26-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Only what tlcs900.cpp, tmp95c061.cpp, dasm900.cpp and their .hxx tables use; arcade-decomp's stand-in for
 *    the V60 (system32/reference/stub/emu.h) is where it starts from. Memory goes through function pointers the
 *    host sets; the TMP95C061's internal registers (its address_map at 000000-00007F) are built into a table
 *    the host reads and writes through (cpu_device::internal_read, internal_write); cpu_device adds a small
 *    public API (start, reset, run, set_input_line, registers).
 */
#pragma once

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using offs_t = uint32_t;

#define ATTR_COLD
#define NAME(x) x, #x
#define FUNC(x) &x, #x
#define DECLARE_DEVICE_TYPE(Type, Class) extern const device_type Type;
#define DEFINE_DEVICE_TYPE(Type, Class, ShortName, FullName) const device_type Type = ShortName;

enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };
enum { INPUT_LINE_NMI = 32 };
enum { AS_PROGRAM = 0, AS_IO = 1 };
enum { STATE_GENPC = -1, STATE_GENPCBASE = -2, STATE_GENFLAGS = -3 };
enum endianness_t { ENDIANNESS_LITTLE, ENDIANNESS_BIG };

// MAME's PAIR (emucore.h), for a little-endian host: a 32-bit register seen as bytes, words or the whole
static_assert(sizeof(void *) == 8, "64-bit hosts only");
union PAIR
{
	struct { u8 l, h, h2, h3; } b;
	struct { u16 l, h; } w;
	struct { s8 l, h, h2, h3; } sb;
	struct { s16 l, h; } sw;
	u32 d;
	s32 sd;
};

// The CPU core calls this on conditions it cannot continue from; the host catches it
[[noreturn]] inline void fatalerror(const char *format, ...)
{
	char message[512];
	va_list arguments;
	va_start(arguments, format);
	std::vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	throw std::runtime_error(message);
}

namespace util {

// An argument as printf takes it: MAME's formatter also accepts std::string, which printf does not
template <typename T>
T format_argument(T value)
{
	return value;
}

inline const char *format_argument(const std::string &value)
{
	return value.c_str();
}

// printf-style formatting into a stream, like MAME's util::stream_format
template <typename... Args>
void stream_format(std::ostream &stream, const char *format, Args... args)
{
	char buffer[256];
	std::snprintf(buffer, sizeof(buffer), format, format_argument(args)...);
	stream << buffer;
}

// A text with nothing to put in it, as it is
inline void stream_format(std::ostream &stream, const char *text)
{
	stream << text;
}

class disasm_interface
{
public:
	enum : u32
	{
		LENGTHMASK = 0x0000ffff,
		STEP_COND = 0x10000000,
		STEP_OVER = 0x20000000,
		STEP_OUT = 0x40000000,
		SUPPORTED = 0x80000000
	};

	// Little-endian reads from a flat image; addresses wrap with a mask, like a mirrored bus
	class data_buffer
	{
	public:
		data_buffer(const u8 *data, u32 mask) : m_data(data), m_mask(mask) {}
		u8 r8(offs_t address) const { return m_data[address & m_mask]; }
		u16 r16(offs_t address) const { return u16(r8(address) | (r8(address + 1) << 8)); }
		u32 r32(offs_t address) const { return u32(r16(address)) | (u32(r16(address + 2)) << 16); }

	private:
		const u8 *m_data;
		u32 m_mask;
	};

	virtual ~disasm_interface() = default;
	virtual u32 opcode_alignment() const = 0;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) = 0;
};

} // namespace util

template <typename... Args>
std::string string_format(const char *format, Args... args)
{
	char buffer[512];
	std::snprintf(buffer, sizeof(buffer), format, util::format_argument(args)...);
	return buffer;
}

// A text with nothing to put in it, as it is
inline std::string string_format(const char *text)
{
	return text;
}

namespace util {
using ::string_format;
}

// make_bitmask, BIT(x, n) and BIT(x, n, w) from MAME's coretmpl.h
template <typename T, typename U>
constexpr T make_bitmask(U n) noexcept
{
	return T((n < U(8 * sizeof(T))) ? ((std::make_unsigned_t<T>(1) << n) - 1) : ~std::make_unsigned_t<T>(0));
}

template <typename T, typename U>
constexpr T BIT(T x, U n) noexcept
{
	return (x >> n) & T(1);
}

template <typename T, typename U, typename V>
constexpr T BIT(T x, U n, V w)
{
	return (x >> n) & make_bitmask<T>(w);
}

class machine_config {};
class device_t;

// The machine as a device sees it: only whether a read is a look (a debugger's, the host's) that must change
// nothing
class running_machine
{
public:
	bool side_effects_disabled() const { return m_side_effects_disabled; }
	bool m_side_effects_disabled = false;
};
inline running_machine g_machine;
using device_type = const char *;

// The internal registers a device maps into its CPU's space: 8-bit handlers by address, called on the device
// with the offset from the start of their range when they take one
class address_map
{
public:
	struct entry
	{
		offs_t start = 0;
		offs_t end = 0;
		device_t *device = nullptr;
		std::function<u8(offs_t)> read;
		std::function<void(offs_t, u8)> write;

		template <typename C, typename R, typename... A>
		entry &r(R (C::*handler)(A...), const char *name)
		{
			(void)name;
			C *object = static_cast<C *>(device);
			if constexpr (sizeof...(A) == 0)
				read = [object, handler](offs_t) { return u8((object->*handler)()); };
			else
				read = [object, handler](offs_t offset) { return u8((object->*handler)(offset)); };
			return *this;
		}

		template <typename C, typename... A>
		entry &w(void (C::*handler)(A...), const char *name)
		{
			(void)name;
			C *object = static_cast<C *>(device);
			if constexpr (sizeof...(A) == 1)
				write = [object, handler](offs_t, u8 data) { (object->*handler)(data); };
			else
				write = [object, handler](offs_t offset, u8 data) { (object->*handler)(offset, data); };
			return *this;
		}

		template <typename R, typename W>
		entry &rw(R read_handler, const char *read_name, W write_handler, const char *write_name)
		{
			r(read_handler, read_name);
			return w(write_handler, write_name);
		}
	};

	entry &operator()(offs_t start, offs_t end)
	{
		m_entries.emplace_back();
		entry &added = m_entries.back();
		added.start = start;
		added.end = end;
		added.device = m_device;
		return added;
	}

	device_t *m_device = nullptr;
	std::deque<entry> m_entries;
};

class address_map_constructor
{
public:
	address_map_constructor() = default;

	template <typename C>
	address_map_constructor(void (C::*builder)(address_map &), const char *name, C *object)
	{
		(void)name;
		m_build = [builder, object](address_map &map) { (object->*builder)(map); };
	}

	std::function<void(address_map &)> m_build;
};

class address_space_config
{
public:
	address_space_config() = default;
	address_space_config(const char *name, endianness_t endian, int data_width, int address_width, int shift,
			address_map_constructor internal = address_map_constructor())
		: m_data_width(data_width), m_internal(std::move(internal))
	{
		(void)name; (void)endian; (void)address_width; (void)shift;
	}

	int m_data_width = 8;
	address_map_constructor m_internal;
};

// Memory as the CPU sees it; the host fills in the six access functions
class address_space
{
public:
	u8 (*r8)(offs_t) = nullptr;
	u16 (*r16)(offs_t) = nullptr;
	u32 (*r32)(offs_t) = nullptr;
	void (*w8)(offs_t, u8) = nullptr;
	void (*w16)(offs_t, u16) = nullptr;
	void (*w32)(offs_t, u32) = nullptr;
	int width = 16;

	int data_width() const { return width; }
	u8 read_byte(offs_t address) { return r8(address); }
	u16 read_word(offs_t address) { return r16(address); }
	u16 read_word_unaligned(offs_t address) { return r16(address); }
	u32 read_dword(offs_t address) { return r32(address); }
	u32 read_dword_unaligned(offs_t address) { return r32(address); }
	void write_byte(offs_t address, u8 data) { w8(address, data); }
	void write_word(offs_t address, u16 data) { w16(address, data); }
	void write_word_unaligned(offs_t address, u16 data) { w16(address, data); }
	void write_dword(offs_t address, u32 data) { w32(address, data); }
	void write_dword_unaligned(offs_t address, u32 data) { w32(address, data); }
};

// A device's callbacks (devcb): the host binds a function to each; unbound, a read gives the default
template <typename T>
class devcb_read
{
public:
	struct binder
	{
		devcb_read *target;

		template <typename F>
		void set(F function)
		{
			if constexpr (std::is_invocable_v<F, offs_t>)
				target->m_function = [function](offs_t offset) { return T(function(offset)); };
			else
				target->m_function = [function](offs_t) { return T(function()); };
		}
	};

	template <unsigned N>
	class array
	{
	public:
		array(device_t &owner, T default_value)
		{
			(void)owner;
			for (devcb_read &callback : m_callbacks)
				callback.m_default = default_value;
		}
		devcb_read &operator[](unsigned index) { return m_callbacks[index]; }

	private:
		devcb_read m_callbacks[N];
	};

	devcb_read() = default;
	devcb_read(device_t &owner, T default_value) : m_default(default_value) { (void)owner; }
	binder bind() { return binder{ this }; }
	T operator()(offs_t offset = 0, T mem_mask = T(~T(0)))
	{
		(void)mem_mask;
		return m_function ? m_function(offset) : m_default;
	}

private:
	std::function<T(offs_t)> m_function;
	T m_default = 0;
};

template <typename T>
class devcb_write
{
public:
	struct binder
	{
		devcb_write *target;

		template <typename F>
		void set(F function)
		{
			if constexpr (std::is_invocable_v<F, offs_t, T>)
				target->m_function = [function](offs_t offset, T data) { function(offset, data); };
			else
				target->m_function = [function](offs_t, T data) { function(data); };
		}
	};

	template <unsigned N>
	class array
	{
	public:
		explicit array(device_t &owner) { (void)owner; }
		devcb_write &operator[](unsigned index) { return m_callbacks[index]; }

	private:
		devcb_write m_callbacks[N];
	};

	devcb_write() = default;
	explicit devcb_write(device_t &owner) { (void)owner; }
	binder bind() { return binder{ this }; }
	void operator()(offs_t offset, T data, T mem_mask = T(~T(0)))
	{
		(void)mem_mask;
		if (m_function)
			m_function(offset, data);
	}
	void operator()(T data) { operator()(0, data); }

private:
	std::function<void(offs_t, T)> m_function;
};

using devcb_read8 = devcb_read<u8>;
using devcb_read16 = devcb_read<u16>;
using devcb_write8 = devcb_write<u8>;

class device_state_entry
{
public:
	explicit device_state_entry(int index) : m_index(index) {}
	int index() const { return m_index; }

private:
	int m_index;
};

class device_t
{
public:
	device_t(const machine_config &config, device_type type, const char *tag, device_t *owner, u32 clock)
		: m_clock(clock)
	{
		(void)config; (void)type; (void)tag; (void)owner;
	}
	virtual ~device_t() = default;

	u32 clock() const { return m_clock; }
	running_machine &machine() const { return g_machine; }

	// Messages from the core about odd situations; the host may print them
	std::function<void(const std::string &)> log_handler;

	template <typename... Args>
	void logerror(const char *format, Args... args)
	{
		if (log_handler)
			log_handler(string_format(format, args...));
	}

protected:
	virtual void device_config_complete() {}
	virtual void device_resolve_objects() {}
	virtual void device_start() {}
	virtual void device_reset() {}

	template <typename T> void save_item(T &item, const char *name) { (void)item; (void)name; }

private:
	u32 m_clock;
};

class device_memory_interface
{
public:
	using space_config_vector = std::vector<std::pair<int, const address_space_config *>>;
};

class cpu_device : public device_t, public device_memory_interface
{
public:
	cpu_device(const machine_config &config, device_type type, const char *tag, device_t *owner, u32 clock)
		: device_t(config, type, tag, owner, clock) {}

	// Host API
	std::function<void(offs_t)> instruction_hook;

	// Configures and starts the device as MAME does, then builds its internal registers' table
	void start()
	{
		device_config_complete();
		device_resolve_objects();
		device_start();
		m_internal.m_device = this;
		for (const auto &space_config : memory_space_config())
			if (space_config.second->m_internal.m_build)
				space_config.second->m_internal.m_build(m_internal);
	}
	void reset() { device_reset(); }

	// Runs at least 'cycles' (an instruction is never cut); gives back the cycles it ran
	int run(int cycles) { *m_icountptr = cycles; execute_run(); return cycles - *m_icountptr; }

	// The cycles of the running slice still to go (the core takes an instruction's cycles once it is done, so
	// while it runs this is the count at its start)
	int cycles_left() const { return *m_icountptr; }
	void set_input_line(int line, int state) { execute_set_input(line, state); }
	address_space &space(int index = AS_PROGRAM) { return m_spaces[index]; }

	// The internal registers' range in the program space, and their handlers (an address no entry holds reads
	// 0 and ignores writes, as an unmapped address in MAME's map does by default)
	const address_map &internal_map() const { return m_internal; }

	u32 state_value(int index)
	{
		state_export(device_state_entry(index));
		auto found = m_state.find(index);
		return found != m_state.end() ? found->second() : 0;
	}
	std::string state_text(int index) const
	{
		std::string text;
		state_string_export(device_state_entry(index), text);
		return text;
	}

protected:
	struct state_entry
	{
		state_entry &formatstr(const char *) { return *this; }
		state_entry &callimport() { return *this; }
		state_entry &callexport() { return *this; }
		state_entry &noshow() { return *this; }
	};

	virtual u32 execute_min_cycles() const noexcept { return 1; }
	virtual u32 execute_max_cycles() const noexcept { return 1; }
	virtual bool execute_input_edge_triggered(int inputnum) const noexcept { (void)inputnum; return false; }
	virtual void execute_run() = 0;
	virtual void execute_set_input(int inputnum, int state) { (void)inputnum; (void)state; }
	virtual space_config_vector memory_space_config() const = 0;
	virtual void state_import(const device_state_entry &entry) { (void)entry; }
	virtual void state_export(const device_state_entry &entry) { (void)entry; }
	virtual void state_string_export(const device_state_entry &entry, std::string &text) const { (void)entry; (void)text; }
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() { return nullptr; }

	void set_icountptr(int &icount) { m_icountptr = &icount; }
	template <typename T>
	state_entry &state_add(int index, const char *name, T &value)
	{
		(void)name;
		m_state[index] = [&value] { return u32(value); };
		return m_state_entry;
	}
	void standard_irq_callback(int line, offs_t pc) { (void)line; (void)pc; }
	void debugger_instruction_hook(offs_t pc) { if (instruction_hook) instruction_hook(pc); }
	void debugger_wait_hook() {}

private:
	address_space m_spaces[2];
	address_map m_internal;
	int m_dummy_icount = 0;
	int *m_icountptr = &m_dummy_icount;
	std::map<int, std::function<u32()>> m_state;
	state_entry m_state_entry;
};
