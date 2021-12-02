/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel i845 (MCH) Chipset.
 *
 *
 *
 * Authors:	Tiseno100
 *
 *		Copyright 2021 Tiseno100.
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
#include <86box/spd.h>
#include <86box/smram.h>

#include <86box/chipset.h>

#ifdef ENABLE_INTEL_MCH_P4_LOG
int intel_mch_p4_do_log = ENABLE_INTEL_MCH_P4_LOG;


static void
intel_mch_p4_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_mch_p4_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define intel_mch_p4_log(fmt, ...)
#endif

typedef struct intel_mch_p4_t
{

    int is_ddr;
	uint8_t pci_conf[256];
    smram_t	*low_smram;
    smram_t *upper_smram_hseg;
    smram_t *upper_smram_tseg;

} intel_mch_p4_t;

static void
intel_mch_p4_pam(int cur_reg, intel_mch_p4_t *dev)
{
    if(cur_reg == 0x90)
        mem_set_mem_state_both(0xf0000, 0x10000, ((dev->pci_conf[0x90] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[0x90] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    else
    {
        int negate = cur_reg - 0x91;
        mem_set_mem_state_both(0xc0000 + (negate << 15), 0x4000, ((dev->pci_conf[cur_reg] & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 2) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_both(0xc4000 + (negate << 15), 0x4000, ((dev->pci_conf[cur_reg] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }

    flushmmucache_nopc();
}

uint32_t
intel_mch_p4_tom_size(int size)
{
    switch(size)
    {
        case 0:
        default:
            return 0x00020000;
        
        case 1:
            return 0x00040000;

        case 2:
            return 0x00080000;
        
        case 3:
            return 0x00100000;
    }
}

static void
intel_mch_p4_smram(intel_mch_p4_t *dev)
{
    uint32_t tom = (mem_size << 10);
    uint32_t tom_size = 0;

    smram_disable_all();

    if(dev->pci_conf[0x9d] & 8)
    {
        smram_enable(dev->low_smram, 0x000a0000, 0x000a0000, 0x20000, !!(dev->pci_conf[0x9d] & 0x40), 1);

        if(dev->pci_conf[0x9d] & 0x20) /* SMM Space Close */
            mem_set_mem_state_smram_ex(1, 0x000a0000, 0x20000, 2);


        if(dev->pci_conf[0x9e] & 0x80) /* SMRAM Mirror */
            smram_enable(dev->upper_smram_hseg, 0xfeea0000, 0x000a0000, 0x20000, 0, 1);

        if(dev->pci_conf[0x9e] & 1)  /* Top Remapping based on intel_4x0.c by OBattler */
        {
            /* Phase 0 */
            tom -= (1 << 20);
            mem_set_mem_state_smm(tom, (1 << 20), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

            /* Phase 1 */
            tom = (mem_size << 10);
            tom_size = intel_mch_p4_tom_size((dev->pci_conf[0x9e] >> 1) & 3);
            tom -= tom_size;

            smram_enable(dev->upper_smram_tseg, tom + (1 << 28), tom, tom_size, 0, 1);
            mem_set_mem_state_smm(tom, tom_size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        }
    }

    flushmmucache();
}

static void
intel_mch_p4_write(int func, int addr, uint8_t val, void *priv)
{

    intel_mch_p4_t *dev = (intel_mch_p4_t *)priv;

    if (func == 0) {
        intel_mch_p4_log("Intel i845: dev->regs[%02x] = %02x POST: %02x \n", addr, val, inb(0x80));

        switch(addr)
        {
            case 0x04:
                dev->pci_conf[addr] = val & 0x80;
            break;

            case 0x07:
                dev->pci_conf[addr] &= val & 0x70;
            break;

            case 0x12:
                dev->pci_conf[addr] = val & 0xc0;
            break;

            case 0x13:
                dev->pci_conf[addr] = val;
            break;

            case 0x2c ... 0x2f:
                if(dev->pci_conf[addr] == 0)
                    dev->pci_conf[addr] = val;
            break; 

            case 0x51:
                dev->pci_conf[addr] = val & 0x02;
            break;

            case 0x60 ... 0x63: /* SPD */
                spd_write_drbs(dev->pci_conf, 0x60, 0x63, 32);
                dev->pci_conf[addr] = val;
            break;

            case 0x64 ... 0x67: /* SPD Note: BIOS must write the value of 0x63 on those registers. */
                dev->pci_conf[addr] = val;
            break;

            case 0x70 ... 0x73:
                dev->pci_conf[addr] = val & 0x77;
            break;

            case 0x78:
                dev->pci_conf[addr] = val & 0x35;
            break;

            case 0x79:
                dev->pci_conf[addr] = val & 6;
            break;

            case 0x7a:
                dev->pci_conf[addr] = val & 7;
            break;

            case 0x7c:
                dev->pci_conf[addr] = (val & 0x73) | dev->is_ddr;
            break;

            case 0x7d:
                dev->pci_conf[addr] = val & 7;
            break;

            case 0x7e:
                dev->pci_conf[addr] = val & 0xf0;
            break;

            case 0x7f:
                dev->pci_conf[addr] = val;
            break;

            case 0x86:
                dev->pci_conf[addr] = val;
            break;

            case 0x90 ... 0x96: /* Shadow */
                dev->pci_conf[addr] = val & 0x33;
                intel_mch_p4_pam(addr, dev);
            break;

            case 0x9d ... 0x9e: /* SMRAM */
                if(!(dev->pci_conf[0x9d] & 0x10))
                {
                    dev->pci_conf[addr] = val;
                    intel_mch_p4_smram(dev);
                }
            break;

            case 0xa8:
                dev->pci_conf[addr] = val & 0x17;
            break;

            case 0xa9:
                dev->pci_conf[addr] = val & 3;
            break;

            case 0xb0:
                dev->pci_conf[addr] = val & 0x81;
            break;

            case 0xb4:
                dev->pci_conf[addr] = val & 0x3f;
            break;

            case 0xb9:
                dev->pci_conf[addr] = val & 0xf0;
            break;

            case 0xba ... 0xbb:
                dev->pci_conf[addr] = val;
            break;

            case 0xbc ... 0xbd:
                dev->pci_conf[addr] = val & 0xf8;
            break;

            case 0xc4:
                dev->pci_conf[addr] = val & 0xf0;
            break;

            case 0xc5:
                dev->pci_conf[addr] = val;
            break;

            case 0xc6:
                dev->pci_conf[addr] = val & 0x22;
            break;

            case 0xc7:
                dev->pci_conf[addr] = val & 8;
            break;

            case 0xc8:
                dev->pci_conf[addr] &= val & 0x7f;
            break;

            case 0xc9:
                dev->pci_conf[addr] &= val & 2;
            break;

            case 0xca:
                dev->pci_conf[addr] = val & 0x7f;
            break;

            case 0xcb:
                dev->pci_conf[addr] = val & 2;
            break;

            case 0xcc:
            case 0xce:
                dev->pci_conf[addr] = val & 3;
            break;

            case 0xde ... 0xdf:
                dev->pci_conf[addr] = val;
            break;
        }
    }

}


static uint8_t
intel_mch_p4_read(int func, int addr, void *priv)
{
    intel_mch_p4_t *dev = (intel_mch_p4_t *)priv;

    if (func == 0) {
        intel_mch_p4_log("Intel i845: dev->regs[%02x] (%02x) POST: %02x \n", addr, dev->pci_conf[addr], inb(0x80));
        return dev->pci_conf[addr];
    }
    else return 0xff;
}


static void
intel_mch_p4_reset(void *priv)
{
    intel_mch_p4_t *dev = (intel_mch_p4_t *)priv;
    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));

    dev->pci_conf[0x00] = 0x86;
    dev->pci_conf[0x01] = 0x80;
    dev->pci_conf[0x02] = 0x30;
    dev->pci_conf[0x03] = 0x1a;
    dev->pci_conf[0x04] = 6;
    dev->pci_conf[0x06] = 0x90;
    dev->pci_conf[0x08] = 3;
    dev->pci_conf[0x0b] = 6;
    dev->pci_conf[0x10] = 8;
    dev->pci_conf[0x34] = 0xa0;
    dev->pci_conf[0x78] = 0x10;
    dev->pci_conf[0x7c] = 1;
    dev->pci_conf[0x9d] = 2;
    dev->pci_conf[0x9e] = 0x38;
    dev->pci_conf[0xa0] = 2;
    dev->pci_conf[0xa2] = 0x20;
    dev->pci_conf[0xa4] = 0x16;
    dev->pci_conf[0xa5] = 2;
    dev->pci_conf[0xa7] = 0x1f;
    dev->pci_conf[0xe4] = 9;
    dev->pci_conf[0xe5] = 0xa0;
    dev->pci_conf[0xe6] = 4;
    dev->pci_conf[0xe7] = 0xf1;

    intel_mch_p4_pam(0x90, dev);
    intel_mch_p4_pam(0x91, dev);
    intel_mch_p4_pam(0x92, dev);
    intel_mch_p4_pam(0x93, dev);
    intel_mch_p4_pam(0x94, dev);
    intel_mch_p4_pam(0x95, dev);
    intel_mch_p4_pam(0x96, dev);

    intel_mch_p4_smram(dev);
}


static void
intel_mch_p4_close(void *priv)
{
    intel_mch_p4_t *dev = (intel_mch_p4_t *)priv;

    smram_del(dev->upper_smram_tseg);
    smram_del(dev->upper_smram_hseg);
    smram_del(dev->low_smram);
    free(dev);
}


static void *
intel_mch_p4_init(const device_t *info)
{
    intel_mch_p4_t *dev = (intel_mch_p4_t *)malloc(sizeof(intel_mch_p4_t));
    memset(dev, 0, sizeof(intel_mch_p4_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, intel_mch_p4_read, intel_mch_p4_write, dev);

    /* AGP Bridge */
    device_add(&intel_mch_p4_agp_device);

    /* Cache */
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    /* DDR */
    dev->is_ddr = info->local;

    /* SMRAM */
    dev->upper_smram_tseg = smram_add(); /* SMRAM High TSEG */
    dev->upper_smram_hseg = smram_add(); /* SMRAM High HSEG */
    dev->low_smram = smram_add(); /* SMRAM Low  */

    intel_mch_p4_reset(dev);

    return dev;
}

const device_t intel_mch_p4_device = {
    "Intel i845 SDRAM (MCH) Chipset",
    DEVICE_PCI,
    0,
    intel_mch_p4_init, intel_mch_p4_close, intel_mch_p4_reset,
    { NULL }, NULL, NULL,
    NULL
};

const device_t intel_mch_p4_ddr_device = {
    "Intel i845 DDR (MCH) Chipset",
    DEVICE_PCI,
    1,
    intel_mch_p4_init, intel_mch_p4_close, intel_mch_p4_reset,
    { NULL }, NULL, NULL,
    NULL
};
