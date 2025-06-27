/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5513 IDE controller.
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

#ifdef ENABLE_SIS_5513_IDE_LOG
int sis_5513_ide_do_log = ENABLE_SIS_5513_IDE_LOG;

static void
sis_5513_ide_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5513_ide_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5513_ide_log(fmt, ...)
#endif

typedef struct sis_5513_ide_t {
    uint8_t            rev;

    uint8_t            pci_conf[256];

    sis_55xx_common_t *sis;
} sis_5513_ide_t;

static void
sis_5513_ide_irq_handler(sis_5513_ide_t *dev)
{
    if (dev->pci_conf[0x09] & 0x01) {
        /* Primary IDE is native. */
        sis_5513_ide_log("Primary IDE IRQ mode: Native, Native\n");
        sff_set_irq_mode(dev->sis->bm[0], IRQ_MODE_SIS_551X);
    } else {
        /* Primary IDE is legacy. */
        sis_5513_ide_log("Primary IDE IRQ mode: IRQ14, IRQ15\n");
        sff_set_irq_mode(dev->sis->bm[0], IRQ_MODE_LEGACY);
    }

    if (dev->pci_conf[0x09] & 0x04) {
        /* Secondary IDE is native. */
        sis_5513_ide_log("Secondary IDE IRQ mode: Native, Native\n");
        sff_set_irq_mode(dev->sis->bm[1], IRQ_MODE_SIS_551X);
    } else {
        /* Secondary IDE is legacy. */
        sis_5513_ide_log("Secondary IDE IRQ mode: IRQ14, IRQ15\n");
        sff_set_irq_mode(dev->sis->bm[1], IRQ_MODE_LEGACY);
    }
}

static void
sis_5513_ide_handler(sis_5513_ide_t *dev)
{
    uint8_t ide_io_on = dev->pci_conf[0x04] & 0x01;

    uint16_t native_base_pri_addr = (dev->pci_conf[0x11] | dev->pci_conf[0x10] << 8) & 0xfffe;
    uint16_t native_side_pri_addr = (dev->pci_conf[0x15] | dev->pci_conf[0x14] << 8) & 0xfffe;
    uint16_t native_base_sec_addr = (dev->pci_conf[0x19] | dev->pci_conf[0x18] << 8) & 0xfffe;
    uint16_t native_side_sec_addr = (dev->pci_conf[0x1c] | dev->pci_conf[0x1b] << 8) & 0xfffe;

    uint16_t current_pri_base;
    uint16_t current_pri_side;
    uint16_t current_sec_base;
    uint16_t current_sec_side;

    /* Primary Channel Programming */
    current_pri_base = (!(dev->pci_conf[0x09] & 1)) ? 0x01f0 : native_base_pri_addr;
    current_pri_side = (!(dev->pci_conf[0x09] & 1)) ? 0x03f6 : native_side_pri_addr;

    /* Secondary Channel Programming */
    current_sec_base = (!(dev->pci_conf[0x09] & 4)) ? 0x0170 : native_base_sec_addr;
    current_sec_side = (!(dev->pci_conf[0x09] & 4)) ? 0x0376 : native_side_sec_addr;

    sis_5513_ide_log("sis_5513_ide_handler(): Disabling primary IDE...\n");
    ide_pri_disable();
    sis_5513_ide_log("sis_5513_ide_handler(): Disabling secondary IDE...\n");
    ide_sec_disable();

    if (ide_io_on) {
        /* Primary Channel Setup */
        if (dev->pci_conf[0x4a] & 0x02) {
            sis_5513_ide_log("sis_5513_ide_handler(): Primary IDE base now %04X...\n", current_pri_base);
            ide_set_base(0, current_pri_base);
            sis_5513_ide_log("sis_5513_ide_handler(): Primary IDE side now %04X...\n", current_pri_side);
            ide_set_side(0, current_pri_side);

            sis_5513_ide_log("sis_5513_ide_handler(): Enabling primary IDE...\n");
            ide_pri_enable();

            sis_5513_ide_log("SiS 5513 PRI: BASE %04x SIDE %04x\n", current_pri_base, current_pri_side);
        }

        /* Secondary Channel Setup */
        if (dev->pci_conf[0x4a] & 0x04) {
            sis_5513_ide_log("sis_5513_ide_handler(): Secondary IDE base now %04X...\n", current_sec_base);
            ide_set_base(1, current_sec_base);
            sis_5513_ide_log("sis_5513_ide_handler(): Secondary IDE side now %04X...\n", current_sec_side);
            ide_set_side(1, current_sec_side);

            sis_5513_ide_log("sis_5513_ide_handler(): Enabling secondary IDE...\n");
            ide_sec_enable();

            sis_5513_ide_log("SiS 5513: BASE %04x SIDE %04x\n", current_sec_base, current_sec_side);
        }
    }

    sff_bus_master_handler(dev->sis->bm[0], ide_io_on,
                           ((dev->pci_conf[0x20] & 0xf0) | (dev->pci_conf[0x21] << 8)) + 0);
    sff_bus_master_handler(dev->sis->bm[1], ide_io_on,
                           ((dev->pci_conf[0x20] & 0xf0) | (dev->pci_conf[0x21] << 8)) + 8);
}

void
sis_5513_ide_write(int addr, uint8_t val, void *priv)
{
    sis_5513_ide_t *dev = (sis_5513_ide_t *) priv;

    sis_5513_ide_log("SiS 5513 IDE: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    switch (addr) {
        case 0x04: /* Command low byte */
            dev->pci_conf[addr] = val & 0x05;
            sis_5513_ide_handler(dev);
            break;
        case 0x06: /* Status low byte */
            dev->pci_conf[addr] = val & 0x20;
            break;
        case 0x07: /* Status high byte */
            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x06) & ~(val & 0x38);
            break;
        case 0x09: /* Programming Interface Byte */
            switch (dev->rev) {
                case 0xd0:
                    if (dev->sis->ide_bits_1_3_writable)
                        val |= 0x0a;
                    fallthrough;
                case 0x00:
                case 0xd1:
                    val &= 0xbf;
                    fallthrough;
                case 0xc0:
                    switch (val & 0x0a) {
                        case 0x00:
                            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x85) | (val & 0x4a);
                            break;
                        case 0x02:
                            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x84) | (val & 0x4b);
                            break;
                        case 0x08:
                            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x81) | (val & 0x4e);
                            break;
                        case 0x0a:
                            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x80) | (val & 0x4f);
                            break;
                    }
                    break;
            }
            sis_5513_ide_irq_handler(dev);
            sis_5513_ide_handler(dev);
            break;
        case 0x0d: /* Latency Timer */
            dev->pci_conf[addr] = val;
            break;

        /* Primary Base Address */
        case 0x10 ... 0x11:
        case 0x14 ... 0x15:
            fallthrough;

        /* Secondary Base Address */
        case 0x18 ... 0x19:
        case 0x1c ... 0x1d:
            fallthrough;

        /* Bus Mastering Base Address */
        case 0x20 ... 0x21:
            if (addr == 0x20)
                dev->pci_conf[addr] = (val & 0xe0) | 0x01;
            else if ((addr & 0x07) == 0x00)
                dev->pci_conf[addr] = (val & 0xf8) | 0x01;
            else if ((addr & 0x07) == 0x04)
                dev->pci_conf[addr] = (val & 0xfc) | 0x01;
            else
                dev->pci_conf[addr] = val;
            sis_5513_ide_handler(dev);
            break;

        case 0x2c ... 0x2f:
            if (dev->rev >= 0xd0)
                dev->pci_conf[addr] = val;
            break;

        case 0x30 ... 0x33: /* Expansion ROM Base Address */
#ifdef DATASHEET
            dev->pci_conf[addr] = val;
#else
            if (dev->rev == 0x00)
                dev->pci_conf[addr] = val;
#endif
            break;

        case 0x40: /* IDE Primary Channel/Master Drive Data Recovery Time Control */
            if (dev->rev >= 0xd0)
                dev->pci_conf[addr] = val & 0xcf;
            else
                dev->pci_conf[addr] = val & 0x0f;
            break;

        case 0x42: /* IDE Primary Channel/Slave Drive Data Recovery Time Control */
        case 0x44: /* IDE Secondary Channel/Master Drive Data Recovery Time Control */
        case 0x46: /* IDE Secondary Channel/Slave Drive Data Recovery Time Control */
        case 0x48: /* IDE Command Recovery Time Control */
            dev->pci_conf[addr] = val & 0x0f;
            break;

        case 0x41: /* IDE Primary Channel/Master Drive DataActive Time Control */
        case 0x43: /* IDE Primary Channel/Slave Drive Data Active Time Control */
        case 0x45: /* IDE Secondary Channel/Master Drive Data Active Time Control */
        case 0x47: /* IDE Secondary Channel/Slave Drive Data Active Time Control */
            if (dev->rev >= 0xd0)
                dev->pci_conf[addr] = val & 0xe7;
            else
                dev->pci_conf[addr] = val & 0x07;
            break;

        case 0x49: /* IDE Command Active Time Control */
            dev->pci_conf[addr] = val & 0x07;
            break;

        case 0x4a: /* IDE General Control Register 0 */
            switch (dev->rev) {
                case 0x00:
                   dev->pci_conf[addr] = val & 0x9e;
                   break;
                case 0xc0:
                   dev->pci_conf[addr] = val & 0xaf;
                   break;
                case 0xd0:
                   dev->pci_conf[addr] = val;
                   break;
            }
            sis_5513_ide_handler(dev);
            break;

        case 0x4b: /* IDE General Control Register 1 */
            if (dev->rev >= 0xc0)
                dev->pci_conf[addr] = val;
            else
                dev->pci_conf[addr] = val & 0xef;
            break;

        case 0x4c: /* Prefetch Count of Primary Channel (Low Byte) */
        case 0x4d: /* Prefetch Count of Primary Channel (High Byte) */
        case 0x4e: /* Prefetch Count of Secondary Channel (Low Byte) */
        case 0x4f: /* Prefetch Count of Secondary Channel (High Byte) */
            dev->pci_conf[addr] = val;
            break;

        case 0x50:
        case 0x51:
            if (dev->rev >= 0xd0)
                dev->pci_conf[addr] = val;
            break;

        case 0x52:
            if (dev->rev >= 0xd0)
                dev->pci_conf[addr] = val & 0x0f;
            break;

        default:
            break;
    }
}

uint8_t
sis_5513_ide_read(int addr, void *priv)
{
    const sis_5513_ide_t *dev = (sis_5513_ide_t *) priv;
    uint8_t ret = 0xff;

    switch (addr) {
        default:
            ret = dev->pci_conf[addr];
            break;
        case 0x09:
            ret = dev->pci_conf[addr];
            if (dev->rev >= 0xc0) {
                if (dev->pci_conf[0x09] & 0x40)
                    ret |= ((dev->pci_conf[0x4a] & 0x06) << 3);
                if ((dev->rev == 0xd0) && dev->sis->ide_bits_1_3_writable)
                    ret |= 0x0a;
            }
            break;
        case 0x3d:
            if (dev->rev >= 0xc0)
                ret = (dev->pci_conf[0x09] & 0x05) ? PCI_INTA : 0x00;
            else
                ret = (((dev->pci_conf[0x4b] & 0xc0) == 0xc0) ||
                       (dev->pci_conf[0x09] & 0x05)) ? PCI_INTA : 0x00;
            break;
    }

    sis_5513_ide_log("SiS 5513 IDE: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5513_ide_reset(void *priv)
{
    sis_5513_ide_t *dev = (sis_5513_ide_t *) priv;

    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x13;
    dev->pci_conf[0x03] = 0x55;
    dev->pci_conf[0x04] = dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = dev->pci_conf[0x07] = 0x00;
    dev->pci_conf[0x08] = (dev->rev == 0xd1) ? 0xd0 : dev->rev;
    dev->pci_conf[0x09] = 0x8a;
    dev->pci_conf[0x0a] = dev->pci_conf[0x0b] = 0x01;
    dev->pci_conf[0x0c] = dev->pci_conf[0x0d] = 0x00;
    dev->pci_conf[0x0e] = 0x80;
    dev->pci_conf[0x0f] = 0x00;
    dev->pci_conf[0x10] = 0xf1;
    dev->pci_conf[0x11] = 0x01;
    dev->pci_conf[0x14] = 0xf5;
    dev->pci_conf[0x15] = 0x03;
    dev->pci_conf[0x18] = 0x71;
    dev->pci_conf[0x19] = 0x01;
    dev->pci_conf[0x1c] = 0x75;
    dev->pci_conf[0x1d] = 0x03;
    dev->pci_conf[0x20] = 0x01;
    dev->pci_conf[0x21] = 0xf0;
    dev->pci_conf[0x22] = dev->pci_conf[0x23] = 0x00;
    dev->pci_conf[0x24] = dev->pci_conf[0x25] = 0x00;
    dev->pci_conf[0x26] = dev->pci_conf[0x27] = 0x00;
    dev->pci_conf[0x28] = dev->pci_conf[0x29] = 0x00;
    dev->pci_conf[0x2a] = dev->pci_conf[0x2b] = 0x00;
    switch (dev->rev) {
        case 0x00:
        case 0xd0:
        case 0xd1:
            dev->pci_conf[0x2c] = dev->pci_conf[0x2d] = 0x00;
            break;
        case 0xc0:
#ifdef DATASHEET
            dev->pci_conf[0x2c] = dev->pci_conf[0x2d] = 0x00;
#else
            /* The only Linux lspci listing I could find of this chipset,
               shows a subsystem of 0058:0000. */
            dev->pci_conf[0x2c] = 0x58;
            dev->pci_conf[0x2d] = 0x00;
#endif
            break;
    }
    dev->pci_conf[0x2e] = dev->pci_conf[0x2f] = 0x00;
    dev->pci_conf[0x30] = dev->pci_conf[0x31] = 0x00;
    dev->pci_conf[0x32] = dev->pci_conf[0x33] = 0x00;
    dev->pci_conf[0x40] = dev->pci_conf[0x41] = 0x00;
    dev->pci_conf[0x42] = dev->pci_conf[0x43] = 0x00;
    dev->pci_conf[0x44] = dev->pci_conf[0x45] = 0x00;
    dev->pci_conf[0x46] = dev->pci_conf[0x47] = 0x00;
    dev->pci_conf[0x48] = dev->pci_conf[0x49] = 0x00;
    dev->pci_conf[0x4a] = 0x06;
    dev->pci_conf[0x4b] = 0x00;
    dev->pci_conf[0x4c] = dev->pci_conf[0x4d] = 0x00;
    dev->pci_conf[0x4e] = dev->pci_conf[0x4f] = 0x00;

    sis_5513_ide_irq_handler(dev);
    sis_5513_ide_handler(dev);

    sff_bus_master_reset(dev->sis->bm[0]);
    sff_bus_master_reset(dev->sis->bm[1]);
}

static void
sis_5513_ide_close(void *priv)
{
    sis_5513_ide_t *dev = (sis_5513_ide_t *) priv;

    free(dev);
}

static void *
sis_5513_ide_init(UNUSED(const device_t *info))
{
    sis_5513_ide_t *dev = (sis_5513_ide_t *) calloc(1, sizeof(sis_5513_ide_t));

    dev->rev = info->local;

    dev->sis = device_get_common_priv();

    /* SFF IDE */
    dev->sis->bm[0] = device_add_inst(&sff8038i_device, 1);
    dev->sis->bm[1] = device_add_inst(&sff8038i_device, 2);

    sis_5513_ide_reset(dev);

    return dev;
}

const device_t sis_5513_ide_device = {
    .name          = "SiS 5513 IDE controller",
    .internal_name = "sis_5513_ide",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = sis_5513_ide_init,
    .close         = sis_5513_ide_close,
    .reset         = sis_5513_ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5572_ide_device = {
    .name          = "SiS 5572 IDE controller",
    .internal_name = "sis_5572_ide",
    .flags         = DEVICE_PCI,
    .local         = 0xc0,
    .init          = sis_5513_ide_init,
    .close         = sis_5513_ide_close,
    .reset         = sis_5513_ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5582_ide_device = {
    .name          = "SiS 5582 IDE controller",
    .internal_name = "sis_5582_ide",
    .flags         = DEVICE_PCI,
    .local         = 0xd0,
    .init          = sis_5513_ide_init,
    .close         = sis_5513_ide_close,
    .reset         = sis_5513_ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5591_5600_ide_device = {
    .name          = "SiS 5591/(5)600 IDE controller",
    .internal_name = "sis_5591_5600_ide",
    .flags         = DEVICE_PCI,
    .local         = 0xd1,    /* D0, but we need to distinguish them. */
    .init          = sis_5513_ide_init,
    .close         = sis_5513_ide_close,
    .reset         = sis_5513_ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
