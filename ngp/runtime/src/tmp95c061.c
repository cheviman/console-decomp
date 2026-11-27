/**
 *    Name:        tmp95c061.c
 *    Description: the Neo Geo Pocket Color's CPU's own devices (TMP95C061) in C: ngp/tmp95c061.h
 *    Version:     0.1.0
 *    Created:     27-11-2026
 *    Modified:    27-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    MAME 0.289's tmp95c061.cpp (Wilbert Pol) without the core, its arithmetic kept, the chip the host's
 *    struct, nothing allocated, integers only. The core's parts (the pushes of an interrupt, HALT, the EI
 *    and RETI shadow) are the host's, and the chip is what MAME's execute_run asks of the device around each
 *    instruction.
 */

#include <string.h>
#include "ngp/tmp95c061.h"

/* The interrupt registers, as MAME's enum: INTE0AD, INTE45, INTE67, INTET10, INTET32, INTET54, INTET76,
   INTES0, INTES1, INTETC10, INTETC32 */
enum
{
	INTE0AD,
	INTE45,
	INTE67,
	INTET10,
	INTET32,
	INTET54,
	INTET76,
	INTES0,
	INTES1,
	INTETC10,
	INTETC32
};

/* The interrupt vector table, as MAME's tmp95c061_irq_vector_map: the register, the request flag (0x80 or
   0x08) and the vector (the offset of the handler's address in the 0xFFFF00 block) */
static const struct
{
	uint8_t reg;
	uint8_t iff;
	uint8_t vector;
} irq_vector_map[] =
{
	{ INTETC32, 0x80, 0x80 },   /* INTTC3 */
	{ INTETC32, 0x08, 0x7c },   /* INTTC2 */
	{ INTETC10, 0x80, 0x78 },   /* INTTC1 */
	{ INTETC10, 0x08, 0x74 },   /* INTTC0 */
	{ INTE0AD,  0x80, 0x70 },   /* INTAD */
	{ INTES1,   0x80, 0x6c },   /* INTTX1 */
	{ INTES1,   0x08, 0x68 },   /* INTRX1 */
	{ INTES0,   0x80, 0x64 },   /* INTTX0 */
	{ INTES0,   0x08, 0x60 },   /* INTRX0 */
	{ INTET76,  0x80, 0x5c },   /* INTTR7 */
	{ INTET76,  0x08, 0x58 },   /* INTTR6 */
	{ INTET54,  0x80, 0x54 },   /* INTTR5 */
	{ INTET54,  0x08, 0x50 },   /* INTTR4 */
	{ INTET32,  0x80, 0x4c },   /* INTT3 */
	{ INTET32,  0x08, 0x48 },   /* INTT2 */
	{ INTET10,  0x80, 0x44 },   /* INTT1 */
	{ INTET10,  0x08, 0x40 },   /* INTT0 */
	/* 0x3c - reserved */
	{ INTE67,   0x80, 0x38 },   /* INT7 */
	{ INTE67,   0x08, 0x34 },   /* INT6 */
	{ INTE45,   0x80, 0x30 },   /* INT5 */
	{ INTE45,   0x08, 0x2c },   /* INT4 */
	{ INTE0AD,  0x08, 0x28 }    /* INT0 */
};
static const int num_maskable_irqs = sizeof(irq_vector_map) / sizeof(irq_vector_map[0]);

/* The flip-flop changes, as MAME's enum ff_change */
enum
{
	FF_CLEAR,
	FF_SET,
	FF_INVERT
};

/* The port A's output, as MAME's update_porta: the flip-flops TO1 and TO3 (bits 2 and 3) through the
   function register, the latch through its complement, all through the control register */
static void update_porta(ngp_tmp95c061 *chip)
{
	uint8_t fc = (uint8_t)((chip->to1 << 2) | (chip->to3 << 3));

	if (chip->host->port_write)
	{
		uint8_t data = (uint8_t)(((fc & chip->port_function[NGP_TMP95C061_PORT_A]) |
			(chip->port_latch[NGP_TMP95C061_PORT_A] & ~chip->port_function[NGP_TMP95C061_PORT_A])) &
			chip->port_control[NGP_TMP95C061_PORT_A]);
		chip->host->port_write(NGP_TMP95C061_PORT_A, data);
	}
}

/* A flip-flop's change, as MAME's tlcs900_change_tff: TO1 or TO3, then port A's output */
static void change_tff(ngp_tmp95c061 *chip, int which, int change)
{
	switch (which)
	{
	case 1:
		switch (change)
		{
		case FF_CLEAR:
			chip->to1 = 0;
			break;
		case FF_SET:
			chip->to1 = 1;
			break;
		case FF_INVERT:
			chip->to1 ^= 1;
			break;
		}
		break;
	case 3:
		switch (change)
		{
		case FF_CLEAR:
			chip->to3 = 0;
			break;
		case FF_SET:
			chip->to3 = 1;
			break;
		case FF_INVERT:
			chip->to3 ^= 1;
			break;
		}
		break;
	}

	update_porta(chip);
}

/* The A/D converter's run, as MAME's tlcs900_handle_ad: the cycles left, and when they run out the result
   and its flags */
static void handle_ad(ngp_tmp95c061 *chip, int cycles)
{
	if (chip->ad_cycles_left > 0)
	{
		chip->ad_cycles_left -= cycles;
		if (chip->ad_cycles_left <= 0)
		{
			if ((chip->ad_mode & 0x10) == 0)
			{
				/* conversion channel fixed */
				if (chip->host->an_read)
				{
					chip->ad_result[chip->ad_mode & 0x03] = (uint16_t)(chip->host->an_read(chip->ad_mode & 0x03) & 0x3ff);
				}
			}
			else
			{
				/* conversion channel sweep */
				switch (chip->ad_mode & 0x03)
				{
				case 0x03:  /* AN3 */
					if (chip->host->an_read)
					{
						chip->ad_result[3] = (uint16_t)(chip->host->an_read(3) & 0x3ff);
					}
					/* fallthrough */
				case 0x02:  /* AN2 */
					if (chip->host->an_read)
					{
						chip->ad_result[2] = (uint16_t)(chip->host->an_read(2) & 0x3ff);
					}
					/* fallthrough */
				case 0x01:  /* AN1 */
					if (chip->host->an_read)
					{
						chip->ad_result[1] = (uint16_t)(chip->host->an_read(1) & 0x3ff);
					}
					/* fallthrough */
				case 0x00:  /* AN0 */
					if (chip->host->an_read)
					{
						chip->ad_result[0] = (uint16_t)(chip->host->an_read(0) & 0x3ff);
					}
					break;
				}
			}

			/* Clear BUSY flag, set END flag */
			chip->ad_mode &= ~0x40;
			chip->ad_mode |= 0x80;

			chip->int_reg[INTE0AD] |= 0x80;
			chip->check_irqs = 1;

			/* AD repeat mode */
			if (chip->ad_mode & 0x20)
			{
				chip->ad_cycles_left = (chip->ad_mode & 0x08) ? 320 : 160;
			}
		}
	}
}

/* The 8-bit timers' run, as MAME's tlcs900_handle_timers: the prescaler, then each timer's count and its
   match */
static void handle_timers(ngp_tmp95c061 *chip, int cycles)
{
	uint32_t old_pre = chip->timer_pre;

	/* Is the pre-scaler active */
	if (chip->trun & 0x80)
	{
		chip->timer_pre += (uint32_t)cycles;
	}

	/* Timer 0 */
	if (chip->trun & 0x01)
	{
		switch (chip->t8_mode[0] & 0x03)
		{
		case 0x00:  /* TIO */
			break;
		case 0x01:  /* T1 */
			chip->timer_change[0] = (int32_t)((uint32_t)chip->timer_change[0] + ((chip->timer_pre >> 7) - (old_pre >> 7)));
			break;
		case 0x02:  /* T4 */
			chip->timer_change[0] = (int32_t)((uint32_t)chip->timer_change[0] + ((chip->timer_pre >> 9) - (old_pre >> 9)));
			break;
		case 0x03:  /* T16 */
			chip->timer_change[0] = (int32_t)((uint32_t)chip->timer_change[0] + ((chip->timer_pre >> 11) - (old_pre >> 11)));
			break;
		}

		while (chip->timer_change[0] > 0)
		{
			chip->timer_change[0]--;
			chip->timer_8[0] += 1;
			if (chip->timer_8[0] == chip->t8_reg[0])
			{
				if ((chip->trun & 0x02) && (chip->t8_mode[0] & 0x0c) == 0x00)
				{
					chip->timer_change[1] += 1;
				}

				/* In 16bit timer mode the timer should not be reset */
				if ((chip->t8_mode[0] & 0xc0) != 0x40)
				{
					chip->timer_8[0] = 0;
					chip->int_reg[INTET10] |= 0x08;
				}
			}
		}
	}

	/* Timer 1 */
	if (chip->trun & 0x02)
	{
		switch ((chip->t8_mode[0] >> 2) & 0x03)
		{
		case 0x00:  /* TO0TRG */
			break;
		case 0x01:  /* T1 */
			chip->timer_change[1] = (int32_t)((uint32_t)chip->timer_change[1] + ((chip->timer_pre >> 7) - (old_pre >> 7)));
			break;
		case 0x02:  /* T16 */
			chip->timer_change[1] = (int32_t)((uint32_t)chip->timer_change[1] + ((chip->timer_pre >> 11) - (old_pre >> 11)));
			break;
		case 0x03:  /* T256 */
			chip->timer_change[1] = (int32_t)((uint32_t)chip->timer_change[1] + ((chip->timer_pre >> 15) - (old_pre >> 15)));
			break;
		}

		while (chip->timer_change[1] > 0)
		{
			chip->timer_change[1]--;
			chip->timer_8[1] += 1;
			if (chip->timer_8[1] == chip->t8_reg[1])
			{
				chip->timer_8[1] = 0;
				chip->int_reg[INTET10] |= 0x80;

				if (chip->t8_invert & 0x02)
				{
					change_tff(chip, 1, FF_INVERT);
				}

				/* In 16bit timer mode also reset timer 0 */
				if ((chip->t8_mode[0] & 0xc0) == 0x40)
				{
					chip->timer_8[0] = 0;
				}
			}
		}
	}

	/* Timer 2 */
	if (chip->trun & 0x04)
	{
		switch (chip->t8_mode[1] & 0x03)
		{
		case 0x00:  /* invalid */
		case 0x01:  /* T1 */
			chip->timer_change[2] = (int32_t)((uint32_t)chip->timer_change[2] + ((chip->timer_pre >> 7) - (old_pre >> 7)));
			break;
		case 0x02:  /* T4 */
			chip->timer_change[2] = (int32_t)((uint32_t)chip->timer_change[2] + ((chip->timer_pre >> 9) - (old_pre >> 9)));
			break;
		case 0x03:  /* T16 */
			chip->timer_change[2] = (int32_t)((uint32_t)chip->timer_change[2] + ((chip->timer_pre >> 11) - (old_pre >> 11)));
			break;
		}

		while (chip->timer_change[2] > 0)
		{
			chip->timer_change[2]--;
			chip->timer_8[2] += 1;
			if (chip->timer_8[2] == chip->t8_reg[2])
			{
				if ((chip->trun & 0x08) && (chip->t8_mode[1] & 0x0c) == 0x00)
				{
					chip->timer_change[3] += 1;
				}

				/* In 16bit timer mode the timer should not be reset */
				if ((chip->t8_mode[1] & 0xc0) != 0x40)
				{
					chip->timer_8[2] = 0;
					chip->int_reg[INTET32] |= 0x08;
				}
			}
		}
	}

	/* Timer 3 */
	if (chip->trun & 0x08)
	{
		switch ((chip->t8_mode[1] >> 2) & 0x03)
		{
		case 0x00:  /* TO2TRG */
			break;
		case 0x01:  /* T1 */
			chip->timer_change[3] = (int32_t)((uint32_t)chip->timer_change[3] + ((chip->timer_pre >> 7) - (old_pre >> 7)));
			break;
		case 0x02:  /* T16 */
			chip->timer_change[3] = (int32_t)((uint32_t)chip->timer_change[3] + ((chip->timer_pre >> 11) - (old_pre >> 11)));
			break;
		case 0x03:  /* T256 */
			chip->timer_change[3] = (int32_t)((uint32_t)chip->timer_change[3] + ((chip->timer_pre >> 15) - (old_pre >> 15)));
			break;
		}

		while (chip->timer_change[3] > 0)
		{
			chip->timer_change[3]--;
			chip->timer_8[3] += 1;
			if (chip->timer_8[3] == chip->t8_reg[3])
			{
				chip->timer_8[3] = 0;
				chip->int_reg[INTET32] |= 0x80;

				if (chip->t8_invert & 0x20)
				{
					change_tff(chip, 3, FF_INVERT);
				}

				/* In 16bit timer mode also reset timer 2 */
				if ((chip->t8_mode[1] & 0xc0) == 0x40)
				{
					chip->timer_8[2] = 0;
				}
			}
		}
	}

	chip->timer_pre &= 0xffffff;
}

/* The micro DMA's one channel, as MAME's tlcs900_process_hdma: the start vector's interrupt, the data move
   and its cycles, the count's end and its interrupt. Returns 1 when the channel ran (its start vector's
   interrupt was requested), whether or not its mode is one that moves data, with its cycles in 'cycles_taken';
   else 0 */
static int process_hdma(ngp_tmp95c061 *chip, int channel, int *cycles_taken)
{
	uint8_t vector = (uint8_t)((chip->dma_vector[channel] & 0x1f) << 2);

	/* Check if any HDMA actions should be performed */
	if (vector >= 0x28 && vector != 0x3c && vector < 0x74)
	{
		int irq = 0;

		while (irq < num_maskable_irqs && irq_vector_map[irq].vector != vector)
		{
			irq++;
		}

		/* Check if our interrupt flip-flop is set */
		if (irq < num_maskable_irqs && (chip->int_reg[irq_vector_map[irq].reg] & irq_vector_map[irq].iff))
		{
			int cycles = 0;

			switch (chip->dmam[channel] & 0x1f)
			{
			case 0x00:
				chip->host->write8(chip->dmad[channel], chip->host->read8(chip->dmas[channel]));
				chip->dmad[channel] += 1;
				cycles = 8;
				break;
			case 0x01:
				chip->host->write16(chip->dmad[channel], chip->host->read16(chip->dmas[channel]));
				chip->dmad[channel] += 2;
				cycles = 8;
				break;
			case 0x02:
				chip->host->write32(chip->dmad[channel], chip->host->read32(chip->dmas[channel]));
				chip->dmad[channel] += 4;
				cycles = 12;
				break;
			case 0x04:
				chip->host->write8(chip->dmad[channel], chip->host->read8(chip->dmas[channel]));
				chip->dmad[channel] -= 1;
				cycles = 8;
				break;
			case 0x05:
				chip->host->write16(chip->dmad[channel], chip->host->read16(chip->dmas[channel]));
				chip->dmad[channel] -= 2;
				cycles = 8;
				break;
			case 0x06:
				chip->host->write32(chip->dmad[channel], chip->host->read32(chip->dmas[channel]));
				chip->dmad[channel] -= 4;
				cycles = 12;
				break;
			case 0x08:
				chip->host->write8(chip->dmad[channel], chip->host->read8(chip->dmas[channel]));
				chip->dmas[channel] += 1;
				cycles = 8;
				break;
			case 0x09:
				chip->host->write16(chip->dmad[channel], chip->host->read16(chip->dmas[channel]));
				chip->dmas[channel] += 2;
				cycles = 8;
				break;
			case 0x0a:
				chip->host->write32(chip->dmad[channel], chip->host->read32(chip->dmas[channel]));
				chip->dmas[channel] += 4;
				cycles = 12;
				break;
			case 0x0c:
				/* MAME's quirk: read16 but write8 (the low byte) */
				chip->host->write8(chip->dmad[channel], (uint8_t)chip->host->read16(chip->dmas[channel]));
				chip->dmas[channel] -= 1;
				cycles = 8;
				break;
			case 0x0d:
				chip->host->write16(chip->dmad[channel], chip->host->read16(chip->dmas[channel]));
				chip->dmas[channel] -= 2;
				cycles = 8;
				break;
			case 0x0e:
				chip->host->write32(chip->dmad[channel], chip->host->read32(chip->dmas[channel]));
				chip->dmas[channel] -= 4;
				cycles = 12;
				break;
			case 0x10:
				/* MAME's quirk: read16 but write8 (the low byte) */
				chip->host->write8(chip->dmad[channel], (uint8_t)chip->host->read16(chip->dmas[channel]));
				cycles = 8;
				break;
			case 0x11:
				chip->host->write16(chip->dmad[channel], chip->host->read16(chip->dmas[channel]));
				cycles = 8;
				break;
			case 0x12:
				chip->host->write32(chip->dmad[channel], chip->host->read32(chip->dmas[channel]));
				cycles = 12;
				break;
			case 0x14:
				chip->dmas[channel] += 1;
				cycles = 5;
				break;
			}

			chip->dmac[channel] -= 1;

			if (chip->dmac[channel] == 0)
			{
				chip->dma_vector[channel] = 0;
				switch (channel)
				{
				case 0:
					chip->int_reg[INTETC10] |= 0x08;
					break;
				case 1:
					chip->int_reg[INTETC10] |= 0x80;
					break;
				case 2:
					chip->int_reg[INTETC32] |= 0x08;
					break;
				case 3:
					chip->int_reg[INTETC32] |= 0x80;
					break;
				}
			}

			/* Clear the interrupt flip-flop */
			chip->int_reg[irq_vector_map[irq].reg] &= ~irq_vector_map[irq].iff;

			*cycles_taken = cycles;
			return 1;
		}
	}
	return 0;
}

/* A port's latch write, as MAME's port_w: the latch, then the hook (port A's through update_porta) */
static void port_w(ngp_tmp95c061 *chip, unsigned port, uint8_t data)
{
	chip->port_latch[port] = data;
	if (port == NGP_TMP95C061_PORT_A)
	{
		update_porta(chip);
	}
	else if (chip->host->port_write)
	{
		chip->host->port_write(port, data);
	}
}

/* A port's latch read, as MAME's port_r: the hook, or 0 when it is unbound */
static uint8_t port_r(const ngp_tmp95c061 *chip, unsigned port)
{
	if (chip->host->port_read)
	{
		return chip->host->port_read(port);
	}
	return 0;
}

/* A port's control register write, as MAME's port_cr_w: the register, and port A's output */
static void port_cr_w(ngp_tmp95c061 *chip, unsigned port, uint8_t data)
{
	chip->port_control[port] = data;
	if (port == NGP_TMP95C061_PORT_A)
	{
		update_porta(chip);
	}
}

/* A port's function register write, as MAME's port_fc_w: the register, and port A's output */
static void port_fc_w(ngp_tmp95c061 *chip, unsigned port, uint8_t data)
{
	chip->port_function[port] = data;
	if (port == NGP_TMP95C061_PORT_A)
	{
		update_porta(chip);
	}
}

/* TRUN's write, as MAME's trun_w: a timer's stop clears its counter and its pending count */
static void trun_w(ngp_tmp95c061 *chip, uint8_t data)
{
	if (!(data & 0x01))
	{
		chip->timer_8[0] = 0;
		chip->timer_change[0] = 0;
	}
	if (!(data & 0x02))
	{
		chip->timer_8[1] = 0;
		chip->timer_change[1] = 0;
	}
	if (!(data & 0x04))
	{
		chip->timer_8[2] = 0;
		chip->timer_change[2] = 0;
	}
	if (!(data & 0x08))
	{
		chip->timer_8[3] = 0;
		chip->timer_change[3] = 0;
	}
	if (!(data & 0x10))
	{
		chip->timer_8[4] = 0;
	}
	if (!(data & 0x20))
	{
		chip->timer_8[5] = 0;
	}

	chip->trun = data;
}

/* TFFCR's write, as MAME's tffcr_w: the flip-flops' changes, then the register with its read-only bits */
static void tffcr_w(ngp_tmp95c061 *chip, uint8_t data)
{
	switch (data & 0x0c)
	{
	case 0x00:
		change_tff(chip, 1, FF_INVERT);
		break;
	case 0x04:
		change_tff(chip, 1, FF_SET);
		break;
	case 0x08:
		change_tff(chip, 1, FF_CLEAR);
		break;
	}
	switch (data & 0xc0)
	{
	case 0x00:
		change_tff(chip, 3, FF_INVERT);
		break;
	case 0x40:
		change_tff(chip, 3, FF_SET);
		break;
	case 0x80:
		change_tff(chip, 3, FF_CLEAR);
		break;
	}

	chip->t8_invert = (uint8_t)(data | 0xcc);
}

/* The 16-bit timer's register write, as MAME's treg45_w and treg67_w: the low or high byte */
static void treg16_w(ngp_tmp95c061 *chip, unsigned offset, uint8_t data)
{
	if (offset & 1)
	{
		chip->t16_reg[offset >> 1] = (uint16_t)((chip->t16_reg[offset >> 1] & 0x00ff) | ((uint16_t)data << 8));
	}
	else
	{
		chip->t16_reg[offset >> 1] = (uint16_t)((chip->t16_reg[offset >> 1] & 0xff00) | data);
	}
}

/* The 16-bit timer's capture read, as MAME's cap12_r and cap34_r: the low or high byte */
static uint8_t cap16_r(const ngp_tmp95c061 *chip, unsigned offset)
{
	if (offset & 1)
	{
		return (uint8_t)(chip->t16_cap[offset >> 1] >> 8);
	}
	return (uint8_t)(chip->t16_cap[offset >> 1] & 0x00ff);
}

/* The serial channel's buffer write, as MAME's sc0buf_w and sc1buf_w: the TX request and check_irqs */
static void scbuf_w(ngp_tmp95c061 *chip, int channel)
{
	chip->int_reg[channel ? INTES1 : INTES0] |= 0x80;
	chip->check_irqs = 1;
}

/* The serial channel's control read, as MAME's sc0cr_r and sc1cr_r: bits 2-4 cleared */
static uint8_t scr_r(ngp_tmp95c061 *chip, int channel)
{
	uint8_t reg = chip->serial_control[channel];

	chip->serial_control[channel] &= 0xe3;
	return reg;
}

/* The A/D's result read, as MAME's adreg_r: the low or high byte, with its side effects */
static uint8_t adreg_r(ngp_tmp95c061 *chip, unsigned offset)
{
	/* ADMOD EOCF is cleared to 0 when reading any ADREG0..3 */
	chip->ad_mode &= ~0x80;

	if (offset & 1)
	{
		return (uint8_t)(chip->ad_result[offset >> 1] >> 2);
	}

	/* Reading data from the upper 8 bits clears INTE0AD IADC */
	chip->int_reg[INTE0AD] &= ~0x80;
	return (uint8_t)((chip->ad_result[offset >> 1] << 6) | 0x3f);
}

/* ADMOD's write, as MAME's admod_w: the read-only bits kept, the start of a conversion */
static void admod_w(ngp_tmp95c061 *chip, uint8_t data)
{
	/* Preserve read-only bits */
	data = (uint8_t)((chip->ad_mode & 0xc0) | (data & 0x3f));

	/* Check for A/D request start */
	if (data & 0x04)
	{
		data &= ~0x04;
		data |= 0x40;
		chip->ad_cycles_left = (data & 0x08) ? 320 : 160;
	}

	chip->ad_mode = data;
}

/* The interrupt register's write, as MAME's inte_w: the request bits kept when written 1, check_irqs set */
static void inte_w(ngp_tmp95c061 *chip, unsigned offset, uint8_t data)
{
	if (data & 0x80)
	{
		data = (uint8_t)((data & 0x7f) | (chip->int_reg[offset] & 0x80));
	}
	if (data & 0x08)
	{
		data = (uint8_t)((data & 0xf7) | (chip->int_reg[offset] & 0x08));
	}

	chip->int_reg[offset] = data;
	chip->check_irqs = 1;
}

/* The memory start address register's read, as MAME's msar01_r and msar23_r: the address or the mask */
static uint8_t msar_r(const ngp_tmp95c061 *chip, unsigned offset)
{
	if (offset & 1)
	{
		return chip->mem_start_mask[offset >> 1];
	}
	return chip->mem_start_reg[offset >> 1];
}

/* The memory start address register's write, as MAME's msar01_w and msar23_w: the address or the mask */
static void msar_w(ngp_tmp95c061 *chip, unsigned offset, uint8_t data)
{
	if (offset & 1)
	{
		chip->mem_start_mask[offset >> 1] = data;
	}
	else
	{
		chip->mem_start_reg[offset >> 1] = data;
	}
}

void ngp_tmp95c061_init(ngp_tmp95c061 *chip, const ngp_tmp95c061_host *host)
{
	memset(chip, 0, sizeof(*chip));
	chip->host = host;
}

void ngp_tmp95c061_reset(ngp_tmp95c061 *chip)
{
	chip->to1 = 0;
	chip->to3 = 0;

	chip->ad_cycles_left = 0;
	chip->timer_pre = 0;
	chip->timer_change[0] = 0;
	chip->timer_change[1] = 0;
	chip->timer_change[2] = 0;
	chip->timer_change[3] = 0;

	chip->port_latch[NGP_TMP95C061_PORT_1] = 0x00;
	chip->port_latch[NGP_TMP95C061_PORT_2] = 0xff;
	chip->port_latch[NGP_TMP95C061_PORT_5] = 0x3d;
	chip->port_latch[NGP_TMP95C061_PORT_6] = 0x3b;
	chip->port_latch[NGP_TMP95C061_PORT_7] = 0xff;
	chip->port_latch[NGP_TMP95C061_PORT_8] = 0x3f;
	chip->port_latch[NGP_TMP95C061_PORT_A] = 0x0f;
	chip->port_latch[NGP_TMP95C061_PORT_B] = 0xff;
	memset(chip->port_control, 0x00, sizeof(chip->port_control));
	memset(chip->port_function, 0x00, sizeof(chip->port_function));
	chip->port_control[NGP_TMP95C061_PORT_A] = 0x0c; /* HACK ngpc needs this but should be zero */
	chip->port_function[NGP_TMP95C061_PORT_A] = 0x0c; /* HACK ngpc needs this but should be zero */
	chip->trun = 0x00;
	memset(chip->t8_mode, 0x00, sizeof(chip->t8_mode));
	chip->t8_invert = 0xcc;
	chip->trdc = 0x00;
	chip->t16_mode[0] = 0x20;
	chip->t16_mode[1] = 0x20;
	chip->t16_invert[0] = 0x00;
	chip->t16_invert[1] = 0x00;
	chip->t45cr = 0x00;
	chip->pgreg[0] &= 0x0f;
	chip->pgreg[1] &= 0x0f;
	chip->pg01cr = 0x00;
	chip->watchdog_mode = 0x80;
	chip->serial_control[0] &= 0x80;
	chip->serial_mode[0] &= 0x80;
	chip->baud_rate[0] = 0x00;
	chip->serial_control[1] &= 0x80;
	chip->serial_mode[1] &= 0x80;
	chip->baud_rate[1] = 0x00;
	chip->od_enable = 0x00;
	chip->ad_mode = 0x00;
	memset(chip->int_reg, 0x00, sizeof(chip->int_reg));
	chip->iimc = 0x00;
	memset(chip->dma_vector, 0x00, sizeof(chip->dma_vector));
	chip->block_cs[0] = 0x00;
	chip->block_cs[1] = 0x00;
	chip->block_cs[2] = 0x10;
	chip->block_cs[3] = 0x00;
	chip->external_cs = 0x00;
	chip->mem_start_reg[0] = 0xff;
	chip->mem_start_reg[1] = 0xff;
	chip->mem_start_reg[2] = 0xff;
	chip->mem_start_reg[3] = 0xff;
	chip->mem_start_mask[0] = 0xff;
	chip->mem_start_mask[1] = 0xff;
	chip->mem_start_mask[2] = 0xff;
	chip->mem_start_mask[3] = 0xff;
	chip->dram_refresh = 0x00;
	chip->dram_access = 0x80;

	/* The core's reset: check_irqs cleared */
	chip->check_irqs = 0;
}

uint8_t ngp_tmp95c061_read(ngp_tmp95c061 *chip, unsigned offset)
{
	switch (offset)
	{
	case 0x01:
		return port_r(chip, NGP_TMP95C061_PORT_1);
	case 0x06:
		return port_r(chip, NGP_TMP95C061_PORT_2);
	case 0x0d:
		return port_r(chip, NGP_TMP95C061_PORT_5);
	case 0x12:
		return port_r(chip, NGP_TMP95C061_PORT_6);
	case 0x13:
		return port_r(chip, NGP_TMP95C061_PORT_7);
	case 0x18:
		return port_r(chip, NGP_TMP95C061_PORT_8);
	case 0x19:
		return port_r(chip, NGP_TMP95C061_PORT_9);
	case 0x1e:
		return port_r(chip, NGP_TMP95C061_PORT_A);
	case 0x1f:
		return port_r(chip, NGP_TMP95C061_PORT_B);
	case 0x20:
		return chip->trun;
	case 0x25:
		return chip->t8_invert;
	case 0x29:
		return chip->trdc;
	case 0x34:
		return cap16_r(chip, 0);
	case 0x35:
		return cap16_r(chip, 1);
	case 0x36:
		return cap16_r(chip, 2);
	case 0x37:
		return cap16_r(chip, 3);
	case 0x38:
		return chip->t16_mode[0];
	case 0x39:
		return chip->t16_invert[0];
	case 0x3a:
		return chip->t45cr;
	case 0x3c:
		return msar_r(chip, 0);
	case 0x3d:
		return msar_r(chip, 1);
	case 0x3e:
		return msar_r(chip, 2);
	case 0x3f:
		return msar_r(chip, 3);
	case 0x44:
		return cap16_r(chip, 4);
	case 0x45:
		return cap16_r(chip, 5);
	case 0x46:
		return cap16_r(chip, 6);
	case 0x47:
		return cap16_r(chip, 7);
	case 0x48:
		return chip->t16_mode[1];
	case 0x49:
		return chip->t16_invert[1];
	case 0x4c:
		return chip->pgreg[0];
	case 0x4d:
		return chip->pgreg[1];
	case 0x4e:
		return chip->pg01cr;
	case 0x50:
		return 0;
	case 0x51:
		return scr_r(chip, 0);
	case 0x52:
		return chip->serial_mode[0];
	case 0x53:
		return chip->baud_rate[0];
	case 0x54:
		return 0;
	case 0x55:
		return scr_r(chip, 1);
	case 0x56:
		return chip->serial_mode[1];
	case 0x57:
		return chip->baud_rate[1];
	case 0x58:
		return chip->od_enable;
	case 0x5a:
		return chip->dram_refresh;
	case 0x5b:
		return chip->dram_access;
	case 0x5c:
		return msar_r(chip, 4);
	case 0x5d:
		return msar_r(chip, 5);
	case 0x5e:
		return msar_r(chip, 6);
	case 0x5f:
		return msar_r(chip, 7);
	case 0x60:
	case 0x61:
	case 0x62:
	case 0x63:
	case 0x64:
	case 0x65:
	case 0x66:
	case 0x67:
		return adreg_r(chip, offset - 0x60);
	case 0x6d:
		return chip->ad_mode;
	case 0x6e:
		return chip->watchdog_mode;
	case 0x70:
	case 0x71:
	case 0x72:
	case 0x73:
	case 0x74:
	case 0x75:
	case 0x76:
	case 0x77:
	case 0x78:
	case 0x79:
	case 0x7a:
		return chip->int_reg[offset - 0x70];
	default:
		return 0;
	}
}

uint8_t ngp_tmp95c061_peek(const ngp_tmp95c061 *chip, unsigned offset)
{
	switch (offset)
	{
	case 0x01:
		return port_r(chip, NGP_TMP95C061_PORT_1);
	case 0x06:
		return port_r(chip, NGP_TMP95C061_PORT_2);
	case 0x0d:
		return port_r(chip, NGP_TMP95C061_PORT_5);
	case 0x12:
		return port_r(chip, NGP_TMP95C061_PORT_6);
	case 0x13:
		return port_r(chip, NGP_TMP95C061_PORT_7);
	case 0x18:
		return port_r(chip, NGP_TMP95C061_PORT_8);
	case 0x19:
		return port_r(chip, NGP_TMP95C061_PORT_9);
	case 0x1e:
		return port_r(chip, NGP_TMP95C061_PORT_A);
	case 0x1f:
		return port_r(chip, NGP_TMP95C061_PORT_B);
	case 0x20:
		return chip->trun;
	case 0x25:
		return chip->t8_invert;
	case 0x29:
		return chip->trdc;
	case 0x34:
		return cap16_r(chip, 0);
	case 0x35:
		return cap16_r(chip, 1);
	case 0x36:
		return cap16_r(chip, 2);
	case 0x37:
		return cap16_r(chip, 3);
	case 0x38:
		return chip->t16_mode[0];
	case 0x39:
		return chip->t16_invert[0];
	case 0x3a:
		return chip->t45cr;
	case 0x3c:
		return msar_r(chip, 0);
	case 0x3d:
		return msar_r(chip, 1);
	case 0x3e:
		return msar_r(chip, 2);
	case 0x3f:
		return msar_r(chip, 3);
	case 0x44:
		return cap16_r(chip, 4);
	case 0x45:
		return cap16_r(chip, 5);
	case 0x46:
		return cap16_r(chip, 6);
	case 0x47:
		return cap16_r(chip, 7);
	case 0x48:
		return chip->t16_mode[1];
	case 0x49:
		return chip->t16_invert[1];
	case 0x4c:
		return chip->pgreg[0];
	case 0x4d:
		return chip->pgreg[1];
	case 0x4e:
		return chip->pg01cr;
	case 0x50:
		return 0;
	case 0x51:
		return chip->serial_control[0];
	case 0x52:
		return chip->serial_mode[0];
	case 0x53:
		return chip->baud_rate[0];
	case 0x54:
		return 0;
	case 0x55:
		return chip->serial_control[1];
	case 0x56:
		return chip->serial_mode[1];
	case 0x57:
		return chip->baud_rate[1];
	case 0x58:
		return chip->od_enable;
	case 0x5a:
		return chip->dram_refresh;
	case 0x5b:
		return chip->dram_access;
	case 0x5c:
		return msar_r(chip, 4);
	case 0x5d:
		return msar_r(chip, 5);
	case 0x5e:
		return msar_r(chip, 6);
	case 0x5f:
		return msar_r(chip, 7);
	case 0x60:
	case 0x61:
	case 0x62:
	case 0x63:
	case 0x64:
	case 0x65:
	case 0x66:
	case 0x67:
		/* The A/D's result without its side effects */
		if (offset & 1)
		{
			return (uint8_t)(chip->ad_result[(offset - 0x60) >> 1] >> 2);
		}
		return (uint8_t)((chip->ad_result[(offset - 0x60) >> 1] << 6) | 0x3f);
	case 0x6d:
		return chip->ad_mode;
	case 0x6e:
		return chip->watchdog_mode;
	case 0x70:
	case 0x71:
	case 0x72:
	case 0x73:
	case 0x74:
	case 0x75:
	case 0x76:
	case 0x77:
	case 0x78:
	case 0x79:
	case 0x7a:
		return chip->int_reg[offset - 0x70];
	default:
		return 0;
	}
}

void ngp_tmp95c061_write(ngp_tmp95c061 *chip, unsigned offset, uint8_t data)
{
	switch (offset)
	{
	case 0x01:
		port_w(chip, NGP_TMP95C061_PORT_1, data);
		break;
	case 0x04:
		port_cr_w(chip, NGP_TMP95C061_PORT_1, data);
		break;
	case 0x06:
		port_w(chip, NGP_TMP95C061_PORT_2, data);
		break;
	case 0x09:
		port_fc_w(chip, NGP_TMP95C061_PORT_2, data);
		break;
	case 0x0d:
		port_w(chip, NGP_TMP95C061_PORT_5, data);
		break;
	case 0x10:
		port_cr_w(chip, NGP_TMP95C061_PORT_5, data);
		break;
	case 0x11:
		port_fc_w(chip, NGP_TMP95C061_PORT_5, data);
		break;
	case 0x12:
		/* MAME's map: port_r<PORT_6>, port_w<PORT_7> */
		port_w(chip, NGP_TMP95C061_PORT_7, data);
		break;
	case 0x13:
		port_w(chip, NGP_TMP95C061_PORT_7, data);
		break;
	case 0x15:
		port_fc_w(chip, NGP_TMP95C061_PORT_6, data);
		break;
	case 0x16:
		port_cr_w(chip, NGP_TMP95C061_PORT_7, data);
		break;
	case 0x17:
		port_fc_w(chip, NGP_TMP95C061_PORT_7, data);
		break;
	case 0x18:
		port_w(chip, NGP_TMP95C061_PORT_8, data);
		break;
	case 0x1a:
		port_cr_w(chip, NGP_TMP95C061_PORT_8, data);
		break;
	case 0x1b:
		port_fc_w(chip, NGP_TMP95C061_PORT_8, data);
		break;
	case 0x1e:
		port_w(chip, NGP_TMP95C061_PORT_A, data);
		break;
	case 0x1f:
		port_w(chip, NGP_TMP95C061_PORT_B, data);
		break;
	case 0x20:
		trun_w(chip, data);
		break;
	case 0x22:
	case 0x23:
		chip->t8_reg[offset - 0x22] = data;
		break;
	case 0x24:
		chip->t8_mode[0] = data;
		break;
	case 0x25:
		tffcr_w(chip, data);
		break;
	case 0x26:
	case 0x27:
		chip->t8_reg[offset - 0x26 + 2] = data;
		break;
	case 0x28:
		chip->t8_mode[1] = data;
		break;
	case 0x29:
		chip->trdc = data;
		break;
	case 0x2c:
		port_cr_w(chip, NGP_TMP95C061_PORT_A, data);
		break;
	case 0x2d:
		port_fc_w(chip, NGP_TMP95C061_PORT_A, data);
		break;
	case 0x2e:
		port_cr_w(chip, NGP_TMP95C061_PORT_B, data);
		break;
	case 0x2f:
		port_fc_w(chip, NGP_TMP95C061_PORT_B, data);
		break;
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
		treg16_w(chip, offset - 0x30, data);
		break;
	case 0x38:
		chip->t16_mode[0] = (uint8_t)(data | 0x20);
		break;
	case 0x39:
		chip->t16_invert[0] = (uint8_t)(data | 0xc3);
		break;
	case 0x3a:
		chip->t45cr = data;
		break;
	case 0x3c:
	case 0x3d:
	case 0x3e:
	case 0x3f:
		msar_w(chip, offset - 0x3c, data);
		break;
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
		treg16_w(chip, offset - 0x40 + 4, data);
		break;
	case 0x48:
		chip->t16_mode[1] = (uint8_t)(data | 0x20);
		break;
	case 0x49:
		chip->t16_invert[1] = (uint8_t)(data | 0xc3);
		break;
	case 0x4c:
	case 0x4d:
		chip->pgreg[offset - 0x4c] = data;
		break;
	case 0x4e:
		chip->pg01cr = data;
		break;
	case 0x50:
		scbuf_w(chip, 0);
		break;
	case 0x51:
		chip->serial_control[0] = data;
		break;
	case 0x52:
		chip->serial_mode[0] = data;
		break;
	case 0x53:
		chip->baud_rate[0] = data;
		break;
	case 0x54:
		scbuf_w(chip, 1);
		break;
	case 0x55:
		chip->serial_control[1] = data;
		break;
	case 0x56:
		chip->serial_mode[1] = data;
		break;
	case 0x57:
		chip->baud_rate[1] = data;
		break;
	case 0x58:
		chip->od_enable = data;
		break;
	case 0x5a:
		chip->dram_refresh = data;
		break;
	case 0x5b:
		chip->dram_access = data;
		break;
	case 0x5c:
	case 0x5d:
	case 0x5e:
	case 0x5f:
		msar_w(chip, offset - 0x5c + 4, data);
		break;
	case 0x68:
	case 0x69:
	case 0x6a:
	case 0x6b:
		chip->block_cs[offset - 0x68] = data;
		break;
	case 0x6c:
		chip->external_cs = data;
		break;
	case 0x6d:
		admod_w(chip, data);
		break;
	case 0x6e:
		chip->watchdog_mode = data;
		break;
	case 0x6f:
		/* WDCR: MAME's handler is empty */
		break;
	case 0x70:
	case 0x71:
	case 0x72:
	case 0x73:
	case 0x74:
	case 0x75:
	case 0x76:
	case 0x77:
	case 0x78:
	case 0x79:
	case 0x7a:
		inte_w(chip, offset - 0x70, data);
		break;
	case 0x7b:
		chip->iimc = data;
		chip->check_irqs = 1;
		break;
	case 0x7c:
	case 0x7d:
	case 0x7e:
	case 0x7f:
		chip->dma_vector[offset - 0x7c] = data;
		break;
	default:
		break;
	}
}

int ngp_tmp95c061_set_input(ngp_tmp95c061 *chip, unsigned line, int state)
{
	int result = 0;

	switch (line)
	{
	case NGP_TMP95C061_NMI:
		if (chip->level[NGP_TMP95C061_NMI] == 0 && state == 1)
		{
			chip->nmi_state = state;
		}
		chip->level[NGP_TMP95C061_NMI] = state;
		break;

	case NGP_TMP95C061_INT0:
		/* Is INT0 functionality enabled? */
		if (chip->iimc & 0x04)
		{
			if (chip->iimc & 0x02)
			{
				/* Rising edge detect */
				if (chip->level[NGP_TMP95C061_INT0] == 0 && state == 1)
				{
					/* Leave HALT state */
					result = 1;
					chip->int_reg[INTE0AD] |= 0x08;
				}
			}
			else
			{
				/* Level detect */
				if (state == 1)
				{
					chip->int_reg[INTE0AD] |= 0x08;
				}
				else
				{
					chip->int_reg[INTE0AD] &= ~0x08;
				}
			}
		}
		chip->level[NGP_TMP95C061_INT0] = state;
		break;

	case NGP_TMP95C061_INT4:
		if (!(chip->port_control[NGP_TMP95C061_PORT_B] & 0x01))
		{
			if (chip->level[NGP_TMP95C061_INT4] == 0 && state == 1)
			{
				chip->int_reg[INTE45] |= 0x08;
			}
		}
		chip->level[NGP_TMP95C061_INT4] = state;
		break;

	case NGP_TMP95C061_INT5:
		if (!(chip->port_control[NGP_TMP95C061_PORT_B] & 0x02))
		{
			if (chip->level[NGP_TMP95C061_INT5] == 0 && state == 1)
			{
				chip->int_reg[INTE45] |= 0x80;
			}
		}
		chip->level[NGP_TMP95C061_INT5] = state;
		break;

	case NGP_TMP95C061_TIO:
		/* External timer input for timer 0 */
		if ((chip->trun & 0x01) && (chip->t8_mode[0] & 0x03) == 0x00)
		{
			if (chip->level[NGP_TMP95C061_TIO] == 0 && state == 1)
			{
				chip->timer_change[0] += 1;
			}
		}
		chip->level[NGP_TMP95C061_TIO] = state;
		break;
	}

	chip->check_irqs = 1;
	return result;
}

void ngp_tmp95c061_run(ngp_tmp95c061 *chip, int cycles)
{
	handle_ad(chip, cycles);
	handle_timers(chip, cycles);
}

int ngp_tmp95c061_dma(ngp_tmp95c061 *chip, unsigned iff)
{
	int cycles = 0;

	/* HDMA can only be performed if interrupts are allowed */
	if (iff != 7)
	{
		/* The first channel that runs ends the check, as in MAME, even with a mode that moves nothing */
		if (!process_hdma(chip, 0, &cycles))
		{
			if (!process_hdma(chip, 1, &cycles))
			{
				if (!process_hdma(chip, 2, &cycles))
				{
					process_hdma(chip, 3, &cycles);
				}
			}
		}
	}

	return cycles;
}

int ngp_tmp95c061_pending(const ngp_tmp95c061 *chip, unsigned iff, ngp_tmp95c061_irq *irq)
{
	/* Check for NMI */
	if (chip->nmi_state == 1)
	{
		irq->vector = 0x20;
		irq->level = NGP_TMP95C061_KEEP_IFF;
		irq->reg = 0;
		irq->flag = 0;
		return 1;
	}

	/* Check regular irqs */
	int irq_vectors[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	int i;

	for (i = 0; i < num_maskable_irqs; i++)
	{
		if (chip->int_reg[irq_vector_map[i].reg] & irq_vector_map[i].iff)
		{
			switch (irq_vector_map[i].iff)
			{
			case 0x80:
				irq_vectors[(chip->int_reg[irq_vector_map[i].reg] >> 4) & 0x07] = i;
				break;
			case 0x08:
				irq_vectors[chip->int_reg[irq_vector_map[i].reg] & 0x07] = i;
				break;
			}
		}
	}

	/* Check highest allowed priority irq */
	int chosen = -1;
	int level = 0;
	int start = (iff > 1) ? (int)iff : 1;

	for (i = start; i < 7; i++)
	{
		if (irq_vectors[i] >= 0)
		{
			chosen = irq_vectors[i];
			level = i + 1;
		}
	}

	/* Take irq */
	if (chosen >= 0)
	{
		irq->vector = irq_vector_map[chosen].vector;
		irq->level = (int8_t)level;
		irq->reg = irq_vector_map[chosen].reg;
		irq->flag = irq_vector_map[chosen].iff;
		return 1;
	}

	return 0;
}

void ngp_tmp95c061_acknowledge(ngp_tmp95c061 *chip, const ngp_tmp95c061_irq *irq)
{
	if (irq->flag == 0)
	{
		chip->nmi_state = 0;
	}
	else
	{
		chip->int_reg[irq->reg] &= ~irq->flag;
	}
}
