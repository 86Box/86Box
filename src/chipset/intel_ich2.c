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
#include <86box/dma.h>
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

	uint8_t lan_conf[256], hub_conf[256], lpc_conf[7][256], gpio_space[64];
    int gpio_base;

    int lpc_slot;
    acpi_t *acpi;
    nvr_t *nvr;
    sff8038i_t *ide_drive[2];
    smbus_piix4_t *smbus;
    usb_t *usb[2];

} intel_ich2_t;

static void
intel_ich2_lan_write(int func, int addr, uint8_t val, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    intel_ich2_log("Intel ICH2-LAN: dev->regs[%02x] = %02x POST: %02x \n", addr, val, inb(0x80));

    if(func == 0)
        switch(addr)
        {
            case 0x04:
                dev->lan_conf[addr] = val & 0x57;
            break;

            case 0x05:
                dev->lan_conf[addr] = val & 1;
            break;

            case 0x06:
                dev->lan_conf[addr] &= val & 0x80;
            break;

            case 0x07:
                dev->lan_conf[addr] &= val & 0xf1;
            break;

            case 0x0c:
                dev->lan_conf[addr] = val & 0x18;
            break;

            case 0x0d:
                dev->lan_conf[addr] = val & 0xf8;
            break;

            case 0x11 ... 0x13: /* LAN Memory Base Address */
                dev->lan_conf[addr] = (addr != 0x11) ? val : (val & 0xf0);
            break;

            case 0x14 ... 0x15: /* LAN I/O Space */
                dev->lan_conf[addr] = (addr & 1) ? val : ((val & 0xf0) | 1);
            break;

            case 0x3c:
                dev->lan_conf[addr] = val;
            break;

            case 0xe0:
                dev->lan_conf[addr] = val & 0x83;
            break;

            case 0xe1:
                dev->lan_conf[addr] = val & 0x1f;
                dev->lan_conf[addr] &= val & 0x80;
            break;

            case 0xe3:
                dev->lan_conf[addr] = val;
            break;
        }
}

static uint8_t
intel_ich2_lan_read(int func, int addr, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    if (func == 0) {
        intel_ich2_log("Intel ICH2-LAN: dev->regs[%02x] (%02x) POST: %02x \n", addr, dev->lan_conf[addr], inb(0x80));
	    return dev->lan_conf[addr];
    }
    else return 0xff;
}

static void
intel_ich2_hub_write(int func, int addr, uint8_t val, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    intel_ich2_log("Intel ICH2-HUB: dev->regs[%02x] = %02x POST: %02x \n", addr, val, inb(0x80));

    if(func == 0)
        switch(addr)
        {
            case 0x04:
                dev->hub_conf[addr] = val & 0x4f;
                break;

            case 0x05:
                dev->hub_conf[addr] = val & 1;
                break;

            case 0x07:
                dev->hub_conf[addr] &= val;
                break;

            case 0x0d:
                dev->hub_conf[addr] = val & 0xf8;
                break;

            case 0x0e:
                dev->hub_conf[addr] = val;
                break;

            case 0x19 ... 0x1a:
                dev->hub_conf[addr] = val;
                break;

            case 0x1b:
                dev->hub_conf[addr] = val & 0xf8;
                break;

            case 0x1c ... 0x1d:
                dev->hub_conf[addr] = val & 0xf0;
                break;

            case 0x1e:
                dev->hub_conf[addr] &= val & 0xe0;
                break;

            case 0x1f:
                dev->hub_conf[addr] &= val;
                break;

            case 0x20:
                dev->hub_conf[addr] = val & 0xf0;
                break;

            case 0x21:
                dev->hub_conf[addr] = val;
                break;

            case 0x22:
                dev->hub_conf[addr] = val & 0xf0;
                break;

            case 0x23:
                dev->hub_conf[addr] = val;
                break;

            case 0x24:
                dev->hub_conf[addr] = val & 0xf0;
                break;

            case 0x25:
                dev->hub_conf[addr] = val;
                break;

            case 0x26:
                dev->hub_conf[addr] = val & 0xf0;
                break;

            case 0x27:
                dev->hub_conf[addr] = val;
                break;

            case 0x3e:
                dev->hub_conf[addr] = val & 0xef;
                break;

            case 0x40:
                dev->hub_conf[addr] = val & 1;
                break;

            case 0x50:
                dev->hub_conf[addr] = val & 6;
                break;

            case 0x51:
                dev->hub_conf[addr] = val & 3;
                break;

            case 0x70:
                dev->hub_conf[addr] = val & 0xf8;
                break;

            case 0x82:
                dev->hub_conf[addr] &= val;
                break;

            case 0x90:
                dev->hub_conf[addr] = val & 6;
                break;

            case 0x92:
                dev->hub_conf[addr] &= val & 6;
                break;
        }
}


static uint8_t
intel_ich2_hub_read(int func, int addr, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    if (func == 0) {
        intel_ich2_log("Intel ICH2-HUB: dev->regs[%02x] (%02x) POST: %02x \n", addr, dev->hub_conf[addr], inb(0x80));
	    return dev->hub_conf[addr];
    }
    else return 0xff;
}

static void
intel_ich2_acpi(intel_ich2_t *dev)
{
    acpi_update_io_mapping(dev->acpi, (dev->lpc_conf[0][0x41] << 8) | (dev->lpc_conf[0][0x40] & 0x80), !!(dev->lpc_conf[0][0x44] & 0x10));
}

static void
intel_ich2_gpio_write(uint16_t addr, uint8_t val, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    intel_ich2_log("Intel ICH2-GPIO: dev->regs[%02x] = %02x POST: %02x \n", addr - dev->gpio_base, val, inb(0x80));
    dev->gpio_space[addr - dev->gpio_base] = val;
}

static uint8_t
intel_ich2_gpio_read(uint16_t addr, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    intel_ich2_log("Intel ICH2-GPIO: dev->regs[%02x] (%02x) POST: %02x \n", addr - dev->gpio_base, dev->gpio_space[addr - dev->gpio_base], inb(0x80));
    return dev->gpio_space[addr - dev->gpio_base];
}

static void
intel_ich2_gpio(intel_ich2_t *dev)
{
    if(dev->gpio_base) /* Remove the old GPIO base*/
        io_removehandler(dev->gpio_base, 64, intel_ich2_gpio_read, NULL, NULL, intel_ich2_gpio_write, NULL, NULL, dev);

    dev->gpio_base = (dev->lpc_conf[0][0x59] << 8) | (dev->lpc_conf[0][0x58] & 0xc0);

    if(dev->gpio_base && !!(dev->lpc_conf[0][0x54] & 0x10)) /* Set the new GPIO base*/
        io_sethandler(dev->gpio_base, 64, intel_ich2_gpio_read, NULL, NULL, intel_ich2_gpio_write, NULL, NULL, dev);

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
intel_ich2_nvr(intel_ich2_t *dev)
{
    nvr_at_handler(0, 0x0070, dev->nvr);
    nvr_at_handler(0, 0x0072, dev->nvr);
    nvr_at_handler(0, 0x0074, dev->nvr);
    nvr_at_handler(0, 0x0076, dev->nvr);

    if(!(dev->lpc_conf[0][0xd8] & 8)) {
        nvr_at_handler(1, 0x0070, dev->nvr);
        nvr_at_handler(1, 0x0074, dev->nvr);
    }

    if(!!(dev->lpc_conf[0][0xd8] & 4) && !(dev->lpc_conf[0][0xd8] & 0x10)) {
        nvr_at_handler(1, 0x0072, dev->nvr);
        nvr_at_handler(1, 0x0076, dev->nvr);
    }
}

static void
intel_ich2_ide(intel_ich2_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();

    if(dev->lpc_conf[1][4] & 1)
    {
        if(dev->lpc_conf[1][0x41] & 0x80)
            ide_pri_enable();

        if(dev->lpc_conf[1][0x43] & 0x80)
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
    smbus_piix4_remap(dev->smbus, (dev->lpc_conf[3][0x21] << 8) | (dev->lpc_conf[3][0x20] & 0xf0), (!!(dev->lpc_conf[3][0x40] & 1) && !!(dev->lpc_conf[3][4] & 1)));
}

static void
intel_ich2_disable(intel_ich2_t *dev)
{

    /* F2h Bit 1: Disable IDE */
    if(dev->lpc_conf[0][0xf2] & 2) {
        ide_pri_disable();
        ide_sec_disable();
    }

    /* F2h Bit 2: Disable USB 0 */
    if(dev->lpc_conf[0][0xf2] & 4) {
        uhci_update_io_mapping(dev->usb[0], dev->lpc_conf[2][0x20] & ~0x1f, dev->lpc_conf[2][0x21], 0);
    }

    /* F2h Bit 3: Disable SMBus (Note that F3h Bit 0 allows the ICH2 to access the SMBus address space even if PCI functionality is disabled) */
    if(dev->lpc_conf[0][0xf2] & 8) {
        if(!(dev->lpc_conf[0][0xf3] & 1))
            smbus_piix4_remap(dev->smbus, (dev->lpc_conf[3][0x21] << 8) | (dev->lpc_conf[3][0x20] & 0xf0), 0);
    }

    /* F2h Bit 4: Disable USB 1 */
    if(dev->lpc_conf[0][0xf2] & 0x10) {
        uhci_update_io_mapping(dev->usb[1], dev->lpc_conf[2][0x20] & ~0x1f, dev->lpc_conf[2][0x21], 0);
    }

    /* F2h Bit 5: Disable AC97 */
    /* F2h Bit 6: Disable AC97 Modem */
}

static void
intel_ich2_lpc_write(int func, int addr, uint8_t val, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;


    if(func == 0)   /* LPC */
    {
        intel_ich2_log("Intel ICH2-LPC: dev->regs[%02x][%02x] = %02x POST: %02x \n", func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x04:
                dev->lpc_conf[func][addr] = val & 0x40;
            break;

            case 0x07:
                dev->lpc_conf[func][addr] = val & 0xf8;
                dev->lpc_conf[func][addr] &= val & 1;
            break;


            case 0x40 ... 0x41: /* ACPI */
            case 0x44:

                if(addr == 0x44) { /* ACPI IRQ */
                    dev->lpc_conf[func][addr] = val & 0x17;
                    acpi_set_irq_line(dev->acpi, ((dev->lpc_conf[0][0x44] & 7) < 3) ? (9 + (dev->lpc_conf[0][0x44] & 7)) : 9);
                }
                else { /* ACPI I/O Base & Enable */
                    dev->lpc_conf[func][addr] = (addr & 1) ? val : (val & 0x80);

                    dev->lpc_conf[func][addr] = val;
                }

                intel_ich2_acpi(dev);
            break;

            case 0x4e: /* BIOS Control SMI */
                if(dev->lpc_conf[func][addr] == 2)
                    dev->lpc_conf[func][addr] = (val & 3) | 2;
                else
                    dev->lpc_conf[func][addr] = val & 3;

                if((val & 3) == 3)
                    smi_line = 1;
            break;

            case 0x54:
                dev->lpc_conf[func][addr] = val & 7;
            break;

            case 0x58 ... 0x59: /* General Purpose 64 byte I/O space */
            case 0x5c:

                if(addr == 0x5c) /* GPIO Enable */
                    dev->lpc_conf[func][addr] = val & 0x10;
                else /* GPIO base address */
                    dev->lpc_conf[func][addr] = (addr & 1) ? val : ((val & 0xc0) | 1);
            
                intel_ich2_gpio(dev);
            break;

            case 0x60 ... 0x63: /* PCI IRQ Routing INT A-D */
                dev->lpc_conf[func][addr] = val & 0x8f;
                intel_ich2_pirq_routing(val & 0x80, addr - 0x60, val & 0x0f, dev);
            break;

            case 0x64:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0x68 ... 0x6b: /* PCI IRQ Routing E-H */
                dev->lpc_conf[func][addr] = val;
                intel_ich2_pirq_routing(val & 0x80, addr - 0x64, val & 0x0f, dev);
            break;

            case 0x88:
                dev->lpc_conf[func][addr] = val & 6;
            break;

            case 0x8a:
                dev->lpc_conf[func][addr] &= val & 6;
            break;

            case 0x90:
                dev->lpc_conf[func][addr] = val & 0xff;
            break;

            case 0x91:
                dev->lpc_conf[func][addr] = val & 0xfc;
            break;

            case 0xa0:
                dev->lpc_conf[func][addr] = val & 0x63;
            break;

            case 0xa1:
                dev->lpc_conf[func][addr] = val & 6;
            break;

            case 0xa2:
                dev->lpc_conf[func][addr] &= val & 3;
            break;

            case 0xa4:
                dev->lpc_conf[func][addr] = val & 1;
                dev->lpc_conf[func][addr] &= val & 6;
            break;

            case 0xb8 ... 0xbb: /* GPIO SMI */
                dev->lpc_conf[func][addr] = val;
            break;

            case 0xd0:
                dev->lpc_conf[func][addr] = val & 0xc7;
            break;

            case 0xc0:
                dev->lpc_conf[func][addr] = val & 0xf0;
            break;

            case 0xc4 ... 0xcd:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0xd1:
                dev->lpc_conf[func][addr] = val & 0x39;
            break;

            case 0xd3:
                dev->lpc_conf[func][addr] = val & 3;
            break;

            case 0xd4:
                dev->lpc_conf[func][addr] = val & 2;
            break;

            case 0xd5:
                dev->lpc_conf[func][addr] = val & 0x3f;
            break;

            case 0xd8: /* RTC Control */
                if((dev->lpc_conf[func][addr] & 0x18) != 0)
                    dev->lpc_conf[func][addr] = (val & 0x1c) | (dev->lpc_conf[func][addr] & 0x18);
                else
                    dev->lpc_conf[func][addr] = val & 0x1c;

                intel_ich2_nvr(dev);
            break;

            case 0xe0:
                dev->lpc_conf[func][addr] = val & 0x77;
            break;

            case 0xe1:
                dev->lpc_conf[func][addr] = val & 0x13;
            break;

            case 0xe2:
                dev->lpc_conf[func][addr] = val & 0x3b;
            break;

            case 0xe3: /* FWH Address Range Enable */
                dev->lpc_conf[func][addr] = val;
            break;

            case 0xe4:
                dev->lpc_conf[func][addr] = val & 0x81;
            break;

            case 0xe5 ... 0xe6:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0xe7:
                dev->lpc_conf[func][addr] = val & 0xcf;
            break;

            case 0xe8 ... 0xeb:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0xec:
                dev->lpc_conf[func][addr] = val & 0xf1;
            break;

            case 0xed ... 0xef:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0xf0:
                dev->lpc_conf[func][addr] = val & 0x0f;
            break;

            case 0xf2: /* Device Function Disable */
            case 0xf3:
                dev->lpc_conf[func][addr] = (addr & 1) ? (val & 1) : (val & 0x7e);
                intel_ich2_disable(dev);
            break;
        }
    }
    else if((func == 1) && !(dev->lpc_conf[0][0xf2] & 2)) /* IDE */
    {
        intel_ich2_log("Intel ICH2-IDE: dev->regs[%02x][%02x] = %02x POST: %02x \n", func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x04: /* IDE Controller */
                dev->lpc_conf[func][addr] = val & 5;
                intel_ich2_ide(dev);
                intel_ich2_bus_master(dev);
            break;

            case 0x07:
                dev->lpc_conf[func][addr] &= val & 0x28;
            break;

            case 0x20 ... 0x21: /* IDE Bus Mastering */
                dev->lpc_conf[func][addr] = (addr & 1) ? val : ((val & 0xf0) | 1);
                intel_ich2_bus_master(dev);    
            break;

            case 0x2c ... 0x2f:
                if(dev->lpc_conf[func][addr] == 0)
                    dev->lpc_conf[func][addr] = val;
            break;

            case 0x40:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0x41: /* Primary Channel Enable */
                dev->lpc_conf[func][addr] = val & 0xf3;
                intel_ich2_ide(dev);
            break;

            case 0x42:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0x43: /* Secondary Channel Enable */
                dev->lpc_conf[func][addr] = val & 0xf3;
                intel_ich2_ide(dev);
            break;

            case 0x44:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0x48:
                dev->lpc_conf[func][addr] = val & 0x0f;
            break;

            case 0x4a ... 0x4b:
                dev->lpc_conf[func][addr] = val & 0x33;
            break;

            case 0x54:
                dev->lpc_conf[func][addr] = val | 0xf0;
            break;

            case 0x55:
                dev->lpc_conf[func][addr] = val & 0xf4;
            break;
        }
    }
    else if(((func == 2) && !(dev->lpc_conf[0][0xf2] & 4)) || ((func == 4) && !(dev->lpc_conf[0][0xf2] & 0x10))) /* USB Hubs */
    {
        int usb_variant = !!(func == 4); /* Function 4 is Hub 1 while 2 is Hub 0 */
        intel_ich2_log("Intel ICH2-USB: HUB: %d dev->regs[%02x][%02x] = %02x POST: %02x\n", usb_variant, func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x04:
                dev->lpc_conf[func][addr] = val & 5;
                intel_ich2_usb(usb_variant, dev);
            break;

            case 0x07:
                dev->lpc_conf[func][addr] &= val & 0x28;
            break;

            case 0x20 ... 0x21: /* USB Base Address */
                dev->lpc_conf[func][addr] = (addr & 1) ? val : ((val & 0xe0) | 1);
                intel_ich2_usb(usb_variant, dev);
            break;

            case 0x2c ... 0x2f:
                if(dev->lpc_conf[func][addr] == 0)
                    dev->lpc_conf[func][addr] = val;
            break;

            case 0x3c:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0x3d:
                dev->lpc_conf[func][addr] = val & 7;
            break;

            case 0xc0:
                dev->lpc_conf[func][addr] = val & 0xbf;
            break;

            case 0xc1:
                dev->lpc_conf[func][addr] = val & 0x20;
                dev->lpc_conf[func][addr] &= val & 0x8f;
            break;

            case 0xc4:
                dev->lpc_conf[func][addr] = val & 3;
            break;

            default:
                dev->lpc_conf[func][addr] = val;
            break;
        }
    }
    else if((func == 3) && !(dev->lpc_conf[0][0xf2] & 8)) /* SMBus */
    {
        intel_ich2_log("Intel ICH2-SMBUS: dev->regs[%02x][%02x] = %02x POST: %02x \n", func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x04:
                dev->lpc_conf[func][addr] = val & 1;
                intel_ich2_smbus(dev);
            break;

            case 0x07:
                dev->lpc_conf[func][addr] &= val & 0x0e;
            break;

            case 0x20 ... 0x21: /* SMBus Base Address */
                dev->lpc_conf[func][addr] = (addr & 1) ? val : ((val & 0xf0) | 1);
                intel_ich2_smbus(dev);
            break;

            case 0x2c ... 0x2d:
                if(dev->lpc_conf[func][addr] == 0)
                    dev->lpc_conf[func][addr] = val;
            break;

            case 0x3c:
                dev->lpc_conf[func][addr] = val;
            break;

            case 0x40:
                dev->lpc_conf[func][addr] = val & 7;
                intel_ich2_smbus(dev);
            break;
        }
    }
    else if((func == 5) && !(dev->lpc_conf[0][0xf2] & 0x20)) /* AC97 */
    {
        intel_ich2_log("Intel ICH2-AC97: dev->regs[%02x][%02x] = %02x POST: %02x \n", func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x04:
                dev->lpc_conf[func][addr] = val & 5;
            break;

            case 0x07:
                dev->lpc_conf[func][addr] &= val & 0x26;
            break;

            case 0x11: /* AC97 Base Address */
//              dev->lpc_conf[func][addr] = val;
            break;

            case 0x14 ... 0x15: /* AC97 Bus Master Base Address */
//              dev->lpc_conf[func][addr] = (addr & 1) ? val : ((val & 0xc0) | 1);
            break;

            case 0x2c ... 0x2d:
                if(dev->lpc_conf[func][addr] == 0)
                    dev->lpc_conf[func][addr] = val;
            break;

            case 0x3c:
                dev->lpc_conf[func][addr] = val;
            break;
        }
    }
    else if((func == 6) && !(dev->lpc_conf[0][0xf2] & 0x40)) /* AC97 Modem */
    {
        intel_ich2_log("Intel ICH2-MODEM: dev->regs[%02x][%02x] = %02x POST: %02x \n", func, addr, val, inb(0x80));
        switch(addr)
        {
            case 0x04:
                dev->lpc_conf[func][addr] = val & 5;
            break;

            case 0x07:
                dev->lpc_conf[func][addr] &= val & 0x20;
            break;

            case 0x11: /* AC97 Modem Base Address */
//              dev->lpc_conf[func][addr] = val;
            break;

            case 0x14 ... 0x15: /* AC97 Modem Bus Master Base Address */
//              dev->lpc_conf[func][addr] = (addr & 1) ? val : ((val & 0xc0) | 1);
            break;

            case 0x2c ... 0x2d:
                if(dev->lpc_conf[func][addr] == 0)
                    dev->lpc_conf[func][addr] = val;
            break;

            case 0x3c:
                dev->lpc_conf[func][addr] = val;
            break;
        }
    }
}

static uint8_t
intel_ich2_lpc_read(int func, int addr, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    if (func == 0) {
        intel_ich2_log("Intel ICH2-LPC: dev->regs[%02x][%02x]  (%02x) POST: %02x \n", func, addr, dev->lpc_conf[func][addr], inb(0x80));
	    return dev->lpc_conf[func][addr];
    }
    else if((func == 1) && !(dev->lpc_conf[0][0xf2] & 2)) {
        intel_ich2_log("Intel ICH2-IDE: dev->regs[%02x][%02x]  (%02x) POST: %02x \n", func, addr, dev->lpc_conf[func][addr], inb(0x80));
        return dev->lpc_conf[func][addr];
    }
    else if(((func == 2) && !(dev->lpc_conf[0][0xf2] & 4)) || ((func == 4) && !(dev->lpc_conf[0][0xf2] & 0x10))) {
        intel_ich2_log("Intel ICH2-USB: HUB: %d dev->regs[%02x][%02x]  (%02x) POST: %02x \n", !!(func == 4), func, addr, dev->lpc_conf[func][addr], inb(0x80));
        return dev->lpc_conf[func][addr];
    }
    else if((func == 3) && !(dev->lpc_conf[0][0xf2] & 8)) {
        intel_ich2_log("Intel ICH2-SMBUS: dev->regs[%02x][%02x]  (%02x) POST: %02x \n", func, addr, dev->lpc_conf[func][addr], inb(0x80));
        return dev->lpc_conf[func][addr];
    }
    else if((func == 5) && !(dev->lpc_conf[0][0xf2] & 0x20)) {
       intel_ich2_log("Intel ICH2-AC97: dev->regs[%02x][%02x]  (%02x) POST: %02x \n", func, addr, dev->lpc_conf[func][addr], inb(0x80));
       return dev->lpc_conf[func][addr];
    }
    else if((func == 6) && !(dev->lpc_conf[0][0xf2] & 0x40)) {
       intel_ich2_log("Intel ICH2-MODEM: dev->regs[%02x][%02x]  (%02x) POST: %02x \n", func, addr, dev->lpc_conf[func][addr], inb(0x80));
       return dev->lpc_conf[func][addr];
    }
    else
        return 0xff;
}

static void
intel_ich2_reset(void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;
    memset(dev->hub_conf, 0, sizeof(dev->hub_conf));
    memset(dev->lpc_conf, 0, sizeof(dev->lpc_conf));

    /* LAN Interface */
    dev->lan_conf[0x00] = 0x86;
    dev->lan_conf[0x01] = 0x80;
    dev->lan_conf[0x02] = 0x49;
    dev->lan_conf[0x03] = 0x24;
    dev->lan_conf[0x06] = 0x80;
    dev->lan_conf[0x07] = 2;
    dev->lan_conf[0x08] = 2;
    dev->lan_conf[0x0b] = 2;
    dev->lan_conf[0x10] = 8;
    dev->lan_conf[0x14] = 1;
    dev->lan_conf[0x34] = 0xdc;
    dev->lan_conf[0x3d] = 1;
    dev->lan_conf[0x3e] = 8;
    dev->lan_conf[0x3f] = 0x38;
    dev->lan_conf[0xdc] = 1;
    dev->lan_conf[0xde] = 0x21;
    dev->lan_conf[0xdf] = 0xfe;


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
    dev->lpc_conf[0][0x58] = 1;
    dev->lpc_conf[0][0x60] = dev->lpc_conf[0][0x61] = dev->lpc_conf[0][0x62] = dev->lpc_conf[0][0x63] = 1;
    dev->lpc_conf[0][0x64] = 0x10;
    dev->lpc_conf[0][0x68] = dev->lpc_conf[0][0x69] = dev->lpc_conf[0][0x6a] = dev->lpc_conf[0][0x6b] = 1;
    dev->lpc_conf[0][0xd5] = 0x0f;
    dev->lpc_conf[0][0xe3] = 0xff;
    dev->lpc_conf[0][0xe8] = 0x33;
    dev->lpc_conf[0][0xe9] = 0x22;
    dev->lpc_conf[0][0xea] = 0x11;
    dev->lpc_conf[0][0xeb] = 0x00;
    dev->lpc_conf[0][0xee] = 0x67;
    dev->lpc_conf[0][0xef] = 0x45;
    dev->lpc_conf[0][0xf0] = 0x0f;

    intel_ich2_acpi(dev);
    intel_ich2_gpio(dev);
    intel_ich2_nvr(dev);

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
    dev->lpc_conf[1][0x54] = 0xf0; /* Bruteforce the 80-conductor cable */

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
    dev->lpc_conf[3][0x0a] = 5;
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

    /* Devices */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, intel_ich2_hub_read, intel_ich2_hub_write, dev);                 /* Bus 0: Device 30: HUB */
    dev->lpc_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, intel_ich2_lpc_read, intel_ich2_lpc_write, dev); /* Bus 0: Device 31: LPC */
    pci_add_card(PCI_ADD_NETWORK, intel_ich2_lan_read, intel_ich2_lan_write, dev);                     /* Bus 1: Device 8:  LAN */

    /* ACPI */
    dev->acpi = device_add(&acpi_intel_ich2_device);
    acpi_set_slot(dev->acpi, dev->lpc_slot);

    /* DMA */
    dma_alias_set_piix();
    ich2_dma_alias_init();
    ich2_dma16_alias_init();

    /* IDE */
    dev->ide_drive[0] = device_add_inst(&sff8038i_device, 1);
    dev->ide_drive[1] = device_add_inst(&sff8038i_device, 2);
    sff_set_irq_line(dev->ide_drive[0], 14);
    sff_set_irq_line(dev->ide_drive[1], 15);

    /* NVR */
    dev->nvr = device_add(&piix4_nvr_device);
    acpi_set_nvr(dev->acpi, dev->nvr);

    /* SMBus */
    dev->smbus = device_add(&ich2_smbus_device);

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
