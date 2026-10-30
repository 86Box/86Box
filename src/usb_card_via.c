/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VIA VT83C572 PCI to USB controller on a PCI add-in card: one
 *          UHCI 1.1 function with two root ports, using the UHCI in usb.c.
 *
 *          Written from:
 *          - VIA VT83C572 PCI to USB Controller, Preliminary Release 0.65,
 *            January 1997 ("data sheet").
 *          - Universal Host Controller Interface (UHCI) Design Guide,
 *            Revision 1.1, March 1996.
 *
 * Authors: skiretic, <skiretic@proton.me>
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/usb.h>
#include <86box/plat_unused.h>

/* LEGSUP (C0h-C1h) bits 12 and 13, as bits of C1h (UHCI 1.1 s5.2.1,
   p. 39-40 / PDF 45-46). */
#define LEGSUP_USBIRQS    0x10
#define LEGSUP_USBPIRQDEN 0x20

typedef struct vt83c572_t {
    usb_t  *usb;
    uint8_t regs[256];
    uint8_t slot;
    uint8_t irq_level; /* the UHCI interrupt output */
    uint8_t inta;      /* INTA# as driven now */
    uint8_t irq_state;
} vt83c572_t;

/* USBIRQS follows the UHCI interrupt. The interrupt reaches INTA# only
   while LEGSUP USBPIRQDEN is set (UHCI 1.1 s5.2.1 bit 13, p. 40 / PDF 46,
   where the line is PIIX's PIRQD). USBSMIEN and the 60h/64h trap enables
   only drive SMI# (pin 51, data sheet p. 4 / PDF 6), which has no line on
   a PCI slot, so they are kept as register bits with no effect. */
static void
vt83c572_update_irq(vt83c572_t *dev)
{
    uint8_t inta;

    if (dev->irq_level)
        dev->regs[0xc1] |= LEGSUP_USBIRQS;
    else
        dev->regs[0xc1] &= ~LEGSUP_USBIRQS;

    inta = dev->irq_level && (dev->regs[0xc1] & LEGSUP_USBPIRQDEN);
    if (inta == dev->inta)
        return;
    dev->inta = inta;

    if (inta)
        pci_set_irq(dev->slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->slot, PCI_INTA, &dev->irq_state);
}

static void
vt83c572_irq(void *priv, int level)
{
    vt83c572_t *dev = (vt83c572_t *) priv;

    dev->irq_level = !!level;
    vt83c572_update_irq(dev);
}

static void
vt83c572_update_io(vt83c572_t *dev)
{
    uhci_update_io_mapping(dev->usb, dev->regs[0x20] & 0xe0, dev->regs[0x21], dev->regs[0x04] & PCI_COMMAND_IO);
    uhci_set_bus_master(dev->usb, dev->regs[0x04] & PCI_COMMAND_L_BM);
}

/* Configuration space at its defaults, as at power-on and on PCIRST#
   (data sheet p. 1-3 / PDF 3-5). The data sheet gives no Revision ID
   value; 00h is used. */
static void
vt83c572_reset_regs(vt83c572_t *dev)
{
    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x00] = 0x06; /* 1106h */
    dev->regs[0x01] = 0x11;
    dev->regs[0x02] = 0x38; /* 3038h */
    dev->regs[0x03] = 0x30;
    dev->regs[0x04] = 0x80; /* address stepping enabled */
    dev->regs[0x07] = 0x02; /* DEVSEL# medium */
    dev->regs[0x08] = 0x00;
    dev->regs[0x0a] = 0x03; /* 0C0300h */
    dev->regs[0x0b] = 0x0c;
    dev->regs[0x0d] = 0x16;
    dev->regs[0x20] = 0x01; /* I/O space indicator */
    dev->regs[0x3d] = 0x01; /* INTA# */
    dev->regs[0x60] = 0x10; /* USB 1.0 */
    dev->regs[0xc1] = 0x20; /* LEGSUP 2000h */

    vt83c572_update_io(dev);
    uhci_set_babble_keep_port(dev->usb, 0);
    vt83c572_update_irq(dev);
}

static uint8_t
vt83c572_read(int func, int addr, UNUSED(int len), void *priv)
{
    const vt83c572_t *dev = (vt83c572_t *) priv;

    if (func > 0)
        return 0xff;

    return dev->regs[addr & 0xff];
}

/* Read-only and reserved bytes keep their value. The data sheet marks
   00h-03h, 08h-0Bh, 0Eh, 0Fh and 60h fixed (p. 1-3 / PDF 3-5); 3Dh, for
   which it gives only a default, is kept read-only as PCI defines the
   Interrupt Pin register. The BAR decodes AD[15:5], 32 bytes of I/O;
   bits 31-16 are reserved. */
static void
vt83c572_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    vt83c572_t *dev = (vt83c572_t *) priv;

    if (func > 0)
        return;

    switch (addr & 0xff) {
        case 0x04:
            /* Bit 3 fixed at 0, bits 6-5 reserved; bit 1 latches but there
               is no memory BAR for it to enable. */
            dev->regs[0x04] = val & 0x97;
            vt83c572_update_io(dev);
            break;
        case 0x07:
            /* Status bits 14-11 are the PCI error flags; writing 1 clears
               one. */
            dev->regs[0x07] &= ~(val & 0x78);
            break;

        case 0x0c:
        case 0x0d:
        case 0x3c:
            dev->regs[addr & 0xff] = val;
            break;

        case 0x20:
            dev->regs[0x20] = (val & 0xe0) | 0x01;
            vt83c572_update_io(dev);
            break;
        case 0x21:
            dev->regs[0x21] = val;
            vt83c572_update_io(dev);
            break;

        case 0x40:
            /* Bit 4 reserved. Bit 6 set: a port babbling at EOF is not
               disabled (data sheet p. 2 / PDF 4). */
            dev->regs[0x40] = val & 0xef;
            uhci_set_babble_keep_port(dev->usb, val & 0x40);
            break;
        case 0x41:
            /* Bit 2 trap status option, bit 1 A20GATE pass-through option
               (p. 3 / PDF 5); both concern SMI# only. */
            dev->regs[0x41] = val & 0x06;
            break;

        case 0xc0:
            uhci_legsup_write_c0(&dev->regs[0xc0], val);
            break;
        case 0xc1:
            /* LEGSUP bits 15 and 11-8 are write-1-to-clear, bit 12 read-only,
               bit 14 reserved (UHCI 1.1 s5.2.1, p. 39-40 / PDF 45-46). */
            dev->regs[0xc1] = (dev->regs[0xc1] & 0x9f & ~(val & 0x8f)) | (val & 0x20);
            vt83c572_update_irq(dev);
            break;

        default:
            break;
    }
}

static void
vt83c572_reset(void *priv)
{
    vt83c572_reset_regs((vt83c572_t *) priv);
}

static void *
vt83c572_init(UNUSED(const device_t *info))
{
    vt83c572_t *dev = (vt83c572_t *) calloc(1, sizeof(vt83c572_t));

    dev->usb = device_add_inst(&usb_device, -1);
    uhci_set_irq_handler(dev->usb, vt83c572_irq, dev);

    pci_add_card(PCI_ADD_NORMAL, vt83c572_read, vt83c572_write, dev, &dev->slot);

    vt83c572_reset_regs(dev);

    return dev;
}

static void
vt83c572_close(void *priv)
{
    free(priv);
}

const device_t usb_via_vt83c572_device = {
    .name          = "VIA VT83C572 PCI to USB Controller",
    .internal_name = "via_vt83c572",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = vt83c572_init,
    .close         = vt83c572_close,
    .reset         = vt83c572_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
