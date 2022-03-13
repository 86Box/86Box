/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Winbond W83787F/IF Super I/O Chip.
 *
 *		Winbond W83787F Super I/O Chip
 *		Used by the Award 430HX
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/sio.h>

#ifdef ENABLE_W83787_LOG
int w83787_do_log = ENABLE_W83787_LOG;
static void
w83787_log(const char *fmt, ...)
{
    va_list ap;

    if (w83787_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define w83787_log(fmt, ...)
#endif

#define FDDA_TYPE	(dev->regs[7] & 3)
#define FDDB_TYPE	((dev->regs[7] >> 2) & 3)
#define FDDC_TYPE	((dev->regs[7] >> 4) & 3)
#define FDDD_TYPE	((dev->regs[7] >> 6) & 3)

#define FD_BOOT		(dev->regs[8] & 3)
#define SWWP		((dev->regs[8] >> 4) & 1)
#define DISFDDWR	((dev->regs[8] >> 5) & 1)

#define EN3MODE		((dev->regs[9] >> 5) & 1)

#define DRV2EN_NEG	(dev->regs[0xB] & 1)		/* 0 = drive 2 installed */
#define INVERTZ		((dev->regs[0xB] >> 1) & 1)	/* 0 = invert DENSEL polarity */
#define IDENT		((dev->regs[0xB] >> 3) & 1)

#define HEFERE		((dev->regs[0xC] >> 5) & 1)

#define HAS_IDE_FUNCTIONALITY	dev->ide_function

typedef struct {
    uint8_t tries, regs[42];
    uint16_t reg_init;
    int locked, rw_locked,
	cur_reg,
	key, ide_function,
	ide_start;
    fdc_t *fdc;
    serial_t *uart[2];
} w83787f_t;


static void	w83787f_write(uint16_t port, uint8_t val, void *priv);
static uint8_t	w83787f_read(uint16_t port, void *priv);


static void
w83787f_remap(w83787f_t *dev)
{
    io_removehandler(0x250, 0x0004,
		     w83787f_read, NULL, NULL, w83787f_write, NULL, NULL, dev);
    io_sethandler(0x250, 0x0004,
		  w83787f_read, NULL, NULL, w83787f_write, NULL, NULL, dev);
    dev->key = 0x88 | HEFERE;
}


#ifdef FIXME
/* FIXME: Implement EPP (and ECP) parallel port modes. */
static uint8_t
get_lpt_length(w83787f_t *dev)
{
    uint8_t length = 4;

    if (dev->regs[9] & 0x80) {
	if (dev->regs[0] & 0x04)
		length = 8;	/* EPP mode. */
	if (dev->regs[0] & 0x08)
		length |= 0x80;	/* ECP mode. */
    }

    return length;
}
#endif


static void
w83787f_serial_handler(w83787f_t *dev, int uart)
{
    int urs0 = !!(dev->regs[1] & (1 << uart));
    int urs1 = !!(dev->regs[1] & (4 << uart));
    int urs2 = !!(dev->regs[3] & (8 >> uart));
    int urs, irq = COM1_IRQ;
    uint16_t addr = COM1_ADDR, enable = 1;

    urs = (urs1 << 1) | urs0;

    if (urs2) {
	addr = uart ? COM1_ADDR : COM2_ADDR;
	irq = uart ? COM1_IRQ : COM2_IRQ;
    } else {
	switch (urs) {
		case 0:
			addr = uart ? COM3_ADDR : COM4_ADDR;
			irq = uart ? COM3_IRQ : COM4_IRQ;
			break;
		case 1:
			addr = uart ? COM4_ADDR : COM3_ADDR;
			irq = uart ? COM4_IRQ : COM3_IRQ;
			break;
		case 2:
			addr = uart ? COM2_ADDR : COM1_ADDR;
			irq = uart ? COM2_IRQ : COM1_IRQ;
			break;
		case 3:
		default:
			enable = 0;
			break;
	}
    }

    if (dev->regs[4] & (0x20 >> uart))
	enable = 0;

    serial_remove(dev->uart[uart]);
    if (enable)
	serial_setup(dev->uart[uart], addr, irq);
}


static void
w83787f_lpt_handler(w83787f_t *dev)
{
    int ptras = (dev->regs[1] >> 4) & 0x03;
    int irq = LPT1_IRQ;
    uint16_t addr = LPT1_ADDR, enable = 1;

    switch (ptras) {
	case 0x00:
		addr = LPT_MDA_ADDR;
		irq = LPT_MDA_IRQ;
		break;
	case 0x01:
		addr = LPT2_ADDR;
		irq = LPT2_IRQ;
		break;
	case 0x02:
		addr = LPT1_ADDR;
		irq = LPT1_IRQ;
		break;
	case 0x03:
	default:
		enable = 0;
		break;
    }

    if (dev->regs[4] & 0x80)
	enable = 0;

    lpt1_remove();
    if (enable) {
	lpt1_init(addr);
	lpt1_irq(irq);
    }
}


static void
w83787f_fdc_handler(w83787f_t *dev)
{
    fdc_remove(dev->fdc);
    if (!(dev->regs[0] & 0x20) && !(dev->regs[6] & 0x08))
	fdc_set_base(dev->fdc, (dev->regs[0] & 0x10) ? FDC_PRIMARY_ADDR : FDC_SECONDARY_ADDR);
}


static void
w83787f_ide_handler(w83787f_t *dev)
{
    if (dev->ide_function & 0x20) {
	ide_sec_disable();
	if (!(dev->regs[0] & 0x80)) {
		ide_set_base(1, (dev->regs[0] & 0x40) ? 0x1f0 : 0x170);
		ide_set_side(1, (dev->regs[0] & 0x40) ? 0x3f6 : 0x376);
		ide_sec_enable();
	}
    } else {
	ide_pri_disable();
	if (!(dev->regs[0] & 0x80)) {
		ide_set_base(0, (dev->regs[0] & 0x40) ? 0x1f0 : 0x170);
		ide_set_side(0, (dev->regs[0] & 0x40) ? 0x3f6 : 0x376);
		ide_pri_enable();
	}
    }
}


static void
w83787f_write(uint16_t port, uint8_t val, void *priv)
{
    w83787f_t *dev = (w83787f_t *) priv;
    uint8_t valxor = 0;
    uint8_t max = 0x15;

    if (port == 0x250) {
	if (val == dev->key)
		dev->locked = 1;
	else
		dev->locked = 0;
	return;
    } else if (port == 0x251) {
	if (val <= max)
		dev->cur_reg = val;
	return;
    } else {
	if (dev->locked) {
		if (dev->rw_locked)
			return;
		if (dev->cur_reg == 6)
			val &= 0xF3;
		valxor = val ^ dev->regs[dev->cur_reg];
		dev->regs[dev->cur_reg] = val;
	} else
		return;
    }

    switch (dev->cur_reg) {
	case 0:
		w83787_log("REG 00: %02X\n", val);
		if ((valxor & 0xc0) && (HAS_IDE_FUNCTIONALITY))
			w83787f_ide_handler(dev);
		if (valxor & 0x30)
			w83787f_fdc_handler(dev);
		if (valxor & 0x0c)
			w83787f_lpt_handler(dev);
		break;
	case 1:
		if (valxor & 0x80)
			fdc_set_swap(dev->fdc, (dev->regs[1] & 0x80) ? 1 : 0);
		if (valxor & 0x30)
			w83787f_lpt_handler(dev);
		if (valxor & 0x0a)
			w83787f_serial_handler(dev, 1);
		if (valxor & 0x05)
			w83787f_serial_handler(dev, 0);
		break;
	case 3:
		if (valxor & 0x80)
			w83787f_lpt_handler(dev);
		if (valxor & 0x08)
			w83787f_serial_handler(dev, 0);
		if (valxor & 0x04)
			w83787f_serial_handler(dev, 1);
		break;
	case 4:
		if (valxor & 0x10)
			w83787f_serial_handler(dev, 1);
		if (valxor & 0x20)
			w83787f_serial_handler(dev, 0);
		if (valxor & 0x80)
			w83787f_lpt_handler(dev);
		break;
	case 6:
		if (valxor & 0x08)
			w83787f_fdc_handler(dev);
		break;
	case 7:
		if (valxor & 0x03)
			fdc_update_rwc(dev->fdc, 0, FDDA_TYPE);
		if (valxor & 0x0c)
			fdc_update_rwc(dev->fdc, 1, FDDB_TYPE);
		if (valxor & 0x30)
			fdc_update_rwc(dev->fdc, 2, FDDC_TYPE);
		if (valxor & 0xc0)
			fdc_update_rwc(dev->fdc, 3, FDDD_TYPE);
		break;
	case 8:
		if (valxor & 0x03)
			fdc_update_boot_drive(dev->fdc, FD_BOOT);
		if (valxor & 0x10)
			fdc_set_swwp(dev->fdc, SWWP ? 1 : 0);
		if (valxor & 0x20)
			fdc_set_diswr(dev->fdc, DISFDDWR ? 1 : 0);
		break;
	case 9:
		if (valxor & 0x20)
			fdc_update_enh_mode(dev->fdc, EN3MODE ? 1 : 0);
		if (valxor & 0x40)
			dev->rw_locked = (val & 0x40) ? 1 : 0;
		if (valxor & 0x80)
			w83787f_lpt_handler(dev);
		break;
	case 0xB:
		w83787_log("Writing %02X to CRB\n", val);
		break;
	case 0xC:
		if (valxor & 0x20)
			w83787f_remap(dev);
		break;
    }
}


static uint8_t
w83787f_read(uint16_t port, void *priv)
{
    w83787f_t *dev = (w83787f_t *) priv;
    uint8_t ret = 0xff;

    if (dev->locked) {
	if (port == 0x251)
		ret = dev->cur_reg;
	else if (port == 0x252) {
		if (dev->cur_reg == 7)
			ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2));
		else if (!dev->rw_locked)
			ret = dev->regs[dev->cur_reg];
	}
    }

    return ret;
}


static void
w83787f_reset(w83787f_t *dev)
{
    lpt1_remove();
    lpt1_init(LPT1_ADDR);
    lpt1_irq(LPT1_IRQ);

    memset(dev->regs, 0, 0x2A);

    if (HAS_IDE_FUNCTIONALITY) {
	if (dev->ide_function & 0x20) {
		dev->regs[0x00] = 0x90;
		ide_sec_disable();
		ide_set_base(1, 0x170);
		ide_set_side(1, 0x376);
	} else {
		dev->regs[0x00] = 0xd0;
		ide_pri_disable();
		ide_set_base(0, 0x1f0);
		ide_set_side(0, 0x3f6);
	}

	if (dev->ide_start) {
		dev->regs[0x00] &= 0x7f;
		if (dev->ide_function & 0x20)
			ide_sec_enable();
		else
			ide_pri_enable();
	}
    } else
	dev->regs[0x00] = 0xd0;

    fdc_reset(dev->fdc);

    dev->regs[0x01] = 0x2C;
    dev->regs[0x03] = 0x30;
    dev->regs[0x07] = 0xF5;
    dev->regs[0x09] = dev->reg_init & 0xff;
    dev->regs[0x0a] = 0x1F;
    dev->regs[0x0c] = 0x2C;
    dev->regs[0x0d] = 0xA3;

    serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);
    serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

    dev->key = 0x89;

    w83787f_remap(dev);

    dev->locked = 0;
    dev->rw_locked = 0;
}


static void
w83787f_close(void *priv)
{
    w83787f_t *dev = (w83787f_t *) priv;

    free(dev);
}


static void *
w83787f_init(const device_t *info)
{
    w83787f_t *dev = (w83787f_t *) malloc(sizeof(w83787f_t));
    memset(dev, 0, sizeof(w83787f_t));

    HAS_IDE_FUNCTIONALITY = (info->local & 0x30);

    dev->fdc = device_add(&fdc_at_winbond_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    if ((dev->ide_function & 0x30) == 0x10)
	device_add(&ide_isa_device);

    dev->ide_start = !!(info->local & 0x40);

    dev->reg_init = info->local & 0x0f;
    w83787f_reset(dev);

    return dev;
}

const device_t w83787f_device = {
    .name = "Winbond W83787F/IF Super I/O",
    .internal_name = "w83787f",
    .flags = 0,
    .local = 0x09,
    .init = w83787f_init,
    .close = w83787f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t w83787f_ide_device = {
    .name = "Winbond W83787F/IF Super I/O (With IDE)",
    .internal_name = "w83787f_ide",
    .flags = 0,
    .local = 0x19,
    .init = w83787f_init,
    .close = w83787f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t w83787f_ide_en_device = {
    .name = "Winbond W83787F/IF Super I/O (With IDE Enabled)",
    .internal_name = "w83787f_ide_en",
    .flags = 0,
    .local = 0x59,
    .init = w83787f_init,
    .close = w83787f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t w83787f_ide_sec_device = {
    .name = "Winbond W83787F/IF Super I/O (With Secondary IDE)",
    .internal_name = "w83787f_ide_sec",
    .flags = 0,
    .local = 0x39,
    .init = w83787f_init,
    .close = w83787f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
