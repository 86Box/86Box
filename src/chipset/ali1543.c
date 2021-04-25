/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M1543 Desktop South Bridge.
 *
 *
 *
 * Authors:	Tiseno100,
 *
 *		Copyright 2021 Tiseno100.
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
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>

#include <86box/apm.h>
#include <86box/dma.h>
#include <86box/ddma.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/lpt.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/serial.h>
#include <86box/smbus_piix4.h>
#include <86box/usb.h>

#include <86box/acpi.h>

#include <86box/chipset.h>

#ifdef ENABLE_ALI1543_LOG
int ali1543_do_log = ENABLE_ALI1543_LOG;
static void
ali1543_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1543_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define ali1543_log(fmt, ...)
#endif

typedef struct ali1543_t
{
    uint8_t pci_conf[256], pmu_conf[256], usb_conf[256], ide_conf[256],
        sio_regs[256], device_regs[8][256], sio_index, in_configuration_mode,
        pci_slot, ide_slot, usb_slot, pmu_slot;

    apm_t *apm;
    acpi_t *acpi;
    ddma_t *ddma;
    fdc_t *fdc_controller;
    nvr_t *nvr;
    port_92_t *port_92;
    serial_t *uart[2];
    sff8038i_t *ide_controller[2];
    smbus_piix4_t *smbus;
    usb_t *usb;

} ali1543_t;

/*

    Notes:
    - Power Managment isn't functioning properly
    - IDE isn't functioning properly
    - 1543C differences have to be examined
    - Some Chipset functionality might be missing
    - Device numbers and types might be incorrect
    - Code quality is abysmal and needs lot's of cleanup.

*/

int ali1533_irq_routing[15] = {9, 3, 0x0a, 4, 5, 7, 6, 1, 0x0b, 0, 0x0c, 0, 0x0e, 0, 0x0f};

void ali1533_ddma_handler(ali1543_t *dev)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        if (i != 4)
            ddma_update_io_mapping(dev->ddma, i & 7, dev->pci_conf[0x73] & 0xf, dev->pci_conf[0x73] & 0xf0, dev->pci_conf[0x45] & 2);
    }
}

void ali5229_ide_handler(ali1543_t *dev);

static void
ali1533_write(int func, int addr, uint8_t val, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;
    ali1543_log("M1533: dev->pci_conf[%02x] = %02x\n", addr, val);
    switch (addr)
    {
    case 0x04:
        if (dev->pci_conf[0x5f] & 8)
            dev->pci_conf[addr] = val;
        break;

    case 0x2c: /* Subsystem Vendor ID */
    case 0x2d:
    case 0x2e:
    case 0x2f:
        if (dev->pci_conf[0x74] & 0x40)
            dev->pci_conf[addr] = val;
        break;

    case 0x40:
        dev->pci_conf[addr] = val & 0x7f;
        break;

    case 0x42: /* ISA Bus Speed */
        dev->pci_conf[addr] = val & 0xcf;
        switch(val & 7)
        {
            case 0:
            cpu_set_isa_speed(7159091);
            break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            cpu_set_isa_pci_div(val & 7);
            break;
        }

        break;

    case 0x43:
        dev->pci_conf[addr] = val;
        if (val & 0x80)
            port_92_add(dev->port_92);
        else
            port_92_remove(dev->port_92);
        break;

    case 0x44: /* Set IRQ Line for Primary IDE if it's on native mode */
        dev->pci_conf[addr] = 0xdf;
        if (dev->ide_conf[0x09] & 1)
            sff_set_irq_line(dev->ide_controller[0], ((val & 0x0f) == 0) ? ali1533_irq_routing[(val & 0x0f) - 1] : PCI_IRQ_DISABLED);
        break;

    case 0x45: /* DDMA Enable */
        dev->pci_conf[addr] = 0xcf;
        ali1533_ddma_handler(dev);
        break;

    case 0x48: /* PCI IRQ Routing */
    case 0x49:
        dev->pci_conf[addr] = val;
        pci_set_irq_routing(((addr & 1) * 2) + 2, (((val >> 4) & 0x0f) == 0) ? ali1533_irq_routing[((val >> 4) & 0x0f) - 1] : PCI_IRQ_DISABLED);
        pci_set_irq_routing(((addr & 1) * 2) + 1, ((val & 0x0f) == 0) ? ali1533_irq_routing[(val & 0x0f) - 1] : PCI_IRQ_DISABLED);
        break;

    case 0x53: /* USB Enable */
        dev->pci_conf[addr] = val & 0xe7;
        ohci_update_mem_mapping(dev->usb, 0x11, 0x12, 0x13, (dev->usb_conf[0x04] & 1) && (!(dev->pci_conf[0x53] & 0x40)));
        break;

    case 0x54: /* USB Control ? */
        dev->pci_conf[addr] = val & 0xdf;
        break;

    case 0x57:
        dev->pci_conf[addr] = val & 0xc7;
        break;

    case 0x58: /* IDE Enable */
        dev->pci_conf[addr] = val & 0x7f;
        ali5229_ide_handler(dev);
        break;

    case 0x59:
    case 0x5a:
        dev->pci_conf[addr] = val & 0x0e;
        break;

    case 0x5b:
        dev->pci_conf[addr] = val & 0x02;
        break;

    case 0x5c:
        dev->pci_conf[addr] = val & 0x7f;
        break;

    case 0x5d:
        dev->pci_conf[addr] = val & 0x02;
        break;

    case 0x5e:
        dev->pci_conf[addr] = val & 0xe0;
        break;

    case 0x5f:
        dev->pci_conf[addr] = val;
        acpi_update_io_mapping(dev->acpi, (dev->pmu_conf[0x11] << 8) | (dev->pmu_conf[0x10] & 0xc0), (dev->pmu_conf[0x04] & 1) && (!(dev->pci_conf[0x5f] & 4)));
        smbus_piix4_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xe0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1) && (!(dev->pci_conf[0x5f] & 4)));
        break;

    case 0x6d:
        dev->pci_conf[addr] = val & 0xbf;
        break;

    case 0x71:
    case 0x72:
        dev->pci_conf[addr] = val & 0xef;
        break;

    case 0x73: /* DDMA Base Address */
        dev->pci_conf[addr] = val;
        ali1533_ddma_handler(dev);
        break;

    case 0x74: /* USB IRQ Routing */
        dev->pci_conf[addr] = val & 0xdf;
        break;

    case 0x75: /* Set IRQ Line for Secondary IDE if it's on native mode */
        dev->pci_conf[addr] = val & 0x1f;
        if (dev->ide_conf[0x09] & 8)
            sff_set_irq_line(dev->ide_controller[1], ((val & 0x0f) == 0) ? ali1533_irq_routing[(val & 0x0f) - 1] : PCI_IRQ_DISABLED);
        break;

    case 0x76: /* PMU IRQ Routing */
        dev->pci_conf[addr] = val & 0x1f;
        acpi_set_irq_line(dev->acpi, val & 0x0f);
        break;

    case 0x77: /* SMBus IRQ Routing */
        dev->pci_conf[addr] = val & 0x1f;
        break;

    default:
        dev->pci_conf[addr] = val;
        break;
    }
}

static uint8_t
ali1533_read(int func, int addr, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;

    if (((dev->pci_conf[0x42] & 0x80) && (addr >= 0x40)) || ((dev->pci_conf[0x5f] & 8) && (addr == 4)))
        return 0;
    else
        return dev->pci_conf[addr];
}

void ali5229_ide_handler(ali1543_t *dev)
{
    uint16_t native_base_pri_addr = (dev->ide_conf[0x11] | dev->ide_conf[0x10] << 8);
    uint16_t native_side_pri_addr = (dev->ide_conf[0x15] | dev->ide_conf[0x14] << 8);
    uint16_t native_base_sec_addr = (dev->ide_conf[0x19] | dev->ide_conf[0x18] << 8);
    uint16_t native_side_sec_addr = (dev->ide_conf[0x1c] | dev->ide_conf[0x1b] << 8);

    uint16_t comp_base_pri_addr = 0x01f0;
    uint16_t comp_side_pri_addr = 0x03f6;
    uint16_t comp_base_sec_addr = 0x0170;
    uint16_t comp_side_sec_addr = 0x0376;

    uint16_t current_pri_base, current_pri_side, current_sec_base, current_sec_side;

    /* Primary Channel Programming */
    if (!(dev->ide_conf[0x52] & 0x10))
    {
        current_pri_base = (!(dev->ide_conf[0x09] & 1)) ? comp_base_pri_addr : native_base_pri_addr;
        current_pri_side = (!(dev->ide_conf[0x09] & 1)) ? comp_side_pri_addr : native_side_pri_addr;
    }
    else
    {
        current_pri_base = (!(dev->ide_conf[0x09] & 1)) ? comp_base_sec_addr : native_base_sec_addr;
        current_pri_side = (!(dev->ide_conf[0x09] & 1)) ? comp_side_sec_addr : native_side_sec_addr;
    }

    /* Secondary Channel Programming */
    if (!(dev->ide_conf[0x52] & 0x10))
    {
        current_sec_base = (!(dev->ide_conf[0x09] & 4)) ? comp_base_sec_addr : native_base_sec_addr;
        current_sec_side = (!(dev->ide_conf[0x09] & 4)) ? comp_side_sec_addr : native_side_sec_addr;
    }
    else
    {
        current_sec_base = (!(dev->ide_conf[0x09] & 4)) ? comp_base_pri_addr : native_base_pri_addr;
        current_sec_side = (!(dev->ide_conf[0x09] & 4)) ? comp_side_pri_addr : native_side_pri_addr;
    }

    /* Both channels use one port */
    if (dev->ide_conf[0x52] & 0x40)
    {
        current_pri_base = current_sec_base;
        current_pri_side = current_sec_side;
    }

    if (dev->ide_conf[0x52] & 0x20)
    {
        current_sec_base = current_pri_base;
        current_sec_side = current_pri_side;
    }

    ide_pri_disable();
    ide_sec_disable();

    if (dev->pci_conf[0x58] & 0x40)
    {
        sff_set_irq_pin(dev->ide_controller[0], dev->ide_conf[0x3d] & 4);
        sff_set_irq_pin(dev->ide_controller[1], dev->ide_conf[0x3d] & 4);

        /* Primary Channel Setup */
        if (dev->ide_conf[0x09] & 0x10)
        {
            ide_pri_enable();
            if (!(dev->ide_conf[0x09] & 1))
                sff_set_irq_line(dev->ide_controller[0], (dev->ide_conf[0x3c] != 0) ? ali1533_irq_routing[(dev->ide_conf[0x3c] & 0x0f) - 1] : PCI_IRQ_DISABLED);

            ide_set_base(0, current_pri_base);
            ide_set_side(0, current_pri_side);

            sff_bus_master_handler(dev->ide_controller[0], dev->ide_conf[0x09] & 0x80, (dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8));
            ali1543_log("M5229 PRI: BASE %04x SIDE %04x\n", current_pri_base, current_pri_side);
        }

        /* Secondary Channel Setup */
        if (dev->ide_conf[0x09] & 8)
        {
            ide_sec_enable();
            if (!(dev->ide_conf[0x09] & 4))
                sff_set_irq_line(dev->ide_controller[1], (dev->ide_conf[0x3c] != 0) ? ali1533_irq_routing[(dev->ide_conf[0x3c] & 0x0f) - 1] : PCI_IRQ_DISABLED);

            ide_set_base(1, current_sec_base);
            ide_set_side(1, current_sec_side);

            sff_bus_master_handler(dev->ide_controller[1], dev->ide_conf[0x09] & 0x80, ((dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8)) + 8);
            ali1543_log("M5229 SEC: BASE %04x SIDE %04x\n", current_sec_base, current_sec_side);
        }
    }
}

static void
ali5229_write(int func, int addr, uint8_t val, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;
    ali1543_log("M5229: dev->ide_conf[%02x] = %02x\n", addr, val);

    switch (addr)
    {
    case 0x09: /* Control */
        if (dev->ide_conf[0x4d] & 0x80)
            dev->ide_conf[addr] |= val & 0x8f;
        else
            dev->ide_conf[addr] = val;
        ali5229_ide_handler(dev);
        break;

    case 0x10: /* Primary Base Address */
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:

    case 0x18: /* Secondary Base Address */
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1c:

    case 0x20: /* Bus Mastering Base Address */
    case 0x21:
    case 0x22:
    case 0x23:
        dev->ide_conf[addr] = val;
        ali5229_ide_handler(dev);
        break;

        /* The machines don't touch anything beyond that point so we avoid any programming */

    case 0x2c: /* Subsystem Vendor */
    case 0x2d:
    case 0x2e:
    case 0x2f:
        if (dev->ide_conf[0x53] & 0x80)
            dev->ide_conf[addr] = val;
        break;

    case 0x4d:
        dev->ide_conf[addr] = val & 0x80;
        break;

    case 0x4f:
        dev->ide_conf[addr] = val & 0x3f;
        break;

    case 0x50: /* Configuration */
        dev->ide_conf[addr] = val & 0x2b;
        break;

    case 0x51:
        dev->ide_conf[addr] = val & 0xf7;
        break;

    case 0x53: /* Subsystem Vendor ID */
        dev->ide_conf[addr] = val & 0x8b;
        break;

    case 0x58:
        dev->ide_conf[addr] = val & 3;
        break;

    case 0x59:
    case 0x5a:
    case 0x5b:
        dev->ide_conf[addr] = val & 0x7f;
        break;

    case 0x5c:
        dev->ide_conf[addr] = val & 3;
        break;

    case 0x5d:
    case 0x5e:
    case 0x5f:
        dev->ide_conf[addr] = val & 0x7f;
        break;

    default:
        dev->ide_conf[addr] = val;
        break;
    }
}

static uint8_t
ali5229_read(int func, int addr, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;
    return dev->ide_conf[addr];
}

static void
ali5237_write(int func, int addr, uint8_t val, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;
    ali1543_log("M5237: dev->usb_conf[%02x] = %02x\n", addr, val);
    switch (addr)
    {
    case 0x04: /* USB Enable */
        dev->usb_conf[addr] = val;
        ohci_update_mem_mapping(dev->usb, 0x10, 0x11, 0x12, (dev->usb_conf[0x04] & 1) && (!(dev->pci_conf[0x53] & 0x40)));
        break;

    case 0x05:
        dev->usb_conf[addr] = 0x03;
        break;

    case 0x06:
        dev->usb_conf[addr] = 0xc0;
        break;

    case 0x11:
    case 0x12:
    case 0x13: /* USB Base I/O */
        dev->usb_conf[addr] = val;
        ohci_update_mem_mapping(dev->usb, 0x11, 0x12, 0x13, (dev->usb_conf[0x04] & 1) && (!(dev->pci_conf[0x53] & 0x40)));
        break;

    case 0x42:
        dev->usb_conf[addr] = 0x10;
        break;

    default:
        dev->usb_conf[addr] = val;
        break;
    }
}

static uint8_t
ali5237_read(int func, int addr, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;
    return dev->usb_conf[addr];
}

static void
ali7101_write(int func, int addr, uint8_t val, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;
    ali1543_log("M7101: dev->pmu_conf[%02x] = %02x\n", addr, val);

    switch (addr)
    {
    case 0x04: /* Enable PMU */
        dev->pmu_conf[addr] = val & 0x1f;
        acpi_update_io_mapping(dev->acpi, (dev->pmu_conf[0x11] << 8) | (dev->pmu_conf[0x10] & 0xc0), (dev->pmu_conf[0x04] & 1) && (!(dev->pci_conf[0x5f] & 4)));
        smbus_piix4_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xe0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1) && (!(dev->pci_conf[0x5f] & 4)));
        break;

    case 0x07:
        dev->pmu_conf[addr] = val & 0xfe;
        break;

    case 0x10: /* PMU Base I/O */
    case 0x11:
        if (addr == 0x10)
            dev->pmu_conf[addr] = (val & 0xe0) | 1;
        else if (addr == 0x11)
            dev->pmu_conf[addr] = val;

        acpi_update_io_mapping(dev->acpi, (dev->pmu_conf[0x11] << 8) | (dev->pmu_conf[0x10] & 0xc0), (dev->pmu_conf[0x04] & 1) && (!(dev->pci_conf[0x5f] & 4)));
        break;

    case 0x14: /* SMBus Base I/O */
    case 0x15:
        dev->pmu_conf[addr] = val;
        smbus_piix4_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xe0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1) && (!(dev->pci_conf[0x5f] & 4)));
        break;

    case 0x2c: /* Subsystem Vendor ID */
    case 0x2d:
    case 0x2e:
    case 0x2f:
        if (dev->pmu_conf[0xd8] & 0x10)
            dev->pmu_conf[addr] = val;

    case 0x40:
        dev->pmu_conf[addr] = val & 0x1f;
        break;

    case 0x41:
        dev->pmu_conf[addr] = val & 0x10;
        break;

    case 0x45:
        dev->pmu_conf[addr] = val & 0x9f;
        break;

    case 0x46:
        dev->pmu_conf[addr] = val & 0x18;
        break;

    case 0x48:
        dev->pmu_conf[addr] = val & 0x9f;
        break;

    case 0x49:
        dev->pmu_conf[addr] = val & 0x38;
        break;

    case 0x4c:
        dev->pmu_conf[addr] = val & 5;
        break;

    case 0x4d:
        dev->pmu_conf[addr] = val & 1;
        break;

    case 0x4e:
        dev->pmu_conf[addr] = val & 5;
        break;

    case 0x4f:
        dev->pmu_conf[addr] = val & 1;
        break;

    case 0x55: /* APM Timer */
        dev->pmu_conf[addr] = val & 0x7f;
        break;

    case 0x59:
        dev->pmu_conf[addr] = val & 0x1f;
        break;

    case 0x5b: /* ACPI/SMB Base I/O Control */
        dev->pmu_conf[addr] = val & 0x7f;
        break;

    case 0x61:
        dev->pmu_conf[addr] = val & 0x13;
        break;

    case 0x62:
        dev->pmu_conf[addr] = val & 0xf1;
        break;

    case 0x63:
        dev->pmu_conf[addr] = val & 3;
        break;

    case 0x65:
        dev->pmu_conf[addr] = val & 0x1f;
        break;

    case 0x68:
        dev->pmu_conf[addr] = val & 7;
        break;

    case 0x6e:
        dev->pmu_conf[addr] = val & 0xef;
        break;

    case 0x6f:
        dev->pmu_conf[addr] = val & 7;
        break;

    /* Continue Further Later */
    case 0xc0: /* GPO Registers */
    case 0xc1:
    case 0xc2:
    case 0xc3:
        dev->pmu_conf[addr] = val;
        acpi_init_gporeg(dev->acpi, dev->pmu_conf[0xc0], dev->pmu_conf[0xc1], dev->pmu_conf[0xc2], dev->pmu_conf[0xc3]);
        break;

    case 0xe0:
        dev->pmu_conf[addr] = val;
        smbus_piix4_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xe0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1) && (!(dev->pci_conf[0x5f] & 4)));
        break;

    default:
        dev->pmu_conf[addr] = val;
        break;
    }
}

static uint8_t
ali7101_read(int func, int addr, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;
    return dev->pmu_conf[addr];
}

void ali1533_sio_fdc_handler(ali1543_t *dev)
{
    fdc_remove(dev->fdc_controller);
    if (dev->device_regs[0][0x30] & 1)
    {
        fdc_set_base(dev->fdc_controller, dev->device_regs[0][0x61] | (dev->device_regs[0][0x60] << 8));
        fdc_set_irq(dev->fdc_controller, dev->device_regs[0][0x70] & 0xf);
        fdc_set_dma_ch(dev->fdc_controller, dev->device_regs[0][0x74] & 0x07);
        ali1543_log("M1543-SIO FDC: ADDR %04x IRQ %02x DMA %02x\n", dev->device_regs[0][0x61] | (dev->device_regs[0][0x60] << 8), dev->device_regs[0][0x70] & 0xf, dev->device_regs[0][0x74] & 0x07);
    }
}

void ali1533_sio_uart_handler(int num, ali1543_t *dev)
{
    serial_remove(dev->uart[num]);
    if (dev->device_regs[num + 4][0x30] & 1)
    {
        serial_setup(dev->uart[num], dev->device_regs[num + 4][0x61] | (dev->device_regs[num + 4][0x60] << 8), dev->device_regs[num + 4][0x70] & 0xf);
        ali1543_log("M1543-SIO UART%d: ADDR %04x IRQ %02x\n", num, dev->device_regs[num + 4][0x61] | (dev->device_regs[num + 4][0x60] << 8), dev->device_regs[num + 4][0x70] & 0xf);
    }
}

void ali1533_sio_lpt_handler(ali1543_t *dev)
{
    lpt1_remove();
    if (dev->device_regs[3][0x30] & 1)
    {
        lpt1_init(dev->device_regs[3][0x61] | (dev->device_regs[3][0x60] << 8));
        lpt1_irq(dev->device_regs[3][0x70] & 0xf);
        ali1543_log("M1543-SIO LPT: ADDR %04x IRQ %02x\n", dev->device_regs[3][0x61] | (dev->device_regs[3][0x60] << 8), dev->device_regs[3][0x70] & 0xf);
    }
}

void ali1533_sio_ldn(uint16_t ldn, ali1543_t *dev)
{
    /* We don't include all LDN's */
    switch (ldn)
    {
    case 0: /* FDC */
        ali1533_sio_fdc_handler(dev);
        break;
    case 3: /* LPT */
        ali1533_sio_lpt_handler(dev);
        break;
    case 4: /* UART */
    case 5:
        ali1533_sio_uart_handler(ldn - 4, dev);
        break;
    }
}

static void
ali1533_sio_write(uint16_t addr, uint8_t val, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;

    switch (addr)
    {
    case 0x3f0:
        dev->sio_index = val;
        if (dev->sio_index == 0x51)
        {
            dev->in_configuration_mode = 1;
        }
        else if (dev->sio_index == 0xbb)
            dev->in_configuration_mode = 0;
        break;

    case 0x3f1:
        if (dev->in_configuration_mode)
        {
            switch (dev->sio_index)
            {
            case 0x07:
                dev->sio_regs[dev->sio_index] = val & 0x7;
                break;

            case 0x22:
                dev->sio_regs[dev->sio_index] = val & 0x39;
                break;

            case 0x23:
                dev->sio_regs[dev->sio_index] = val & 0x38;
                break;

            default:
                if ((dev->sio_index < 0x30) || (dev->sio_index == 0x51) || (dev->sio_index == 0xbb))
                    dev->sio_regs[dev->sio_index] = val;
                else if (dev->sio_regs[0x07] <= 7)
                    dev->device_regs[dev->sio_regs[0x07]][dev->sio_index] = val;
                break;
            }
        }
        break;
    }

    if ((!dev->in_configuration_mode) && (dev->sio_regs[0x07] <= 7) && (addr == 0x03f0))
    {
        ali1533_sio_ldn(dev->sio_regs[0x07], dev);
    }
}

static uint8_t
ali1533_sio_read(uint16_t addr, void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;
    if (dev->sio_index >= 0x30)
        return dev->device_regs[dev->sio_regs[0x07]][dev->sio_index];
    else
        return dev->sio_regs[dev->sio_index];
}

static void
ali1543_reset(void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;

    /* M1533 */
    dev->pci_conf[0x00] = 0xb9;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x33;
    dev->pci_conf[0x03] = 0x15;
    dev->pci_conf[0x04] = 0x0f;
    dev->pci_conf[0x08] = 0xb4;
    dev->pci_conf[0x0a] = 0x01;
    dev->pci_conf[0x0b] = 0x06;

    ali1533_write(0, 0x48, 0x00, dev); // Disables all IRQ's
    ali1533_write(0, 0x44, 0x00, dev);
    ali1533_write(0, 0x74, 0x00, dev);
    ali1533_write(0, 0x75, 0x00, dev);
    ali1533_write(0, 0x76, 0x00, dev);

    /* M5229 */
    dev->ide_conf[0x00] = 0xb9;
    dev->ide_conf[0x01] = 0x10;
    dev->ide_conf[0x02] = 0x29;
    dev->ide_conf[0x03] = 0x52;
    dev->ide_conf[0x06] = 0x80;
    dev->ide_conf[0x07] = 0x02;
    dev->ide_conf[0x08] = 0x20;
    dev->ide_conf[0x09] = 0xfa;
    dev->ide_conf[0x0a] = 0x01;
    dev->ide_conf[0x0b] = 0x01;
    dev->ide_conf[0x10] = 0xf1;
    dev->ide_conf[0x11] = 0x01;
    dev->ide_conf[0x14] = 0xf5;
    dev->ide_conf[0x15] = 0x03;
    dev->ide_conf[0x18] = 0x71;
    dev->ide_conf[0x19] = 0x01;
    dev->ide_conf[0x1a] = 0x75;
    dev->ide_conf[0x1b] = 0x03;
    dev->ide_conf[0x20] = 0x01;
    dev->ide_conf[0x23] = 0xf0;
    dev->ide_conf[0x3d] = 0x01;
    dev->ide_conf[0x3c] = 0x02;
    dev->ide_conf[0x3d] = 0x03;
    dev->ide_conf[0x54] = 0x55;
    dev->ide_conf[0x55] = 0x55;
    dev->ide_conf[0x63] = 0x01;
    dev->ide_conf[0x64] = 0x02;
    dev->ide_conf[0x67] = 0x01;
    dev->ide_conf[0x78] = 0x21;

    sff_set_slot(dev->ide_controller[0], dev->ide_slot);
    sff_set_slot(dev->ide_controller[1], dev->ide_slot);
    sff_bus_master_reset(dev->ide_controller[0], (dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8));
    sff_bus_master_reset(dev->ide_controller[1], ((dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8)) + 8);
    ali5229_ide_handler(dev);

    /* M5237 */
    dev->usb_conf[0x00] = 0xb9;
    dev->usb_conf[0x01] = 0x10;
    dev->usb_conf[0x02] = 0x37;
    dev->usb_conf[0x03] = 0x52;
    dev->usb_conf[0x06] = 0x80;
    dev->usb_conf[0x07] = 0x02;
    dev->usb_conf[0x08] = 0x03;
    dev->usb_conf[0x09] = 0x10;
    dev->usb_conf[0x0a] = 0x03;
    dev->usb_conf[0x0b] = 0x0c;
    dev->usb_conf[0x3d] = 0x01;

    ali5237_write(0, 0x04, 0x00, dev);

    /* M7101 */
    dev->pmu_conf[0x00] = 0xb9;
    dev->pmu_conf[0x01] = 0x10;
    dev->pmu_conf[0x02] = 0x01;
    dev->pmu_conf[0x03] = 0x71;
    dev->pmu_conf[0x04] = 0x0f;
    dev->pmu_conf[0x05] = 0x00;
    dev->pmu_conf[0x0a] = 0x01;
    dev->pmu_conf[0x0b] = 0x06;

    acpi_set_slot(dev->acpi, dev->pmu_slot);
    acpi_set_nvr(dev->acpi, dev->nvr);

    ali7101_write(0, 0x04, 0x00, dev);
    ali7101_write(0, 0xc0, 0x00, dev);

    /* M1543 Super I/O */
    dev->device_regs[0][0x60] = 0x03;
    dev->device_regs[0][0x61] = 0xf0;
    dev->device_regs[0][0x70] = 0x06;
    dev->device_regs[0][0x74] = 0x02;
    dev->device_regs[0][0xf0] = 0x08;
    dev->device_regs[0][0xf2] = 0xff;

    dev->device_regs[3][0x60] = 0x03;
    dev->device_regs[3][0x61] = 0x78;
    dev->device_regs[3][0x70] = 0x05;
    dev->device_regs[3][0x74] = 0x04;
    dev->device_regs[3][0xf0] = 0x0c;
    dev->device_regs[3][0xf1] = 0x05;

    dev->device_regs[4][0x60] = 0x03;
    dev->device_regs[4][0x61] = 0xf8;
    dev->device_regs[4][0x70] = 0x04;
    dev->device_regs[4][0xf1] = 0x02;
    dev->device_regs[4][0xf2] = 0x0c;

    dev->device_regs[5][0x60] = 0x02;
    dev->device_regs[5][0x61] = 0xf8;
    dev->device_regs[5][0x70] = 0x03;
    dev->device_regs[5][0xf1] = 0x02;
    dev->device_regs[5][0xf2] = 0x0c;

    dev->device_regs[7][0x70] = 0x01;

    ali1533_sio_fdc_handler(dev);
    ali1533_sio_uart_handler(0, dev);
    ali1533_sio_uart_handler(1, dev);
    ali1533_sio_lpt_handler(dev);
}

static void
ali1543_close(void *priv)
{
    ali1543_t *dev = (ali1543_t *)priv;

    free(dev);
}

static void *
ali1543_init(const device_t *info)
{
    ali1543_t *dev = (ali1543_t *)malloc(sizeof(ali1543_t));
    memset(dev, 0, sizeof(ali1543_t));

    /* Device 02: M1533 Southbridge */
    dev->pci_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, ali1533_read, ali1533_write, dev);

    /* Device 0B: M5229 IDE Controller*/
    dev->ide_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, ali5229_read, ali5229_write, dev);

    /* Device 0C: M7101 Power Managment Controller */
    dev->pmu_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, ali7101_read, ali7101_write, dev);

    /* Device 0D: M5237 USB */
    dev->usb_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, ali5237_read, ali5237_write, dev);

    /* Ports 3F0-1h: M1543 Super I/O */
    io_sethandler(0x03f0, 0x0002, ali1533_sio_read, NULL, NULL, ali1533_sio_write, NULL, NULL, dev);

    /* ACPI */
    dev->acpi = device_add(&acpi_ali_device);
    dev->nvr = device_add(&at_nvr_device); // Generic NVR

    /* APM */
    dev->apm = device_add(&apm_pci_device);

    /* DMA */
    dma_alias_set();
    dma_high_page_init();

    /* DDMA */
    dev->ddma = device_add(&ddma_device);

    /* Floppy Disk Controller */
    dev->fdc_controller = device_add(&fdc_at_device);

    /* IDE Controllers */
    dev->ide_controller[0] = device_add_inst(&sff8038i_device, 1);
    dev->ide_controller[1] = device_add_inst(&sff8038i_device, 2);

    /* Port 92h */
    dev->port_92 = device_add(&port_92_pci_device);

    /* Serial NS16500 */
    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    /* Standard SMBus */
    dev->smbus = device_add(&piix4_smbus_device);

    /* Super I/O Configuration Mechanism */
    dev->in_configuration_mode = 1;

    /* USB */
    dev->usb = device_add(&usb_device);

    ali1543_reset(dev);

    return dev;
}

const device_t ali1543_device = {
    "ALi M1543 Desktop South Bridge",
    DEVICE_PCI,
    0,
    ali1543_init,
    ali1543_close,
    ali1543_reset,
    {NULL},
    NULL,
    NULL,
    NULL};
