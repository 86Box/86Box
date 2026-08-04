/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the HighPoint HPT366 IDE controller.
 *
 * Authors: James Weidner, <jamesr@theweidners.us>
 *
 *          Copyright 2026 James Weidner.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>

typedef struct hpt366_t {
    uint8_t     pci_slot;
    uint8_t     regs[2][256];
    sff8038i_t *bm[2];
} hpt366_t;

#ifdef ENABLE_HPT366_LOG
int hpt366_do_log = ENABLE_HPT366_LOG;

static void
hpt366_log(const char *fmt, ...)
{
    va_list ap;

    if (hpt366_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define hpt366_log(fmt, ...)
#endif

static void
hpt366_ide_handler(hpt366_t *dev, int func)
{
    const int channel = func + 2;
    uint16_t  main;
    uint16_t  side;

    ide_handlers(channel, 0);

    main = (dev->regs[func][0x11] << 8) | (dev->regs[func][0x10] & 0xf8);
    side = ((dev->regs[func][0x15] << 8) | (dev->regs[func][0x14] & 0xfc)) + 2;

    ide_set_base(channel, main);
    ide_set_side(channel, side);

    if (hdc_onboard_enabled && (dev->regs[func][0x04] & 0x01) && main && side)
        ide_handlers(channel, 1);

    hpt366_log("HPT366 function %i: IDE %i at %04X/%04X\n", func, channel, main, side);
}

static void
hpt366_bm_handler(hpt366_t *dev, int func)
{
    const uint16_t base = (dev->regs[func][0x20] & 0xf0) | (dev->regs[func][0x21] << 8);
    const int      enabled = hdc_onboard_enabled && ((dev->regs[func][0x04] & 0x05) == 0x05);

    sff_bus_master_handler(dev->bm[func], enabled, base);
}

static void
hpt366_set_irq_0(uint8_t status, void *priv)
{
    hpt366_t *dev = (hpt366_t *) priv;

    sff_bus_master_set_irq(status, dev->bm[0]);
}

static void
hpt366_set_irq_1(uint8_t status, void *priv)
{
    hpt366_t *dev = (hpt366_t *) priv;

    sff_bus_master_set_irq(status, dev->bm[1]);
}

static int
hpt366_bus_master_dma_0(uint8_t *data, int transfer_length, int total_length, int out, void *priv)
{
    hpt366_t *dev = (hpt366_t *) priv;

    return sff_bus_master_dma(data, transfer_length, total_length, out, dev->bm[0]);
}

static int
hpt366_bus_master_dma_1(uint8_t *data, int transfer_length, int total_length, int out, void *priv)
{
    hpt366_t *dev = (hpt366_t *) priv;

    return sff_bus_master_dma(data, transfer_length, total_length, out, dev->bm[1]);
}

static void
hpt366_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    hpt366_t *dev = (hpt366_t *) priv;

    if (func > 1)
        return;

    switch (addr) {
        case 0x04:
            dev->regs[func][addr] = val & 0x45;
            hpt366_ide_handler(dev, func);
            hpt366_bm_handler(dev, func);
            break;
        case 0x05:
            dev->regs[func][addr] = val & 0x01;
            break;
        case 0x07:
            dev->regs[func][addr] &= ~(val & 0xb8);
            break;
        case 0x0d:
            dev->regs[func][addr] = val;
            break;
        case 0x10:
            dev->regs[func][addr] = (val & 0xf8) | 0x01;
            hpt366_ide_handler(dev, func);
            break;
        case 0x11 ... 0x13:
            dev->regs[func][addr] = val;
            hpt366_ide_handler(dev, func);
            break;
        case 0x14:
            dev->regs[func][addr] = (val & 0xfc) | 0x01;
            hpt366_ide_handler(dev, func);
            break;
        case 0x15 ... 0x17:
            dev->regs[func][addr] = val;
            hpt366_ide_handler(dev, func);
            break;
        case 0x20:
            dev->regs[func][addr] = (val & 0xf0) | 0x01;
            hpt366_bm_handler(dev, func);
            break;
        case 0x21 ... 0x23:
            dev->regs[func][addr] = val;
            hpt366_bm_handler(dev, func);
            break;
        case 0x3c:
            dev->regs[func][addr] = val;
            break;
        case 0x40 ... 0xff:
            dev->regs[func][addr] = val;
            break;
        default:
            break;
    }
}

static uint8_t
hpt366_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    hpt366_t *dev = (hpt366_t *) priv;

    if (func > 1)
        return 0xff;

    return dev->regs[func][addr];
}

static void
hpt366_reset(void *priv)
{
    hpt366_t *dev = (hpt366_t *) priv;

    for (int func = 0; func < 2; func++) {
        sff_bus_master_reset(dev->bm[func]);
        memset(dev->regs[func], 0x00, sizeof(dev->regs[func]));

        dev->regs[func][0x00] = 0x03; /* HighPoint Technologies */
        dev->regs[func][0x01] = 0x11;
        dev->regs[func][0x02] = 0x04; /* HPT366 */
        dev->regs[func][0x03] = 0x00;
        dev->regs[func][0x07] = 0x02;
        dev->regs[func][0x08] = 0x01;
        dev->regs[func][0x09] = 0x85; /* Native-mode, bus-master IDE */
        dev->regs[func][0x0a] = 0x01;
        dev->regs[func][0x0b] = 0x01;
        dev->regs[func][0x0e] = func ? 0x00 : 0x80;
        dev->regs[func][0x10] = 0x01;
        dev->regs[func][0x14] = 0x01;
        dev->regs[func][0x20] = 0x01;
        dev->regs[func][0x2c] = 0x03;
        dev->regs[func][0x2d] = 0x11;
        dev->regs[func][0x2e] = 0x04;
        dev->regs[func][0x2f] = 0x00;
        dev->regs[func][0x3d] = PCI_INTA;

        sff_set_slot(dev->bm[func], dev->pci_slot);
        sff_set_irq_pin(dev->bm[func], PCI_INTA);
        sff_set_irq_mode(dev->bm[func], IRQ_MODE_PCI_IRQ_PIN);

        hpt366_ide_handler(dev, func);
        hpt366_bm_handler(dev, func);
    }
}

static void
hpt366_close(void *priv)
{
    hpt366_t *dev = (hpt366_t *) priv;

    free(dev);
}

static void *
hpt366_init(UNUSED(const device_t *info))
{
    hpt366_t *dev = (hpt366_t *) calloc(1, sizeof(hpt366_t));

    device_add(&ide_pci_ter_qua_2ch_device);
    pci_add_card(PCI_ADD_IDE, hpt366_pci_read, hpt366_pci_write, dev, &dev->pci_slot);

    dev->bm[0] = device_add_inst(&sff8038i_device, 3);
    dev->bm[1] = device_add_inst(&sff8038i_device, 4);

    ide_set_bus_master(2, hpt366_bus_master_dma_0, hpt366_set_irq_0, dev);
    ide_set_bus_master(3, hpt366_bus_master_dma_1, hpt366_set_irq_1, dev);

    hpt366_reset(dev);

    return dev;
}

const device_t ide_hpt366_ter_qua_onboard_device = {
    .name          = "HighPoint HPT366 (Tertiary and Quaternary) On-Board",
    .internal_name = "ide_hpt366_ter_qua_onboard",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = hpt366_init,
    .close         = hpt366_close,
    .reset         = hpt366_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
