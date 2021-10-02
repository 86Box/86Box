/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel i815EP GMCH Chipset.
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
#include <86box/port_92.h>
#include <86box/spd.h>
#include <86box/smram.h>

#include <86box/chipset.h>

#define ENABLE_INTEL_GMCH_LOG 1
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

	uint8_t pci_conf[2][256];
    smram_t	*smram[3];

} intel_gmch_t;

static void
intel_gmch_pam(int cur_reg, intel_gmch_t *dev)
{
    if(cur_reg == 0x59)
        mem_set_mem_state_both(0xf0000, 0x10000, ((dev->pci_conf[0][0x59] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[0][0x59] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    else
    {
        int negate = cur_reg - 0x5a;
        mem_set_mem_state_both(0xc0000 + (negate << 15), 0x4000, ((dev->pci_conf[0][cur_reg] & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[0][cur_reg] & 2) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_both(0xc4000 + (negate << 15), 0x4000, ((dev->pci_conf[0][cur_reg] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[0][cur_reg] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }
}

static void
intel_gmch_smram(intel_gmch_t *dev)
{
    if(!(dev->pci_conf[0][0x70] & 2))
    {
        smram_disable_all();

        if((((dev->pci_conf[0][0x70] >> 4) & 3) >= 1) && (((dev->pci_conf[0][0x70] >> 2) & 3) == 0))
        {
            /* Top Remap Goes Here */
        }

        if(((dev->pci_conf[0][0x70] >> 4) & 3) >= 2)
        {
            switch((dev->pci_conf[0][0x70] >> 4) & 3)
            {
                case 2:
                    smram_enable(dev->smram[1], 0xfeea0000, 0x000a0000, 0x10000, 0, 1);
                break;

                case 3:
                    smram_enable(dev->smram[1], 0xfeea0000, 0x000a0000, 0x20000, 0, 1);
                break;
            }
        }

        switch((dev->pci_conf[0][0x70] >> 2) & 3)
        {
            case 0:
                mem_set_mem_state_both(0xa0000, 0x20000, MEM_READ_DISABLED | MEM_WRITE_DISABLED);
            break;

            case 1:
                mem_set_mem_state_both(0xa0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
            break;

            case 2:
                smram_enable(dev->smram[2], 0x000a0000, 0x000a0000, 0x20000, 0, 1);
                mem_set_mem_state_smram_ex(0, 0xa0000, 0x20000, 2);
            break;

            case 3:
                smram_enable(dev->smram[2], 0x000a0000, 0x000a0000, 0x20000, 0, 1);
            break;
        }

    }
}

static void
intel_gmch_write(int func, int addr, uint8_t val, void *priv)
{
    intel_gmch_t *dev = (intel_gmch_t *)priv;

    intel_gmch_log("Intel 815EP: dev->regs[%02x] = %02x POST: %02x \n", addr, val, inb(0x80));

    if(func == 0)
    switch(addr)
    {
        case 0x52: /* DRAM Banking */
        case 0x54:
            dev->pci_conf[func][addr] = val;
        break;

        case 0x59 ... 0x5f: /* PAM */
            dev->pci_conf[func][addr] = val;
            intel_gmch_pam(addr, dev);
        break;

        case 0x70: /* SMRAM */
            dev->pci_conf[func][addr] = val;
            intel_gmch_smram(dev);
        break;

        default:
            dev->pci_conf[func][addr] = val;
        break;
    }
}


static uint8_t
intel_gmch_read(int func, int addr, void *priv)
{
    intel_gmch_t *dev = (intel_gmch_t *)priv;
    uint8_t ret = 0xff;

    if (func == 0)
	ret = dev->pci_conf[func][addr];

    return ret;
}


static void
intel_gmch_reset(void *priv)
{
    intel_gmch_t *dev = (intel_gmch_t *)priv;
    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));

    dev->pci_conf[0][0x00] = 0x86;
    dev->pci_conf[0][0x01] = 0x80;
    dev->pci_conf[0][0x02] = 0x30;
    dev->pci_conf[0][0x03] = 0x11;
    dev->pci_conf[0][0x04] = 6;
    dev->pci_conf[0][0x06] = 0x90;
    dev->pci_conf[0][0x08] = 2;
    dev->pci_conf[0][0x0b] = 6;
    dev->pci_conf[0][0x10] = 8;
    dev->pci_conf[0][0x34] = 0xa0;
    dev->pci_conf[0][0x50] = 0x40;
    dev->pci_conf[0][0x88] = 9;
    dev->pci_conf[0][0x89] = 0xa0;
    dev->pci_conf[0][0x8a] = 4;
    dev->pci_conf[0][0x8b] = 0xf1;
    dev->pci_conf[0][0x92] = dev->pci_conf[0][0x93] = dev->pci_conf[0][0x94] = dev->pci_conf[0][0x95] = 0xff;
    dev->pci_conf[0][0xa0] = 2;
    dev->pci_conf[0][0xa2] = 0x20;
    dev->pci_conf[0][0xa4] = 7;
    dev->pci_conf[0][0xa5] = 2;
    dev->pci_conf[0][0xa7] = 0x1f;

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

    smram_del(dev->smram[0]);
    smram_del(dev->smram[1]);
    smram_del(dev->smram[2]);
    free(dev);
}


static void *
intel_gmch_init(const device_t *info)
{
    intel_gmch_t *dev = (intel_gmch_t *)malloc(sizeof(intel_gmch_t));
    memset(dev, 0, sizeof(intel_gmch_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, intel_gmch_read, intel_gmch_write, dev);

    /* Port 92 */
    device_add(&port_92_pci_device);

    /* SMRAM */
    dev->smram[0] = smram_add(); /* SMRAM High TSEG */
    dev->smram[1] = smram_add(); /* SMRAM High HSEG */
    dev->smram[2] = smram_add(); /* SMRAM Low  */

    intel_gmch_reset(dev);

    return dev;
}


const device_t intel_gmch_device = {
    "Intel i815EP(GMCH) Chipset",
    DEVICE_PCI,
    0,
    intel_gmch_init, intel_gmch_close, intel_gmch_reset,
    { NULL }, NULL, NULL,
    NULL
};
