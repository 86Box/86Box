/*
 * Intel ICH2
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

#include <86box/apm.h>
#include <86box/nvr.h>

#include <86box/acpi.h>
#include <86box/dma.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/intel_ich2_gpio.h>
#include <86box/intel_ich2_trap.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/smbus.h>
#include <86box/sound.h>
#include <86box/tco.h>
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
    uint8_t pci_conf[7][256];

    acpi_t *acpi;
    intel_ich2_gpio_t *gpio;
    intel_ich2_trap_t *trap_device[10];
    nvr_t *nvr;
    sff8038i_t *ide_drive[2];
    smbus_piix4_t *smbus;
    tco_t *tco;
    usb_t *usb_hub[2];

} intel_ich2_t;

/* LPC Bridge functions */
static void
intel_ich2_acpi_setup(intel_ich2_t *dev)
{
    uint32_t base = (dev->pci_conf[0][0x41] << 8) | (dev->pci_conf[0][0x40] & 0x80);
    int acpi_irq = ((dev->pci_conf[0][0x44] & 7) < 3) ? (9 + (dev->pci_conf[0][0x44] & 7)) : 9; /* Under APIC you can set this even higher but */
    int enable = !!(dev->pci_conf[0][0x44] & 0x10);                                             /* as we lack it we are restricted with low.   */

    acpi_update_io_mapping(dev->acpi, base, enable);
    acpi_set_irq_line(dev->acpi, acpi_irq);
}

static void
intel_ich2_bioswe(intel_ich2_t *dev)
{
    int bios_lock_enable = dev->pci_conf[0][0x4e] & 2;
    int bios_write = dev->pci_conf[0][0x4e] & 1;

    if(bios_lock_enable)
        if(bios_write) {
            intel_ich2_log("Intel ICH2 BIOSWE: BIOSWE SMI was raised\n");
            smi_raise();
        }
}

static void
intel_ich2_tco_interrupt(intel_ich2_t *dev)
{
    uint16_t tco_irq = ((dev->pci_conf[0][0x54] & 7) < 3) ? (9 + (dev->pci_conf[0][0x45] & 7)) : 9; /* Under APIC you can set this even higher but */
                                                                                                    /* as we lack it we are restricted with low.   */
    tco_irq_update(dev->tco, tco_irq);
}

static void
intel_ich2_gpio_setup(intel_ich2_t *dev)
{
    uint16_t base = (dev->pci_conf[0][0x59] << 8) | (dev->pci_conf[0][0x58] & 0xc0);
    int enable = !!(dev->pci_conf[0][0x5c] & 0x10);

    intel_ich2_gpio_base(enable, base, dev->gpio);
}

static int
intel_ich2_pirq_table(uint8_t val)
{
switch(val)
{
    case 0 ... 2:
    case 8:
    case 13:
    return PCI_IRQ_DISABLED;

    default:
    return val;
}
}

static void
intel_ich2_pirq_update(int reset, int addr, uint8_t val)
{
    int pirq = (addr >= 0x68) ? (addr - 0x63) : (addr - 0x5f);

    if(((val & 0x80) != 0x80) && !reset) {                                             /* 86Box doesn't have an APIC yet.                          */ 
        intel_ich2_log("Intel ICH2 LPC: Update PIRQ %c to IRQ %d\n", '@' + pirq, val); /* Under normal circumstances on an APIC enabled motherboard*/
        pci_set_irq_routing(pirq, intel_ich2_pirq_table(val));                         /* this remains disabled and the IRQ are handed by the APIC */  
    }                                                                                  /* itself.                                                  */
    else if(reset)
        for(int i = 1; i <= 8; i++)
            pci_set_irq_routing(i, PCI_IRQ_DISABLED);
}

static void
intel_ich2_nvr_handler(intel_ich2_t *dev)
{
    intel_ich2_log("Intel ICH2 LPC: Extended NVR Aliases %s\n", (dev->pci_conf[0][0xd8] & 4) ? "Enabled" : "Disabled");

    nvr_at_handler(!!(dev->pci_conf[0][0xd8] & 4), 0x74, dev->nvr);
    nvr_at_handler(!!(dev->pci_conf[0][0xd8] & 4), 0x76, dev->nvr);
}

static void
intel_ich2_trap_update(void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *) priv;
    uint16_t temp_addr = 0;

    /* Hard Drives */
    intel_ich2_device_trap_setup(0x48, 0x01, 0x1f0, 8, dev->trap_device[0]); // HDD's don't have a decode bit
    intel_ich2_device_trap_setup(0x48, 0x01, 0x3f6, 1, dev->trap_device[0]);

    intel_ich2_device_trap_setup(0x48, 0x02, 0x170, 8, dev->trap_device[1]);
    intel_ich2_device_trap_setup(0x48, 0x02, 0x376, 1, dev->trap_device[1]);

    /* COM A */
    switch(dev->pci_conf[0][0xe0] & 7)
    {
        case 0:
            temp_addr = 0x3f8;
        break;

        case 1:
            temp_addr = 0x2f8;
        break;

        case 2:
            temp_addr = 0x220;
        break;

        case 3:
            temp_addr = 0x228;
        break;

        case 4:
            temp_addr = 0x238;
        break;

        case 5:
            temp_addr = 0x2e8;
        break;

        case 6:
            temp_addr = 0x338;
        break;

        case 7:
            temp_addr = 0x3e8;
        break;
    }
    intel_ich2_device_trap_setup(0x48, 0x10, temp_addr, 8, dev->trap_device[2]);

    /* COM B */
    switch((dev->pci_conf[0][0xe0] >> 4) & 7)
    {
        case 0:
            temp_addr = 0x3f8;
        break;

        case 1:
            temp_addr = 0x2f8;
        break;

        case 2:
            temp_addr = 0x220;
        break;

        case 3:
            temp_addr = 0x228;
        break;

        case 4:
            temp_addr = 0x238;
        break;

        case 5:
            temp_addr = 0x2e8;
        break;

        case 6:
            temp_addr = 0x338;
        break;

        case 7:
            temp_addr = 0x3e8;
        break;
    }
    intel_ich2_device_trap_setup(0x48, 0x10, temp_addr, 8, dev->trap_device[3]);

    /* LPT */
    switch(dev->pci_conf[0][0xe1] & 3)
    {
        case 0:
            temp_addr = 0x378;
        break;

        case 1:
            temp_addr = 0x278;
        break;

        case 2:
            temp_addr = 0x3bc;
        break;
    }
    intel_ich2_device_trap_setup(0x48, 0x10, temp_addr, 8, dev->trap_device[4]);

    /* FDC */
    temp_addr = (dev->pci_conf[0][0xe1] & 0x10) ? 0x3f0 : 0x370;
    intel_ich2_device_trap_setup(0x48, 0x10, temp_addr, 8, dev->trap_device[5]);

    /* MSS (Note: There's no clear explaination about the SB Trap so only the MSS Trap is implementated) */
    switch((dev->pci_conf[0][0xe2] >> 4) & 3)
    {
        case 0:
            temp_addr = 0x530;
        break;

        case 1:
            temp_addr = 0x604;
        break;

        case 2:
            temp_addr = 0xe80;
        break;

        case 3:
            temp_addr = 0xf40;
        break;
    }
    intel_ich2_device_trap_setup(0x49, 0x04, temp_addr, 8, dev->trap_device[6]);

    /* MIDI */
    temp_addr = (dev->pci_conf[0][0xe2] & 8) ? 0x300 : 0x330;
    intel_ich2_device_trap_setup(0x49, 0x08, temp_addr, 2, dev->trap_device[7]);

    /* KBC */
    intel_ich2_device_trap_setup(0x49, 0x10, 0x60, 4, dev->trap_device[8]); // KBC doesn't have a decode bit

    /* Adlib */
    intel_ich2_device_trap_setup(0x49, 0x20, 0x388, 4, dev->trap_device[9]);
}

static void
intel_ich2_function_disable(intel_ich2_t *dev)
{
uint16_t smbus_addr = (dev->pci_conf[3][0x21] << 8) | (dev->pci_conf[3][0x20] & 0xf0); // Hold the SMBus Base Address value

/* Disable IDE */
if(dev->pci_conf[0][0xf2] & 2) {
    ide_pri_disable();
    ide_sec_disable();
    sff_bus_master_handler(dev->ide_drive[0], 0, 0);
    sff_bus_master_handler(dev->ide_drive[1], 0, 0);
}

/* Disable USB Hub 1 */
if(dev->pci_conf[0][0xf2] & 4) {
    uhci_update_io_mapping(dev->usb_hub[0], dev->pci_conf[2][0x20] & 0xe0, dev->pci_conf[0][0x21], 0);
}

/* Disable SMBus */
if(dev->pci_conf[0][0xf2] & 8) { // ICH2 Supports the ability of the SMBus Controller to be active even if it's PCI device is disabled
    smbus_piix4_remap(dev->smbus, smbus_addr, dev->pci_conf[0][0xf3] & 1);
}

/* Disable USB Hub 2 */
if(dev->pci_conf[0][0xf2] & 0x10) {
    uhci_update_io_mapping(dev->usb_hub[1], 0, 0, 0);
}
}

/* IDE Controller functions */
static void
intel_ich2_ide_setup(intel_ich2_t *dev)
{
    uint16_t bm_base = (dev->pci_conf[1][0x21] << 8) | (dev->pci_conf[1][0x20] & 0xf0); /* Get the Bus Master Address */

    ide_pri_disable();
    ide_sec_disable();
    sff_bus_master_handler(dev->ide_drive[0], 0, bm_base);
    sff_bus_master_handler(dev->ide_drive[1], 0, bm_base + 8);

    if(dev->pci_conf[1][0x41] & 0x80) {
        intel_ich2_log("Intel ICH2 IDE: Primary Channel is enabled with Bus Master Address 0x%x.\n", bm_base);
        ide_pri_enable();
        sff_bus_master_handler(dev->ide_drive[0], 1, bm_base);
    }

    if(dev->pci_conf[1][0x43] & 0x80) {
        intel_ich2_log("Intel ICH2 IDE: Secondary Channel is enabled with Bus Master Address 0x%x.\n", bm_base + 8);
        ide_sec_enable();
        sff_bus_master_handler(dev->ide_drive[1], 1, bm_base + 8);
    }
}

/* USB Controller functions */
static void
intel_ich2_usb_setup(int func, intel_ich2_t* dev)
{
    int current_hub = (func == 4) ? 4 : 2;
    int hub_num = (func == 4);
    uhci_update_io_mapping(dev->usb_hub[hub_num], dev->pci_conf[current_hub][0x20] & 0xe0, dev->pci_conf[current_hub][0x21], !!(dev->pci_conf[current_hub][0x04] & 1));
}

/* SMBus Controller functions */
static void
intel_ich2_smbus_setup(intel_ich2_t *dev)
{
    uint16_t base = (dev->pci_conf[3][0x21] << 8) | (dev->pci_conf[3][0x20] & 0xf0);

    if((dev->pci_conf[3][0x40] & 1) && (dev->pci_conf[3][0x04] & 1))
        intel_ich2_log("Intel ICH2 SMBus: SMBus is enabled.\n");

    smbus_piix4_remap(dev->smbus, base, (dev->pci_conf[3][0x40] & 1) && (dev->pci_conf[3][0x04] & 1));
}

/* ICH2 Registers */
static void
intel_ich2_write(int func, int addr, uint8_t val, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    if(func == 0) {
        intel_ich2_log("Intel ICH2 LPC: dev->regs[%02x] = %02x\n", addr, val);
        switch(addr)
        {
            case 0x04:
                dev->pci_conf[func][addr] = (val & 0x40) | 0x0f;
            break;

            case 0x05:
                dev->pci_conf[func][addr] = val & 0x01;
            break;

            case 0x07:
                dev->pci_conf[func][addr] &= val & 0xf9;
            break;

            case 0x40 ... 0x41:
                dev->pci_conf[func][addr] = val & ((addr & 1) ? 0xff : (0x80 | 1));
                intel_ich2_acpi_setup(dev);
            break;

            case 0x44:
                dev->pci_conf[func][addr] = val & 0x17;
                intel_ich2_acpi_setup(dev);
            break;

            case 0x4e:
                if(!(val & 2))
                    dev->pci_conf[func][addr] = val & 3;
                else
                    dev->pci_conf[func][addr] = (val & 1) | 2;

                intel_ich2_bioswe(dev);
            break;

            case 0x54:
                dev->pci_conf[func][addr] = val & 0x0f;
                intel_ich2_tco_interrupt(dev);
            break;

            case 0x58 ... 0x59:
                dev->pci_conf[func][addr] = val & ((addr & 1) ? 0xff : (0xc0 | 1));
                intel_ich2_gpio_setup(dev);
            break;

            case 0x5c:
                dev->pci_conf[func][addr] = val & 0x10;
                intel_ich2_gpio_setup(dev);
            break;

            case 0x60 ... 0x63:
            case 0x68 ... 0x6b:
                dev->pci_conf[func][addr] = val & 0x8f;
                intel_ich2_pirq_update(0, addr, val);
            break;

            case 0x64:
                dev->pci_conf[func][addr] = val;
            break;

            case 0x88:
                dev->pci_conf[func][addr] = val & 6;
            break;

            case 0x8a:
                dev->pci_conf[func][addr] &= val & 6;
            break;

            case 0x90:
                dev->pci_conf[func][addr] = val;
            break;

            case 0x91:
                dev->pci_conf[func][addr] = val & 0xfc;
            break;

            case 0xa0:
                dev->pci_conf[func][addr] = val & 0x6c;
            break;

            case 0xa1:
                dev->pci_conf[func][addr] = val & 6;
            break;

            case 0xa2:
                dev->pci_conf[func][addr] &= val & 3;
            break;

            case 0xa4:
                dev->pci_conf[func][addr] = val & 1;
                dev->pci_conf[func][addr] &= val & 6;
            break;

            case 0xb8 ... 0xbb: 
                dev->pci_conf[func][addr] = val; /* GPIO Routing */
            break;

            case 0xc0:
                dev->pci_conf[func][addr] = val & 0xf0;
            break;

            case 0xc4 ... 0xcb:
                dev->pci_conf[func][addr] = val;
            break;

            case 0xcc ... 0xcd:
                dev->pci_conf[func][addr] &= val;
            break;

            case 0xd0:
                dev->pci_conf[func][addr] = val & 0x4f; /* Brute force APIC support as disabled */
            break;

            case 0xd1:
                dev->pci_conf[func][addr] = val & 0x38; /* Brute force APIC support as disabled */
            break;

            case 0xd3:
                dev->pci_conf[func][addr] = val & 0x03;
            break;

            case 0xd4:
                dev->pci_conf[func][addr] = val & 0x02;
            break;

            case 0xd5:
                dev->pci_conf[func][addr] = val & 0x3f;
            break;

            case 0xd8:
                dev->pci_conf[func][addr] = val & 0x1c;
                intel_ich2_nvr_handler(dev);
            break;

            case 0xe0:
                dev->pci_conf[func][addr] = val & 0x77;
                intel_ich2_trap_update(dev);
            break;

            case 0xe1:
                dev->pci_conf[func][addr] = val & 0x13;
                intel_ich2_trap_update(dev);
            break;

            case 0xe2:
                dev->pci_conf[func][addr] = val & 0x3b;
                intel_ich2_trap_update(dev);
            break;

            case 0xe3:
                dev->pci_conf[func][addr] = val;
            break;

            case 0xe4:
                dev->pci_conf[func][addr] = val & 0x81;
            break;

            case 0xe5 ... 0xe6:
                dev->pci_conf[func][addr] = val;
            break;

            case 0xe7:
                dev->pci_conf[func][addr] = val & 0x3f;
            break;

            case 0xe8 ... 0xeb:
                dev->pci_conf[func][addr] = val;
            break;

            case 0xec:
                dev->pci_conf[func][addr] = val & 0xf1;
            break;

            case 0xed:
                dev->pci_conf[func][addr] = val;
            break;

            case 0xee ... 0xef:
                dev->pci_conf[func][addr] = val;
            break;

            case 0xf0:
                dev->pci_conf[func][addr] = val & 0x0f;
            break;

            case 0xf2 ... 0xf3:
                dev->pci_conf[func][addr] = val & ((addr & 1) ? 0x01 : 0xfe);
                intel_ich2_function_disable(dev);
            break;
        }
    }
    else if((func == 1) && !(dev->pci_conf[0][0xf2] & 2)) {
        intel_ich2_log("Intel ICH2 IDE: dev->regs[%02x] = %02x\n", addr, val);
        switch(addr)
        {
            case 0x04:
                dev->pci_conf[func][addr] = val & 5;
                intel_ich2_ide_setup(dev);
            break;

            case 0x07:
                dev->pci_conf[func][addr] &= val & 0x2e;
            break;

            case 0x20 ... 0x21:
                dev->pci_conf[func][addr] = val & ((addr & 1) ? 0xff : (0xf0 | 1));
                intel_ich2_ide_setup(dev);
            break;

            case 0x2c ... 0x2f:
                if(dev->pci_conf[func][addr] != 0)
                    dev->pci_conf[func][addr] = val;
            break;

            case 0x40 ... 0x43:
                dev->pci_conf[func][addr] = val & ((addr & 1) ? 0xf3 : 0xff);

                if((addr == 0x41) || (addr == 0x43))
                    intel_ich2_ide_setup(dev);
            break;

            case 0x44:
                dev->pci_conf[func][addr] = val;
            break;

            case 0x48:
                dev->pci_conf[func][addr] = val & 0x0f;
            break;

            case 0x4a ... 0x4b:
                dev->pci_conf[func][addr] = val & 0x33;
            break;
        }
    }
    else if(((func == 2) && !(dev->pci_conf[0][0xf2] & 4)) || ((func == 4) && !(dev->pci_conf[0][0xf2] & 0x10))) {
        intel_ich2_log("Intel ICH2 USB Hub %d: dev->regs[%02x] = %02x\n", (func == 4), addr, val);
        switch(addr)
        {
            case 0x04:
                dev->pci_conf[func][addr] = val & 5;
                intel_ich2_usb_setup(func, dev);
            break;

            case 0x07:
                dev->pci_conf[func][addr] &= val & 0x2e;
            break;

            case 0x20 ... 0x21:
                dev->pci_conf[func][addr] = val & ((addr & 1) ? 0xff : (0xf0 | 1));
                intel_ich2_usb_setup(func, dev);
            break;

            case 0xc0:
                dev->pci_conf[func][addr] = val & 0xbf;
            break;

            case 0xc1:
                dev->pci_conf[func][addr] &= val & 0xaf;
            break;

            case 0xc4:
                dev->pci_conf[func][addr] = val & 3;
            break;
        }
    }
    else if((func == 3) && !(dev->pci_conf[0][0xf2] & 8)) {
        intel_ich2_log("Intel ICH2 SMBus: dev->regs[%02x] = %02x\n", addr, val);
        switch(addr)
        {
            case 0x04:
                dev->pci_conf[func][addr] = val & 1;
                intel_ich2_smbus_setup(dev);
            break;

            case 0x07:
                dev->pci_conf[func][addr] &= val & 0x0e;
            break;

            case 0x20 ... 0x21:
                dev->pci_conf[func][addr] = val & ((addr & 1) ? 0xff : (0xf0 | 1));
                intel_ich2_smbus_setup(dev);
            break;

            case 0x3c:
                dev->pci_conf[func][addr] = val;                       /* 86Box doesn't give any capabilities to take the PCI IRQ pin, also */
                smbus_piix4_get_irq(pci_get_int(0x1f, 2), dev->smbus); /* can't use pointers as whatever recieved from there is temporary.  */
                intel_ich2_log("Intel ICH2 SMBus: Got IRQ %d\n", pci_get_int(0x1f, 2));
            break;

            case 0x40:
                dev->pci_conf[func][addr] = val & 7;
                intel_ich2_smbus_setup(dev);
                smbus_piix4_smi_en(!!(val & 2), dev->smbus);
            break;
        }
    }
}


static uint8_t
intel_ich2_read(int func, int addr, void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;

    if(func == 0) {
    intel_ich2_log("Intel ICH2 LPC: dev->regs[%02x] (%02x)\n", addr, dev->pci_conf[func][addr]);
    return dev->pci_conf[func][addr];
    }
    else if((func == 1) && !(dev->pci_conf[0][0xf2] & 2)) {
    intel_ich2_log("Intel ICH2 IDE: dev->regs[%02x] (%02x)\n", addr, dev->pci_conf[func][addr]);
    return dev->pci_conf[func][addr];
    }
    else if(((func == 2) && !(dev->pci_conf[0][0xf2] & 4)) || ((func == 4) && !(dev->pci_conf[0][0xf2] & 0x10))) {
    intel_ich2_log("Intel ICH2 USB Hub %d: dev->regs[%02x] (%02x)\n", (func == 4), addr, dev->pci_conf[func][addr]);

    if((addr >= 0x2c) && (addr <= 0x2f)) /* USB shares the same subsystem vendor info as the IDE */
        return dev->pci_conf[1][addr];

    return dev->pci_conf[func][addr];
    }
    else if((func == 3) && !(dev->pci_conf[0][0xf2] & 8)) {
    intel_ich2_log("Intel ICH2 SMBus: dev->regs[%02x] (%02x)\n", addr, dev->pci_conf[func][addr]);

    if((addr >= 0x2c) && (addr <= 0x2f)) /* SMBus shares the same subsystem vendor info as the IDE */
        return dev->pci_conf[1][addr];

    return dev->pci_conf[func][addr];
    }
    else return 0xff;
}


static void
intel_ich2_reset(void *priv)
{
    intel_ich2_t *dev = (intel_ich2_t *)priv;
    memset(dev->pci_conf, 0, sizeof(dev->pci_conf)); /* Wash out the Registers */

    /* Function 0: LPC Bridge */
    dev->pci_conf[0][0x00] = 0x86;
    dev->pci_conf[0][0x01] = 0x80;

    dev->pci_conf[0][0x02] = 0x40;
    dev->pci_conf[0][0x03] = 0x24;

    dev->pci_conf[0][0x04] = 0x0f;

    dev->pci_conf[0][0x06] = 0x80;
    dev->pci_conf[0][0x07] = 0x02;

    dev->pci_conf[0][0x08] = 0x02;

    dev->pci_conf[0][0x0a] = 0x01;
    dev->pci_conf[0][0x0b] = 0x06;

    dev->pci_conf[0][0x0e] = 0x80;

    dev->pci_conf[0][0x40] = 0x01;

    dev->pci_conf[0][0x58] = 0x01;

    dev->pci_conf[0][0x60] = 0x80;
    dev->pci_conf[0][0x61] = 0x80;
    dev->pci_conf[0][0x62] = 0x80;
    dev->pci_conf[0][0x63] = 0x80;

    dev->pci_conf[0][0x64] = 0x10;

    dev->pci_conf[0][0x68] = 0x80;
    dev->pci_conf[0][0x69] = 0x80;
    dev->pci_conf[0][0x6a] = 0x80;
    dev->pci_conf[0][0x6b] = 0x80;

    dev->pci_conf[0][0xd5] = 0x0f;

    dev->pci_conf[0][0xe3] = 0xff;

    dev->pci_conf[0][0xe8] = 0x33;
    dev->pci_conf[0][0xe9] = 0x22;
    dev->pci_conf[0][0xea] = 0x11;
    dev->pci_conf[0][0xeb] = 0x00;

    dev->pci_conf[0][0xee] = 0x78;
    dev->pci_conf[0][0xef] = 0x56;

    dev->pci_conf[0][0xf0] = 0x0f;

    intel_ich2_acpi_setup(dev); /* Setup the ACPI Interface */
    intel_ich2_tco_interrupt(dev); /* Configure the TCO Interrupt */
    intel_ich2_gpio_setup(dev); /* Setup the GPIO */
    intel_ich2_pirq_update(1, 0, 0);  /* Reset the PIRQ interrupts */
    intel_ich2_nvr_handler(dev); /* Set the NVR aliases */

    /* Function 1: IDE Controller */
    dev->pci_conf[1][0x00] = 0x86;
    dev->pci_conf[1][0x01] = 0x80;

    dev->pci_conf[1][0x02] = 0x4b;
    dev->pci_conf[1][0x03] = 0x24;

    dev->pci_conf[1][0x06] = 0x80;
    dev->pci_conf[1][0x07] = 0x02;

    dev->pci_conf[1][0x08] = 0x02;

    dev->pci_conf[1][0x09] = 0x80;
    dev->pci_conf[1][0x0a] = 0x01;
    dev->pci_conf[1][0x0b] = 0x01;

    dev->pci_conf[1][0x20] = 0x01;

    dev->pci_conf[1][0x54] = 0xff; /* Hack: Fake Cable Conductor & UltraDMA details */

    if(cpu_busspeed >= 100000000)  /* Go UltraDMA 100 if CPU is up for it. Not that it actually matters */
        dev->pci_conf[1][0x55] = 0xf0;

    sff_bus_master_reset(dev->ide_drive[0], 0); /* Setup the IDE */
    sff_bus_master_reset(dev->ide_drive[1], 8);
    intel_ich2_ide_setup(dev);

    /* Function 2: USB Hub 0 */
    dev->pci_conf[2][0x00] = 0x86;
    dev->pci_conf[2][0x01] = 0x80;

    dev->pci_conf[2][0x02] = 0x42;
    dev->pci_conf[2][0x03] = 0x24;

    dev->pci_conf[2][0x06] = 0x80;
    dev->pci_conf[2][0x07] = 0x02;

    dev->pci_conf[2][0x08] = 0x02;

    dev->pci_conf[2][0x0a] = 0x03;
    dev->pci_conf[2][0x0b] = 0x0c;

    dev->pci_conf[2][0x20] = 0x01;

    dev->pci_conf[2][0x3d] = 0x03;

    dev->pci_conf[2][0x60] = 0x10;

    dev->pci_conf[2][0xc1] = 0x20;

    intel_ich2_usb_setup(2, dev);

    /* Function 3: SMBus Controller */
    dev->pci_conf[3][0x00] = 0x86;
    dev->pci_conf[3][0x01] = 0x80;

    dev->pci_conf[3][0x02] = 0x43;
    dev->pci_conf[3][0x03] = 0x24;

    dev->pci_conf[3][0x06] = 0x80;
    dev->pci_conf[3][0x07] = 0x02;

    dev->pci_conf[3][0x08] = 0x02;

    dev->pci_conf[3][0x09] = 0x80;
    dev->pci_conf[3][0x0a] = 0x05;
    dev->pci_conf[3][0x0b] = 0x0c;

    dev->pci_conf[3][0x20] = 0x01;

    dev->pci_conf[3][0x3d] = 0x02;

    intel_ich2_smbus_setup(dev); /* Setup the SMBus */

    /* Function 4: USB Hub 1*/
    dev->pci_conf[4][0x00] = 0x86;
    dev->pci_conf[4][0x01] = 0x80;

    dev->pci_conf[4][0x02] = 0x44;
    dev->pci_conf[4][0x03] = 0x24;

    dev->pci_conf[4][0x06] = 0x80;
    dev->pci_conf[4][0x07] = 0x02;

    dev->pci_conf[4][0x08] = 0x02;

    dev->pci_conf[4][0x0a] = 0x03;
    dev->pci_conf[4][0x0b] = 0x0c;

    dev->pci_conf[4][0x20] = 0x01;

    dev->pci_conf[4][0x3d] = 0x03;

    dev->pci_conf[4][0x60] = 0x10;

    dev->pci_conf[4][0xc1] = 0x20;

    intel_ich2_usb_setup(4, dev);
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
    int slot;

    /* Device */
    slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, intel_ich2_read, intel_ich2_write, dev); /* Device 31: Intel ICH2 */

    /* ACPI Interface */
    dev->acpi = device_add(&acpi_intel_ich2_device);
    acpi_set_slot(dev->acpi, slot);

    /* DMA */
    dma_alias_set_piix();
    dma_lpc_init();

    /* GPIO */
    dev->gpio = device_add(&intel_ich2_gpio_device);

    /* NVR Handler */
    dev->nvr = device_add(&piix4_nvr_device);
    acpi_set_nvr(dev->acpi, dev->nvr);

    /* Intel ICH2 Hub */
    device_add(&intel_ich2_hub_device);

    /* PIC */
    pic_set_pci();

    /* SMBus */
    dev->smbus = device_add(&intel_ich2_smbus_device);
    smbus_piix4_get_acpi(dev->smbus, dev->acpi);

    /* SFF Compatible IDE Drives */
    dev->ide_drive[0] = device_add_inst(&sff8038i_device, 1);
    dev->ide_drive[1] = device_add_inst(&sff8038i_device, 2);
    sff_set_slot(dev->ide_drive[0], slot);
    sff_set_slot(dev->ide_drive[1], slot);

    /* TCO */
    dev->tco = device_add(&tco_device);
    acpi_set_tco(dev->acpi, dev->tco);

    /* I/O Traps */
    acpi_set_trap_update(dev->acpi, intel_ich2_trap_update, dev);

    for (int i = 0; i < 10; i++) {
        dev->trap_device[i] = device_add_inst(&intel_ich2_trap_device, i + 1);
        intel_ich2_trap_set_acpi(dev->trap_device[i], dev->acpi);
    }

    /* USB */
    dev->usb_hub[0] = device_add_inst(&usb_device, 1);
    dev->usb_hub[1] = device_add_inst(&usb_device, 2);

    intel_ich2_reset(dev);

    return dev;
}

const device_t intel_ich2_device = {
    .name = "Intel ICH2",
    .internal_name = "intel_ich2",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = intel_ich2_init,
    .close = intel_ich2_close,
    .reset = intel_ich2_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
