/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M1543 Desktop South Bridge.
 *
 *
 *
 * Authors: Tiseno100,
 *
 *          Copyright 2021 Tiseno100.
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
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/port_92.h>
#include <86box/sio.h>
#include <86box/smbus.h>
#include <86box/usb.h>

#include <86box/acpi.h>

#include <86box/chipset.h>

typedef struct ali1543_t {
    uint8_t pci_conf[256], pmu_conf[256], usb_conf[256], ide_conf[256],
        pci_slot, ide_slot, usb_slot, pmu_slot, usb_dev_enable, ide_dev_enable,
        pmu_dev_enable, type;
    int offset;

    apm_t           *apm;
    acpi_t          *acpi;
    ddma_t          *ddma;
    nvr_t           *nvr;
    port_92_t       *port_92;
    sff8038i_t      *ide_controller[2];
    smbus_ali7101_t *smbus;
    usb_t           *usb;
    usb_params_t     usb_params;

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

int ali1533_irq_routing[16] = { PCI_IRQ_DISABLED, 9, 3, 10, 4, 5, 7, 6,
                                1, 11, PCI_IRQ_DISABLED, 12, PCI_IRQ_DISABLED, 14, PCI_IRQ_DISABLED, 15 };

#ifdef ENABLE_ALI1543_LOG
int ali1543_do_log = ENABLE_ALI1543_LOG;

static void
ali1543_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1543_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ali1543_log(fmt, ...)
#endif

static void
ali1533_ddma_handler(ali1543_t *dev)
{
    /* TODO: Find any documentation that actually explains the ALi southbridge DDMA mapping. */
}

static void ali5229_ide_handler(ali1543_t *dev);
static void ali5229_ide_irq_handler(ali1543_t *dev);

static void ali5229_write(int func, int addr, uint8_t val, void *priv);

static void    ali7101_write(int func, int addr, uint8_t val, void *priv);
static uint8_t ali7101_read(int func, int addr, void *priv);

static void
ali1533_write(int func, int addr, uint8_t val, void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;
    int        irq;
    ali1543_log("M1533: dev->pci_conf[%02x] = %02x\n", addr, val);

    if (func > 0)
        return;

    switch (addr) {
        case 0x04: /* Command Register */
            if (dev->type == 1) {
                if (dev->pci_conf[0x5f] & 0x08)
                    dev->pci_conf[0x04] = val & 0x0f;
                else
                    dev->pci_conf[0x04] = val;
            } else {
                if (!(dev->pci_conf[0x5f] & 0x08))
                    dev->pci_conf[0x04] = val;
            }
            break;
        case 0x05: /* Command Register */
            if (!(dev->pci_conf[0x5f] & 0x08))
                dev->pci_conf[0x04] = val & 0x03;
            break;

        case 0x07: /* Status Byte */
            dev->pci_conf[addr] &= ~(val & 0x30);
            break;

        case 0x2c: /* Subsystem Vendor ID */
        case 0x2d:
        case 0x2e:
        case 0x2f:
            if (!(dev->pci_conf[0x74] & 0x40))
                dev->pci_conf[addr] = val;
            break;

        case 0x40:
            dev->pci_conf[addr] = val & 0x7f;
            break;

        case 0x41:
            dev->pci_conf[addr] = val;
            break;

        case 0x42: /* ISA Bus Speed */
            dev->pci_conf[addr] = val & 0xcf;
            switch (val & 7) {
                case 0:
                    cpu_set_isa_speed(7159091);
                    break;
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                    cpu_set_isa_pci_div((val & 7) + 1);
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

        /* We're going to cheat a little bit here and use MIRQ's as a substitute for the ALi's INTAJ's,
           as they work pretty much the same - specifically, we're going to use MIRQ2 and MIRQ3 for them,
           as MIRQ0 and MIRQ1 map to the ALi's MBIRQ0 and MBIRQ1. */
        case 0x44: /* Set IRQ Line for Primary IDE if it's on native mode */
            dev->pci_conf[addr] = val & 0xdf;
            soft_reset_pci      = !!(val & 0x80);
            sff_set_irq_level(dev->ide_controller[0], 0, !(val & 0x10));
            sff_set_irq_level(dev->ide_controller[1], 0, !(val & 0x10));
            ali1543_log("INTAJ = IRQ %i\n", ali1533_irq_routing[val & 0x0f]);
            pci_set_mirq_routing(PCI_MIRQ0, ali1533_irq_routing[val & 0x0f]);
            pci_set_mirq_routing(PCI_MIRQ2, ali1533_irq_routing[val & 0x0f]);
            break;

        /* TODO: Implement a ROMCS# assertion bitmask for I/O ports. */
        case 0x45: /* DDMA Enable */
            dev->pci_conf[addr] = val & 0xcb;
            ali1533_ddma_handler(dev);
            break;

        /* TODO: For 0x47, we need a way to obtain the memory state for an address
                 and toggle ROMCS#. */
        case 0x47: /* BIOS chip select control */
            dev->pci_conf[addr] = val;
            break;

        /* PCI IRQ Routing */
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
            dev->pci_conf[addr] = val;

            pci_set_irq_routing(((addr & 0x03) << 1) + 2, ali1533_irq_routing[(val >> 4) & 0x0f]);
            pci_set_irq_routing(((addr & 0x03) << 1) + 1, ali1533_irq_routing[val & 0x0f]);
            break;

        case 0x4c: /* PCI INT to ISA Level to Edge transfer */
            dev->pci_conf[addr] = val;

            for (irq = 1; irq < 9; irq++)
                pci_set_irq_level(irq, !(val & (1 << (irq - 1))));
            break;

        case 0x4d: /* MBIRQ0(SIRQI#), MBIRQ1(SIRQII#) Interrupt to ISA IRQ routing table */
            if (dev->type == 0) {
                dev->pci_conf[addr] = val;

                ali1543_log("SIRQI = IRQ %i; SIRQII = IRQ %i\n", ali1533_irq_routing[(val >> 4) & 0x0f], ali1533_irq_routing[val & 0x0f]);
                // pci_set_mirq_routing(PCI_MIRQ0, ali1533_irq_routing[(val >> 4) & 0x0f]);
                // pci_set_mirq_routing(PCI_MIRQ1, ali1533_irq_routing[val & 0x0f]);
            }
            break;

        /* I/O cycle posted-write first port definition */
        case 0x50:
            dev->pci_conf[addr] = val;
            break;
        case 0x51:
            dev->pci_conf[addr] = val & 0x8f;
            break;

        /* I/O cycle posted-write second port definition */
        case 0x52:
            dev->pci_conf[addr] = val;
            break;
        case 0x53:
            if (dev->type == 1)
                dev->pci_conf[addr] = val;
            else
                dev->pci_conf[addr] = val & 0xcf;
            /* This actually enables/disables the USB *device* rather than the interface itself. */
            dev->usb_dev_enable = !(val & 0x40);
            if (dev->type == 1) {
                nvr_at_index_read_handler(0, 0x0070, dev->nvr);
                nvr_at_index_read_handler(0, 0x0072, dev->nvr);
                if (val & 0x20) {
                    nvr_at_index_read_handler(1, 0x0070, dev->nvr);
                    nvr_at_index_read_handler(1, 0x0072, dev->nvr);
                }
            }
            break;

        /* Hardware setting status bits, read-only (register 0x54) */

        /* Programmable chip select (pin PCSJ) address define */
        case 0x55:
        case 0x56:
            dev->pci_conf[addr] = val;
            break;
        case 0x57:
            if (dev->type == 1)
                dev->pci_conf[addr] = val & 0xf0;
            else
                dev->pci_conf[addr] = val & 0xe0;
            break;

        /* IDE interface control */
        case 0x58:
            dev->pci_conf[addr] = val & 0x7f;
            ali1543_log("PCI58: %02X\n", val);
            dev->ide_dev_enable = !!(val & 0x40);
            switch (val & 0x30) {
                case 0x00:
                    dev->ide_slot = 0x10; /* A27 = slot 16 */
                    break;
                case 0x10:
                    dev->ide_slot = 0x0f; /* A26 = slot 15 */
                    break;
                case 0x20:
                    dev->ide_slot = 0x0e; /* A25 = slot 14 */
                    break;
                case 0x30:
                    dev->ide_slot = 0x0d; /* A24 = slot 13 */
                    break;
            }
            pci_relocate_slot(PCI_CARD_SOUTHBRIDGE_IDE, ((int) dev->ide_slot) + dev->offset);
            ali1543_log("IDE slot = %02X (A%0i)\n", ((int) dev->ide_slot) + dev->offset, dev->ide_slot + 11);
            ali5229_ide_irq_handler(dev);
            break;

        /* General Purpose input multiplexed pin(GPI) select */
        case 0x59:
            dev->pci_conf[addr] = val & 0x0e;
            break;

        /* General Purpose output multiplexed pin(GPO) select low */
        case 0x5a:
            dev->pci_conf[addr] = val & 0x0f;
            break;
        /* General Purpose output multiplexed pin(GPO) select high */
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
            if (dev->type == 1)
                dev->pci_conf[addr] = val & 0xe1;
            else
                dev->pci_conf[addr] = val & 0xe0;
            break;

        case 0x5f:
            dev->pci_conf[addr] = val;
            dev->pmu_dev_enable = !(val & 0x04);
            break;

        case 0x6c: /* Deleted - no idea what it used to do */
            dev->pci_conf[addr] = val;
            break;

        case 0x6d:
            dev->pci_conf[addr] = val & 0xbf;
            break;

        case 0x6e:
        case 0x70:
            dev->pci_conf[addr] = val;
            break;

        case 0x71:
            dev->pci_conf[addr] = val & 0xef;
            break;

        case 0x72:
            dev->pci_conf[addr] = val & 0xef;
            switch (val & 0x0c) {
                case 0x00:
                    dev->pmu_slot = 0x11; /* A28 = slot 17 */
                    break;
                case 0x04:
                    dev->pmu_slot = 0x12; /* A29 = slot 18 */
                    break;
                case 0x08:
                    dev->pmu_slot = 0x03; /* A14 = slot 03 */
                    break;
                case 0x0c:
                    dev->pmu_slot = 0x04; /* A15 = slot 04 */
                    break;
            }
            pci_relocate_slot(PCI_CARD_SOUTHBRIDGE_PMU, ((int) dev->pmu_slot) + dev->offset);
            ali1543_log("PMU slot = %02X (A%0i)\n", ((int) dev->pmu_slot) + dev->offset, dev->pmu_slot + 11);
            switch (val & 0x03) {
                case 0x00:
                    dev->usb_slot = 0x14; /* A31 = slot 20 */
                    break;
                case 0x01:
                    dev->usb_slot = 0x13; /* A30 = slot 19 */
                    break;
                case 0x02:
                    dev->usb_slot = 0x02; /* A13 = slot 02 */
                    break;
                case 0x03:
                    dev->usb_slot = 0x01; /* A12 = slot 01 */
                    break;
            }
            pci_relocate_slot(PCI_CARD_SOUTHBRIDGE_USB, ((int) dev->usb_slot) + dev->offset);
            ali1543_log("USB slot = %02X (A%0i)\n", ((int) dev->usb_slot) + dev->offset, dev->usb_slot + 11);
            break;

        case 0x73: /* DDMA Base Address */
            dev->pci_conf[addr] = val;
            ali1533_ddma_handler(dev);
            break;

        case 0x74: /* USB IRQ Routing - we cheat and use MIRQ4 */
            dev->pci_conf[addr] = val & 0xdf;
            /* TODO: MIRQ level/edge control - if bit 4 = 1, it's level */
            pci_set_mirq_routing(PCI_MIRQ4, ali1533_irq_routing[val & 0x0f]);
            break;

        case 0x75: /* Set IRQ Line for Secondary IDE if it's on native mode */
            dev->pci_conf[addr] = val & 0x1f;
            sff_set_irq_level(dev->ide_controller[0], 1, !(val & 0x10));
            sff_set_irq_level(dev->ide_controller[1], 1, !(val & 0x10));
            ali1543_log("INTBJ = IRQ %i\n", ali1533_irq_routing[val & 0x0f]);
            pci_set_mirq_routing(PCI_MIRQ1, ali1533_irq_routing[val & 0x0f]);
            pci_set_mirq_routing(PCI_MIRQ3, ali1533_irq_routing[val & 0x0f]);
            break;

        case 0x76: /* PMU IRQ Routing - we cheat and use MIRQ5 */
            if (dev->type == 1)
                dev->pci_conf[addr] = val & 0x9f;
            else
                dev->pci_conf[addr] = val & 0x1f;
            acpi_set_mirq_is_level(dev->acpi, !!(val & 0x10));
            if ((dev->type == 1) && (val & 0x80))
                pci_set_mirq_routing(PCI_MIRQ5, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ5, ali1533_irq_routing[val & 0x0f]);
            /* TODO: Tell ACPI to use MIRQ5 */
            break;

        case 0x77: /* SMBus IRQ Routing - we cheat and use MIRQ6 */
            dev->pci_conf[addr] = val & 0x1f;
            pci_set_mirq_routing(PCI_MIRQ6, ali1533_irq_routing[val & 0x0f]);
            break;

        case 0x78:
            if (dev->type == 1) {
                ali1543_log("PCI78 = %02X\n", val);
                dev->pci_conf[addr] = val & 0x33;
            }
            break;

        case 0x7c ... 0xff:
            if ((dev->type == 1) && !dev->pmu_dev_enable) {
                dev->pmu_dev_enable = 1;
                ali7101_write(func, addr, val, priv);
                dev->pmu_dev_enable = 0;
            }
            break;
    }
}

static uint8_t
ali1533_read(int func, int addr, void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;
    uint8_t    ret = 0xff;

    if (func == 0) {
        if (((dev->pci_conf[0x42] & 0x80) && (addr >= 0x40)) || ((dev->pci_conf[0x5f] & 8) && (addr == 4)))
            ret = 0x00;
        else {
            ret = dev->pci_conf[addr];
            if (addr == 0x58)
                ret = (ret & 0xbf) | (dev->ide_dev_enable ? 0x40 : 0x00);
            else if ((dev->type == 1) && ((addr >= 0x7c) && (addr <= 0xff)) && !dev->pmu_dev_enable) {
                dev->pmu_dev_enable = 1;
                ret                 = ali7101_read(func, addr, priv);
                dev->pmu_dev_enable = 0;
            }
        }
    }

    return ret;
}

static void
ali5229_ide_irq_handler(ali1543_t *dev)
{
    int ctl = 0, ch = 0;
    int bit = 0;

    if (dev->ide_conf[0x52] & 0x10) {
        ctl ^= 1;
        ch ^= 1;
        bit ^= 5;
    }

    if (dev->ide_conf[0x09] & (1 ^ bit)) {
        /* Primary IDE is native. */
        ali1543_log("Primary IDE IRQ mode: Native, Native\n");
        sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 4);
        sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 4);
    } else {
        /* Primary IDE is legacy. */
        switch (dev->pci_conf[0x58] & 0x03) {
            case 0x00:
                /* SIRQI, SIRQII */
                ali1543_log("Primary IDE IRQ mode: SIRQI, SIRQII\n");
                sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 2);
                sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 5);
                break;
            case 0x01:
                /* IRQ14, IRQ15 */
                ali1543_log("Primary IDE IRQ mode: IRQ14, IRQ15\n");
                sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 0);
                sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 0);
                break;
            case 0x02:
                /* IRQ14, SIRQII */
                ali1543_log("Primary IDE IRQ mode: IRQ14, SIRQII\n");
                sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 0);
                sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 5);
                break;
            case 0x03:
                /* IRQ14, SIRQI */
                ali1543_log("Primary IDE IRQ mode: IRQ14, SIRQI\n");
                sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 0);
                sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 2);
                break;
        }
    }

    ctl ^= 1;

    if (dev->ide_conf[0x09] & (4 ^ bit)) {
        /* Secondary IDE is native. */
        ali1543_log("Secondary IDE IRQ mode: Native, Native\n");
        sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 4);
        sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 4);
    } else {
        /* Secondary IDE is legacy. */
        switch (dev->pci_conf[0x58] & 0x03) {
            case 0x00:
                /* SIRQI, SIRQII */
                ali1543_log("Secondary IDE IRQ mode: SIRQI, SIRQII\n");
                sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 2);
                sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 5);
                break;
            case 0x01:
                /* IRQ14, IRQ15 */
                ali1543_log("Secondary IDE IRQ mode: IRQ14, IRQ15\n");
                sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 0);
                sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 0);
                break;
            case 0x02:
                /* IRQ14, SIRQII */
                ali1543_log("Secondary IDE IRQ mode: IRQ14, SIRQII\n");
                sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 0);
                sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 5);
                break;
            case 0x03:
                /* IRQ14, SIRQI */
                ali1543_log("Secondary IDE IRQ mode: IRQ14, SIRQI\n");
                sff_set_irq_mode(dev->ide_controller[ctl], 0 ^ ch, 0);
                sff_set_irq_mode(dev->ide_controller[ctl], 1 ^ ch, 2);
                break;
        }
    }
}

static void
ali5229_ide_handler(ali1543_t *dev)
{
    uint32_t ch = 0;

    uint16_t native_base_pri_addr = ((dev->ide_conf[0x11] | dev->ide_conf[0x10] << 8)) & 0xfffe;
    uint16_t native_side_pri_addr = ((dev->ide_conf[0x15] | dev->ide_conf[0x14] << 8)) & 0xfffe;
    uint16_t native_base_sec_addr = ((dev->ide_conf[0x19] | dev->ide_conf[0x18] << 8)) & 0xfffe;
    uint16_t native_side_sec_addr = ((dev->ide_conf[0x1c] | dev->ide_conf[0x1b] << 8)) & 0xfffe;

    uint16_t comp_base_pri_addr = 0x01f0;
    uint16_t comp_side_pri_addr = 0x03f6;
    uint16_t comp_base_sec_addr = 0x0170;
    uint16_t comp_side_sec_addr = 0x0376;

    uint16_t current_pri_base, current_pri_side, current_sec_base, current_sec_side;

    /* Primary Channel Programming */
    if (dev->ide_conf[0x52] & 0x10) {
        current_pri_base = (!(dev->ide_conf[0x09] & 1)) ? comp_base_sec_addr : native_base_sec_addr;
        current_pri_side = (!(dev->ide_conf[0x09] & 1)) ? comp_side_sec_addr : native_side_sec_addr;
    } else {
        current_pri_base = (!(dev->ide_conf[0x09] & 1)) ? comp_base_pri_addr : native_base_pri_addr;
        current_pri_side = (!(dev->ide_conf[0x09] & 1)) ? comp_side_pri_addr : native_side_pri_addr;
    }

    /* Secondary Channel Programming */
    if (dev->ide_conf[0x52] & 0x10) {
        current_sec_base = (!(dev->ide_conf[0x09] & 4)) ? comp_base_pri_addr : native_base_pri_addr;
        current_sec_side = (!(dev->ide_conf[0x09] & 4)) ? comp_side_pri_addr : native_side_pri_addr;
    } else {
        current_sec_base = (!(dev->ide_conf[0x09] & 4)) ? comp_base_sec_addr : native_base_sec_addr;
        current_sec_side = (!(dev->ide_conf[0x09] & 4)) ? comp_side_sec_addr : native_side_sec_addr;
    }

    if (dev->ide_conf[0x52] & 0x10)
        ch ^= 8;

    ali1543_log("ali5229_ide_handler(): Disabling primary IDE...\n");
    ide_pri_disable();
    ali1543_log("ali5229_ide_handler(): Disabling secondary IDE...\n");
    ide_sec_disable();

    if (dev->ide_conf[0x04] & 0x01) {
        /* Primary Channel Setup */
        if ((dev->ide_conf[0x09] & 0x20) || (dev->ide_conf[0x4d] & 0x80)) {
            ali1543_log("ali5229_ide_handler(): Primary IDE base now %04X...\n", current_pri_base);
            ide_set_base(0, current_pri_base);
            ali1543_log("ali5229_ide_handler(): Primary IDE side now %04X...\n", current_pri_side);
            ide_set_side(0, current_pri_side);

            ali1543_log("ali5229_ide_handler(): Enabling primary IDE...\n");
            ide_pri_enable();

            sff_bus_master_handler(dev->ide_controller[0], dev->ide_conf[0x04] & 0x01, ((dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8)) + (0 ^ ch));
            ali1543_log("M5229 PRI: BASE %04x SIDE %04x\n", current_pri_base, current_pri_side);
        }

        /* Secondary Channel Setup */
        if ((dev->ide_conf[0x09] & 0x10) || (dev->ide_conf[0x4d] & 0x80)) {
            ali1543_log("ali5229_ide_handler(): Secondary IDE base now %04X...\n", current_sec_base);
            ide_set_base(1, current_sec_base);
            ali1543_log("ali5229_ide_handler(): Secondary IDE side now %04X...\n", current_sec_side);
            ide_set_side(1, current_sec_side);

            ali1543_log("ali5229_ide_handler(): Enabling secondary IDE...\n");
            ide_sec_enable();

            sff_bus_master_handler(dev->ide_controller[1], dev->ide_conf[0x04] & 0x01, (((dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8))) + (8 ^ ch));
            ali1543_log("M5229 SEC: BASE %04x SIDE %04x\n", current_sec_base, current_sec_side);
        }
    } else {
        sff_bus_master_handler(dev->ide_controller[0], dev->ide_conf[0x04] & 0x01, (dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8));
        sff_bus_master_handler(dev->ide_controller[1], dev->ide_conf[0x04] & 0x01, ((dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8)) + 8);
    }
}

static void
ali5229_chip_reset(ali1543_t *dev)
{
    /* M5229 */
    memset(dev->ide_conf, 0x00, sizeof(dev->pmu_conf));
    dev->ide_conf[0x00] = 0xb9;
    dev->ide_conf[0x01] = 0x10;
    dev->ide_conf[0x02] = 0x29;
    dev->ide_conf[0x03] = 0x52;
    dev->ide_conf[0x06] = 0x80;
    dev->ide_conf[0x07] = 0x02;
    dev->ide_conf[0x08] = 0x20;
    dev->ide_conf[0x0a] = 0x01;
    dev->ide_conf[0x0b] = 0x01;
    dev->ide_conf[0x10] = 0xf1;
    dev->ide_conf[0x11] = 0x01;
    dev->ide_conf[0x14] = 0xf5;
    dev->ide_conf[0x15] = 0x03;
    dev->ide_conf[0x18] = 0x71;
    dev->ide_conf[0x19] = 0x01;
    dev->ide_conf[0x1c] = 0x75;
    dev->ide_conf[0x1d] = 0x03;
    dev->ide_conf[0x20] = 0x01;
    dev->ide_conf[0x21] = 0xf0;
    dev->ide_conf[0x3d] = 0x01;
    dev->ide_conf[0x3e] = 0x02;
    dev->ide_conf[0x3f] = 0x04;
    dev->ide_conf[0x53] = 0x03;
    dev->ide_conf[0x54] = 0x55;
    dev->ide_conf[0x55] = 0x55;
    dev->ide_conf[0x63] = 0x01;
    dev->ide_conf[0x64] = 0x02;
    dev->ide_conf[0x67] = 0x01;
    dev->ide_conf[0x78] = 0x21;

    if (dev->type == 1) {
        dev->ide_conf[0x08] = 0xc1;
        dev->ide_conf[0x43] = 0x00;
        dev->ide_conf[0x4b] = 0x4a;
        dev->ide_conf[0x4e] = 0xba;
        dev->ide_conf[0x4f] = 0x1a;
    }

    ali5229_write(0, 0x04, 0x05, dev);
    ali5229_write(0, 0x10, 0xf1, dev);
    ali5229_write(0, 0x11, 0x01, dev);
    ali5229_write(0, 0x14, 0xf5, dev);
    ali5229_write(0, 0x15, 0x03, dev);
    ali5229_write(0, 0x18, 0x71, dev);
    ali5229_write(0, 0x19, 0x01, dev);
    ali5229_write(0, 0x1a, 0x75, dev);
    ali5229_write(0, 0x1b, 0x03, dev);
    ali5229_write(0, 0x20, 0x01, dev);
    ali5229_write(0, 0x21, 0xf0, dev);
    ali5229_write(0, 0x4d, 0x00, dev);
    dev->ide_conf[0x09] = 0xfa;
    ali5229_write(0, 0x09, 0xfa, dev);
    ali5229_write(0, 0x52, 0x00, dev);

    ali5229_write(0, 0x50, 0x00, dev);

    sff_set_slot(dev->ide_controller[0], dev->ide_slot);
    sff_set_slot(dev->ide_controller[1], dev->ide_slot);
    sff_bus_master_reset(dev->ide_controller[0], (dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8));
    sff_bus_master_reset(dev->ide_controller[1], ((dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8)) + 8);
    ali5229_ide_handler(dev);
}

static void
ali5229_write(int func, int addr, uint8_t val, void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;
    ali1543_log("M5229: dev->ide_conf[%02x] = %02x\n", addr, val);

    if (func > 0)
        return;

    if (!dev->ide_dev_enable)
        return;

    switch (addr) {
        case 0x04: /* COM - Command Register */
            ali1543_log("IDE04: %02X\n", val);
            dev->ide_conf[addr] = val & 0x45;
            ali5229_ide_handler(dev);
            break;

        case 0x05:
            dev->ide_conf[addr] = val & 0x01;
            break;

        case 0x07:
            dev->ide_conf[addr] &= ~(val & 0xf1);
            break;

        case 0x09: /* Control */
            ali1543_log("IDE09: %02X\n", val);

            if (dev->type == 1) {
                val &= ~(dev->ide_conf[0x43]);
                val |= (dev->ide_conf[addr] & dev->ide_conf[0x43]);
            }

            if (dev->ide_conf[0x4d] & 0x80)
                dev->ide_conf[addr] = (dev->ide_conf[addr] & 0xfa) | (val & 0x05);
            else
                dev->ide_conf[addr] = (dev->ide_conf[addr] & 0x8a) | (val & 0x75);
            ali5229_ide_handler(dev);
            ali5229_ide_irq_handler(dev);
            break;

        /* Primary Base Address */
        case 0x10:
        case 0x11:
        case 0x14:
        case 0x15:
            /* FALLTHROUGH */

        /* Secondary Base Address */
        case 0x18:
        case 0x19:
        case 0x1c:
        case 0x1d:
            /* FALLTHROUGH */

        /* Bus Mastering Base Address */
        case 0x20:
        case 0x21:
            /* Datasheet erratum: the PCI BAR's actually have different sizes. */
            if (addr == 0x20)
                dev->ide_conf[addr] = (val & 0xe0) | 0x01;
            else if ((addr & 0x43) == 0x00)
                dev->ide_conf[addr] = (val & 0xf8) | 0x01;
            else if ((addr & 0x43) == 0x40)
                dev->ide_conf[addr] = (val & 0xfc) | 0x01;
            else
                dev->ide_conf[addr] = val;
            ali5229_ide_handler(dev);
            break;

        case 0x2c: /* Subsystem Vendor ID */
        case 0x2d:
        case 0x2e:
        case 0x2f:
            if (!(dev->ide_conf[0x53] & 0x80))
                dev->ide_conf[addr] = val;
            break;

        case 0x3c: /* Interrupt Line */
        case 0x3d: /* Interrupt Pin */
            dev->ide_conf[addr] = val;
            break;

        /* The machines don't touch anything beyond that point so we avoid any programming */
        case 0x43:
            if (dev->type == 1)
                dev->ide_conf[addr] = val & 0x7f;
            break;

        case 0x4b:
            if (dev->type == 1)
                dev->ide_conf[addr] = val;
            break;

        case 0x4d:
            dev->ide_conf[addr] = val & 0x80;
            ali5229_ide_handler(dev);
            break;

        case 0x4f:
            if (dev->type == 0)
                dev->ide_conf[addr] = val & 0x3f;
            break;

        case 0x50: /* Configuration */
            ali1543_log("IDE50: %02X\n", val);
            dev->ide_conf[addr] = val & 0x2b;
            dev->ide_dev_enable = !!(val & 0x01);
            break;

        case 0x51:
            dev->ide_conf[addr] = val & 0xf7;
            if (val & 0x80)
                ali5229_chip_reset(dev);
            else if (val & 0x40) {
                sff_bus_master_reset(dev->ide_controller[0], (dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8));
                sff_bus_master_reset(dev->ide_controller[1], ((dev->ide_conf[0x20] & 0xf0) | (dev->ide_conf[0x21] << 8)) + 8);
            }
            break;

        case 0x52: /* FCS - Flexible Channel Setting Register */
            dev->ide_conf[addr] = val;
            ali5229_ide_handler(dev);
            ali5229_ide_irq_handler(dev);
            break;

        case 0x53: /* Subsystem Vendor ID */
            dev->ide_conf[addr] = val & 0x8b;
            break;

        case 0x54: /* FIFO threshold of primary channel drive 0 and drive 1 */
        case 0x55: /* FIFO threshold of secondary channel drive 0 and drive 1 */
        case 0x56: /* Ultra DMA /33 setting for Primary drive 0 and drive 1 */
        case 0x57: /* Ultra DMA /33 setting for Secondary drive 0 and drive 1 */
        case 0x78: /* IDE clock's frequency (default value is 33 = 21H) */
            dev->ide_conf[addr] = val;
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
    }
}

static uint8_t
ali5229_read(int func, int addr, void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;
    uint8_t    ret = 0xff;

    if (dev->ide_dev_enable && (func == 0)) {
        ret = dev->ide_conf[addr];
        if ((addr == 0x09) && !(dev->ide_conf[0x50] & 0x02))
            ret &= 0x0f;
        else if (addr == 0x50)
            ret = (ret & 0xfe) | (dev->ide_dev_enable ? 0x01 : 0x00);
        else if (addr == 0x75)
            ret = ide_read_ali_75();
        else if (addr == 0x76)
            ret = ide_read_ali_76();
    }

    return ret;
}

static void
ali5237_write(int func, int addr, uint8_t val, void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;
    ali1543_log("M5237: dev->usb_conf[%02x] = %02x\n", addr, val);

    if (func > 0)
        return;

    if (!dev->usb_dev_enable)
        return;

    switch (addr) {
        case 0x04: /* USB Enable */
            dev->usb_conf[addr] = val & 0x5f;
            ohci_update_mem_mapping(dev->usb, dev->usb_conf[0x11], dev->usb_conf[0x12], dev->usb_conf[0x13], dev->usb_conf[0x04] & 1);
            break;

        case 0x05:
            dev->usb_conf[addr] = 0x01;
            break;

        case 0x07:
            dev->usb_conf[addr] &= ~(val & 0xc9);
            break;

        case 0x0c: /* Cache Line Size */
        case 0x0d: /* Latency Timer */
            dev->usb_conf[addr] = val;
            break;

        case 0x3c: /* Interrupt Line Register */
            dev->usb_conf[addr] = val;
            break;

        case 0x42: /* Test Mode Register */
            dev->usb_conf[addr] = val & 0x10;
            break;
        case 0x43:
            if (dev->type == 1)
                dev->usb_conf[addr] = val & 0x04;
            break;

        /* USB Base I/O */
        case 0x11:
            dev->usb_conf[addr] = val & 0xf0;
            ohci_update_mem_mapping(dev->usb, dev->usb_conf[0x11], dev->usb_conf[0x12], dev->usb_conf[0x13], dev->usb_conf[0x04] & 1);
            break;
        case 0x12:
        case 0x13:
            dev->usb_conf[addr] = val;
            ohci_update_mem_mapping(dev->usb, dev->usb_conf[0x11], dev->usb_conf[0x12], dev->usb_conf[0x13], dev->usb_conf[0x04] & 1);
            break;

        case 0x2c: /* Subsystem Vendor ID */
        case 0x2d:
        case 0x2e:
        case 0x2f:
            if (!(dev->usb_conf[0x42] & 0x10))
                dev->usb_conf[addr] = val;
            break;
    }
}

static uint8_t
ali5237_read(int func, int addr, void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;
    uint8_t    ret = 0xff;

    if (dev->usb_dev_enable && (func == 0))
        ret = dev->usb_conf[addr];

    return ret;
}

static void
ali7101_write(int func, int addr, uint8_t val, void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;
    ali1543_log("M7101: dev->pmu_conf[%02x] = %02x\n", addr, val);

    if (func > 0)
        return;

    if (!dev->pmu_dev_enable)
        return;

    if ((dev->pmu_conf[0xc9] & 0x01) && (addr >= 0x40) && (addr != 0xc9))
        return;

    switch (addr) {
        case 0x04: /* Enable PMU */
            ali1543_log("PMU04: %02X\n", val);
            dev->pmu_conf[addr] = val & 0x01;
            if (!(dev->pmu_conf[0x5b] & 0x02))
                acpi_update_io_mapping(dev->acpi, (dev->pmu_conf[0x11] << 8) | (dev->pmu_conf[0x10] & 0xc0), dev->pmu_conf[0x04] & 1);
            if (!(dev->pmu_conf[0x5b] & 0x04)) {
                if (dev->type == 1)
                    smbus_ali7101_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xc0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1));
                else
                    smbus_ali7101_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xe0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1));
            }
            break;

        /* PMU Base I/O */
        case 0x10:
        case 0x11:
            if (!(dev->pmu_conf[0x5b] & 0x02)) {
                if (addr == 0x10)
                    dev->pmu_conf[addr] = (val & 0xc0) | 1;
                else if (addr == 0x11)
                    dev->pmu_conf[addr] = val;

                ali1543_log("New ACPI base address: %08X\n", (dev->pmu_conf[0x11] << 8) | (dev->pmu_conf[0x10] & 0xc0));
                acpi_update_io_mapping(dev->acpi, (dev->pmu_conf[0x11] << 8) | (dev->pmu_conf[0x10] & 0xc0), dev->pmu_conf[0x04] & 1);
            }
            break;

        /* SMBus Base I/O */
        case 0x14:
        case 0x15:
            if (!(dev->pmu_conf[0x5b] & 0x04)) {
                if (addr == 0x14) {
                    if (dev->type == 1)
                        dev->pmu_conf[addr] = (val & 0xc0) | 1;
                    else
                        dev->pmu_conf[addr] = (val & 0xe0) | 1;
                } else if (addr == 0x15)
                    dev->pmu_conf[addr] = val;

                if (dev->type == 1) {
                    ali1543_log("New SMBUS base address: %08X\n", (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xc0));
                    smbus_ali7101_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xc0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1));
                } else {
                    ali1543_log("New SMBUS base address: %08X\n", (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xe0));
                    smbus_ali7101_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xe0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1));
                }
            }
            break;

        /* Subsystem Vendor ID */
        case 0x2c:
        case 0x2d:
        case 0x2e:
        case 0x2f:
            if (!(dev->pmu_conf[0xd8] & 0x08))
                dev->pmu_conf[addr] = val;
            break;

        case 0x40:
            dev->pmu_conf[addr] = val & 0x1f;
            pic_set_smi_irq_mask(8, (dev->pmu_conf[0x77] & 0x08) && (dev->pmu_conf[0x40] & 0x03));
            break;
        case 0x41:
            dev->pmu_conf[addr] = val & 0x10;
            ali1543_log("PMU41: %02X\n", val);
            apm_set_do_smi(dev->acpi->apm, (dev->pmu_conf[0x77] & 0x08) && (dev->pmu_conf[0x41] & 0x10));
            break;

        /* TODO: Is the status R/W or R/WC? */
        case 0x42:
            dev->pmu_conf[addr] &= ~(val & 0x1f);
            break;
        case 0x43:
            dev->pmu_conf[addr] &= ~(val & 0x10);
            if (val & 0x10)
                acpi_ali_soft_smi_status_write(dev->acpi, 0);
            break;

        case 0x44:
            dev->pmu_conf[addr] = val;
            break;
        case 0x45:
            dev->pmu_conf[addr] = val & 0x9f;
            break;
        case 0x46:
            dev->pmu_conf[addr] = val & 0x18;
            break;

        /* TODO: Is the status R/W or R/WC? */
        case 0x48:
            dev->pmu_conf[addr] &= ~val;
            break;
        case 0x49:
            dev->pmu_conf[addr] &= ~(val & 0x9f);
            break;
        case 0x4a:
            dev->pmu_conf[addr] &= ~(val & 0x38);
            break;

        case 0x4c:
            dev->pmu_conf[addr] = val & 5;
            break;
        case 0x4d:
            dev->pmu_conf[addr] = val & 1;
            break;

        /* TODO: Is the status R/W or R/WC? */
        case 0x4e:
            dev->pmu_conf[addr] &= ~(val & 5);
            break;
        case 0x4f:
            dev->pmu_conf[addr] &= ~(val & 1);
            break;

        case 0x50:
        case 0x51:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val;
            break;

        case 0x52:
        case 0x53:
            if (dev->type == 1)
                dev->pmu_conf[addr] &= ~val;
            break;

        case 0x54: /* Standby timer */
            dev->pmu_conf[addr] = val;
            break;
        case 0x55: /* APM Timer */
            dev->pmu_conf[addr] = val & 0x7f;
            break;
        case 0x59: /* Global display timer. */
            dev->pmu_conf[addr] = val & 0x1f;
            break;

        case 0x5b: /* ACPI/SMB Base I/O Control */
            if (dev->type == 1)
                dev->pmu_conf[addr] = val & 0x87;
            else
                dev->pmu_conf[addr] = val & 0x7f;
            break;

        case 0x60:
            dev->pmu_conf[addr] = val;
            break;
        case 0x61:
            dev->pmu_conf[addr] = val & 0x13;
            break;
        case 0x62:
            dev->pmu_conf[addr] = val & 0xf1;
            break;
        case 0x63:
            dev->pmu_conf[addr] = val & 0x07;
            break;

        case 0x64:
            dev->pmu_conf[addr] = val;
            break;
        case 0x65:
            dev->pmu_conf[addr] = val & 0x11;
            break;

        case 0x68:
            dev->pmu_conf[addr] = val & 0x07;
            break;

        case 0x6c:
        case 0x6d:
            dev->pmu_conf[addr] = val;
            break;
        case 0x6e:
            dev->pmu_conf[addr] = val & 0xbf;
            break;
        case 0x6f:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val & 0x1e;
            else
                dev->pmu_conf[addr] = val & 0x1f;
            break;

        case 0x70:
            dev->pmu_conf[addr] = val;
            break;
        case 0x71:
            dev->pmu_conf[addr] = val & 0x3f;
            break;

        case 0x72:
            dev->pmu_conf[addr] = val & 0x0f;
            break;

        /* TODO: Is the status R/W or R/WC? */
        case 0x74:
            dev->pmu_conf[addr] &= ~(val & 0x33);
            break;

        case 0x75:
            dev->pmu_conf[addr] = val;
            break;

        case 0x76:
            dev->pmu_conf[addr] = val & 0x7f;
            break;

        case 0x77:
            /* TODO: If bit 1 is clear, then status bit is set even if SMI is disabled. */
            dev->pmu_conf[addr] = val;
            pic_set_smi_irq_mask(8, (dev->pmu_conf[0x77] & 0x08) && (dev->pmu_conf[0x40] & 0x03));
            ali1543_log("PMU77: %02X\n", val);
            apm_set_do_smi(dev->acpi->apm, (dev->pmu_conf[0x77] & 0x08) && (dev->pmu_conf[0x41] & 0x10));
            break;

        case 0x78:
            dev->pmu_conf[addr] = val;
            break;
        case 0x79:
            dev->pmu_conf[addr] = val & 0x0f;
            break;

        case 0x7a:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val & 0x07;
            else
                dev->pmu_conf[addr] = val & 0x02;
            break;

        case 0x7b:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val;
            else
                dev->pmu_conf[addr] = val & 0x7f;
            break;

        case 0x7c ... 0x7f:
            dev->pmu_conf[addr] = val;
            break;

        case 0x81:
            dev->pmu_conf[addr] = val & 0xf0;
            break;

        case 0x82:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val & 0x01;
            break;

        case 0x84 ... 0x87:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val;
            break;
        case 0x88 ... 0x8b:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val;
            break;

        case 0x8c:
        case 0x8d:
            dev->pmu_conf[addr] = val & 0x0f;
            break;

        case 0x90:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val & 0x0f;
            else
                dev->pmu_conf[addr] = val & 0x01;
            break;

        case 0x91:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val & 0x02;
            break;

        case 0x94:
            dev->pmu_conf[addr] = val & 0xf0;
            break;
        case 0x95 ... 0x97:
            dev->pmu_conf[addr] = val;
            break;

        case 0x98:
        case 0x99:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val;
            break;

        case 0xa4:
        case 0xa5:
            dev->pmu_conf[addr] = val;
            break;

        case 0xb2:
            dev->pmu_conf[addr] = val & 0x01;
            break;

        case 0xb3:
            dev->pmu_conf[addr] = val & 0x7f;
            break;

        case 0xb4:
            dev->pmu_conf[addr] = val & 0x7c;
            break;

        case 0xb5:
        case 0xb7:
            dev->pmu_conf[addr] = val & 0x0f;
            break;

        case 0xb8:
        case 0xb9:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val;
            break;

        case 0xbc:
            outb(0x70, val);
            break;

        case 0xbd:
            dev->pmu_conf[addr] = val & 0x0f;
            acpi_set_timer32(dev->acpi, val & 0x04);
            break;

        case 0xbe:
            dev->pmu_conf[addr] = val & 0x03;
            break;

        /* Continue Further Later */
        /* GPO Registers */
        case 0xc0:
            dev->pmu_conf[addr] = val & 0x0f;
            acpi_init_gporeg(dev->acpi, dev->pmu_conf[0xc0], dev->pmu_conf[0xc1], dev->pmu_conf[0xc2] | (dev->pmu_conf[0xc3] << 5), 0x00);
            break;
        case 0xc1:
            dev->pmu_conf[addr] = val & 0x12;
            acpi_init_gporeg(dev->acpi, dev->pmu_conf[0xc0], dev->pmu_conf[0xc1], dev->pmu_conf[0xc2] | (dev->pmu_conf[0xc3] << 5), 0x00);
            break;
        case 0xc2:
            dev->pmu_conf[addr] = val & 0x1c;
            acpi_init_gporeg(dev->acpi, dev->pmu_conf[0xc0], dev->pmu_conf[0xc1], dev->pmu_conf[0xc2] | (dev->pmu_conf[0xc3] << 5), 0x00);
            break;
        case 0xc3:
            dev->pmu_conf[addr] = val & 0x06;
            acpi_init_gporeg(dev->acpi, dev->pmu_conf[0xc0], dev->pmu_conf[0xc1], dev->pmu_conf[0xc2] | (dev->pmu_conf[0xc3] << 5), 0x00);
            break;

        case 0xc6:
            dev->pmu_conf[addr] = val & 0x06;
            break;

        case 0xc8:
        case 0xc9:
            dev->pmu_conf[addr] = val & 0x01;
            break;

        case 0xca:
            /* TODO: Write to this port causes a beep. */
            dev->pmu_conf[addr] = val;
            break;

        case 0xcc:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val & 0x1f;
            break;
        case 0xcd:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val & 0x33;
            break;

        case 0xd4:
            dev->pmu_conf[addr] = val & 0x01;
            break;

        case 0xd8:
            dev->pmu_conf[addr] = val & 0xfd;
            break;
        case 0xd9:
            if (dev->type == 1)
                dev->pmu_conf[addr] = val & 0x3f;
            break;

        case 0xe0:
            dev->pmu_conf[addr] = val & 0x03;
            if (dev->type == 1)
                smbus_ali7101_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xc0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1) && (!(dev->pci_conf[0x5f] & 4)));
            else
                smbus_ali7101_remap(dev->smbus, (dev->pmu_conf[0x15] << 8) | (dev->pmu_conf[0x14] & 0xe0), (dev->pmu_conf[0xe0] & 1) && (dev->pmu_conf[0x04] & 1) && (!(dev->pci_conf[0x5f] & 4)));
            break;

        case 0xe1:
            dev->pmu_conf[addr] = val;
            break;

        case 0xe2:
            dev->pmu_conf[addr] = val & 0xf8;
            break;

        default:
            dev->pmu_conf[addr] = val;
            break;
    }
}

static uint8_t
ali7101_read(int func, int addr, void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;
    uint8_t    ret = 0xff;

    if (dev->pmu_dev_enable && (func == 0)) {
        if ((dev->pmu_conf[0xc9] & 0x01) && (addr >= 0x40) && (addr != 0xc9))
            return 0xff;

        /* TODO: C4, C5 = GPIREG (masks: 0D, 0E) */
        if (addr == 0x43)
            ret = acpi_ali_soft_smi_status_read(dev->acpi) ? 0x10 : 0x00;
        else if (addr == 0x7f)
            ret = 0x80;
        else if (addr == 0xbc)
            ret = inb(0x70);
        else
            ret = dev->pmu_conf[addr];

        if (dev->pmu_conf[0x77] & 0x10) {
            switch (addr) {
                case 0x42:
                    dev->pmu_conf[addr] &= 0xe0;
                    break;
                case 0x43:
                    dev->pmu_conf[addr] &= 0xef;
                    acpi_ali_soft_smi_status_write(dev->acpi, 0);
                    break;

                case 0x48:
                    dev->pmu_conf[addr] = 0x00;
                    break;
                case 0x49:
                    dev->pmu_conf[addr] &= 0x60;
                    break;
                case 0x4a:
                    dev->pmu_conf[addr] &= 0xc7;
                    break;

                case 0x4e:
                    dev->pmu_conf[addr] &= 0xfa;
                    break;
                case 0x4f:
                    dev->pmu_conf[addr] &= 0xfe;
                    break;

                case 0x74:
                    dev->pmu_conf[addr] &= 0xcc;
                    break;
            }
        }
    }

    return ret;
}

static void
ali5237_usb_update_interrupt(usb_t* usb, void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;

    if (usb->irq_level)
        pci_set_mirq(4, !!(dev->pci_conf[0x74] & 0x10));
    else
        pci_clear_mirq(4, !!(dev->pci_conf[0x74] & 0x10));
}

static void
ali1543_reset(void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;

    /* Temporarily enable everything. Register writes will disable the devices. */
    dev->ide_dev_enable = 1;
    dev->usb_dev_enable = 1;
    dev->pmu_dev_enable = 1;

    /* M5229 */
    ali5229_chip_reset(dev);

    /* M5237 */
    memset(dev->usb_conf, 0x00, sizeof(dev->usb_conf));
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
    ali5237_write(0, 0x10, 0x00, dev);
    ali5237_write(0, 0x11, 0x00, dev);
    ali5237_write(0, 0x12, 0x00, dev);
    ali5237_write(0, 0x13, 0x00, dev);

    /* M7101 */
    memset(dev->pmu_conf, 0x00, sizeof(dev->pmu_conf));
    dev->pmu_conf[0x00] = 0xb9;
    dev->pmu_conf[0x01] = 0x10;
    dev->pmu_conf[0x02] = 0x01;
    dev->pmu_conf[0x03] = 0x71;
    dev->pmu_conf[0x05] = 0x00;
    dev->pmu_conf[0x0a] = 0x01;
    dev->pmu_conf[0x0b] = 0x06;
    dev->pmu_conf[0xe2] = 0x20;

    acpi_set_slot(dev->acpi, dev->pmu_slot);
    acpi_set_nvr(dev->acpi, dev->nvr);

    ali7101_write(0, 0x04, 0x0f, dev);
    ali7101_write(0, 0x10, 0x01, dev);
    ali7101_write(0, 0x11, 0x00, dev);
    ali7101_write(0, 0x12, 0x00, dev);
    ali7101_write(0, 0x13, 0x00, dev);
    ali7101_write(0, 0x14, 0x01, dev);
    ali7101_write(0, 0x15, 0x00, dev);
    ali7101_write(0, 0x16, 0x00, dev);
    ali7101_write(0, 0x17, 0x00, dev);
    ali7101_write(0, 0x40, 0x00, dev);
    ali7101_write(0, 0x41, 0x00, dev);
    ali7101_write(0, 0x42, 0x00, dev);
    ali7101_write(0, 0x43, 0x00, dev);
    ali7101_write(0, 0x77, 0x00, dev);
    ali7101_write(0, 0xbd, 0x00, dev);
    ali7101_write(0, 0xc0, 0x00, dev);
    ali7101_write(0, 0xc1, 0x00, dev);
    ali7101_write(0, 0xc2, 0x00, dev);
    ali7101_write(0, 0xc3, 0x00, dev);
    ali7101_write(0, 0xe0, 0x00, dev);

    /* Do the bridge last due to device deactivations. */
    /* M1533 */
    dev->pci_conf[0x00] = 0xb9;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x33;
    dev->pci_conf[0x03] = 0x15;
    dev->pci_conf[0x04] = 0x0f;
    dev->pci_conf[0x07] = 0x02;
    if (dev->type == 1)
        dev->pci_conf[0x08] = 0xc0;
    dev->pci_conf[0x0a] = 0x01;
    dev->pci_conf[0x0b] = 0x06;

    ali1533_write(0, 0x41, 0x00, dev);    /* Disables the keyboard and mouse IRQ latch. */
    ali1533_write(0, 0x48, 0x00, dev);    /* Disables all IRQ's. */
    ali1533_write(0, 0x44, 0x00, dev);
    ali1533_write(0, 0x4d, 0x00, dev);
    ali1533_write(0, 0x53, 0x00, dev);
    ali1533_write(0, 0x58, 0x00, dev);
    ali1533_write(0, 0x5f, 0x00, dev);
    ali1533_write(0, 0x72, 0x00, dev);
    ali1533_write(0, 0x74, 0x00, dev);
    ali1533_write(0, 0x75, 0x00, dev);
    ali1533_write(0, 0x76, 0x00, dev);
    if (dev->type == 1)
        ali1533_write(0, 0x78, 0x00, dev);

    unmask_a20_in_smm = 1;
}

static void
ali1543_close(void *priv)
{
    ali1543_t *dev = (ali1543_t *) priv;

    free(dev);
}

static void *
ali1543_init(const device_t *info)
{
    ali1543_t *dev = (ali1543_t *) malloc(sizeof(ali1543_t));
    memset(dev, 0, sizeof(ali1543_t));

    /* Device 02: M1533 Southbridge */
    dev->pci_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, ali1533_read, ali1533_write, dev);

    /* Device 0B: M5229 IDE Controller*/
    dev->ide_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE_IDE, ali5229_read, ali5229_write, dev);

    /* Device 0C: M7101 Power Managment Controller */
    dev->pmu_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE_PMU, ali7101_read, ali7101_write, dev);

    /* Device 0F: M5237 USB */
    dev->usb_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE_USB, ali5237_read, ali5237_write, dev);

    /* ACPI */
    dev->acpi = device_add(&acpi_ali_device);
    dev->nvr  = device_add(&piix4_nvr_device);

    /* DMA */
    dma_alias_set();

    dma_set_sg_base(0x04);
    dma_set_params(1, 0xffffffff);
    dma_ext_mode_init();
    dma_high_page_init();

    /* DDMA */
    dev->ddma = device_add(&ddma_device);

    /* IDE Controllers */
    dev->ide_controller[0] = device_add_inst(&sff8038i_device, 1);
    dev->ide_controller[1] = device_add_inst(&sff8038i_device, 2);

    /* Port 92h */
    dev->port_92 = device_add(&port_92_pci_device);

    /* Standard SMBus */
    dev->smbus = device_add(&ali7101_smbus_device);

    /* USB */
    dev->usb_params.parent_priv      = dev;
    dev->usb_params.smi_handle       = NULL;
    dev->usb_params.update_interrupt = ali5237_usb_update_interrupt;
    dev->usb                         = device_add_parameters(&usb_device, &dev->usb_params);

    dev->type   = info->local & 0xff;
    dev->offset = (info->local >> 8) & 0x7f;
    if (info->local & 0x8000)
        dev->offset = -dev->offset;
    ali1543_log("Offset = %i\n", dev->offset);

    pci_enable_mirq(0);
    pci_enable_mirq(1);
    pci_enable_mirq(2);
    pci_enable_mirq(3);
    pci_enable_mirq(4);
    pci_enable_mirq(5);
    pci_enable_mirq(6);

    /* Super I/O chip */
    device_add(&ali5123_device);

    ali1543_reset(dev);

    return dev;
}

const device_t ali1543_device = {
    .name          = "ALi M1543 Desktop South Bridge",
    .internal_name = "ali1543",
    .flags         = DEVICE_PCI,
    .local         = 0x8500, /* -5 slot offset, we can do this because we currently
                                have no case of M1543 non-C with a different offset */
    .init  = ali1543_init,
    .close = ali1543_close,
    .reset = ali1543_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ali1543c_device = {
    .name          = "ALi M1543C Desktop South Bridge",
    .internal_name = "ali1543c",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = ali1543_init,
    .close         = ali1543_close,
    .reset         = ali1543_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
