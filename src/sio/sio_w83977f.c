/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Winbond W83977F Super I/O Chip.
 *
 *		Winbond W83977F Super I/O Chip
 *		Used by the Award 430TX
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>


#define HEFRAS		(dev->regs[0x26] & 0x40)


typedef struct {
    uint8_t id, tries,
	    regs[48],
	    dev_regs[256][208];
    int locked, rw_locked,
	cur_reg, base_address,
	type, hefras;
    fdc_t *fdc;
    serial_t *uart[2];
} w83977f_t;


static int	next_id = 0;


static void	w83977f_write(uint16_t port, uint8_t val, void *priv);
static uint8_t	w83977f_read(uint16_t port, void *priv);


static void
w83977f_remap(w83977f_t *dev)
{
    io_removehandler(FDC_PRIMARY_ADDR, 0x0002,
		     w83977f_read, NULL, NULL, w83977f_write, NULL, NULL, dev);
    io_removehandler(FDC_SECONDARY_ADDR, 0x0002,
		     w83977f_read, NULL, NULL, w83977f_write, NULL, NULL, dev);

    dev->base_address = (HEFRAS ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);

    io_sethandler(dev->base_address, 0x0002,
		  w83977f_read, NULL, NULL, w83977f_write, NULL, NULL, dev);
}


static uint8_t
get_lpt_length(w83977f_t *dev)
{
    uint8_t length = 4;

    if (((dev->dev_regs[1][0xc0] & 0x07) != 0x00) && ((dev->dev_regs[1][0xc0] & 0x07) != 0x02) &&
	((dev->dev_regs[1][0xc0] & 0x07) != 0x04))
	length = 8;

    return length;
}


static void
w83977f_fdc_handler(w83977f_t *dev)
{
    uint16_t io_base = (dev->dev_regs[0][0x30] << 8) | dev->dev_regs[0][0x31];

    if (dev->id == 1)
	return;

    fdc_remove(dev->fdc);

    if ((dev->dev_regs[0][0x00] & 0x01) && (dev->regs[0x22] & 0x01) && (io_base >= 0x100) && (io_base <= 0xff8))
	fdc_set_base(dev->fdc, io_base);

    fdc_set_irq(dev->fdc, dev->dev_regs[0][0x40] & 0x0f);
}


static void
w83977f_lpt_handler(w83977f_t *dev)
{
    uint16_t io_mask, io_base = (dev->dev_regs[1][0x30] << 8) | dev->dev_regs[1][0x31];
    int io_len = get_lpt_length(dev);
    io_base &= (0xff8 | io_len);
    io_mask = 0xffc;
    if (io_len == 8)
	io_mask = 0xff8;

    if (dev->id == 1) {
	lpt2_remove();

	if ((dev->dev_regs[1][0x00] & 0x01) && (dev->regs[0x22] & 0x08) && (io_base >= 0x100) && (io_base <= io_mask))
		lpt2_init(io_base);

	lpt2_irq(dev->dev_regs[1][0x40] & 0x0f);
    } else {
	lpt1_remove();

	if ((dev->dev_regs[1][0x00] & 0x01) && (dev->regs[0x22] & 0x08) && (io_base >= 0x100) && (io_base <= io_mask))
		lpt1_init(io_base);

	lpt1_irq(dev->dev_regs[1][0x40] & 0x0f);
    }
}


static void
w83977f_serial_handler(w83977f_t *dev, int uart)
{
    uint16_t io_base = (dev->dev_regs[2 + uart][0x30] << 8) | dev->dev_regs[2 + uart][0x31];
    double clock_src = 24000000.0 / 13.0;

    serial_remove(dev->uart[uart]);

    if ((dev->dev_regs[2 + uart][0x00] & 0x01) && (dev->regs[0x22] & (0x10 << uart)) && (io_base >= 0x100) && (io_base <= 0xff8))
	serial_setup(dev->uart[uart], io_base, dev->dev_regs[2 + uart][0x40] & 0x0f);

    switch (dev->dev_regs[2 + uart][0xc0] & 0x03) {
	case 0x00:
		clock_src = 24000000.0 / 13.0;
		break;
	case 0x01:
		clock_src = 24000000.0 / 12.0;
		break;
	case 0x02:
		clock_src = 24000000.0 / 1.0;
		break;
	case 0x03:
		clock_src = 24000000.0 / 1.625;
		break;
    }

    serial_set_clock_src(dev->uart[uart], clock_src);
}


static void
w83977f_write(uint16_t port, uint8_t val, void *priv)
{
    w83977f_t *dev = (w83977f_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t valxor = 0;
    uint8_t ld = dev->regs[7];

    if (index) {
	if ((val == 0x87) && !dev->locked) {
		if (dev->tries) {
			dev->locked = 1;
			dev->tries = 0;
		} else
			dev->tries++;
	} else {
		if (dev->locked) {
			if (val == 0xaa)
				dev->locked = 0;
			else
				dev->cur_reg = val;
		} else {
			if (dev->tries)
				dev->tries = 0;
		}
	}
	return;
    } else {
	if (dev->locked) {
		if (dev->rw_locked)
			return;
		if (dev->cur_reg >= 0x30) {
			valxor = val ^ dev->dev_regs[ld][dev->cur_reg - 0x30];
			dev->dev_regs[ld][dev->cur_reg - 0x30] = val;
		} else {
			valxor = val ^ dev->regs[dev->cur_reg];
			dev->regs[dev->cur_reg] = val;
		}
	} else
		return;
    }

    switch (dev->cur_reg) {
	case 0x02:
		/* if (valxor & 0x02)
			softresetx86(); */
		break;
	case 0x22:
		if (valxor & 0x20)
			w83977f_serial_handler(dev, 1);
		if (valxor & 0x10)
			w83977f_serial_handler(dev, 0);
		if (valxor & 0x08)
			w83977f_lpt_handler(dev);
		if (valxor & 0x01)
			w83977f_fdc_handler(dev);
		break;
	case 0x26:
		if (valxor & 0x40)
			w83977f_remap(dev);
		if (valxor & 0x20)
			dev->rw_locked = (val & 0x20) ? 1 : 0;
		break;
	case 0x30:
		if (valxor & 0x01)  switch (ld) {
			case 0x00:
				w83977f_fdc_handler(dev);
				break;
			case 0x01:
				w83977f_lpt_handler(dev);
				break;
			case 0x02: case 0x03:
				w83977f_serial_handler(dev, ld - 2);
				break;
		}
		break;
	case 0x60: case 0x61:
		if (valxor & 0xff)  switch (ld) {
			case 0x00:
				w83977f_fdc_handler(dev);
				break;
			case 0x01:
				w83977f_lpt_handler(dev);
				break;
			case 0x02: case 0x03:
				w83977f_serial_handler(dev, ld - 2);
				break;
		}
		break;
	case 0x70:
		if (valxor & 0x0f)  switch (ld) {
			case 0x00:
				w83977f_fdc_handler(dev);
				break;
			case 0x01:
				w83977f_lpt_handler(dev);
				break;
			case 0x02: case 0x03:
				w83977f_serial_handler(dev, ld - 2);
				break;
		}
		break;
	case 0xf0:
		switch (ld) {
			case 0x00:
				if (dev->id == 1)
					break;

				if (!dev->id && (valxor & 0x20))
					fdc_update_drv2en(dev->fdc, (val & 0x20) ? 0 : 1);
				if (!dev->id && (valxor & 0x10))
					fdc_set_swap(dev->fdc, (val & 0x10) ? 1 : 0);
				if (!dev->id && (valxor & 0x01))
					fdc_update_enh_mode(dev->fdc, (val & 0x01) ? 1 : 0);
				break;
			case 0x01:
				if (valxor & 0x07)
					w83977f_lpt_handler(dev);
				break;
			case 0x02: case 0x03:
				if (valxor & 0x03)
					w83977f_serial_handler(dev, ld - 2);
				break;
		}
		break;
	case 0xf1:
		switch (ld) {
			case 0x00:
				if (dev->id == 1)
					break;

				if (!dev->id && (valxor & 0xc0))
					fdc_update_boot_drive(dev->fdc, (val & 0xc0) >> 6);
				if (!dev->id && (valxor & 0x0c))
					fdc_update_densel_force(dev->fdc, (val & 0x0c) >> 2);
				if (!dev->id && (valxor & 0x02))
					fdc_set_diswr(dev->fdc, (val & 0x02) ? 1 : 0);
				if (!dev->id && (valxor & 0x01))
					fdc_set_swwp(dev->fdc, (val & 0x01) ? 1 : 0);
				break;
		}
		break;
	case 0xf2:
		switch (ld) {
			case 0x00:
				if (dev->id == 1)
					break;

				if (!dev->id && (valxor & 0xc0))
					fdc_update_rwc(dev->fdc, 3, (val & 0xc0) >> 6);
				if (!dev->id && (valxor & 0x30))
					fdc_update_rwc(dev->fdc, 2, (val & 0x30) >> 4);
				if (!dev->id && (valxor & 0x0c))
					fdc_update_rwc(dev->fdc, 1, (val & 0x0c) >> 2);
				if (!dev->id && (valxor & 0x03))
					fdc_update_rwc(dev->fdc, 0, val & 0x03);
				break;
		}
		break;
	case 0xf4: case 0xf5: case 0xf6: case 0xf7:
		switch (ld) {
			case 0x00:
				if (dev->id == 1)
					break;

				if (!dev->id && (valxor & 0x18))
					fdc_update_drvrate(dev->fdc, dev->cur_reg & 0x03, (val & 0x18) >> 3);
				break;
		}
		break;
    }
}


static uint8_t
w83977f_read(uint16_t port, void *priv)
{
    w83977f_t *dev = (w83977f_t *) priv;
    uint8_t ret = 0xff;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t ld = dev->regs[7];

    if (dev->locked) {
	if (index)
		ret = dev->cur_reg;
	else {
		if (!dev->rw_locked) {
			if (!dev->id && ((dev->cur_reg == 0xf2) && (ld == 0x00)))
				ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2) | (fdc_get_rwc(dev->fdc, 2) << 4) | (fdc_get_rwc(dev->fdc, 3) << 6));
			else if (dev->cur_reg >= 0x30)
				ret = dev->dev_regs[ld][dev->cur_reg - 0x30];
			else
				ret = dev->regs[dev->cur_reg];
		}
	}
    }

    return ret;
}


static void
w83977f_reset(w83977f_t *dev)
{
    int i;

    memset(dev->regs, 0, 48);
    for (i = 0; i < 256; i++)
	memset(dev->dev_regs[i], 0, 208);

    if (dev->type < 2) {
	dev->regs[0x20] = 0x97;
	dev->regs[0x21] = dev->type ? 0x73 : 0x71;
    } else {
	dev->regs[0x20] = 0x52;
	dev->regs[0x21] = 0xf0;
    }
    dev->regs[0x22] = 0xff;
    dev->regs[0x24] = dev->type ? 0x84 : 0xa4;
    dev->regs[0x26] = dev->hefras;

    /* WARNING: Array elements are register - 0x30. */
    /* Logical Device 0 (FDC) */
    dev->dev_regs[0][0x00] = 0x01;
    if (!dev->type)
	dev->dev_regs[0][0x01] = 0x02;
    if (next_id == 1) {
	dev->dev_regs[0][0x30] = 0x03; dev->dev_regs[0][0x31] = 0x70;
    } else {
	dev->dev_regs[0][0x30] = 0x03; dev->dev_regs[0][0x31] = 0xf0;
    }
    dev->dev_regs[0][0x40] = 0x06;
    if (!dev->type)
	dev->dev_regs[0][0x41] = 0x02;	/* Read-only */
    dev->dev_regs[0][0x44] = 0x02;
    dev->dev_regs[0][0xc0] = 0x0e;

    /* Logical Device 1 (Parallel Port) */
    dev->dev_regs[1][0x00] = 0x01;
    if (!dev->type)
	dev->dev_regs[1][0x01] = 0x02;
    if (next_id == 1) {
	dev->dev_regs[1][0x30] = 0x02; dev->dev_regs[1][0x31] = 0x78;
	dev->dev_regs[1][0x40] = 0x05;
    } else {
	dev->dev_regs[1][0x30] = 0x03; dev->dev_regs[1][0x31] = 0x78;
	dev->dev_regs[1][0x40] = 0x07;
    }
    if (!dev->type)
	dev->dev_regs[1][0x41] = 0x01 /*0x02*/;	/* Read-only */
    dev->dev_regs[1][0x44] = 0x04;
    dev->dev_regs[1][0xc0] = 0x3c;	/* The datasheet says default is 3f, but also default is printer mode. */

    /* Logical Device 2 (UART A) */
    dev->dev_regs[2][0x00] = 0x01;
    if (!dev->type)
	dev->dev_regs[2][0x01] = 0x02;
    if (next_id == 1) {
	dev->dev_regs[2][0x30] = 0x03; dev->dev_regs[2][0x31] = 0xe8;
    } else {
	dev->dev_regs[2][0x30] = 0x03; dev->dev_regs[2][0x31] = 0xf8;
    }
    dev->dev_regs[2][0x40] = 0x04;
    if (!dev->type)
	dev->dev_regs[2][0x41] = 0x02;	/* Read-only */

    /* Logical Device 3 (UART B) */
    dev->dev_regs[3][0x00] = 0x01;
    if (!dev->type)
	dev->dev_regs[3][0x01] = 0x02;
    if (next_id == 1) {
	dev->dev_regs[3][0x30] = 0x02; dev->dev_regs[3][0x31] = 0xe8;
    } else {
	dev->dev_regs[3][0x30] = 0x02; dev->dev_regs[3][0x31] = 0xf8;
    }
    dev->dev_regs[3][0x40] = 0x03;
    if (!dev->type)
	dev->dev_regs[3][0x41] = 0x02;	/* Read-only */

    /* Logical Device 4 (RTC) */
    if (!dev->type) {
	dev->dev_regs[4][0x00] = 0x01;
	dev->dev_regs[4][0x01] = 0x02;
	dev->dev_regs[4][0x30] = 0x00; dev->dev_regs[4][0x31] = 0x70;
	dev->dev_regs[4][0x40] = 0x08;
	dev->dev_regs[4][0x41] = 0x02;	/* Read-only */
    }

    /* Logical Device 5 (KBC) */
    dev->dev_regs[5][0x00] = 0x01;
    if (!dev->type)
	dev->dev_regs[5][0x01] = 0x02;
    dev->dev_regs[5][0x30] = 0x00; dev->dev_regs[5][0x31] = 0x60;
    dev->dev_regs[5][0x32] = 0x00; dev->dev_regs[5][0x33] = 0x64;
    dev->dev_regs[5][0x40] = 0x01;
    if (!dev->type)
	dev->dev_regs[5][0x41] = 0x02;	/* Read-only */
    dev->dev_regs[5][0x42] = 0x0c;
    if (!dev->type)
	dev->dev_regs[5][0x43] = 0x02;	/* Read-only? */
    dev->dev_regs[5][0xc0] = dev->type ? 0x83 : 0x40;

    /* Logical Device 6 (IR) = UART C */
    if (!dev->type) {
	dev->dev_regs[6][0x01] = 0x02;
	dev->dev_regs[6][0x41] = 0x02;	/* Read-only */
	dev->dev_regs[6][0x44] = 0x04;
	dev->dev_regs[6][0x45] = 0x04;
    }

    /* Logical Device 7 (Auxiliary I/O Part I) */
    if (!dev->type)
	dev->dev_regs[7][0x01] = 0x02;
    if (!dev->type)
	dev->dev_regs[7][0x41] = 0x02;	/* Read-only */
    if (!dev->type)
	dev->dev_regs[7][0x43] = 0x02;	/* Read-only? */
    dev->dev_regs[7][0xb0] = 0x01; dev->dev_regs[7][0xb1] = 0x01;
    dev->dev_regs[7][0xb2] = 0x01; dev->dev_regs[7][0xb3] = 0x01;
    dev->dev_regs[7][0xb4] = 0x01; dev->dev_regs[7][0xb5] = 0x01;
    dev->dev_regs[7][0xb6] = 0x01;
    if (dev->type)
	dev->dev_regs[7][0xb7] = 0x01;

    /* Logical Device 8 (Auxiliary I/O Part II) */
    if (!dev->type)
	dev->dev_regs[8][0x01] = 0x02;
    if (!dev->type)
	dev->dev_regs[8][0x41] = 0x02;	/* Read-only */
    if (!dev->type)
	dev->dev_regs[8][0x43] = 0x02;	/* Read-only? */
    dev->dev_regs[8][0xb8] = 0x01; dev->dev_regs[8][0xb9] = 0x01;
    dev->dev_regs[8][0xba] = 0x01; dev->dev_regs[8][0xbb] = 0x01;
    dev->dev_regs[8][0xbc] = 0x01; dev->dev_regs[8][0xbd] = 0x01;
    dev->dev_regs[8][0xbe] = 0x01; dev->dev_regs[8][0xbf] = 0x01;

    /* Logical Device 9 (Auxiliary I/O Part III) */
    if (dev->type) {
	dev->dev_regs[9][0xb0] = 0x01; dev->dev_regs[9][0xb1] = 0x01;
	dev->dev_regs[9][0xb2] = 0x01; dev->dev_regs[9][0xb3] = 0x01;
	dev->dev_regs[9][0xb4] = 0x01; dev->dev_regs[9][0xb5] = 0x01;
	dev->dev_regs[9][0xb6] = 0x01; dev->dev_regs[9][0xb7] = 0x01;

	dev->dev_regs[10][0xc0] = 0x8f;
    }

    if (dev->id == 1) {
	serial_setup(dev->uart[0], COM3_ADDR, COM3_IRQ);
	serial_setup(dev->uart[1], COM4_ADDR, COM4_IRQ);
    } else {
	fdc_reset(dev->fdc);

	serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);
	serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

	w83977f_fdc_handler(dev);
    }

    w83977f_lpt_handler(dev);
    w83977f_serial_handler(dev, 0);
    w83977f_serial_handler(dev, 1);

    w83977f_remap(dev);

    dev->locked = 0;
    dev->rw_locked = 0;
}


static void
w83977f_close(void *priv)
{
    w83977f_t *dev = (w83977f_t *) priv;

    next_id = 0;

    free(dev);
}


static void *
w83977f_init(const device_t *info)
{
    w83977f_t *dev = (w83977f_t *) malloc(sizeof(w83977f_t));
    memset(dev, 0, sizeof(w83977f_t));

    dev->type = info->local & 0x0f;
    dev->hefras = info->local & 0x40;

    dev->id = next_id;

    if (next_id == 1)
	dev->hefras ^= 0x40;
    else
	dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, (next_id << 1) + 1);
    dev->uart[1] = device_add_inst(&ns16550_device, (next_id << 1) + 2);

    w83977f_reset(dev);

    next_id++;

    return dev;
}

const device_t w83977f_device = {
    .name = "Winbond W83977F Super I/O",
    .internal_name = "w83977f",
    .flags = 0,
    .local = 0,
    .init = w83977f_init,
    .close = w83977f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t w83977f_370_device = {
    .name = "Winbond W83977F Super I/O (Port 370h)",
    .internal_name = "w83977f_370",
    .flags = 0,
    .local = 0x40,
    .init = w83977f_init,
    .close = w83977f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t w83977tf_device = {
    .name = "Winbond W83977TF Super I/O",
    .internal_name = "w83977tf",
    .flags = 0,
    .local = 1,
    .init = w83977f_init,
    .close = w83977f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t w83977ef_device = {
    .name = "Winbond W83977TF Super I/O",
    .internal_name = "w83977ef",
    .flags = 0,
    .local = 2,
    .init = w83977f_init,
    .close = w83977f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t w83977ef_370_device = {
    .name = "Winbond W83977TF Super I/O (Port 370h)",
    .internal_name = "w83977ef_370",
    .flags = 0,
    .local = 0x42,
    .init = w83977f_init,
    .close = w83977f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
