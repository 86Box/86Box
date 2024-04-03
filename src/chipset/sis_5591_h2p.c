/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5591 Host to PCI bridge.
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
#include <86box/agpgart.h>

#ifdef ENABLE_SIS_5591_HOST_TO_PCI_LOG
int sis_5591_host_to_pci_do_log = ENABLE_SIS_5591_HOST_TO_PCI_LOG;

static void
sis_5591_host_to_pci_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5591_host_to_pci_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5591_host_to_pci_log(fmt, ...)
#endif

typedef struct {
    uint8_t  installed;
    uint8_t  code;
    uint32_t phys_size;
} ram_bank_t;

typedef struct sis_5591_host_to_pci_t {
    uint8_t            pci_conf[256];

    uint8_t            states[7];
    uint8_t            states_bus[7];

    ram_bank_t         ram_banks[3];

    sis_55xx_common_t *sis;

    smram_t           *smram;

    agpgart_t         *agpgart;
} sis_5591_host_to_pci_t;

static uint8_t  bank_codes[6] = { 0x00, 0x20, 0x24, 0x22, 0x26, 0x2a };

static uint32_t bank_sizes[6] = { 0x00800000,      /*   8 MB */
                                  0x01000000,      /*  16 MB */
                                  0x02000000,      /*  32 MB */
                                  0x04000000,      /*  64 MB */
                                  0x08000000,      /* 128 MB */
                                  0x10000000 };    /* 256 MB */

static void
sis_5591_shadow_recalc(sis_5591_host_to_pci_t *dev)
{
    uint32_t base;
    uint32_t state;
    uint8_t  val;

    for (uint8_t i = 0x70; i <= 0x76; i++) {
        if (i == 0x76) {
            val = dev->pci_conf[i];
            if ((dev->states[i & 0x0f] ^ val) & 0xa0) {
                state = (val & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_cpu_both(0xf0000, 0x10000, state);
                sis_5591_host_to_pci_log("000F0000-000FFFFF\n");

                dev->states[i & 0x0f] = val;
            }

            if (!(dev->pci_conf[0x76] & 0x08))
                val &= 0x5f;
            if ((dev->states_bus[i & 0x0f] ^ val) & 0xa0) {
                state = (val & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_bus_both(0xf0000, 0x10000, state);
                sis_5591_host_to_pci_log("000F0000-000FFFFF\n");

                dev->states_bus[i & 0x0f] = val;
            }
        } else {
            base = ((i & 0x07) << 15) + 0xc0000;

            val = dev->pci_conf[i];
            if ((dev->states[i & 0x0f] ^ val) & 0xa0) {
                state = (val & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_cpu_both(base, 0x4000, state);
                sis_5591_host_to_pci_log("%08X-%08X\n", base, base + 0x3fff);

                dev->states[i & 0x0f] = (dev->states[i & 0x0f] & 0x0f) | (val & 0xf0);
            }
            if ((dev->states[i & 0x0f] ^ val) & 0x0a) {
                state = (val & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (val & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_cpu_both(base + 0x4000, 0x4000, state);
                sis_5591_host_to_pci_log("%08X-%08X\n", base + 0x4000, base + 0x7fff);

                dev->states[i & 0x0f] = (dev->states[i & 0x0f] & 0xf0) | (val & 0x0f);
            }

            if (!(dev->pci_conf[0x76] & 0x08))
                val &= 0x55;
            if ((dev->states_bus[i & 0x0f] ^ val) & 0xa0) {
                state = (val & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_bus_both(base, 0x4000, state);
                sis_5591_host_to_pci_log("%08X-%08X\n", base, base + 0x3fff);

                dev->states_bus[i & 0x0f] = (dev->states_bus[i & 0x0f] & 0x0f) | (val & 0xf0);
            }
            if ((dev->states_bus[i & 0x0f] ^ val) & 0x0a) {
                state = (val & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (val & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_bus_both(base + 0x4000, 0x4000, state);
                sis_5591_host_to_pci_log("%08X-%08X\n", base + 0x4000, base + 0x7fff);

                dev->states_bus[i & 0x0f] = (dev->states_bus[i & 0x0f] & 0xf0) | (val & 0x0f);
            }
        }
    }

    flushmmucache_nopc();
}

static void
sis_5591_smram_recalc(sis_5591_host_to_pci_t *dev)
{
    smram_disable_all();

    switch (dev->pci_conf[0x68] >> 6) {
        case 0:
            smram_enable(dev->smram, 0x000e0000, 0x000e0000, 0x8000, dev->pci_conf[0x68] & 0x10, 1);
            break;
        case 1:
            smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x8000, dev->pci_conf[0x68] & 0x10, 1);
            break;
        case 2:
            smram_enable(dev->smram, 0x000e0000, 0x000b0000, 0x8000, dev->pci_conf[0x68] & 0x10, 1);
            break;
        case 3:
            smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x10000, dev->pci_conf[0x68] & 0x10, 1);
            break;

        default:
            break;
    }

    flushmmucache();
}

static void
sis_5591_mask_bar(uint8_t *regs, void *agpgart)
{
    uint32_t bar;
    uint32_t sizes[8] = { 0x00400000, 0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000,
                          0x10000000, 0x00000000 } ;

    /* Make sure the aperture's base is aligned to its size. */
    bar = (regs[0x13] << 24) | (regs[0x12] << 16);
    bar &= (sizes[(regs[0x94] >> 4) & 0x07] | 0xf0000000);
    regs[0x12] = (bar >> 16) & 0xff;
    regs[0x13] = (bar >> 24) & 0xff;

    if (!agpgart)
        return;

    /* Map aperture and GART. */
    agpgart_set_aperture(agpgart,
                         bar,
                         sizes[(regs[0x94] >> 4) & 0x07],
                         !!(regs[0x94] & 0x02));
    if (regs[0x94] & 0x01)
        agpgart_set_gart(agpgart, (regs[0x91] << 8) | (regs[0x92] << 16) | (regs[0x93] << 24));
    else
        agpgart_set_gart(agpgart, 0x00000000);
}

void
sis_5591_host_to_pci_write(int addr, uint8_t val, void *priv)
{
    sis_5591_host_to_pci_t *dev = (sis_5591_host_to_pci_t *) priv;

    sis_5591_host_to_pci_log("SiS 5591 H2P: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    switch (addr) {
        default:
            break;

        case 0x04: /* Command - Low Byte */
            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0xfd) | (val & 0x02);
            break;
        case 0x05: /* Command - High Byte */
            dev->pci_conf[addr] = val & 0x03;
            break;

        case 0x07: /* Status - High Byte */
            dev->pci_conf[addr] &= ~(val & 0xf0);
            break;

        case 0x12:
            dev->pci_conf[addr] = val & 0xc0;
            sis_5591_mask_bar(dev->pci_conf, dev->agpgart);
            break;
        case 0x13:
            dev->pci_conf[addr] = val;
            sis_5591_mask_bar(dev->pci_conf, dev->agpgart);
            break;

        case 0x51:
            dev->pci_conf[addr] = val;
            cpu_cache_ext_enabled = !!(val & 0x80);
            cpu_update_waitstates();
            break;

        case 0x60 ... 0x62:
            dev->pci_conf[addr] = dev->ram_banks[addr & 0x0f].code | 0xc0;
            break;

        case 0x63:
            dev->pci_conf[addr] = dev->ram_banks[0].installed |
                                  (dev->ram_banks[1].installed << 1) |
                                  (dev->ram_banks[2].installed << 2);
            break;

        case 0x68:
            dev->pci_conf[addr] = val;
            sis_5591_smram_recalc(dev);
            break;

        case 0x70 ... 0x75:
            dev->pci_conf[addr] = val & 0xee;
            sis_5591_shadow_recalc(dev);
            break;
        case 0x76:
            dev->pci_conf[addr] = val & 0xe8;
            sis_5591_shadow_recalc(dev);
            break;

        case 0x0d: /* Master latency timer */
        case 0x50:
        case 0x52:
        case 0x54 ... 0x5a:
        case 0x5c ... 0x5f:
        case 0x64 ... 0x65:
        case 0x69 ... 0x6c:
        case 0x77 ... 0x7b:
        case 0x80 ... 0x8d:
        case 0x90:
        case 0x97 ... 0xab:
        case 0xb0:
        case 0xc8 ... 0xcb:
        case 0xd4 ... 0xda:
        case 0xe0 ... 0xe3:
        case 0xef:
            dev->pci_conf[addr] = val;
            break;

        case 0x91 ... 0x93:
            dev->pci_conf[addr] = val;
            sis_5591_mask_bar(dev->pci_conf, dev->agpgart);
            break;
        case 0x94:
            dev->pci_conf[addr] = val & 0x7f;
            sis_5591_mask_bar(dev->pci_conf, dev->agpgart);
            break;

        case 0xb2:
            dev->pci_conf[addr] &= ~(val & 0x01);
            break;
    }
}

uint8_t
sis_5591_host_to_pci_read(int addr, void *priv)
{
    const sis_5591_host_to_pci_t *dev = (sis_5591_host_to_pci_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->pci_conf[addr];

    sis_5591_host_to_pci_log("SiS 5591 H2P: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5591_host_to_pci_reset(void *priv)
{
    sis_5591_host_to_pci_t *dev = (sis_5591_host_to_pci_t *) priv;

    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x91;
    dev->pci_conf[0x03] = 0x55;
    dev->pci_conf[0x04] = 0x05;
    dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = 0x10;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = 0x02;
    dev->pci_conf[0x09] = dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x0c] = 0x00;
    dev->pci_conf[0x0d] = 0xff;
    dev->pci_conf[0x0e] = 0x80;
    dev->pci_conf[0x0f] = 0x00;
    dev->pci_conf[0x10] = dev->pci_conf[0x11] = 0x00;
    dev->pci_conf[0x12] = dev->pci_conf[0x13] = 0x00;
    dev->pci_conf[0x34] = 0xc0;
    dev->pci_conf[0x50] = 0x00;
    dev->pci_conf[0x51] = 0x18;
    dev->pci_conf[0x52] = dev->pci_conf[0x54] = 0x00;
    dev->pci_conf[0x55] = 0x0e;
    dev->pci_conf[0x56] = 0x40;
    dev->pci_conf[0x57] = 0x00;
    dev->pci_conf[0x58] = 0x50;
    dev->pci_conf[0x59] = dev->pci_conf[0x5a] = 0x00;
    dev->pci_conf[0x5c] = dev->pci_conf[0x5d] = 0x00;
    dev->pci_conf[0x5e] = dev->pci_conf[0x5f] = 0x00;
    dev->pci_conf[0x60] = dev->pci_conf[0x61] = 0x00;
    dev->pci_conf[0x62] = 0x00;
    dev->pci_conf[0x63] = 0xff;
    dev->pci_conf[0x64] = dev->pci_conf[0x65] = 0x00;
    dev->pci_conf[0x68] = dev->pci_conf[0x69] = 0x00;
    dev->pci_conf[0x6a] = dev->pci_conf[0x6b] = 0x00;
    dev->pci_conf[0x6c] = 0x00;
    dev->pci_conf[0x70] = dev->pci_conf[0x71] = 0x00;
    dev->pci_conf[0x72] = dev->pci_conf[0x73] = 0x00;
    dev->pci_conf[0x74] = dev->pci_conf[0x75] = 0x00;
    dev->pci_conf[0x76] = dev->pci_conf[0x77] = 0x00;
    dev->pci_conf[0x78] = dev->pci_conf[0x79] = 0x00;
    dev->pci_conf[0x7a] = dev->pci_conf[0x7b] = 0x00;
    dev->pci_conf[0x80] = dev->pci_conf[0x81] = 0x00;
    dev->pci_conf[0x82] = dev->pci_conf[0x83] = 0x00;
    dev->pci_conf[0x84] = dev->pci_conf[0x85] = 0xff;
    dev->pci_conf[0x86] = 0xff;
    dev->pci_conf[0x87] = 0x00;
    dev->pci_conf[0x88] = dev->pci_conf[0x89] = 0x00;
    dev->pci_conf[0x8a] = dev->pci_conf[0x8b] = 0x00;
    dev->pci_conf[0x8c] = dev->pci_conf[0x8d] = 0x00;
    dev->pci_conf[0x90] = dev->pci_conf[0x91] = 0x00;
    dev->pci_conf[0x92] = dev->pci_conf[0x93] = 0x00;
    dev->pci_conf[0x94] = dev->pci_conf[0x97] = 0x00;
    dev->pci_conf[0x98] = dev->pci_conf[0x99] = 0x00;
    dev->pci_conf[0x9a] = dev->pci_conf[0x9b] = 0x00;
    dev->pci_conf[0x9c] = dev->pci_conf[0x9d] = 0x00;
    dev->pci_conf[0x9e] = dev->pci_conf[0x9f] = 0x00;
    dev->pci_conf[0xa0] = dev->pci_conf[0xa1] = 0x00;
    dev->pci_conf[0xa2] = dev->pci_conf[0xa3] = 0x00;
    dev->pci_conf[0xa4] = dev->pci_conf[0xa5] = 0x00;
    dev->pci_conf[0xa6] = dev->pci_conf[0xa7] = 0x00;
    dev->pci_conf[0xa8] = dev->pci_conf[0xa9] = 0x00;
    dev->pci_conf[0xaa] = dev->pci_conf[0xab] = 0x00;
    dev->pci_conf[0xb0] = dev->pci_conf[0xb2] = 0x00;
    dev->pci_conf[0xc0] = 0x02;
    dev->pci_conf[0xc1] = 0x00;
    dev->pci_conf[0xc2] = 0x10;
    dev->pci_conf[0xc3] = 0x00;
    dev->pci_conf[0xc4] = 0x03;
    dev->pci_conf[0xc5] = 0x02;
    dev->pci_conf[0xc6] = 0x00;
    dev->pci_conf[0xc7] = 0x1f;
    dev->pci_conf[0xc8] = dev->pci_conf[0xc9] = 0x00;
    dev->pci_conf[0xca] = dev->pci_conf[0xcb] = 0x00;
    dev->pci_conf[0xd4] = dev->pci_conf[0xd5] = 0x00;
    dev->pci_conf[0xd6] = dev->pci_conf[0xd7] = 0x00;
    dev->pci_conf[0xd8] = dev->pci_conf[0xd9] = 0x00;
    dev->pci_conf[0xda] = 0x00;
    dev->pci_conf[0xe0] = dev->pci_conf[0xe1] = 0x00;
    dev->pci_conf[0xe2] = dev->pci_conf[0xe3] = 0x00;
    dev->pci_conf[0xef] = 0x00;

    sis_5591_mask_bar(dev->pci_conf, dev->agpgart);

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    sis_5591_shadow_recalc(dev);

    sis_5591_smram_recalc(dev);
}

static void
sis_5591_host_to_pci_close(void *priv)
{
    sis_5591_host_to_pci_t *dev = (sis_5591_host_to_pci_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
sis_5591_host_to_pci_init(UNUSED(const device_t *info))
{
    sis_5591_host_to_pci_t *dev = (sis_5591_host_to_pci_t *) calloc(1, sizeof(sis_5591_host_to_pci_t));
    uint32_t total_mem = mem_size << 10;
    ram_bank_t *rb;

    dev->sis = device_get_common_priv();

    /* Calculate the physical RAM banks. */
    for (uint8_t i = 0; i < 3; i++) {
        rb = &(dev->ram_banks[i]);
        uint32_t size = 0x00000000;
        uint8_t  index = 0;
        for (int8_t j = 5; j >= 0; j--) {
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

    device_add(&sis_5xxx_agp_device);
    dev->agpgart = device_add(&agpgart_device);

    sis_5591_host_to_pci_reset(dev);

    return dev;
}

const device_t sis_5591_h2p_device = {
    .name          = "SiS 5591 Host to PCI bridge",
    .internal_name = "sis_5591_host_to_pci",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = sis_5591_host_to_pci_init,
    .close         = sis_5591_host_to_pci_close,
    .reset         = sis_5591_host_to_pci_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
