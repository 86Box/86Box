/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the OPTi 82C822 VESA Local Bus to PCI
 *          Bridge Interface.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2022 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/apm.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/hdc_ide.h>
#include <86box/hdc.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include <86box/spd.h>

typedef struct opti822_t {
    uint8_t irq_convert;
    uint8_t pci_slot;
    uint8_t pad;
    uint8_t pad0;

    uint8_t pci_regs[256];
} opti822_t;

// #define ENABLE_OPTI822_LOG 1
#ifdef ENABLE_OPTI822_LOG
int opti822_do_log = ENABLE_OPTI822_LOG;

static void
opti822_log(const char *fmt, ...)
{
    va_list ap;

    if (opti822_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opti822_log(fmt, ...)
#endif

/* NOTE: We currently cheat and pass all PCI shadow RAM accesses to ISA as well.
         This is because we currently do not have separate access mappings for
         PCI and ISA at all. */
static void
opti822_recalc(opti822_t *dev)
{
    int      reg;
    int      bit_r;
    int      bit_w;
    int      state;
    uint32_t base;

    for (uint8_t i = 0; i < 12; i++) {
        base  = 0x000c0000 + (i << 14);
        reg   = 0x44 + ((i >> 2) ^ 3);
        bit_w = (i & 3);
        bit_r = bit_w + 4;
        bit_w = 1 << bit_w;
        bit_r = 1 << bit_r;
        state = (dev->pci_regs[reg] & bit_w) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
        state |= (dev->pci_regs[reg] & bit_r) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
        mem_set_mem_state_bus_both(base, 0x00004000, state);
    }

    state = (dev->pci_regs[0x44] & 0x01) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
    state |= (dev->pci_regs[0x44] & 0x02) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    mem_set_mem_state_bus_both(0x000f0000, 0x00010000, state);
}

/* NOTE: We cheat here. The real ALi M1435 uses a level to edge triggered IRQ converter
         when the most siginificant bit is set. We work around that by manipulating the
         emulated PIC's ELCR register. */
static void
opti822_update_irqs(opti822_t *dev, int set)
{
    uint8_t val;
    int     reg;
    int     shift;
    int     irq;
    int     irq_map[8] = { -1, 5, 9, 10, 11, 12, 14, 15 };
    pic_t  *temp_pic;

#if 0
    dev->irq_convert = (dev->pci_regs[0x53] & 0x08);
#endif
    dev->irq_convert = 1;

    for (uint8_t i = 0; i < 16; i++) {
        reg   = 0x88 + (i >> 1);
        shift = (i & 1) << 2;
        val   = (dev->pci_regs[reg] >> shift) & 0x0f;
        irq   = irq_map[val & 0x07];
        if (irq == -1)
            continue;
        temp_pic = (irq >= 8) ? &pic2 : &pic;
        irq &= 7;
        if (dev->irq_convert && set && (val & 0x08))
            temp_pic->elcr |= (1 << irq);
        else
            temp_pic->elcr &= ~(1 << irq);
    }
}

static void
opti822_pci_write(int func, int addr, uint8_t val, void *priv)
{
    opti822_t *dev = (opti822_t *) priv;
    int        irq;
    int        irq_map[8] = { -1, 5, 9, 10, 11, 12, 14, 15 };
    int        pin;
    int        slot;

    opti822_log("opti822_write(%02X, %02X, %02X)\n", func, addr, val);

    if (func > 0)
        return;

    switch (addr) {
        /* Command Register */
        case 0x04:
            dev->pci_regs[addr] = (val & 0x40) | 0x07;
            break;

        /* Status Register */
        case 0x06:
            if (!(dev->pci_regs[0x52] & 0x04))
                dev->pci_regs[addr] = (val & 0x80);
            break;
        case 0x07:
            dev->pci_regs[addr] &= ~(val & 0xf9);
            break;

        /* Master Latency Timer Register */
        case 0x0d:
            dev->pci_regs[addr] = val;
            break;

        case 0x40:
            dev->pci_regs[addr] = (val & 0xc0) | 0x01;
            break;
        case 0x41:
            /* TODO: Bit 15th enable the PCI Bridge when 1. */
            dev->pci_regs[addr] = val & 0xcf;
            break;

        case 0x42:
            dev->pci_regs[addr] = val & 0xf8;
            break;
        case 0x43:
            dev->pci_regs[addr] = val;
            break;

        case 0x44:
            dev->pci_regs[addr] = val & 0xcb;
            opti822_recalc(dev);
            break;
        case 0x45 ... 0x47:
            dev->pci_regs[addr] = val;
            opti822_recalc(dev);
            break;

        /* Memory hole stuff. */
        case 0x48 ... 0x51:
            dev->pci_regs[addr] = val;
            break;

        case 0x52:
            dev->pci_regs[addr] = val;
            break;

        case 0x53:
            dev->pci_regs[addr] = val;
            opti822_update_irqs(dev, 0);
            opti822_update_irqs(dev, 1);
            break;

        case 0x54 ... 0x57:
            dev->pci_regs[addr] = val;
            break;

        case 0x58:
            dev->pci_regs[addr] = val & 0xfc;
            break;
        case 0x59 ... 0x5b:
            dev->pci_regs[addr] = val;
            break;

        case 0x5c ... 0x5f:
            dev->pci_regs[addr] = val;
            break;

        case 0x60:
            dev->pci_regs[addr] = val & 0xfc;
            break;
        case 0x61 ... 0x63:
            dev->pci_regs[addr] = val;
            break;

        case 0x64 ... 0x67:
            dev->pci_regs[addr] = val;
            break;

        case 0x68:
            dev->pci_regs[addr] = val & 0xfc;
            break;
        case 0x69 ... 0x6b:
            dev->pci_regs[addr] = val;
            break;

        case 0x6c ... 0x6f:
            dev->pci_regs[addr] = val;
            break;

        case 0x70:
            dev->pci_regs[addr] = val & 0xfc;
            break;
        case 0x71 ... 0x73:
            dev->pci_regs[addr] = val;
            break;

        case 0x74:
            dev->pci_regs[addr] = val & 0xf8;
            break;

        /* ROMCS# and NVMCS# stuff. */
        case 0x75:
            dev->pci_regs[addr] = val;
            break;

        case 0x76:
            dev->pci_regs[addr] = val;
            break;

        case 0x77:
            dev->pci_regs[addr] = val;
            break;

        /* Enabling of memory blocks at ISA bus. */
        case 0x78:
            dev->pci_regs[addr] = val;
            break;
        case 0x79:
            dev->pci_regs[addr] = val & 0xfc;
            break;

        case 0x7a:
            dev->pci_regs[addr] = val;
            break;

        case 0x7b ... 0x7c:
            dev->pci_regs[addr] = val;
            break;

        case 0x7d ... 0x7e:
            dev->pci_regs[addr] = val;
            break;

        case 0x7f:
            dev->pci_regs[addr] = val & 0x03;
            break;

        case 0x80 ... 0x81:
            dev->pci_regs[addr] = val;
            break;
        case 0x82:
            dev->pci_regs[addr] = val;
            break;

        case 0x84 ... 0x85:
            dev->pci_regs[addr] = val;
            break;
        case 0x86:
            dev->pci_regs[addr] = val;
            break;

        case 0x88 ... 0x8f:
            dev->pci_regs[addr] = val;
            opti822_update_irqs(dev, 0);
            irq  = irq_map[val & 0x07];
            pin  = 4 - ((addr & 0x01) << 1);
            slot = ((addr & 0x06) >> 1);
            if (irq >= 0) {
                opti822_log("Set IRQ routing: INT %c%c -> %02X\n", pin + 0x40, slot + 0x31, irq);
                pci_set_irq_routing(pin + (slot << 2), irq);
                pci_set_irq_level(pin + (slot << 2), !!(val & 0x07));
            } else {
                opti822_log("Set IRQ routing: INT %c%c -> FF\n", pin + 0x40, slot + 0x31);
                pci_set_irq_routing(pin + (slot << 2), PCI_IRQ_DISABLED);
            }
            irq  = irq_map[(val >> 4) & 0x07];
            pin  = 3 - ((addr & 0x01) << 1);
            slot = ((addr & 0x06) >> 1);
            if (irq >= 0) {
                opti822_log("Set IRQ routing: INT %c%c -> %02X\n", pin + 0x40, slot + 0x31, irq);
                pci_set_irq_routing(pin + (slot << 2), irq);
                pci_set_irq_level(pin + (slot << 2), !!((val >> 4) & 0x07));
            } else {
                opti822_log("Set IRQ routing: INT %c%c -> FF\n", pin + 0x40, slot + 0x31);
                pci_set_irq_routing(pin + (slot << 2), PCI_IRQ_DISABLED);
            }
            opti822_update_irqs(dev, 1);
            break;

        default:
            break;
    }
}

static uint8_t
opti822_pci_read(int func, int addr, void *priv)
{
    const opti822_t *dev = (opti822_t *) priv;
    uint8_t          ret;

    ret = 0xff;

    if (func == 0)
        ret = dev->pci_regs[addr];

    opti822_log("opti822_read(%02X, %02X) = %02X\n", func, addr, ret);

    return ret;
}

static void
opti822_reset(void *priv)
{
    opti822_t *dev = (opti822_t *) priv;

    memset(dev->pci_regs, 0, 256);

    dev->pci_regs[0x00] = 0x45;
    dev->pci_regs[0x01] = 0x10; /*OPTi*/
    dev->pci_regs[0x02] = 0x22;
    dev->pci_regs[0x03] = 0xc8; /*82C822 PCIB*/
    dev->pci_regs[0x04] = 0x07;
    dev->pci_regs[0x06] = 0x80;
    dev->pci_regs[0x07] = 0x02;
    dev->pci_regs[0x08] = 0x01;
    dev->pci_regs[0x0b] = 0x06;
    dev->pci_regs[0x0d] = 0x20;

    dev->pci_regs[0x40] = 0x01;
    dev->pci_regs[0x41] = 0x0c;
    dev->pci_regs[0x43] = 0x02;
    dev->pci_regs[0x52] = 0x06;
    dev->pci_regs[0x53] = 0x90;

    dev->irq_convert = 1 /*0*/;

    for (uint8_t i = 0; i < 16; i++)
        pci_set_irq_routing(PCI_INTA + i, PCI_IRQ_DISABLED);
}

static void
opti822_close(void *priv)
{
    opti822_t *dev = (opti822_t *) priv;

    free(dev);
}

static void *
opti822_init(UNUSED(const device_t *info))
{
    opti822_t *dev = (opti822_t *) calloc(1, sizeof(opti822_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, opti822_pci_read, opti822_pci_write, dev, &dev->pci_slot);

    opti822_reset(dev);

    return dev;
}

const device_t opti822_device = {
    .name          = "OPTi 82C822 PCIB",
    .internal_name = "opti822",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = opti822_init,
    .close         = opti822_close,
    .reset         = opti822_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
