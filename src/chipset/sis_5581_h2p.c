/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5581 Host to PCI bridge.
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

#ifdef ENABLE_SIS_5581_HOST_TO_PCI_LOG
int sis_5581_host_to_pci_do_log = ENABLE_SIS_5581_HOST_TO_PCI_LOG;

static void
sis_5581_host_to_pci_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5581_host_to_pci_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5581_host_to_pci_log(fmt, ...)
#endif

typedef struct {
    uint8_t  installed;
    uint8_t  code;
    uint32_t phys_size;
} ram_bank_t;

typedef struct sis_5581_io_trap_t {
    void          *priv;
    void          *trap;
    uint8_t        flags, mask;
    uint8_t       *sts_reg, sts_mask;
    uint16_t       addr;
} sis_5581_io_trap_t;

typedef struct sis_5581_host_to_pci_t {
    uint8_t            pci_conf[256];
    uint8_t            states[7];

    ram_bank_t         ram_banks[3];

    sis_5581_io_trap_t io_traps[10];

    sis_55xx_common_t *sis;

    smram_t           *smram;
} sis_5581_host_to_pci_t;

static uint8_t  bank_codes[7] = { 0x00, 0x20, 0x24, 0x22, 0x26, 0x2a, 0x2b };

static uint32_t bank_sizes[7] = { 0x00800000,      /*   8 MB */
                                  0x01000000,      /*  16 MB */
                                  0x02000000,      /*  32 MB */
                                  0x04000000,      /*  64 MB */
                                  0x08000000,      /* 128 MB */
                                  0x10000000,      /* 256 MB */
                                  0x20000000 };    /* 512 MB */

static void
sis_5581_shadow_recalc(sis_5581_host_to_pci_t *dev)
{
    int      state;
    uint32_t base;

    for (uint8_t i = 0x70; i <= 0x76; i++) {
        if (i == 0x76) {
            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(0xf0000, 0x10000, state);
                sis_5581_host_to_pci_log("000F0000-000FFFFF\n");
            }
        } else {
            base = ((i & 0x07) << 15) + 0xc0000;

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base, 0x4000, state);
                sis_5581_host_to_pci_log("%08X-%08X\n", base, base + 0x3fff);
            }

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0x0a) {
                state = (dev->pci_conf[i] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base + 0x4000, 0x4000, state);
                sis_5581_host_to_pci_log("%08X-%08X\n", base + 0x4000, base + 0x7fff);
            }
        }

        dev->states[i & 0x0f] = dev->pci_conf[i];
    }

    flushmmucache_nopc();
}

static void
sis_5581_trap_io(UNUSED(int size), UNUSED(uint16_t addr), UNUSED(uint8_t write), UNUSED(uint8_t val),
                        void *priv)
{
    sis_5581_io_trap_t *trap = (sis_5581_io_trap_t *) priv;
    sis_5581_host_to_pci_t *dev = (sis_5581_host_to_pci_t *) trap->priv;

    trap->sts_reg[0x04] |= trap->sts_mask;

    if (trap->sts_reg[0x00] & trap->sts_mask)
        acpi_sis5582_pmu_event(dev->sis->acpi);
}

static void
sis_5581_trap_io_mask(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv)
{
    sis_5581_io_trap_t *trap = (sis_5581_io_trap_t *) priv;

    if ((addr & trap->mask) == (trap->addr & trap->mask))
        sis_5581_trap_io(size, addr, write, val, priv);
}

static void
sis_5581_trap_update_devctl(sis_5581_host_to_pci_t *dev, uint8_t trap_id, uint8_t enable,
                            uint8_t flags, uint8_t mask, uint8_t *sts_reg, uint8_t sts_mask,
                            uint16_t addr, uint16_t size)
{
    sis_5581_io_trap_t *trap = &dev->io_traps[trap_id];

    /* Set up Device I/O traps dynamically. */
    if (enable && !trap->trap) {
        trap->priv     = (void *) dev;
        trap->flags    = flags;
        trap->mask     = mask;
        trap->addr     = addr;
        if (flags & 0x08)
            trap->trap     = io_trap_add(sis_5581_trap_io_mask, trap);
        else
            trap->trap     = io_trap_add(sis_5581_trap_io, trap);
        trap->sts_reg  = sts_reg;
        trap->sts_mask = sts_mask;
    }

    /* Remap I/O trap. */
    io_trap_remap(trap->trap, enable, addr, size);
}

static void
sis_5581_trap_update(void *priv)
{
    sis_5581_host_to_pci_t *dev     = (sis_5581_host_to_pci_t *) priv;
    uint8_t                 trap_id = 0;
    uint8_t                *fregs   = dev->pci_conf;
    uint16_t                temp;
    uint8_t                 mask;
    uint8_t                 on;

    on = fregs[0x9a];

    temp = ((fregs[0x96] & 0x02) | (fregs[0x97] << 2)) & 0x03ff;
    mask = ~((1 << ((fregs[0x96] >> 3) & 0x07)) - 1);

    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x40, 0x08, mask, &(fregs[0x9c]), 0x40, temp, 0x80);

    temp = fregs[0x98] | (fregs[0x99] << 8);
    mask = 0xff;

    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x20, 0x08, mask, &(fregs[0x9c]), 0x20, temp, 0x80);

    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x10, 0x00, 0xff, &(fregs[0x9c]), 0x10, 0x378, 0x08);
    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x10, 0x00, 0xff, &(fregs[0x9c]), 0x10, 0x278, 0x08);

    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x08, 0x00, 0xff, &(fregs[0x9c]), 0x08, 0x3f8, 0x08);

    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x04, 0x00, 0xff, &(fregs[0x9c]), 0x04, 0x2f8, 0x08);

    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x02, 0x00, 0xff, &(fregs[0x9c]), 0x02, 0x1f0, 0x08);

    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x01, 0x00, 0xff, &(fregs[0x9c]), 0x01, 0x170, 0x08);

    on = fregs[0x9b];
    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x08, 0x00, 0xff, &(fregs[0x9d]), 0x08, 0x064, 0x01);
    sis_5581_trap_update_devctl(dev, trap_id++,
                                on & 0x08, 0x00, 0xff, &(fregs[0x9d]), 0x08, 0x060, 0x01);
}

static void
sis_5581_smram_recalc(sis_5581_host_to_pci_t *dev)
{
    smram_disable_all();

    switch (dev->pci_conf[0xa3] >> 6) {
        case 0:
            smram_enable(dev->smram, 0x000e0000, 0x000e0000, 0x8000, dev->pci_conf[0xa3] & 0x10, 1);
            break;
        case 1:
            smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x8000, dev->pci_conf[0xa3] & 0x10, 1);
            break;
        case 2:
            smram_enable(dev->smram, 0x000e0000, 0x000b0000, 0x8000, dev->pci_conf[0xa3] & 0x10, 1);
            break;
        case 3:
            smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x10000, dev->pci_conf[0xa3] & 0x10, 1);
            break;

        default:
            break;
    }

    flushmmucache();
}

void
sis_5581_host_to_pci_write(int addr, uint8_t val, void *priv)
{
    sis_5581_host_to_pci_t *dev = (sis_5581_host_to_pci_t *) priv;

    sis_5581_host_to_pci_log("SiS 5581 H2P: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    switch (addr) {
        default:
            break;

        case 0x04: /* Command - Low Byte */
            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0xfc) | (val & 0x03);
            break;
        case 0x05: /* Command - High Byte */
            dev->pci_conf[addr] = val & 0x02;
            break;

        case 0x07: /* Status - High Byte */
            dev->pci_conf[addr] &= ~(val & 0xb8);
            break;

        case 0x0d: /* Master latency timer */
        case 0x50:
        case 0x54:
        case 0x56 ... 0x57:
        case 0x59:
            dev->pci_conf[addr] = val;
            break;

        case 0x51:
            dev->pci_conf[addr] = val;
            cpu_cache_ext_enabled = !!(val & 0x40);
            cpu_update_waitstates();
            break;

        case 0x52:
            dev->pci_conf[addr] = val & 0xeb;
            break;

        case 0x53:
        case 0x55:
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x58:
            dev->pci_conf[addr] = val & 0xfc;
            break;

        case 0x5a:
            dev->pci_conf[addr] = val & 0x03;
            break;

        case 0x60 ... 0x62:
            dev->pci_conf[addr] = dev->ram_banks[addr & 0x0f].code | 0xc0;
            break;

        case 0x63:
            dev->pci_conf[addr] = dev->ram_banks[0].installed |
                                  (dev->ram_banks[1].installed << 1) |
                                  (dev->ram_banks[2].installed << 2);
            break;

        case 0x70 ... 0x75:
            dev->pci_conf[addr] = val & 0xee;
            sis_5581_shadow_recalc(dev);
            break;
        case 0x76:
            dev->pci_conf[addr] = val & 0xe8;
            sis_5581_shadow_recalc(dev);
            break;

        case 0x77: /* Characteristics of non-cacheable area */
            dev->pci_conf[addr] = val & 0x0f;
            break;

        case 0x78: /* Allocation of Non-Cacheable area #1 */
        case 0x79: /* NCA1REG2 */
        case 0x7a: /* Allocation of Non-Cacheable area #2 */
        case 0x7b: /* NCA2REG2 */
            dev->pci_conf[addr] = val;
            break;

        case 0x80: /* PCI master characteristics */
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x81:
            dev->pci_conf[addr] = val & 0xde;
            break;

        case 0x82:
            dev->pci_conf[addr] = val;
            break;

        case 0x83: /* CPU to PCI characteristics */
            dev->pci_conf[addr] = val;
            /* TODO: Implement Fast A20 and Fast reset stuff on the KBC already! */
            break;

        case 0x84 ... 0x86:
        case 0x88 ... 0x8b:
            dev->pci_conf[addr] = val;
            break;

        case 0x87: /* Miscellanea */
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x8c ... 0x92:
        case 0x9e ... 0xa2:
            dev->pci_conf[addr] = val;
            break;

        case 0x93:
            dev->pci_conf[addr] = val;
            if (val & 0x02) {
                dev->pci_conf[0x9d] |= 0x01;
                if (dev->pci_conf[0x9b] & 0x01)
                    acpi_sis5582_pmu_event(dev->sis->acpi);
            }
            break;

        case 0x94:
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x95:
            dev->pci_conf[addr] = val & 0xfb;
            break;

        case 0x96:
            dev->pci_conf[addr] = val & 0xfb;
            sis_5581_trap_update(dev);
            break;
        case 0x97 ... 0x9b:
            dev->pci_conf[addr] = val;
            sis_5581_trap_update(dev);
            break;

        case 0x9c ... 0x9d:
            dev->pci_conf[addr] &= ~val;
            break;

        case 0xa3:
            dev->pci_conf[addr] = val;
            sis_5581_smram_recalc(dev);
            break;
    }
}

uint8_t
sis_5581_host_to_pci_read(int addr, void *priv)
{
    const sis_5581_host_to_pci_t *dev = (sis_5581_host_to_pci_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->pci_conf[addr];

    sis_5581_host_to_pci_log("SiS 5581 H2P: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5581_host_to_pci_reset(void *priv)
{
    sis_5581_host_to_pci_t *dev = (sis_5581_host_to_pci_t *) priv;

    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x97;
    dev->pci_conf[0x03] = 0x55;
    dev->pci_conf[0x04] = 0x05;
    dev->pci_conf[0x05] = dev->pci_conf[0x06] = 0x00;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = 0x02;
    dev->pci_conf[0x09] = dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x0c] = 0x00;
    dev->pci_conf[0x0d] = 0xff;
    dev->pci_conf[0x0e] = dev->pci_conf[0x0f] = 0x00;
    dev->pci_conf[0x50] = dev->pci_conf[0x51] = 0x00;
    dev->pci_conf[0x52] = 0x00;
    dev->pci_conf[0x53] = 0x38;
    dev->pci_conf[0x54] = 0x54;
    dev->pci_conf[0x55] = 0x00;
    dev->pci_conf[0x56] = 0x80;
    dev->pci_conf[0x57] = dev->pci_conf[0x58] = 0x00;
    dev->pci_conf[0x59] = dev->pci_conf[0x5a] = 0x00;
    dev->pci_conf[0x60] = dev->pci_conf[0x61] = 0x00;
    dev->pci_conf[0x62] = 0x00;
    dev->pci_conf[0x63] = 0xff;
    dev->pci_conf[0x70] = dev->pci_conf[0x71] = 0x00;
    dev->pci_conf[0x72] = dev->pci_conf[0x73] = 0x00;
    dev->pci_conf[0x74] = dev->pci_conf[0x75] = 0x00;
    dev->pci_conf[0x76] = dev->pci_conf[0x77] = 0x00;
    dev->pci_conf[0x78] = dev->pci_conf[0x79] = 0x00;
    dev->pci_conf[0x7a] = dev->pci_conf[0x7b] = 0x00;
    dev->pci_conf[0x80] = dev->pci_conf[0x81] = 0x00;
    dev->pci_conf[0x82] = dev->pci_conf[0x83] = 0x00;
    dev->pci_conf[0x84] = dev->pci_conf[0x85] = 0x00;
    dev->pci_conf[0x86] = dev->pci_conf[0x87] = 0x00;
    dev->pci_conf[0x88] = dev->pci_conf[0x89] = 0x00;
    dev->pci_conf[0x8a] = dev->pci_conf[0x8b] = 0x00;
    dev->pci_conf[0x90] = dev->pci_conf[0x91] = 0x00;
    dev->pci_conf[0x92] = dev->pci_conf[0x93] = 0x00;
    dev->pci_conf[0x94] = dev->pci_conf[0x95] = 0x00;
    dev->pci_conf[0x96] = dev->pci_conf[0x97] = 0x00;
    dev->pci_conf[0x98] = dev->pci_conf[0x99] = 0x00;
    dev->pci_conf[0x9a] = dev->pci_conf[0x9b] = 0x00;
    dev->pci_conf[0x9c] = dev->pci_conf[0x9d] = 0x00;
    dev->pci_conf[0x9e] = dev->pci_conf[0x9f] = 0xff;
    dev->pci_conf[0xa0] = 0xff;
    dev->pci_conf[0xa1] = 0x00;
    dev->pci_conf[0xa2] = 0xff;
    dev->pci_conf[0xa3] = 0x00;

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    sis_5581_shadow_recalc(dev);

    sis_5581_trap_update(dev);

    sis_5581_smram_recalc(dev);
}

static void
sis_5581_host_to_pci_close(void *priv)
{
    sis_5581_host_to_pci_t *dev = (sis_5581_host_to_pci_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
sis_5581_host_to_pci_init(UNUSED(const device_t *info))
{
    sis_5581_host_to_pci_t *dev = (sis_5581_host_to_pci_t *) calloc(1, sizeof(sis_5581_host_to_pci_t));
    uint32_t total_mem = mem_size << 10;
    ram_bank_t *rb;

    dev->sis = device_get_common_priv();

    /* Calculate the physical RAM banks. */
    for (uint8_t i = 0; i < 3; i++) {
        rb = &(dev->ram_banks[i]);
        uint32_t size = 0x00000000;
        uint8_t  index = 0;
        for (int8_t j = 6; j >= 0; j--) {
            uint32_t *bs = &(bank_sizes[j]);
            if (*bs <= total_mem) {
                size = *bs;
                index = j;
                break;
            }
        }
        if (size != 0x00000000) {
            rb->installed = 1;
            rb->code = bank_codes[index];
            rb->phys_size = size;
            total_mem -= size;
        } else
            rb->installed = 0;
    }

    /* SMRAM */
    dev->smram = smram_add();

    sis_5581_host_to_pci_reset(dev);

    return dev;
}

const device_t sis_5581_h2p_device = {
    .name          = "SiS 5581 Host to PCI bridge",
    .internal_name = "sis_5581_host_to_pci",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = sis_5581_host_to_pci_init,
    .close         = sis_5581_host_to_pci_close,
    .reset         = sis_5581_host_to_pci_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
