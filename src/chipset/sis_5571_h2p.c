/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5571 Host to PCI bridge.
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

#ifdef ENABLE_SIS_5571_HOST_TO_PCI_LOG
int sis_5571_host_to_pci_do_log = ENABLE_SIS_5571_HOST_TO_PCI_LOG;

static void
sis_5571_host_to_pci_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5571_host_to_pci_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5571_host_to_pci_log(fmt, ...)
#endif

typedef struct sis_5571_host_to_pci_t {
    uint8_t            pci_conf[256];
    uint8_t            states[7];

    sis_55xx_common_t *sis;

    smram_t           *smram;
} sis_5571_host_to_pci_t;

static void
sis_5571_shadow_recalc(sis_5571_host_to_pci_t *dev)
{
    int      state;
    uint32_t base;

    for (uint8_t i = 0x70; i <= 0x76; i++) {
        if (i == 0x76) {
            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(0xf0000, 0x10000, state);
                sis_5571_host_to_pci_log("000F0000-000FFFFF\n");
            }
        } else {
            base = ((i & 0x07) << 15) + 0xc0000;

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base, 0x4000, state);
                sis_5571_host_to_pci_log("%08X-%08X\n", base, base + 0x3fff);
            }

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0x0a) {
                state = (dev->pci_conf[i] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base + 0x4000, 0x4000, state);
                sis_5571_host_to_pci_log("%08X-%08X\n", base + 0x4000, base + 0x7fff);
            }
        }

        dev->states[i & 0x0f] = dev->pci_conf[i];
    }

    flushmmucache_nopc();
}

static void
sis_5571_smram_recalc(sis_5571_host_to_pci_t *dev)
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
sis_5571_host_to_pci_write(int addr, uint8_t val, void *priv)
{
    sis_5571_host_to_pci_t *dev = (sis_5571_host_to_pci_t *) priv;

    sis_5571_host_to_pci_log("SiS 5571 H2P: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    switch (addr) {
        default:
            break;

        case 0x04: /* Command - low byte */
        case 0x05: /* Command - high byte */
            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0xfd) | (val & 0x02);
            break;

        case 0x07: /* Status - High Byte */
            dev->pci_conf[addr] &= ~(val & 0xb8);
            break;

        case 0x0d: /* Master latency timer */
            dev->pci_conf[addr] = val;
            break;

        case 0x50: /* Host Interface and DRAM arbiter */
            dev->pci_conf[addr] = val & 0xec;
            break;

        case 0x51: /* CACHE */
            dev->pci_conf[addr]   = val;
            cpu_cache_ext_enabled = !!(val & 0x40);
            cpu_update_waitstates();
            break;

        case 0x52:
            dev->pci_conf[addr] = val & 0xd0;
            break;

        case 0x53: /* DRAM */
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x54: /* FP/EDO */
            dev->pci_conf[addr] = val;
            break;

        case 0x55:
            dev->pci_conf[addr] = val & 0xe0;
            break;

        case 0x56: /* MDLE delay */
            dev->pci_conf[addr] = val & 0x07;
            break;

        case 0x57: /* SDRAM */
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x59: /* Buffer strength and current rating  */
            dev->pci_conf[addr] = val;
            break;

        case 0x5a:
            dev->pci_conf[addr] = val & 0x03;
            break;

        /* Undocumented - DRAM bank registers, the exact layout is currently unknown. */
        case 0x60 ... 0x6b:
            dev->pci_conf[addr] = val;
            break;

        case 0x70 ... 0x75:
            dev->pci_conf[addr] = val & 0xee;
            sis_5571_shadow_recalc(dev);
            break;
        case 0x76:
            dev->pci_conf[addr] = val & 0xe8;
            sis_5571_shadow_recalc(dev);
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
            dev->pci_conf[addr] = val & 0xcc;
            break;

        case 0x82:
            dev->pci_conf[addr] = val;
            break;

        case 0x83: /* CPU to PCI characteristics */
            dev->pci_conf[addr] = val;
            /* TODO: Implement Fast A20 and Fast reset stuff on the KBC already! */
            break;

        case 0x84 ... 0x86:
            dev->pci_conf[addr] = val;
            break;

        case 0x87: /* Miscellanea */
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x90: /* PMU control register */
        case 0x91: /* Address trap for green function */
        case 0x92:
            dev->pci_conf[addr] = val;
            break;

        case 0x93: /* STPCLK# and APM SMI control */
            dev->pci_conf[addr] = val;

            if ((dev->pci_conf[0x9b] & 0x01) && (val & 0x02)) {
                smi_raise();
                dev->pci_conf[0x9d] |= 0x01;
            }
            break;

        case 0x94: /* 6x86 and Green function control */
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x95: /* Test mode control */
        case 0x96: /* Time slot and Programmable 10-bit I/O port definition */
            dev->pci_conf[addr] = val & 0xfb;
            break;

        case 0x97: /* programmable 10-bit I/O port address */
        case 0x98: /* Programmable 16-bit I/O port */
        case 0x99 ... 0x9c:
            dev->pci_conf[addr] = val;
            break;

        case 0x9d:
            dev->pci_conf[addr] &= val;
            break;

        case 0x9e: /* STPCLK# Assertion Timer */
        case 0x9f: /* STPCLK# De-assertion Timer */
        case 0xa0 ... 0xa2:
            dev->pci_conf[addr] = val;
            break;

        case 0xa3: /* SMRAM access control and Power supply control */
            dev->pci_conf[addr] = val & 0xd0;
            sis_5571_smram_recalc(dev);
            break;
    }
}

uint8_t
sis_5571_host_to_pci_read(int addr, void *priv)
{
    const sis_5571_host_to_pci_t *dev = (sis_5571_host_to_pci_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->pci_conf[addr];

    sis_5571_host_to_pci_log("SiS 5571 H2P: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5571_host_to_pci_reset(void *priv)
{
    sis_5571_host_to_pci_t *dev = (sis_5571_host_to_pci_t *) priv;

    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x71;
    dev->pci_conf[0x03] = 0x55;
    dev->pci_conf[0x04] = 0x05;
    dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = 0x00;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = 0x00;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x0c] = 0x00;
    dev->pci_conf[0x0d] = 0x00;
    dev->pci_conf[0x0e] = 0x00;
    dev->pci_conf[0x0f] = 0x00;

    dev->pci_conf[0x50] = 0x00;
    dev->pci_conf[0x51] = 0x00;
    dev->pci_conf[0x52] = 0x00;
    dev->pci_conf[0x53] = 0x00;
    dev->pci_conf[0x54] = 0x54;
    dev->pci_conf[0x55] = 0x54;
    dev->pci_conf[0x56] = 0x03;
    dev->pci_conf[0x57] = 0x00;
    dev->pci_conf[0x58] = 0x00;
    dev->pci_conf[0x59] = 0x00;
    dev->pci_conf[0x5a] = 0x00;

    /* Undocumented DRAM bank registers. */
    dev->pci_conf[0x60] = dev->pci_conf[0x62] = 0x04;
    dev->pci_conf[0x64] = dev->pci_conf[0x66] = 0x04;
    dev->pci_conf[0x68] = dev->pci_conf[0x6a] = 0x04;
    dev->pci_conf[0x61] = dev->pci_conf[0x65] = 0x00;
    dev->pci_conf[0x63] = dev->pci_conf[0x67] = 0x80;
    dev->pci_conf[0x69]                       = 0x00;
    dev->pci_conf[0x6b]                       = 0x80;

    dev->pci_conf[0x70] = 0x00;
    dev->pci_conf[0x71] = 0x00;
    dev->pci_conf[0x72] = 0x00;
    dev->pci_conf[0x73] = 0x00;
    dev->pci_conf[0x74] = 0x00;
    dev->pci_conf[0x75] = 0x00;
    dev->pci_conf[0x76] = 0x00;

    dev->pci_conf[0x77] = 0x00;
    dev->pci_conf[0x78] = 0x00;
    dev->pci_conf[0x79] = 0x00;
    dev->pci_conf[0x7a] = 0x00;
    dev->pci_conf[0x7b] = 0x00;

    dev->pci_conf[0x80] = 0x00;
    dev->pci_conf[0x81] = 0x00;
    dev->pci_conf[0x82] = 0x00;
    dev->pci_conf[0x83] = 0x00;
    dev->pci_conf[0x84] = 0x00;
    dev->pci_conf[0x85] = 0x00;
    dev->pci_conf[0x86] = 0x00;
    dev->pci_conf[0x87] = 0x00;

    dev->pci_conf[0x8c] = 0x00;
    dev->pci_conf[0x8d] = 0x00;
    dev->pci_conf[0x8e] = 0x00;
    dev->pci_conf[0x8f] = 0x00;

    dev->pci_conf[0x90] = 0x00;
    dev->pci_conf[0x91] = 0x00;
    dev->pci_conf[0x92] = 0x00;
    dev->pci_conf[0x93] = 0x00;
    dev->pci_conf[0x93] = 0x00;
    dev->pci_conf[0x94] = 0x00;
    dev->pci_conf[0x95] = 0x00;
    dev->pci_conf[0x96] = 0x00;
    dev->pci_conf[0x97] = 0x00;
    dev->pci_conf[0x98] = 0x00;
    dev->pci_conf[0x99] = 0x00;
    dev->pci_conf[0x9a] = 0x00;
    dev->pci_conf[0x9b] = 0x00;
    dev->pci_conf[0x9c] = 0x00;
    dev->pci_conf[0x9d] = 0x00;
    dev->pci_conf[0x9e] = 0xff;
    dev->pci_conf[0x9f] = 0xff;

    dev->pci_conf[0xa0] = 0xff;
    dev->pci_conf[0xa1] = 0x00;
    dev->pci_conf[0xa2] = 0xff;
    dev->pci_conf[0xa3] = 0x00;

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    sis_5571_smram_recalc(dev);
    sis_5571_shadow_recalc(dev);

    flushmmucache();
}

static void
sis_5571_host_to_pci_close(void *priv)
{
    sis_5571_host_to_pci_t *dev = (sis_5571_host_to_pci_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
sis_5571_host_to_pci_init(UNUSED(const device_t *info))
{
    sis_5571_host_to_pci_t *dev = (sis_5571_host_to_pci_t *) calloc(1, sizeof(sis_5571_host_to_pci_t));

    dev->sis = device_get_common_priv();

    /* SMRAM */
    dev->smram = smram_add();

    sis_5571_host_to_pci_reset(dev);

    return dev;
}

const device_t sis_5571_h2p_device = {
    .name          = "SiS 5571 Host to PCI bridge",
    .internal_name = "sis_5571_host_to_pci",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = sis_5571_host_to_pci_init,
    .close         = sis_5571_host_to_pci_close,
    .reset         = sis_5571_host_to_pci_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
