/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M6117 SoC.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/device.h>
#include <86box/port_92.h>
#include <86box/usb.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/chipset.h>


typedef struct ali6117_t
{
    uint32_t	local;

    /* Main registers (port 22h/23h) */
    uint8_t	unlocked;
    uint8_t	reg_offset;
    uint8_t	regs[256];
} ali6117_t;


#ifdef ENABLE_ALI6117_LOG
int ali6117_do_log = ENABLE_ALI6117_LOG;

static void
ali6117_log(const char *fmt, ...)
{
    va_list ap;

    if (ali6117_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ali6117_log(fmt, ...)
#endif


static void
ali6117_recalcmapping(ali6117_t *dev)
{
    uint8_t reg, bitpair;
    uint32_t base, size;
    int state;

    shadowbios = 0;
    shadowbios_write = 0;

    ali6117_log("ALI6117: Shadowing for A0000-BFFFF (reg 12 bit 1) = %s\n", (dev->regs[0x12] & 0x02) ? "on" : "off");
    mem_set_mem_state(0xa0000, 0x20000, (dev->regs[0x12] & 0x02) ? (MEM_WRITE_INTERNAL | MEM_READ_INTERNAL) : (MEM_WRITE_EXTANY | MEM_READ_EXTANY));

    for (reg = 0; reg <= 1; reg++) {
	for (bitpair = 0; bitpair <= 3; bitpair++) {
		size = 0x8000;
		base = 0xc0000 + (size * ((reg * 4) + bitpair));
		ali6117_log("ALI6117: Shadowing for %05X-%05X (reg %02X bp %d wmask %02X rmask %02X) =", base, base + size - 1, 0x14 + reg, bitpair, 1 << ((bitpair * 2) + 1), 1 << (bitpair * 2));

		state = 0;
		if (dev->regs[0x14 + reg] & (1 << ((bitpair * 2) + 1))) {
			ali6117_log(" w on");
			state |= MEM_WRITE_INTERNAL;
			if (base >= 0xe0000)
				shadowbios_write |= 1;
		} else {
			ali6117_log(" w off");
			state |= MEM_WRITE_EXTANY;
		}
		if (dev->regs[0x14 + reg] & (1 << (bitpair * 2))) {
			ali6117_log("; r on\n");
			state |= MEM_READ_INTERNAL;
			if (base >= 0xe0000)
				shadowbios |= 1;
		} else {
			ali6117_log("; r off\n");
			state |= MEM_READ_EXTANY;
		}

		mem_set_mem_state(base, size, state);
	}
    }

    flushmmucache_nopc();
}


static void
ali6117_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    ali6117_t *dev = (ali6117_t *) priv;

    ali6117_log("ALI6117: reg_write(%04X, %02X)\n", addr, val);

    if (addr == 0x22)
	dev->reg_offset = val;
    else if (dev->reg_offset == 0x13)
    	dev->unlocked = (val == 0xc5);
    else if (dev->unlocked) {
	ali6117_log("ALI6117: regs[%02X] = %02X\n", dev->reg_offset, val);

	if (!(dev->local & 0x08) || (dev->reg_offset < 0x30))  switch (dev->reg_offset) {
		case 0x30: case 0x34: case 0x35: case 0x3e:
		case 0x3f: case 0x46: case 0x4c: case 0x6a:
		case 0x73:
			return; /* read-only registers */

		case 0x10:
			refresh_at_enable = !(val & 0x02) || !!(dev->regs[0x20] & 0x80);
			break;

		case 0x12:
			val &= 0xf7;
			/* FALL-THROUGH */

		case 0x14: case 0x15:
			dev->regs[dev->reg_offset] = val;
			ali6117_recalcmapping(dev);
			break;

		case 0x1e:
			val &= 0x07;

			switch (val) {
				/* Half PIT clock. */
				case 0x0:
					cpu_set_isa_speed(7159091);
					break;

				/* Divisors on the input clock PCLK2, which is double the CPU clock. */
				case 0x1:
					cpu_set_isa_speed(cpu_busspeed / 1.5);
					break;

				case 0x2:
					cpu_set_isa_speed(cpu_busspeed / 2);
					break;

				case 0x3:
					cpu_set_isa_speed(cpu_busspeed / 2.5);
					break;

				case 0x4:
					cpu_set_isa_speed(cpu_busspeed / 3);
					break;

				case 0x5:
					cpu_set_isa_speed(cpu_busspeed / 4);
					break;

				case 0x6:
					cpu_set_isa_speed(cpu_busspeed / 5);
					break;

				case 0x7:
					cpu_set_isa_speed(cpu_busspeed / 6);
					break;
			}
			break;

		case 0x20:
			val &= 0xbf;
			refresh_at_enable = !(dev->regs[0x10] & 0x02) || !!(val & 0x80);
			break;

		case 0x31:
			/* TODO: fast gate A20 (bit 0) */
			val &= 0x21;
			break;

		case 0x32:
			val &= 0xc1;
			break;

		case 0x33:
			val &= 0xfd;
			break;

		case 0x36:
			val &= 0xf0;
			val |= dev->regs[dev->reg_offset];
			break;

		case 0x37:
			val &= 0xf5;
			break;

		case 0x3c:
			val &= 0x8f;
			ide_pri_disable();
			ide_set_base(1, (val & 0x01) ? 0x170 : 0x1f0);
			ide_set_side(1, (val & 0x01) ? 0x376 : 0x3f6);
			ide_pri_enable();
			break;

		case 0x44: case 0x45:
			val &= 0x3f;
			break;

		case 0x4a:
			val &= 0xfe;
			break;

		case 0x55:
			val &= 0x03;
			break;

		case 0x56:
			val &= 0xc7;
			break;

		case 0x58:
			val &= 0xc3;
			break;

		case 0x59:
			val &= 0x60;
			break;

		case 0x5b:
			val &= 0x1f;
			break;

		case 0x64:
			val &= 0xf7;
			break;

		case 0x66:
			val &= 0xe3;
			break;

		case 0x67:
			val &= 0xdf;
			break;

		case 0x69:
			val &= 0x50;
			break;

		case 0x6b:
			val &= 0x7f;
			break;

		case 0x6e: case 0x6f:
			val &= 0x03;
			break;

		case 0x71:
			val &= 0x1f;
			break;
	}

	dev->regs[dev->reg_offset] = val;
    }
}


static uint8_t
ali6117_reg_read(uint16_t addr, void *priv)
{
    ali6117_t *dev = (ali6117_t *) priv;
    uint8_t ret;

    if (addr == 0x22)
	ret = dev->reg_offset;
    else
	ret = dev->regs[dev->reg_offset];

    ali6117_log("ALI6117: reg_read(%04X) = %02X\n", dev->reg_offset, ret);
    return ret;
}


static void
ali6117_reset(void *priv)
{
    ali6117_t *dev = (ali6117_t *) priv;

    ali6117_log("ALI6117: reset()\n");

    memset(dev->regs, 0, sizeof(dev->regs));
    dev->regs[0x11] = 0xf8;
    dev->regs[0x12] = 0x20;
    dev->regs[0x17] = 0xff;
    dev->regs[0x18] = 0xf0;
    dev->regs[0x1a] = 0xff;
    dev->regs[0x1b] = 0xf0;
    dev->regs[0x1d] = 0xff;
    dev->regs[0x20] = 0x80;
    if (dev->local & 0x08) {
	dev->regs[0x30] = 0x08;
	dev->regs[0x31] = 0x01;
	dev->regs[0x34] = 0x04; /* enable internal RTC */
	dev->regs[0x35] = 0x20; /* enable internal KBC */
	dev->regs[0x36] = dev->local & 0x07; /* M6117D ID */
    }

    cpu_set_isa_speed(7159091);

    refresh_at_enable = 1;
}


static void
ali6117_setup(ali6117_t *dev)
{
    ali6117_log("ALI6117: setup()\n");

    /* Main register interface */
    io_sethandler(0x22, 2,
		  ali6117_reg_read, NULL, NULL, ali6117_reg_write, NULL, NULL, dev);
}


static void
ali6117_close(void *priv)
{
    ali6117_t *dev = (ali6117_t *) priv;

    ali6117_log("ALI6117: close()\n");

    io_removehandler(0x22, 2,
		     ali6117_reg_read, NULL, NULL, ali6117_reg_write, NULL, NULL, dev);

    free(dev);
}


static void *
ali6117_init(const device_t *info)
{
    ali6117_log("ALI6117: init()\n");

    ali6117_t *dev = (ali6117_t *) malloc(sizeof(ali6117_t));
    memset(dev, 0, sizeof(ali6117_t));

    dev->local = info->local;

    device_add(&ide_isa_device);

    ali6117_setup(dev);
    ali6117_reset(dev);

    if (!(dev->local & 0x08))
	pic_elcr_io_handler(0);

    return dev;
}

const device_t ali1217_device = {
    .name = "ALi M1217",
    .internal_name = "ali1217",
    .flags = DEVICE_AT,
    .local = 0x8,
    .init = ali6117_init,
    .close = ali6117_close,
    .reset = ali6117_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ali6117d_device = {
    .name = "ALi M6117D",
    .internal_name = "ali6117d",
    .flags = DEVICE_AT,
    .local = 0x2,
    .init = ali6117_init,
    .close = ali6117_close,
    .reset = ali6117_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
