/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Intel 82091AA Super I/O chip.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
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
#include <86box/lpt.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>

typedef struct i82091aa_t {
    uint8_t   cur_reg;
    uint8_t   has_ide;
    uint8_t   regs[81];
    uint16_t  base_address;
    fdc_t    *fdc;
    serial_t *uart[2];
} i82091aa_t;

static void
fdc_handler(i82091aa_t *dev)
{
    fdc_remove(dev->fdc);
    if (dev->regs[0x10] & 0x01)
        fdc_set_base(dev->fdc, (dev->regs[0x10] & 0x02) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
}

static void
lpt1_handler(i82091aa_t *dev)
{
    uint16_t lpt_port = LPT1_ADDR;

    lpt1_remove();

    switch ((dev->regs[0x20] >> 1) & 0x03) {
        case 0x00:
            lpt_port = LPT1_ADDR;
            break;
        case 1:
            lpt_port = LPT2_ADDR;
            break;
        case 2:
            lpt_port = LPT_MDA_ADDR;
            break;
        case 3:
            lpt_port = 0x000;
            break;

        default:
            break;
    }

    if ((dev->regs[0x20] & 0x01) && lpt_port)
        lpt1_setup(lpt_port);

    lpt1_irq((dev->regs[0x20] & 0x08) ? LPT1_IRQ : LPT2_IRQ);
}

static void
serial_handler(i82091aa_t *dev, int uart)
{
    int      reg       = (0x30 + (uart << 4));
    uint16_t uart_port = COM1_ADDR;

    serial_remove(dev->uart[uart]);

    switch ((dev->regs[reg] >> 1) & 0x07) {
        case 0x00:
            uart_port = COM1_ADDR;
            break;
        case 0x01:
            uart_port = COM2_ADDR;
            break;
        case 0x02:
            uart_port = 0x220;
            break;
        case 0x03:
            uart_port = 0x228;
            break;
        case 0x04:
            uart_port = 0x238;
            break;
        case 0x05:
            uart_port = COM4_ADDR;
            break;
        case 0x06:
            uart_port = 0x338;
            break;
        case 0x07:
            uart_port = COM3_ADDR;
            break;

        default:
            break;
    }

    if (dev->regs[reg] & 0x01)
        serial_setup(dev->uart[uart], uart_port, (dev->regs[reg] & 0x10) ? COM1_IRQ : COM2_IRQ);
}

static void
ide_handler(i82091aa_t *dev)
{
    int board = dev->has_ide - 1;

    ide_remove_handlers(board);
    ide_set_base(board, (dev->regs[0x50] & 0x02) ? 0x170 : 0x1f0);
    ide_set_side(board, (dev->regs[0x50] & 0x02) ? 0x376 : 0x3f6);
    if (dev->regs[0x50] & 0x01)
        ide_set_handlers(board);
}

static void
i82091aa_write(uint16_t port, uint8_t val, void *priv)
{
    i82091aa_t *dev = (i82091aa_t *) priv;
    uint8_t     index;
    uint8_t     valxor = 0;
    uint8_t     uart = (dev->cur_reg >> 4) - 0x03;
    uint8_t    *reg  = &(dev->regs[dev->cur_reg]);

    index = (port & 1) ? 0 : 1;

    if (index) {
        dev->cur_reg = val;
        return;
    } else if (dev->cur_reg < 0x51)
        valxor = val ^ *reg;
    else if (dev->cur_reg >= 0x51)
        return;

    switch (dev->cur_reg) {
        case 0x02:
            *reg = (*reg & 0x78) | (val & 0x01);
            break;
        case 0x03:
            *reg = (val & 0xf8);
            break;
        case 0x10:
            *reg = (val & 0x83);
            if (valxor & 0x03)
                fdc_handler(dev);
            break;
        case 0x11:
            *reg = (val & 0x0f);
            if ((valxor & 0x04) && (val & 0x04))
                fdc_reset(dev->fdc);
            break;
        case 0x20:
            *reg = (val & 0xef);
            if (valxor & 0x07)
                lpt1_handler(dev);
            break;
        case 0x21:
            *reg = (val & 0x2f);
            break;
        case 0x30:
        case 0x40:
            *reg = (val & 0x9f);
            if (valxor & 0x1f)
                serial_handler(dev, uart);
            if (valxor & 0x80)
                serial_set_clock_src(dev->uart[uart], (val & 0x80) ? 2000000.0 : (24000000.0 / 13.0));
            break;
        case 0x31:
        case 0x41:
            *reg = (val & 0x1f);
            if ((valxor & 0x04) && (val & 0x04))
                serial_reset_port(dev->uart[uart]);
            break;
        case 0x50:
            *reg = (val & 0x07);
            if (dev->has_ide && (valxor & 0x03))
                ide_handler(dev);
            break;

        default:
            break;
    }
}

uint8_t
i82091aa_read(uint16_t port, void *priv)
{
    const i82091aa_t *dev = (i82091aa_t *) priv;
    uint8_t           ret = 0xff;
    uint8_t           index;

    index = (port & 1) ? 0 : 1;

    if (index)
        ret = dev->cur_reg;
    else if (dev->cur_reg < 0x51)
        ret = dev->regs[dev->cur_reg];

    return ret;
}

void
i82091aa_reset(i82091aa_t *dev)
{
    memset(dev->regs, 0x00, 81);

    dev->regs[0x00] = 0xa0;
    dev->regs[0x10] = 0x01;
    dev->regs[0x31] = dev->regs[0x41] = 0x02;
    dev->regs[0x50]                   = 0x01;

    fdc_reset(dev->fdc);

    fdc_handler(dev);
    lpt1_handler(dev);
    serial_handler(dev, 0);
    serial_handler(dev, 1);
    serial_set_clock_src(dev->uart[0], (24000000.0 / 13.0));
    serial_set_clock_src(dev->uart[1], (24000000.0 / 13.0));

    if (dev->has_ide)
        ide_handler(dev);
}

static void
i82091aa_close(void *priv)
{
    i82091aa_t *dev = (i82091aa_t *) priv;

    free(dev);
}

static void *
i82091aa_init(const device_t *info)
{
    i82091aa_t *dev = (i82091aa_t *) calloc(1, sizeof(i82091aa_t));

    dev->fdc = device_add(&fdc_at_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->has_ide = (info->local >> 9) & 0x03;

    i82091aa_reset(dev);

    dev->regs[0x02] = info->local & 0xff;

    if (info->local & 0x08)
        dev->base_address = (info->local & 0x100) ? 0x0398 : 0x0024;
    else
        dev->base_address = (info->local & 0x100) ? 0x026e : 0x0022;

    io_sethandler(dev->base_address, 0x0002,
                  i82091aa_read, NULL, NULL, i82091aa_write, NULL, NULL, dev);

    return dev;
}

const device_t i82091aa_device = {
    .name          = "Intel 82091AA Super I/O",
    .internal_name = "i82091aa",
    .flags         = 0,
    .local         = 0x40,
    .init          = i82091aa_init,
    .close         = i82091aa_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i82091aa_398_device = {
    .name          = "Intel 82091AA Super I/O (Port 398h)",
    .internal_name = "i82091aa_398",
    .flags         = 0,
    .local         = 0x148,
    .init          = i82091aa_init,
    .close         = i82091aa_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i82091aa_ide_pri_device = {
    .name          = "Intel 82091AA Super I/O (With Primary IDE)",
    .internal_name = "i82091aa_ide",
    .flags         = 0,
    .local         = 0x240,
    .init          = i82091aa_init,
    .close         = i82091aa_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i82091aa_ide_device = {
    .name          = "Intel 82091AA Super I/O (With IDE)",
    .internal_name = "i82091aa_ide",
    .flags         = 0,
    .local         = 0x440,
    .init          = i82091aa_init,
    .close         = i82091aa_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
