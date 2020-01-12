/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the VIA Apollo MVP3 southbridge
 *
 * Version:	@(#)via_mvp3_sb.c	1.0.22	2018/10/31
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *      Melissa Goad, <mszoopers@protonmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *      Copyright 2020 Melissa Goad.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "cdrom/cdrom.h"
#include "cpu/cpu.h"
#include "scsi/scsi_device.h"
#include "scsi/scsi_cdrom.h"
#include "dma.h"
#include "io.h"
#include "device.h"
#include "apm.h"
#include "keyboard.h"
#include "mem.h"
#include "pci.h"
#include "pic.h"
#include "port_92.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "disk/hdc_ide_sff8038i.h"
#include "disk/zip.h"
#include "machine/machine.h"
#include "via_mvp3_sb.h"

typedef struct
{
    uint8_t pci_isa_regs[256];
    uint8_t ide_regs[256];
    uint8_t usb_regs[256];
    uint8_t power_regs[256];
    sff8038i_t *bm[2];
} via_mvp3_sb_t;

static void
via_mvp3_sb_reset_hard(void *priv)
{
    via_mvp3_sb_t *via_mvp3_sb = (via_mvp3_sb_t *) priv;

    uint16_t old_base = (via_mvp3_sb->ide_regs[0x20] & 0xf0) | (via_mvp3_sb->ide_regs[0x21] << 8);

    sff_bus_master_reset(via_mvp3_sb->bm[0], old_base);
    sff_bus_master_reset(via_mvp3_sb->bm[1], old_base + 8);

    memset(via_mvp3_sb->pci_isa_regs, 0, 256);
    memset(via_mvp3_sb->ide_regs, 0, 256);
    memset(via_mvp3_sb->usb_regs, 0, 256);
    memset(via_mvp3_sb->power_regs, 0, 256);

    via_mvp3_sb->pci_isa_regs[0x00] = 0x06; via_mvp3_sb->pci_isa_regs[0x01] = 0x11; /*VIA*/
    via_mvp3_sb->pci_isa_regs[0x02] = 0x86; via_mvp3_sb->pci_isa_regs[0x03] = 0x05; /*VT82C586B*/
    via_mvp3_sb->pci_isa_regs[0x04] = 0x0f;
    via_mvp3_sb->pci_isa_regs[0x07] = 0x02;
    via_mvp3_sb->pci_isa_regs[0x0a] = 0x01;
    via_mvp3_sb->pci_isa_regs[0x0b] = 0x06;
    via_mvp3_sb->pci_isa_regs[0x0e] = 0x80;

    via_mvp3_sb->pci_isa_regs[0x48] = 0x01;
    via_mvp3_sb->pci_isa_regs[0x4a] = 0x04;
    via_mvp3_sb->pci_isa_regs[0x4f] = 0x03;

    via_mvp3_sb->pci_isa_regs[0x50] = 0x24;
    via_mvp3_sb->pci_isa_regs[0x59] = 0x04;

    //IDE registers
    via_mvp3_sb->ide_regs[0x00] = 0x06; via_mvp3_sb->ide_regs[0x01] = 0x11; /*VIA*/
    via_mvp3_sb->ide_regs[0x02] = 0x71; via_mvp3_sb->ide_regs[0x03] = 0x05; /*VT82C586B*/
    via_mvp3_sb->ide_regs[0x04] = 0x80;
    via_mvp3_sb->ide_regs[0x06] = 0x80; via_mvp3_sb->ide_regs[0x07] = 0x02;
    via_mvp3_sb->ide_regs[0x09] = 0x85;
    via_mvp3_sb->ide_regs[0x0a] = 0x01;
    via_mvp3_sb->ide_regs[0x0b] = 0x01;

    via_mvp3_sb->ide_regs[0x10] = 0xf0; via_mvp3_sb->ide_regs[0x11] = 0x01;
    via_mvp3_sb->ide_regs[0x14] = 0xf4; via_mvp3_sb->ide_regs[0x15] = 0x03;
    via_mvp3_sb->ide_regs[0x18] = 0x70; via_mvp3_sb->ide_regs[0x19] = 0x01;
    via_mvp3_sb->ide_regs[0x1c] = 0x74; via_mvp3_sb->ide_regs[0x1d] = 0x03;
    via_mvp3_sb->ide_regs[0x20] = 0x01; via_mvp3_sb->ide_regs[0x21] = 0xcc;

    via_mvp3_sb->ide_regs[0x3c] = 0x0e;

    via_mvp3_sb->ide_regs[0x40] = 0x08;
    via_mvp3_sb->ide_regs[0x41] = 0x02;
    via_mvp3_sb->ide_regs[0x42] = 0x09;
    via_mvp3_sb->ide_regs[0x43] = 0x3a;
    via_mvp3_sb->ide_regs[0x44] = 0x68;
    via_mvp3_sb->ide_regs[0x46] = 0xc0;
    via_mvp3_sb->ide_regs[0x48] = 0xa8; via_mvp3_sb->ide_regs[0x49] = 0xa8;
    via_mvp3_sb->ide_regs[0x4a] = 0xa8; via_mvp3_sb->ide_regs[0x4b] = 0xa8;
    via_mvp3_sb->ide_regs[0x4c] = 0xff;
    via_mvp3_sb->ide_regs[0x4e] = 0xff;
    via_mvp3_sb->ide_regs[0x4f] = 0xff;
    via_mvp3_sb->ide_regs[0x50] = 0x03; via_mvp3_sb->ide_regs[0x51] = 0x03;
    via_mvp3_sb->ide_regs[0x52] = 0x03; via_mvp3_sb->ide_regs[0x53] = 0x03;

    via_mvp3_sb->ide_regs[0x61] = 0x02;
    via_mvp3_sb->ide_regs[0x69] = 0x02;

    via_mvp3_sb->usb_regs[0x00] = 0x06; via_mvp3_sb->usb_regs[0x01] = 0x11; /*VIA*/
    via_mvp3_sb->usb_regs[0x02] = 0x38; via_mvp3_sb->usb_regs[0x03] = 0x30;
    via_mvp3_sb->usb_regs[0x04] = 0x00; via_mvp3_sb->usb_regs[0x05] = 0x00;
    via_mvp3_sb->usb_regs[0x06] = 0x00; via_mvp3_sb->usb_regs[0x07] = 0x02;
    via_mvp3_sb->usb_regs[0x0a] = 0x03;
    via_mvp3_sb->usb_regs[0x0b] = 0x0c;
    via_mvp3_sb->usb_regs[0x0d] = 0x16;
    via_mvp3_sb->usb_regs[0x20] = 0x01;
    via_mvp3_sb->usb_regs[0x21] = 0x03;
    via_mvp3_sb->usb_regs[0x3d] = 0x04;

    via_mvp3_sb->usb_regs[0x60] = 0x10;
    via_mvp3_sb->usb_regs[0xc1] = 0x20;

    via_mvp3_sb->power_regs[0x00] = 0x06; via_mvp3_sb->power_regs[0x01] = 0x11; /*VIA*/
    via_mvp3_sb->power_regs[0x02] = 0x40; via_mvp3_sb->power_regs[0x03] = 0x30;
    via_mvp3_sb->power_regs[0x04] = 0x00; via_mvp3_sb->power_regs[0x05] = 0x00;
    via_mvp3_sb->power_regs[0x06] = 0x80; via_mvp3_sb->power_regs[0x07] = 0x02;
    via_mvp3_sb->power_regs[0x08] = 0x10; /*Production version (3041)*/
    via_mvp3_sb->power_regs[0x48] = 0x01;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);

    ide_pri_disable();
    ide_sec_disable();
}

static void
via_mvp3_sb_bus_master_handlers(via_mvp3_sb_t *dev, uint16_t old_base)
{
    uint16_t base;

    base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);

    sff_bus_master_handlers(dev->bm[0], old_base, base, (dev->ide_regs[0x04] & 1));
    sff_bus_master_handlers(dev->bm[1], old_base + 8, base + 8, (dev->ide_regs[0x04] & 1));
}

static uint8_t
via_mvp3_sb_read(int func, int addr, void *priv)
{
    via_mvp3_sb_t *dev = (via_mvp3_sb_t *) priv;

    switch(func)
    {
        case 0:
        {
            return dev->pci_isa_regs[addr];
        }
        case 1:
        {
            return dev->ide_regs[addr];
        }
        case 2:
        {
            return dev->usb_regs[addr];
        }
        case 3:
        {
            return dev->power_regs[addr];
        }
        default:
        {
            return 0xff;
        }
    }
}

static void
via_mvp3_sb_write(int func, int addr, uint8_t val, void *priv)
{
    via_mvp3_sb_t *dev = (via_mvp3_sb_t *) priv;
    uint16_t old_base;

    if(func > 3) return;

    old_base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);

    switch(func)
    {
        case 0: //PCI-ISA bridge
        {
            /*Read-only addresses*/
            if ((addr < 4) || (addr == 5) || (addr == 6)
                || (addr >= 8 && addr < 0x40)
                || (addr == 0x49)
                || (addr == 0x4b)
                || (addr >= 0x51 && addr < 0x54)
                || (addr >= 0x5d && addr < 0x60)
                || (addr >= 0x68 && addr < 0x6a)
                || (addr >= 0x71))
                return;

            switch(addr)
            {
                case 0x04:
                dev->pci_isa_regs[0x04] = (val & 8) | 7;
                break;
                case 0x06:
                dev->pci_isa_regs[0x06] &= ~(val & 0xb0);
                break;

                case 0x47:
                if((val & 0x81) == 0x81) resetx86();
                if(val & 0x20) pci_elcr_set_enabled(1);
                else pci_elcr_set_enabled(0);
                dev->pci_isa_regs[0x47] = val & 0xfe;
                break;

                case 0x54:
                if(val & 8) pci_set_irq_level(PCI_INTA, 0);
                else pci_set_irq_level(PCI_INTA, 1);
                if(val & 4) pci_set_irq_level(PCI_INTB, 0);
                else pci_set_irq_level(PCI_INTB, 1);
                if(val & 2) pci_set_irq_level(PCI_INTC, 0);
                else pci_set_irq_level(PCI_INTC, 1);
                if(val & 1) pci_set_irq_level(PCI_INTD, 0);
                else pci_set_irq_level(PCI_INTD, 1);
                break;

                case 0x55:
                if(!(val & 0xf0)) pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
                else pci_set_irq_routing(PCI_INTD, val >> 4);

                if(!(val & 0x0f)) pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
                else pci_set_mirq_routing(PCI_MIRQ0, val & 0xf);
                dev->pci_isa_regs[0x55] = val;
                break;

                case 0x56:
                if(!(val & 0xf0)) pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
                else pci_set_irq_routing(PCI_INTA, val >> 4);
                
                if(!(val & 0x0f)) pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
                else pci_set_irq_routing(PCI_INTB, val & 0xf);
                dev->pci_isa_regs[0x56] = val;
                break;

                case 0x57:
                if(!(val & 0xf0)) pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
                else pci_set_irq_routing(PCI_INTC, val >> 4);

                if(!(val & 0x0f)) pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
                else pci_set_mirq_routing(PCI_MIRQ1, val & 0xf);
                dev->pci_isa_regs[0x57] = val;
                break;

                case 0x58:
                if(!(val & 0x0f)) pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
                else pci_set_mirq_routing(PCI_MIRQ2, val & 0xf);
                dev->pci_isa_regs[0x58] = val;
            }
            break;
        }
        case 1: /*IDE regs*/
        {
            /*Read-only addresses*/
            if ((addr < 4) || (addr == 5) || (addr == 8)
                || (addr >= 0xa && addr < 0x0d)
                || (addr >= 0x0e && addr < 0x20)
                || (addr >= 0x22 && addr < 0x3c)
                || (addr >= 0x3d && addr < 0x40)
                || (addr >= 0x54 && addr < 0x60)
                || (addr >= 0x52 && addr < 0x68)
                || (addr >= 0x62))
                return;

            switch(addr)
            {
                case 0x04:
                {
                    uint16_t base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);
                    ide_pri_disable();
                    ide_sec_disable();
                    if(val & PCI_COMMAND_IO)
                    {
                        if(dev->ide_regs[0x40] & 0x02) ide_pri_enable();
                        if(dev->ide_regs[0x40] & 0x01) ide_sec_enable();
                    }
                    via_mvp3_sb_bus_master_handlers(dev, base);
                    dev->ide_regs[0x04] = val & 0x85;
                    break;
                }
                case 0x06:
                dev->ide_regs[0x06] &= ~(val & 0xb0);
                break;

                case 0x09:
                dev->ide_regs[0x09] = (dev->pci_isa_regs[0x09] & ~0x70) | 0x8a;
                break;

                case 0x20:
                {
                    dev->ide_regs[0x20] = (val & 0xf0) | 1;
                    via_mvp3_sb_bus_master_handlers(dev, old_base);
                    break;
                }

                case 0x21:
                {
                    dev->ide_regs[0x21] = val;
                    via_mvp3_sb_bus_master_handlers(dev, old_base);
                    break;
                }

                case 0x40:
                {
                    dev->ide_regs[0x40] = val;
                    ide_pri_disable();
                    ide_sec_disable();
                    if(val & PCI_COMMAND_IO)
                    {
                        if(dev->ide_regs[0x40] & 0x02) ide_pri_enable();
                        if(dev->ide_regs[0x40] & 0x01) ide_sec_enable();
                    }
                    break;
                }

                default:
                dev->ide_regs[addr] = val;
                break;
            }
            break;
        }
        case 2:
        {
            /*Read-only addresses*/
            if ((addr < 4) || (addr == 5) || (addr == 6)
                || (addr >= 8 && addr < 0xd)
                || (addr >= 0xe && addr < 0x20)
                || (addr >= 0x22 && addr < 0x3c)
                || (addr >= 0x3e && addr < 0x40)
                || (addr >= 0x42 && addr < 0x44)
                || (addr >= 0x46 && addr < 0xc0)
                || (addr >= 0xc2))
                return;
            
            switch(addr)
            {
                case 0x04:
                dev->usb_regs[0x04] = val & 0x97;
                break;
                case 0x07:
                dev->usb_regs[0x07] = val & 0x7f;
                break;

                case 0x20:
                dev->usb_regs[0x20] = (val & ~0x1f) | 1;
                break;

                default:
                dev->usb_regs[addr] = val;
                break;
            }
            break;
        }
        case 3:
        {
            /*Read-only addresses*/
            if ((addr < 0xd) || (addr >= 0xe && addr < 0x40)
                || (addr == 0x43)
                || (addr == 0x48)
                || (addr >= 0x4a && addr < 0x50)
                || (addr >= 0x54))
                return;

            dev->power_regs[addr] = val;
            break;
        }
    }
}

static void
*via_mvp3_sb_init(const device_t *info)
{
    via_mvp3_sb_t *via_mvp3_sb = (via_mvp3_sb_t *) malloc(sizeof(via_mvp3_sb_t));
    memset(via_mvp3_sb, 0, sizeof(via_mvp3_sb_t));

    pci_add_card(7, via_mvp3_sb_read, via_mvp3_sb_write, via_mvp3_sb);

    via_mvp3_sb->bm[0] = device_add_inst(&sff8038i_device, 1);
    via_mvp3_sb->bm[1] = device_add_inst(&sff8038i_device, 2);

    via_mvp3_sb_reset_hard(via_mvp3_sb);

    device_add(&port_92_pci_device);

    dma_alias_set();

    pci_enable_mirq(0);
    pci_enable_mirq(1);
    pci_enable_mirq(2);

    return via_mvp3_sb;
}

static void
via_mvp3_sb_close(void *p)
{
    via_mvp3_sb_t *via_mvp3_sb = (via_mvp3_sb_t *)p;

    free(via_mvp3_sb);
}

const device_t via_mvp3_sb_device =
{
    "VIA VT82C586B",
    DEVICE_PCI,
    0,
    via_mvp3_sb_init, 
    via_mvp3_sb_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};