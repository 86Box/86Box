/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M1487/89 chipset.
 *
 *
 *      Note: This is a very basic implementation of it as of now. Enough to boot
 *      and do basic tasks like running an OS.
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
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#define disabled_shadow (MEM_READ_EXTANY | MEM_WRITE_EXTANY)
#define pci_irq_bit03 (val & 0xf)
#define pci_irq_bit47 ((val & 0xf) >> 4)

typedef struct
{

    uint8_t
    index, 
    reg_lock,
	regs[256];

    port_92_t * port_92;

} ali1489_t;

static void ali1489_shadow_recalc(ali1489_t *dev)
{

uint32_t base, i;
uint32_t shflags, canread, canwrite;

canread = (dev->regs[0x14] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
canwrite = (dev->regs[0x14] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;

shflags = canread | canwrite;

for(i = 0; i < 8; i++){
    base = 0xc0000 + (i << 14);

    if(dev->regs[0x13] & (1 << i))
    mem_set_mem_state_both(base, 0x4000, shflags);
    else
    mem_set_mem_state_both(base, 0x4000, disabled_shadow);
    
}

for(i = 0; i < 4; i++){
    base = 0xe0000 + (i << 15);

    if(dev->regs[0x14] & (1 << i)){
    shadowbios = (dev->regs[0x14] & 0x10) & (base >= 0xe0000);
    shadowbios_write = (dev->regs[0x14] & 0x20) & (base >= 0xe0000);
    mem_set_mem_state_both(base, 0x8000, shflags);
    }
    else
    mem_set_mem_state_both(base, 0x8000, disabled_shadow);
    
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
        pclog("M1489: dev->regs[%02x] = %02x\n", dev->index, val);
		dev->regs[dev->index] = val;

        switch(dev->index){

            /* Shadow RAM */
            case 0x13:
            case 0x14:
            ali1489_shadow_recalc(dev);
            break;

            /* Cache */
            case 0x16:
            cpu_cache_int_enabled = (val & 0x01);
            cpu_cache_ext_enabled = (val & 0x02);
            cpu_update_waitstates();
            break;

            /* SMM */

            /* Port 92 */
            case 0x29:
            if(dev->regs[0x29] & 0x10)
            port_92_add(dev->port_92);
            else
            port_92_remove(dev->port_92);
            break;

            /* PCI IRQ Steering */
            case 0x42:
            pci_set_irq_routing(PCI_INTA, (!(pci_irq_bit03 & 0x00)) ? pci_irq_bit03 : PCI_IRQ_DISABLED);
            pci_set_irq_routing(PCI_INTB, (!(pci_irq_bit47 & 0x00)) ? pci_irq_bit47 : PCI_IRQ_DISABLED);
            break;
            case 0x43:
            pci_set_irq_routing(PCI_INTC, (!(pci_irq_bit03 & 0x00)) ? pci_irq_bit03 : PCI_IRQ_DISABLED);
            pci_set_irq_routing(PCI_INTD, (!(pci_irq_bit47 & 0x00)) ? pci_irq_bit47 : PCI_IRQ_DISABLED);
            break;
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

        if (((dev->index >= 0xc0) || (dev->index == 0x20)) && cpu_iscyrix)
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
ali1489_close(void *priv)
{
    ali1489_t *dev = (ali1489_t *) priv;

    free(dev);
}

static void
ali1489_reset(void *priv)
{  

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

}

static void *
ali1489_init(const device_t *info)
{
    ali1489_t *dev = (ali1489_t *) malloc(sizeof(ali1489_t));
    memset(dev, 0, sizeof(ali1489_t));

    dev->port_92 = device_add(&port_92_pci_device);

    io_sethandler(0x022, 0x0001, ali1489_read, NULL, NULL, ali1489_write, NULL, NULL, dev);
    io_sethandler(0x023, 0x0001, ali1489_read, NULL, NULL, ali1489_write, NULL, NULL, dev);

    dev->regs[0x03] = 0xc5;
    dev->regs[0x13] = 0x00;
    dev->regs[0x14] = 0x00;
    ali1489_reset(dev);

    return dev;
}


const device_t ali1489_device = {
    "ALi M1489",
    DEVICE_PCI,
    0,
    ali1489_init, ali1489_close, ali1489_reset,
    NULL, NULL, NULL,
    NULL
};
