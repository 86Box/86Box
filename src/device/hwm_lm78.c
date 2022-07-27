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
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/nvr.h>
#include "cpu.h"
#include <86box/i2c.h>
#include <86box/hwm.h>


#define LM78_I2C		0x010000
#define LM78_W83781D		0x020000
#define LM78_AS99127F_REV1	0x040000
#define LM78_AS99127F_REV2	0x080000
#define LM78_W83782D		0x100000
#define LM78_P5A		0x200000
#define LM78_AS99127F		(LM78_AS99127F_REV1 | LM78_AS99127F_REV2) /* mask covering both _REV1 and _REV2 */
#define LM78_WINBOND		(LM78_W83781D | LM78_AS99127F | LM78_W83782D) /* mask covering all Winbond variants */
#define LM78_WINBOND_VENDOR_ID	((dev->local & LM78_AS99127F_REV1) ? 0x12c3 : 0x5ca3)
#define LM78_WINBOND_BANK	(dev->regs[0x4e] & 0x07)

#define CLAMP(a, min, max)	(((a) < (min)) ? (min) : (((a) > (max)) ? (max) : (a)))
#define LM78_RPM_TO_REG(r, d)	((r) ? CLAMP(1350000 / (r * d), 1, 255) : 0)
#define LM78_VOLTAGE_TO_REG(v)	((v) >> 4)
#define LM78_NEG_VOLTAGE(v, r)	(v * (604.0 / ((double) r))) /* negative voltage formula from the W83781D datasheet */
#define LM78_NEG_VOLTAGE2(v, r)	(((3600 + v) * (((double) r) / (((double) r) + 56.0))) - v) /* negative voltage formula from the W83782D datasheet */


typedef struct {
    uint32_t	local;
    hwm_values_t *values;
    device_t    *lm75[2];
    pc_timer_t	reset_timer;

    uint8_t	regs[256];
    union {
	struct {
		uint8_t	regs[2][16];
	} w83782d;
	struct {
		uint8_t regs[3][128];

		uint8_t	nvram[1024], nvram_i2c_state: 2, nvram_updated: 1;
		uint16_t nvram_addr_register: 10;
		int8_t	nvram_block_len: 6;

		uint8_t	security_i2c_state: 1, security_addr_register: 7;
	} as99127f;
    };
    uint8_t	addr_register, data_register;

    uint8_t	i2c_addr: 7, i2c_state: 1, i2c_enabled: 1;
} lm78_t;


static void	lm78_remap(lm78_t *dev, uint8_t addr);


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


void
lm78_nvram(lm78_t *dev, uint8_t save)
{
    size_t l = strlen(machine_get_internal_name_ex(machine)) + 14;
    char *nvr_path = (char *) malloc(l);
    sprintf(nvr_path, "%s_as99127f.nvr", machine_get_internal_name_ex(machine));

    FILE *f = nvr_fopen(nvr_path, save ? "wb" : "rb");
    if (f) {
	if (save)
		fwrite(&dev->as99127f.nvram, sizeof(dev->as99127f.nvram), 1, f);
	else
		fread(&dev->as99127f.nvram, sizeof(dev->as99127f.nvram), 1, f);
	fclose(f);
    }

    free(nvr_path);
}


static uint8_t
lm78_nvram_start(void *bus, uint8_t addr, uint8_t read, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    dev->as99127f.nvram_i2c_state = 0;

    return 1;
}


static uint8_t
lm78_nvram_read(void *bus, uint8_t addr, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;
    uint8_t ret = 0xff;

    switch (dev->as99127f.nvram_i2c_state) {
	case 0:
		dev->as99127f.nvram_i2c_state = 1;
		/* fall-through */

	case 1:
		ret = dev->as99127f.regs[0][0x0b] & 0x3f;
		lm78_log("LM78: nvram_read(blocklen) = %02X\n", ret);
		break;

	case 2:
		ret = dev->as99127f.nvram[dev->as99127f.nvram_addr_register];
		lm78_log("LM78: nvram_read(%03X) = %02X\n", dev->as99127f.nvram_addr_register, ret);

		dev->as99127f.nvram_addr_register++;
		break;

	default:
		lm78_log("LM78: nvram_read(unknown) = %02X\n", ret);
		break;
    }

    if (dev->as99127f.nvram_i2c_state < 2)
	dev->as99127f.nvram_i2c_state++;

    return ret;
}


static uint8_t
lm78_nvram_write(void *bus, uint8_t addr, uint8_t val, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    switch (dev->as99127f.nvram_i2c_state) {
	case 0:
		lm78_log("LM78: nvram_write(address, %02X)\n", val);
		dev->as99127f.nvram_addr_register = (addr << 8) | val;
		break;

	case 1:
		lm78_log("LM78: nvram_write(blocklen, %02X)\n", val);
		dev->as99127f.nvram_block_len = val & 0x3f;
		if (dev->as99127f.nvram_block_len <= 0)
			dev->as99127f.nvram_i2c_state = 3;
		break;

	case 2:
		lm78_log("LM78: nvram_write(%03X, %02X)\n", dev->as99127f.nvram_addr_register, val);
		dev->as99127f.nvram[dev->as99127f.nvram_addr_register++] = val;
		dev->as99127f.nvram_updated = 1;
		if (--dev->as99127f.nvram_block_len <= 0)
			dev->as99127f.nvram_i2c_state = 3;
		break;

	default:
		lm78_log("LM78: nvram_write(unknown, %02X)\n", val);
		break;
    }

    if (dev->as99127f.nvram_i2c_state < 2)
	dev->as99127f.nvram_i2c_state++;

    return dev->as99127f.nvram_i2c_state < 3;
}


static uint8_t
lm78_security_start(void *bus, uint8_t addr, uint8_t read, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    dev->as99127f.security_i2c_state = 0;

    return 1;
}


static uint8_t
lm78_security_read(void *bus, uint8_t addr, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    return dev->as99127f.regs[2][dev->as99127f.security_addr_register++];
}


static uint8_t
lm78_security_write(void *bus, uint8_t addr, uint8_t val, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    if (dev->as99127f.security_i2c_state == 0) {
	dev->as99127f.security_i2c_state = 1;
	dev->as99127f.security_addr_register = val;
    } else {
	switch (dev->as99127f.security_addr_register) {
		case 0xe0: case 0xe4: case 0xe5: case 0xe6: case 0xe7:
			/* read-only registers */
			return 1;
	}

	dev->as99127f.regs[2][dev->as99127f.security_addr_register++] = val;
    }

    return 1;
}


static void
lm78_reset(void *priv)
{
    lm78_t *dev = (lm78_t *) priv;
    uint8_t initialization = dev->regs[0x40] & 0x80;

    memset(dev->regs, 0, 256);
    memset(dev->regs + 0xc0, 0xff, 32); /* C0-DF are 0xFF on a real AS99127F */

    dev->regs[0x40] = 0x08;
    dev->regs[0x46] = 0x40;
    dev->regs[0x47] = 0x50;
    if (dev->local & LM78_I2C) {
	if (!initialization) { /* don't reset main I2C address if the reset was triggered by the INITIALIZATION bit */
		if (dev->local & LM78_P5A)
			dev->i2c_addr = 0x77;
		else
			dev->i2c_addr = 0x2d;
		dev->i2c_enabled = 1;
	}
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
	dev->regs[0x4f] = LM78_WINBOND_VENDOR_ID >> 8;
	dev->regs[0x57] = 0x80;

	if (dev->local & LM78_AS99127F) {
		dev->regs[0x49] = 0x20;
		dev->regs[0x4c] = 0x00;
		dev->regs[0x56] = 0xff;
		dev->regs[0x57] = 0xff;
		dev->regs[0x58] = 0x31;
		dev->regs[0x59] = 0x8f;
		dev->regs[0x5a] = 0x8f;
		dev->regs[0x5b] = 0x2a;
		dev->regs[0x5c] = 0xe0;
		dev->regs[0x5d] = 0x48;
		dev->regs[0x5e] = 0xe2;
		dev->regs[0x5f] = 0x1f;

		dev->as99127f.regs[0][0x02] = 0xff;
		dev->as99127f.regs[0][0x03] = 0xff;
		dev->as99127f.regs[0][0x08] = 0xff;
		dev->as99127f.regs[0][0x09] = 0xff;
		dev->as99127f.regs[0][0x0b] = 0x01;

		/* regs[1] and regs[2] start at 0x80 */
		dev->as99127f.regs[1][0x00] = 0x88;
		dev->as99127f.regs[1][0x01] = 0x10;
		dev->as99127f.regs[1][0x03] = 0x02; /* GPO, but things break if GPO16 isn't set */
		dev->as99127f.regs[1][0x04] = 0x01;
		dev->as99127f.regs[1][0x05] = 0x1f;
		lm78_as99127f_write(dev, 0x06, 0x2f);

		dev->as99127f.regs[2][0x60] = 0xf0;
	} else if (dev->local & LM78_W83781D) {
		dev->regs[0x58] = 0x10;
	} else if (dev->local & LM78_W83782D) {
		dev->regs[0x58] = 0x30;
	}
    } else {
	dev->regs[0x49] = 0x40;
    }

    lm78_remap(dev, dev->i2c_addr | (dev->i2c_enabled ? 0x00 : 0x80));
}


static uint8_t
lm78_i2c_start(void *bus, uint8_t addr, uint8_t read, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    dev->i2c_state = 0;

    return 1;
}


static uint8_t
lm78_read(lm78_t *dev, uint8_t reg, uint8_t bank)
{
    uint8_t ret = 0, masked_reg = reg, bankswitched = ((reg & 0xf8) == 0x50);
    lm75_t *lm75;

    if ((dev->local & LM78_AS99127F) && (bank == 3) && (reg != 0x4e)) {
	/* AS99127F additional registers */
	if (!((dev->local & LM78_AS99127F_REV2) && ((reg == 0x80) || (reg == 0x81))))
		ret = dev->as99127f.regs[0][reg & 0x7f];
    } else if (bankswitched && ((bank == 1) || (bank == 2))) {
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
			ret = dev->w83782d.regs[bank - 4][reg & 0x0f];
	}
    } else {
	/* regular registers */
	if ((reg >= 0x60) && (reg <= 0x94)) /* read auto-increment value RAM registers from their non-auto-increment locations */
		masked_reg = reg & 0x3f;
	if ((masked_reg >= 0x20) && (masked_reg <= 0x26)) /* voltages */
		ret = LM78_VOLTAGE_TO_REG(dev->values->voltages[reg & 7]);
	else if ((dev->local & LM78_AS99127F) && (masked_reg <= 0x05)) /* AS99127F additional voltages */
		ret = LM78_VOLTAGE_TO_REG(dev->values->voltages[7 + masked_reg]);
	else if (masked_reg == 0x27) /* temperature */
		ret = dev->values->temperatures[0];
	else if ((masked_reg >= 0x28) && (masked_reg <= 0x2a)) { /* fan speeds */
		ret = (dev->regs[((reg & 3) == 2) ? 0x4b : 0x47] >> ((reg & 3) ? 6 : 4)) & 0x03; /* bits [1:0] */
		if (dev->local & LM78_W83782D)
			ret |= (dev->regs[0x5d] >> (3 + (reg & 3))) & 0x04; /* bit 2 */
		ret = LM78_RPM_TO_REG(dev->values->fans[reg & 3], 1 << ret);
	} else if ((reg == 0x4f) && (dev->local & LM78_WINBOND)) /* two-byte vendor ID register */
		ret = (dev->regs[0x4e] & 0x80) ? (uint8_t) (LM78_WINBOND_VENDOR_ID >> 8) : (uint8_t) LM78_WINBOND_VENDOR_ID;
	else
		ret = dev->regs[masked_reg];
    }

    lm78_log("LM78: read(%02X, %d) = %02X\n", reg, bank, ret);

    return ret;
}


static uint8_t
lm78_isa_read(uint16_t port, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;
    uint8_t ret = 0xff;

    switch (port & 0x7) {
	case 0x5:
		ret = dev->addr_register & 0x7f;
		break;

	case 0x6:
		ret = lm78_read(dev, dev->addr_register, LM78_WINBOND_BANK);

		if (((LM78_WINBOND_BANK == 0) &&
		    ((dev->addr_register == 0x41) || (dev->addr_register == 0x43) || (dev->addr_register == 0x45) || (dev->addr_register == 0x56) ||
		     ((dev->addr_register >= 0x60) && (dev->addr_register < 0x94)))) ||
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


uint8_t
lm78_as99127f_read(void *priv, uint8_t reg)
{
    lm78_t *dev = (lm78_t *) priv;
    uint8_t ret = dev->as99127f.regs[1][reg & 0x7f];

    lm78_log("LM78: read(%02X, AS99127F) = %02X\n", reg, ret);

    return ret;
}


static uint8_t
lm78_write(lm78_t *dev, uint8_t reg, uint8_t val, uint8_t bank)
{
    lm75_t *lm75;

    lm78_log("LM78: write(%02X, %d, %02X)\n", reg, bank, val);

    if ((dev->local & LM78_AS99127F) && (bank == 3) && (reg != 0x4e)) {
	/* AS99127F additional registers */
	reg &= 0x7f;
	switch (reg) {
		case 0x00: case 0x01: case 0x04: case 0x05: case 0x06: case 0x07:
			/* read-only registers */
			return 0;

		case 0x20:
			val &= 0x7f;
			break;
	}

	dev->as99127f.regs[0][reg] = val;
	return 1;
    } else if ((reg & 0xf8) == 0x50) {
	if ((bank == 1) || (bank == 2)) {
		/* LM75 registers */
		lm75 = device_get_priv(dev->lm75[bank - 1]);
		if (lm75)
			return lm75_write(lm75, reg, val);
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

			dev->w83782d.regs[0][reg & 0x0f] = val;
			return 1;
		} else if (bank == 5) {
			switch (reg) {
				case 0x50: case 0x51: case 0x52: case 0x53: case 0x58: case 0x59: case 0x5a:
				case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
					/* read-only registers */
					return 0;
			}

			dev->w83782d.regs[1][reg & 0x0f] = val;
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
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
	case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
		/* read-only registers */
		return 0;

	case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e:
		/* Winbond-only registers */
		if (!(dev->local & LM78_WINBOND))
			return 0;
		break;
    }

    if ((reg >= 0x60) && (reg <= 0x94)) /* write auto-increment value RAM registers to their non-auto-increment locations */
	reg &= 0x3f;
    uint8_t prev = dev->regs[reg];
    dev->regs[reg] = val;

    switch (reg) {
	case 0x40:
		if (val & 0x80) /* INITIALIZATION bit resets all registers except main I2C address */
			lm78_reset(dev);
		break;

	case 0x48:
		/* set main I2C address */
		if (dev->local & LM78_I2C)
			lm78_remap(dev, dev->regs[0x48] & 0x7f);
		break;

	case 0x49:
		if (!(dev->local & LM78_WINBOND)) {
			if (val & 0x20) /* Chip Reset bit (LM78 only) resets all registers */
				lm78_reset(dev);
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
				if (val & (0x08 * (0x10 * i))) /* DIS_T2 and DIS_T3 bit disable those interfaces */
					lm75_remap(lm75, 0x80);
				else
					lm75_remap(lm75, 0x48 + ((val >> (i * 4)) & 0x7));
			}
		}
		break;

	case 0x5c:
		/* enable/disable AS99127F NVRAM */
		if (dev->local & LM78_AS99127F) {
			if (prev & 0x01)
				i2c_removehandler(i2c_smbus, (prev & 0xf8) >> 1, 4, lm78_nvram_start, lm78_nvram_read, lm78_nvram_write, NULL, dev);
			if (val & 0x01)
				i2c_sethandler(i2c_smbus, (val & 0xf8) >> 1, 4, lm78_nvram_start, lm78_nvram_read, lm78_nvram_write, NULL, dev);
		}
		break;
    }

    return 1;
}


static void
lm78_isa_write(uint16_t port, uint8_t val, void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    switch (port & 0x7) {
	case 0x5:
		dev->addr_register = val & 0x7f;
		break;

	case 0x6:
		lm78_write(dev, dev->addr_register, val, LM78_WINBOND_BANK);

		if (((LM78_WINBOND_BANK == 0) &&
		    ((dev->addr_register == 0x41) || (dev->addr_register == 0x43) || (dev->addr_register == 0x45) || (dev->addr_register == 0x56) ||
		     ((dev->addr_register >= 0x60) && (dev->addr_register < 0x94)))) ||
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


uint8_t
lm78_as99127f_write(void *priv, uint8_t reg, uint8_t val)
{
    lm78_t *dev = (lm78_t *) priv;

    lm78_log("LM78: write(%02X, AS99127F, %02X)\n", reg, val);

    reg &= 0x7f;
    uint8_t prev = dev->as99127f.regs[1][reg];
    dev->as99127f.regs[1][reg] = val;

    switch (reg) {
	case 0x01:
		if (val & 0x40) {
			dev->as99127f.regs[1][0x00]  = 0x88;
			dev->as99127f.regs[1][0x01] &= 0xe0;
			dev->as99127f.regs[1][0x03] &= 0xf7;
			dev->as99127f.regs[1][0x07] &= 0xfe;
		}
		if (!(val & 0x10)) { /* CUV4X-LS */
			lm78_log("LM78: Reset requested through AS99127F CLKRST\n");
			timer_set_delay_u64(&dev->reset_timer, 300000 * TIMER_USEC);
		}
		break;

	case 0x06:
		/* security device I2C address */
		i2c_removehandler(i2c_smbus, prev & 0x7f, 1, lm78_security_start, lm78_security_read, lm78_security_write, NULL, dev);
		i2c_sethandler(i2c_smbus, val & 0x7f, 1, lm78_security_start, lm78_security_read, lm78_security_write, NULL, dev);
		break;

	case 0x07:
		if (val & 0x01) { /* other AS99127F boards */
			lm78_log("LM78: Reset requested through AS99127F GPO15\n");
			resetx86();
		}
		break;
    }

    return 1;
}


static void
lm78_reset_timer(void *priv)
{
    pc_reset_hard();
}


static void
lm78_remap(lm78_t *dev, uint8_t addr)
{
    lm75_t *lm75;

    if (!(dev->local & LM78_I2C)) return;

    lm78_log("LM78: remapping to SMBus %02Xh\n", addr);

    if (dev->i2c_enabled)
	i2c_removehandler(i2c_smbus, dev->i2c_addr, 1, lm78_i2c_start, lm78_i2c_read, lm78_i2c_write, NULL, dev);

    if (addr < 0x80)
	i2c_sethandler(i2c_smbus, addr, 1, lm78_i2c_start, lm78_i2c_read, lm78_i2c_write, NULL, dev);

    dev->i2c_addr = addr & 0x7f;
    dev->i2c_enabled = !(addr & 0x80);

    if (dev->local & LM78_AS99127F) {
	/* Store our handle on the primary LM75 device to ensure reads/writes
	   to the AS99127F's proprietary registers are passed through to this side. */
	if ((lm75 = device_get_priv(dev->lm75[0])))
		lm75->as99127f = dev;
    }
}


static void
lm78_close(void *priv)
{
    lm78_t *dev = (lm78_t *) priv;

    uint16_t isa_io = dev->local & 0xffff;
    if (isa_io)
	io_removehandler(isa_io, 8, lm78_isa_read, NULL, NULL, lm78_isa_write, NULL, NULL, dev);

    if (dev->as99127f.nvram_updated)
	lm78_nvram(dev, 1);

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
		RESISTOR_DIVIDER(5000,  51, 75), /* W83782D/AS99127F only: +5VSB (5.1K/7.5K divider suggested in the datasheet) */
		3000,				 /* W83782D/AS99127F only: Vbat */
		2500,				 /* AS99127F only: +2.5V */
		1500,				 /* AS99127F only: +1.5V */
		3000,				 /* AS99127F only: NVRAM */
		3300				 /* AS99127F only: +3.3VSB */
	}
    };

    /* Set chip-specific default values. */
    if (dev->local & LM78_AS99127F) {
	/* AS99127F: different -12V Rin value (bruteforced) */
	defaults.voltages[5] = LM78_NEG_VOLTAGE(12000, 2400);

	timer_add(&dev->reset_timer, lm78_reset_timer, dev, 0);

	lm78_nvram(dev, 0);
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

    lm78_reset(dev);

    uint16_t isa_io = dev->local & 0xffff;
    if (isa_io)
	io_sethandler(isa_io, 8, lm78_isa_read, NULL, NULL, lm78_isa_write, NULL, NULL, dev);

    return dev;
}

/* National Semiconductor LM78 on ISA and SMBus. */
const device_t lm78_device = {
    .name = "National Semiconductor LM78 Hardware Monitor",
    .internal_name = "lm78",
    .flags = DEVICE_ISA,
    .local = 0x290 | LM78_I2C,
    .init = lm78_init,
    .close = lm78_close,
    .reset = lm78_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

/* Winbond W83781D on ISA and SMBus. */
const device_t w83781d_device = {
    .name = "Winbond W83781D Hardware Monitor",
    .internal_name = "w83781d",
    .flags = DEVICE_ISA,
    .local = 0x290 | LM78_I2C | LM78_W83781D,
    .init = lm78_init,
    .close = lm78_close,
    .reset = lm78_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

/* Winbond W83781D on ISA and SMBus. */
const device_t w83781d_p5a_device = {
    .name = "Winbond W83781D Hardware Monitor (ASUS P5A)",
    .internal_name = "w83781d_p5a",
    .flags = DEVICE_ISA,
    .local = 0x290 | LM78_I2C | LM78_W83781D | LM78_P5A,
    .init = lm78_init,
    .close = lm78_close,
    .reset = lm78_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

/* The AS99127F is an ASIC manufactured by Holtek for ASUS, containing an
   I2C-only W83781D clone with additional voltages, GPIOs and fan control. */
const device_t as99127f_device = {
    .name = "ASUS AS99127F Rev. 1 Hardware Monitor",
    .internal_name = "as99137f",
    .flags = DEVICE_ISA,
    .local = LM78_I2C | LM78_AS99127F_REV1,
    .init = lm78_init,
    .close = lm78_close,
    .reset = lm78_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

/* Rev. 2 is manufactured by Winbond and differs only in GPI registers. */
const device_t as99127f_rev2_device = {
    .name = "ASUS AS99127F Rev. 2 Hardware Monitor",
    .internal_name = "as99127f_rev2",
    .flags = DEVICE_ISA,
    .local = LM78_I2C | LM78_AS99127F_REV2,
    .init = lm78_init,
    .close = lm78_close,
    .reset = lm78_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

/* Winbond W83782D on ISA and SMBus. */
const device_t w83782d_device = {
    .name = "Winbond W83782D Hardware Monitor",
    .internal_name = "w83783d",
    .flags = DEVICE_ISA,
    .local = 0x290 | LM78_I2C | LM78_W83782D,
    .init = lm78_init,
    .close = lm78_close,
    .reset = lm78_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
