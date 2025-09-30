/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Winbond W83877 family of Super I/O Chips.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
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
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/machine.h>
#include <86box/sio.h>

#define FDDA_TYPE  (dev->regs[0x07] & 3)
#define FDDB_TYPE  ((dev->regs[0x07] >> 2) & 3)
#define FDDC_TYPE  ((dev->regs[0x07] >> 4) & 3)
#define FDDD_TYPE  ((dev->regs[0x07] >> 6) & 3)

#define FD_BOOT    (dev->regs[0x08] & 3)
#define SWWP       ((dev->regs[0x08] >> 4) & 1)
#define DISFDDWR   ((dev->regs[0x08] >> 5) & 1)

#define EN3MODE    ((dev->regs[0x09] >> 5) & 1)

#define DRV2EN_NEG (dev->regs[0x0b] & 1)        /* 0 = drive 2 installed */
#define INVERTZ    ((dev->regs[0x0b] >> 1) & 1) /* 0 = invert DENSEL polarity */
#define IDENT      ((dev->regs[0x0b] >> 3) & 1)

#define HEFERE     ((dev->regs[0x0c] >> 5) & 1)

#define HEFRAS     (dev->regs[0x16] & 1)

#define PRTIQS     (dev->regs[0x27] & 0x0f)
#define ECPIRQ     ((dev->regs[0x27] >> 5) & 0x07)

typedef struct w83877_t {
    uint8_t   tries;
    uint8_t   has_ide;
    uint8_t   dma_map[4];
    uint8_t   irq_map[10];
    uint8_t   regs[256];
    uint16_t  reg_init;
    int       locked;
    int       rw_locked;
    int       cur_reg;
    int       base_address;
    int       key;
    int       key_times;
    fdc_t    *fdc;
    serial_t *uart[2];
    lpt_t    *lpt;
} w83877_t;

static void    w83877_write(uint16_t port, uint8_t val, void *priv);
static uint8_t w83877_read(uint16_t port, void *priv);

static void
w83877_remap(w83877_t *dev)
{
    uint8_t hefras = HEFRAS;

    io_removehandler(0x250, 0x0003,
                     w83877_read, NULL, NULL, w83877_write, NULL, NULL, dev);
    io_removehandler(FDC_PRIMARY_ADDR, 0x0002,
                     w83877_read, NULL, NULL, w83877_write, NULL, NULL, dev);
    dev->base_address = (hefras ? FDC_PRIMARY_ADDR : 0x250);
    io_sethandler(dev->base_address, hefras ? 0x0002 : 0x0003,
                  w83877_read, NULL, NULL, w83877_write, NULL, NULL, dev);
    dev->key_times = hefras + 1;
    dev->key       = (hefras ? 0x86 : 0x88) | HEFERE;
}

static uint8_t
get_lpt_length(w83877_t *dev)
{
    uint8_t length = 4;

    if (dev->regs[0x09] & 0x80) {
        if (dev->regs[0x00] & 0x04)
            length = 8; /* EPP mode. */
        if (dev->regs[0x00] & 0x08)
            length |= 0x80; /* ECP mode. */
    }

    return length;
}

static uint16_t
make_port(w83877_t *dev, uint8_t reg)
{
    uint16_t p = 0;
    uint8_t  l;

    switch (reg) {
        case 0x20:
            p = ((uint16_t) (dev->regs[reg] & 0xfc)) << 2;
            p &= 0x0ff0;
            if ((p < 0x0100) || (p > 0x03f0))
                p = 0x03f0;
            break;
        case 0x23:
            l = get_lpt_length(dev);
            p = ((uint16_t) (dev->regs[reg] & 0xff)) << 2;
            /* 8 ports in EPP mode, 4 in non-EPP mode. */
            if ((l & 0x0f) == 8)
                p &= 0x03f8;
            else
                p &= 0x03fc;
            if ((p < 0x0100) || (p > 0x03ff))
                p = LPT1_ADDR;
            break;
        case 0x24:
            p = ((uint16_t) (dev->regs[reg] & 0xfe)) << 2;
            p &= 0x0ff8;
            if ((p < 0x0100) || (p > 0x03f8))
                p = COM1_ADDR;
            break;
        case 0x25:
            p = ((uint16_t) (dev->regs[reg] & 0xfe)) << 2;
            p &= 0x0ff8;
            if ((p < 0x0100) || (p > 0x03f8))
                p = COM2_ADDR;
            break;

        default:
            break;
    }

    return p;
}

static void
w83877_ide_handler(w83877_t *dev)
{
    uint16_t ide_port     = 0x0000;

    if (dev->has_ide > 0) {
        int ide_id = dev->has_ide - 1;

        ide_handlers(ide_id, 0);

        ide_port     = (dev->regs[0x21] << 2) & 0xfff0;
        ide_set_base_addr(ide_id, 0, ide_port);

        ide_port     = ((dev->regs[0x22] << 2) & 0xfff0) | 0x0006;
        ide_set_base_addr(ide_id, 1, ide_port);

        if (!(dev->regs[0x06] & 0x04))
            ide_handlers(ide_id, 1);
    }
}

static void
w83877_fdc_handler(w83877_t *dev)
{
    fdc_remove(dev->fdc);
    if (!(dev->regs[0x06] & 0x08) && (dev->regs[0x20] & 0xc0))
        fdc_set_base(dev->fdc, make_port(dev, 0x20));
    fdc_set_irq(dev->fdc, dev->irq_map[dev->regs[0x29] >> 4]);
    fdc_set_dma_ch(dev->fdc, dev->dma_map[(dev->regs[0x26] >> 4) & 0x03]);
    fdc_set_power_down(dev->fdc, !!(dev->regs[0x06] & 0x08));
}

static void
w83877_lpt_handler(w83877_t *dev)
{
    const uint8_t lpt_irq = dev->irq_map[PRTIQS];

    lpt_port_remove(dev->lpt);

    lpt_set_ext(dev->lpt, 1);

    lpt_set_epp(dev->lpt, (dev->regs[0x09] & 0x80) && (dev->regs[0x00] & 0x04));
    lpt_set_ecp(dev->lpt, (dev->regs[0x09] & 0x80) && (dev->regs[0x00] & 0x08));

    lpt_set_fifo_threshold(dev->lpt, dev->regs[0x05] & 0x0f);

    if (!(dev->regs[0x04] & 0x80) && (dev->regs[0x23] & 0xc0))
        lpt_port_setup(dev->lpt, make_port(dev, 0x23));

    lpt_port_irq(dev->lpt, lpt_irq);
    lpt_port_dma(dev->lpt, dev->dma_map[dev->regs[0x26] & 0x03]);

    lpt_set_cnfgb_readout(dev->lpt, ((dev->regs[0x27] & 0xe0) >> 2) | 0x07);
}

static void
w83877_serial_handler(w83877_t *dev, int uart)
{
    int    reg_mask  = uart ? 0x10 : 0x20;
    int    reg_id    = uart ? 0x25 : 0x24;
    int    irq_mask  = uart ? 0x0f : 0xf0;
    int    irq_shift = uart ? 0 : 4;
    double clock_src = 24000000.0 / 13.0;

    serial_remove(dev->uart[uart]);
    if (!(dev->regs[4] & reg_mask) && (dev->regs[reg_id] & 0xc0))
        serial_setup(dev->uart[uart], make_port(dev, reg_id), dev->irq_map[(dev->regs[0x28] & irq_mask) >> irq_shift]);

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
w83877_write(uint16_t port, uint8_t val, void *priv)
{
    w83877_t *dev    = (w83877_t *) priv;
    uint8_t    valxor = 0;

    if (port == 0x0250) {
        if (val == dev->key)
            dev->locked = 1;
        else
            dev->locked = 0;
        return;
    } else if (port == 0x0251) {
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
                dev->cur_reg = val;

                if (val == 0xaa)
                    dev->locked = 0;
            } else {
                if (dev->tries)
                    dev->tries = 0;
            }
        }
        return;
    } else if ((port == 0x0252) || (port == 0x03f1)) {
        if (dev->locked) {
            if (dev->rw_locked)
                return;
            valxor                  = val ^ dev->regs[dev->cur_reg];
            dev->regs[dev->cur_reg] = val;
        } else
            return;
    }

    switch (dev->cur_reg) {
        case 0x00:
            if (valxor & 0x0c)
                w83877_lpt_handler(dev);
            break;
        case 0x01:
            if (valxor & 0x80)
                fdc_set_swap(dev->fdc, (dev->regs[0x01] & 0x80) ? 1 : 0);
            break;
        case 0x03:
            if (valxor & 0x02)
                w83877_serial_handler(dev, 0);
            if (valxor & 0x01)
                w83877_serial_handler(dev, 1);
            break;
        case 0x04:
            if (valxor & 0x10)
                w83877_serial_handler(dev, 1);
            if (valxor & 0x20)
                w83877_serial_handler(dev, 0);
            if (valxor & 0x80)
                w83877_lpt_handler(dev);
            break;
        case 0x05:
            if (valxor & 0x0f)
                w83877_lpt_handler(dev);
            break;
        case 0x06:
            if (valxor & 0x08)
                w83877_fdc_handler(dev);
            if (valxor & 0x04)
                w83877_ide_handler(dev);
            break;
        case 0x07:
            if (valxor & 0x03)
                fdc_update_rwc(dev->fdc, 0, FDDA_TYPE);
            if (valxor & 0x0c)
                fdc_update_rwc(dev->fdc, 1, FDDB_TYPE);
            if (valxor & 0x30)
                fdc_update_rwc(dev->fdc, 2, FDDC_TYPE);
            if (valxor & 0xc0)
                fdc_update_rwc(dev->fdc, 3, FDDD_TYPE);
            break;
        case 0x08:
            if (valxor & 0x03)
                fdc_update_boot_drive(dev->fdc, FD_BOOT);
            if (valxor & 0x10)
                fdc_set_swwp(dev->fdc, SWWP ? 1 : 0);
            if (valxor & 0x20)
                fdc_set_diswr(dev->fdc, DISFDDWR ? 1 : 0);
            break;
        case 0x09:
            if (valxor & 0x20)
                fdc_update_enh_mode(dev->fdc, EN3MODE ? 1 : 0);
            if (valxor & 0x40)
                dev->rw_locked = (val & 0x40) ? 1 : 0;
            if (valxor & 0x80)
                w83877_lpt_handler(dev);
            break;
        case 0x0b:
            if (valxor & 0x0c) {
                fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);
                switch (val & 0x0c) {
                    case 0x00:
                        fdc_set_flags(dev->fdc, FDC_FLAG_PS2);
                        break;
                    case 0x04:
                        fdc_set_flags(dev->fdc, FDC_FLAG_PS2_MCA);
                        break;
                }
            }
            if (valxor & 0x02)
                fdc_update_densel_polarity(dev->fdc, INVERTZ ? 1 : 0);
            if (valxor & 0x01)
                fdc_update_drv2en(dev->fdc, DRV2EN_NEG ? 0 : 1);
            break;
        case 0x0c:
            if (valxor & 0x20)
                w83877_remap(dev);
            break;
        case 0x16:
            if (valxor & 0x02) {
                dev->regs[0x1e] = (val & 0x02) ? 0x81 : 0x00;
                dev->regs[0x20] = (val & 0x02) ? 0xfc : 0x00;
                dev->regs[0x21] = (val & 0x02) ? 0x7c : 0x00;
                dev->regs[0x22] = (val & 0x02) ? 0xfd : 0x00;
                dev->regs[0x23] = (val & 0x02) ? 0xde : 0x00;
                dev->regs[0x24] = (val & 0x02) ? 0xfe : 0x00;
                dev->regs[0x25] = (val & 0x02) ? 0xbe : 0x00;
                dev->regs[0x26] = (val & 0x02) ? 0x23 : 0x00;
                dev->regs[0x27] = (val & 0x02) ? 0x65 : 0x00;
                dev->regs[0x28] = (val & 0x02) ? 0x43 : 0x00;
                dev->regs[0x29] = (val & 0x02) ? 0x62 : 0x00;
                w83877_fdc_handler(dev);
                w83877_lpt_handler(dev);
                w83877_serial_handler(dev, 0);
                w83877_serial_handler(dev, 1);
            }
            if (valxor & 0x01)
                w83877_remap(dev);
            break;
        case 0x19:
            if (valxor & 0x02)
                w83877_serial_handler(dev, 0);
            if (valxor & 0x01)
                w83877_serial_handler(dev, 1);
            break;
        case 0x20:
            if (valxor)
                w83877_fdc_handler(dev);
            break;
        case 0x21: case 0x22:
            if (valxor)
                w83877_ide_handler(dev);
            break;
        case 0x23:
            if (valxor)
                w83877_lpt_handler(dev);
            break;
        case 0x24:
            if (valxor & 0xfe)
                w83877_serial_handler(dev, 0);
            break;
        case 0x25:
            if (valxor & 0xfe)
                w83877_serial_handler(dev, 1);
            break;
        case 0x26:
            if (valxor & 0x0f)
                w83877_lpt_handler(dev);
            if (valxor & 0xf0)
                w83877_fdc_handler(dev);
            break;
        case 0x27:
            if (valxor & 0xef)
                w83877_lpt_handler(dev);
            break;
        case 0x28:
            if (valxor & 0x0f) {
                if ((dev->regs[0x28] & 0x0f) == 0)
                    dev->regs[0x28] |= 0x03;
                w83877_serial_handler(dev, 1);
            }
            if (valxor & 0xf0) {
                if ((dev->regs[0x28] & 0xf0) == 0)
                    dev->regs[0x28] |= 0x40;
                w83877_serial_handler(dev, 0);
            }
            break;
        case 0x29:
            if (valxor & 0xf0)
                w83877_fdc_handler(dev);
            break;

        default:
            break;
    }
}

static uint8_t
w83877_read(uint16_t port, void *priv)
{
    w83877_t *dev = (w83877_t *) priv;
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
w83877_reset(w83877_t *dev)
{
    fdc_reset(dev->fdc);

    memset(dev->regs, 0, 256);
    dev->regs[0x03] = 0x30;
    dev->regs[0x07] = 0xf5;
    dev->regs[0x09] = (dev->reg_init >> 8) & 0xff;
    dev->regs[0x0a] = 0x1f;
    dev->regs[0x0c] = 0x28;
    dev->regs[0x0d] = 0xa3;
    dev->regs[0x16] = (dev->reg_init & 0xff) | 0x02;
    dev->regs[0x1e] = 0x81;
    dev->regs[0x20] = 0xfc;
    dev->regs[0x21] = 0x7c;
    dev->regs[0x22] = 0xfd;
    dev->regs[0x23] = 0xde;
    dev->regs[0x24] = 0xfe;
    dev->regs[0x25] = 0xbe;
    dev->regs[0x26] = 0x23;
    dev->regs[0x27] = 0x65;
    dev->regs[0x28] = 0x43;
    dev->regs[0x29] = 0x62;

    w83877_fdc_handler(dev);
    fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);

    w83877_lpt_handler(dev);

    w83877_serial_handler(dev, 0);
    w83877_serial_handler(dev, 1);

    if (dev->has_ide)
        w83877_ide_handler(dev);

    dev->base_address = FDC_PRIMARY_ADDR;
    dev->key          = 0x89;
    dev->key_times    = 1;

    w83877_remap(dev);

    dev->locked    = 0;
    dev->rw_locked = 0;
}

static void
w83877_close(void *priv)
{
    w83877_t *dev = (w83877_t *) priv;

    free(dev);
}

static void *
w83877_init(const device_t *info)
{
    w83877_t *dev = (w83877_t *) calloc(1, sizeof(w83877_t));

    dev->fdc = device_add(&fdc_at_winbond_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt = device_add_inst(&lpt_port_device, 1);

    dev->reg_init = info->local;

    dev->has_ide = (info->local >> 16) & 0xff;

    if (machines[machine].init == machine_at_ficpa2012_init) {
        dev->dma_map[0] = 4;
        dev->dma_map[1] = 3;
        dev->dma_map[2] = 1;
        dev->dma_map[3] = 2;
    } else {
        dev->dma_map[0] = 4;
        for (int i = 1; i < 4; i++)
            dev->dma_map[i] = i;
    }

    memset(dev->irq_map, 0xff, 16);
    dev->irq_map[0] = 0xff;
    for (int i = 1; i < 7; i++)
         dev->irq_map[i] = i;
    dev->irq_map[1] = 5;
    dev->irq_map[5] = 7;
    dev->irq_map[7] = 9;    /* Guesswork, I can't find a single BIOS that lets me assign IRQ_G to something. */
    dev->irq_map[8] = 10;

    w83877_reset(dev);

    return dev;
}

const device_t w83877_device = {
    .name          = "Winbond W83877F Super I/O",
    .internal_name = "w83877",
    .flags         = 0,
    .local         = 0,
    .init          = w83877_init,
    .close         = w83877_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
