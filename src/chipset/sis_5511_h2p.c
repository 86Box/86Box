/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5511 Host to PCI bridge.
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
#include <86box/spd.h>
#include <86box/sis_55xx.h>
#include <86box/chipset.h>
#include <86box/usb.h>
#include <86box/agpgart.h>

#ifdef ENABLE_SIS_5511_HOST_TO_PCI_LOG
int sis_5511_host_to_pci_do_log = ENABLE_SIS_5511_HOST_TO_PCI_LOG;

static void
sis_5511_host_to_pci_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5511_host_to_pci_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5511_host_to_pci_log(fmt, ...)
#endif

typedef struct sis_5511_host_to_pci_t {
    uint8_t            pci_conf[256];
    uint8_t            states[7];

    uint8_t            slic_regs[4096];

    sis_55xx_common_t *sis;

    smram_t           *smram;

    mem_mapping_t      slic_mapping;
} sis_5511_host_to_pci_t;

static void
sis_5511_shadow_recalc(sis_5511_host_to_pci_t *dev)
{
    int      state;
    uint32_t base;

    for (uint8_t i = 0x80; i <= 0x86; i++) {
        if (i == 0x86) {
            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(0xf0000, 0x10000, state);
                sis_5511_host_to_pci_log("000F0000-000FFFFF\n");
            }
        } else {
            base = ((i & 0x07) << 15) + 0xc0000;

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base, 0x4000, state);
                sis_5511_host_to_pci_log("%08X-%08X\n", base, base + 0x3fff);
            }

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0x0a) {
                state = (dev->pci_conf[i] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base + 0x4000, 0x4000, state);
                sis_5511_host_to_pci_log("%08X-%08X\n", base + 0x4000, base + 0x7fff);
            }
        }

        dev->states[i & 0x0f] = dev->pci_conf[i];
    }

    flushmmucache_nopc();
}

static void
sis_5511_smram_recalc(sis_5511_host_to_pci_t *dev)
{
    smram_disable_all();

    switch (dev->pci_conf[0x65] >> 6) {
        case 0:
            smram_enable(dev->smram, 0x000e0000, 0x000e0000, 0x8000, dev->pci_conf[0x65] & 0x10, 1);
            break;
        case 1:
            smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x8000, dev->pci_conf[0x65] & 0x10, 1);
            break;
        case 2:
            smram_enable(dev->smram, 0x000e0000, 0x000b0000, 0x8000, dev->pci_conf[0x65] & 0x10, 1);
            break;

        default:
            break;
    }

    flushmmucache();
}

void
sis_5511_host_to_pci_write(int addr, uint8_t val, void *priv)
{
    sis_5511_host_to_pci_t *dev = (sis_5511_host_to_pci_t *) priv;

    sis_5511_host_to_pci_log("SiS 5511 H2P: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    switch (addr) {
        default:
            break;

        case 0x07: /* Status - High Byte */
            dev->pci_conf[addr] &= 0xb0;
            break;

        case 0x50:
            dev->pci_conf[addr]   = val;
            cpu_cache_ext_enabled = !!(val & 0x40);
            cpu_update_waitstates();
            break;

        case 0x51:
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x52:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0x53:
        case 0x54:
            dev->pci_conf[addr] = val;
            break;

        case 0x55:
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x56 ... 0x59:
            dev->pci_conf[addr] = val;
            break;

        case 0x5a:
            /* TODO: Fast Gate A20 Emulation and Fast Reset Emulation on the KBC.
                     The former (bit 7) means the chipset intercepts D1h to 64h and 00h to 60h.
                     The latter (bit 6) means the chipset intercepts all odd FXh to 64h.
                     Bit 5 sets fast reset latency. This should be fixed on the other SiS
                     chipsets as well. */
            dev->pci_conf[addr] = val;
            break;

        case 0x5b:
            dev->pci_conf[addr] = val & 0xf7;
            break;

        case 0x5c:
            dev->pci_conf[addr] = val & 0xcf;
            break;

        case 0x5d:
            dev->pci_conf[addr] = val;
            break;

        case 0x5e:
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x5f:
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x60:
            dev->pci_conf[addr] = val & 0x3e;
            if ((dev->pci_conf[0x68] & 1) && (val & 2)) {
                smi_raise();
                dev->pci_conf[0x69] |= 1;
            }
            break;

        case 0x61 ... 0x64:
            dev->pci_conf[addr] = val;
            break;

        case 0x65:
            dev->pci_conf[addr] = val & 0xd0;
            sis_5511_smram_recalc(dev);
            break;

        case 0x66:
            dev->pci_conf[addr] = val & 0x7f;
            break;

        case 0x67:
        case 0x68:
            dev->pci_conf[addr] = val;
            break;

        case 0x69:
            dev->pci_conf[addr] &= val;
            break;

        case 0x6a ... 0x6e:
            dev->pci_conf[addr] = val;
            break;

        case 0x6f:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0x70: /* DRAM Bank Register 0-0 */
        case 0x72: /* DRAM Bank Register 0-1 */
        case 0x74: /* DRAM Bank Register 1-0 */
        case 0x76: /* DRAM Bank Register 1-1 */
        case 0x78: /* DRAM Bank Register 2-0 */
        case 0x7a: /* DRAM Bank Register 2-1 */
        case 0x7c: /* DRAM Bank Register 3-0 */
        case 0x7e: /* DRAM Bank Register 3-1 */
            spd_write_drbs(dev->pci_conf, 0x70, 0x7e, 0x82);
            break;
 
        case 0x71: /* DRAM Bank Register 0-0 */
            dev->pci_conf[addr] = val;
            break;

        case 0x75: /* DRAM Bank Register 1-0 */
        case 0x79: /* DRAM Bank Register 2-0 */
        case 0x7d: /* DRAM Bank Register 3-0 */
            dev->pci_conf[addr] = val & 0x7f;
            break;

        case 0x73: /* DRAM Bank Register 0-1 */
        case 0x77: /* DRAM Bank Register 1-1 */
        case 0x7b: /* DRAM Bank Register 2-1 */
        case 0x7f: /* DRAM Bank Register 3-1 */
            dev->pci_conf[addr] = val & 0x83;
            break;

        case 0x80 ... 0x85:
            dev->pci_conf[addr] = val & 0xee;
            sis_5511_shadow_recalc(dev);
            break;
        case 0x86:
            dev->pci_conf[addr] = val & 0xe8;
            sis_5511_shadow_recalc(dev);
            break;

        case 0x90 ... 0x93: /* 5512 General Purpose Register Index */
            dev->pci_conf[addr] = val;
            break;
    }
}

uint8_t
sis_5511_host_to_pci_read(int addr, void *priv)
{
    const sis_5511_host_to_pci_t *dev = (sis_5511_host_to_pci_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->pci_conf[addr];

    sis_5511_host_to_pci_log("SiS 5511 H2P: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5511_slic_write(uint32_t addr, uint8_t val, void *priv)
{
    sis_5511_host_to_pci_t *dev = (sis_5511_host_to_pci_t *) priv;

    addr &= 0x00000fff;

    switch (addr) {
        case 0x00000000:
        case 0x00000008:    /* 0x00000008 is a SiS 5512 register. */
            dev->slic_regs[addr] = val;
            break;
        case 0x00000010:
        case 0x00000018:
        case 0x00000028:
        case 0x00000038:
            dev->slic_regs[addr] = val & 0x01;
            break;
        case 0x00000030:
            dev->slic_regs[addr] = val & 0x0f;
            mem_mapping_set_addr(&dev->slic_mapping,
                                 (((uint32_t) (val & 0x0f)) << 28) | 0x0fc00000, 0x00001000);
            break;
    }
}

static uint8_t
sis_5511_slic_read(uint32_t addr, void *priv)
{
    sis_5511_host_to_pci_t *dev = (sis_5511_host_to_pci_t *) priv;
    uint8_t ret = 0xff;

    addr &= 0x00000fff;

    switch (addr) {
        case 0x00000008:    /* 0x00000008 is a SiS 5512 register. */
            ret = dev->slic_regs[addr];
            break;
    }

    return ret;
}

static void
sis_5511_host_to_pci_reset(void *priv)
{
    sis_5511_host_to_pci_t *dev = (sis_5511_host_to_pci_t *) priv;

    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x11;
    dev->pci_conf[0x03] = 0x55;
    dev->pci_conf[0x04] = 0x07;
    dev->pci_conf[0x05] = dev->pci_conf[0x06] = 0x00;
    dev->pci_conf[0x07]                       = 0x02;
    dev->pci_conf[0x08]                       = 0x00;
    dev->pci_conf[0x09] = dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b]                       = 0x06;
    dev->pci_conf[0x50] = dev->pci_conf[0x51] = 0x00;
    dev->pci_conf[0x52]                       = 0x20;
    dev->pci_conf[0x53] = dev->pci_conf[0x54] = 0x00;
    dev->pci_conf[0x55] = dev->pci_conf[0x56] = 0x00;
    dev->pci_conf[0x57] = dev->pci_conf[0x58] = 0x00;
    dev->pci_conf[0x59] = dev->pci_conf[0x5a] = 0x00;
    dev->pci_conf[0x5b] = dev->pci_conf[0x5c] = 0x00;
    dev->pci_conf[0x5d] = dev->pci_conf[0x5e] = 0x00;
    dev->pci_conf[0x5f] = dev->pci_conf[0x60] = 0x00;
    dev->pci_conf[0x61] = dev->pci_conf[0x62] = 0xff;
    dev->pci_conf[0x63]                       = 0xff;
    dev->pci_conf[0x64] = dev->pci_conf[0x65] = 0x00;
    dev->pci_conf[0x66]                       = 0x00;
    dev->pci_conf[0x67]                       = 0xff;
    dev->pci_conf[0x68] = dev->pci_conf[0x69] = 0x00;
    dev->pci_conf[0x6a]                       = 0x00;
    dev->pci_conf[0x6b] = dev->pci_conf[0x6c] = 0xff;
    dev->pci_conf[0x6d] = dev->pci_conf[0x6e] = 0xff;
    dev->pci_conf[0x6f]                       = 0x00;
    dev->pci_conf[0x70] = dev->pci_conf[0x72] = 0x04;
    dev->pci_conf[0x74] = dev->pci_conf[0x76] = 0x04;
    dev->pci_conf[0x78] = dev->pci_conf[0x7a] = 0x04;
    dev->pci_conf[0x7c] = dev->pci_conf[0x7e] = 0x04;
    dev->pci_conf[0x71] = dev->pci_conf[0x75] = 0x00;
    dev->pci_conf[0x73] = dev->pci_conf[0x77] = 0x80;
    dev->pci_conf[0x79] = dev->pci_conf[0x7d] = 0x00;
    dev->pci_conf[0x7b] = dev->pci_conf[0x7f] = 0x80;
    dev->pci_conf[0x80] = dev->pci_conf[0x81] = 0x00;
    dev->pci_conf[0x82] = dev->pci_conf[0x83] = 0x00;
    dev->pci_conf[0x84] = dev->pci_conf[0x85] = 0x00;
    dev->pci_conf[0x86] = 0x00;

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    sis_5511_smram_recalc(dev);
    sis_5511_shadow_recalc(dev);

    flushmmucache();

    memset(dev->slic_regs, 0x00, 4096 * sizeof(uint8_t));
    dev->slic_regs[0x18] = 0x0f;

    mem_mapping_set_addr(&dev->slic_mapping, 0xffc00000, 0x00001000);
}

static void
sis_5511_host_to_pci_close(void *priv)
{
    sis_5511_host_to_pci_t *dev = (sis_5511_host_to_pci_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
sis_5511_host_to_pci_init(UNUSED(const device_t *info))
{
    sis_5511_host_to_pci_t *dev = (sis_5511_host_to_pci_t *) calloc(1, sizeof(sis_5511_host_to_pci_t));

    dev->sis = device_get_common_priv();

    /* SLiC Memory Mapped Registers */
    mem_mapping_add(&dev->slic_mapping,
                    0xffc00000, 0x00001000,
                    sis_5511_slic_read,
                    NULL,
                    NULL,
                    sis_5511_slic_write,
                    NULL,
                    NULL,
                    NULL, MEM_MAPPING_EXTERNAL,
                    dev);

    /* SMRAM */
    dev->smram = smram_add();

    sis_5511_host_to_pci_reset(dev);

    return dev;
}

const device_t sis_5511_h2p_device = {
    .name          = "SiS 5511 Host to PCI bridge",
    .internal_name = "sis_5511_host_to_pci",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = sis_5511_host_to_pci_init,
    .close         = sis_5511_host_to_pci_close,
    .reset         = sis_5511_host_to_pci_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
