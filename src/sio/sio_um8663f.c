/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the UMC UMF8663F Super I/O chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2024 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/sio.h>
#include <86box/random.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_UM8663F_LOG
int um8663f_do_log = ENABLE_UM8663F_LOG;

static void
um8663f_log(const char *fmt, ...)
{
    va_list ap;

    if (um8663f_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define um8663f_log(fmt, ...)
#endif

typedef struct um8663f_t {
    uint8_t max_reg;
    uint8_t ide;
    uint8_t locked;
    uint8_t cur_reg;
    uint8_t regs[5];

    fdc_t    *fdc;
    serial_t *uart[2];
} um8663f_t;

static void
um8663f_fdc_handler(um8663f_t *dev)
{
    fdc_remove(dev->fdc);
    if (dev->regs[0] & 0x01)
        fdc_set_base(dev->fdc, (dev->regs[1] & 0x01) ? FDC_PRIMARY_ADDR : FDC_SECONDARY_ADDR);
}

static void
um8663f_uart_handler(um8663f_t *dev, int port)
{
    uint8_t shift     = (port + 1);

    serial_remove(dev->uart[port]);
    if (dev->regs[0] & (2 << port)) {
        switch ((dev->regs[1] >> shift) & 0x01) {
            case 0x00:
                if (port == 1)
                    serial_setup(dev->uart[port], COM4_ADDR, COM4_IRQ);
                else
                    serial_setup(dev->uart[port], COM3_ADDR, COM1_IRQ);
                break;
            case 0x01:
                if (port == 1)
                    serial_setup(dev->uart[port], COM2_ADDR, COM2_IRQ);
                else
                    serial_setup(dev->uart[port], COM1_ADDR, COM1_IRQ);
                break;

            default:
                break;
        }
    }
}

static void
um8663f_lpt_handler(um8663f_t *dev)
{
    lpt1_remove();
    if (dev->regs[0] & 0x08) {
        switch ((dev->regs[1] >> 3) & 0x01) {
            case 0x01:
                lpt1_setup(LPT1_ADDR);
                lpt1_irq(LPT1_IRQ);
                break;
            case 0x00:
                lpt1_setup(LPT2_ADDR);
                lpt1_irq(LPT2_IRQ);
                break;

            default:
                break;
        }
    }
}

static void
um8663f_ide_handler(um8663f_t *dev)
{
    int board = dev->ide - 1;

    if (dev->ide > 0) {
        ide_handlers(board, 0);
        ide_set_base(board, (dev->regs[1] & 0x10) ? 0x01f0 : 0x0170);
        ide_set_side(board, (dev->regs[1] & 0x10) ? 0x03f6 : 0x0376);
        if (dev->regs[0] & 0x10)
            ide_handlers(board, 1);
    }
}

static void
um8663f_write(uint16_t port, uint8_t val, void *priv)
{
    um8663f_t *dev = (um8663f_t *) priv;
    uint8_t valxor;

    um8663f_log("UM8663F: write(%04X, %02X)\n", port, val);

    if (dev->locked) {
        if ((port == 0x108) && (val == 0xaa))
            dev->locked = 0;
    } else {
        if (port == 0x108) {
            if (val == 0x55)
                dev->locked = 1;
            else
                dev->cur_reg = val;
        } else if ((dev->cur_reg >= 0xc0) && (dev->cur_reg <= dev->max_reg)) {
            valxor = (dev->regs[dev->cur_reg - 0xc0] ^ val);
            dev->regs[dev->cur_reg - 0xc0] = val;
            switch (dev->cur_reg - 0xc0) {
                /* Port enable register. */
                case 0x00:
                    if (valxor & 0x10)
                        um8663f_ide_handler(dev);
                    if (valxor & 0x08)
                        um8663f_lpt_handler(dev);
                    if (valxor & 0x04)
                        um8663f_uart_handler(dev, 1);
                    if (valxor & 0x02)
                        um8663f_uart_handler(dev, 0);
                    if (valxor & 0x01)
                        um8663f_fdc_handler(dev);
                    break;
                /*
                   Port configuration register:
                   - Bits 7, 6:
                     - 0, 0 = LPT 1 is none;
                     - 0, 1 = LPT 1 is EPP;
                     - 1, 0 = LPT 1 is SPP;
                     - 1, 1 = LPT 1 is ECP;
                   - Bit 4 = 0 = IDE is secondary, 1 = IDE is primary;
                   - Bit 3 = 0 = LPT 1 is 278h, 1 = LPT 1 is 378h;
                   - Bit 2 = 0 = UART 2 is COM4, 1 = UART 2 is COM2;
                   - Bit 1 = 0 = UART 1 is COM3, 1 = UART 2 is COM1;
                   - Bit 0 = 0 = FDC is 370h, 1 = UART 2 is 3f0h.
                  */
                case 0x01:
                    if (valxor & 0x10)
                        um8663f_ide_handler(dev);
                    if (valxor & 0x08)
                        um8663f_lpt_handler(dev);
                    if (valxor & 0x04)
                        um8663f_uart_handler(dev, 1);
                    if (valxor & 0x02)
                        um8663f_uart_handler(dev, 0);
                    if (valxor & 0x01)
                        um8663f_fdc_handler(dev);
                    break;
            }
        }
    }
}

static uint8_t
um8663f_read(uint16_t port, void *priv)
{
    const um8663f_t *dev = (um8663f_t *) priv;
    uint8_t          ret = 0xff;

    if (!dev->locked) {
        if (port == 0x108)
            ret = dev->cur_reg; /* ??? */
        else if ((dev->cur_reg >= 0xc0) && (dev->cur_reg <= dev->max_reg)) {
            ret = dev->regs[dev->cur_reg - 0xc0];
            if (dev->cur_reg == 0xc0)
                ret = (ret & 0x1f) | ((random_generate() & 0x07) << 5);
        }
    }

    um8663f_log("UM8663F: read(%04X) = %02X\n", port, ret);

    return ret;
}

static void
um8663f_reset(void *priv)
{
    um8663f_t *dev = (um8663f_t *) priv;

    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);

    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

    lpt1_remove();
    lpt1_setup(LPT1_ADDR);

    fdc_reset(dev->fdc);
    fdc_remove(dev->fdc);

    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x00] = (dev->ide > 0) ? 0x1f : 0x0f;
    dev->regs[0x01] = (dev->ide == 2) ? 0x0f : 0x1f;

    um8663f_fdc_handler(dev);
    um8663f_uart_handler(dev, 0);
    um8663f_uart_handler(dev, 1);
    um8663f_lpt_handler(dev);
    um8663f_ide_handler(dev);

    dev->locked = 1;
}

static void
um8663f_close(void *priv)
{
    um8663f_t *dev = (um8663f_t *) priv;

    free(dev);
}

static void *
um8663f_init(UNUSED(const device_t *info))
{
    um8663f_t *dev = (um8663f_t *) calloc(1, sizeof(um8663f_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->ide = info->local & 0xff;
    if (dev->ide < IDE_BUS_MAX)
        device_add(&ide_isa_device);

    dev->max_reg = info->local >> 8;

    io_sethandler(0x0108, 0x0002, um8663f_read, NULL, NULL, um8663f_write, NULL, NULL, dev);

    um8663f_reset(dev);

    return dev;
}

const device_t um8663af_device = {
    .name          = "UMC UM8663AF Super I/O",
    .internal_name = "um8663af",
    .flags         = 0,
    .local         = 0xc300,
    .init          = um8663f_init,
    .close         = um8663f_close,
    .reset         = um8663f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t um8663af_ide_device = {
    .name          = "UMC UM8663AF Super I/O (With IDE)",
    .internal_name = "um8663af_ide",
    .flags         = 0,
    .local         = 0xc301,
    .init          = um8663f_init,
    .close         = um8663f_close,
    .reset         = um8663f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t um8663af_ide_sec_device = {
    .name          = "UMC UM8663AF Super I/O (With Secondary IDE)",
    .internal_name = "um8663af_ide_sec",
    .flags         = 0,
    .local         = 0xc302,
    .init          = um8663f_init,
    .close         = um8663f_close,
    .reset         = um8663f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t um8663bf_device = {
    .name          = "UMC UM8663BF Super I/O",
    .internal_name = "um8663bf",
    .flags         = 0,
    .local         = 0xc400,
    .init          = um8663f_init,
    .close         = um8663f_close,
    .reset         = um8663f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t um8663bf_ide_device = {
    .name          = "UMC UM8663BF Super I/O (With IDE)",
    .internal_name = "um8663bf_ide",
    .flags         = 0,
    .local         = 0xc401,
    .init          = um8663f_init,
    .close         = um8663f_close,
    .reset         = um8663f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t um8663bf_ide_sec_device = {
    .name          = "UMC UM8663BF Super I/O (With Secondary IDE)",
    .internal_name = "um8663bf_ide_sec",
    .flags         = 0,
    .local         = 0xc402,
    .init          = um8663f_init,
    .close         = um8663f_close,
    .reset         = um8663f_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
