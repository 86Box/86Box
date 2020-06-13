/*

 86Box  A hypervisor and IBM PC system emulator that specializes in
 		running old operating systems and software designed for IBM
 		PC systems and compatibles from 1981 through fairly recent
 		system designs based on the PCI bus.


    Implementation of the VT82C596B. Based on VT82C586B + PIIX4

    Authors: Sarah Walker, <http://pcem-emulator.co.uk/>

        Copyright 2020 Tiseno100 <Implemented the 596B overlay>
		Copyright 2020 Sarah Walker <Main 586B code>
		Copyright 2020 Miran Grca <Author>
		Copyright 2020 Melissa Goad <Port to 86Box>

    TODO:
     - The SMBus must be checked and implemented properly
     - Fix Documentation errors

*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/cdrom.h>
#include "cpu.h"
#include <86box/scsi_device.h>
#include <86box/scsi_cdrom.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/apm.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/port_92.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/zip.h>
#include <86box/machine.h>
#include <86box/chipset.h>

// As of now
#include <86box/smbus_piix4.h>

#define ACPI_TIMER_FREQ 3579545 // Probably Emulator related

#define ACPI_IO_ENABLE   (1 << 7)
#define ACPI_TIMER_32BIT (1 << 3)

#if defined(DEV_BRANCH) && defined(USE_596B)

typedef struct
{
    uint8_t		pci_to_isa_regs[256]; // PCI-to-ISA (Same as 586B)
    uint8_t		ide_regs[256]; // Common VIA IDE controller
    uint8_t		usb_regs[256]; // Common VIA USB controller
    uint8_t		power_regs[256]; // VT82C596B Power Managment Device(Same as 586B + SMBus)
    sff8038i_t *	bm[2];

    nvr_t *		nvr;
    int			nvr_enabled, slot;

    struct
    {
	    uint16_t io_base;
    }usb;

    struct
    {
	    uint16_t io_base;
    }power;

    smbus_piix4_t *	smbus;
    
} via_vt82c596b_t;

static void
via_vt82c596b_reset(void *priv)
{

    via_vt82c596b_t *via_vt82c596b = (via_vt82c596b_t *) priv;
    uint16_t old_base = (via_vt82c596b->ide_regs[0x20] & 0xf0) | (via_vt82c596b->ide_regs[0x21] << 8);

    sff_bus_master_reset(via_vt82c596b->bm[0], old_base);
    sff_bus_master_reset(via_vt82c596b->bm[1], old_base + 8);

    memset(via_vt82c596b->pci_to_isa_regs, 0, 256);
    memset(via_vt82c596b->ide_regs, 0, 256);
    memset(via_vt82c596b->usb_regs, 0, 256);
    memset(via_vt82c596b->power_regs, 0, 256);


    //PCI-to-ISA registers
    via_vt82c596b->pci_to_isa_regs[0x00] = 0x06; //VIA
    via_vt82c596b->pci_to_isa_regs[0x01] = 0x11;

    via_vt82c596b->pci_to_isa_regs[0x02] = 0x96; //VT82C596B
    via_vt82c596b->pci_to_isa_regs[0x03] = 0x05;

    via_vt82c596b->pci_to_isa_regs[0x04] = 0x0f; //Control

    via_vt82c596b->pci_to_isa_regs[0x07] = 2; //Status

    via_vt82c596b->pci_to_isa_regs[0x09] = 0; //Program Interface

    via_vt82c596b->pci_to_isa_regs[0x0A] = 1; //Sub-Class code
    via_vt82c596b->pci_to_isa_regs[0x0B] = 6; //Class code

    via_vt82c596b->pci_to_isa_regs[0x0E] = 0x80; //Header Type

    via_vt82c596b->pci_to_isa_regs[0x2C] = 0x73; //Subsystem ID
    via_vt82c596b->pci_to_isa_regs[0x2D] = 0x72;
    via_vt82c596b->pci_to_isa_regs[0x2E] = 0x71;
    via_vt82c596b->pci_to_isa_regs[0x2F] = 0x70;

    via_vt82c596b->pci_to_isa_regs[0x48] = 1; //Miscellaneous control 3
    via_vt82c596b->pci_to_isa_regs[0x4A] = 4;
    via_vt82c596b->pci_to_isa_regs[0x4F] = 3;

    via_vt82c596b->pci_to_isa_regs[0x50] = 0x24; //Reserved(?)
    via_vt82c596b->pci_to_isa_regs[0x59] = 4; //PIRQ Pin Configuration

    //Resetting the DMA
    dma_e = 0x00;
    for (int i = 0; i < 8; i++) {
	dma[i].ab &= 0xffff000f;
	dma[i].ac &= 0xffff000f;
    }

    pic_set_shadow(0);

    //IDE registers
    via_vt82c596b->ide_regs[0x00] = 0x06; //VIA
    via_vt82c596b->ide_regs[0x01] = 0x11;

    via_vt82c596b->ide_regs[0x02] = 0x71; //Common VIA IDE Controller
    via_vt82c596b->ide_regs[0x03] = 0x05;

    via_vt82c596b->ide_regs[0x04] = 0x80; //Command

    via_vt82c596b->ide_regs[0x06] = 0x80; //Status
    via_vt82c596b->ide_regs[0x07] = 0x02;

    via_vt82c596b->ide_regs[0x09] = 0x85; //Programming Interface

    via_vt82c596b->ide_regs[0x0A] = 0x01; //Sub class code
    via_vt82c596b->ide_regs[0x0B] = 0x01; //Base class code

    //Base address control commands
    via_vt82c596b->ide_regs[0x10] = 0xF0;
    via_vt82c596b->ide_regs[0x11] = 0x01;

    via_vt82c596b->ide_regs[0x14] = 0xF4;
    via_vt82c596b->ide_regs[0x15] = 0x03;

    via_vt82c596b->ide_regs[0x18] = 0x70;
    via_vt82c596b->ide_regs[0x19] = 0x01;

    via_vt82c596b->ide_regs[0x1C] = 0x74; 
    via_vt82c596b->ide_regs[0x1D] = 0x03;

    via_vt82c596b->ide_regs[0x20] = 0x01;
    via_vt82c596b->ide_regs[0x21] = 0xCC;
    ////

    via_vt82c596b->ide_regs[0x3C] = 0x0E; //Interrupt line

    via_vt82c596b->ide_regs[0x40] = 0x08; //Chip Enable

    via_vt82c596b->ide_regs[0x41] = 0x02; //IDE Configuration

    via_vt82c596b->ide_regs[0x42] = 0x09; //Reserved

    via_vt82c596b->ide_regs[0x43] = 0x3A; //FIFO Configuration

    via_vt82c596b->ide_regs[0x44] = 0x68; //Miscellaneous Control 1

    via_vt82c596b->ide_regs[0x46] = 0xC0; //Miscellaneous Control 3

    via_vt82c596b->ide_regs[0x48] = 0xA8; //Driver Timing Control
    via_vt82c596b->ide_regs[0x49] = 0xA8;
    via_vt82c596b->ide_regs[0x4A] = 0xA8;
    via_vt82c596b->ide_regs[0x4B] = 0xA8;

    via_vt82c596b->ide_regs[0x4C] = 0xFF; //Address Setup Time

    via_vt82c596b->ide_regs[0x4E] = 0xFF; //Sec Non-1F0 Port Access Timing
    via_vt82c596b->ide_regs[0x4F] = 0xFF; //Pri Non-1F0 Port Access Timing

    via_vt82c596b->ide_regs[0x50] = 0x03; //UltraDMA33 Extended Timing Control
    via_vt82c596b->ide_regs[0x51] = 0x03;
    via_vt82c596b->ide_regs[0x52] = 0x03;
    via_vt82c596b->ide_regs[0x53] = 0x03;

    via_vt82c596b->ide_regs[0x61] = 0x02; //IDE Primary Sector Size
    via_vt82c596b->ide_regs[0x69] = 0x02; //IDE Secondary Sector Size

    via_vt82c596b->usb_regs[0x00] = 0x06; //VIA USB Common Controller
    via_vt82c596b->usb_regs[0x01] = 0x11;
    via_vt82c596b->usb_regs[0x02] = 0x38;
    via_vt82c596b->usb_regs[0x03] = 0x30;

    //USB Registers
    via_vt82c596b->usb_regs[0x04] = 0; //Control
    via_vt82c596b->usb_regs[0x05] = 0;

    via_vt82c596b->usb_regs[0x06] = 0;
    via_vt82c596b->usb_regs[0x07] = 2; //Status

    via_vt82c596b->usb_regs[0x0A] = 3; //Sub Class Code
    via_vt82c596b->usb_regs[0x0B] = 0x0C; //Base Class Code

    via_vt82c596b->usb_regs[0x0D] = 0x16; //Latency Timer

    via_vt82c596b->usb_regs[0x20] = 1; //Base address
    via_vt82c596b->usb_regs[0x21] = 3;

    via_vt82c596b->usb_regs[0x3D] = 4; //Interrupt Pin

    via_vt82c596b->usb_regs[0x60] = 0x10; //Serial Bus Release Number(USB 1.0)
    via_vt82c596b->usb_regs[0xC1] = 0x20; //Legacy Support

    //Power Management Registers
    via_vt82c596b->power_regs[0x00] = 0x06; //VT82C596B Power Managment Controller
    via_vt82c596b->power_regs[0x01] = 0x11;
    via_vt82c596b->power_regs[0x02] = 0x50;
    via_vt82c596b->power_regs[0x03] = 0x30;

    via_vt82c596b->power_regs[0x04] = 0; //Control
    via_vt82c596b->power_regs[0x05] = 0;

    via_vt82c596b->power_regs[0x06] = 0x80; //Status
    via_vt82c596b->power_regs[0x07] = 2;

    via_vt82c596b->power_regs[0x48] = 1; //Power Managment IO Base

    via_vt82c596b->power_regs[0x90] = 1; //SMBus IO Base

    //Setting up Routing
    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);

    //Disabling the Primary & Secondary controller(?)
    ide_pri_disable();
    ide_sec_disable();
}


// IDE CONTROLLER
static void
ide_handlers(via_vt82c596b_t *dev)
{
    uint16_t main, side;

    ide_pri_disable();
    ide_sec_disable();

    //On Bitfield 0(0x01) Primary Channel operating mode
    if (dev->ide_regs[0x09] & 0x01) {
	main = (dev->ide_regs[0x11] << 8) | (dev->ide_regs[0x10] & 0xf8);
	side = ((dev->ide_regs[0x15] << 8) | (dev->ide_regs[0x14] & 0xfc)) + 2;
    } else {
	main = 0x1f0;
	side = 0x3f6;
    }
    ide_set_base(0, main);
    ide_set_side(0, side);

    //On Bitfield 2(0x04) Secondary Channel operating mode
    if (dev->ide_regs[0x09] & 0x04) {
	main = (dev->ide_regs[0x19] << 8) | (dev->ide_regs[0x18] & 0xf8);
	side = ((dev->ide_regs[0x1d] << 8) | (dev->ide_regs[0x1c] & 0xfc)) + 2;
    } else {
	main = 0x170;
	side = 0x376;
    }
    ide_set_base(1, main);
    ide_set_side(1, side);

    //Enable the Primary & Secondary Controllers
    if (dev->ide_regs[0x04] & PCI_COMMAND_IO) {

        //On Bitfield 0(0x01) Enable the Secondary Controller
        //On Bitfield 1(0x02) Enable the Primary Controller
	if (dev->ide_regs[0x40] & 0x02)
		ide_pri_enable();
	if (dev->ide_regs[0x40] & 0x01)
		ide_sec_enable();
    }
}


static void
ide_irqs(via_vt82c596b_t *dev)
{
    int irq_mode[2] = { 0, 0 };

    if (dev->ide_regs[0x09] & 0x01)
	irq_mode[0] = (dev->ide_regs[0x3d] & 0x01);

    if (dev->ide_regs[0x09] & 0x04)
	irq_mode[1] = (dev->ide_regs[0x3d] & 0x01);

    sff_set_irq_mode(dev->bm[0], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[0], 1, irq_mode[1]);

    sff_set_irq_mode(dev->bm[1], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[1], 1, irq_mode[1]);
}


static void
bus_master_handlers(via_vt82c596b_t *dev)
{
    uint16_t base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);

    //On Bitfield 0(0x01) I/O Space
    sff_bus_master_handler(dev->bm[0], (dev->ide_regs[0x04] & 1), base);
    sff_bus_master_handler(dev->bm[1], (dev->ide_regs[0x04] & 1), base + 8);
}
////

// USB CONTROLLER

static uint8_t
usb_reg_read(uint16_t addr, void *p)
{
    uint8_t ret = 0xff;

    switch (addr & 0x1f) {
	case 0x10: case 0x11: case 0x12: case 0x13:
        // Return 0 on port status
                ret = 0x00;
		break;
    }

    return ret;
}

static void
usb_reg_write(uint16_t addr, uint8_t val, void *p) {}

static void
usb_update_io_mapping(via_vt82c596b_t *dev)
{
    if (dev->usb.io_base != 0x0000)
	io_removehandler(dev->usb.io_base, 0x20, usb_reg_read, NULL, NULL, usb_reg_write, NULL, NULL, dev);

    //On Bitfield 31 defaults to zero (0x20)
    dev->usb.io_base = (dev->usb_regs[0x20] & ~0x1f) | (dev->usb_regs[0x21] << 8);

    //0x20
    //Master Interrupt Control(?)
    //TODO: Find the exact meaning
    if ((dev->usb_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) && (dev->usb.io_base != 0x0000))
	io_sethandler(dev->usb.io_base, 0x20, usb_reg_read, NULL, NULL, usb_reg_write, NULL, NULL, dev);
}

////

// NVR
static void
nvr_update_io_mapping(via_vt82c596b_t *dev)
{

    //0x74 CMOS Memory Address
    //0x75 CMOS Memory Data

    //Ports 74-75 may be used to access CMOS if the internal RTC is disabled.

    if (dev->nvr_enabled)
	nvr_at_handler(0, 0x0074, dev->nvr);

    //In case of we are set on 5B bitfield 1(0x02)(RTC SRAM Access Enable) and 48 bitfield 3(0x08)
    //(Extra RTC Port 74/75 Enable)
    if ((dev->pci_to_isa_regs[0x5b] & 0x02) && (dev->pci_to_isa_regs[0x48] & 0x08))
	nvr_at_handler(1, 0x0074, dev->nvr);
}
////

// POWER MANAGMENT

// Need excessive documentation
static uint8_t
power_reg_read(uint16_t addr, void *p)
{
    via_vt82c596b_t *dev = (via_vt82c596b_t *) p;

    uint32_t timer;
    uint8_t ret = 0xff;

    switch (addr & 0xff) {
	case 0x08: case 0x09: case 0x0a: case 0x0b:

		//ACPI Timer
		    timer = (tsc * ACPI_TIMER_FREQ) / machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed;
		    
            if (!(dev->power_regs[0x41] & ACPI_TIMER_32BIT))
			    timer &= 0x00ffffff;
		    ret = (timer >> (8 * (addr & 3))) & 0xff;

		break;
    }

    return ret;
}

static void
power_reg_write(uint16_t addr, uint8_t val, void *p) {}

static void
power_update_io_mapping(via_vt82c596b_t *dev)
{
    if (dev->power.io_base != 0x0000)
	io_removehandler(dev->power.io_base, 0x100, power_reg_read, NULL, NULL, power_reg_write, NULL, NULL, dev);

    dev->power.io_base = (dev->power_regs[0x49] << 8);

    if ((dev->power_regs[0x41] & ACPI_IO_ENABLE) && (dev->power.io_base != 0x0000))
	io_sethandler(dev->power.io_base, 0x100, power_reg_read, NULL, NULL, power_reg_write, NULL, NULL, dev);
}

static void
smbus_update_io_mapping(via_vt82c596b_t *dev)
{
    smbus_piix4_remap(dev->smbus, (dev->power_regs[0x91] << 8) | (dev->power_regs[0x90] & 0xf0), (dev->power_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) && (dev->power_regs[0xD2] & 0x01));
}
////


static uint8_t
via_vt82c596b_read(int func, int addr, void *priv)
{
    via_vt82c596b_t *dev = (via_vt82c596b_t *) priv;

    uint8_t ret = 0xff;
    int c;

    switch(func) {
	    case 0:
        //By System I/O Map, setting up the Keyboard Controller
        //60-6F Keyboard Controller [0000 0000 0110] xnxn
		    if ((addr >= 0x60) && (addr <= 0x6f)) {
			    c = (addr & 0x0e) >> 1;
			    if (addr & 0x01) //If Enabled
				    ret = (dma[c].ab & 0x0000ff00) >> 8;
			    else {
				    ret = (dma[c].ab & 0x000000f0);
				    ret |= (!!(dma_e & (1 << c)) << 3);
			    }
		    } else
			    ret = dev->pci_to_isa_regs[addr];
		    break;
        case 1:
		    ret = dev->ide_regs[addr];
		    break;
	    case 2:
		    ret = dev->usb_regs[addr];
		    break;
        case 3:
		    ret = dev->power_regs[addr];
		    break;
    }

    return ret;
}

static void
via_vt82c596b_write(int func, int addr, uint8_t val, void *priv)
{
    via_vt82c596b_t *dev = (via_vt82c596b_t *) priv;
    int c;

    //Excessive Documentation is needed!!

    if (func > 3)
	return;

    switch(func) {
    //PCI-to-ISA
	case 0:
    //Read-only addresses
		if ((addr < 4) || (addr == 5) || ((addr >= 8) && (addr < 0x40)) ||
		   (addr == 0x49) || (addr == 0x4b) || ((addr >= 0x51) && (addr < 0x54)) || 
           ((addr >= 0x5d) && (addr < 0x60)) ||
		   ((addr >= 0x68) && (addr < 0x6a)) || (addr >= 0x73))
		return;

		switch (addr) {
			case 0x04:
				dev->pci_to_isa_regs[0x04] = (val & 8) | 7;
				break;
			case 0x07:
				dev->pci_to_isa_regs[0x07] &= ~(val & 0xb0);
				break;

			case 0x47:
				if ((val & 0x81) == 0x81)
					resetx86();
				pic_set_shadow(!!(val & 0x10));
				pci_elcr_set_enabled(!!(val & 0x20));
				dev->pci_to_isa_regs[0x47] = val & 0xfe;
				break;
			case 0x48:
				dev->pci_to_isa_regs[0x48] = val;
				nvr_update_io_mapping(dev);
				break;

			case 0x54:
				pci_set_irq_level(PCI_INTA, !(val & 8));
				pci_set_irq_level(PCI_INTB, !(val & 4));
				pci_set_irq_level(PCI_INTC, !(val & 2));
				pci_set_irq_level(PCI_INTD, !(val & 1));
				break;
			case 0x55:
				pci_set_irq_routing(PCI_INTD, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
				pci_set_mirq_routing(PCI_MIRQ0, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
				dev->pci_to_isa_regs[0x55] = val;
		                break;
	                case 0x56:
				pci_set_irq_routing(PCI_INTA, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
				pci_set_irq_routing(PCI_INTB, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
				dev->pci_to_isa_regs[0x56] = val;
				break;
			case 0x57:
				pci_set_irq_routing(PCI_INTC, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
				pci_set_mirq_routing(PCI_MIRQ1, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
				dev->pci_to_isa_regs[0x57] = val;
				break;
			case 0x58:
				pci_set_mirq_routing(PCI_MIRQ2, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
				dev->pci_to_isa_regs[0x58] = val;
				break;
			case 0x5b:
				dev->pci_to_isa_regs[0x5b] = val;
				nvr_update_io_mapping(dev);
				break;

			case 0x60: case 0x62: case 0x64: case 0x66:
			case 0x6a: case 0x6c: case 0x6e:
				c = (addr & 0x0e) >> 1;
				dma[c].ab = (dma[c].ab & 0xffffff0f) | (val & 0xf0);
				dma[c].ac = (dma[c].ac & 0xffffff0f) | (val & 0xf0);
				if (val & 0x08)
					dma_e |= (1 << c);
				else
					dma_e &= ~(1 << c);
				break;
			case 0x61: case 0x63: case 0x65: case 0x67:
			case 0x6b: case 0x6d: case 0x6f:
				c = (addr & 0x0e) >> 1;
				dma[c].ab = (dma[c].ab & 0xffff00ff) | (val << 8);
				dma[c].ac = (dma[c].ac & 0xffff00ff) | (val << 8);
				break;

			case 0x70: case 0x71: case 0x72: case 0x73:
				dev->pci_to_isa_regs[(addr - 0x44)] = val;
				break;
		}
		break;

        //IDE Controller
        case 1:
		//Read-only addresses
		if ((addr < 4) || (addr == 5) || (addr == 8) || ((addr >= 0xa) && (addr < 0x0d)) ||
		    ((addr >= 0x0e) && (addr < 0x10)) || ((addr >= 0x12) && (addr < 0x13)) ||
		    ((addr >= 0x16) && (addr < 0x17)) || ((addr >= 0x1a) && (addr < 0x1b)) ||
		    ((addr >= 0x1e) && (addr < 0x1f)) || ((addr >= 0x22) && (addr < 0x3c)) ||
		     ((addr >= 0x3e) && (addr < 0x40)) || ((addr >= 0x54) && (addr < 0x60)) ||
		     ((addr >= 0x52) && (addr < 0x68)) || (addr >= 0x62))
			return;

		switch (addr) {
			case 0x04:
				dev->ide_regs[0x04] = val & 0x85;
				ide_handlers(dev);
				bus_master_handlers(dev);
				break;
			case 0x07:
				dev->ide_regs[0x07] &= ~(val & 0xf1);
				break;

			case 0x09:
				dev->ide_regs[0x09] = (val & 0x05) | 0x8a;
				ide_handlers(dev);
				ide_irqs(dev);
				break;

			case 0x10:
				dev->ide_regs[0x10] = (val & 0xf8) | 1;
				ide_handlers(dev);
				break;
			case 0x11:
				dev->ide_regs[0x11] = val;
				ide_handlers(dev);
				break;

			case 0x14:
				dev->ide_regs[0x14] = (val & 0xfc) | 1;
				ide_handlers(dev);
				break;
			case 0x15:
				dev->ide_regs[0x15] = val;
				ide_handlers(dev);
				break;

			case 0x18:
				dev->ide_regs[0x18] = (val & 0xf8) | 1;
				ide_handlers(dev);
				break;
			case 0x19:
				dev->ide_regs[0x19] = val;
				ide_handlers(dev);
				break;

			case 0x1c:
				dev->ide_regs[0x1c] = (val & 0xfc) | 1;
				ide_handlers(dev);
				break;
			case 0x1d:
				dev->ide_regs[0x1d] = val;
				ide_handlers(dev);
				break;

			case 0x20:
				dev->ide_regs[0x20] = (val & 0xf0) | 1;
				bus_master_handlers(dev);
				break;
			case 0x21:
				dev->ide_regs[0x21] = val;
				bus_master_handlers(dev);
				break;

			case 0x3d:
				dev->ide_regs[0x3d] = val & 0x01;
				ide_irqs(dev);
				break;

			case 0x40:
				dev->ide_regs[0x40] = val;
				ide_handlers(dev);
				break;

			default:
				dev->ide_regs[addr] = val;
				break;
		}
		break;

    //USB Controller
	case 2:
		//Read-only addresses
		if ((addr < 4) || (addr == 5) || (addr == 6) || ((addr >= 8) && (addr < 0xd)) ||
		    ((addr >= 0xe) && (addr < 0x20)) || ((addr >= 0x22) && (addr < 0x3c)) ||
		    ((addr >= 0x3e) && (addr < 0x40)) || ((addr >= 0x42) && (addr < 0x44)) ||
		    ((addr >= 0x46) && (addr < 0xc0)) || (addr >= 0xc2))
			return;

		switch (addr) {
			case 0x04:
				dev->usb_regs[0x04] = val & 0x97;
				usb_update_io_mapping(dev);
				break;
			case 0x07:
				dev->usb_regs[0x07] &= ~(val & 0x78);
				break;

			case 0x20:
				dev->usb_regs[0x20] = (val & ~0x1f) | 1;
				usb_update_io_mapping(dev);
				break;
			case 0x21:
				dev->usb_regs[0x21] = val;
				usb_update_io_mapping(dev);
				break;

			default:
				dev->usb_regs[addr] = val;
				break;
		}
		break;

    //Power Management
	case 3:
		//Read-Only Addresses
		if ((addr < 0xd) || ((addr >= 0xe) && (addr < 0x40)) || (addr == 0x43) || (addr == 0x48) ||
		    ((addr >= 0x4a) && (addr < 0x50)) || (addr >= 0x54))
			return;

		switch (addr) {
			case 0x41: case 0x49:
				dev->power_regs[addr] = val;
				power_update_io_mapping(dev);
                smbus_update_io_mapping(dev);
				break;

	    case 0x90:
		    dev->power_regs[0x90] = (val & 0xf0) | 1;
		    smbus_update_io_mapping(dev);
		    break;
	    case 0x91:
		    dev->power_regs[0x91] = val;
		    smbus_update_io_mapping(dev);
		    break;

			default:
				dev->power_regs[addr] = val;
				break;
		}
    }
}

static void *
via_vt82c596b_init(const device_t *info)
{
    via_vt82c596b_t *dev = (via_vt82c596b_t *) malloc(sizeof(via_vt82c596b_t));
    memset(dev, 0, sizeof(via_vt82c596b_t));

    dev->slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, via_vt82c596b_read, via_vt82c596b_write, dev);

    dev->bm[0] = device_add_inst(&sff8038i_device, 1);
    sff_set_slot(dev->bm[0], dev->slot);
    sff_set_irq_mode(dev->bm[0], 0, 0);
    sff_set_irq_mode(dev->bm[0], 1, 0);
    sff_set_irq_pin(dev->bm[0], PCI_INTA);

    dev->bm[1] = device_add_inst(&sff8038i_device, 2);
    sff_set_slot(dev->bm[1], dev->slot);
    sff_set_irq_mode(dev->bm[1], 0, 0);
    sff_set_irq_mode(dev->bm[1], 1, 0);
    sff_set_irq_pin(dev->bm[1], PCI_INTA);

    dev->nvr = device_add(&via_nvr_device);

    via_vt82c596b_reset(dev);

    dev->smbus = device_add(&piix4_smbus_device);

    device_add(&port_92_pci_device);

    dma_alias_set();

    pci_enable_mirq(0);
    pci_enable_mirq(1);
    pci_enable_mirq(2);

    return dev;
}

static void
via_vt82c596b_close(void *p)
{
    via_vt82c596b_t *via_vt82c596b = (via_vt82c596b_t *)p;

    free(via_vt82c596b);
}

const device_t via_vt82c596b_device =
{
    "VIA VT82C596B",
    DEVICE_PCI,
    0,
    via_vt82c596b_init, 
    via_vt82c596b_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
#endif