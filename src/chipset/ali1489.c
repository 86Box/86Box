/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M1489 chipset.
 *
 *
 *
 *      Authors: Tiseno100
 *
 *		Copyright 2020 Tiseno100
 *
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
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/pci.h>
#include <86box/dma.h>
#include <86box/hdc_ide.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#define disabled_shadow (MEM_READ_EXTANY | MEM_WRITE_EXTANY)
#define ENABLE_ALI1489_LOG 0

#ifdef ENABLE_ALI1489_LOG
int ali1489_do_log = ENABLE_ALI1489_LOG;
static void
ali1489_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1489_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ali1489_log(fmt, ...)
#endif

typedef struct
{
    uint8_t	index, ide_index, ide_chip_id,
	regs[256], pci_conf[256], ide_regs[256];
    port_92_t * port_92;

} ali1489_t;

static void
ali1489_defaults(void *priv)
{
    ali1489_t *dev = (ali1489_t *) priv;

    /* IDE registers */
    dev->ide_regs[0x01] = 0x02;
    dev->ide_regs[0x08] = 0xff;
    dev->ide_regs[0x09] = 0x41;
    dev->ide_regs[0x34] = 0xff;
    dev->ide_regs[0x35] = 0x01;

    /* PCI registers */
    dev->pci_conf[0x00] = 0xb9;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x89;
    dev->pci_conf[0x03] = 0x14;
    dev->pci_conf[0x04] = 0x07;
    dev->pci_conf[0x07] = 0x04;
    dev->pci_conf[0x0b] = 0x06;

    /* ISA registers */
    dev->regs[0x01] = 0x0f;
    dev->regs[0x02] = 0x0f;
    dev->regs[0x10] = 0xf1;
    dev->regs[0x11] = 0xff;
    dev->regs[0x13] = 0x00;
    dev->regs[0x14] = 0x00;
    dev->regs[0x15] = 0x20;
    dev->regs[0x16] = 0x30;
    dev->regs[0x19] = 0x04;
    dev->regs[0x21] = 0x72;
    dev->regs[0x28] = 0x02;
    dev->regs[0x2b] = 0xdb;
    dev->regs[0x3c] = 0x03;
    dev->regs[0x3d] = 0x01;
    dev->regs[0x40] = 0x03;

}

static void ali1489_shadow_recalc(ali1489_t *dev)
{

uint32_t base, i;


for(i = 0; i < 8; i++){
    
base = 0xc0000 + (i << 14);
if(dev->regs[0x13] & (1 << i))
mem_set_mem_state_both(base, 0x4000, ((dev->regs[0x14] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x14] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
else
mem_set_mem_state_both(base, 0x4000, disabled_shadow);

}


for(i = 0; i < 4; i++){
base = 0xe0000 + (i << 15);

shadowbios = (dev->regs[0x14] & 0x10);
shadowbios_write = (dev->regs[0x14] & 0x20);

if(dev->regs[0x14] & (1 << i))
mem_set_mem_state_both(base, 0x8000, ((dev->regs[0x14] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x14] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
else
mem_set_mem_state_both(base, 0x8000, disabled_shadow);
}

flushmmucache();
}

static void ali1489_smm_recalc(ali1489_t *dev)
{
    if((dev->regs[0x19] & 0x08) && (((dev->regs[0x19] & 0x03) << 4) != 0x00))
    {
        if(((dev->regs[0x19] & 0x03) << 4) & 0x01)
        {
        mem_set_mem_state_smm(0xa0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        }
            
        if(((dev->regs[0x19] & 0x03) << 4) & 0x02)
        {
        mem_set_mem_state_smm(0xe0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        }

        if(((dev->regs[0x19] & 0x03) << 4) & 0x03)
        {
        mem_set_mem_state_smm(0x38000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        }
    }
}

static void
ali1489_write(uint16_t addr, uint8_t val, void *priv)
{
    ali1489_t *dev = (ali1489_t *) priv;

    switch (addr) {
	case 0x22:
		dev->index = val;
		break;
	case 0x23:

		dev->regs[dev->index] = val;

        if(dev->regs[0x03] == 0xc5) /* Check if the configuration registers are unlocked */
        {
        switch(dev->index){

            case 0x10: /* DRAM Configuration Register I */
            case 0x11: /* DRAM Configuration Register II */
            case 0x12: /* ROM Function Register */
            dev->regs[dev->index] = val;
            break;

            case 0x13: /* Shadow Region Register */
            case 0x14: /* Shadow Control Register */

            if(dev->index == 0x14)
            dev->regs[dev->index] = (val & 0xbf);
            else
            {
            dev->regs[dev->index] = val;
            }

            ali1489_shadow_recalc(dev);
            break;

            case 0x15: /* Cycle Check Point Control Register */
            dev->regs[dev->index] = (val & 0xf1);
            break;

            case 0x16: /* Cache Control Register I */
            dev->regs[dev->index] = val;
            cpu_cache_int_enabled = (val & 0x01);
            cpu_cache_ext_enabled = (val & 0x02);
            break;

            case 0x17: /* Cache Control Register II */
            dev->regs[dev->index] = val;
            break;

            case 0x19: /* SMM Control Register */
            dev->regs[dev->index] = val;
            ali1489_smm_recalc(dev);
            break;

            case 0x1a: /* EDO DRAM Configuration Register */
            case 0x1b: /* DRAM Timing Control Register */
            case 0x1c: /* Memory Data Buffer Direction Control Register */
            dev->regs[dev->index] = val;
            break;

            case 0x1e: /* Linear Wrapped Burst Order Mode Control Register */
            dev->regs[dev->index] = (val & 0x40);
            break;

            case 0x20: /* CPU to PCI Buffer Control Register */
            case 0x21: /* DEVSELJ Check Point Setting Register */
            dev->regs[dev->index] = val;
            break;

            case 0x22: /* PCI to CPU W/R Buffer Configuration Register */
            dev->regs[dev->index] = (val & 0xfd);
            break;

            case 0x25: /* GP/MEM Address Definition Register I */
            case 0x26: /* GP/MEM Address Definition Register II */
            case 0x27: /* GP/MEM Address Definition Register III */
            case 0x28: /* PCI Arbiter Control Register */
            dev->regs[dev->index] = val;
            break;

            case 0x29: /* System Clock Register */
            dev->regs[dev->index] = val;

            if(val & 0x10)
            port_92_add(dev->port_92);
            else
            port_92_remove(dev->port_92);
            break;

            case 0x2a: /* I/O Recovery Register */
            dev->regs[dev->index] = val;
            break;

            case 0x2b: /* Turbo Function Register */
            dev->regs[dev->index] = (val & 0xbf);
            break;

            case 0x30: /* Power Management Unit Control Register */
            case 0x31: /* Mode Timer Monitoring Events Selection Register I */
            case 0x32: /* Mode Timer Monitoring Events Selection Register II */
            case 0x33: /* SMI Triggered Events Selection Register I */
            case 0x34: /* SMI Triggered Events Selection Register II */
            case 0x35: /* SMI Status Register */
            dev->regs[dev->index] = val;
            break;

            case 0x36: /* IRQ Channel Group Selected Control Register I */
            dev->regs[dev->index] = (val & 0xe5);
            break;

            case 0x37: /* IRQ Channel Group Selected Control Register II */
            dev->regs[dev->index] = (val & 0xef);
            break;

            case 0x38: /* DRQ Channel Selected Control  Register */
            case 0x39: /* Mode Timer Setting Register */
            case 0x3a: /* Input_device Timer Setting Register */
            case 0x3b: /* GP/MEM Timer Setting Register */
            case 0x3c: /* LED Flash Control Register */
            dev->regs[dev->index] = val;
            break;

            case 0x3d: /* Miscellaneous Register I */
            dev->regs[dev->index] = (val & 0x07);
            break;

            case 0x3f: /* Shadow Port 70h Register */
            dev->regs[dev->index] = val;
            break;

            case 0x40: /* Clock Generator Control Feature Register */
            dev->regs[dev->index] = (val & 0x3f);
            break;

            case 0x41: /* Power Control Output Register */
            dev->regs[dev->index] = val;
            break;

            case 0x42: /* PCI INTx Routing Table Mapping Register I */
            if((val & 0x0f) != 0)
            pci_set_irq(PCI_INTA, (val & 0x0f));
            else
            pci_set_irq(PCI_INTA, PCI_IRQ_DISABLED);

            if(((val & 0x0f) << 4) != 0)
            pci_set_irq(PCI_INTB, ((val & 0x0f) << 4));
            else
            pci_set_irq(PCI_INTB, PCI_IRQ_DISABLED);
            break;

            case 0x43: /* PCI INTx Routing Table Mapping Register II */
            if((val & 0x0f) != 0)
            pci_set_irq(PCI_INTC, (val & 0x0f));
            else
            pci_set_irq(PCI_INTC, PCI_IRQ_DISABLED);

            if(((val & 0x0f) << 4) != 0)
            pci_set_irq(PCI_INTD, ((val & 0x0f) << 4));
            else
            pci_set_irq(PCI_INTD, PCI_IRQ_DISABLED);
            break;

            case 0x44: /* PCI INTx Sensitivity Register */
            dev->regs[dev->index] = val;
            break;
            
        }

        if(dev->index != 0x03)
        {
        ali1489_log("M1489: dev->regs[%02x] = %02x\n", dev->index, val);
        }

        }

		break;
    }
}


static uint8_t
ali1489_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    ali1489_t *dev = (ali1489_t *) priv;

    switch (addr) {
	case 0x23:

        if(((dev->index == 0x20) || (dev->index >= 0xc0)) && cpu_iscyrix) /* Avoid conflict with Cyrix CPU registers */
        ret = 0xff;
        else
        {
		ret = dev->regs[dev->index];
        }

		break;
    }

    return ret;
}

static void
ali1489_pci_write(int func, int addr, uint8_t val, void *priv)
{

ali1489_t *dev = (ali1489_t *) priv;

ali1489_log("M1489-PCI: dev->regs[%02x] = %02x\n", addr, val);

switch (addr)
{
    /* Dummy PCI Config */
    case 0x04:
    dev->pci_conf[0x04] = (dev->pci_conf[0x04] & ~0x07) | (val & 0x07);
    break;

    /* Dummy PCI Status */
    case 0x07:
    dev->pci_conf[0x07] = val;
    break;
}

}

static uint8_t
ali1489_pci_read(int func, int addr, void *priv)
{
    ali1489_t *dev = (ali1489_t *) priv;
    uint8_t ret = 0xff;
	
    ret = dev->pci_conf[addr];

    return ret;
}

static void
ali1489_ide_write(uint16_t addr, uint8_t val, void *priv)
{

ali1489_t *dev = (ali1489_t *) priv;

    switch (addr) {
    case 0xf4: /* Usually it writes 30h here */
        dev->ide_chip_id = val;
        break;

	case 0xf8:
		dev->ide_index = val;
		break;
    
	case 0xfc:
        ali1489_log("M1489-IDE: dev->regs[%02x] = %02x\n", dev->ide_index, val);
		dev->ide_regs[dev->ide_index] = val;

        ide_pri_disable();
        ide_sec_disable();

        if(dev->ide_regs[0x01] & 0x01){ /*The datasheet doesn't clearly explain the channel selection */
        ide_pri_enable();               /*So we treat it according to the chipset programming manual. */
        ide_set_base(0, 0x1f0);
        ide_set_side(0, 0x3f6);

        if(!(dev->ide_regs[0x35] & 0x41)){
        ide_sec_enable();
        ide_set_base(1, 0x170);
        ide_set_side(1, 0x376);
        }

        }

        break;
    }

}

static uint8_t
ali1489_ide_read(uint16_t addr, void *priv)
{

    uint8_t ret = 0xff;
    ali1489_t *dev = (ali1489_t *) priv;

    switch (addr) {
    case 0xf4:
        ret = dev->ide_chip_id;
        break;
	case 0xfc:
		ret = dev->ide_regs[dev->ide_index];
		break;
    }

    return ret;
}

static void
ali1489_reset(void *priv)
{

ali1489_t *dev = (ali1489_t *) priv;

ide_pri_disable();
ide_sec_disable();

pci_set_irq(PCI_INTA, PCI_IRQ_DISABLED);
pci_set_irq(PCI_INTB, PCI_IRQ_DISABLED);
pci_set_irq(PCI_INTC, PCI_IRQ_DISABLED);
pci_set_irq(PCI_INTD, PCI_IRQ_DISABLED);

ali1489_defaults(dev);

}

static void
ali1489_close(void *priv)
{
    ali1489_t *dev = (ali1489_t *) priv;

    free(dev);
}


static void *
ali1489_init(const device_t *info)
{
    ali1489_t *dev = (ali1489_t *) malloc(sizeof(ali1489_t));
    memset(dev, 0, sizeof(ali1489_t));

    /*
    M1487/M1489
    22h Index Port
    23h Data Port
    */
    io_sethandler(0x022, 0x0001, ali1489_read, NULL, NULL, ali1489_write, NULL, NULL, dev);
    io_sethandler(0x023, 0x0001, ali1489_read, NULL, NULL, ali1489_write, NULL, NULL, dev);

    /*
    M1489 IDE controller
    F4h Chip ID we write always 30h onto it
    F8h Index Port
    FCh Data Port
    */
    io_sethandler(0x0f4, 0x0001, ali1489_ide_read, NULL, NULL, ali1489_ide_write, NULL, NULL, dev);
    io_sethandler(0x0f8, 0x0001, ali1489_ide_read, NULL, NULL, ali1489_ide_write, NULL, NULL, dev);
    io_sethandler(0x0fc, 0x0001, ali1489_ide_read, NULL, NULL, ali1489_ide_write, NULL, NULL, dev);

    /* Dummy M1489 PCI device */
    pci_add_card(0, ali1489_pci_read, ali1489_pci_write, dev);

    ide_pri_disable();
    ide_sec_disable();

    dev->port_92 = device_add(&port_92_pci_device);

    pci_set_irq(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq(PCI_INTD, PCI_IRQ_DISABLED);

    ali1489_defaults(dev);

    ali1489_shadow_recalc(dev);

    return dev;
}


const device_t ali1489_device = {
    "ALi M1489",
    0,
    0,
    ali1489_init,
    ali1489_close,
    ali1489_reset,
    NULL,
    NULL,
    NULL,
    NULL
};
