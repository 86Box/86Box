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

#ifdef ENABLE_SIS_5595_PMU_LOG
int sis_5595_pmu_do_log = ENABLE_SIS_5595_PMU_LOG;

static void
sis_5595_pmu_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5595_pmu_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5595_pmu_log(fmt, ...)
#endif

typedef struct sis_5595_pmu_io_trap_t {
    void          *priv;
    void          *trap;
    uint8_t        flags, mask;
    uint8_t       *sts_reg, sts_mask;
    uint16_t       addr;
} sis_5595_pmu_io_trap_t;

typedef struct sis_5595_pmu_t {
    uint8_t     is_1997;

    uint8_t     pci_conf[256];

    sis_5595_pmu_io_trap_t io_traps[22];

    sis_55xx_common_t *sis;
} sis_5595_pmu_t;

static void
sis_5595_pmu_trap_io(UNUSED(int size), UNUSED(uint16_t addr), UNUSED(uint8_t write), UNUSED(uint8_t val),
                     void *priv)
{
    sis_5595_pmu_io_trap_t *trap = (sis_5595_pmu_io_trap_t *) priv;
    sis_5595_pmu_t *dev = (sis_5595_pmu_t *) trap->priv;

    trap->sts_reg[0x04] |= trap->sts_mask;

    if (trap->sts_reg[0x00] & trap->sts_mask)
        acpi_sis5595_pmu_event(dev->sis->acpi);

    if (trap->sts_reg[0x20] & trap->sts_mask)
        acpi_update_irq(dev->sis->acpi);
}

static void
sis_5595_pmu_trap_io_ide(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv)
{
    sis_5595_pmu_io_trap_t *trap = (sis_5595_pmu_io_trap_t *) priv;

    /* IDE traps are per drive, not per channel. */
    if (ide_drives[trap->flags & 0x03]->selected)
        sis_5595_pmu_trap_io(size, addr, write, val, priv);
}

static void
sis_5595_pmu_trap_io_mask(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv)
{
    sis_5595_pmu_io_trap_t *trap = (sis_5595_pmu_io_trap_t *) priv;

    if ((addr & trap->mask) == (trap->addr & trap->mask))
        sis_5595_pmu_trap_io(size, addr, write, val, priv);
}

static void
sis_5595_pmu_trap_io_ide_bm(UNUSED(int size), UNUSED(uint16_t addr), UNUSED(uint8_t write), UNUSED(uint8_t val), void *priv)
{
    sis_5595_pmu_io_trap_t *trap = (sis_5595_pmu_io_trap_t *) priv;
    sis_5595_pmu_t *dev = (sis_5595_pmu_t *) trap->priv;

    if (trap->flags & 0x01) {
        dev->pci_conf[0x67] |= 0x01;
        dev->pci_conf[0x64] |= 0x08;
    } else {
        dev->pci_conf[0x67] |= 0x02;
        dev->pci_conf[0x64] |= 0x10;
    }
    acpi_sis5595_pmu_event(dev->sis->acpi);
}

static void
sis_5595_pmu_trap_update_devctl(sis_5595_pmu_t *dev, uint8_t trap_id, uint8_t enable,
                                uint8_t flags, uint8_t mask, uint8_t *sts_reg, uint8_t sts_mask,
                                uint16_t addr, uint16_t size)
{
    sis_5595_pmu_io_trap_t *trap = &dev->io_traps[trap_id];

    /* Set up Device I/O traps dynamically. */
    if (enable && !trap->trap) {
        trap->priv     = (void *) dev;
        trap->flags    = flags;
        trap->mask     = mask;
        trap->addr     = addr;
        if (flags & 0x10)
            trap->trap     = io_trap_add(sis_5595_pmu_trap_io_ide_bm, trap);
        else if (flags & 0x08)
            trap->trap     = io_trap_add(sis_5595_pmu_trap_io_mask, trap);
        else if (flags & 0x04)
            trap->trap     = io_trap_add(sis_5595_pmu_trap_io_ide, trap);
        else
            trap->trap     = io_trap_add(sis_5595_pmu_trap_io, trap);
        trap->sts_reg  = sts_reg;
        trap->sts_mask = sts_mask;
    }

    /* Remap I/O trap. */
    io_trap_remap(trap->trap, enable, addr, size);
}

static void
sis_5595_pmu_trap_update(void *priv)
{
    sis_5595_pmu_t *dev     = (sis_5595_pmu_t *) priv;
    uint8_t         trap_id = 0;
    uint8_t        *fregs   = dev->pci_conf;
    uint16_t        temp;
    uint8_t         mask;
    uint8_t         on;

    temp = (fregs[0x7e] | (fregs[0x7f] << 8)) & 0xffe0;

    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    fregs[0x7e] & 0x08, 0x10, 0xff, NULL, 0xff, temp, 0x08);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    fregs[0x7e] & 0x04, 0x10, 0xff, NULL, 0xff, temp + 8, 0x08);

    on = fregs[0x63] | fregs[0x83];
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x02, 0x04, 0xff, &(fregs[0x63]), 0x02, 0x1f0, 0x08);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x01, 0x06, 0xff, &(fregs[0x63]), 0x01, 0x170, 0x08);

    on = fregs[0x62] | fregs[0x82];
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x80, 0x00, 0xff, &(fregs[0x62]), 0x80, 0x064, 0x01);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x80, 0x00, 0xff, &(fregs[0x62]), 0x80, 0x060, 0x01);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x40, 0x00, 0xff, &(fregs[0x62]), 0x40, 0x3f8, 0x08);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x20, 0x00, 0xff, &(fregs[0x62]), 0x20, 0x2f8, 0x08);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x10, 0x00, 0xff, &(fregs[0x62]), 0x10, 0x378, 0x08);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x10, 0x00, 0xff, &(fregs[0x62]), 0x10, 0x278, 0x08);

    temp = (fregs[0x5c] | (fregs[0x5d] << 8)) & 0x03ff;
    mask = fregs[0x5d] >> 2;

    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x04, 0x08, mask, &(fregs[0x62]), 0x04, temp, 0x40);

    temp = fregs[0x5e] | (fregs[0x5f] << 8);

    if (dev->is_1997) {
        mask = fregs[0x4d] & 0x1f;

        sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                        on & 0x02, 0x08, mask, &(fregs[0x62]), 0x02, temp, 0x20);
    } else {
        mask = fregs[0x4d];

        sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                        on & 0x02, 0x08, mask, &(fregs[0x62]), 0x02, temp, 0x100);
    }

    on = fregs[0x61] | fregs[0x81];
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x40, 0x00, 0xff, &(fregs[0x61]), 0x40, 0x3b0, 0x30);

    switch ((fregs[0x4c] >> 6) & 0x03) {
        case 0x00:
            temp = 0xf40;
            break;
        case 0x01:
            temp = 0xe80;
            break;
        case 0x02:
            temp = 0x604;
            break;
        default:
            temp = 0x530;
            break;
    }

    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x10, 0x00, 0xff, &(fregs[0x61]), 0x10, temp, 0x08);

    switch ((fregs[0x4c] >> 4) & 0x03) {
        case 0x00:
            temp = 0x280;
            break;
        case 0x01:
            temp = 0x260;
            break;
        case 0x02:
            temp = 0x240;
            break;
        default:
            temp = 0x220;
            break;
    }

    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x08, 0x00, 0xff, &(fregs[0x61]), 0x08, temp, 0x14);

    switch ((fregs[0x4c] >> 2) & 0x03) {
        case 0x00:
            temp = 0x330;
            break;
        case 0x01:
            temp = 0x320;
            break;
        case 0x02:
            temp = 0x310;
            break;
        default:
            temp = 0x300;
            break;
    }

    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x04, 0x00, 0xff, &(fregs[0x61]), 0x04, temp, 0x04);

    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x02, 0x00, 0xff, &(fregs[0x61]), 0x02, 0x200, 0x08);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x02, 0x00, 0xff, &(fregs[0x61]), 0x02, 0x388, 0x04);

    on = fregs[0x60] | fregs[0x80];
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x20, 0x00, 0xff, &(fregs[0x60]), 0x20, 0x3f0, 0x08);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x20, 0x00, 0xff, &(fregs[0x60]), 0x20, 0x370, 0x08);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x10, 0x05, 0xff, &(fregs[0x60]), 0x10, 0x1f0, 0x08);
    sis_5595_pmu_trap_update_devctl(dev, trap_id++,
                                    on & 0x08, 0x07, 0xff, &(fregs[0x60]), 0x08, 0x170, 0x08);
}

void
sis_5595_pmu_write(int addr, uint8_t val, void *priv)
{
    sis_5595_pmu_t *dev = (sis_5595_pmu_t *) priv;

    sis_5595_pmu_log("SiS 5595 PMU: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    if (dev->sis->usb_enabled)  switch (addr) {
        default:
            break;

        case 0x40 ... 0x4b:
        case 0x50 ... 0x5b:
        case 0x68 ... 0x7b:
        case 0x7d:
            dev->pci_conf[addr] = val;
            break;
        case 0x4c ... 0x4d:
        case 0x5c ... 0x63:
        case 0x7e ... 0x7f:
        case 0x80 ... 0x83:
            dev->pci_conf[addr] = val;
            sis_5595_pmu_trap_update(dev);
            break;
       case 0x64 ... 0x67:
            dev->pci_conf[addr] &= ~val;
            break;
       case 0x7c:
            dev->pci_conf[addr] = val;
            if (val & 0x02) {
                dev->pci_conf[0x64] |= 0x04;
                if (dev->pci_conf[0x60] & 0x04)
                    acpi_sis5595_pmu_event(dev->sis->acpi);
            }
            break;
    }
}

uint8_t
sis_5595_pmu_read(int addr, void *priv)
{
    const sis_5595_pmu_t *dev = (sis_5595_pmu_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->pci_conf[addr];

    sis_5595_pmu_log("SiS 5595 PMU: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5595_pmu_reset(void *priv)
{
    sis_5595_pmu_t *dev = (sis_5595_pmu_t *) priv;

    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x09;
    dev->pci_conf[0x03] = 0x00;
    dev->pci_conf[0x04] = dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = 0x00;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0xff;
    dev->pci_conf[0x0c] = dev->pci_conf[0x0d] = 0x00;
    dev->pci_conf[0x0e] = 0x80;
    dev->pci_conf[0x0f] = 0x00;
    dev->pci_conf[0x40] = dev->pci_conf[0x41] = 0x00;
    dev->pci_conf[0x42] = dev->pci_conf[0x43] = 0x00;
    dev->pci_conf[0x44] = dev->pci_conf[0x45] = 0x00;
    dev->pci_conf[0x46] = dev->pci_conf[0x47] = 0x00;
    dev->pci_conf[0x48] = dev->pci_conf[0x49] = 0x00;
    dev->pci_conf[0x4a] = dev->pci_conf[0x4b] = 0x00;
    dev->pci_conf[0x4c] = dev->pci_conf[0x4d] = 0x00;
    dev->pci_conf[0x4e] = dev->pci_conf[0x4f] = 0x00;
    dev->pci_conf[0x50] = dev->pci_conf[0x51] = 0x00;
    dev->pci_conf[0x52] = dev->pci_conf[0x53] = 0x00;
    dev->pci_conf[0x54] = dev->pci_conf[0x55] = 0x00;
    dev->pci_conf[0x56] = dev->pci_conf[0x57] = 0x00;
    dev->pci_conf[0x58] = dev->pci_conf[0x59] = 0x00;
    dev->pci_conf[0x5a] = dev->pci_conf[0x5b] = 0x00;
    dev->pci_conf[0x5c] = dev->pci_conf[0x5d] = 0x00;
    dev->pci_conf[0x5e] = dev->pci_conf[0x5f] = 0x00;
    dev->pci_conf[0x60] = dev->pci_conf[0x61] = 0x00;
    dev->pci_conf[0x62] = dev->pci_conf[0x63] = 0x00;
    dev->pci_conf[0x64] = dev->pci_conf[0x65] = 0x00;
    dev->pci_conf[0x66] = dev->pci_conf[0x67] = 0x00;
    dev->pci_conf[0x68] = dev->pci_conf[0x69] = 0x00;
    dev->pci_conf[0x6a] = dev->pci_conf[0x6b] = 0x00;
    dev->pci_conf[0x6c] = dev->pci_conf[0x6d] = 0x00;
    dev->pci_conf[0x6e] = dev->pci_conf[0x6f] = 0x00;
    dev->pci_conf[0x70] = dev->pci_conf[0x71] = 0x00;
    dev->pci_conf[0x72] = dev->pci_conf[0x73] = 0x00;
    dev->pci_conf[0x74] = dev->pci_conf[0x75] = 0x00;
    dev->pci_conf[0x76] = dev->pci_conf[0x77] = 0x00;
    dev->pci_conf[0x78] = dev->pci_conf[0x79] = 0x00;
    dev->pci_conf[0x7a] = dev->pci_conf[0x7b] = 0x00;
    dev->pci_conf[0x7c] = dev->pci_conf[0x7d] = 0x00;
    dev->pci_conf[0x7e] = dev->pci_conf[0x7f] = 0x00;
    dev->pci_conf[0x80] = dev->pci_conf[0x81] = 0x00;
    dev->pci_conf[0x82] = dev->pci_conf[0x83] = 0x00;

    sis_5595_pmu_trap_update(dev);
    acpi_update_irq(dev->sis->acpi);
}

static void
sis_5595_pmu_close(void *priv)
{
    sis_5595_pmu_t *dev = (sis_5595_pmu_t *) priv;

    free(dev);
}

static void *
sis_5595_pmu_init(UNUSED(const device_t *info))
{
    sis_5595_pmu_t *dev = (sis_5595_pmu_t *) calloc(1, sizeof(sis_5595_pmu_t));

    dev->sis = device_get_common_priv();
    dev->sis->pmu_regs = dev->pci_conf;

    dev->is_1997 = info->local;

    sis_5595_pmu_reset(dev);

    return dev;
}

const device_t sis_5595_1997_pmu_device = {
    .name          = "SiS 5595 (1997) PMU",
    .internal_name = "sis_5595_1997_pmu",
    .flags         = DEVICE_PCI,
    .local         = 0x01,
    .init          = sis_5595_pmu_init,
    .close         = sis_5595_pmu_close,
    .reset         = sis_5595_pmu_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5595_pmu_device = {
    .name          = "SiS 5595 PMU",
    .internal_name = "sis_5595_pmu",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = sis_5595_pmu_init,
    .close         = sis_5595_pmu_close,
    .reset         = sis_5595_pmu_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
