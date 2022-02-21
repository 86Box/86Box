/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel i815EP (GMCH) Chipset.
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

#ifdef ENABLE_INTEL_GMCH_LOG
int intel_gmch_do_log = ENABLE_INTEL_GMCH_LOG;


static void
intel_gmch_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_gmch_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define intel_gmch_log(fmt, ...)
#endif

typedef struct intel_gmch_t
{

	uint8_t pci_conf[256];
    smram_t	*low_smram;
    smram_t *upper_smram_hseg;
    smram_t *upper_smram_tseg;

} intel_gmch_t;

static void
intel_gmch_pam(int cur_reg, intel_gmch_t *dev)
{
    if(cur_reg == 0x59)
        mem_set_mem_state_both(0xf0000, 0x10000, ((dev->pci_conf[0x59] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[0x59] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    else
    {
        int negate = cur_reg - 0x5a;
        mem_set_mem_state_both(0xc0000 + (negate << 15), 0x4000, ((dev->pci_conf[cur_reg] & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 2) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_both(0xc4000 + (negate << 15), 0x4000, ((dev->pci_conf[cur_reg] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }

    flushmmucache_nopc();
}

static void
intel_gmch_smram(intel_gmch_t *dev)
{
    uint32_t tom = (mem_size << 10);
    uint32_t tom_size = 0;

    smram_disable_all();

    if(((dev->pci_conf[0x70] >> 4) & 3) != 0) {
        if(((dev->pci_conf[0x70] >> 4) & 3) >= 2) { /* Top Remapping based on intel_4x0.c by OBattler */
            /* Phase 0 */
            tom -= (1 << 20);
            mem_set_mem_state_smm(tom, (1 << 20), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

            /* Phase 1 */
            tom = (mem_size << 10);
            tom_size = (((dev->pci_conf[0x70] >> 4) & 3) == 3) ? 0x100000 : 0x80000; /* 3: 1MB / 2: 512KB */
            tom -= tom_size;

            smram_enable(dev->upper_smram_tseg, tom + (1 << 28), tom, tom_size, 0, 1);
            mem_set_mem_state_smm(tom, tom_size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
            mem_set_mem_state_smram_ex(0, tom, tom_size, 0); /* All Non-SMM accesses are forwarded to the Hub */
        }

        if(((dev->pci_conf[0x70] >> 2) & 3) == 0) {
            smram_enable(dev->upper_smram_hseg, 0xfeea0000, 0x000a0000, 0x20000, 0, 1);
            mem_set_mem_state_smram_ex(0, 0xfeea0000, 0x00020000, 0); /* All Non-SMM accesses are forwarded to the Hub */
        }
    }

    switch((dev->pci_conf[0x70] >> 2) & 3)
    {
        case 1:
            smram_enable(dev->low_smram, 0x000a0000, 0x000a0000, 0x20000, 1, 1);
        break;

        case 2:
            smram_enable(dev->low_smram, 0x000a0000, 0x000a0000, 0x20000, 0, 1);
            mem_set_mem_state_smram_ex(1, 0x000a0000, 0x00020000, 2);
        break;

        case 3:
            smram_enable(dev->low_smram, 0x000a0000, 0x000a0000, 0x20000, 0, 1);
		    mem_set_mem_state_smram_ex(1, 0x000a0000, 0x20000, 1); /* All Non-SMM accesses are forwarded to the Hub */
	        mem_set_mem_state_smram_ex(0, 0x000a0000, 0x20000, 0);
        break;
    }

    flushmmucache();
}

static void
intel_gmch_write(int func, int addr, uint8_t val, void *priv)
{

    intel_gmch_t *dev = (intel_gmch_t *)priv;

    if (func == 0) {
        intel_gmch_log("Intel i815EP: dev->regs[%02x] = %02x POST: %02x \n", addr, val, inb(0x80));

        switch(addr)
        {
            case 0x04:
                dev->pci_conf[addr] = val;
            break;

            case 0x06:
                dev->pci_conf[addr] &= val & 0x90;
            break;

            case 0x07:
                dev->pci_conf[addr] &= val;
            break;

            case 0x13:
                dev->pci_conf[addr] = val & 0xfe;
            break;

            case 0x2c ... 0x2f:
                if(dev->pci_conf[addr] == 0)
                    dev->pci_conf[addr] = val;
            break;

            case 0x50:
                dev->pci_conf[addr] = val & 0xdc;
            break;

            case 0x51:
                dev->pci_conf[addr] = val & 7;
            break;

            case 0x52: /* DRAM Population */
            case 0x54:
                spd_write_drbs_intel_gmch(dev->pci_conf);
                dev->pci_conf[addr] = val;
            break;

            case 0x53:
                dev->pci_conf[addr] = val & 0xf7;
            break;

            case 0x56:
                dev->pci_conf[addr] = val & 0x80;
            break;

            case 0x59 ... 0x5f: /* PAM */
                dev->pci_conf[addr] = (addr != 0x59) ? (val & 0x33) : (val & 0x30);
                intel_gmch_pam(addr, dev);
            break;

            case 0x70: /* SMRAM */
                if(!(dev->pci_conf[addr] & 2)) { /* D_LCK Locks SMRAM Registers from being configured. Clear only possible via reset */
                    dev->pci_conf[addr] = val;
                    intel_gmch_smram(dev);
                }
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
                dev->pci_conf[addr] = val & 0x77;
            break;

            case 0x9b:
                dev->pci_conf[addr] = val & 0x80;
            break;

            case 0x9d:
                dev->pci_conf[addr] = val & 0x80;
            break;

            case 0xa8:
                dev->pci_conf[addr] = val & 0x37;
            break;

            case 0xa9:
                dev->pci_conf[addr] = val & 0x03;
            break;

            case 0xb0:
                dev->pci_conf[addr] = val & 0x81;
            break;

            case 0xb4:
                dev->pci_conf[addr] = val & 8;
            break;

            case 0xb9:
                dev->pci_conf[addr] = val & 0xf0;
            break;

            case 0xba:
                dev->pci_conf[addr] = val & 0xff;
            break;

            case 0xbb:
                dev->pci_conf[addr] = val & 0x1f;
            break;

            case 0xbc ... 0xbd:
                dev->pci_conf[addr] = val & 0xf8;
            break;

            case 0xbe:
                dev->pci_conf[addr] = val & 0x20;
            break;

            case 0xcb:
                dev->pci_conf[addr] = val & 0x3f;
            break;
        }
    }

}


static uint8_t
intel_gmch_read(int func, int addr, void *priv)
{
    intel_gmch_t *dev = (intel_gmch_t *)priv;

    if (func == 0) {
        intel_gmch_log("Intel i815EP: dev->regs[%02x] (%02x) POST: %02x \n", addr, dev->pci_conf[addr], inb(0x80));
        return dev->pci_conf[addr];
    }
    else return 0xff;
}


static void
intel_gmch_reset(void *priv)
{
    intel_gmch_t *dev = (intel_gmch_t *)priv;
    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));

    dev->pci_conf[0x00] = 0x86;
    dev->pci_conf[0x01] = 0x80;
    dev->pci_conf[0x02] = 0x30;
    dev->pci_conf[0x03] = 0x11;
    dev->pci_conf[0x04] = 6;
    dev->pci_conf[0x06] = 0x90;
    dev->pci_conf[0x08] = 2;
    dev->pci_conf[0x0b] = 6;
    dev->pci_conf[0x10] = 8;
    dev->pci_conf[0x34] = 0xa0;
    dev->pci_conf[0x50] = 0x40;
    dev->pci_conf[0x88] = 9;
    dev->pci_conf[0x89] = 0xa0;
    dev->pci_conf[0x8a] = 4;
    dev->pci_conf[0x8b] = 0xf1;
    dev->pci_conf[0x92] = dev->pci_conf[0x93] = dev->pci_conf[0x94] = dev->pci_conf[0x95] = 0xff;
    dev->pci_conf[0xa0] = 2;
    dev->pci_conf[0xa2] = 0x20;
    dev->pci_conf[0xa4] = 7;
    dev->pci_conf[0xa5] = 2;
    dev->pci_conf[0xa7] = 0x1f;

    intel_gmch_pam(0x59, dev);
    intel_gmch_pam(0x5a, dev);
    intel_gmch_pam(0x5b, dev);
    intel_gmch_pam(0x5c, dev);
    intel_gmch_pam(0x5d, dev);
    intel_gmch_pam(0x5e, dev);
    intel_gmch_pam(0x5f, dev);

    intel_gmch_smram(dev);
}


static void
intel_gmch_close(void *priv)
{
    intel_gmch_t *dev = (intel_gmch_t *)priv;

    smram_del(dev->upper_smram_tseg);
    smram_del(dev->upper_smram_hseg);
    smram_del(dev->low_smram);
    free(dev);
}


static void *
intel_gmch_init(const device_t *info)
{
    intel_gmch_t *dev = (intel_gmch_t *)malloc(sizeof(intel_gmch_t));
    memset(dev, 0, sizeof(intel_gmch_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, intel_gmch_read, intel_gmch_write, dev);

    /* AGP Bridge */
    device_add(&intel_gmch_agp_device);

    /* Cache */
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    /* SMRAM */
    dev->upper_smram_tseg = smram_add(); /* SMRAM High TSEG */
    dev->upper_smram_hseg = smram_add(); /* SMRAM High HSEG */
    dev->low_smram = smram_add(); /* SMRAM Low  */

    intel_gmch_reset(dev);

    return dev;
}

const device_t intel_gmch_device = {
    "Intel i815EP (GMCH) Chipset",
    "i815ep",
    DEVICE_PCI,
    0,
    intel_gmch_init, intel_gmch_close, intel_gmch_reset,
    { NULL }, NULL, NULL,
    NULL
};
