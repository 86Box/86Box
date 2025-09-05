/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the UMC UM82C862F, UM82C863F, UM86863F,
 *          and UM8663BF Super I/O chips.
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

#ifdef ENABLE_UM866X_LOG
int um866x_do_log = ENABLE_UM866X_LOG;

static void
um866x_log(const char *fmt, ...)
{
    va_list ap;

    if (um866x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define um866x_log(fmt, ...)
#endif

typedef struct um866x_t {
    uint8_t max_reg;
    uint8_t ide;
    uint8_t locked;
    uint8_t cur_reg;
    uint8_t regs[5];

    fdc_t    *fdc;

    serial_t *uart[2];
    lpt_t *   lpt;
} um866x_t;

static void
um866x_fdc_handler(um866x_t *dev)
{
    fdc_remove(dev->fdc);
    if (dev->regs[0] & 0x01)
        fdc_set_base(dev->fdc, (dev->regs[1] & 0x01) ? FDC_PRIMARY_ADDR : FDC_SECONDARY_ADDR);
}

static void
um866x_uart_handler(um866x_t *dev, int port)
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
um866x_lpt_handler(um866x_t *dev)
{
    int enabled = (dev->regs[0] & 0x08);

    lpt_port_remove(dev->lpt);
    if (dev->max_reg != 0x00) {
        switch(dev->regs[1] & 0xc0) {
            case 0x00:
                enabled = 0;
                break;
            case 0x40:
                lpt_set_epp(dev->lpt, 1);
                lpt_set_ecp(dev->lpt, 0);
                lpt_set_ext(dev->lpt, 0);
                break;
            case 0x80:
                lpt_set_epp(dev->lpt, 0);
                lpt_set_ecp(dev->lpt, 0);
                lpt_set_ext(dev->lpt, 1);
                break;
            case 0xc0:
                lpt_set_epp(dev->lpt, 0);
                lpt_set_ecp(dev->lpt, 1);
                lpt_set_ext(dev->lpt, 0);
                break;
        }
    }
    if (enabled) {
        switch ((dev->regs[1] >> 3) & 0x01) {
            case 0x01:
                lpt_port_setup(dev->lpt, LPT1_ADDR);
                lpt_port_irq(dev->lpt, LPT1_IRQ);
                break;
            case 0x00:
                lpt_port_setup(dev->lpt, LPT2_ADDR);
                lpt_port_irq(dev->lpt, LPT2_IRQ);
                break;

            default:
                break;
        }
    }
}

static void
um866x_ide_handler(um866x_t *dev)
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
um866x_write(uint16_t port, uint8_t val, void *priv)
{
    um866x_t *dev = (um866x_t *) priv;
    uint8_t valxor;

    um866x_log("UM866X: write(%04X, %02X)\n", port, val);

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
                        um866x_ide_handler(dev);
                    if (valxor & 0x08)
                        um866x_lpt_handler(dev);
                    if (valxor & 0x04)
                        um866x_uart_handler(dev, 1);
                    if (valxor & 0x02)
                        um866x_uart_handler(dev, 0);
                    if (valxor & 0x01)
                        um866x_fdc_handler(dev);
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
                    if (valxor & 0xc8)
                        um866x_lpt_handler(dev);
                    if (valxor & 0x10)
                        um866x_ide_handler(dev);
                    if (valxor & 0x04)
                        um866x_uart_handler(dev, 1);
                    if (valxor & 0x02)
                        um866x_uart_handler(dev, 0);
                    if (valxor & 0x01)
                        um866x_fdc_handler(dev);
                    break;
            }
        }
    }
}

static uint8_t
um866x_read(uint16_t port, void *priv)
{
    const um866x_t *dev = (um866x_t *) priv;
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

    um866x_log("UM866X: read(%04X) = %02X\n", port, ret);

    return ret;
}

static void
um866x_reset(void *priv)
{
    um866x_t *dev = (um866x_t *) priv;

    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);

    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

    lpt_port_remove(dev->lpt);
    lpt_port_setup(dev->lpt, LPT1_ADDR);

    fdc_reset(dev->fdc);
    fdc_remove(dev->fdc);

    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x00] = (dev->ide > 0) ? 0x1f : 0x0f;
    dev->regs[0x01] = (dev->ide == 2) ? 0x0f : 0x1f;

    um866x_fdc_handler(dev);
    um866x_uart_handler(dev, 0);
    um866x_uart_handler(dev, 1);
    um866x_lpt_handler(dev);
    um866x_ide_handler(dev);

    dev->locked = 1;
}

static void
um866x_close(void *priv)
{
    um866x_t *dev = (um866x_t *) priv;

    free(dev);
}

static void *
um866x_init(UNUSED(const device_t *info))
{
    um866x_t *dev = (um866x_t *) calloc(1, sizeof(um866x_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt = device_add_inst(&lpt_port_device, 1);
    lpt_set_cnfgb_readout(dev->lpt, 0x00);

    dev->ide = info->local & 0xff;
    if (dev->ide < IDE_BUS_MAX)
        device_add(&ide_isa_device);

    dev->max_reg = info->local >> 8;

    if (dev->max_reg != 0x00)
        io_sethandler(0x0108, 0x0002, um866x_read, NULL, NULL, um866x_write, NULL, NULL, dev);

    um866x_reset(dev);

    return dev;
}

const device_t um866x_device = {
    .name          = "UMC UM82C86x/866x Super I/O",
    .internal_name = "um866x",
    .flags         = 0,
    .local         = 0,
    .init          = um866x_init,
    .close         = um866x_close,
    .reset         = um866x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
