/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the NatSemi PC87332 Super I/O chip.
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

typedef struct pc87332_t {
    uint8_t   tries;
    uint8_t   has_ide;
    uint8_t   fdc_on;
    uint8_t   regs[15];
    int       cur_reg;
    fdc_t    *fdc;
    serial_t *uart[2];
} pc87332_t;

static void
lpt1_handler(pc87332_t *dev)
{
    int      temp;
    uint16_t lpt_port = LPT1_ADDR;
    uint8_t  lpt_irq  = LPT2_IRQ;

    temp = dev->regs[0x01] & 3;

    switch (temp) {
        case 0:
            lpt_port = LPT1_ADDR;
            lpt_irq  = (dev->regs[0x02] & 0x08) ? LPT1_IRQ : LPT2_IRQ;
            break;
        case 1:
            lpt_port = LPT_MDA_ADDR;
            lpt_irq  = LPT_MDA_IRQ;
            break;
        case 2:
            lpt_port = LPT2_ADDR;
            lpt_irq  = LPT2_IRQ;
            break;
        case 3:
            lpt_port = 0x000;
            lpt_irq  = 0xff;
            break;

        default:
            break;
    }

    if (lpt_port)
        lpt1_setup(lpt_port);

    lpt1_irq(lpt_irq);
}

static void
serial_handler(pc87332_t *dev, int uart)
{
    int temp;

    temp = (dev->regs[1] >> (2 << uart)) & 3;

    switch (temp) {
        case 0:
            serial_setup(dev->uart[uart], COM1_ADDR, 4);
            break;
        case 1:
            serial_setup(dev->uart[uart], COM2_ADDR, 3);
            break;
        case 2:
            switch ((dev->regs[1] >> 6) & 3) {
                case 0:
                    serial_setup(dev->uart[uart], COM3_ADDR, COM3_IRQ);
                    break;
                case 1:
                    serial_setup(dev->uart[uart], 0x338, COM3_IRQ);
                    break;
                case 2:
                    serial_setup(dev->uart[uart], COM4_ADDR, COM3_IRQ);
                    break;
                case 3:
                    serial_setup(dev->uart[uart], 0x220, COM3_IRQ);
                    break;

                default:
                    break;
            }
            break;
        case 3:
            switch ((dev->regs[1] >> 6) & 3) {
                case 0:
                    serial_setup(dev->uart[uart], COM4_ADDR, COM4_IRQ);
                    break;
                case 1:
                    serial_setup(dev->uart[uart], 0x238, COM4_IRQ);
                    break;
                case 2:
                    serial_setup(dev->uart[uart], 0x2e0, COM4_IRQ);
                    break;
                case 3:
                    serial_setup(dev->uart[uart], 0x228, COM4_IRQ);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static void
ide_handler(pc87332_t *dev)
{
    /* TODO: Make an ide_disable(channel) and ide_enable(channel) so we can simplify this. */
    if (dev->has_ide == 2) {
        ide_sec_disable();
        ide_set_base(1, (dev->regs[0x00] & 0x80) ? 0x170 : 0x1f0);
        ide_set_side(1, (dev->regs[0x00] & 0x80) ? 0x376 : 0x3f6);
        if (dev->regs[0x00] & 0x40)
            ide_sec_enable();
    } else if (dev->has_ide == 1) {
        ide_pri_disable();
        ide_set_base(0, (dev->regs[0x00] & 0x80) ? 0x170 : 0x1f0);
        ide_set_side(0, (dev->regs[0x00] & 0x80) ? 0x376 : 0x3f6);
        if (dev->regs[0x00] & 0x40)
            ide_pri_enable();
    }
}

static void
pc87332_write(uint16_t port, uint8_t val, void *priv)
{
    pc87332_t *dev = (pc87332_t *) priv;
    uint8_t    index;
    uint8_t    valxor;

    index = (port & 1) ? 0 : 1;

    if (index) {
        dev->cur_reg = val & 0x1f;
        dev->tries   = 0;
        return;
    } else {
        if (dev->tries) {
            valxor     = val ^ dev->regs[dev->cur_reg];
            dev->tries = 0;
            if ((dev->cur_reg <= 14) && (dev->cur_reg != 8))
                dev->regs[dev->cur_reg] = val;
            else
                return;
        } else {
            dev->tries++;
            return;
        }
    }

    switch (dev->cur_reg) {
        case 0:
            if (valxor & 1) {
                lpt1_remove();
                if ((val & 1) && !(dev->regs[2] & 1))
                    lpt1_handler(dev);
            }
            if (valxor & 2) {
                serial_remove(dev->uart[0]);
                if ((val & 2) && !(dev->regs[2] & 1))
                    serial_handler(dev, 0);
            }
            if (valxor & 4) {
                serial_remove(dev->uart[1]);
                if ((val & 4) && !(dev->regs[2] & 1))
                    serial_handler(dev, 1);
            }
            if (valxor & 0x28) {
                fdc_remove(dev->fdc);
                if ((val & 8) && !(dev->regs[2] & 1))
                    fdc_set_base(dev->fdc, (val & 0x20) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
            }
            if (dev->has_ide && (valxor & 0xc0))
                ide_handler(dev);
            break;
        case 1:
            if (valxor & 3) {
                lpt1_remove();
                if ((dev->regs[0] & 1) && !(dev->regs[2] & 1))
                    lpt1_handler(dev);
            }
            if (valxor & 0xcc) {
                serial_remove(dev->uart[0]);
                if ((dev->regs[0] & 2) && !(dev->regs[2] & 1))
                    serial_handler(dev, 0);
            }
            if (valxor & 0xf0) {
                serial_remove(dev->uart[1]);
                if ((dev->regs[0] & 4) && !(dev->regs[2] & 1))
                    serial_handler(dev, 1);
            }
            break;
        case 2:
            if (valxor & 1) {
                lpt1_remove();
                serial_remove(dev->uart[0]);
                serial_remove(dev->uart[1]);
                fdc_remove(dev->fdc);

                if (!(val & 1)) {
                    if (dev->regs[0] & 1)
                        lpt1_handler(dev);
                    if (dev->regs[0] & 2)
                        serial_handler(dev, 0);
                    if (dev->regs[0] & 4)
                        serial_handler(dev, 1);
                    if (dev->regs[0] & 8)
                        fdc_set_base(dev->fdc, (dev->regs[0] & 0x20) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
                }
            }
            if (valxor & 8) {
                lpt1_remove();
                if ((dev->regs[0] & 1) && !(dev->regs[2] & 1))
                    lpt1_handler(dev);
            }
            break;

        default:
            break;
    }
}

uint8_t
pc87332_read(uint16_t port, void *priv)
{
    pc87332_t *dev = (pc87332_t *) priv;
    uint8_t    ret = 0xff;
    uint8_t    index;

    index = (port & 1) ? 0 : 1;

    dev->tries = 0;

    if (index)
        ret = dev->cur_reg & 0x1f;
    else {
        if (dev->cur_reg == 8)
            ret = 0x10;
        else if (dev->cur_reg < 14)
            ret = dev->regs[dev->cur_reg];
    }

    return ret;
}

void
pc87332_reset(pc87332_t *dev)
{
    memset(dev->regs, 0, 15);

    dev->regs[0x00] = dev->fdc_on ? 0x4f : 0x07;
    if (dev->has_ide == 2)
        dev->regs[0x00] |= 0x80;
    dev->regs[0x01] = 0x10;
    dev->regs[0x03] = 0x01;
    dev->regs[0x05] = 0x0D;
    dev->regs[0x08] = 0x70;

    /*
        0 = 360 rpm @ 500 kbps for 3.5"
        1 = Default, 300 rpm @ 500, 300, 250, 1000 kbps for 3.5"
    */
    lpt1_remove();
    lpt1_handler(dev);
    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);
    serial_handler(dev, 0);
    serial_handler(dev, 1);
    fdc_reset(dev->fdc);
    if (!dev->fdc_on)
        fdc_remove(dev->fdc);

    if (dev->has_ide)
        ide_handler(dev);
}

static void
pc87332_close(void *priv)
{
    pc87332_t *dev = (pc87332_t *) priv;

    free(dev);
}

static void *
pc87332_init(const device_t *info)
{
    pc87332_t *dev = (pc87332_t *) calloc(1, sizeof(pc87332_t));

    dev->fdc = device_add(&fdc_at_nsc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->has_ide = (info->local >> 8) & 0xff;
    dev->fdc_on  = (info->local >> 16) & 0xff;
    pc87332_reset(dev);

    if ((info->local & 0xff) == 0x01) {
        io_sethandler(0x398, 0x0002,
                      pc87332_read, NULL, NULL, pc87332_write, NULL, NULL, dev);
    } else {
        io_sethandler(0x02e, 0x0002,
                      pc87332_read, NULL, NULL, pc87332_write, NULL, NULL, dev);
    }

    return dev;
}

const device_t pc87332_device = {
    .name          = "National Semiconductor PC87332 Super I/O",
    .internal_name = "pc87332",
    .flags         = 0,
    .local         = 0x00,
    .init          = pc87332_init,
    .close         = pc87332_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87332_398_device = {
    .name          = "National Semiconductor PC87332 Super I/O (Port 398h)",
    .internal_name = "pc87332_398",
    .flags         = 0,
    .local         = 0x01,
    .init          = pc87332_init,
    .close         = pc87332_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87332_398_ide_device = {
    .name          = "National Semiconductor PC87332 Super I/O (Port 398h) (With IDE)",
    .internal_name = "pc87332_398_ide",
    .flags         = 0,
    .local         = 0x101,
    .init          = pc87332_init,
    .close         = pc87332_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87332_398_ide_sec_device = {
    .name          = "National Semiconductor PC87332 Super I/O (Port 398h) (With Secondary IDE)",
    .internal_name = "pc87332_398_ide_sec",
    .flags         = 0,
    .local         = 0x201,
    .init          = pc87332_init,
    .close         = pc87332_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87332_398_ide_fdcon_device = {
    .name          = "National Semiconductor PC87332 Super I/O (Port 398h) (With IDE and FDC on)",
    .internal_name = "pc87332_398_ide_fdcon",
    .flags         = 0,
    .local         = 0x10101,
    .init          = pc87332_init,
    .close         = pc87332_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
