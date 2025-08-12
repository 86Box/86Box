/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the NatSemi PC87311 and PC87332 Super I/O chips.
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

typedef struct pc873xx_t {
    uint8_t   baddr;
    uint8_t   is_332;
    uint8_t   tries;
    uint8_t   has_ide;
    uint8_t   fdc_on;
    uint8_t   regs[256];
    uint16_t  base_addr;
    int       cur_reg;
    int       max_reg;
    fdc_t    *fdc;
    serial_t *uart[2];
    lpt_t    *lpt;
} pc873xx_t;

static void
lpt_handler(pc873xx_t *dev)
{
    int      temp;
    uint16_t lpt_port = LPT1_ADDR;
    uint8_t  lpt_irq = LPT2_IRQ;
    uint8_t  lpt_dma = ((dev->regs[0x18] & 0x06) >> 1);

    lpt_port_remove(dev->lpt);

    if (lpt_dma == 0x00)
        lpt_dma = 0xff;

    temp  = dev->regs[0x01] & 0x03;

    switch (temp) {
        case 0x00:
            lpt_port = LPT1_ADDR;
            lpt_irq  = (dev->regs[0x02] & 0x08) ? LPT1_IRQ : LPT2_IRQ;
            break;
        case 0x01:
            lpt_port = LPT_MDA_ADDR;
            lpt_irq = LPT_MDA_IRQ;
            break;
        case 0x02:
            lpt_port = LPT2_ADDR;
            lpt_irq  = LPT2_IRQ;
            break;
        case 0x03:
            lpt_port = 0x000;
            lpt_irq  = 0xff;
            break;

        default:
            break;
    }

    lpt_set_cnfgb_readout(dev->lpt, (lpt_irq == 5) ? 0x38 : 0x08);
    lpt_set_ext(dev->lpt, !!(dev->regs[0x02] & 0x80));

    if (dev->is_332) {
        lpt_set_epp(dev->lpt, !!(dev->regs[0x04] & 0x01));
        lpt_set_ecp(dev->lpt, !!(dev->regs[0x04] & 0x04));

        lpt_port_dma(dev->lpt, lpt_dma);
    }

    if (lpt_port)
        lpt_port_setup(dev->lpt, lpt_port);

    lpt_port_irq(dev->lpt, lpt_irq);
}

static void
serial_handler(pc873xx_t *dev, int uart)
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
ide_handler(pc873xx_t *dev)
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
pc873xx_write(uint16_t port, uint8_t val, void *priv)
{
    pc873xx_t *dev = (pc873xx_t *) priv;
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
            if ((dev->cur_reg <= dev->max_reg) && (dev->cur_reg != 8))
                dev->regs[dev->cur_reg] = val;
            else
                return;
        } else {
            dev->tries++;
            return;
        }
    }

    if (dev->cur_reg <= dev->max_reg)  switch (dev->cur_reg) {
        case 0x00:
            if (valxor & 0x01) {
                lpt_port_remove(dev->lpt);
                if ((val & 0x01) && !(dev->regs[0x02] & 0x01))
                    lpt_handler(dev);
            }
            if (valxor & 0x02) {
                serial_remove(dev->uart[0]);
                if ((val & 0x02) && !(dev->regs[0x02] & 0x01))
                    serial_handler(dev, 0);
            }
            if (valxor & 0x04) {
                serial_remove(dev->uart[1]);
                if ((val & 0x04) && !(dev->regs[0x02] & 0x01))
                    serial_handler(dev, 1);
            }
            if (valxor & 0x28) {
                fdc_remove(dev->fdc);
                if ((val & 0x08) && !(dev->regs[0x02] & 0x01))
                    fdc_set_base(dev->fdc, (val & 0x20) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
            }
            if (dev->has_ide && (valxor & 0xc0))
                ide_handler(dev);
            break;
        case 0x01:
            if (valxor & 0x03) {
                lpt_port_remove(dev->lpt);
                if ((dev->regs[0x00] & 0x01) && !(dev->regs[0x02] & 0x01))
                    lpt_handler(dev);
            }
            if (valxor & 0xcc) {
                serial_remove(dev->uart[0]);
                if ((dev->regs[0x00] & 0x02) && !(dev->regs[0x02] & 0x01))
                    serial_handler(dev, 0);
            }
            if (valxor & 0xf0) {
                serial_remove(dev->uart[1]);
                if ((dev->regs[0x00] & 0x04) && !(dev->regs[0x02] & 0x01))
                    serial_handler(dev, 1);
            }
            break;
        case 0x02:
            if (valxor & 0x01) {
                lpt_port_remove(dev->lpt);
                serial_remove(dev->uart[0]);
                serial_remove(dev->uart[1]);
                fdc_remove(dev->fdc);

                if (!(val & 0x01)) {
                    if (dev->regs[0x00] & 0x01)
                        lpt_handler(dev);
                    if (dev->regs[0x00] & 0x02)
                        serial_handler(dev, 0);
                    if (dev->regs[0x00] & 0x04)
                        serial_handler(dev, 1);
                    if (dev->regs[0x00] & 0x08)
                        fdc_set_base(dev->fdc, (dev->regs[0x00] & 0x20) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
                }
            }
            if (valxor & 0x88) {
                lpt_port_remove(dev->lpt);
                if ((dev->regs[0x00] & 0x01) && !(dev->regs[0x02] & 0x01))
                    lpt_handler(dev);
            }
            break;
        case 0x04:
            if (valxor & 0x05) {
                lpt_port_remove(dev->lpt);
                if ((dev->regs[0x00] & 0x01) && !(dev->regs[0x02] & 0x01))
                    lpt_handler(dev);
            }
            break;
        case 0x06:
            if (valxor & 0x08) {
                lpt_port_remove(dev->lpt);
                if ((dev->regs[0x00] & 0x01) && !(dev->regs[0x02] & 0x01))
                    lpt_handler(dev);
            }
            break;

        default:
            break;
    }
}

uint8_t
pc873xx_read(uint16_t port, void *priv)
{
    pc873xx_t *dev = (pc873xx_t *) priv;
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
pc873xx_reset(pc873xx_t *dev)
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
    lpt_port_remove(dev->lpt);
    lpt_handler(dev);
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
pc873xx_close(void *priv)
{
    pc873xx_t *dev = (pc873xx_t *) priv;

    free(dev);
}

static void *
pc873xx_init(const device_t *info)
{
    pc873xx_t *dev = (pc873xx_t *) calloc(1, sizeof(pc873xx_t));

    dev->fdc = device_add(&fdc_at_nsc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt = device_add_inst(&lpt_port_device, 1);
    lpt_set_cnfgb_readout(dev->lpt, 0x08);

    dev->is_332  = !!(info->local & PC87332);
    dev->max_reg = dev->is_332 ? 0x08 : 0x02;

    dev->has_ide = info->local & (PCX73XX_IDE_PRI | PCX73XX_IDE_SEC);
    dev->fdc_on  = info->local & PCX73XX_FDC_ON;

    dev->baddr   = (info->local & PCX730X_BADDR) >> PCX730X_BADDR_SHIFT;
    pc873xx_reset(dev);

    switch (dev->baddr) {
        default:
        case 0x00:
            dev->base_addr = 0x0398;
            break;
        case 0x01:
            dev->base_addr = 0x026e;
            break;
        case 0x02:
            dev->base_addr = 0x015c;
            break;
        case 0x03:
            /* Our PC87332 machine use this unless otherwise specified. */
            dev->base_addr = 0x002e;
            break;
    }

    io_sethandler(dev->base_addr, 0x0002,
                  pc873xx_read, NULL, NULL, pc873xx_write, NULL, NULL, dev);

    return dev;
}

const device_t pc873xx_device = {
    .name          = "National Semiconductor PC873xx Super I/O",
    .internal_name = "pc873xx",
    .flags         = 0,
    .local         = 0x00,
    .init          = pc873xx_init,
    .close         = pc873xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
