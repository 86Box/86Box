/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Dataworld 90C50 (COMBAT) Super I/O chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
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
#include <86box/plat_unused.h>

#ifdef ENABLE_90C50_LOG
int dw90c50_do_log = ENABLE_90C50_LOG;

static void
dw90c50_log(const char *fmt, ...)
{
    va_list ap;

    if (dw90c50_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define dw90c50_log(fmt, ...)
#endif

typedef struct dw90c50_t {
    uint8_t   flags;
    uint8_t   reg;
    fdc_t    *fdc;
    serial_t *uart[2];
    lpt_t    *lpt;
} dw90c50_t;

static void
lpt_handler(dw90c50_t *dev)
{
    int      temp;
    uint16_t lpt_port = LPT1_ADDR;
    uint8_t  lpt_irq  = LPT1_IRQ;

    /* bits 0-1:
     * 00 378h
     * 01 3bch
     * 10 278h
     * 11 disabled
     */
    temp = (dev->reg & 0x06) >> 1;

    lpt_port_remove(dev->lpt);

    switch (temp) {
        case 0x00:
            lpt_port = 0x000;
            lpt_irq  = 0xff;
            break;
        case 0x01:
            lpt_port = LPT_MDA_ADDR;
            break;
        case 0x02:
            lpt_port = LPT1_ADDR;
            break;
        case 0x03:
            lpt_port = LPT2_ADDR;
            break;

        default:
            break;
    }

    if (lpt_port)
        lpt_port_setup(dev->lpt, lpt_port);

    lpt_port_irq(dev->lpt, lpt_irq);
}

static void
serial_handler(dw90c50_t *dev)
{
    uint16_t base1 = 0x0000, base2 = 0x0000;
    uint8_t irq1, irq2;

    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);

    switch ((dev->reg >> 3) & 0x07) {
        case 0x0001:
            base1 = 0x03f8;
            break;
        case 0x0002:
            base2 = 0x02f8;
            break;
        case 0x0003:
            base1 = 0x03f8;
            base2 = 0x02f8;
            break;
        case 0x0004:
            base1 = 0x03e8;
            base2 = 0x02e8;
            break;
        case 0x0006:
            base2 = 0x03f8;
            break;
        case 0x0007:
            base1 = 0x02f8;
            base2 = 0x03f8;
            break;
    }

    irq1 = (base1 & 0x0100) ? COM1_IRQ : COM2_IRQ;
    irq2 = (base2 & 0x0100) ? COM1_IRQ : COM2_IRQ;

    if (base1 != 0x0000)
        serial_setup(dev->uart[0], base1, irq1);

    if (base2 != 0x0000)
        serial_setup(dev->uart[0], base2, irq2);
}

static void
dw90c50_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    dw90c50_t *dev = (dw90c50_t *) priv;
    uint8_t    valxor;

    dw90c50_log("[%04X:%08X] [W] %02X = %02X (%i)\n", CS, cpu_state.pc, port, val, dev->tries);

    /* Second write to config register. */
    valxor   = val ^ dev->reg;
    dev->reg = val;

    dw90c50_log("SIO: Register written %02X\n", val);

    /* Reconfigure floppy disk controller. */
    if (valxor & 0x01) {
        dw90c50_log("SIO: FDC disabled\n");
        fdc_remove(dev->fdc);
        /* Bit 0: 1 = Enable FDC. */
        if (val & 0x01) {
            dw90c50_log("SIO: FDC enabled\n");
            fdc_set_base(dev->fdc, FDC_PRIMARY_ADDR);
        }
    }

    /* Reconfigure parallel port. */
    if (valxor & 0x06)
        lpt_handler(dev);

    /* Reconfigure serial ports. */
    if (valxor & 0x38)
        serial_handler(dev);

    /* Reconfigure IDE controller. */
    if ((dev->flags & PCX73XX_IDE) && (valxor & 0x40))  {
        dw90c50_log("SIO: HDC disabled\n");
        ide_pri_disable();
        /* Bit 6: 1 = Enable IDE controller. */
        if (val & 0x40) {
            dw90c50_log("SIO: HDC enabled\n");
            ide_set_base(0, 0x1f0);
            ide_set_side(0, 0x3f6);
            ide_pri_enable();
        }
    }
}

uint8_t
dw90c50_read(UNUSED(uint16_t port), void *priv)
{
    dw90c50_t *dev = (dw90c50_t *) priv;
    uint8_t    ret = 0xff;

    ret = dev->reg;

    dw90c50_log("[%04X:%08X] [R] %02X = %02X\n", CS, cpu_state.pc, port, ret);

    return ret;
}

void
dw90c50_reset(dw90c50_t *dev)
{
    fdc_reset(dev->fdc);

    dev->reg = 0x62;
    dw90c50_write(0x03f3, 0x9d, dev);
}

static void
dw90c50_close(void *priv)
{
    dw90c50_t *dev = (dw90c50_t *) priv;

    free(dev);
}

static void *
dw90c50_init(const device_t *info)
{
    dw90c50_t *dev = (dw90c50_t *) calloc(1, sizeof(dw90c50_t));

    /* Avoid conflicting with machines that make no use of the 90C50 Internal IDE */
    dev->flags = info->local;

    dev->fdc = device_add(&fdc_at_nsc_device);

    dev->uart[0] = device_add_inst(&ns16450_device, 1);
    dev->uart[1] = device_add_inst(&ns16450_device, 2);

    dev->lpt = device_add_inst(&lpt_port_device, 1);
    lpt_set_ext(dev->lpt, 1);

    if (dev->flags & DW90C50_IDE)
        device_add(&ide_isa_device);

    dw90c50_reset(dev);

    io_sethandler(0x03f3, 0x0001,
                  dw90c50_read, NULL, NULL, dw90c50_write, NULL, NULL, dev);

    return dev;
}

const device_t dw90c50_device = {
    .name          = "National Semiconductor 90C50 Super I/O",
    .internal_name = "90c50",
    .flags         = 0,
    .local         = 0,
    .init          = dw90c50_init,
    .close         = dw90c50_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
