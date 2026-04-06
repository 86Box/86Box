/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the VIA Apollo series of chips.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Melissa Goad, <mszoopers@protonmail.com>
 *          Tiseno100,
 *
 *          Copyright 2020 Miran Grca.
 *          Copyright 2020 Melissa Goad.
 *          Copyright 2020 Tiseno100.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/chipset.h>
#include <86box/spd.h>
#include <86box/agpgart.h>
#include <86box/plat_unused.h>

typedef struct amd751_t {
    uint8_t pci_conf[256];
    uint8_t pci_slot;

    agpgart_t *agpgart;
} amd751_t;

static void
amd751_agp_map(amd751_t* dev)
{
    agpgart_set_aperture(dev->agpgart,
                         (dev->pci_conf[0x13] << 24),
                         0x2000000 << (((uint32_t)dev->pci_conf[0xac] & 0x0e) >> 1),
                         !!(dev->pci_conf[0xac] & 0x01));
    agpgart_set_gart(dev->agpgart, (dev->pci_conf[0x15] << 8) | (dev->pci_conf[0x16] << 16) | (dev->pci_conf[0x17] << 24));
}

static uint8_t
amd751_host_read(int addr, amd751_t* dev)
{
    pclog("amd751 read %02x\n", addr);

    switch (addr) {
        case 0x00: return 0x22;
        case 0x01: return 0x10;
        case 0x02: return 0x06;
        case 0x03: return 0x70;
        case 0x04: return (dev->pci_conf[0x04] & 0x02) | 0x04;
        case 0x05: return 0x00;
        case 0x06: return 0x10;
        case 0x07: return 0x02;
        case 0x08: return 0x25;
        case 0x09: case 0x0a: case 0x0c: case 0x0f: return 0x00;
        case 0x0b: return 0x06;
        case 0x0d: return dev->pci_conf[0x0d];
        case 0x0e: return 0x80;
        case 0x10: return 0x08;
        case 0x11: return 0x00;
        case 0x12: return 0x00;
        case 0x13: return dev->pci_conf[0x13];
        case 0x14: return 0x08;
        case 0x15: return dev->pci_conf[0x15] & 0xf0;
        case 0x16: return dev->pci_conf[0x16];
        case 0x17: return dev->pci_conf[0x17];
        case 0x18: return (dev->pci_conf[0x18] & 0xfc) | 0x01;
        case 0x19: return dev->pci_conf[0x19];
        case 0x1a: return dev->pci_conf[0x1a];
        case 0x1b: return 0x00;
        case 0x34: return 0xa0;
        case 0x35: case 0x36: case 0x37: return 0x00;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a: case 0x4b:
        case 0x50: case 0x51: case 0x52:
            return dev->pci_conf[addr];
#if 0
        case 0x5a:
            return dev->pci_conf[0x5a] & 0x97;
        case 0x5b:
            return (dev->pci_conf[0x5b] & 0x01) | 0x02;
#endif
        case 0x80: case 0x81: return 0x00;
        case 0xa0: return 0x02;
        case 0xa1: case 0xa3: case 0xa6: return 0x00;
        case 0xa2: return 0x22;
        case 0xa4: return (dev->pci_conf[0xa8] & 0x20) | 0x03;
        case 0xa5: return 0x02;
        case 0xa7: return 0x0f;
        case 0xa8: return dev->pci_conf[0xa8] & 3;
        case 0xa9: return dev->pci_conf[0xa9] & 3;
        case 0xac: return dev->pci_conf[0xac] & 0xf;
        case 0xae: return dev->pci_conf[0xae] & 1;
        case 0xb0: return dev->pci_conf[0xb0];
        case 0xb2: return dev->pci_conf[0xb2] & 0x1f;
        default: return 0x00;
    }
}

static void
amd751_host_write(int addr, uint8_t val, void *priv)
{
    pclog("amd751 write %02x %02x\n", addr, val);
    amd751_t *dev = (amd751_t *) priv;

    switch (addr) {
        case 0x13: /* Graphics Aperture Base */
            dev->pci_conf[0x13] = val;
            amd751_agp_map(dev);
            break;
        case 0x15: /* GART Base */
            dev->pci_conf[0x15] = val & 0xf0;
            amd751_agp_map(dev);
            break;
        case 0x16: /* GART Base */
            dev->pci_conf[0x16] = val;
            amd751_agp_map(dev);
            break;
        case 0x17: /* GART Base */
            dev->pci_conf[0x17] = val;
            amd751_agp_map(dev);
            break;
        case 0x18:
            dev->pci_conf[0x18] = val & 0xfc;
            break;
        case 0x19:
            dev->pci_conf[0x19] = val;
            break;
        case 0x1a:
            dev->pci_conf[0x1a] = val;
            break;
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
        case 0x50:
        case 0x51:
        case 0x52:
            dev->pci_conf[addr] = val;
            spd_write_drbs_amd751(dev->pci_conf, 0x40, 0x4b, 0x50, 0x52);
            break;
        default:
            dev->pci_conf[addr] = val;
            break;
    }
}

static uint8_t
amd751_read(int func, int addr, void *priv)
{
    amd751_t *dev = (amd751_t *) priv;
    uint8_t       ret = 0xff;

    switch (func) {
        case 0:
            ret = amd751_host_read(addr, dev);
            break;

        default:
            break;
    }

    return ret;
}

static void
amd751_write(int func, int addr, uint8_t val, void *priv)
{
    switch (func) {
        case 0:
            amd751_host_write(addr, val, priv);
            break;

        default:
            break;
    }
}

static void
amd751_reset(UNUSED(void *priv))
{
    //
}

static void *
amd751_init(UNUSED(const device_t *info))
{
    amd751_t *dev = (amd751_t *) malloc(sizeof(amd751_t));
    memset(dev, 0, sizeof(amd751_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, amd751_read, amd751_write, dev, &dev->pci_slot);

    device_add(&amd751_agp_device);

    dev->agpgart = device_add(&agpgart_device);

    cpu_cache_int_enabled = 1;
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    amd751_reset(dev);

    return dev;
}

static void
amd751_close(void *priv)
{
    amd751_t *dev = (amd751_t *) priv;

    free(dev);
}

const device_t amd751_device = {
    .name          = "AMD 751 System Controller",
    .internal_name = "amd751",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = amd751_init,
    .close         = amd751_close,
    .reset         = amd751_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
