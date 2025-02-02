/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of PCI-PCI and host-AGP bridges.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2020 RichardG.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/machine.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/pci.h>

#define PCI_BRIDGE_DEC_21150   0x10110022
#define AGP_BRIDGE_ALI_M5243   0x10b95243
#define AGP_BRIDGE_ALI_M5247   0x10b95247
#define AGP_BRIDGE_INTEL_440LX 0x80867181
#define AGP_BRIDGE_INTEL_440BX 0x80867191
#define AGP_BRIDGE_INTEL_440GX 0x808671a1
#define AGP_BRIDGE_VIA_597     0x11068597
#define AGP_BRIDGE_VIA_598     0x11068598
#define AGP_BRIDGE_VIA_691     0x11068691
#define AGP_BRIDGE_VIA_8601    0x11068601
#define AGP_BRIDGE_SIS_5XXX    0x10390001

#define AGP_BRIDGE_ALI(x)      (((x) >> 16) == 0x10b9)
#define AGP_BRIDGE_INTEL(x)    (((x) >> 16) == 0x8086)
#define AGP_BRIDGE_VIA(x)      (((x) >> 16) == 0x1106)
#define AGP_BRIDGE_SIS(x)      (((x) >> 16) == 0x1039)
#define AGP_BRIDGE(x)          ((x) >= AGP_BRIDGE_SIS_5XXX)

typedef struct pci_bridge_t {
    uint32_t local;
    uint8_t  type;
    uint8_t  ctl;

    uint8_t regs[256];
    uint8_t bus_index;
    uint8_t slot;
} pci_bridge_t;

#ifdef ENABLE_PCI_BRIDGE_LOG
int pci_bridge_do_log = ENABLE_PCI_BRIDGE_LOG;

static void
pci_bridge_log(const char *fmt, ...)
{
    va_list ap;

    if (pci_bridge_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pci_bridge_log(fmt, ...)
#endif

void
pci_bridge_set_ctl(void *priv, uint8_t ctl)
{
    pci_bridge_t *dev = (pci_bridge_t *) priv;

    dev->ctl = ctl;
}

static void
pci_bridge_write(int func, int addr, uint8_t val, void *priv)
{
    pci_bridge_t *dev = (pci_bridge_t *) priv;

    pci_bridge_log("PCI Bridge %d: write(%d, %02X, %02X)\n", dev->bus_index, func, addr, val);

    if (func > 0)
        return;

    if ((dev->local == AGP_BRIDGE_ALI_M5247) && (addr >= 0x40))
        return;

    switch (addr) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x06:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x1e:
        case 0x34:
        case 0x3d:
        case 0x67:
        case 0xdc:
        case 0xdd:
        case 0xde:
        case 0xdf:
            return;

        case 0x04:
            if (AGP_BRIDGE_INTEL(dev->local)) {
                if (dev->local == AGP_BRIDGE_INTEL_440BX)
                    val &= 0x1f;
            } else if (dev->local == AGP_BRIDGE_ALI_M5243)
                val |= 0x02;
            else if (dev->local == AGP_BRIDGE_ALI_M5247)
                val &= 0xc3;
            else if (AGP_BRIDGE_SIS(dev->local))
                val &= 0x27;
            else
                val &= 0x67;
            break;

        case 0x05:
            if (AGP_BRIDGE_INTEL(dev->local))
                val &= 0x01;
            else if (AGP_BRIDGE_ALI(dev->local))
                val &= 0x01;
            else
                val &= 0x03;
            break;

        case 0x07:
            if (dev->local == AGP_BRIDGE_INTEL_440LX)
                dev->regs[addr] &= ~(val & 0x40);
            else if (dev->local == AGP_BRIDGE_ALI_M5243)
                dev->regs[addr] &= ~(val & 0xf8);
            else if (dev->local == AGP_BRIDGE_ALI_M5247)
                dev->regs[addr] &= ~(val & 0xc0);
            return;

        case 0x0c:
        case 0x18:
            /* Parent bus number (0x18) is always 0 on AGP bridges. */
            if (AGP_BRIDGE(dev->local))
                return;
            break;

        case 0x0d:
            if (AGP_BRIDGE_VIA(dev->local))
                return;
            else if (AGP_BRIDGE_INTEL(dev->local))
                val &= 0xf8;
            else if (AGP_BRIDGE_ALI(dev->local))
                val &= 0xf8;
            break;

        case 0x19:
            /* Set our bus number. */
            pci_bridge_log("PCI Bridge %d: remapping from bus %02X to %02X\n", dev->bus_index, dev->regs[addr], val);
            pci_remap_bus(dev->bus_index, val);
            break;

        case 0x1f:
            if (AGP_BRIDGE_INTEL(dev->local)) {
                if (dev->local == AGP_BRIDGE_INTEL_440LX)
                    dev->regs[addr] &= ~(val & 0xf1);
                else if ((dev->local == AGP_BRIDGE_INTEL_440BX) || (dev->local == AGP_BRIDGE_INTEL_440GX))
                    dev->regs[addr] &= ~(val & 0xf0);
            } else if (AGP_BRIDGE_ALI(dev->local))
                dev->regs[addr] &= ~(val & 0xf0);
            return;

        case 0x1c:
        case 0x1d:
        case 0x20:
        case 0x22:
        case 0x24:
        case 0x26:
            val &= 0xf0;    /* SiS datasheets say 0Fh for 1Ch but that's clearly an erratum since the
                               definition of the bits is identical to the other vendors' AGP bridges. */
            break;

        case 0x3c:
            if (!(dev->ctl & 0x80))
                return;
            break;

        case 0x3e:
            if (AGP_BRIDGE_VIA(dev->local))
                val &= 0x0c;
            else if (AGP_BRIDGE_SIS(dev->local))
                val &= 0x0e;
            else if (dev->local == AGP_BRIDGE_ALI_M5247)
                val &= 0x0f;
            else if (dev->local == AGP_BRIDGE_ALI_M5243)
                return;
            else if (AGP_BRIDGE(dev->local)) {
                if ((dev->local == AGP_BRIDGE_INTEL_440BX) || (dev->local == AGP_BRIDGE_INTEL_440GX))
                    val &= 0xed;
                else
                    val &= 0x0f;
            } else if (dev->local == PCI_BRIDGE_DEC_21150)
                val &= 0xef;
            break;

        case 0x3f:
            if (dev->local == AGP_BRIDGE_INTEL_440LX) {
                dev->regs[addr] = ((dev->regs[addr] & 0x04) | (val & 0x02)) & ~(val & 0x04);
                return;
            } else if (dev->local == AGP_BRIDGE_ALI_M5247)
                return;
            else if (dev->local == AGP_BRIDGE_ALI_M5243)
                val &= 0x06;
            else if (AGP_BRIDGE(dev->local))
                return;
            else if (dev->local == PCI_BRIDGE_DEC_21150)
                val &= 0x0f;
            break;

        case 0x40:
            if (dev->local == PCI_BRIDGE_DEC_21150)
                val &= 0x32;
            break;

        case 0x41:
            if (AGP_BRIDGE_VIA(dev->local))
                val &= 0x7e;
            else if (dev->local == PCI_BRIDGE_DEC_21150)
                val &= 0x07;
            break;

        case 0x42:
            if (AGP_BRIDGE_VIA(dev->local))
                val &= 0xfe;
            break;

        case 0x43:
            if (dev->local == PCI_BRIDGE_DEC_21150)
                val &= 0x03;
            break;

        case 0x64:
            if (dev->local == PCI_BRIDGE_DEC_21150)
                val &= 0x7e;
            break;

        case 0x69:
            if (dev->local == PCI_BRIDGE_DEC_21150)
                val &= 0x3f;
            break;

        case 0x86:
            if (AGP_BRIDGE_ALI(dev->local))
                val &= 0x3f;
            break;

        case 0x87:
            if (AGP_BRIDGE_ALI(dev->local))
                val &= 0x60;
            break;

        case 0x88:
            if (AGP_BRIDGE_ALI(dev->local))
                val &= 0x8c;
            break;

        case 0x8b:
            if (AGP_BRIDGE_ALI(dev->local))
                val &= 0x0f;
            break;

        case 0x8c:
            if (AGP_BRIDGE_ALI(dev->local))
                val &= 0x83;
            break;

        case 0x8d:
            if (AGP_BRIDGE_ALI(dev->local))
                return;
            break;

        case 0xe0:
        case 0xe1:
            if (AGP_BRIDGE_ALI(dev->local)) {
                if (!(dev->ctl & 0x20))
                    return;
            } else
                return;
            break;

        case 0xe2:
            if (AGP_BRIDGE_ALI(dev->local)) {
                if (dev->ctl & 0x20)
                    val &= 0x3f;
                else
                    return;
            } else
                return;
            break;
        case 0xe3:
            if (AGP_BRIDGE_ALI(dev->local)) {
                if (dev->ctl & 0x20)
                    val &= 0xfe;
                else
                    return;
            } else
                return;
            break;

        case 0xe4:
            if (AGP_BRIDGE_ALI(dev->local)) {
                if (dev->ctl & 0x20)
                    val &= 0x03;
                else
                    return;
            }
            break;
        case 0xe5:
            if (AGP_BRIDGE_ALI(dev->local)) {
                if (!(dev->ctl & 0x20))
                    return;
            }
            break;

        case 0xe6:
            if (AGP_BRIDGE_ALI(dev->local)) {
                if (dev->ctl & 0x20)
                    val &= 0xc0;
                else
                    return;
            }
            break;

        case 0xe7:
            if (AGP_BRIDGE_ALI(dev->local)) {
                if (!(dev->ctl & 0x20))
                    return;
            }
            break;

        default:
            break;
    }

    dev->regs[addr] = val;
}

static uint8_t
pci_bridge_read(int func, int addr, void *priv)
{
    const pci_bridge_t *dev = (pci_bridge_t *) priv;
    uint8_t             ret;

    if (func > 0)
        ret = 0xff;
    else
        ret = dev->regs[addr];

    pci_bridge_log("PCI Bridge %d: read(%d, %02X) = %02X\n", dev->bus_index, func, addr, ret);
    return ret;
}

static void
pci_bridge_reset(void *priv)
{
    pci_bridge_t *dev = (pci_bridge_t *) priv;

    pci_bridge_log("PCI Bridge %d: reset()\n", dev->bus_index);

    memset(dev->regs, 0, sizeof(dev->regs));

    /* IDs */
    dev->regs[0x00] = dev->local >> 16;
    dev->regs[0x01] = dev->local >> 24;
    dev->regs[0x02] = dev->local;
    dev->regs[0x03] = dev->local >> 8;

    /* command and status */
    switch (dev->local) {
        case PCI_BRIDGE_DEC_21150:
            dev->regs[0x06] = 0x80;
            dev->regs[0x07] = 0x02;
            break;

        case AGP_BRIDGE_ALI_M5243:
            dev->regs[0x04] = 0x06;
            dev->regs[0x07] = 0x04;
            dev->regs[0x0d] = 0x20;
            dev->regs[0x19] = 0x01;
            dev->regs[0x1b] = 0x20;
            dev->regs[0x34] = 0xe0;
            dev->regs[0x89] = 0x20;
            dev->regs[0x8a] = 0xa0;
            dev->regs[0x8e] = 0x20;
            dev->regs[0x8f] = 0x20;
            dev->regs[0xe0] = 0x01;
            pci_remap_bus(dev->bus_index, 0x01);
            break;

        case AGP_BRIDGE_ALI_M5247:
            dev->regs[0x04] = 0x03;
            dev->regs[0x08] = 0x01;
            break;

        case AGP_BRIDGE_INTEL_440LX:
            dev->regs[0x06] = 0xa0;
            dev->regs[0x07] = 0x02;
            dev->regs[0x08] = 0x03;
            break;

        case AGP_BRIDGE_INTEL_440BX:
        case AGP_BRIDGE_INTEL_440GX:
            dev->regs[0x06] = 0x20;
            dev->regs[0x07] = dev->regs[0x08] = 0x02;
            break;

        case AGP_BRIDGE_VIA_597:
        case AGP_BRIDGE_VIA_598:
        case AGP_BRIDGE_VIA_691:
        case AGP_BRIDGE_VIA_8601:
            dev->regs[0x04] = 0x07;
            dev->regs[0x06] = 0x20;
            dev->regs[0x07] = 0x02;
            break;

        default:
            break;
    }

    /* class */
    dev->regs[0x0a] = 0x04; /* PCI-PCI bridge */
    dev->regs[0x0b] = 0x06; /* bridge device */
    dev->regs[0x0e] = 0x01; /* bridge header */

    /* IO BARs */
    if (AGP_BRIDGE(dev->local))
        dev->regs[0x1c] = 0xf0;
    else
        dev->regs[0x1c] = dev->regs[0x1d] = 0x01;

    if (dev->local == AGP_BRIDGE_ALI_M5247)
        dev->regs[0x1e] = 0x20;
    else if (!AGP_BRIDGE_VIA(dev->local)) {
        dev->regs[0x1e] = AGP_BRIDGE(dev->local) ? 0xa0 : 0x80;
        dev->regs[0x1f] = 0x02;
    }

    /* prefetchable memory limits */
    if (AGP_BRIDGE(dev->local)) {
        dev->regs[0x20] = dev->regs[0x24] = 0xf0;
        dev->regs[0x21] = dev->regs[0x25] = 0xff;
    } else {
        dev->regs[0x24] = dev->regs[0x26] = 0x01;
    }

    /* power management */
    if (dev->local == PCI_BRIDGE_DEC_21150) {
        dev->regs[0x34] = 0xdc;
        dev->regs[0x43] = 0x02;
        dev->regs[0xdc] = dev->regs[0xde] = 0x01;
    }
}

static void *
pci_bridge_init(const device_t *info)
{
    uint8_t interrupts[4];
    uint8_t interrupt_count;
    uint8_t interrupt_mask;
    uint8_t slot_count;

    pci_bridge_t *dev = (pci_bridge_t *) calloc(1, sizeof(pci_bridge_t));

    dev->local     = info->local;
    dev->bus_index = pci_register_bus();
    pci_bridge_log("PCI Bridge %d: init()\n", dev->bus_index);

    pci_bridge_reset(dev);

    pci_add_bridge(AGP_BRIDGE(dev->local), pci_bridge_read, pci_bridge_write, dev, &dev->slot);

    interrupt_count = sizeof(interrupts);
    interrupt_mask  = interrupt_count - 1;
    if (dev->slot < 32) {
        for (uint8_t i = 0; i < interrupt_count; i++)
            interrupts[i] = pci_get_int(dev->slot, PCI_INTA + i);
    }
    pci_bridge_log("PCI Bridge %d: upstream bus %02X slot %02X interrupts %02X %02X %02X %02X\n",
                   dev->bus_index, (dev->slot >> 5) & 0xff, dev->slot & 31, interrupts[0],
                   interrupts[1], interrupts[2], interrupts[3]);

    if (info->local == PCI_BRIDGE_DEC_21150)
        slot_count = 9; /* 9 bus masters */
    else
        slot_count = 1; /* AGP bridges always have 1 slot */

    for (uint8_t i = 0; i < slot_count; i++) {
        /* Interrupts for bridge slots are assigned in round-robin: ABCD, BCDA, CDAB and so on. */
        pci_bridge_log("PCI Bridge %d: downstream slot %02X interrupts %02X %02X %02X %02X\n",
                       dev->bus_index, i, interrupts[i & interrupt_mask],
                       interrupts[(i + 1) & interrupt_mask], interrupts[(i + 2) & interrupt_mask],
                       interrupts[(i + 3) & interrupt_mask]);
        pci_register_bus_slot(dev->bus_index, i, AGP_BRIDGE(dev->local) ? PCI_CARD_AGP : PCI_CARD_NORMAL,
                              interrupts[i & interrupt_mask],
                              interrupts[(i + 1) & interrupt_mask],
                              interrupts[(i + 2) & interrupt_mask],
                              interrupts[(i + 3) & interrupt_mask]);
    }

    return dev;
}

/* PCI bridges */
const device_t dec21150_device = {
    .name          = "DEC 21150 PCI Bridge",
    .internal_name = "dec21150",
    .flags         = DEVICE_PCI,
    .local         = PCI_BRIDGE_DEC_21150,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* AGP bridges */
const device_t ali5243_agp_device = {
    .name          = "ALi M5243 AGP Bridge",
    .internal_name = "ali5243_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_ALI_M5243,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* AGP bridges */
const device_t ali5247_agp_device = {
    .name          = "ALi M5247 AGP Bridge",
    .internal_name = "ali5247_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_ALI_M5247,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i440lx_agp_device = {
    .name          = "Intel 82443LX/EX AGP Bridge",
    .internal_name = "i440lx_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_INTEL_440LX,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i440bx_agp_device = {
    .name          = "Intel 82443BX/ZX AGP Bridge",
    .internal_name = "i440bx_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_INTEL_440BX,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i440gx_agp_device = {
    .name          = "Intel 82443GX AGP Bridge",
    .internal_name = "i440gx_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_INTEL_440GX,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t via_vp3_agp_device = {
    .name          = "VIA Apollo VP3 AGP Bridge",
    .internal_name = "via_vp3_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_VIA_597,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t via_mvp3_agp_device = {
    .name          = "VIA Apollo MVP3 AGP Bridge",
    .internal_name = "via_mvp3_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_VIA_598,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t via_apro_agp_device = {
    .name          = "VIA Apollo Pro AGP Bridge",
    .internal_name = "via_apro_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_VIA_691,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t via_vt8601_agp_device = {
    .name          = "VIA Apollo ProMedia AGP Bridge",
    .internal_name = "via_vt8601_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_VIA_8601,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5xxx_agp_device = {
    .name          = "SiS 5591/(5)600 AGP Bridge",
    .internal_name = "via_5xxx_agp",
    .flags         = DEVICE_PCI,
    .local         = AGP_BRIDGE_SIS_5XXX,
    .init          = pci_bridge_init,
    .close         = NULL,
    .reset         = pci_bridge_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
