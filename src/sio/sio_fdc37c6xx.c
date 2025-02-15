/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SMC FDC37C663 and FDC37C665 Super
 *          I/O Chips.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2020 Miran Grca.
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
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>

typedef struct fdc37c6xx_t {
    uint8_t   max_reg;
    uint8_t   chip_id;
    uint8_t   tries;
    uint8_t   has_ide;
    uint8_t   regs[16];
    int       cur_reg;
    int       com3_addr;
    int       com4_addr;
    fdc_t    *fdc;
    serial_t *uart[2];
} fdc37c6xx_t;

static void
set_com34_addr(fdc37c6xx_t *dev)
{
    switch (dev->regs[1] & 0x60) {
        case 0x00:
            dev->com3_addr = 0x338;
            dev->com4_addr = 0x238;
            break;
        case 0x20:
            dev->com3_addr = COM3_ADDR;
            dev->com4_addr = COM4_ADDR;
            break;
        case 0x40:
            dev->com3_addr = COM3_ADDR;
            dev->com4_addr = 0x2e0;
            break;
        case 0x60:
            dev->com3_addr = 0x220;
            dev->com4_addr = 0x228;
            break;

        default:
            break;
    }
}

static void
set_serial_addr(fdc37c6xx_t *dev, int port)
{
    uint8_t shift     = (port << 2);
    double  clock_src = 24000000.0 / 13.0;

    if (dev->regs[4] & (1 << (4 + port)))
        clock_src = 24000000.0 / 12.0;

    serial_remove(dev->uart[port]);
    if (dev->regs[2] & (4 << shift)) {
        switch ((dev->regs[2] >> shift) & 3) {
            case 0:
                serial_setup(dev->uart[port], COM1_ADDR, COM1_IRQ);
                break;
            case 1:
                serial_setup(dev->uart[port], COM2_ADDR, COM2_IRQ);
                break;
            case 2:
                serial_setup(dev->uart[port], dev->com3_addr, COM3_IRQ);
                break;
            case 3:
                serial_setup(dev->uart[port], dev->com4_addr, COM4_IRQ);
                break;

            default:
                break;
        }
    }

    serial_set_clock_src(dev->uart[port], clock_src);
}

static void
lpt1_handler(fdc37c6xx_t *dev)
{
    lpt1_remove();
    switch (dev->regs[1] & 3) {
        case 1:
            lpt1_setup(LPT_MDA_ADDR);
            lpt1_irq(LPT_MDA_IRQ);
            break;
        case 2:
            lpt1_setup(LPT1_ADDR);
            lpt1_irq(LPT1_IRQ /*LPT2_IRQ*/);
            break;
        case 3:
            lpt1_setup(LPT2_ADDR);
            lpt1_irq(LPT1_IRQ /*LPT2_IRQ*/);
            break;

        default:
            break;
    }
}

static void
fdc_handler(fdc37c6xx_t *dev)
{
    fdc_remove(dev->fdc);
    if (dev->regs[0] & 0x10)
        fdc_set_base(dev->fdc, (dev->regs[5] & 0x01) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
}

static void
ide_handler(fdc37c6xx_t *dev)
{
    /* TODO: Make an ide_disable(channel) and ide_enable(channel) so we can simplify this. */
    if (dev->has_ide == 2) {
        ide_sec_disable();
        ide_set_base(1, (dev->regs[0x05] & 0x02) ? 0x170 : 0x1f0);
        ide_set_side(1, (dev->regs[0x05] & 0x02) ? 0x376 : 0x3f6);
        if (dev->regs[0x00] & 0x01)
            ide_sec_enable();
    } else if (dev->has_ide == 1) {
        ide_pri_disable();
        ide_set_base(0, (dev->regs[0x05] & 0x02) ? 0x170 : 0x1f0);
        ide_set_side(0, (dev->regs[0x05] & 0x02) ? 0x376 : 0x3f6);
        if (dev->regs[0x00] & 0x01)
            ide_pri_enable();
    }
}

static void
fdc37c6xx_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c6xx_t *dev    = (fdc37c6xx_t *) priv;
    uint8_t      valxor = 0;

    if (dev->tries == 2) {
        if (port == FDC_PRIMARY_ADDR) {
            if (val == 0xaa)
                dev->tries = 0;
            else
                dev->cur_reg = val;
        } else {
            if (dev->cur_reg > dev->max_reg)
                return;

            valxor                  = val ^ dev->regs[dev->cur_reg];
            dev->regs[dev->cur_reg] = val;

            switch (dev->cur_reg) {
                case 0:
                    if (dev->has_ide && (valxor & 0x01))
                        ide_handler(dev);
                    if (valxor & 0x10)
                        fdc_handler(dev);
                    break;
                case 1:
                    if (valxor & 3)
                        lpt1_handler(dev);
                    if (valxor & 0x60) {
                        set_com34_addr(dev);
                        set_serial_addr(dev, 0);
                        set_serial_addr(dev, 1);
                    }
                    break;
                case 2:
                    if (valxor & 7)
                        set_serial_addr(dev, 0);
                    if (valxor & 0x70)
                        set_serial_addr(dev, 1);
                    break;
                case 3:
                    if (valxor & 2)
                        fdc_update_enh_mode(dev->fdc, (dev->regs[3] & 2) ? 1 : 0);
                    break;
                case 4:
                    if (valxor & 0x10)
                        set_serial_addr(dev, 0);
                    if (valxor & 0x20)
                        set_serial_addr(dev, 1);
                    break;
                case 5:
                    if (valxor & 0x01)
                        fdc_handler(dev);
                    if (dev->has_ide && (valxor & 0x02))
                        ide_handler(dev);
                    if (valxor & 0x18)
                        fdc_update_densel_force(dev->fdc, (dev->regs[5] & 0x18) >> 3);
                    if (valxor & 0x20)
                        fdc_set_swap(dev->fdc, (dev->regs[5] & 0x20) >> 5);
                    break;

                default:
                    break;
            }
        }
    } else if ((port == FDC_PRIMARY_ADDR) && (val == 0x55))
        dev->tries++;
}

static uint8_t
fdc37c6xx_read(uint16_t port, void *priv)
{
    const fdc37c6xx_t *dev = (fdc37c6xx_t *) priv;
    uint8_t            ret = 0xff;

    if (dev->tries == 2) {
        if (port == 0x3f1)
            ret = dev->regs[dev->cur_reg];
    }

    return ret;
}

static void
fdc37c6xx_reset(fdc37c6xx_t *dev)
{
    dev->com3_addr = 0x338;
    dev->com4_addr = 0x238;

    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);

    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

    lpt1_remove();
    lpt1_setup(LPT1_ADDR);

    fdc_reset(dev->fdc);
    fdc_remove(dev->fdc);

    dev->tries = 0;
    memset(dev->regs, 0, 16);

    switch (dev->chip_id) {
        case 0x63:
        case 0x65:
            dev->max_reg   = 0x0f;
            dev->regs[0x0] = 0x3b;
            break;
        case 0x64:
        case 0x66:
            dev->max_reg   = 0x0f;
            dev->regs[0x0] = 0x2b;
            break;
        default:
            dev->max_reg   = (dev->chip_id >= 0x61) ? 0x03 : 0x02;
            dev->regs[0x0] = 0x3f;
            break;
    }

    dev->regs[0x1] = 0x9f;
    dev->regs[0x2] = 0xdc;
    dev->regs[0x3] = 0x78;

    if (dev->chip_id >= 0x63) {
        dev->regs[0x6] = 0xff;
        dev->regs[0xd] = dev->chip_id;
        if (dev->chip_id >= 0x65)
            dev->regs[0xe] = 0x02;
        else
            dev->regs[0xe] = 0x01;
    }

    set_serial_addr(dev, 0);
    set_serial_addr(dev, 1);

    lpt1_handler(dev);

    fdc_handler(dev);

    if (dev->has_ide)
        ide_handler(dev);
}

static void
fdc37c6xx_close(void *priv)
{
    fdc37c6xx_t *dev = (fdc37c6xx_t *) priv;

    free(dev);
}

static void *
fdc37c6xx_init(const device_t *info)
{
    fdc37c6xx_t *dev = (fdc37c6xx_t *) calloc(1, sizeof(fdc37c6xx_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->chip_id = info->local & 0xff;
    dev->has_ide = (info->local >> 8) & 0xff;

    if (dev->chip_id >= 0x63) {
        dev->uart[0] = device_add_inst(&ns16550_device, 1);
        dev->uart[1] = device_add_inst(&ns16550_device, 2);
    } else {
        dev->uart[0] = device_add_inst(&ns16450_device, 1);
        dev->uart[1] = device_add_inst(&ns16450_device, 2);
    }

    io_sethandler(FDC_PRIMARY_ADDR, 0x0002,
                  fdc37c6xx_read, NULL, NULL, fdc37c6xx_write, NULL, NULL, dev);

    fdc37c6xx_reset(dev);

    return dev;
}

/* The three appear to differ only in the chip ID, if I
   understood their datasheets correctly. */
const device_t fdc37c651_device = {
    .name          = "SMC FDC37C651 Super I/O",
    .internal_name = "fdc37c651",
    .flags         = 0,
    .local         = 0x51,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c651_ide_device = {
    .name          = "SMC FDC37C651 Super I/O (With IDE)",
    .internal_name = "fdc37c651_ide",
    .flags         = 0,
    .local         = 0x151,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c661_device = {
    .name          = "SMC FDC37C661 Super I/O",
    .internal_name = "fdc37c661",
    .flags         = 0,
    .local         = 0x61,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c661_ide_device = {
    .name          = "SMC FDC37C661 Super I/O (With IDE)",
    .internal_name = "fdc37c661_ide",
    .flags         = 0,
    .local         = 0x161,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c661_ide_sec_device = {
    .name          = "SMC FDC37C661 Super I/O (With Secondary IDE)",
    .internal_name = "fdc37c661_ide_sec",
    .flags         = 0,
    .local         = 0x261,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c663_device = {
    .name          = "SMC FDC37C663 Super I/O",
    .internal_name = "fdc37c663",
    .flags         = 0,
    .local         = 0x63,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c663_ide_device = {
    .name          = "SMC FDC37C663 Super I/O (With IDE)",
    .internal_name = "fdc37c663_ide",
    .flags         = 0,
    .local         = 0x163,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c665_device = {
    .name          = "SMC FDC37C665 Super I/O",
    .internal_name = "fdc37c665",
    .flags         = 0,
    .local         = 0x65,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c665_ide_device = {
    .name          = "SMC FDC37C665 Super I/O (With IDE)",
    .internal_name = "fdc37c665_ide",
    .flags         = 0,
    .local         = 0x265,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c665_ide_pri_device = {
    .name          = "SMC FDC37C665 Super I/O (With Primary IDE)",
    .internal_name = "fdc37c665_ide_pri",
    .flags         = 0,
    .local         = 0x165,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c665_ide_sec_device = {
    .name          = "SMC FDC37C665 Super I/O (With Secondary IDE)",
    .internal_name = "fdc37c665_ide_sec",
    .flags         = 0,
    .local         = 0x265,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c666_device = {
    .name          = "SMC FDC37C666 Super I/O",
    .internal_name = "fdc37c666",
    .flags         = 0,
    .local         = 0x66,
    .init          = fdc37c6xx_init,
    .close         = fdc37c6xx_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
