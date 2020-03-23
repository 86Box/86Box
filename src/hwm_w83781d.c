/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Winbond W83781D hardware monitoring chip.
 *
 * Version:	@(#)hwm_w83781d.c 1.0.0	2020/03/21
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *		Copyright 2020 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "device.h"
#include "86box_io.h"
#include "smbus.h"
#include "hwm.h"


#define W83781D_SMBUS		0x10000
#define W83781D_AS99127F	0x20000
#define W83781D_VENDOR_ID	((dev->local & W83781D_AS99127F) ? 0x12C3 : 0x5CA3)

#define CLAMP(a, min, max) (((a) < (min)) ? (min) : (((a) > (max)) ? (max) : (a)))
#define W83781D_RPM_TO_REG(r, d)	CLAMP(1350000 / (r * d), 1, 255)
#define W83781D_TEMP_TO_REG(t)		((t) * 8) << 5


typedef struct {
    uint32_t	  local;
    hwm_values_t* values;

    uint8_t regs[64];
    uint8_t regs_bank1[6];
    uint8_t regs_bank2[6];
    uint8_t addr_register;
    uint8_t data_register;

    uint8_t smbus_addr_main;
    uint8_t smbus_addr_temp2;
    uint8_t smbus_addr_temp3;
    uint8_t hbacs;
    uint8_t active_bank;
} w83781d_t;


static uint8_t	w83781d_isa_read(uint16_t port, void *priv);
static uint8_t	w83781d_smbus_read_byte(uint8_t addr, void *priv);
static uint8_t	w83781d_smbus_read_byte_cmd(uint8_t addr, uint8_t cmd, void *priv);
static uint16_t	w83781d_smbus_read_word_cmd(uint8_t addr, uint8_t cmd, void *priv);
static uint8_t	w83781d_read(w83781d_t *dev, uint8_t reg, uint8_t bank);
static void	w83781d_isa_write(uint16_t port, uint8_t val, void *priv);
static void	w83781d_smbus_write_byte(uint8_t addr, uint8_t val, void *priv);
static void	w83781d_smbus_write_byte_cmd(uint8_t addr, uint8_t cmd, uint8_t val, void *priv);
static void	w83781d_smbus_write_word_cmd(uint8_t addr, uint8_t cmd, uint16_t val, void *priv);
static uint8_t	w83781d_write(w83781d_t *dev, uint8_t reg, uint8_t val, uint8_t bank);
static void	w83781d_reset(w83781d_t *dev, uint8_t initialization);


static void
w83781d_remap(w83781d_t *dev)
{
    if (!(dev->local & W83781D_SMBUS)) return;

    smbus_removehandler(0x00, 0x80,
    			w83781d_smbus_read_byte, w83781d_smbus_read_byte_cmd, w83781d_smbus_read_word_cmd, NULL,
    			w83781d_smbus_write_byte, w83781d_smbus_write_byte_cmd, w83781d_smbus_write_word_cmd, NULL,
    			dev);

    if (dev->smbus_addr_main) smbus_sethandler(dev->smbus_addr_main, 1,
    			w83781d_smbus_read_byte, w83781d_smbus_read_byte_cmd, w83781d_smbus_read_word_cmd, NULL,
    			w83781d_smbus_write_byte, w83781d_smbus_write_byte_cmd, w83781d_smbus_write_word_cmd, NULL,
    			dev);

    if (dev->smbus_addr_temp2) smbus_sethandler(dev->smbus_addr_temp2, 1,
    			w83781d_smbus_read_byte, w83781d_smbus_read_byte_cmd, w83781d_smbus_read_word_cmd, NULL,
    			w83781d_smbus_write_byte, w83781d_smbus_write_byte_cmd, w83781d_smbus_write_word_cmd, NULL,
    			dev);

    if (dev->smbus_addr_temp3) smbus_sethandler(dev->smbus_addr_temp3, 1,
    			w83781d_smbus_read_byte, w83781d_smbus_read_byte_cmd, w83781d_smbus_read_word_cmd, NULL,
    			w83781d_smbus_write_byte, w83781d_smbus_write_byte_cmd, w83781d_smbus_write_word_cmd, NULL,
    			dev);
}


static uint8_t
w83781d_isa_read(uint16_t port, void *priv)
{
    w83781d_t *dev = (w83781d_t *) priv;
    uint8_t ret = 0xFF;

    switch (port - (dev->local & 0xFFFF)) {
    	case 0x0:
    		ret = dev->addr_register & 0x7F;
    		break;
    	case 0x1:
    		ret = w83781d_read(dev, dev->addr_register, dev->active_bank);

    		if (dev->active_bank == 0 &&
    		    (dev->addr_register == 0x41 || dev->addr_register == 0x43 || dev->addr_register == 0x45 || dev->addr_register == 0x56 ||
    		     (dev->addr_register >= 0x60 && dev->addr_register < 0x7F))) {
    			/* auto-increment registers */
    			dev->addr_register++;
    		}
    		break;
    }

    return ret;
}


static uint8_t
w83781d_smbus_read_byte(uint8_t addr, void *priv)
{
    w83781d_t *dev = (w83781d_t *) priv;

    return w83781d_read(dev, dev->addr_register, 0);
}


static uint8_t
w83781d_smbus_read_byte_cmd(uint8_t addr, uint8_t cmd, void *priv)
{
    w83781d_t *dev = (w83781d_t *) priv;

    return w83781d_read(dev, cmd, 0);
}


static uint16_t
w83781d_smbus_read_word_cmd(uint8_t addr, uint8_t cmd, void *priv)
{
    w83781d_t *dev = (w83781d_t *) priv;
    uint8_t rethi = 0;
    uint8_t retlo = 0;
    uint8_t bank = 0;

    if (addr == dev->smbus_addr_temp2 || addr == dev->smbus_addr_temp3) {
    	if (addr == dev->smbus_addr_temp2)
    		bank = 2;
    	else
    		bank = 3;

    	switch (cmd & 0x3) {
    		case 0x0:
    			rethi = w83781d_read(dev, 0x50, bank);
    			retlo = w83781d_read(dev, 0x51, bank);
    			break;
    		case 0x1:
    			rethi = retlo = w83781d_read(dev, 0x52, bank);
    			break;
    		case 0x2:
    			rethi = w83781d_read(dev, 0x53, bank);
    			retlo = w83781d_read(dev, 0x54, bank);
    			break;
    		case 0x3:
    			rethi = w83781d_read(dev, 0x55, bank);
    			retlo = w83781d_read(dev, 0x56, bank);
    			break;
    	}

    	return (retlo << 8) | rethi; /* byte-swapped for some reason */
    }

    return w83781d_read(dev, cmd, bank);
}


static uint8_t
w83781d_read(w83781d_t *dev, uint8_t reg, uint8_t bank)
{
    uint8_t ret = 0;

    if ((reg >> 4) == 0x5 && bank != 0) {
    	/* bank-switched temperature registers */
    	if (bank == 1)
    		ret = dev->regs_bank1[reg - 0x50];
    	else
    		ret = dev->regs_bank2[reg - 0x50];
    } else {
    	/* regular registers */
    	if (reg == 0x4F) /* special case for two-byte vendor ID register */
    		ret = dev->hbacs ? (W83781D_VENDOR_ID >> 8) : (W83781D_VENDOR_ID & 0xFF);
    	else if (reg >= 0x60) /* read auto-increment value RAM registers from their non-auto-increment locations */
    		ret = dev->regs[reg - 0x40];
    	else
    		ret = dev->regs[reg - 0x20];
    }

    return ret;
}


static void
w83781d_isa_write(uint16_t port, uint8_t val, void *priv)
{
    w83781d_t *dev = (w83781d_t *) priv;

    switch (port - (dev->local & 0xFFFF)) {
    	case 0x0:
    		dev->addr_register = val & 0x7F;
    		break;
    	case 0x1:
    		w83781d_write(dev, dev->addr_register, val, dev->active_bank);

    		if (dev->active_bank == 0 &&
    		    (dev->addr_register == 0x41 || dev->addr_register == 0x43 || dev->addr_register == 0x45 || dev->addr_register == 0x56 ||
    		     (dev->addr_register >= 0x60 && dev->addr_register < 0x7F))) {
    			/* auto-increment registers */
    			dev->addr_register++;
    		}
    		break;
    }
}


static void
w83781d_smbus_write_byte(uint8_t addr, uint8_t val, void *priv)
{
    w83781d_t *dev = (w83781d_t *) priv;

    dev->addr_register = val;
}


static void
w83781d_smbus_write_byte_cmd(uint8_t addr, uint8_t cmd, uint8_t val, void *priv)
{
    w83781d_t *dev = (w83781d_t *) priv;

    w83781d_write(dev, cmd, val, 0);
}


static void
w83781d_smbus_write_word_cmd(uint8_t addr, uint8_t cmd, uint16_t val, void *priv)
{
    w83781d_t *dev = (w83781d_t *) priv;
    uint8_t valhi = (val >> 8);
    uint8_t vallo = (val & 0xFF);
    uint8_t bank = 0;

    if (addr == dev->smbus_addr_temp2 || addr == dev->smbus_addr_temp3) {
    	if (addr == dev->smbus_addr_temp2)
    		bank = 2;
    	else
    		bank = 3;

    	switch (cmd & 0x3) {
    		case 0x0:
    			w83781d_write(dev, 0x50, valhi, bank);
    			w83781d_write(dev, 0x51, vallo, bank);
    			break;
    		case 0x1:
    			w83781d_write(dev, 0x52, vallo, bank);
    			break;
    		case 0x2:
    			w83781d_write(dev, 0x53, valhi, bank);
    			w83781d_write(dev, 0x54, vallo, bank);
    			break;
    		case 0x3:
    			w83781d_write(dev, 0x55, valhi, bank);
    			w83781d_write(dev, 0x56, vallo, bank);
    			break;
    		break;
    	}
    	return;
    }

    w83781d_write(dev, cmd, vallo, bank);
}


static uint8_t
w83781d_write(w83781d_t *dev, uint8_t reg, uint8_t val, uint8_t bank)
{
    uint8_t remap = 0;

    if ((reg >> 4) == 0x5 && bank != 0) {
    	/* bank-switched temperature registers */
    	switch (reg) {
    		case 0x50: case 0x51:
    			/* read-only registers */
    			return 0;
    	}

    	if (bank == 1)
    		dev->regs_bank1[reg - 0x50] = val;
    	else
    		dev->regs_bank2[reg - 0x50] = val;

    	return 1;
    }

    /* regular registers */
    switch (reg) {
    	case 0x41: case 0x42: case 0x4F: case 0x58:
    	case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A:
    	case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69: case 0x6A:
    		/* read-only registers */
    		return 0;
    }

    if (reg >= 0x60) /* write auto-increment value RAM registers to their non-auto-increment locations */
    	dev->regs[reg - 0x40] = val;
    else
    	dev->regs[reg - 0x20] = val;

    switch (reg) {
    	case 0x40:
    		if (val >> 7) {
    			/* INITIALIZATION bit set: reset all registers except main SMBus address */
    			w83781d_reset(dev, 1);
    		}
    		break;
    	case 0x47:
    		/* update FAN1/FAN2 values to match the new divisor */
    		dev->regs[0x08] = W83781D_RPM_TO_REG(dev->values->fans[0], 1 << ((dev->regs[0x27] >> 4) & 0x3));
    		dev->regs[0x09] = W83781D_RPM_TO_REG(dev->values->fans[1], 1 << ((dev->regs[0x27] >> 6) & 0x3));
    		break;
    	case 0x48:
    		if (dev->local & W83781D_SMBUS) {
    			dev->smbus_addr_main = (dev->regs[0x28] & 0x7F);
    			remap = 1;
    		}
    		break;
    	case 0x4A:
    		if (dev->local & W83781D_SMBUS) {
    			/* DIS_T2 and DIS_T3 bits can disable those interfaces */
    			if ((dev->regs[0x2A] >> 3) & 0x1)
    				dev->smbus_addr_temp2 = 0x00;
    			else
    				dev->smbus_addr_temp2 = 0x48 + (dev->regs[0x2A] & 0x7);
    			if (dev->regs[0x2A] >> 7)
    				dev->smbus_addr_temp3 = 0x00;
    			else
    				dev->smbus_addr_temp3 = 0x48 + ((dev->regs[0x2A] >> 4) & 0x7);
    			remap = 1;
    		}
    		break;
    	case 0x4B:
    		/* update FAN3 value to match the new divisor */
    		dev->regs[0x0A] = W83781D_RPM_TO_REG(dev->values->fans[2], 1 << ((dev->regs[0x2B] >> 6) & 0x3));
    		break;
    	case 0x4E:
    		dev->hbacs = (dev->regs[0x2E] & 0x80);
    		/* FIXME: Winbond's datasheet does not specify how BANKSEL[0:2] work */
    		if (dev->regs[0x2E] & 0x1)
    			dev->active_bank = 0;
    		else if (dev->regs[0x2E] & 0x2)
    			dev->active_bank = 1;
    		else if (dev->regs[0x2E] & 0x4)
    			dev->active_bank = 2;
    		break;
    }

    if (remap)
    	w83781d_remap(dev);

   return 1;
}


static void
w83781d_reset(w83781d_t *dev, uint8_t initialization)
{
    memset(dev->regs, 0, 64);
    memset(dev->regs_bank1, 0, 6);
    memset(dev->regs_bank2, 0, 6);

    /* WARNING: Array elements are register - 0x20. */
    uint8_t i;
    for (i = 0; i <= 6; i++)
    	dev->regs[i] = dev->values->voltages[i];
    dev->regs[0x07] = dev->values->temperatures[0];
    for (i = 0; i <= 2; i++)
    	dev->regs[0x08 + i] = W83781D_RPM_TO_REG(dev->values->fans[i], 2);
    dev->regs[0x20] = 0x01;
    dev->regs[0x26] = 0x40;
    dev->regs[0x27] = 0x50;
    if (dev->local & W83781D_SMBUS) {
    	if (!initialization) /* don't reset main SMBus address if the reset was triggered by the INITIALIZATION bit */
    		dev->smbus_addr_main = 0x2D;
    	dev->regs[0x28] = dev->smbus_addr_main;
    	dev->regs[0x2A] = 0x01;
    	dev->smbus_addr_temp2 = 0x48 + (dev->regs[0x2A] & 0x7);
    	dev->smbus_addr_temp3 = 0x48 + ((dev->regs[0x2A] >> 4) & 0x7);
    } else {
    	dev->regs[0x28] = 0x00;
    	dev->regs[0x2A] = 0x88;
    	dev->smbus_addr_temp2 = dev->smbus_addr_temp3 = 0x00;
    }
    dev->regs[0x29] = 0x02;
    dev->regs[0x2B] = 0x44;
    dev->regs[0x2C] = 0x01;
    dev->regs[0x2D] = 0x15;
    dev->regs[0x2E] = 0x80;
    dev->hbacs = (dev->regs[0x2E] & 0x80);
    dev->regs[0x2F] = W83781D_VENDOR_ID >> 8;
    dev->regs[0x37] = 0x80;
    dev->regs[0x38] = (dev->local & W83781D_AS99127F) ? 0x31 : 0x10;

    /* WARNING: Array elements are register - 0x50. */
    uint16_t temp;
    temp = W83781D_TEMP_TO_REG(dev->values->temperatures[1]);
    dev->regs_bank1[0x0] = temp >> 8;
    dev->regs_bank1[0x1] = temp & 0xFF;
    dev->regs_bank1[0x3] = 0x4B;
    dev->regs_bank1[0x5] = 0x50;
    temp = W83781D_TEMP_TO_REG(dev->values->temperatures[2]);
    dev->regs_bank2[0x0] = temp >> 8;
    dev->regs_bank2[0x1] = temp & 0xFF;
    dev->regs_bank2[0x3] = 0x4B;
    dev->regs_bank2[0x5] = 0x50;

    w83781d_remap(dev);
}


static void
w83781d_close(void *priv)
{
    w83781d_t *dev = (w83781d_t *) priv;

    uint16_t isa_io = dev->local & 0xFFFF;
    if (isa_io)
    	io_removehandler(isa_io, 2, w83781d_isa_read, NULL, NULL, w83781d_isa_write, NULL, NULL, dev);

    free(dev);
}


static void *
w83781d_init(const device_t *info)
{
    w83781d_t *dev = (w83781d_t *) malloc(sizeof(w83781d_t));
    memset(dev, 0, sizeof(w83781d_t));

    dev->local = info->local;
    dev->values = hwm_get_values();
    w83781d_reset(dev, 0);

    uint16_t isa_io = dev->local & 0xFFFF;
    if (isa_io)
    	io_sethandler(isa_io, 2, w83781d_isa_read, NULL, NULL, w83781d_isa_write, NULL, NULL, dev);

    return dev;
}


const device_t w83781d_device = {
    "Winbond W83781D Hardware Monitor",
    DEVICE_ISA,
    0x295 | W83781D_SMBUS,
    w83781d_init, w83781d_close, NULL,
    NULL, NULL, NULL,
    NULL
};


/*
 * ASUS rebadged version of the W83781D.
 * Some claim it's SMBus-only, yet the BIOS clearly reads most values over ISA,
 * except TEMP3 (CPU Temperature) which is read over SMBus.
 */
const device_t as99127f_device = {
    "ASUS AS99127F Hardware Monitor",
    DEVICE_ISA,
    0x295 | W83781D_SMBUS | W83781D_AS99127F,
    w83781d_init, w83781d_close, NULL,
    NULL, NULL, NULL,
    NULL
};
