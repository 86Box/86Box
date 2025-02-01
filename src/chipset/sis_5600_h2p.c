/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS (5)600 Host to PCI bridge.
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

#ifdef ENABLE_SIS_5600_HOST_TO_PCI_LOG
int sis_5600_host_to_pci_do_log = ENABLE_SIS_5600_HOST_TO_PCI_LOG;

static void
sis_5600_host_to_pci_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5600_host_to_pci_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5600_host_to_pci_log(fmt, ...)
#endif

typedef struct {
    uint8_t  installed;
    uint8_t  code;
    uint32_t phys_size;
} ram_bank_t;

typedef struct sis_5600_host_to_pci_t {
    uint8_t            pci_conf[256];
    uint8_t            states[7];

    ram_bank_t         ram_banks[3];

    sis_55xx_common_t *sis;

    smram_t           *smram;

    agpgart_t         *agpgart;
} sis_5600_host_to_pci_t;

static uint8_t  bank_codes[7] = { 0x00, 0x20, 0x24, 0x22, 0x26, 0x2a, 0x2b };

static uint32_t bank_sizes[7] = { 0x00800000,      /*   8 MB */
                                  0x01000000,      /*  16 MB */
                                  0x02000000,      /*  32 MB */
                                  0x04000000,      /*  64 MB */
                                  0x08000000,      /* 128 MB */
                                  0x10000000,      /* 256 MB */
                                  0x20000000 };    /* 512 MB */

static void
sis_5600_shadow_recalc(sis_5600_host_to_pci_t *dev)
{
    int      state;
    uint32_t base;

    for (uint8_t i = 0; i < 8; i++) {
        base = 0x000c0000 + (i << 14);
        state = (dev->pci_conf[0x70] & (1 << i)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
        state |= (dev->pci_conf[0x72] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
        if (((dev->pci_conf[0x70] ^ dev->states[0]) & (1 << i)) ||
            ((dev->pci_conf[0x72] ^ dev->states[2]) & (1 << i))) {
            mem_set_mem_state_both(base, 0x4000, state);
            sis_5600_host_to_pci_log("%08X-%08X\n", base, base + 0x3fff);
        }
    }

    for (uint8_t i = 0; i < 4; i++) {
        base = 0x000e0000 + (i << 14);
        state = (dev->pci_conf[0x71] & (1 << i)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
        state |= (dev->pci_conf[0x73] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
        if (((dev->pci_conf[0x71] ^ dev->states[1]) & (1 << i)) ||
            ((dev->pci_conf[0x73] ^ dev->states[3]) & (1 << i))) {
            mem_set_mem_state_both(base, 0x4000, state);
            sis_5600_host_to_pci_log("%08X-%08X\n", base, base + 0x3fff);
        }
    }

    base = 0x000f0000;
    state = (dev->pci_conf[0x71] & (1 << 4)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    state |= (dev->pci_conf[0x73] & (1 << 4)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
    if (((dev->pci_conf[0x71] ^ dev->states[1]) & (1 << 4)) ||
        ((dev->pci_conf[0x73] ^ dev->states[3]) & (1 << 4))) {
        mem_set_mem_state_both(base, 0x10000, state);
        sis_5600_host_to_pci_log("%08X-%08X\n", base, base + 0xffff);
    }

    for (uint8_t i = 0; i < 4; i++)
        dev->states[i] = dev->pci_conf[0x70 + i];

    flushmmucache_nopc();
}

static void
sis_5600_smram_recalc(sis_5600_host_to_pci_t *dev)
{
    smram_disable_all();

    switch (dev->pci_conf[0x6a] >> 6) {
        case 0:
            smram_enable(dev->smram, 0x000e0000, 0x000e0000, 0x8000, dev->pci_conf[0x6a] & 0x10, 1);
            break;
        case 1:
            smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x8000, dev->pci_conf[0x6a] & 0x10, 1);
            break;
        case 2:
            smram_enable(dev->smram, 0x000e0000, 0x000b0000, 0x8000, dev->pci_conf[0x6a] & 0x10, 1);
            break;
        case 3:
            smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x10000, dev->pci_conf[0x6a] & 0x10, 1);
            break;

        default:
            break;
    }

    flushmmucache();
}

static void
sis_5600_mask_bar(uint8_t *regs, void *agpgart)
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
sis_5600_host_to_pci_write(int addr, uint8_t val, void *priv)
{
    sis_5600_host_to_pci_t *dev = (sis_5600_host_to_pci_t *) priv;

    sis_5600_host_to_pci_log("SiS 5600 H2P: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

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
            dev->pci_conf[addr] = (dev->pci_conf[addr] & ~(val & 0x70)) | (val & 0x01);
            break;

        case 0x0d: /* Master latency timer */
        case 0x50 ... 0x5a:
        case 0x64 ... 0x69:
        case 0x6b ... 0x6c:
        case 0x74 ... 0x75:
        case 0x77 ... 0x80:
        case 0x82 ... 0x8f:
        case 0x97 ... 0x9b:
        case 0xc8 ... 0xcb:
        case 0xd4 ... 0xd8:
        case 0xda:
        case 0xe0:
        case 0xe2 ... 0xe3:
            dev->pci_conf[addr] = val;
            break;

        case 0x12:
            dev->pci_conf[addr] = val & 0xc0;
            sis_5600_mask_bar(dev->pci_conf, dev->agpgart);
            break;
        case 0x13:
            dev->pci_conf[addr] = val;
            sis_5600_mask_bar(dev->pci_conf, dev->agpgart);
            break;

        case 0x60 ... 0x62:
            dev->pci_conf[addr] = dev->ram_banks[addr & 0x0f].code | 0xc0;
            break;

        case 0x63:
            dev->pci_conf[addr] = dev->ram_banks[0].installed |
                                  (dev->ram_banks[1].installed << 1) |
                                  (dev->ram_banks[2].installed << 2);
            break;

        case 0x6a:
            dev->pci_conf[addr] = val;
            sis_5600_smram_recalc(dev);
            break;

        case 0x70 ... 0x73:
            dev->pci_conf[addr] = val;
            sis_5600_shadow_recalc(dev);
            break;

        case 0x91 ... 0x93:
            dev->pci_conf[addr] = val;
            sis_5600_mask_bar(dev->pci_conf, dev->agpgart);
            break;
        case 0x94:
            dev->pci_conf[addr] = val & 0x7f;
            sis_5600_mask_bar(dev->pci_conf, dev->agpgart);
            break;
    }
}

uint8_t
sis_5600_host_to_pci_read(int addr, void *priv)
{
    const sis_5600_host_to_pci_t *dev = (sis_5600_host_to_pci_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->pci_conf[addr];

    sis_5600_host_to_pci_log("SiS 5600 H2P: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5600_host_to_pci_reset(void *priv)
{
    sis_5600_host_to_pci_t *dev = (sis_5600_host_to_pci_t *) priv;

    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x00;
    dev->pci_conf[0x03] = 0x56;
    dev->pci_conf[0x04] = 0x05;
    dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = 0x10;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = 0x10;
    dev->pci_conf[0x09] = dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x0c] = 0x00;
    dev->pci_conf[0x0d] = 0xff;
    dev->pci_conf[0x0e] = 0x80;
    dev->pci_conf[0x0f] = 0x00;
    dev->pci_conf[0x10] = dev->pci_conf[0x11] = 0x00;
    dev->pci_conf[0x12] = dev->pci_conf[0x13] = 0x00;
    dev->pci_conf[0x34] = 0xc0;
    dev->pci_conf[0x50] = dev->pci_conf[0x51] = 0x02;
    dev->pci_conf[0x52] = dev->pci_conf[0x53] = 0x00;
    dev->pci_conf[0x54] = dev->pci_conf[0x55] = 0x00;
    dev->pci_conf[0x56] = dev->pci_conf[0x57] = 0x00;
    dev->pci_conf[0x58] = dev->pci_conf[0x59] = 0x00;
    dev->pci_conf[0x5a] = 0x00;
    dev->pci_conf[0x60] = dev->pci_conf[0x61] = 0x00;
    dev->pci_conf[0x62] = 0x00;
    dev->pci_conf[0x63] = 0xff;
    dev->pci_conf[0x64] = dev->pci_conf[0x65] = 0x00;
    dev->pci_conf[0x66] = dev->pci_conf[0x67] = 0x00;
    dev->pci_conf[0x68] = dev->pci_conf[0x69] = 0x00;
    dev->pci_conf[0x6a] = dev->pci_conf[0x6b] = 0x00;
    dev->pci_conf[0x6c] = 0x00;
    dev->pci_conf[0x70] = dev->pci_conf[0x71] = 0x00;
    dev->pci_conf[0x72] = dev->pci_conf[0x73] = 0x00;
    dev->pci_conf[0x74] = dev->pci_conf[0x75] = 0x00;
    dev->pci_conf[0x77] = 0x00;
    dev->pci_conf[0x78] = dev->pci_conf[0x79] = 0x00;
    dev->pci_conf[0x7a] = dev->pci_conf[0x7b] = 0x00;
    dev->pci_conf[0x7c] = dev->pci_conf[0x7d] = 0x00;
    dev->pci_conf[0x7e] = dev->pci_conf[0x7f] = 0x00;
    dev->pci_conf[0x80] = 0x00;
    dev->pci_conf[0x82] = dev->pci_conf[0x83] = 0x00;
    dev->pci_conf[0x84] = dev->pci_conf[0x85] = 0xff;
    dev->pci_conf[0x86] = 0xff;
    dev->pci_conf[0x87] = 0x00;
    dev->pci_conf[0x88] = dev->pci_conf[0x89] = 0x00;
    dev->pci_conf[0x8a] = dev->pci_conf[0x8b] = 0x00;
    dev->pci_conf[0x8c] = 0x00;
    dev->pci_conf[0x8d] = 0x62;
    dev->pci_conf[0x8e] = dev->pci_conf[0x8f] = 0x00;
    dev->pci_conf[0x90] = dev->pci_conf[0x91] = 0x00;
    dev->pci_conf[0x92] = dev->pci_conf[0x93] = 0x00;
    dev->pci_conf[0x94] = dev->pci_conf[0x97] = 0x00;
    dev->pci_conf[0x98] = dev->pci_conf[0x99] = 0x00;
    dev->pci_conf[0x9a] = dev->pci_conf[0x9b] = 0x00;
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
    dev->pci_conf[0xd8] = dev->pci_conf[0xda] = 0x00;
    dev->pci_conf[0xe0] = 0x00;
    dev->pci_conf[0xe2] = dev->pci_conf[0xe3] = 0x00;

    sis_5600_mask_bar(dev->pci_conf, dev->agpgart);

    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    sis_5600_shadow_recalc(dev);

    sis_5600_smram_recalc(dev);
}

static void
sis_5600_host_to_pci_close(void *priv)
{
    sis_5600_host_to_pci_t *dev = (sis_5600_host_to_pci_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
sis_5600_host_to_pci_init(UNUSED(const device_t *info))
{
    sis_5600_host_to_pci_t *dev = (sis_5600_host_to_pci_t *) calloc(1, sizeof(sis_5600_host_to_pci_t));
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

    device_add(&sis_5xxx_agp_device);
    dev->agpgart = device_add(&agpgart_device);

    sis_5600_host_to_pci_reset(dev);

    return dev;
}

const device_t sis_5600_h2p_device = {
    .name          = "SiS (5)600 Host to PCI bridge",
    .internal_name = "sis_5600_host_to_pci",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = sis_5600_host_to_pci_init,
    .close         = sis_5600_host_to_pci_close,
    .reset         = sis_5600_host_to_pci_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
