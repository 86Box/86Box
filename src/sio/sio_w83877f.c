/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Winbond W83877F Super I/O Chip.
 *
 *		Winbond W83877F Super I/O Chip
 *		Used by the Award 430HX
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

#define FDDA_TYPE  (dev->regs[7] & 3)
#define FDDB_TYPE  ((dev->regs[7] >> 2) & 3)
#define FDDC_TYPE  ((dev->regs[7] >> 4) & 3)
#define FDDD_TYPE  ((dev->regs[7] >> 6) & 3)

#define FD_BOOT    (dev->regs[8] & 3)
#define SWWP       ((dev->regs[8] >> 4) & 1)
#define DISFDDWR   ((dev->regs[8] >> 5) & 1)

#define EN3MODE    ((dev->regs[9] >> 5) & 1)

#define DRV2EN_NEG (dev->regs[0xB] & 1)        /* 0 = drive 2 installed */
#define INVERTZ    ((dev->regs[0xB] >> 1) & 1) /* 0 = invert DENSEL polarity */
#define IDENT      ((dev->regs[0xB] >> 3) & 1)

#define HEFERE     ((dev->regs[0xC] >> 5) & 1)

#define HEFRAS     (dev->regs[0x16] & 1)

#define PRTIQS     (dev->regs[0x27] & 0x0f)
#define ECPIRQ     ((dev->regs[0x27] >> 5) & 0x07)

typedef struct {
    uint8_t  tries, regs[42];
    uint16_t reg_init;
    int      locked, rw_locked,
        cur_reg,
        base_address, key,
        key_times;
    fdc_t    *fdc;
    serial_t *uart[2];
} w83877f_t;

static void    w83877f_write(uint16_t port, uint8_t val, void *priv);
static uint8_t w83877f_read(uint16_t port, void *priv);

static void
w83877f_remap(w83877f_t *dev)
{
    uint8_t hefras = HEFRAS;

    io_removehandler(0x250, 0x0002,
                     w83877f_read, NULL, NULL, w83877f_write, NULL, NULL, dev);
    io_removehandler(FDC_PRIMARY_ADDR, 0x0002,
                     w83877f_read, NULL, NULL, w83877f_write, NULL, NULL, dev);
    dev->base_address = (hefras ? FDC_PRIMARY_ADDR : 0x250);
    io_sethandler(dev->base_address, 0x0002,
                  w83877f_read, NULL, NULL, w83877f_write, NULL, NULL, dev);
    dev->key_times = hefras + 1;
    dev->key       = (hefras ? 0x86 : 0x88) | HEFERE;
}

static uint8_t
get_lpt_length(w83877f_t *dev)
{
    uint8_t length = 4;

    if (dev->regs[9] & 0x80) {
        if (dev->regs[0] & 0x04)
            length = 8; /* EPP mode. */
        if (dev->regs[0] & 0x08)
            length |= 0x80; /* ECP mode. */
    }

    return length;
}

static uint16_t
make_port(w83877f_t *dev, uint8_t reg)
{
    uint16_t p = 0;
    uint8_t  l;

    switch (reg) {
        case 0x20:
            p = ((uint16_t) (dev->regs[reg] & 0xfc)) << 2;
            p &= 0xFF0;
            if ((p < 0x100) || (p > 0x3F0))
                p = 0x3F0;
            break;
        case 0x23:
            l = get_lpt_length(dev);
            p = ((uint16_t) (dev->regs[reg] & 0xff)) << 2;
            /* 8 ports in EPP mode, 4 in non-EPP mode. */
            if ((l & 0x0f) == 8)
                p &= 0x3F8;
            else
                p &= 0x3FC;
            if ((p < 0x100) || (p > 0x3FF))
                p = LPT1_ADDR;
            /* In ECP mode, A10 is active. */
            if (l & 0x80)
                p |= 0x400;
            break;
        case 0x24:
            p = ((uint16_t) (dev->regs[reg] & 0xfe)) << 2;
            p &= 0xFF8;
            if ((p < 0x100) || (p > 0x3F8))
                p = COM1_ADDR;
            break;
        case 0x25:
            p = ((uint16_t) (dev->regs[reg] & 0xfe)) << 2;
            p &= 0xFF8;
            if ((p < 0x100) || (p > 0x3F8))
                p = COM2_ADDR;
            break;
    }

    return p;
}

static void
w83877f_fdc_handler(w83877f_t *dev)
{
    fdc_remove(dev->fdc);
    if (!(dev->regs[6] & 0x08) && (dev->regs[0x20] & 0xc0))
        fdc_set_base(dev->fdc, FDC_PRIMARY_ADDR);
}

static void
w83877f_lpt_handler(w83877f_t *dev)
{
    uint8_t lpt_irq;
    uint8_t lpt_irqs[8] = { 0, 7, 9, 10, 11, 14, 15, 5 };

    lpt1_remove();
    if (!(dev->regs[4] & 0x80) && (dev->regs[0x23] & 0xc0))
        lpt1_init(make_port(dev, 0x23));

    lpt_irq = 0xff;

    lpt_irq = lpt_irqs[ECPIRQ];
    if (lpt_irq == 0)
        lpt_irq = PRTIQS;

    lpt1_irq(lpt_irq);
}

static void
w83877f_serial_handler(w83877f_t *dev, int uart)
{
    int    reg_mask  = uart ? 0x10 : 0x20;
    int    reg_id    = uart ? 0x25 : 0x24;
    int    irq_mask  = uart ? 0x0f : 0xf0;
    int    irq_shift = uart ? 0 : 4;
    double clock_src = 24000000.0 / 13.0;

    serial_remove(dev->uart[uart]);
    if (!(dev->regs[4] & reg_mask) && (dev->regs[reg_id] & 0xc0))
        serial_setup(dev->uart[uart], make_port(dev, reg_id), (dev->regs[0x28] & irq_mask) >> irq_shift);

    if (dev->regs[0x19] & (0x02 >> uart)) {
        clock_src = 14769000.0;
    } else if (dev->regs[0x03] & (0x02 >> uart)) {
        clock_src = 24000000.0 / 12.0;
    } else {
        clock_src = 24000000.0 / 13.0;
    }

    serial_set_clock_src(dev->uart[uart], clock_src);
}

static void
w83877f_write(uint16_t port, uint8_t val, void *priv)
{
    w83877f_t *dev    = (w83877f_t *) priv;
    uint8_t    valxor = 0;
    uint8_t    max    = 0x2A;

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
    } else if (port == FDC_PRIMARY_ADDR) {
        if ((val == dev->key) && !dev->locked) {
            if (dev->key_times == 2) {
                if (dev->tries) {
                    dev->locked = 1;
                    dev->tries  = 0;
                } else
                    dev->tries++;
            } else {
                dev->locked = 1;
                dev->tries  = 0;
            }
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
    } else if ((port == 0x252) || (port == 0x3f1)) {
        if (dev->locked) {
            if (dev->rw_locked)
                return;
            if ((dev->cur_reg >= 0x26) && (dev->cur_reg <= 0x27))
                return;
            if (dev->cur_reg == 0x29)
                return;
            if (dev->cur_reg == 6)
                val &= 0xF3;
            valxor                  = val ^ dev->regs[dev->cur_reg];
            dev->regs[dev->cur_reg] = val;
        } else
            return;
    }

    switch (dev->cur_reg) {
        case 0:
            if (valxor & 0x0c)
                w83877f_lpt_handler(dev);
            break;
        case 1:
            if (valxor & 0x80)
                fdc_set_swap(dev->fdc, (dev->regs[1] & 0x80) ? 1 : 0);
            break;
        case 3:
            if (valxor & 0x02)
                w83877f_serial_handler(dev, 0);
            if (valxor & 0x01)
                w83877f_serial_handler(dev, 1);
            break;
        case 4:
            if (valxor & 0x10)
                w83877f_serial_handler(dev, 1);
            if (valxor & 0x20)
                w83877f_serial_handler(dev, 0);
            if (valxor & 0x80)
                w83877f_lpt_handler(dev);
            break;
        case 6:
            if (valxor & 0x08)
                w83877f_fdc_handler(dev);
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
                w83877f_lpt_handler(dev);
            break;
        case 0xB:
            if (valxor & 1)
                fdc_update_drv2en(dev->fdc, DRV2EN_NEG ? 0 : 1);
            if (valxor & 2)
                fdc_update_densel_polarity(dev->fdc, INVERTZ ? 1 : 0);
            break;
        case 0xC:
            if (valxor & 0x20)
                w83877f_remap(dev);
            break;
        case 0x16:
            if (valxor & 1)
                w83877f_remap(dev);
            break;
        case 0x19:
            if (valxor & 0x02)
                w83877f_serial_handler(dev, 0);
            if (valxor & 0x01)
                w83877f_serial_handler(dev, 1);
            break;
        case 0x20:
            if (valxor)
                w83877f_fdc_handler(dev);
            break;
        case 0x23:
            if (valxor)
                w83877f_lpt_handler(dev);
            break;
        case 0x24:
            if (valxor & 0xfe)
                w83877f_serial_handler(dev, 0);
            break;
        case 0x25:
            if (valxor & 0xfe)
                w83877f_serial_handler(dev, 1);
            break;
        case 0x27:
            if (valxor & 0xef)
                w83877f_lpt_handler(dev);
            break;
        case 0x28:
            if (valxor & 0xf) {
                if ((dev->regs[0x28] & 0x0f) == 0)
                    dev->regs[0x28] |= 0x03;
                w83877f_serial_handler(dev, 1);
            }
            if (valxor & 0xf0) {
                if ((dev->regs[0x28] & 0xf0) == 0)
                    dev->regs[0x28] |= 0x40;
                w83877f_serial_handler(dev, 0);
            }
            break;
    }
}

static uint8_t
w83877f_read(uint16_t port, void *priv)
{
    w83877f_t *dev = (w83877f_t *) priv;
    uint8_t    ret = 0xff;

    if (dev->locked) {
        if ((port == FDC_PRIMARY_ADDR) || (port == 0x251))
            ret = dev->cur_reg;
        else if ((port == 0x3f1) || (port == 0x252)) {
            if (dev->cur_reg == 7)
                ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2) | (fdc_get_rwc(dev->fdc, 2) << 4) | (fdc_get_rwc(dev->fdc, 3) << 6));
            else if ((dev->cur_reg >= 0x18) || !dev->rw_locked)
                ret = dev->regs[dev->cur_reg];
        }
    }

    return ret;
}

static void
w83877f_reset(w83877f_t *dev)
{
    fdc_reset(dev->fdc);

    memset(dev->regs, 0, 0x2A);
    dev->regs[0x03] = 0x30;
    dev->regs[0x07] = 0xF5;
    dev->regs[0x09] = (dev->reg_init >> 8) & 0xff;
    dev->regs[0x0a] = 0x1F;
    dev->regs[0x0c] = 0x28;
    dev->regs[0x0d] = 0xA3;
    dev->regs[0x16] = dev->reg_init & 0xff;
    dev->regs[0x1e] = 0x81;
    dev->regs[0x20] = (FDC_PRIMARY_ADDR >> 2) & 0xfc;
    dev->regs[0x21] = (0x1f0 >> 2) & 0xfc;
    dev->regs[0x22] = ((0x3f6 >> 2) & 0xfc) | 1;
    dev->regs[0x23] = (LPT1_ADDR >> 2);
    dev->regs[0x24] = (COM1_ADDR >> 2) & 0xfe;
    dev->regs[0x25] = (COM2_ADDR >> 2) & 0xfe;
    dev->regs[0x26] = (2 << 4) | 4;
    dev->regs[0x27] = (2 << 4) | 5;
    dev->regs[0x28] = (4 << 4) | 3;
    dev->regs[0x29] = 0x62;

    w83877f_fdc_handler(dev);

    w83877f_lpt_handler(dev);

    w83877f_serial_handler(dev, 0);
    w83877f_serial_handler(dev, 1);

    dev->base_address = FDC_PRIMARY_ADDR;
    dev->key          = 0x89;
    dev->key_times    = 1;

    w83877f_remap(dev);

    dev->locked    = 0;
    dev->rw_locked = 0;
}

static void
w83877f_close(void *priv)
{
    w83877f_t *dev = (w83877f_t *) priv;

    free(dev);
}

static void *
w83877f_init(const device_t *info)
{
    w83877f_t *dev = (w83877f_t *) malloc(sizeof(w83877f_t));
    memset(dev, 0, sizeof(w83877f_t));

    dev->fdc = device_add(&fdc_at_winbond_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->reg_init = info->local;

    w83877f_reset(dev);

    return dev;
}

const device_t w83877f_device = {
    .name          = "Winbond W83877F Super I/O",
    .internal_name = "w83877f",
    .flags         = 0,
    .local         = 0x0a05,
    .init          = w83877f_init,
    .close         = w83877f_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t w83877f_president_device = {
    .name          = "Winbond W83877F Super I/O (President)",
    .internal_name = "w83877f_president",
    .flags         = 0,
    .local         = 0x0a04,
    .init          = w83877f_init,
    .close         = w83877f_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t w83877tf_device = {
    .name          = "Winbond W83877TF Super I/O",
    .internal_name = "w83877tf",
    .flags         = 0,
    .local         = 0x0c04,
    .init          = w83877f_init,
    .close         = w83877f_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t w83877tf_acorp_device = {
    .name          = "Winbond W83877TF Super I/O",
    .internal_name = "w83877tf_acorp",
    .flags         = 0,
    .local         = 0x0c05,
    .init          = w83877f_init,
    .close         = w83877f_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
