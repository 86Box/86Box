/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the VIA VT82C505 VL/PCI Bridge Controller.
 *
 *
 *
 * Authors: Tiseno100,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Tiseno100.
 *          Copyright 2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/chipset.h>

typedef struct vt82c505_t {
    uint8_t index;
    uint8_t pci_slot;
    uint8_t pad;
    uint8_t pad0;

    uint8_t pci_conf[256];
} vt82c505_t;

static void
vt82c505_write(int func, int addr, uint8_t val, void *priv)
{
    vt82c505_t   *dev = (vt82c505_t *) priv;
    uint8_t       irq;
    const uint8_t irq_array[8] = { 0, 5, 9, 10, 11, 14, 15, 0 };

    if (func != 0)
        return;

    switch (addr) {
        /* RX00-07h: Mandatory header field */
        case 0x04:
            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0xbf) | (val & 0x40);
            break;
        case 0x07:
            dev->pci_conf[addr] &= ~(val & 0x90);
            break;

        /* RX80-9F: VT82C505 internal configuration registers */
        case 0x80:
            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x0f) | (val & 0xf0);
            break;
        case 0x81:
        case 0x84:
        case 0x85:
        case 0x87:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x8b:
        case 0x8c:
        case 0x8d:
        case 0x8e:
        case 0x8f:
        case 0x92:
        case 0x94:
            dev->pci_conf[addr] = val;
            break;
        case 0x82:
            dev->pci_conf[addr] = val & 0xdb;
            break;
        case 0x83:
            dev->pci_conf[addr] = val & 0xf9;
            break;
        case 0x86:
            dev->pci_conf[addr] = val & 0xef;
            /* Bit 7 switches between the two PCI configuration mechanisms:
               0 = configuration mechanism 1, 1 = configuration mechanism 2 */
            pci_set_pmc(!(val & 0x80));
            break;
        case 0x90:
            dev->pci_conf[addr] = val;
            irq                 = irq_array[val & 0x07];
            if ((val & 0x08) && (irq != 0))
                pci_set_irq_routing(PCI_INTC, irq);
            else
                pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);

            irq = irq_array[(val & 0x70) >> 4];
            if ((val & 0x80) && (irq != 0))
                pci_set_irq_routing(PCI_INTD, irq);
            else
                pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
            break;
        case 0x91:
            dev->pci_conf[addr] = val;
            irq                 = irq_array[val & 0x07];
            if ((val & 0x08) && (irq != 0))
                pci_set_irq_routing(PCI_INTA, irq);
            else
                pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);

            irq = irq_array[(val & 0x70) >> 4];
            if ((val & 0x80) && (irq != 0))
                pci_set_irq_routing(PCI_INTB, irq);
            else
                pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
            break;
        case 0x93:
            dev->pci_conf[addr] = val & 0xe0;
            break;

        default:
            break;
    }
}

static uint8_t
vt82c505_read(int func, int addr, void *priv)
{
    const vt82c505_t *dev = (vt82c505_t *) priv;
    uint8_t           ret = 0xff;

    if (func != 0)
        return ret;

    ret = dev->pci_conf[addr];

    return ret;
}

static void
vt82c505_out(uint16_t addr, uint8_t val, void *priv)
{
    vt82c505_t *dev = (vt82c505_t *) priv;

    if (addr == 0xa8)
        dev->index = val;
    else if ((addr == 0xa9) && (dev->index >= 0x80) && (dev->index <= 0x9f))
        vt82c505_write(0, dev->index, val, priv);
}

static uint8_t
vt82c505_in(uint16_t addr, void *priv)
{
    const vt82c505_t *dev = (vt82c505_t *) priv;
    uint8_t           ret = 0xff;

    if ((addr == 0xa9) && (dev->index >= 0x80) && (dev->index <= 0x9f))
        ret = vt82c505_read(0, dev->index, priv);

    return ret;
}

static void
vt82c505_reset(void *priv)
{
    vt82c505_t *dev = (vt82c505_t *) calloc(1, sizeof(vt82c505_t));

    dev->pci_conf[0x04] = 0x07;
    dev->pci_conf[0x07] = 0x00;

    for (uint8_t i = 0x80; i <= 0x9f; i++) {
        switch (i) {
            case 0x81:
                vt82c505_write(0, i, 0x01, priv);
                break;
            case 0x84:
                vt82c505_write(0, i, 0x03, priv);
                break;
            case 0x93:
                vt82c505_write(0, i, 0x40, priv);
                break;
            default:
                vt82c505_write(0, i, 0x00, priv);
                break;
        }
    }

    pic_reset();
    pic_set_pci_flag(1);
}

static void
vt82c505_close(void *priv)
{
    vt82c505_t *dev = (vt82c505_t *) priv;

    free(dev);
}

static void *
vt82c505_init(UNUSED(const device_t *info))
{
    vt82c505_t *dev = (vt82c505_t *) calloc(1, sizeof(vt82c505_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, vt82c505_read, vt82c505_write, dev, &dev->pci_slot);

    dev->pci_conf[0x00] = 0x06;
    dev->pci_conf[0x01] = 0x11;
    dev->pci_conf[0x02] = 0x05;
    dev->pci_conf[0x03] = 0x05;
    dev->pci_conf[0x04] = 0x07;
    dev->pci_conf[0x07] = 0x00;
    dev->pci_conf[0x81] = 0x01;
    dev->pci_conf[0x84] = 0x03;
    dev->pci_conf[0x93] = 0x40;

    io_sethandler(0x0a8, 0x0002, vt82c505_in, NULL, NULL, vt82c505_out, NULL, NULL, dev);

    return dev;
}

const device_t via_vt82c505_device = {
    .name          = "VIA VT82C505",
    .internal_name = "via_vt82c505",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = vt82c505_init,
    .close         = vt82c505_close,
    .reset         = vt82c505_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
