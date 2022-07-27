/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SMC FDC37C669 Super I/O Chip.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>


typedef struct {
    uint8_t id, tries,
	    regs[42];
    int locked, rw_locked,
	cur_reg;
    fdc_t *fdc;
    serial_t *uart[2];
} fdc37c669_t;


static int	next_id = 0;


static uint16_t
make_port(fdc37c669_t *dev, uint8_t reg)
{
    uint16_t p = 0;
    uint16_t mask = 0;

    switch(reg) {
	case 0x20:
	case 0x21:
	case 0x22:
		mask = 0xfc;
		break;
	case 0x23:
		mask = 0xff;
		break;
	case 0x24:
	case 0x25:
		mask = 0xfe;
		break;
    }

    p = ((uint16_t) (dev->regs[reg] & mask)) << 2;
    if (reg == 0x22)
	p |= 6;

    return p;
}


static void
fdc37c669_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c669_t *dev = (fdc37c669_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t valxor = 0;
    uint8_t max = 42;

    if (index) {
	if ((val == 0x55) && !dev->locked) {
		if (dev->tries) {
			dev->locked = 1;
			dev->tries = 0;
		} else
			dev->tries++;
	} else {
		if (dev->locked) {
			if (val < max)
				dev->cur_reg = val;
			if (val == 0xaa)
				dev->locked = 0;
		} else {
			if (dev->tries)
				dev->tries = 0;
		}
	}
	return;
    } else {
	if (dev->locked) {
		if ((dev->cur_reg < 0x18) && (dev->rw_locked))
			return;
		if ((dev->cur_reg >= 0x26) && (dev->cur_reg <= 0x27))
			return;
		if (dev->cur_reg == 0x29)
			return;
		valxor = val ^ dev->regs[dev->cur_reg];
		dev->regs[dev->cur_reg] = val;
	} else
		return;
    }

    switch(dev->cur_reg) {
	case 0:
		if (!dev->id && (valxor & 8)) {
			fdc_remove(dev->fdc);
			if ((dev->regs[0] & 8) && (dev->regs[0x20] & 0xc0))
				fdc_set_base(dev->fdc, make_port(dev, 0x20));
		}
		break;
	case 1:
		if (valxor & 4) {
			if (dev->id) {
				lpt2_remove();
				if ((dev->regs[1] & 4) && (dev->regs[0x23] >= 0x40))
					lpt2_init(make_port(dev, 0x23));
			} else {
				lpt1_remove();
				if ((dev->regs[1] & 4) && (dev->regs[0x23] >= 0x40))
					lpt1_init(make_port(dev, 0x23));
			}
		}
		if (valxor & 7)
			dev->rw_locked = (val & 8) ? 0 : 1;
		break;
	case 2:
		if (valxor & 8) {
			serial_remove(dev->uart[0]);
			if ((dev->regs[2] & 8) && (dev->regs[0x24] >= 0x40))
				serial_setup(dev->uart[0], make_port(dev, 0x24), (dev->regs[0x28] & 0xf0) >> 4);
		}
		if (valxor & 0x80) {
			serial_remove(dev->uart[1]);
			if ((dev->regs[2] & 0x80) && (dev->regs[0x25] >= 0x40))
				serial_setup(dev->uart[1], make_port(dev, 0x25), dev->regs[0x28] & 0x0f);
		}
		break;
	case 3:
		if (!dev->id && (valxor & 2))
			fdc_update_enh_mode(dev->fdc, (val & 2) ? 1 : 0);
		break;
	case 5:
		if (!dev->id && (valxor & 0x18))
			fdc_update_densel_force(dev->fdc, (val & 0x18) >> 3);
		if (!dev->id && (valxor & 0x20))
			fdc_set_swap(dev->fdc, (val & 0x20) >> 5);
		break;
	case 0xB:
		if (!dev->id && (valxor & 3))
			fdc_update_rwc(dev->fdc, 0, val & 3);
		if (!dev->id && (valxor & 0xC))
			fdc_update_rwc(dev->fdc, 1, (val & 0xC) >> 2);
		break;
	case 0x20:
		if (!dev->id && (valxor & 0xfc)) {
			fdc_remove(dev->fdc);
			if ((dev->regs[0] & 8) && (dev->regs[0x20] & 0xc0))
				fdc_set_base(dev->fdc, make_port(dev, 0x20));
		}
		break;
	case 0x23:
		if (valxor) {
			if (dev->id) {
				lpt2_remove();
				if ((dev->regs[1] & 4) && (dev->regs[0x23] >= 0x40))
					lpt2_init(make_port(dev, 0x23));
			} else {
				lpt1_remove();
				if ((dev->regs[1] & 4) && (dev->regs[0x23] >= 0x40))
					lpt1_init(make_port(dev, 0x23));
			}
		}
		break;
	case 0x24:
		if (valxor & 0xfe) {
			serial_remove(dev->uart[0]);
			if ((dev->regs[2] & 8) && (dev->regs[0x24] >= 0x40))
				serial_setup(dev->uart[0], make_port(dev, 0x24), (dev->regs[0x28] & 0xf0) >> 4);
		}
		break;
	case 0x25:
		if (valxor & 0xfe) {
			serial_remove(dev->uart[1]);
			if ((dev->regs[2] & 0x80) && (dev->regs[0x25] >= 0x40))
				serial_setup(dev->uart[1], make_port(dev, 0x25), dev->regs[0x28] & 0x0f);
		}
		break;
	case 0x27:
		if (valxor & 0xf) {
			if (dev->id)
				lpt2_irq(val & 0xf);
			else
				lpt1_irq(val & 0xf);
		}
		break;
	case 0x28:
		if (valxor & 0xf) {
			serial_remove(dev->uart[1]);
			if ((dev->regs[2] & 0x80) && (dev->regs[0x25] >= 0x40))
				serial_setup(dev->uart[1], make_port(dev, 0x25), dev->regs[0x28] & 0x0f);
		}
		if (valxor & 0xf0) {
			serial_remove(dev->uart[0]);
			if ((dev->regs[2] & 8) && (dev->regs[0x24] >= 0x40))
				serial_setup(dev->uart[0], make_port(dev, 0x24), (dev->regs[0x28] & 0xf0) >> 4);
		}
		break;
    }
}


static uint8_t
fdc37c669_read(uint16_t port, void *priv)
{
    fdc37c669_t *dev = (fdc37c669_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t ret = 0xff;

    if (dev->locked) {
	    if (index)
		ret = dev->cur_reg;
	    else if ((dev->cur_reg >= 0x18) || !dev->rw_locked)
		ret = dev->regs[dev->cur_reg];
    }

    return ret;
}


static void
fdc37c669_reset(fdc37c669_t *dev)
{
    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);

    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

    memset(dev->regs, 0, 42);
    dev->regs[0x00] = 0x28;
    dev->regs[0x01] = 0x9c;
    dev->regs[0x02] = 0x88;
    dev->regs[0x03] = 0x78;
    dev->regs[0x06] = 0xff;
    dev->regs[0x0d] = 0x03;
    dev->regs[0x0e] = 0x02;
    dev->regs[0x1e] = 0x80;	/* Gameport controller. */
    dev->regs[0x20] = (FDC_PRIMARY_ADDR >> 2) & 0xfc;
    dev->regs[0x21] = (0x1f0 >> 2) & 0xfc;
    dev->regs[0x22] = ((0x3f6 >> 2) & 0xfc) | 1;
    if (dev->id == 1) {
	dev->regs[0x23] = (LPT2_ADDR >> 2);

	lpt2_remove();
	lpt2_init(LPT2_ADDR);

	dev->regs[0x24] = (COM3_ADDR >> 2) & 0xfe;
	dev->regs[0x25] = (COM4_ADDR >> 2) & 0xfe;
    } else {
	fdc_reset(dev->fdc);

	lpt1_remove();
	lpt1_init(LPT1_ADDR);

	dev->regs[0x23] = (LPT1_ADDR >> 2);

	dev->regs[0x24] = (COM1_ADDR >> 2) & 0xfe;
	dev->regs[0x25] = (COM2_ADDR >> 2) & 0xfe;
    }
    dev->regs[0x26] = (2 << 4) | 3;
    dev->regs[0x27] = (6 << 4) | (dev->id ? 5 : 7);
    dev->regs[0x28] = (4 << 4) | 3;

    dev->locked = 0;
    dev->rw_locked = 0;
}


static void
fdc37c669_close(void *priv)
{
    fdc37c669_t *dev = (fdc37c669_t *) priv;

    next_id = 0;

    free(dev);
}


static void *
fdc37c669_init(const device_t *info)
{
    fdc37c669_t *dev = (fdc37c669_t *) malloc(sizeof(fdc37c669_t));
    memset(dev, 0, sizeof(fdc37c669_t));

    dev->id = next_id;

    if (next_id != 1)
	dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, (next_id << 1) + 1);
    dev->uart[1] = device_add_inst(&ns16550_device, (next_id << 1) + 2);

    io_sethandler(info->local ? FDC_SECONDARY_ADDR : (next_id ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR), 0x0002,
		  fdc37c669_read, NULL, NULL, fdc37c669_write, NULL, NULL, dev);

    fdc37c669_reset(dev);

    next_id++;

    return dev;
}

const device_t fdc37c669_device = {
    .name = "SMC FDC37C669 Super I/O",
    .internal_name = "fdc37c669",
    .flags = 0,
    .local = 0,
    .init = fdc37c669_init,
    .close = fdc37c669_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};


const device_t fdc37c669_370_device = {
    .name = "SMC FDC37C669 Super I/O (Port 370h)",
    .internal_name = "fdc37c669_370",
    .flags = 0,
    .local = 1,
    fdc37c669_init,
    fdc37c669_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
