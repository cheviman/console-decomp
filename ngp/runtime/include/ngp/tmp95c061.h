/**
 *    Name:        tmp95c061.h
 *    Description: the Neo Geo Pocket Color's CPU's own devices (the TMP95C061 around its TLCS-900/H core): the
 *                 registers at 000000-00007F, the ports, the 8-bit timers, the A/D converter, the input
 *                 lines, the micro DMA and the choice of the interrupt taken
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    MAME 0.289's tmp95c061.cpp without its core: what the core does is the host's (the instructions, the
 *    pushes of an interrupt, HALT, the EI and RETI shadow), and the chip is what MAME's execute_run asks of
 *    the device around each instruction. The chip is the host's struct: nothing is allocated, integers only.
 *
 *    Around each instruction, as MAME's tlcs900_device::execute_run:
 *      1. when check_irqs is set and the instruction before was not EI or RETI (MAME's m_irq_inhibit): the
 *         host asks ngp_tmp95c061_pending; when it gives an interrupt, the host pushes the PC (4 bytes) then
 *         the SR (2 bytes) on XSSP, sets the SR's IFF to the level it gives (not for the NMI), reads the PC
 *         from 0xFFFF00 + its vector, calls ngp_tmp95c061_acknowledge, counts 18 cycles and leaves HALT;
 *         then it clears check_irqs, whether or not one was taken;
 *      2. the instruction (or 8 cycles of HALT);
 *      3. ngp_tmp95c061_run with the cycles of 1 and 2: the A/D converter, then the timers;
 *      4. ngp_tmp95c061_dma, whose cycles count against the time but not in the timers.
 *    The core sets check_irqs too, as MAME's does (EI, RETI, a write of the SR...).
 */
#ifndef NGP_TMP95C061_H
#define NGP_TMP95C061_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ports, as MAME's tmp95c061_device numbers them */
enum
{
	NGP_TMP95C061_PORT_1,
	NGP_TMP95C061_PORT_2,
	NGP_TMP95C061_PORT_5,
	NGP_TMP95C061_PORT_6,
	NGP_TMP95C061_PORT_7,
	NGP_TMP95C061_PORT_8,
	NGP_TMP95C061_PORT_9,
	NGP_TMP95C061_PORT_A,
	NGP_TMP95C061_PORT_B,
	NGP_TMP95C061_PORTS
};

/* The input lines (MAME's execute_set_input: TLCS900_NMI, INT0, INT4, INT5, TIO) */
enum
{
	NGP_TMP95C061_NMI,
	NGP_TMP95C061_INT0,
	NGP_TMP95C061_INT4,
	NGP_TMP95C061_INT5,
	NGP_TMP95C061_TIO,
	NGP_TMP95C061_LINES
};

/* The interrupt registers (000070-00007A: INTE0AD, INTE45, INTE67, INTET10, INTET32, INTET54, INTET76,
   INTES0, INTES1, INTETC10, INTETC32), each with two sources: the request flag of one at bit 7 and its
   priority at bits 4-6, the other's at bit 3 and bits 0-2 */
#define NGP_TMP95C061_INT_REGS 11

/* What the chip asks of the console. A pointer left null is MAME's callback left unbound */
typedef struct ngp_tmp95c061_host
{
	/* A port's output, MAME's m_port_write: port A's whenever its latch, control or function register or TO1
	   or TO3 changes (MAME's update_porta), another port's when its latch is written */
	void (*port_write)(unsigned port, uint8_t data);
	/* A port's input (MAME's m_port_read); unbound, 0 */
	uint8_t (*port_read)(unsigned port);
	/* An analogue input, AN0-AN3 (MAME's m_an_read); unbound, 0 */
	uint16_t (*an_read)(unsigned channel);
	/* The bus, for the micro DMA: MAME's RDMEM, RDMEMW, RDMEML, WRMEM, WRMEMW, WRMEML on the program space */
	uint8_t (*read8)(uint32_t address);
	uint16_t (*read16)(uint32_t address);
	uint32_t (*read32)(uint32_t address);
	void (*write8)(uint32_t address, uint8_t data);
	void (*write16)(uint32_t address, uint16_t data);
	void (*write32)(uint32_t address, uint32_t data);
} ngp_tmp95c061_host;

/* The chip: MAME's tmp95c061_device's members, and the tlcs900_device's ones it uses. The host may read any
   of it (a state's hash, a comparison) */
typedef struct ngp_tmp95c061
{
	const ngp_tmp95c061_host *host;

	/* Ports */
	uint8_t port_latch[NGP_TMP95C061_PORTS];
	uint8_t port_control[NGP_TMP95C061_PORTS];
	uint8_t port_function[NGP_TMP95C061_PORTS];

	/* Timers: the 8-bit timers 0-3 (TRUN, TREG0-3, T01MOD and T23MOD, TFFCR, TRDC, the flip-flops TO1 and
	   TO3) and the 16-bit timers' registers, which MAME keeps but never runs */
	uint8_t trun;
	uint8_t t8_reg[4];
	uint8_t t8_mode[2];
	uint8_t t8_invert;
	uint8_t trdc;
	uint8_t to1;
	uint8_t to3;
	uint16_t t16_reg[4];
	uint16_t t16_cap[4];
	uint8_t t16_mode[2];
	uint8_t t16_invert[2];
	uint8_t t45cr;

	/* Pattern generator, watchdog, serial channels, open drain */
	uint8_t pgreg[2];
	uint8_t pg01cr;
	uint8_t watchdog_mode;
	uint8_t serial_control[2];
	uint8_t serial_mode[2];
	uint8_t baud_rate[2];
	uint8_t od_enable;

	/* A/D converter */
	uint16_t ad_result[4];
	uint8_t ad_mode;

	/* Interrupts and the micro DMA's start vectors */
	uint8_t int_reg[NGP_TMP95C061_INT_REGS];
	uint8_t iimc;
	uint8_t dma_vector[4];

	/* Chip selects and DRAM */
	uint8_t block_cs[4];
	uint8_t external_cs;
	uint8_t mem_start_reg[4];
	uint8_t mem_start_mask[4];
	uint8_t dram_refresh;
	uint8_t dram_access;

	/* The core's (tlcs900.h) that the devices use: the prescaler (24 bits once a run ends), the 8-bit
	   timers' counters (4 and 5 only cleared) and the counts each timer has still to take, the A/D's cycles
	   left, the NMI's request, the lines' levels (0 or 1), and whether interrupts are to be checked before
	   the next instruction */
	uint32_t timer_pre;
	uint8_t timer_8[6];
	int32_t timer_change[4];
	int32_t ad_cycles_left;
	uint8_t nmi_state;
	uint8_t level[NGP_TMP95C061_LINES];
	uint8_t check_irqs;

	/* The micro DMA's registers, the core's (LDC writes them): source, destination, count, mode */
	uint32_t dmas[4];
	uint32_t dmad[4];
	uint16_t dmac[4];
	uint8_t dmam[4];
} ngp_tmp95c061;

/* An interrupt to take: its vector (the handler's address is the 32-bit word at 0xFFFF00 + vector) and the
   SR's new IFF, or NGP_TMP95C061_KEEP_IFF for the NMI, which MAME takes without changing the SR */
#define NGP_TMP95C061_KEEP_IFF (-1)
typedef struct ngp_tmp95c061_irq
{
	uint8_t vector;
	int8_t level;
	uint8_t reg;                       /* which request it is: an index of int_reg and its flag (0x80 or */
	uint8_t flag;                      /* 0x08), or flag 0 for the NMI */
} ngp_tmp95c061_irq;

/* The chip as MAME builds it (its constructor and device_resolve_objects: every member 0, the lines and the
   NMI clear), with the console's hooks */
void ngp_tmp95c061_init(ngp_tmp95c061 *chip, const ngp_tmp95c061_host *host);

/* MAME's device_reset of the TMP95C061 (the base's part that is the chip's: check_irqs cleared). It calls no
   hook */
void ngp_tmp95c061_reset(ngp_tmp95c061 *chip);

/* The CPU reads the register at 'offset' (00-7F), as MAME's internal map: an offset with no handler reads 0.
   A read has MAME's side effects (SC0CR and SC1CR lose bits 2-4, ADREG clears ADMOD's bit 7 and, its upper
   byte, INTE0AD's bit 7) */
uint8_t ngp_tmp95c061_read(ngp_tmp95c061 *chip, unsigned offset);

/* What a read of 'offset' would give, with no side effect and no hook called but the ports' reads */
uint8_t ngp_tmp95c061_peek(const ngp_tmp95c061 *chip, unsigned offset);

/* The CPU writes 'data' at 'offset' (00-7F), as MAME's internal map: an offset with no handler is lost */
void ngp_tmp95c061_write(ngp_tmp95c061 *chip, unsigned offset, uint8_t data);

/* An input line changes to 'state' (0 or 1), MAME's execute_set_input; check_irqs is set. Returns 1 when the
   CPU leaves HALT (INT0's rising edge when INT0 is enabled and edge triggered), else 0 */
int ngp_tmp95c061_set_input(ngp_tmp95c061 *chip, unsigned line, int state);

/* After an instruction of 'cycles' (MAME's m_cycles: 18 more when it began by taking an interrupt, 8 for a
   step of HALT): MAME's tlcs900_handle_ad, then tlcs900_handle_timers */
void ngp_tmp95c061_run(ngp_tmp95c061 *chip, int cycles);

/* MAME's tlcs900_check_hdma, with the SR's IFF (bits 12-14) at 'iff': when it is not 7, the first channel of
   0-3 whose start vector names an interrupt that is requested moves its data through the hooks and clears
   that request. Returns the cycles it took (0 when none moved) */
int ngp_tmp95c061_dma(ngp_tmp95c061 *chip, unsigned iff);

/* MAME's tlcs900_check_irqs's choice, with the SR's IFF at 'iff', changing nothing: the NMI when it is
   requested, else the requested interrupt of the highest priority from max(1, iff) to 6 (at a priority, the
   last of MAME's table). Returns 1 and fills 'irq', or 0 when none is taken */
int ngp_tmp95c061_pending(const ngp_tmp95c061 *chip, unsigned iff, ngp_tmp95c061_irq *irq);

/* The interrupt 'irq' given by ngp_tmp95c061_pending is taken (after the host's pushes and its read of the
   vector, as MAME's order): its request is cleared */
void ngp_tmp95c061_acknowledge(ngp_tmp95c061 *chip, const ngp_tmp95c061_irq *irq);

#ifdef __cplusplus
}
#endif

#endif
