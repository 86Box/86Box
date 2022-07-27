/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C822 VESA Local Bus to PCI Bridge Interface.
 *
 *
 * Authors:	Tiseno100,
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
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>

#include <86box/mem.h>
#include <86box/pci.h>

#include <86box/chipset.h>

/* Shadow RAM */
#define SYSTEM_READ ((dev->pci_conf[0x44] & 2) ? MEM_READ_INTERNAL : MEM_READ_EXTANY)
#define SYSTEM_WRITE ((dev->pci_conf[0x44] & 1) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY)
#define SHADOW_READ ((dev->pci_conf[cur_reg] & (1 << (4 + i))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY)
#define SHADOW_WRITE ((dev->pci_conf[cur_reg] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY)

#ifdef ENABLE_OPTI822_LOG
int opti822_do_log = ENABLE_OPTI822_LOG;
static void
opti822_log(const char *fmt, ...)
{
    va_list ap;

    if (opti822_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define opti822_log(fmt, ...)
#endif

typedef struct opti822_t
{
    uint8_t pci_conf[256];
} opti822_t;

int opti822_irq_routing[7] = {5, 9, 0x0a, 0x0b, 0x0c, 0x0e, 0x0f};

void opti822_shadow(int cur_reg, opti822_t *dev)
{
    if (cur_reg == 0x44)
        mem_set_mem_state_both(0xf0000, 0x10000, SYSTEM_READ | SYSTEM_WRITE);
    else
        for (int i = 0; i < 4; i++)
            mem_set_mem_state_both(0xe0000 - (((cur_reg & 3) - 1) << 16) + (i << 14), 0x4000, SHADOW_READ | SHADOW_WRITE);

    flushmmucache_nopc();
}

static void
opti822_write(int func, int addr, uint8_t val, void *priv)
{

    opti822_t *dev = (opti822_t *)priv;

    switch (func)
    {
    case 0x04: /* Command Register */
        dev->pci_conf[addr] = val & 0x40;
        break;

    case 0x05: /* Command Register */
        dev->pci_conf[addr] = val & 1;
        break;

    case 0x06: /* Status Register */
        dev->pci_conf[addr] |= val & 0xc0;
        break;

    case 0x07: /* Status Register */
        dev->pci_conf[addr] = val & 0xa9;
        break;

    case 0x40:
        dev->pci_conf[addr] = val & 0xc0;
        break;

    case 0x41:
        dev->pci_conf[addr] = val & 0xcf;
        break;

    case 0x42:
        dev->pci_conf[addr] = val & 0xf8;
        break;

    case 0x43:
        dev->pci_conf[addr] = val;
        break;

    case 0x44: /* Shadow RAM */
    case 0x45:
    case 0x46:
    case 0x47:
        dev->pci_conf[addr] = (addr == 0x44) ? (val & 0xcb) : val;
        opti822_shadow(addr, dev);
        break;

    case 0x48:
    case 0x49:
    case 0x4a:
    case 0x4b:
    case 0x4c:
    case 0x4d:
    case 0x4e:
    case 0x4f:
    case 0x50:
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57:
        dev->pci_conf[addr] = val;
        break;

    case 0x58:
        dev->pci_conf[addr] = val & 0xfc;
        break;

    case 0x59:
    case 0x5a:
    case 0x5b:
    case 0x5c:
    case 0x5d:
    case 0x5e:
    case 0x5f:
        dev->pci_conf[addr] = val;
        break;

    case 0x60:
        dev->pci_conf[addr] = val & 0xfc;
        break;

    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
        dev->pci_conf[addr] = val;
        break;

    case 0x68:
        dev->pci_conf[addr] = val & 0xfc;
        break;

    case 0x69:
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6d:
    case 0x6e:
    case 0x6f:
        dev->pci_conf[addr] = val;
        break;

    case 0x70:
        dev->pci_conf[addr] = val & 0xfc;
        break;

    case 0x71:
    case 0x72:
    case 0x73:
        dev->pci_conf[addr] = val;
        break;

    case 0x74:
        dev->pci_conf[addr] = val & 0xfc;
        break;

    case 0x75:
    case 0x76:
        dev->pci_conf[addr] = val;
        break;

    case 0x77:
        dev->pci_conf[addr] = val & 0xe7;
        break;

    case 0x78:
        dev->pci_conf[addr] = val;
        break;

    case 0x79:
        dev->pci_conf[addr] = val & 0xfc;
        break;

    case 0x7a:
    case 0x7b:
    case 0x7c:
    case 0x7d:
    case 0x7e:
        dev->pci_conf[addr] = val;
        break;

    case 0x7f:
        dev->pci_conf[addr] = val & 3;
        break;

    case 0x80:
    case 0x81:
    case 0x82:
    case 0x84:
    case 0x85:
    case 0x86:
        dev->pci_conf[addr] = val;
        break;

    case 0x88: /* PCI IRQ Routing */
    case 0x89: /* Very hacky implementation. Needs surely a rewrite after */
    case 0x8a: /* a PCI rework happens. */
    case 0x8b:
    case 0x8c:
    case 0x8d:
    case 0x8e:
    case 0x8f:
        dev->pci_conf[addr] = val;
        if (addr % 2)
        {
            pci_set_irq_routing(PCI_INTB, ((val & 0x0f) != 0) ? opti822_irq_routing[(val & 7) - 1] : PCI_IRQ_DISABLED);
            pci_set_irq_routing(PCI_INTA, (((val >> 4) & 0x0f) != 0) ? opti822_irq_routing[((val >> 4) & 7) - 1] : PCI_IRQ_DISABLED);
        }
        else
        {
            pci_set_irq_routing(PCI_INTD, ((val & 0x0f) != 0) ? opti822_irq_routing[(val & 7) - 1] : PCI_IRQ_DISABLED);
            pci_set_irq_routing(PCI_INTC, (((val >> 4) & 0x0f) != 0) ? opti822_irq_routing[((val >> 4) & 7) - 1] : PCI_IRQ_DISABLED);
        }
        break;
    }

    opti822_log("OPTI822: dev->pci_conf[%02x] = %02x\n", addr, dev->pci_conf[addr]);
}

static uint8_t
opti822_read(int func, int addr, void *priv)
{
    opti822_t *dev = (opti822_t *)priv;
    return dev->pci_conf[addr];
}

static void
opti822_reset(void *priv)
{
    opti822_t *dev = (opti822_t *)priv;

    dev->pci_conf[0x00] = 0x45;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x22;
    dev->pci_conf[0x03] = 0xc8;
    dev->pci_conf[0x04] = 7;
    dev->pci_conf[0x06] = 0x40;
    dev->pci_conf[0x07] = 1;
    dev->pci_conf[0x08] = 1;
    dev->pci_conf[0x0b] = 6;
    dev->pci_conf[0x0d] = 0x20;
    dev->pci_conf[0x40] = 1;
    dev->pci_conf[0x43] = 0x20;
    dev->pci_conf[0x52] = 6;
    dev->pci_conf[0x53] = 0x90;
}

static void
opti822_close(void *priv)
{
    opti822_t *dev = (opti822_t *)priv;

    free(dev);
}

static void *
opti822_init(const device_t *info)
{
    opti822_t *dev = (opti822_t *)malloc(sizeof(opti822_t));
    memset(dev, 0, sizeof(opti822_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, opti822_read, opti822_write, dev);

    opti822_reset(dev);

    return dev;
}

const device_t opti822_device = {
    .name = "OPTi 82C822 PCIB",
    .internal_name = "opti822",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = opti822_init,
    .close = opti822_close,
    .reset = opti822_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
