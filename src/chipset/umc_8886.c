/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the UMC 8886xx PCI to ISA Bridge .
 *
 * Note:    This chipset has no datasheet, everything were done via
 *          reverse engineering the BIOS of various machines using it.
 *
 *
 *
 * Authors: Tiseno100,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2021 Tiseno100.
 *          Copyright 2021 Miran Grca.
 */

/*
   UMC 8886xx Configuration Registers

   Note: PMU functionality is quite basic. There may be Enable/Disable bits, IRQ/SMI picks and it also
   required for 386_common.c to get patched in order to function properly.

   Warning: Register documentation may be inaccurate!

   UMC 8886xx:
   (F: Has No Internal IDE / AF or BF: Has Internal IDE)

   Function 0 Register 43:
   Bits 7-4 PCI IRQ for INTB
   Bits 3-0 PCI IRQ for INTA

   Function 0 Register 44:
   Bits 7-4 PCI IRQ for INTD
   Bits 3-0 PCI IRQ for INTC

   Function 0 Register 46 (corrected by Miran Grca):
   Bit 7: IRQ SMI Request (1: IRQ 15, 0: IRQ 10)
   Bit 6: PMU Trigger(1: By IRQ/0: By SMI)

   Function 0 Register 56:
   Bit 1-0 ISA Bus Speed
       0 0 PCICLK/3
       0 1 PCICLK/4
       1 0 PCICLK/2

   Function 0 Register A2 - non-software SMI# status register
                            (documented by Miran Grca):
   Bit 4: I set, graphics card goes into sleep mode
   This register is most likely R/WC

   Function 0 Register A3 (added more details by Miran Grca):
   Bit 7: Unlock SMM
   Bit 6: Software SMI trigger (also doubles as software SMI# status register,
          cleared by writing a 0 to it - see the handler used by Phoenix BIOS'es):
          If Function 0 Register 46 Bit 6 is set, it raises the specified IRQ (15
          or 10) instead.

   Function 0 Register A4:
   Bit 0: Host to PCI Clock (1: 1 by 1/0: 1 by half)

   Function 1 Register 4: (UMC 8886AF/8886BF Only!)
   Bit 0: Enable Internal IDE
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
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/machine.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#ifdef ENABLE_UMC_8886_LOG
int umc_8886_do_log = ENABLE_UMC_8886_LOG;

static void
umc_8886_log(const char *fmt, ...)
{
    va_list ap;

    if (umc_8886_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define umc_8886_log(fmt, ...)
#endif

typedef struct umc_8886_t {
    uint8_t  max_func;            /* Last function number */
    uint8_t  pci_slot;
    uint8_t  pad;
    uint8_t  pad0;

    uint8_t  pci_conf_sb[2][256]; /* PCI Registers */

    uint16_t sb_id;               /* Southbridge Revision */
    uint16_t ide_id;              /* IDE Revision */

    int      has_ide;             /* Check if Southbridge Revision is F, AF, or BF */
} umc_8886_t;

static void
umc_8886_ide_handler(umc_8886_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();

    if (dev->pci_conf_sb[1][0x04] & 0x01) {
        if (dev->pci_conf_sb[1][0x41] & 0x80)
            ide_pri_enable();

        if (dev->pci_conf_sb[1][0x41] & 0x40)
            ide_sec_enable();
    }
}

static void
umc_8886_bus_recalc(umc_8886_t *dev)
{
    switch (dev->pci_conf_sb[0x00][0xa4] & 0x03) {
        case 0x00:
            cpu_set_pci_speed(cpu_busspeed / 2);
            break;
        case 0x01:
            cpu_set_pci_speed(cpu_busspeed);
            break;
        case 0x02:
            cpu_set_pci_speed((cpu_busspeed * 2) / 3);
            break;
    }

    switch (dev->pci_conf_sb[0x00][0x56] & 0x03) {
        default:
            break;
        case 0x00:
            cpu_set_isa_pci_div(3);
            break;
        case 0x01:
            cpu_set_isa_pci_div(4);
            break;
        case 0x02:
            cpu_set_isa_pci_div(2);
            break;
    }
}

static void
umc_8886_irq_recalc(umc_8886_t *dev)
{
    int irq_routing;
    uint8_t *conf = dev->pci_conf_sb[0];

    irq_routing = (conf[0x46] & 0x01) ? (conf[0x43] >> 4) : PCI_IRQ_DISABLED;
    pci_set_irq_routing(PCI_INTA, irq_routing);
    irq_routing = (conf[0x46] & 0x02) ? (conf[0x43] & 0x0f) : PCI_IRQ_DISABLED;
    pci_set_irq_routing(PCI_INTB, irq_routing);

    irq_routing = (conf[0x46] & 0x04) ? (conf[0x44] >> 4) : PCI_IRQ_DISABLED;
    pci_set_irq_routing(PCI_INTC, irq_routing);
    irq_routing = (conf[0x46] & 0x08) ? (conf[0x44] & 0x0f) : PCI_IRQ_DISABLED;
    pci_set_irq_routing(PCI_INTD, irq_routing);

    pci_set_irq_level(PCI_INTA, (conf[0x47] & 0x01));
    pci_set_irq_level(PCI_INTB, (conf[0x47] & 0x02));
    pci_set_irq_level(PCI_INTC, (conf[0x47] & 0x04));
    pci_set_irq_level(PCI_INTD, (conf[0x47] & 0x08));
}

static void
umc_8886_write(int func, int addr, uint8_t val, void *priv)
{
    umc_8886_t *dev = (umc_8886_t *) priv;

    if (func <= dev->max_func)
        switch (func) {
            case 0: /* PCI to ISA Bridge */
                umc_8886_log("UM8886: dev->regs[%02x] = %02x POST %02x\n", addr, val, inb(0x80));

                switch (addr) {
                    case 0x04 ... 0x05:
                    case 0x0c ... 0x0d:
                    case 0x40 ... 0x42:
                    case 0x45:
                    case 0x50 ... 0x55:
                    case 0x57:
                    case 0x70 ... 0x76:
                    case 0x80 ... 0x83:
                    case 0x90 ... 0x92:
                    case 0xa0 ... 0xa1:
                    case 0xa5 ... 0xa8:
                        dev->pci_conf_sb[func][addr] = val;
                        break;

                    case 0x07:
                        dev->pci_conf_sb[func][addr] &= ~(val & 0xf9);
                        break;

                    case 0x43:
                    case 0x44:
                    case 0x46:    /* Bits 3-0 = 0 = IRQ disabled, 1 = IRQ enabled. */
                    case 0x47:    /* Bits 3-0 = 0 = IRQ edge-triggered, 1 = IRQ level-triggered. */
                        /* Bit 6 seems to be the IRQ/SMI# toggle, 1 = IRQ, 0 = SMI#. */
                        dev->pci_conf_sb[func][addr] = val;
                        umc_8886_irq_recalc(dev);
                        break;

                    case 0x56:
                        dev->pci_conf_sb[func][addr] = val;
                        umc_8886_bus_recalc(dev);
                        break;

                    case 0xa2:
                        dev->pci_conf_sb[func][addr] &= ~val;
                        break;

                    case 0xa3:
                        /* SMI Provocation (Bit 7 Enable SMM + Bit 6 Software SMI) */
                        if (((val & 0xc0) == 0xc0) && !(dev->pci_conf_sb[0][0xa3] & 0x40)) {
                            if (dev->pci_conf_sb[0][0x46] & 0x40)
                                picint(1 << ((dev->pci_conf_sb[0][0x46] & 0x80) ? 15 : 10));
                            else
                                smi_raise();
                        }

                        dev->pci_conf_sb[func][addr] = val;
                        break;

                    case 0xa4:
                        dev->pci_conf_sb[func][addr] = val;
                        umc_8886_bus_recalc(dev);
                        break;

                    default:
                        break;
                }
                break;

            case 1: /* IDE Controller */
                umc_8886_log("UM8886-IDE: dev->regs[%02x] = %02x POST: %02x\n", addr, val, inb(0x80));

                switch (addr) {
                    case 0x04:
                        dev->pci_conf_sb[func][addr] = val;
                        if (dev->ide_id == 0x673a)
                            umc_8886_ide_handler(dev);
                        break;

                    case 0x07:
                        dev->pci_conf_sb[func][addr] &= ~(val & 0xf9);
                        break;

                    case 0x3c:
                    case 0x40:
                    case 0x42 ... 0x59:
                        if (dev->ide_id == 0x673a)
                            dev->pci_conf_sb[func][addr] = val;
                        break;

                    case 0x41:
                        if (dev->ide_id == 0x673a) {
                            dev->pci_conf_sb[func][addr] = val;
                            umc_8886_ide_handler(dev);
                        }
                        break;

                    default:
                        break;
                }
                break;
            default:
                break;
        }
}

static uint8_t
umc_8886_read(int func, int addr, void *priv)
{
    const umc_8886_t *dev = (umc_8886_t *) priv;
    uint8_t           ret = 0xff;

    if (func <= dev->max_func)
        ret = dev->pci_conf_sb[func][addr];

    return ret;
}

static void
umc_8886_reset(void *priv)
{
    umc_8886_t *dev = (umc_8886_t *) priv;

    memset(dev->pci_conf_sb[0], 0x00, sizeof(dev->pci_conf_sb[0]));
    memset(dev->pci_conf_sb[1], 0x00, sizeof(dev->pci_conf_sb[1]));

    dev->pci_conf_sb[0][0x00] = 0x60; /* UMC */
    dev->pci_conf_sb[0][0x01] = 0x10;
    dev->pci_conf_sb[0][0x02] = (dev->sb_id & 0xff); /* 8886xx */
    dev->pci_conf_sb[0][0x03] = ((dev->sb_id >> 8) & 0xff);
    dev->pci_conf_sb[0][0x04] = 0x0f;
    dev->pci_conf_sb[0][0x07] = 0x02;
    dev->pci_conf_sb[0][0x08] = 0x0e;
    dev->pci_conf_sb[0][0x09] = 0x00;
    dev->pci_conf_sb[0][0x0a] = 0x01;
    dev->pci_conf_sb[0][0x0b] = 0x06;

    dev->pci_conf_sb[0][0x40] = 0x01;
    dev->pci_conf_sb[0][0x41] = 0x04;
    dev->pci_conf_sb[0][0x42] = 0x08;
    dev->pci_conf_sb[0][0x43] = 0x9a;
    dev->pci_conf_sb[0][0x44] = 0xbc;
    dev->pci_conf_sb[0][0x45] = 0x00;
    dev->pci_conf_sb[0][0x46] = 0x10;
    dev->pci_conf_sb[0][0x47] = 0x30;

    dev->pci_conf_sb[0][0x51] = 0x02;

    if (dev->has_ide) {
        dev->pci_conf_sb[1][0x00] = 0x60; /* UMC */
        dev->pci_conf_sb[1][0x01] = 0x10;
        dev->pci_conf_sb[1][0x02] = (dev->ide_id & 0xff); /* 8886xx IDE */
        dev->pci_conf_sb[1][0x03] = ((dev->ide_id >> 8) & 0xff);
        dev->pci_conf_sb[1][0x04] = 0x05; /* Start with Internal IDE Enabled */
        dev->pci_conf_sb[1][0x08] = 0x10;
        dev->pci_conf_sb[1][0x09] = 0x8f;
        dev->pci_conf_sb[1][0x0a] = dev->pci_conf_sb[1][0x0b] = 0x01;
        dev->pci_conf_sb[1][0x10] = 0xf1;
        dev->pci_conf_sb[1][0x11] = 0x01;
        dev->pci_conf_sb[1][0x14] = 0xf5;
        dev->pci_conf_sb[1][0x15] = 0x03;
        dev->pci_conf_sb[1][0x18] = 0x71;
        dev->pci_conf_sb[1][0x19] = 0x01;
        dev->pci_conf_sb[1][0x1c] = 0x75;
        dev->pci_conf_sb[1][0x1d] = 0x03;
        dev->pci_conf_sb[1][0x20] = 0x01;
        dev->pci_conf_sb[1][0x21] = 0x10;

        if (dev->ide_id == 0x673a) {
            dev->pci_conf_sb[1][0x40] = 0x00;
            dev->pci_conf_sb[1][0x41] = 0xc0;
            dev->pci_conf_sb[1][0x42] = dev->pci_conf_sb[1][0x43] = 0x00;
            dev->pci_conf_sb[1][0x44] = dev->pci_conf_sb[1][0x45] = 0x00;
            dev->pci_conf_sb[1][0x46] = dev->pci_conf_sb[1][0x47] = 0x00;
            dev->pci_conf_sb[1][0x48] = dev->pci_conf_sb[1][0x49] = 0x55;
            dev->pci_conf_sb[1][0x4a] = dev->pci_conf_sb[1][0x4b] = 0x55;
            dev->pci_conf_sb[1][0x4c] = dev->pci_conf_sb[1][0x4d] = 0x88;
            dev->pci_conf_sb[1][0x4e] = dev->pci_conf_sb[1][0x4f] = 0xaa;
            dev->pci_conf_sb[1][0x54] = dev->pci_conf_sb[1][0x55] = 0x00;
            dev->pci_conf_sb[1][0x56] = dev->pci_conf_sb[1][0x57] = 0x00;
            dev->pci_conf_sb[1][0x58] = dev->pci_conf_sb[1][0x59] = 0x00;

            umc_8886_ide_handler(dev);

            picintc(1 << 14);
            picintc(1 << 15);
        }
    }

    for (uint8_t i = 1; i < 5; i++) /* Disable all IRQ interrupts */
        pci_set_irq_routing(i, PCI_IRQ_DISABLED);

    umc_8886_bus_recalc(dev);
}

static void
umc_8886_close(void *priv)
{
    umc_8886_t *dev = (umc_8886_t *) priv;

    free(dev);
}

static void *
umc_8886_init(const device_t *info)
{
    umc_8886_t *dev = (umc_8886_t *) calloc(1, sizeof(umc_8886_t));

    /* Device 12: UMC 8886xx */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, umc_8886_read, umc_8886_write, dev, &dev->pci_slot);

    /* Get the Southbridge Revision */
    dev->sb_id = info->local & 0xffff;

    /* IDE Revision */
    dev->ide_id = info->local >> 16;

    dev->has_ide = (dev->ide_id != 0x0000);

    dev->max_func = 0;

    /* Add IDE if this is the UM8886AF or UM8886BF. */
    if (dev->ide_id == 0x673a) {
        /* UM8886BF */
        device_add(&ide_pci_2ch_device);
        dev->max_func = 1;
    } else if (dev->ide_id == 0x1001) {
        /* UM8886AF */
        device_add(&ide_um8673f_device);
    }

    umc_8886_reset(dev);

    return dev;
}

const device_t umc_8886f_device = {
    .name          = "UMC 8886F",
    .internal_name = "umc_8886f",
    .flags         = DEVICE_PCI,
    .local         = 0x00008886,
    .init          = umc_8886_init,
    .close         = umc_8886_close,
    .reset         = umc_8886_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t umc_8886af_device = {
    .name          = "UMC 8886AF",
    .internal_name = "umc_8886af",
    .flags         = DEVICE_PCI,
    .local         = 0x1001886a,
    .init          = umc_8886_init,
    .close         = umc_8886_close,
    .reset         = umc_8886_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t umc_8886bf_device = {
    .name          = "UMC 8886BF",
    .internal_name = "umc_8886bf",
    .flags         = DEVICE_PCI,
    .local         = 0x673a888a,
    .init          = umc_8886_init,
    .close         = umc_8886_close,
    .reset         = umc_8886_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
