/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5572 USB controller.
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
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/smram.h>
#include <86box/spd.h>
#include <86box/apm.h>
#include <86box/ddma.h>
#include <86box/acpi.h>
#include <86box/smbus.h>
#include <86box/sis_55xx.h>
#include <86box/chipset.h>
#include <86box/usb.h>

#ifdef ENABLE_SIS_5572_USB_LOG
int sis_5572_usb_do_log = ENABLE_SIS_5572_USB_LOG;

static void
sis_5572_usb_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5572_usb_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5572_usb_log(fmt, ...)
#endif

typedef struct sis_5572_usb_t {
    uint8_t     rev;

    uint8_t     usb_unk_regs[256];
    uint8_t     pci_conf[256];

    uint16_t    usb_unk_base;

    usb_t      *usb;

    sis_55xx_common_t *sis;
} sis_5572_usb_t;

/* SiS 5572 unknown I/O port (second USB PCI BAR). */
static void
sis_5572_usb_unk_write(uint16_t addr, uint8_t val, void *priv)
{
    sis_5572_usb_t *dev = (sis_5572_usb_t *) priv;

    addr = (addr - dev->usb_unk_base) & 0x07;

    sis_5572_usb_log("SiS 5572 USB UNK: [W] dev->usb_unk_regs[%02X] = %02X\n", addr, val);

    dev->usb_unk_regs[addr] = val;
}

static uint8_t
sis_5572_usb_unk_read(uint16_t addr, void *priv)
{
    const sis_5572_usb_t *dev = (sis_5572_usb_t *) priv;
    uint8_t ret = 0xff;

    addr = (addr - dev->usb_unk_base) & 0x07;

    ret = dev->usb_unk_regs[addr & 0x07];

    sis_5572_usb_log("SiS 5572 USB UNK: [R] dev->usb_unk_regs[%02X] = %02X\n", addr, ret);

    return ret;
}

void
sis_5572_usb_write(int addr, uint8_t val, void *priv)
{
    sis_5572_usb_t *dev = (sis_5572_usb_t *) priv;

    sis_5572_usb_log("SiS 5572 USB: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    if (dev->sis->usb_enabled)  switch (addr) {
        default:
            break;

        case 0x04: /* Command - Low Byte */
            if (dev->rev == 0xb0)
                dev->pci_conf[addr] = val & 0x47;
            else
                dev->pci_conf[addr] = val & 0x57;
            if (dev->usb_unk_base != 0x0000) {
                io_removehandler(dev->usb_unk_base, 0x0002,
                                 sis_5572_usb_unk_read, NULL, NULL,
                                 sis_5572_usb_unk_write, NULL, NULL, dev);
                if (dev->pci_conf[0x04] & 0x01)
                    io_sethandler(dev->usb_unk_base, 0x0002,
                                  sis_5572_usb_unk_read, NULL, NULL,
                                  sis_5572_usb_unk_write, NULL, NULL, dev);
            }
            ohci_update_mem_mapping(dev->usb,
                                    dev->pci_conf[0x11], dev->pci_conf[0x12],
                                    dev->pci_conf[0x13], dev->pci_conf[0x04] & 0x02);
            break;

        case 0x05: /* Command - High Byte */
            dev->pci_conf[addr] = val & 0x01;
            break;

        case 0x07: /* Status - High Byte */
            dev->pci_conf[addr] &= ~(val & 0xf9);
            break;

        case 0x0d: /* Latency Timer */
            dev->pci_conf[addr] = val;
            break;

        case 0x11 ... 0x13: /* Memory Space Base Address Register */
            dev->pci_conf[addr] = val & ((addr == 0x11) ? 0xf0 : 0xff);
            ohci_update_mem_mapping(dev->usb,
                                    dev->pci_conf[0x11], dev->pci_conf[0x12],
                                    dev->pci_conf[0x13], dev->pci_conf[4] & 0x02);
            break;

        case 0x14 ... 0x15: /* IO Space Base Address Register */
            if (dev->rev == 0xb0) {
                if (dev->usb_unk_base != 0x0000) {
                    io_removehandler(dev->usb_unk_base, 0x0002,
                                     sis_5572_usb_unk_read, NULL, NULL,
                                     sis_5572_usb_unk_write, NULL, NULL, dev);
                }
                dev->pci_conf[addr] = val;
                dev->usb_unk_base = (dev->pci_conf[0x14] & 0xf8) |
                                    (dev->pci_conf[0x15] << 8);
                if (dev->usb_unk_base != 0x0000) {
                    io_sethandler(dev->usb_unk_base, 0x0002,
                                  sis_5572_usb_unk_read, NULL, NULL,
                                  sis_5572_usb_unk_write, NULL, NULL, dev);
                }
            }
            break;

        case 0x2c ... 0x2f:
            if (dev->rev == 0x11)
                dev->pci_conf[addr] = val;
            break;

        case 0x3c: /* Interrupt Line */
            dev->pci_conf[addr] = val;
            break;
    }
}

uint8_t
sis_5572_usb_read(int addr, void *priv)
{
    const sis_5572_usb_t *dev = (sis_5572_usb_t *) priv;
    uint8_t ret = 0xff;

    if (dev->sis->usb_enabled) {
        ret = dev->pci_conf[addr];

        sis_5572_usb_log("SiS 5572 USB: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);
    }

    return ret;
}

static void
sis_5572_usb_reset(void *priv)
{
    sis_5572_usb_t *dev = (sis_5572_usb_t *) priv;

    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x01;
    dev->pci_conf[0x03] = 0x70;
    dev->pci_conf[0x04] = dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = (dev->rev == 0xb0) ? 0x00 : 0x80;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = dev->rev;
    dev->pci_conf[0x09] = 0x10;
    dev->pci_conf[0x0a] = 0x03;
    dev->pci_conf[0x0b] = 0x0c;
    dev->pci_conf[0x0c] = dev->pci_conf[0x0d] = 0x00;
    dev->pci_conf[0x0e] = 0x80 /* 0x10  - Datasheet erratum - header type 0x10 is invalid! */;
    dev->pci_conf[0x0f] = 0x00;
    dev->pci_conf[0x10] = 0x00;
    dev->pci_conf[0x11] = 0x00;
    dev->pci_conf[0x12] = 0x00;
    dev->pci_conf[0x13] = 0x00;
    if (dev->rev == 0xb0) {
        dev->pci_conf[0x14] = 0x01;
        dev->pci_conf[0x15] = 0x00;
        dev->pci_conf[0x16] = 0x00;
        dev->pci_conf[0x17] = 0x00;
    } else if (dev->rev == 0x11) {
        dev->pci_conf[0x2c] = 0x00;
        dev->pci_conf[0x2d] = 0x00;
        dev->pci_conf[0x2e] = 0x00;
        dev->pci_conf[0x2f] = 0x00;
    }
    dev->pci_conf[0x3c] = 0x00;
    dev->pci_conf[0x3d] = PCI_INTA;
    dev->pci_conf[0x3e] = 0x00;
    dev->pci_conf[0x3f] = 0x00;

    if (dev->rev == 0xb0) {
        ohci_update_mem_mapping(dev->usb,
                                dev->pci_conf[0x11], dev->pci_conf[0x12],
                                dev->pci_conf[0x13], dev->pci_conf[0x04] & 0x02);

        if (dev->usb_unk_base != 0x0000) {
            io_removehandler(dev->usb_unk_base, 0x0002,
                             sis_5572_usb_unk_read, NULL, NULL,
                             sis_5572_usb_unk_write, NULL, NULL, dev);
        }

        dev->usb_unk_base = 0x0000;

        memset(dev->usb_unk_regs, 0x00, sizeof(dev->usb_unk_regs));
    }
}

static void
sis_5572_usb_close(void *priv)
{
    sis_5572_usb_t *dev = (sis_5572_usb_t *) priv;

    free(dev);
}

static void *
sis_5572_usb_init(UNUSED(const device_t *info))
{
    sis_5572_usb_t *dev = (sis_5572_usb_t *) calloc(1, sizeof(sis_5572_usb_t));

    dev->rev = info->local;

    dev->sis = device_get_common_priv();

    /* USB */
    dev->usb = device_add(&usb_device);

    sis_5572_usb_reset(dev);

    return dev;
}

const device_t sis_5572_usb_device = {
    .name          = "SiS 5572 USB controller",
    .internal_name = "sis_5572_usb",
    .flags         = DEVICE_PCI,
    .local         = 0xb0,
    .init          = sis_5572_usb_init,
    .close         = sis_5572_usb_close,
    .reset         = sis_5572_usb_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5582_usb_device = {
    .name          = "SiS 5582 USB controller",
    .internal_name = "sis_5582_usb",
    .flags         = DEVICE_PCI,
    .local         = 0xe0,
    .init          = sis_5572_usb_init,
    .close         = sis_5572_usb_close,
    .reset         = sis_5572_usb_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5595_usb_device = {
    .name          = "SiS 5595 USB controller",
    .internal_name = "sis_5595_usb",
    .flags         = DEVICE_PCI,
    .local         = 0x11,
    .init          = sis_5572_usb_init,
    .close         = sis_5572_usb_close,
    .reset         = sis_5572_usb_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
