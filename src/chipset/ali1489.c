/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M1489 chipset.
 *
 *
 *
 * Authors: Tiseno100,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2021 Tiseno100.
 *          Copyright 2020-2021 Miran Grca.
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

#include <86box/hdc_ide.h>
#include <86box/hdc.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/smram.h>

#include <86box/chipset.h>

#define DEFINE_SHADOW_PROCEDURE (((dev->regs[0x14] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | \
                                ((dev->regs[0x14] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY))
#define DISABLED_SHADOW         (MEM_READ_EXTANY | MEM_WRITE_EXTANY)

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
#    define ali1489_log(fmt, ...)
#endif

typedef struct ali1489_t {
    uint8_t index;
    uint8_t pci_slot;
    uint8_t regs[256];
    uint8_t pci_conf[256];

    port_92_t *port_92;
    smram_t   *smram;
} ali1489_t;

static void
ali1489_shadow_recalc(ali1489_t *dev)
{
    shadowbios = shadowbios_write = 0;

    for (uint8_t i = 0; i < 8; i++) {
        if (dev->regs[0x13] & (1 << i)) {
            ali1489_log("%06Xh-%06Xh region shadow enabled: read = %i, write = %i\n",
                        0xc0000 + (i << 14), 0xc3fff + (i << 14),
                        !!(dev->regs[0x14] & 0x10), !!(dev->regs[0x14] & 0x20));
            mem_set_mem_state_both(0xc0000 + (i << 14), 0x4000, DEFINE_SHADOW_PROCEDURE);
        } else {
            ali1489_log("%06Xh-%06Xh region shadow disabled\n", 0xc0000 + (i << 14), 0xc3fff + (i << 14));
            mem_set_mem_state_both(0xc0000 + (i << 14), 0x4000, DISABLED_SHADOW);
        }
    }

    for (uint8_t i = 0; i < 4; i++) {
        if (dev->regs[0x14] & (1 << i)) {
            ali1489_log("%06Xh-%06Xh region shadow enabled: read = %i, write = %i\n",
                        0xe0000 + (i << 15), 0xe7fff + (i << 15),
                        !!(dev->regs[0x14] & 0x10), !!(dev->regs[0x14] & 0x20));
            mem_set_mem_state_both(0xe0000 + (i << 15), 0x8000, DEFINE_SHADOW_PROCEDURE);
            shadowbios |= !!(dev->regs[0x14] & 0x10);
            shadowbios_write |= !!(dev->regs[0x14] & 0x20);
        } else {
            ali1489_log("%06Xh-%06Xh region shadow disabled\n", 0xe0000 + (i << 15), 0xe7fff + (i << 15));
            mem_set_mem_state_both(0xe0000 + (i << 15), 0x8000, DISABLED_SHADOW);
        }
    }

    flushmmucache_nopc();
}

static void
ali1489_smram_recalc(ali1489_t *dev)
{
    /* The datasheet documents SMM behavior quite terribly.
       Everything were done according to the M1489 programming guide. */
    smram_disable(dev->smram);

    switch (dev->regs[0x19] & 0x30) {
        case 0x10:
            smram_enable(dev->smram, 0xa0000, 0xa0000, 0x20000, (dev->regs[0x19] & 0x08), 1);
            break;
        case 0x20:
            smram_enable(dev->smram, 0xe0000, 0xe0000, 0x10000, (dev->regs[0x19] & 0x08), 1);
            break;
        case 0x30:
            if ((dev->regs[0x35] & 0xc0) == 0x80)
                smram_enable(dev->smram, 0x68000, 0xa8000, 0x08000, (dev->regs[0x19] & 0x08), 1);
            else
                smram_enable(dev->smram, 0x38000, 0xa8000, 0x08000, (dev->regs[0x19] & 0x08), 1);
            break;
        default:
            break;
    }

    if ((dev->regs[0x19] & 0x31) == 0x11) {
        /* If SMRAM is enabled and bit 0 is set, code still goes to DRAM. */
        mem_set_mem_state_smram_ex(1, 0xa0000, 0x20000, 0x02);
    }
}

static void
ali1489_defaults(ali1489_t *dev)
{
    memset(dev->pci_conf, 0x00, 256);
    memset(dev->regs, 0x00, 256);

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
    dev->regs[0x15] = 0x20;
    dev->regs[0x16] = 0x30;
    dev->regs[0x19] = 0x04;
    dev->regs[0x21] = 0x72;
    dev->regs[0x28] = 0x02;
    dev->regs[0x2b] = 0xdb;
    dev->regs[0x3c] = 0x03;
    dev->regs[0x3d] = 0x01;
    dev->regs[0x40] = 0x03;

    ali1489_shadow_recalc(dev);
    cpu_cache_int_enabled = 0;
    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();
    ali1489_smram_recalc(dev);

    port_92_remove(dev->port_92);

    picintc(1 << 10);
    picintc(1 << 15);
    nmi      = 0;
    smi_line = 0;
    in_smm   = 0;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
}

static void
ali1489_write(uint16_t addr, uint8_t val, void *priv)
{
    ali1489_t    *dev = (ali1489_t *) priv;
    uint8_t       old;
    uint8_t       irq;
    const uint8_t irq_array[16] = { 0, 9, 3, 10, 4, 5, 7, 6, 0, 11, 0, 12, 0, 14, 0, 15 };

    switch (addr) {
        case 0x22:
            dev->index = val;
            break;
        case 0x23:
            /* Check if the configuration registers are unlocked */
            if (dev->regs[0x03] == 0xc5) {
                switch (dev->index) {
                    case 0x03: /* Lock Register */
                    case 0x10: /* DRAM Configuration Register I */
                    case 0x11: /* DRAM Configuration Register II */
                    case 0x12: /* ROM Function Register */
                        dev->regs[dev->index] = val;
                        break;

                    case 0x13: /* Shadow Region Register */
                    case 0x14: /* Shadow Control Register */
                        if (dev->index == 0x14)
                            dev->regs[dev->index] = (val & 0xbf);
                        else
                            dev->regs[dev->index] = val;

                        ali1489_shadow_recalc(dev);
                        ali1489_smram_recalc(dev);
                        break;

                    case 0x15: /* Cycle Check Point Control Register */
                        dev->regs[dev->index] = (val & 0xf1);
                        break;

                    case 0x16: /* Cache Control Register I */
                        dev->regs[dev->index] = val;
                        cpu_cache_int_enabled = (val & 0x01);
                        cpu_cache_ext_enabled = (val & 0x02);
                        cpu_update_waitstates();
                        break;
                    case 0x17: /* Cache Control Register II */
                        dev->regs[dev->index] = val;
                        break;

                    case 0x19: /* SMM Control Register */
                        dev->regs[dev->index] = val;
                        ali1489_smram_recalc(dev);
                        break;

                    case 0x1a: /* EDO DRAM Configuration Register */
                    case 0x1b: /* DRAM Timing Control Register */
                        dev->regs[dev->index] = val;
                        break;
                    case 0x1c: /* Memory Data Buffer Direction Control Register */
                        dev->regs[dev->index] = val & 0x1f;
                        break;

                    case 0x1e: /* Linear Wrapped Burst Order Mode Control Register */
                        dev->regs[dev->index] = (val & 0x40);
                        break;

                    case 0x20: /* CPU to PCI Buffer Control Register */
                        dev->regs[dev->index] = val;
                        break;
                    case 0x21: /* DEVSELJ Check Point Setting Register */
                        dev->regs[dev->index] = (val & 0xbb) | 0x04;
                        break;
                    case 0x22: /* PCI to CPU W/R Buffer Configuration Register */
                        dev->regs[dev->index] = (val & 0xfd);
                        break;

                    case 0x25: /* GP/MEM Address Definition Register I */
                    case 0x26: /* GP/MEM Address Definition Register II */
                    case 0x27: /* GP/MEM Address Definition Register III */
                        dev->regs[dev->index] = val;
                        break;
                    case 0x28: /* PCI Arbiter Control Register */
                        dev->regs[dev->index] = val & 0x3f;
                        break;

                    case 0x29: /* System Clock Register */
                        dev->regs[dev->index] = val;

                        port_92_remove(dev->port_92);
                        if (val & 0x10)
                            port_92_add(dev->port_92);
                        break;

                    case 0x2a: /* I/O Recovery Register */
                        dev->regs[dev->index] = val;
                        break;

                    case 0x2b: /* Turbo Function Register */
                        dev->regs[dev->index] = (val & 0xbf) | 0x40;
                        break;

                    case 0x30: /* Power Management Unit Control Register */
                        old                   = dev->regs[dev->index];
                        dev->regs[dev->index] = val;

                        if (((val & 0x14) == 0x14) && !(old & 0x08) && (val & 0x08)) {
                            switch (dev->regs[0x35] & 0x30) {
                                case 0x00:
                                    smi_raise();
                                    break;
                                case 0x10:
                                    nmi_raise();
                                    break;
                                case 0x20:
                                    picint(1 << 15);
                                    break;
                                case 0x30:
                                    picint(1 << 10);
                                    break;
                                default:
                                    break;
                            }
                            dev->regs[0x35] |= 0x0e;
                        } else if (!(val & 0x10))
                            dev->regs[0x35] &= ~0x0f;
                        break;

                    case 0x31: /* Mode Timer Monitoring Events Selection Register I */
                    case 0x32: /* Mode Timer Monitoring Events Selection Register II */
                    case 0x33: /* SMI Triggered Events Selection Register I */
                    case 0x34: /* SMI Triggered Events Selection Register II */
                        dev->regs[dev->index] = val;
                        break;

                    case 0x35: /* SMI Status Register */
                        dev->regs[dev->index] = (dev->regs[dev->index] & 0x0f) | (val & 0xf0);
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

                    case 0x40: /* Clock Generator Control Feature Register */
                        dev->regs[dev->index] = (val & 0x3f);
                        break;
                    case 0x41: /* Power Control Output Register */
                        dev->regs[dev->index] = val;
                        break;

                    case 0x42: /* PCI INTx Routing Table Mapping Register I */
                        irq = irq_array[val & 0x0f];
                        pci_set_irq_routing(PCI_INTA, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                        irq = irq_array[(val & 0xf0) >> 4];
                        pci_set_irq_routing(PCI_INTB, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                        break;

                    case 0x43: /* PCI INTx Routing Table Mapping Register II */
                        irq = irq_array[val & 0x0f];
                        pci_set_irq_routing(PCI_INTC, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                        irq = irq_array[(val & 0xf0) >> 4];
                        pci_set_irq_routing(PCI_INTD, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                        break;

                    case 0x44: /* PCI INTx Sensitivity Register */
                        /* TODO: When doing the IRQ and PCI IRQ rewrite,
                                 bits 0 to 3 toggle edge/level output. */
                        dev->regs[dev->index] = val;
                        break;
                    default:
                        break;
                }

                if (dev->index != 0x03) {
                    ali1489_log("M1489: dev->regs[%02x] = %02x\n", dev->index, val);
                }
            } else if (dev->index == 0x03)
                dev->regs[dev->index] = val;

            break;

        default:
            break;
    }
}

static uint8_t
ali1489_read(uint16_t addr, void *priv)
{
    uint8_t          ret = 0xff;
    const ali1489_t *dev = (ali1489_t *) priv;

    switch (addr) {
        case 0x23:
            /* Avoid conflict with Cyrix CPU registers */
            if (((dev->index == 0x20) || (dev->index >= 0xc0)) && cpu_iscyrix)
                ret = 0xff;
            else if (dev->index == 0x3f)
                ret = inb(0x70);
            else
                ret = dev->regs[dev->index];
            break;
        default:
            break;
    }

    ali1489_log("M1489: dev->regs[%02x] (%02x)\n", dev->index, ret);

    return ret;
}

static void
ali1489_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    ali1489_t *dev = (ali1489_t *) priv;

    ali1489_log("M1489-PCI: dev->pci_conf[%02x] = %02x\n", addr, val);

    switch (addr) {
        /* Dummy PCI Config */
        case 0x04:
            dev->pci_conf[0x04] = val & 0x7f;
            break;

        /* Dummy PCI Status */
        case 0x07:
            dev->pci_conf[0x07] &= ~(val & 0xb8);
            break;

        default:
            break;
    }
}

static uint8_t
ali1489_pci_read(UNUSED(int func), int addr, void *priv)
{
    const ali1489_t *dev = (ali1489_t *) priv;
    uint8_t          ret = 0xff;

    ret = dev->pci_conf[addr];
    ali1489_log("M1489-PCI: dev->pci_conf[%02x] (%02x)\n", addr, ret);
    return ret;
}

static void
ali1489_reset(void *priv)
{
    ali1489_t *dev = (ali1489_t *) priv;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    ali1489_defaults(dev);
}

static void
ali1489_close(void *priv)
{
    ali1489_t *dev = (ali1489_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
ali1489_init(UNUSED(const device_t *info))
{
    ali1489_t *dev = (ali1489_t *) calloc(1, sizeof(ali1489_t));

    /* M1487/M1489
       22h Index Port
       23h Data Port */
    io_sethandler(0x0022, 0x0002, ali1489_read, NULL, NULL, ali1489_write, NULL, NULL, dev);

    /* Dummy M1489 PCI device */
    pci_add_card(PCI_ADD_NORTHBRIDGE, ali1489_pci_read, ali1489_pci_write, dev, &dev->pci_slot);

    device_add(&ide_ali1489_device);

    dev->port_92 = device_add(&port_92_pci_device);
    dev->smram   = smram_add();

    ali1489_defaults(dev);

    return dev;
}

const device_t ali1489_device = {
    .name          = "ALi M1489",
    .internal_name = "ali1489",
    .flags         = 0,
    .local         = 0,
    .init          = ali1489_init,
    .close         = ali1489_close,
    .reset         = ali1489_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
