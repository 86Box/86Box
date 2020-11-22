/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the National Semiconductor LM78 hardware monitoring chip.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include "cpu.h"
#include <86box/i2c.h>
#include <86box/hwm.h>


#define LM78_I2C		0x010000
#define LM78_W83781D		0x020000
#define LM78_AS99127F_REV1	0x040000
#define LM78_AS99127F_REV2	0x080000
#define LM78_W83782D		0x100000
#define LM78_AS99127F		(LM78_AS99127F_REV1 | LM78_AS99127F_REV2) /* special mask covering both _REV1 and _REV2 */
#define LM78_WINBOND		(LM78_W83781D | LM78_AS99127F | LM78_W83782D) /* special mask covering all Winbond variants */
#define LM78_WINBOND_VENDOR_ID	((dev->local & LM78_AS99127F_REV1) ? 0x12c3 : 0x5ca3)
#define LM78_WINBOND_BANK	(dev->regs[0x4e] & 0x07)

#define CLAMP(a, min, max)	(((a) < (min)) ? (min) : (((a) > (max)) ? (max) : (a)))
#define LM78_RPM_TO_REG(r, d)	((r) ? CLAMP(1350000 / (r * d), 1, 255) : 0)
#define LM78_VOLTAGE_TO_REG(v)	((v) >> 4)
#define LM78_NEG_VOLTAGE(v, r)	(v * (604.0 / ((double) r))) /* negative voltage formula from the W83781D datasheet */
#define LM78_NEG_VOLTAGE2(v, r)	(((3600 + v) * (((double) r) / (((double) r) + 56.0))) - v) /* negative voltage formula from the W83782D datasheet */


typedef struct {
    uint32_t	  local;
    hwm_values_t  *values;
    device_t      *lm75[2];

    uint8_t regs[256];
    uint8_t regs_782d[2][16];
    uint8_t addr_register;
    uint8_t data_register;

    uint8_t i2c_addr, i2c_state;
} lm78_t;


static uint8_t	lm78_i2c_start(void *bus, uint8_t addr, uint8_t read, void *priv);
static uint8_t	lm78_isa_read(uint16_t port, void *priv);
static uint8_t	lm78_i2c_read(void *bus, uint8_t addr, void *priv);
static uint8_t	lm78_read(lm78_t *dev, uint8_t reg, uint8_t bank);
static void	lm78_isa_write(uint16_t port, uint8_t val, void *priv);
static uint8_t	lm78_i2c_write(void *bus, uint8_t addr, uint8_t data, void *priv);
static uint8_t	lm78_write(lm78_t *dev, uint8_t reg, uint8_t val, uint8_t bank);
static void	lm78_reset(lm78_t *dev, uint8_t initialization);


#ifdef ENABLE_LM78_LOG
int lm78_do_log = ENABLE_LM78_LOG;


static void
lm78_log(const char *fmt, ...)
{
    va_list ap;

    if (lm78_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define lm78_log(fmt, ...)
#endif


static void
lm78_remap(lm78_t *dev, uint8_t addr)
{
    lm75_t *lm75;

    if (!(dev->local & LM78_I2C)) return;

    lm78_log("LM78: remapping to SMBus %02Xh\n", addr);

    i2c_removehandler(i2c_smbus, dev->i2c_addr, 1, lm78_i2c_start, lm78_i2c_read, lm78_i2c_write, NULL, dev);

    if (addr < 0x80)
	i2c_sethandler(i2c_smbus, addr, 1, lm78_i2c_start, lm78_i2c_read, lm78_i2c_write, NULL, dev);

    dev->i2c_addr = addr;

    if (dev->local & LM78_AS99127F) {
	/* Store the main I2C address on the LM75 devices to ensure reads/writes
	   to the AS99127F's proprietary registers are passed through to this side. */
	for (uint8_t i = 0; i <= 1; i++) {
		lm75 = device_get_priv(dev->lm75[i]);
		if (lm75)
			lm75->as99127f_i2c_addr = dev->i2c_addr;
	}
    }
}


static uint8_t
lm78_i2c_start(void *bus, uint8_t addr, uint8_t read, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    dev->i2c_state = 0;

    return 1;
}


static uint8_t
lm78_isa_read(uint16_t port, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;
    uint8_t ret = 0xff;

    switch (port & 0x7) {
	case 0x5:
		ret = (dev->addr_register & 0x7f);
		break;

	case 0x6:
		ret = lm78_read(dev, dev->addr_register, LM78_WINBOND_BANK);

		if (((LM78_WINBOND_BANK == 0) &&
		    ((dev->addr_register == 0x41) || (dev->addr_register == 0x43) || (dev->addr_register == 0x45) || (dev->addr_register == 0x56) ||
		     ((dev->addr_register >= 0x60) && (dev->addr_register < 0x7f)))) ||
		    ((dev->local & LM78_W83782D) && (LM78_WINBOND_BANK == 5) && (dev->addr_register >= 0x50) && (dev->addr_register < 0x58))) {
			/* auto-increment registers */
			dev->addr_register++;
		}
		break;

	default:
		lm78_log("LM78: Read from unknown ISA port %d\n", port & 0x7);
		break;
    }

    return ret;
}


static uint8_t
lm78_i2c_read(void *bus, uint8_t addr, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    return lm78_read(dev, dev->addr_register++, LM78_WINBOND_BANK);
}


static uint8_t
lm78_read(lm78_t *dev, uint8_t reg, uint8_t bank)
{
    uint8_t ret = 0, masked_reg = reg, bankswitched = ((reg & 0xf8) == 0x50);
    lm75_t *lm75;

    if (bankswitched && ((bank == 1) || (bank == 2))) {
	/* LM75 registers */
	lm75 = device_get_priv(dev->lm75[bank - 1]);
	if (lm75)
		ret = lm75_read(lm75, reg);
    } else if (bankswitched && ((bank == 4) || (bank == 5) || (bank == 6))) {
	/* W83782D additional registers */
	if (dev->local & LM78_W83782D) {
		if ((bank == 5) && ((reg == 0x50) || (reg == 0x51))) /* voltages */
			ret = LM78_VOLTAGE_TO_REG(dev->values->voltages[7 + (reg & 1)]);
		else if (bank < 6)
			ret = dev->regs_782d[bank - 4][reg & 0x0f];
	}
    } else {
	/* regular registers */
	ret = dev->regs[reg];
	if (reg >= 0x40)
		masked_reg = reg & 0x3f; /* match both non-auto-increment and auto-increment locations */
	if ((masked_reg >= 0x20) && (masked_reg <= 0x26)) /* voltages */
		ret = LM78_VOLTAGE_TO_REG(dev->values->voltages[reg & 7]);
	else if (masked_reg == 0x27) /* temperature */
		ret = dev->values->temperatures[0];
	else if ((masked_reg >= 0x28) && (masked_reg <= 0x2a)) /* fan speeds */
		ret = LM78_RPM_TO_REG(dev->values->fans[reg & 3], 1 << ((dev->regs[((reg & 3) == 2) ? 0x4b : 0x47] >> ((reg & 3) ? 6 : 4)) & 0x3));
	else if ((reg == 0x4f) && (dev->local & LM78_WINBOND)) /* two-byte vendor ID register */
		ret = ((dev->regs[0x4e] & 0x80) ? (LM78_WINBOND_VENDOR_ID >> 8) : LM78_WINBOND_VENDOR_ID);
	else if ((reg >= 0x60) && (reg <= 0x7f)) /* read auto-increment value RAM registers from their non-auto-increment locations */
		ret = dev->regs[reg & 0x3f];
	else if (dev->local & LM78_AS99127F) { /* AS99127F mirrored registers */
		masked_reg = reg & 0x7f;
		if (masked_reg == 0x00) /* IN2 Low Limit */
			ret = dev->regs[0x30];
		else if ((masked_reg == 0x01) || (masked_reg == 0x04)) /* IN3 */
			ret = LM78_VOLTAGE_TO_REG(dev->values->voltages[3]);
		else if (masked_reg == 0x05) /* IN2 */
			ret = LM78_VOLTAGE_TO_REG(dev->values->voltages[2]);
		else if (masked_reg == 0x08) /* IN3 Low Limit */
			ret = dev->regs[0x32];
		else if ((reg >= 0x80) && (reg <= 0x92)) /* mirror [0x00:0x12] to [0x80:0x92] */
			ret = dev->regs[masked_reg];
	}
    }

    lm78_log("LM78: read(%02X, %d) = %02X\n", reg, bank, ret);

    return ret;
}


static void
lm78_isa_write(uint16_t port, uint8_t val, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    switch (port & 0x7) {
	case 0x5:
		dev->addr_register = (val & 0x7f);
		break;
	case 0x6:
		lm78_write(dev, dev->addr_register, val, LM78_WINBOND_BANK);

		if (((LM78_WINBOND_BANK == 0) &&
		    ((dev->addr_register == 0x41) || (dev->addr_register == 0x43) || (dev->addr_register == 0x45) || (dev->addr_register == 0x56) ||
		     ((dev->addr_register >= 0x60) && (dev->addr_register < 0x7f)))) ||
		    ((dev->local & LM78_W83782D) && (LM78_WINBOND_BANK == 5) && (dev->addr_register >= 0x50) && (dev->addr_register < 0x58))) {
			/* auto-increment registers */
			dev->addr_register++;
		}
		break;
	default:
		lm78_log("LM78: Write %02X to unknown ISA port %d\n", val, port & 0x7);
		break;
    }
}


static uint8_t
lm78_i2c_write(void *bus, uint8_t addr, uint8_t val, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    if (dev->i2c_state == 0) {
	dev->i2c_state = 1;
	dev->addr_register = val;
    } else
	lm78_write(dev, dev->addr_register++, val, LM78_WINBOND_BANK);

    return 1;
}


static uint8_t
lm78_write(lm78_t *dev, uint8_t reg, uint8_t val, uint8_t bank)
{
    lm75_t *lm75;

    lm78_log("LM78: write(%02X, %d, %02X)\n", reg, bank, val);

    if ((reg & 0xf8) == 0x50) {
	if ((bank == 1) || (bank == 2)) {
		/* LM75 registers */
		lm75 = device_get_priv(dev->lm75[bank - 1]);
		if (lm75)
			lm75_write(lm75, reg, val);
		return 1;
	} else if (dev->local & LM78_W83782D) {
		/* W83782D additional registers */
		if (bank == 4) {
			switch (reg) {
				case 0x50: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
				case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5d: case 0x5e: case 0x5f:
					/* read-only registers */
					return 0;
			}

			dev->regs_782d[0][reg & 0x0f] = val;
			return 1;
		} else if (bank == 5) {
			switch (reg) {
				case 0x50: case 0x51: case 0x52: case 0x53: case 0x58: case 0x59: case 0x5a:
				case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
					/* read-only registers */
					return 0;
			}

			dev->regs_782d[1][reg & 0x0f] = val;
			return 1;
		} else if (bank == 6) {
			return 0;
		}
	}
    }

    /* regular registers */
    switch (reg) {
	case 0x41: case 0x42: case 0x4f: case 0x58:
	case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2a:
	case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69: case 0x6a:
		/* read-only registers */
		return 0;

	case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e:
		/* Winbond-only registers */
		if (!(dev->local & LM78_WINBOND))
			return 0;
		break;
    }

    if ((reg >= 0x60) && (reg <= 0x7f)) /* write auto-increment value RAM registers to their non-auto-increment locations */
	dev->regs[reg & 0x3f] = val;
    else if ((reg >= 0x80) && (reg <= 0x92)) /* AS99127F mirrors [0x00:0x12] to [0x80:0x92] */
	dev->regs[reg & 0x7f] = val;
    else
	dev->regs[reg] = val;

    switch (reg) {
	case 0x40:
		if (val & 0x80) /* INITIALIZATION bit resets all registers except main I2C address */
			lm78_reset(dev, 1);
		break;

	case 0x48:
		/* set main I2C address */
		if (dev->local & LM78_I2C)
			lm78_remap(dev, dev->regs[0x48] & 0x7f);
		break;

	case 0x49:
		if (!(dev->local & LM78_WINBOND)) {
			if (val & 0x20) /* Chip Reset bit (LM78 only) resets all registers */
				lm78_reset(dev, 0);
			else
				dev->regs[0x49] = 0x40;
		} else {
			dev->regs[0x49] &= 0x01;
		}
		break;

	case 0x4a:
		/* set LM75 I2C addresses (Winbond only) */
		if (dev->local & LM78_I2C) {
			for (uint8_t i = 0; i <= 1; i++) {
				lm75 = device_get_priv(dev->lm75[i]);
				if (!lm75)
					continue;
				if (dev->regs[0x4a] & (0x08 * (0x10 * i))) /* DIS_T2 and DIS_T3 bit disable those interfaces */
					lm75_remap(lm75, 0x80);
				else
					lm75_remap(lm75, 0x48 + ((dev->regs[0x4a] >> (i * 4)) & 0x7));
			}
		}
		break;

	case 0x87:
		/* AS99127F boards perform a soft reset through this register */
		if ((dev->local & LM78_AS99127F) && (val == 0x01)) {
			lm78_log("LM78: Reset requested through AS99127F\n");
			resetx86();
		}
		break;
    }

    return 1;
}


static void
lm78_reset(lm78_t *dev, uint8_t initialization)
{
    memset(dev->regs, 0, 256);
    memset(dev->regs + 0xc0, 0xff, 32); /* C0-DF are 0xFF at least on the AS99127F */

    dev->regs[0x40] = 0x08;
    dev->regs[0x46] = 0x40;
    dev->regs[0x47] = 0x50;
    if (dev->local & LM78_I2C) {
	if (!initialization) /* don't reset main I2C address if the reset was triggered by the INITIALIZATION bit */
		dev->i2c_addr = 0x2d;
	dev->regs[0x48] = dev->i2c_addr;
	if (dev->local & LM78_WINBOND)
		dev->regs[0x4a] = 0x01;
    } else {
	dev->regs[0x48] = 0x00;
	if (dev->local & LM78_WINBOND)
		dev->regs[0x4a] = 0x88;
    }
    if (dev->local & LM78_WINBOND) {
	dev->regs[0x49] = 0x02;
	dev->regs[0x4b] = 0x44;
	dev->regs[0x4c] = 0x01;
	dev->regs[0x4d] = 0x15;
	dev->regs[0x4e] = 0x80;
	dev->regs[0x4f] = (LM78_WINBOND_VENDOR_ID >> 8);
	dev->regs[0x57] = 0x80;

	/* Initialize proprietary registers on the AS99127F. The BIOS accesses some
	   of these on boot through read_byte_cmd on the TEMP2 address, hanging on
	   POST code C1 if they're defaulted to 0. There's no documentation on what
	   these are for. The following values were dumped from a live, initialized
	   AS99127F Rev. 2 on a P4B motherboard, and they seem to work well enough. */
	if (dev->local & LM78_AS99127F) {
		/* 0x00 appears to mirror IN2 Low Limit */
		/* 0x01 appears to mirror IN3 */
		dev->regs[0x02] = LM78_VOLTAGE_TO_REG(2800); /* appears to be a "maximum VCORE" of some kind; must read 2.8V on P3 boards */
		dev->regs[0x03] = 0x60;
		/* 0x04 appears to mirror IN3 */
		/* 0x05 appears to mirror IN2 */
		dev->regs[0x07] = 0xcd;
		/* 0x08 appears to mirror IN3 Low Limit */
		dev->regs[0x09] = dev->regs[0x0f] = dev->regs[0x11] = 0xf8; /* three instances of */
		dev->regs[0x0a] = dev->regs[0x10] = dev->regs[0x12] = 0xa5; /* the same word      */
		dev->regs[0x0b] = 0xac;
		dev->regs[0x0c] = 0x8c;
		dev->regs[0x0d] = 0x68;
		dev->regs[0x0e] = 0x54;

		dev->regs[0x53] = dev->regs[0x54] = dev->regs[0x55] = 0xff;
		dev->regs[0x58] = 0x31;
		dev->regs[0x59] = dev->regs[0x5a] = 0x8f;
		dev->regs[0x5c] = 0xe0;
		dev->regs[0x5d] = 0x48;
		dev->regs[0x5e] = 0xe2;
		dev->regs[0x5f] = 0x3f;
	} else if (dev->local & LM78_W83781D) {
		dev->regs[0x58] = 0x10;
	} else if (dev->local & LM78_W83782D) {
		dev->regs[0x58] = 0x30;
	}
    } else {
	dev->regs[0x49] = 0x40;
    }

    lm78_remap(dev, dev->i2c_addr);
}


static void
lm78_close(void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    uint16_t isa_io = dev->local & 0xffff;
    if (isa_io)
	io_removehandler(isa_io, 8, lm78_isa_read, NULL, NULL, lm78_isa_write, NULL, NULL, dev);

    free(dev);
}


static void *
lm78_init(const device_t *info)
{
    lm78_t *dev = (lm78_t *) malloc(sizeof(lm78_t));
    memset(dev, 0, sizeof(lm78_t));

    dev->local = info->local;

    /* Set global default values. */
    hwm_values_t defaults = {
	{    /* fan speeds */
		3000,	/* usually Chassis, sometimes CPU */
		3000,	/* usually CPU, sometimes Chassis */
		3000	/* usually PSU, sometimes Chassis */
	}, { /* temperatures */
		30,	/* usually Board, sometimes Chassis */
		30,	/* Winbond only: usually CPU, sometimes Probe */
		30	/* Winbond only: usually CPU when not the one above */
	}, { /* voltages */
		hwm_get_vcore(),		 /* Vcore */
		0,				 /* sometimes Vtt, Vio or second CPU */
		3300,				 /* +3.3V */
		RESISTOR_DIVIDER(5000,  11, 16), /* +5V  (divider values bruteforced) */
		RESISTOR_DIVIDER(12000, 28, 10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
		LM78_NEG_VOLTAGE(12000, 2100),	 /* -12V */
		LM78_NEG_VOLTAGE(5000,  909),	 /* -5V */
		RESISTOR_DIVIDER(5000,  51, 75), /* W83782D only: +5VSB (5.1K/7.5K divider suggested in the datasheet) */
		3000				 /* W83782D only: Vbat */
	}
    };

    /* Set chip-specific default values. */
    if (dev->local & LM78_AS99127F) {
	/* AS99127: different -12V Rin value (bruteforced) */
	defaults.voltages[5] = LM78_NEG_VOLTAGE(12000, 2400);
    } else if (dev->local & LM78_W83782D) {
	/* W83782D: different negative voltage formula */
	defaults.voltages[5] = LM78_NEG_VOLTAGE2(12000, 232);
	defaults.voltages[6] = LM78_NEG_VOLTAGE2(5000,  120);
    }

    hwm_values = defaults;
    dev->values = &hwm_values;

    /* Initialize secondary/tertiary LM75 sensors on Winbond. */
    for (uint8_t i = 0; i <= 1; i++) {
	if (dev->local & LM78_WINBOND) {
		dev->lm75[i] = (device_t *) malloc(sizeof(device_t));
		memcpy(dev->lm75[i], &lm75_w83781d_device, sizeof(device_t));
		dev->lm75[i]->local = (i + 1) << 8;
		if (dev->local & LM78_I2C)
			dev->lm75[i]->local |= 0x48 + i;
		device_add(dev->lm75[i]);
	} else {
		dev->lm75[i] = NULL;
	}
    }

    lm78_reset(dev, 0);

    uint16_t isa_io = dev->local & 0xffff;
    if (isa_io)
	io_sethandler(isa_io, 8, lm78_isa_read, NULL, NULL, lm78_isa_write, NULL, NULL, dev);

    return dev;
}


/* National Semiconductor LM78 on ISA and SMBus. */
const device_t lm78_device = {
    "National Semiconductor LM78 Hardware Monitor",
    DEVICE_ISA,
    0x290 | LM78_I2C,
    lm78_init, lm78_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


/* Winbond W83781D (or ASUS AS97127F) on ISA and SMBus. */
const device_t w83781d_device = {
    "Winbond W83781D Hardware Monitor",
    DEVICE_ISA,
    0x290 | LM78_I2C | LM78_W83781D,
    lm78_init, lm78_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


/* The ASUS AS99127F is a customized W83781D with no ISA interface (I2C
   only), added proprietary registers and different chip/vendor IDs. */
const device_t as99127f_device = {
    "ASUS AS99127F Rev. 1 Hardware Monitor",
    DEVICE_ISA,
    LM78_I2C | LM78_AS99127F_REV1,
    lm78_init, lm78_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


/* Rev. 2 changes the vendor ID back to Winbond's and brings some other changes. */
const device_t as99127f_rev2_device = {
    "ASUS AS99127F Rev. 2 Hardware Monitor",
    DEVICE_ISA,
    LM78_I2C | LM78_AS99127F_REV2,
    lm78_init, lm78_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};


/* Winbond W83782D on ISA and SMBus. */
const device_t w83782d_device = {
    "Winbond W83782D Hardware Monitor",
    DEVICE_ISA,
    0x290 | LM78_I2C | LM78_W83782D,
    lm78_init, lm78_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
