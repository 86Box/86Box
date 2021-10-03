/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 82801BA(ICH2) I/O Controller.
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

#include <86box/nvr.h>

#include <86box/apm.h>
#include <86box/acpi.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/smbus.h>
#include <86box/spd.h>
#include <86box/usb.h>

#include <86box/chipset.h>

#define ENABLE_INTEL_ICH2_LOG 1
#ifdef ENABLE_INTEL_ICH2_LOG
int intel_ich2_do_log = ENABLE_INTEL_ICH2_LOG;


static void
intel_ich2_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_ich2_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define intel_ich2_log(fmt, ...)
#endif


typedef struct intel_ich2_t
{

	uint8_t hub_conf[256], lpc_conf[7][256];

    int lpc_slot;
    acpi_t *acpi;
    nvr_t *nvr;
    sff8038i_t *ide_drive[2];
    smbus_piix4_t *smbus;
    usb_t *usb[2];

} intel_ich2_t;

static void
intel_ich2_hub_write(int func, int addr, uint8_t val, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    intel_ich2_log("Intel ICH2-HUB: dev->regs[%02x] = %02x POST: %02x \n", addr, val, inb(0x80));

    if(func == 0)
    switch(addr)
    {
        default:
            dev->hub_conf[addr] = val;
        break;
    }
}


static uint8_t
intel_ich2_hub_read(int func, int addr, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    if (func == 0)
	    return dev->hub_conf[addr];
    else
        return 0xff;
}

static void
intel_ich2_acpi(intel_ich2_t *dev)
{
    acpi_update_io_mapping(dev->acpi, (dev->lpc_conf[0][0x41] << 8) | (dev->lpc_conf[0][0x40] & 0x80), !!(dev->lpc_conf[0][0x44] & 0x10));
}

static void
intel_ich2_pirq_routing(int enabled, int cur_reg, int irq, intel_ich2_t *dev)
{
    if(enabled)
        pci_set_irq_routing(cur_reg, irq);
    else
        pci_set_irq_routing(cur_reg, PCI_IRQ_DISABLED);
}

static void
intel_ich2_ide(intel_ich2_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();

    if(dev->lpc_conf[1][4] & 1)
    {
        ide_pri_enable();
        ide_sec_enable();
    }
}

static void
intel_ich2_bus_master(intel_ich2_t *dev)
{
    sff_bus_master_handler(dev->ide_drive[0], !!(dev->lpc_conf[1][4] & 4), ((dev->lpc_conf[1][0x20] & 0xf0) | (dev->lpc_conf[1][0x21] << 8)));
    sff_bus_master_handler(dev->ide_drive[1], !!(dev->lpc_conf[1][4] & 4), ((dev->lpc_conf[1][0x20] & 0xf0) | (dev->lpc_conf[1][0x21] << 8)) + 8);
}

static void
intel_ich2_usb(int hub, intel_ich2_t *dev)
{
if(hub)
    uhci_update_io_mapping(dev->usb[1], dev->lpc_conf[4][0x20] & ~0x1f, dev->lpc_conf[4][0x21], dev->lpc_conf[4][4] & 1);
else
    uhci_update_io_mapping(dev->usb[0], dev->lpc_conf[2][0x20] & ~0x1f, dev->lpc_conf[2][0x21], dev->lpc_conf[2][4] & 1);
}

static void
intel_ich2_smbus(intel_ich2_t *dev)
{
    smbus_piix4_remap(dev->smbus, ((uint16_t) (dev->lpc_conf[3][0x21] << 8)) | (dev->lpc_conf[3][0x20] & 0xf0), (!!(dev->lpc_conf[3][0x40] & 1) && !!(dev->lpc_conf[3][4] & 1)));
}

static void
intel_ich2_lpc_write(int func, int addr, uint8_t val, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;


    if(func == 0)
    {
        intel_ich2_log("Intel ICH2-LPC: dev->regs[%02x][%02x] = %02x POST: %02x \n", func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x40 ... 0x44: /* ACPI */
                dev->lpc_conf[func][addr] = val;
                intel_ich2_acpi(dev);
            break;

            case 0x4e: /* BIOS SMI */
                dev->lpc_conf[func][addr] = val;

                if((val & 3) == 3)
                    smi_line = 1;
            break;

            case 0x60 ... 0x63: /* PCI IRQ Routing */
            dev->lpc_conf[func][addr] = val;
            intel_ich2_pirq_routing(val & 0x80, addr - 0x60, val & 0x0f, dev);
            break;

            case 0x68 ... 0x6b: /* PCI IRQ Routing */
            dev->lpc_conf[func][addr] = val;
            intel_ich2_pirq_routing(val & 0x80, addr - 0x64, val & 0x0f, dev);
            break;

            case 0xd8: /* RTC Control */
                dev->lpc_conf[func][addr] = val;
            break;

            default:
                dev->lpc_conf[func][addr] = val;
            break;
        }
    }
    else if(func == 1)
    {
        intel_ich2_log("Intel ICH2-IDE: dev->regs[%02x][%02x] = %02x POST: %02x \n", func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x04: /* IDE Controller */
                dev->lpc_conf[func][addr] = val;
                intel_ich2_ide(dev);
                intel_ich2_bus_master(dev);
            break;

            case 0x20 ... 0x23: /* IDE Bus Mastering */
                dev->lpc_conf[func][addr] = val;
                intel_ich2_bus_master(dev);    
            break;

            default:
                dev->lpc_conf[func][addr] = val;
            break;
        }
    }
    else if((func == 2) || (func == 4))
    {
        int usb_variant = !!(func == 4); /* Function 4 is Hub 1 while 2 is Hub 0 */
        intel_ich2_log("Intel ICH2-USB: HUB: %d dev->regs[%02x][%02x] = %02x POST: %02x\n", usb_variant, func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x04:
            case 0x20 ... 0x21:
                dev->lpc_conf[func][addr] = val;
                intel_ich2_usb(usb_variant, dev);
            break;

            default:
                dev->lpc_conf[func][addr] = val;
            break;
        }
    }
    else if(func == 3)
    {
        intel_ich2_log("Intel ICH2-SMBus: dev->regs[%02x][%02x] = %02x POST: %02x \n", func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x04:
            case 0x20 ... 0x21:
            case 0x40:
                dev->lpc_conf[func][addr] = val;
                intel_ich2_smbus(dev);
            break;

            default:
                dev->lpc_conf[func][addr] = val;
            break;
        }
    }
    else if((func == 5) || (func == 6))
    {
        intel_ich2_log("Intel ICH2-%s: dev->regs[%02x][%02x] = %02x POST: %02x \n", (func == 5) ? "AC97" : "MODEM", func, addr, val, inb(0x80));
        switch(addr)
        {
            default:
            dev->lpc_conf[func][addr] = val;
            break;
        }
    }
}

static uint8_t
intel_ich2_lpc_read(int func, int addr, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    intel_ich2_log("Intel ICH2: dev->regs[%02x][%02x]  (%02x) POST: %02x \n", func, addr, dev->lpc_conf[func][addr], inb(0x80));

    if (func == 0)
	    return dev->lpc_conf[func][addr];
    else if(func == 1)
        return dev->lpc_conf[func][addr];
    else if((func == 2) || (func == 4))
        return dev->lpc_conf[func][addr];
    else if((func == 5) || (func == 6))
        return dev->lpc_conf[func][addr];
    else
        return 0xff;
}

static void
intel_ich2_reset(void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;
    memset(dev->hub_conf, 0, sizeof(dev->hub_conf));
    memset(dev->lpc_conf, 0, sizeof(dev->lpc_conf));

    /* HUB Interface */
    dev->hub_conf[0x00] = 0x86;
    dev->hub_conf[0x01] = 0x80;
    dev->hub_conf[0x02] = 0x4e;
    dev->hub_conf[0x03] = 0x24;
    dev->hub_conf[0x04] = 1;
    dev->hub_conf[0x06] = 0x80;
    dev->hub_conf[0x08] = 2;
    dev->hub_conf[0x0a] = 4;
    dev->hub_conf[0x0b] = 6;
    dev->hub_conf[0x0e] = 1;
    dev->hub_conf[0x1e] = 0x80;
    dev->hub_conf[0x1f] = 2;
    dev->hub_conf[0x20] = 0xf0;
    dev->hub_conf[0x21] = 0xff;
    dev->hub_conf[0x70] = 0x20;

    /* LPC Interface */
    dev->lpc_conf[0][0x00] = 0x86;
    dev->lpc_conf[0][0x01] = 0x80;
    dev->lpc_conf[0][0x02] = 0x40;
    dev->lpc_conf[0][0x03] = 0x24;
    dev->lpc_conf[0][0x04] = 0x0f;
    dev->lpc_conf[0][0x06] = 0x80;
    dev->lpc_conf[0][0x07] = 2;
    dev->lpc_conf[0][0x08] = 2;
    dev->lpc_conf[0][0x0a] = 1;
    dev->lpc_conf[0][0x0b] = 6;
    dev->lpc_conf[0][0x0e] = 0x80;
    dev->lpc_conf[0][0x40] = 1;

    intel_ich2_acpi(dev);

    /* ICH2 IDE Controller */
    dev->lpc_conf[1][0x00] = 0x86;
    dev->lpc_conf[1][0x01] = 0x80;
    dev->lpc_conf[1][0x02] = 0x4b;
    dev->lpc_conf[1][0x03] = 0x24;
    dev->lpc_conf[1][0x04] = 0x0f;
    dev->lpc_conf[1][0x06] = 0x80;
    dev->lpc_conf[1][0x07] = 2;
    dev->lpc_conf[1][0x08] = 2;
    dev->lpc_conf[1][0x09] = 0x80;
    dev->lpc_conf[1][0x0a] = 1;
    dev->lpc_conf[1][0x0b] = 1;
    dev->lpc_conf[1][0x0e] = 0x80;
    dev->lpc_conf[1][0x20] = 1;

    intel_ich2_ide(dev);
    sff_bus_master_reset(dev->ide_drive[0], ((dev->lpc_conf[1][0x20] & 0xf0) | (dev->lpc_conf[1][0x21] << 8)));
    sff_bus_master_reset(dev->ide_drive[0], ((dev->lpc_conf[1][0x20] & 0xf0) | (dev->lpc_conf[1][0x21] << 8)) + 8);

    /* USB Hub 1 & 2 */
    dev->lpc_conf[2][0x00] = 0x86;
    dev->lpc_conf[2][0x01] = 0x80;
    dev->lpc_conf[2][0x02] = 0x42;
    dev->lpc_conf[2][0x03] = 0x24;
    dev->lpc_conf[2][0x06] = 0x80;
    dev->lpc_conf[2][0x07] = 2;
    dev->lpc_conf[2][0x08] = 2;
    dev->lpc_conf[2][0x0a] = 3;
    dev->lpc_conf[2][0x0b] = 0x0c;
    dev->lpc_conf[2][0x20] = 1;
    dev->lpc_conf[2][0x3d] = 3;
    dev->lpc_conf[2][0x60] = 0x10;
    dev->lpc_conf[2][0xc1] = 0x20;

    intel_ich2_usb(0, dev);

    dev->lpc_conf[4][0x00] = 0x86;
    dev->lpc_conf[4][0x01] = 0x80;
    dev->lpc_conf[4][0x02] = 0x44;
    dev->lpc_conf[4][0x03] = 0x24;
    dev->lpc_conf[4][0x06] = 0x80;
    dev->lpc_conf[4][0x07] = 2;
    dev->lpc_conf[4][0x08] = 2;
    dev->lpc_conf[4][0x0a] = 3;
    dev->lpc_conf[4][0x0b] = 0x0c;
    dev->lpc_conf[4][0x20] = 1;
    dev->lpc_conf[4][0x3d] = 3;
    dev->lpc_conf[4][0x60] = 0x10;
    dev->lpc_conf[4][0xc1] = 0x20;

    intel_ich2_usb(1, dev);

    /* SMBus */
    dev->lpc_conf[3][0x00] = 0x86;
    dev->lpc_conf[3][0x01] = 0x80;
    dev->lpc_conf[3][0x02] = 0x43;
    dev->lpc_conf[3][0x03] = 0x24;
    dev->lpc_conf[3][0x06] = 0x80;
    dev->lpc_conf[3][0x07] = 2;
    dev->lpc_conf[3][0x08] = 2;
    dev->lpc_conf[3][0x09] = 0x80;
    dev->lpc_conf[3][0x0a] = 3;
    dev->lpc_conf[3][0x0b] = 0x0c;
    dev->lpc_conf[3][0x20] = 1;
    dev->lpc_conf[3][0x3d] = 2;

    intel_ich2_smbus(dev);

    /* ICH2 AC97 */
    dev->lpc_conf[5][0x00] = 0x86;
    dev->lpc_conf[5][0x01] = 0x80;
    dev->lpc_conf[5][0x02] = 0x45;
    dev->lpc_conf[5][0x03] = 0x24;
    dev->lpc_conf[5][0x06] = 0x80;
    dev->lpc_conf[5][0x07] = 2;
    dev->lpc_conf[5][0x08] = 2;
    dev->lpc_conf[5][0x0a] = 1;
    dev->lpc_conf[5][0x0b] = 4;
    dev->lpc_conf[5][0x10] = 1;
    dev->lpc_conf[5][0x14] = 1;
    dev->lpc_conf[5][0x3d] = 2;

    /* ICH2 AC97 Modem */
    dev->lpc_conf[6][0x00] = 0x86;
    dev->lpc_conf[6][0x01] = 0x80;
    dev->lpc_conf[6][0x02] = 0x46;
    dev->lpc_conf[6][0x03] = 0x24;
    dev->lpc_conf[6][0x06] = 0x80;
    dev->lpc_conf[6][0x07] = 2;
    dev->lpc_conf[6][0x08] = 2;
    dev->lpc_conf[6][0x0a] = 3;
    dev->lpc_conf[6][0x0b] = 7;
    dev->lpc_conf[6][0x10] = 1;
    dev->lpc_conf[6][0x14] = 1;
    dev->lpc_conf[6][0x18] = 1;
    dev->lpc_conf[6][0x3d] = 2;
}


static void
intel_ich2_close(void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    free(dev);
}


static void *
intel_ich2_init(const device_t *info)
{
    intel_ich2_t *dev = (intel_ich2_t *)malloc(sizeof(intel_ich2_t));
    memset(dev, 0, sizeof(intel_ich2_t));

    pci_add_card(PCI_ADD_SOUTHBRIDGE, intel_ich2_hub_read, intel_ich2_hub_write, dev);
    dev->lpc_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, intel_ich2_lpc_read, intel_ich2_lpc_write, dev);

    /* ACPI */
    dev->acpi = device_add(&acpi_intel_device);
    acpi_set_slot(dev->acpi, dev->lpc_slot);

    /* NVR */
    dev->nvr = device_add(&piix4_nvr_device);
    acpi_set_nvr(dev->acpi, dev->nvr);

    /* IDE */
    dev->ide_drive[0] = device_add_inst(&sff8038i_device, 1);
    dev->ide_drive[1] = device_add_inst(&sff8038i_device, 2);
    sff_set_irq_line(dev->ide_drive[0], 14);
    sff_set_irq_line(dev->ide_drive[1], 15);

    /* SMBus */
    dev->smbus = device_add(&piix4_smbus_device);

    /* PIC */
    pic_set_pci();

    /* USB */
    dev->usb[0] = device_add_inst(&usb_device, 1);
    dev->usb[1] = device_add_inst(&usb_device, 2);

    intel_ich2_reset(dev);

    return dev;
}


const device_t intel_ich2_device = {
    "Intel 82801BA(ICH2)",
    DEVICE_PCI,
    0,
    intel_ich2_init, intel_ich2_close, intel_ich2_reset,
    { NULL }, NULL, NULL,
    NULL
};
