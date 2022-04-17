/*
 * Intel 815EP MCH Bridge
 *
 * Authors:	Tiseno100,
 *
 * Copyright 2022 Tiseno100.
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

#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/smram.h>
#include <86box/spd.h>
#include <86box/chipset.h>
#define ENABLE_INTEL_MCH_LOG 1
#ifdef ENABLE_INTEL_MCH_LOG
int intel_mch_do_log = ENABLE_INTEL_MCH_LOG;
static void
intel_mch_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_mch_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define intel_mch_log(fmt, ...)
#endif

typedef struct intel_mch_t
{
    uint8_t pci_conf[256];
    smram_t *lsmm_segment;
} intel_mch_t;

static void
intel_lsmm_segment_recalc(intel_mch_t *dev, uint8_t val)
{
    intel_mch_log("Intel MCH: LSMM update to status %d\n", val); /* Check the i815EP datasheet for status */

    switch(val)
    {
    case 0:
        smram_disable(dev->lsmm_segment);
    break;

    case 1:
        smram_enable(dev->lsmm_segment, 0x000a0000, 0x000a0000, 0x20000, 1, 0);
    break;

    case 2:
        smram_enable(dev->lsmm_segment, 0x000a0000, 0x000a0000, 0x20000, 0, 0);
    break;

    case 3:
        smram_enable(dev->lsmm_segment, 0x000a0000, 0x000a0000, 0x20000, 0, 1);
    break;
    }
}

static void
intel_pam_recalc(int addr, uint8_t val)
{
    int region = 0xc0000 + ((addr - 0x5a) << 15);

    if(addr == 0x59)
        mem_set_mem_state_both(0xf0000, 0x10000, ((val & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    else
    {
        mem_set_mem_state_both(region, 0x4000, ((val & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((val & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_both(region + 0x4000, 0x4000, ((val & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }
}

static void
intel_mch_write(int func, int addr, uint8_t val, void *priv)
{
    intel_mch_t *dev = (intel_mch_t *)priv;

    intel_mch_log("Intel MCH: dev->regs[%02x] = %02x\n", addr, val);

    switch(addr)
    {
        case 0x51:
            dev->pci_conf[addr] = val & 2; // Brute force to AGP Mode
        break;

        case 0x52:
        case 0x54:
            dev->pci_conf[addr] = val & ((addr & 4) ? 0x0f : 0xff);
            spd_write_drbs_intel_mch(dev->pci_conf);
        break;

        case 0x59 ... 0x5f:
            dev->pci_conf[addr] = val;
            intel_pam_recalc(addr, val);
        break;

        case 0x70:
            dev->pci_conf[addr] = val;
            intel_lsmm_segment_recalc(dev, (val >> 2) & 3);
        break;

        default:
            dev->pci_conf[addr] = val;
        break;
    }
}


static uint8_t
intel_mch_read(int func, int addr, void *priv)
{
    intel_mch_t *dev = (intel_mch_t *)priv;

    intel_mch_log("Intel MCH: dev->regs[%02x] (%02x)\n", addr, dev->pci_conf[addr]);
    return dev->pci_conf[addr];
}


static void
intel_mch_reset(void *priv)
{
    intel_mch_t *dev = (intel_mch_t *)priv;
    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf)); /* Wash out the registers */

    dev->pci_conf[0x00] = 0x86;    /* Intel */
    dev->pci_conf[0x01] = 0x80;

    dev->pci_conf[0x02] = 0x30;    /* 815EP */
    dev->pci_conf[0x03] = 0x11;

    dev->pci_conf[0x04] = 0x06;

    dev->pci_conf[0x06] = 0x90;
    dev->pci_conf[0x08] = 0x02;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x10] = 0x03;
    dev->pci_conf[0x34] = 0xa0;
    dev->pci_conf[0x50] = 0x40;

    dev->pci_conf[0x88] = 0x09;
    dev->pci_conf[0x89] = 0xa0;
    dev->pci_conf[0x8a] = 0x04;
    dev->pci_conf[0x8b] = 0xf1;

    dev->pci_conf[0x92] = 0xff;
    dev->pci_conf[0x93] = 0xff;
    dev->pci_conf[0x94] = 0xff;
    dev->pci_conf[0x95] = 0xff;

    dev->pci_conf[0xa0] = 0x02;
    dev->pci_conf[0xa2] = 0x20;

    dev->pci_conf[0xa4] = 0x07;
    dev->pci_conf[0xa5] = 0x02;
    dev->pci_conf[0xa7] = 0x1f;

    for(int i = 0x58; i <= 0x5f; i++)  /* Reset PAM to defaults */
        intel_pam_recalc(i, 0);


    intel_lsmm_segment_recalc(dev, 0); /* Reset LSMM SMRAM to defaults */

}


static void
intel_mch_close(void *priv)
{
    intel_mch_t *dev = (intel_mch_t *)priv;

    smram_del(dev->lsmm_segment);
    free(dev);
}


static void *
intel_mch_init(const device_t *info)
{
    intel_mch_t *dev = (intel_mch_t *)malloc(sizeof(intel_mch_t));
    memset(dev, 0, sizeof(intel_mch_t));

    /* Device */
    pci_add_card(PCI_ADD_NORTHBRIDGE, intel_mch_read, intel_mch_write, dev); /* Device 0: Intel i815EP MCH */

    /* AGP Bridge */
    device_add(&intel_mch_agp_device);

    /* L2 Cache */
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    /* LSMM SMRAM Segment */
    dev->lsmm_segment = smram_add();

    intel_mch_reset(dev);
    return dev;
}

const device_t intel_mch_device = {
    .name = "Intel 815EP MCH Bridge",
    .internal_name = "intel_mch",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = intel_mch_init,
    .close = intel_mch_close,
    .reset = intel_mch_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
