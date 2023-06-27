/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Intel 82815EP & 82815P MCH Bridge
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/plat_unused.h>

#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/smram.h>
#include <86box/spd.h>
#include <86box/agpgart.h>
#include <86box/chipset.h>

#ifdef ENABLE_INTEL_815EP_LOG
int intel_815ep_do_log = ENABLE_INTEL_815EP_LOG;
static void
intel_815ep_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_815ep_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define intel_815ep_log(fmt, ...)
#endif

typedef struct intel_815ep_t {
    uint8_t    pci_conf[256];
    smram_t   *lsmm_segment;
    smram_t   *h_segment;
    smram_t   *usmm_segment;
    agpgart_t *agpgart;
} intel_815ep_t;

static void
intel_815ep_agp_aperature(intel_815ep_t *dev)
{
    uint32_t aperature_base;
    uint32_t aperature_size_calc;
    int      aperature_size;
    int      aperature_enable;

    aperature_base      = dev->pci_conf[0x13] << 24;
    aperature_size      = !!(dev->pci_conf[0xb4] & 8);
    aperature_size_calc = 1 << (aperature_size ? 25 : 24); /* 815EP has the choice of 64 & 32MB only */

    aperature_enable = aperature_base != 0;

    if (aperature_size)
        dev->pci_conf[0x13] &= 0xfe;

    if (aperature_enable)
        intel_815ep_log("Intel 815EP AGP Aperature: Enabled at size 0x%x and size of %dMB\n", aperature_base, aperature_size ? 64 : 32);
    else
        intel_815ep_log("Intel 815EP AGP Aperature: AGP Aperature disabled\n");

    agpgart_set_aperture(dev->agpgart, aperature_base, aperature_size_calc, aperature_enable);
}

static void
intel_usmm_segment_recalc(intel_815ep_t *dev, uint8_t val)
{
    intel_815ep_log("Intel 815EP MCH: USMM update to status %d\n", val); /* Check the 815EP datasheet for status */

    smram_disable(dev->h_segment);
    smram_disable(dev->usmm_segment);

    if (val != 0)
        smram_enable(dev->h_segment, 0xfeea0000, 0x000a0000, 0x20000, 0, 1);

    if (val >= 2) { /* TOM recalc based on intel_4x0.c by OBattler */
        uint32_t tom = (mem_size << 10);
        mem_set_mem_state_smm(tom, 0x100000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        uint32_t size = (val != 3) ? 0x100000 : 0x80000;
        smram_enable(dev->usmm_segment, tom + 0x10000000, tom, size, 0, 1);
        mem_set_mem_state_smm(tom, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }
}

static void
intel_lsmm_segment_recalc(intel_815ep_t *dev, uint8_t val)
{
    intel_815ep_log("Intel 815EP MCH: LSMM update to status %d\n", val); /* Check the 815EP datasheet for status */

    smram_disable(dev->lsmm_segment);

    switch (val) {
        case 1:
            smram_enable(dev->lsmm_segment, 0x000a0000, 0x000a0000, 0x20000, 1, 0);
            break;

        case 2:
            smram_enable(dev->lsmm_segment, 0x000a0000, 0x000a0000, 0x20000, 0, 0);
            break;

        case 3:
            smram_enable(dev->lsmm_segment, 0x000a0000, 0x000a0000, 0x20000, 0, 1);
            break;
        default:
            break;
    }

    flushmmucache();
}

static void
intel_pam_recalc(int addr, uint8_t val)
{
    int region = 0xc0000 + ((addr - 0x5a) << 15);

    if (addr == 0x59)
        mem_set_mem_state_both(0xf0000, 0x10000, ((val & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    else {
        mem_set_mem_state_both(region, 0x4000, ((val & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((val & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_both(region + 0x4000, 0x4000, ((val & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }

    flushmmucache_nopc();
}

static void
intel_815ep_gart_table(intel_815ep_t *dev)
{
    uint32_t agp_gart_base = (dev->pci_conf[0xbb] << 24) | (dev->pci_conf[0xba] << 16) | (dev->pci_conf[0xb9] << 8);

    intel_815ep_log("Intel 815EP AGP GART: GART address updated at 0x%x\n", agp_gart_base);

    agpgart_set_gart(dev->agpgart, agp_gart_base);
}

static void
intel_815ep_write(int func, int addr, uint8_t val, void *priv)
{
    intel_815ep_t *dev = (intel_815ep_t *) priv;

    intel_815ep_log("Intel 815EP MCH: dev->regs[%02x] = %02x\n", addr, val);

    if (func)
        return;

    switch (addr) {
        case 0x05:
            dev->pci_conf[addr] = val & 3;
            break;

        case 0x07:
            dev->pci_conf[addr] &= val & 0x70;
            break;

        case 0x2c ... 0x2f:
            if (dev->pci_conf[addr] != 0)
                dev->pci_conf[addr] = val;
            break;

        case 0x13:
            dev->pci_conf[addr] = val;
            intel_815ep_agp_aperature(dev);
            break;

        case 0x50:
            dev->pci_conf[addr] = val & 0xdc;
            break;

        case 0x51:
            dev->pci_conf[addr] = val & 2; // Brute force to AGP Mode
            break;

        case 0x52:
        case 0x54:
            if (!(dev->pci_conf[0x70] & 2)) {
                dev->pci_conf[addr] = val & ((addr & 4) ? 0x0f : 0xff);
                spd_write_drbs_intel_815ep(dev->pci_conf);
            }
            break;

        case 0x53:
            dev->pci_conf[addr] = val;
            break;

        case 0x58:
            dev->pci_conf[addr] = val & 0x80;
            break;

        case 0x59 ... 0x5f:
            dev->pci_conf[addr] = val;
            intel_pam_recalc(addr, val);
            break;

        case 0x70:
            if (!(dev->pci_conf[0x70] & 2)) {
                dev->pci_conf[addr] = val & 0xfe;
                intel_usmm_segment_recalc(dev, (val >> 4) & 3);
            } else {
                dev->pci_conf[addr] = (dev->pci_conf[addr] & 0xfa) | (val & 4);
            }

            intel_lsmm_segment_recalc(dev, (val >> 2) & 3);
            break;

        case 0x72:
            dev->pci_conf[addr] = val & 0xfb;
            break;

        case 0x73:
            dev->pci_conf[addr] = val & 0xa8;
            break;

        case 0x92 ... 0x93:
            dev->pci_conf[addr] = val;
            break;

        case 0x94:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0x98:
            dev->pci_conf[addr] = val & 0x77;
            break;

        case 0x99:
            dev->pci_conf[addr] = val & 0x80;
            break;

        case 0x9a:
            dev->pci_conf[addr] = val & 0xef;
            break;

        case 0x9b:
        case 0x9d:
            dev->pci_conf[addr] = val & 0x80;
            break;

        case 0xa4:
            dev->pci_conf[addr] = val & 7;
            break;

        case 0xa8:
            dev->pci_conf[addr] = val & 0x37;
            break;

        case 0xa9:
            dev->pci_conf[addr] = (val & 2) | 1;
            break;

        case 0xb0:
            dev->pci_conf[addr] = val & 0x81;
            break;

        case 0xb4:
            dev->pci_conf[addr] = val & 8;
            intel_815ep_agp_aperature(dev);
            break;

        case 0xb9:
            dev->pci_conf[addr] = val & 0xf0;
            intel_815ep_gart_table(dev);
            break;

        case 0xba:
            dev->pci_conf[addr] = val;
            intel_815ep_gart_table(dev);
            break;

        case 0xbb:
            dev->pci_conf[addr] = val & 0x1f;
            intel_815ep_gart_table(dev);
            break;

        case 0xbc ... 0xbd:
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0xbe:
            dev->pci_conf[addr] = val & 0x28;
            break;

        case 0xcb:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        default:
            break;
    }
}

static uint8_t
intel_815ep_read(int func, int addr, void *priv)
{
    intel_815ep_t *dev = (intel_815ep_t *) priv;

    intel_815ep_log("Intel 815EP MCH: dev->regs[%02x] (%02x)\n", addr, dev->pci_conf[addr]);

    if (func)
        return 0xff;

    if (addr == 0x51) // Bit 2 is Write Only. It cannot be read.
        return dev->pci_conf[addr] & 3;
    else if (addr == 0x52)
        return intel_815ep_get_banking();
    else
        return dev->pci_conf[addr];
}

static void
intel_815ep_reset(void *priv)
{
    intel_815ep_t *dev = (intel_815ep_t *) priv;
    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf)); /* Wash out the registers */

    /* VID - Vendor Identification Register */
    dev->pci_conf[0x00] = 0x86; /* Intel */
    dev->pci_conf[0x01] = 0x80;

    /* DID - Device Identification Register */
    dev->pci_conf[0x02] = 0x30; /* 815EP */
    dev->pci_conf[0x03] = 0x11;

    /* PCICMD - PCI Command Register */
    dev->pci_conf[0x04] = 0x06;
    dev->pci_conf[0x05] = 0x00;

    /* PCISTS - PCI Status Register */
    dev->pci_conf[0x06] = 0x90;
    dev->pci_conf[0x07] = 0x00;

    /* RID - Revision Identification Register */
    dev->pci_conf[0x08] = 0x02;

    /* BCC - Base Class Code Register */
    dev->pci_conf[0x0b] = 0x06;

    /* APBASE - Aperture Base Configuration Register */
    dev->pci_conf[0x10] = 0x03;

    /* CAPPTR - Capabilities Pointer */
    dev->pci_conf[0x34] = 0xa0;

    /* MCHCFG - MCH Configuration Register */
    dev->pci_conf[0x50] = 0x40;

    /* CAPID - Capability Identification */
    dev->pci_conf[0x88] = 0x09;
    dev->pci_conf[0x89] = 0xa0;
    dev->pci_conf[0x8a] = 0x04;
    dev->pci_conf[0x8b] = 0xf1;

    /* BUFF_SC - System Memory Buffer Strength Control Register */
    dev->pci_conf[0x92] = 0xff;
    dev->pci_conf[0x93] = 0xff;

    /* BUFF_SC2 - System Memory Buffer Strength Control Register 2 */
    dev->pci_conf[0x94] = 0xff;
    dev->pci_conf[0x95] = 0xff;

    /* ACAPID—AGP Capability Identifier Register */
    dev->pci_conf[0xa0] = 0x02;
    dev->pci_conf[0xa1] = 0x00;
    dev->pci_conf[0xa2] = 0x20;
    dev->pci_conf[0xa3] = 0x00;

    dev->pci_conf[0xa4] = 0x07;
    dev->pci_conf[0xa5] = 0x02;
    dev->pci_conf[0xa6] = 0x00;
    dev->pci_conf[0xa7] = 0x1f;

    /* AGPCMD - AGP Command Register (Device 0: AGP Mode Only) */
    dev->pci_conf[0xa9] = 0x01; /* Hack: Brute Force AGP Enabled */

    intel_815ep_agp_aperature(dev); /* Configure AGP Aperature */

    for (int i = 0x59; i <= 0x5f; i++) /* Reset PAM to defaults */
        intel_pam_recalc(i, 0);

    intel_lsmm_segment_recalc(dev, 0); /* Reset LSMM SMRAM to defaults */
    intel_usmm_segment_recalc(dev, 0); /* Reset USMM SMRAM to defaults */

    intel_815ep_gart_table(dev); /* Reset AGP GART to defaults */
}

static void
intel_815ep_close(void *priv)
{
    intel_815ep_t *dev = (intel_815ep_t *) priv;

    smram_del(dev->lsmm_segment);
    smram_del(dev->h_segment);
    smram_del(dev->usmm_segment);
    free(dev);
}

static void *
intel_815ep_init(UNUSED(const device_t *info))
{
    intel_815ep_t *dev = (intel_815ep_t *) malloc(sizeof(intel_815ep_t));
    memset(dev, 0, sizeof(intel_815ep_t));

    /* Bus Speed(815EP runs at 133Mhz) */
    if (cpu_busspeed >= 133333333)
        cpu_set_pci_speed(133333333);
    else
        cpu_set_pci_speed(cpu_busspeed);

    /* Device */
    pci_add_card(PCI_ADD_NORTHBRIDGE, intel_815ep_read, intel_815ep_write, dev); /* Device 0: Intel 815EP MCH */

    /* AGP Bridge */
    device_add(&intel_815ep_agp_device);

    /* AGP GART*/
    dev->agpgart = device_add(&agpgart_device);

    /* L1 & L2 Cache */
    cpu_cache_int_enabled = 1;
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    /* SMRAM Segments */
    dev->lsmm_segment = smram_add();
    dev->h_segment    = smram_add();
    dev->usmm_segment = smram_add();

    intel_815ep_reset(dev);
    return dev;
}

const device_t intel_815ep_device = {
    .name          = "Intel 815EP MCH Bridge",
    .internal_name = "intel_815ep",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = intel_815ep_init,
    .close         = intel_815ep_close,
    .reset         = intel_815ep_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
