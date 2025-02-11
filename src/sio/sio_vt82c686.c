/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the VIA VT82C686A/B integrated Super I/O.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2020 RichardG.
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
#include <86box/plat_unused.h>

typedef struct vt82c686_t {
    uint8_t   cur_reg;
    uint8_t   last_val;
    uint8_t   regs[25];
    uint8_t   fdc_dma;
    uint8_t   fdc_irq;
    uint8_t   uart_irq[2];
    uint8_t   lpt_dma;
    uint8_t   lpt_irq;
    fdc_t    *fdc;
    serial_t *uart[2];
} vt82c686_t;

static uint8_t
get_lpt_length(vt82c686_t *dev)
{
    uint8_t length = 4; /* non-EPP */

    if ((dev->regs[0x02] & 0x03) == 0x02)
        length = 8; /* EPP */

    return length;
}

static void
vt82c686_fdc_handler(vt82c686_t *dev)
{
    uint16_t io_base = (dev->regs[0x03] & 0xfc) << 2;

    fdc_remove(dev->fdc);

    if ((dev->regs[0x02] & 0x10) && !(dev->regs[0x0f] & 0x03))
        fdc_set_base(dev->fdc, io_base);

    fdc_set_dma_ch(dev->fdc, dev->fdc_dma);
    fdc_set_irq(dev->fdc, dev->fdc_irq);
    fdc_set_swap(dev->fdc, dev->regs[0x16] & 0x01);
}

static void
vt82c686_lpt_handler(vt82c686_t *dev)
{
    uint16_t io_mask;
    uint16_t io_base = dev->regs[0x06] << 2;
    int      io_len = get_lpt_length(dev);
    io_base &= (0xff8 | io_len);
    io_mask = 0x3fc; /* non-EPP */
    if (io_len == 8)
        io_mask = 0x3f8; /* EPP */

    lpt1_remove();

    if (((dev->regs[0x02] & 0x03) != 0x03) && !(dev->regs[0x0f] & 0x11) && (io_base >= 0x100) && (io_base <= io_mask))
        lpt1_setup(io_base);

    if (dev->lpt_irq) {
        lpt1_irq(dev->lpt_irq);
    } else {
        lpt1_irq(0xff);
    }
}

static void
vt82c686_serial_handler(vt82c686_t *dev, int uart)
{
    serial_remove(dev->uart[uart]);

    if ((dev->regs[0x02] & (0x04 << uart)) && !(dev->regs[0x0f] & ((0x04 << uart) | 0x01)))
        serial_setup(dev->uart[uart], dev->regs[0x07 + uart] << 2, dev->uart_irq[uart]);
}

static void
vt82c686_write(uint16_t port, uint8_t val, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;

    /* Store last written value for echo (see comment on read). */
    dev->last_val = val;

    /* Write current register index on port 0. */
    if (!(port & 1)) {
        dev->cur_reg = val;
        return;
    }

    /* NOTE: Registers are [0xE0:0xF8] but we store them as [0x00:0x18]. */
    if ((dev->cur_reg < 0xe0) || (dev->cur_reg > 0xf8))
        return;
    uint8_t reg = dev->cur_reg & 0x1f;

    /* Read-only registers. */
    if ((reg < 0x02) || (reg == 0x0c))
        return;

    /* Write current register value on port 1. */
    dev->regs[reg] = val;

    /* Update device state. */
    switch (reg) {
        case 0x02:
            dev->regs[reg] &= 0xbf;
            vt82c686_lpt_handler(dev);
            vt82c686_serial_handler(dev, 0);
            vt82c686_serial_handler(dev, 1);
            vt82c686_fdc_handler(dev);
            break;

        case 0x03:
            dev->regs[reg] &= 0xfc;
            vt82c686_fdc_handler(dev);
            break;

        case 0x04:
            dev->regs[reg] &= 0xfc;
            break;

        case 0x05:
            dev->regs[reg] |= 0x03;
            break;

        case 0x06:
            vt82c686_lpt_handler(dev);
            break;

        case 0x07:
        case 0x08:
            dev->regs[reg] &= 0xfe;
            vt82c686_serial_handler(dev, reg == 0x08);
            break;

        case 0x0d:
            dev->regs[reg] &= 0x0f;
            break;

        case 0x0f:
            dev->regs[reg] &= 0x7f;
            vt82c686_lpt_handler(dev);
            vt82c686_serial_handler(dev, 0);
            vt82c686_serial_handler(dev, 1);
            vt82c686_fdc_handler(dev);
            break;

        case 0x10:
            dev->regs[reg] &= 0xf4;
            break;

        case 0x11:
            dev->regs[reg] &= 0x3f;
            break;

        case 0x13:
            dev->regs[reg] &= 0xfb;
            break;

        case 0x14:
        case 0x17:
            dev->regs[reg] &= 0xfe;
            break;

        case 0x16:
            dev->regs[reg] &= 0xf7;
            vt82c686_fdc_handler(dev);
            break;

        default:
            break;
    }
}

static uint8_t
vt82c686_read(uint16_t port, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;

    /* NOTE: Registers are [0xE0:0xF8] but we store them as [0x00:0x18].
       Real 686B echoes the last read/written value when reading from
       registers outside that range. */
    if (!(port & 1))
        dev->last_val = dev->cur_reg;
    else if ((dev->cur_reg >= 0xe0) && (dev->cur_reg <= 0xf8))
        dev->last_val = dev->regs[dev->cur_reg & 0x1f];

    return dev->last_val;
}

/* Writes to Super I/O-related configuration space registers
   of the VT82C686 PCI-ISA bridge are sent here by via_pipc.c */
void
vt82c686_sio_write(uint8_t addr, uint8_t val, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;

    switch (addr) {
        case 0x50:
            dev->fdc_dma = val & 0x03;
            vt82c686_fdc_handler(dev);
            dev->lpt_dma = (val >> 2) & 0x03;
            vt82c686_lpt_handler(dev);
            break;

        case 0x51:
            dev->fdc_irq = val & 0x0f;
            vt82c686_fdc_handler(dev);
            dev->lpt_irq = val >> 4;
            vt82c686_lpt_handler(dev);
            break;

        case 0x52:
            dev->uart_irq[0] = val & 0x0f;
            vt82c686_serial_handler(dev, 0);
            dev->uart_irq[1] = val >> 4;
            vt82c686_serial_handler(dev, 1);
            break;

        case 0x85:
            io_removehandler(FDC_PRIMARY_ADDR, 2, vt82c686_read, NULL, NULL, vt82c686_write, NULL, NULL, dev);
            if (val & 0x02)
                io_sethandler(FDC_PRIMARY_ADDR, 2, vt82c686_read, NULL, NULL, vt82c686_write, NULL, NULL, dev);
            break;

        default:
            break;
    }
}

static void
vt82c686_reset(vt82c686_t *dev)
{
    memset(dev->regs, 0, 21);

    dev->regs[0x00] = 0x3c;
    dev->regs[0x02] = 0x03;

    fdc_reset(dev->fdc);

    vt82c686_lpt_handler(dev);
    vt82c686_serial_handler(dev, 0);
    vt82c686_serial_handler(dev, 1);
    vt82c686_fdc_handler(dev);

    vt82c686_sio_write(0x85, 0x00, dev);
}

static void
vt82c686_close(void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;

    free(dev);
}

static void *
vt82c686_init(UNUSED(const device_t *info))
{
    vt82c686_t *dev = (vt82c686_t *) calloc(1, sizeof(vt82c686_t));

    dev->fdc     = device_add(&fdc_at_smc_device);
    dev->fdc_dma = 2;

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt_dma = 3;

    vt82c686_reset(dev);

    return dev;
}

const device_t via_vt82c686_sio_device = {
    .name          = "VIA VT82C686 Integrated Super I/O",
    .internal_name = "via_vt82c686_sio",
    .flags         = 0,
    .local         = 0,
    .init          = vt82c686_init,
    .close         = vt82c686_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
