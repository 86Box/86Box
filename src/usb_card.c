/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PCI USB host controller cards, chosen on the Ports page, and
 *          the OPTi 82C861 FireLink: one OHCI 1.0a controller with two
 *          root ports.
 *
 *          Written from:
 *          - OPTi FireLink 82C861 PCI-to-USB Bridge Data Book,
 *            912-2000-049, Revision 1.0, May 27, 1998.
 *          - OPTi 82C861/82C871 FireLink/FireBlast PCI-to-USB Bus
 *            Bridges (preliminary), 912-2000-022, Revision 2.0.
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

typedef struct opti861_t {
    usb_t  *usb;
    uint8_t conf[256];
    uint8_t slot;
    uint8_t irq_state;
    uint8_t int_level; /* the OHCI's INT output */
    uint8_t inta;      /* INTA# as driven */
} opti861_t;

/* The chip has one PCI interrupt pin, INTA# (data book s3.2.4, p. 11 /
   PDF 19; 3Dh = 01h). 51h[4:0] picks where the OHCI interrupt goes
   (Table 5-1, p. 21-22 / PDF 29-30). The default, 00001b = PCIRQ0#, is
   taken to be INTA#. Every other choice is either off or reaches the host
   only by OPTi's IRQ driveback cycle, which no 86Box chipset decodes, so
   INTA# stays high. */
static void
opti861_update_inta(opti861_t *dev)
{
    uint8_t level = dev->int_level && ((dev->conf[0x51] & 0x1f) == 0x01);

    if (level == dev->inta)
        return;

    dev->inta = level;
    if (level)
        pci_set_irq(dev->slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->slot, PCI_INTA, &dev->irq_state);
}

static void
opti861_irq(void *priv, int level)
{
    opti861_t *dev = (opti861_t *) priv;

    dev->int_level = !!level;
    opti861_update_inta(dev);
}

static void
opti861_update_bar(opti861_t *dev)
{
    ohci_update_mem_mapping(dev->usb, dev->conf[0x11], dev->conf[0x12], dev->conf[0x13], dev->conf[0x04] & 0x02);
    ohci_set_bus_master(dev->usb, dev->conf[0x04] & 0x04);
}

/* Defaults from Table 5-1 (data book p. 19-22 / PDF 27-30); every register
   not listed reads 0. */
static void
opti861_conf_reset(opti861_t *dev)
{
    memset(dev->conf, 0x00, sizeof(dev->conf));
    dev->conf[0x00] = 0x45; /* 1045h OPTi */
    dev->conf[0x01] = 0x10;
    dev->conf[0x02] = 0x61; /* C861h */
    dev->conf[0x03] = 0xc8;
    dev->conf[0x06] = 0x80; /* fast back-to-back capable */
    dev->conf[0x07] = 0x02; /* DEVSEL# medium */
    dev->conf[0x08] = 0x10;
    dev->conf[0x09] = 0x10; /* 0C0310h, OHCI */
    dev->conf[0x0a] = 0x03;
    dev->conf[0x0b] = 0x0c;
    dev->conf[0x3d] = 0x01;
    dev->conf[0x51] = 0x01;
    dev->conf[0x52] = 0x26;
    dev->conf[0x54] = 0x30;
    dev->conf[0x55] = 0x33;
    dev->conf[0x56] = 0x33;
    dev->conf[0x57] = 0x33;
}

static uint8_t
opti861_read(int func, int addr, UNUSED(int len), void *priv)
{
    const opti861_t *dev = (opti861_t *) priv;
    uint8_t          ret;

    if (func > 0)
        return 0xff;

    ret = dev->conf[addr];
    /* 4Eh bits 3 and 4 read back the I2C clock and data outputs, bits 1
       and 2 (Table 5-1, p. 21 / PDF 29). */
    if (addr == 0x4e)
        ret |= (ret & 0x06) << 2;

    return ret;
}

static void
opti861_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    opti861_t *dev = (opti861_t *) priv;

    if (func > 0)
        return;

    switch (addr) {
        case 0x04:
            /* I/O, Memory, Bus Master and PERR# response. */
            dev->conf[addr] = val & 0x47;
            opti861_update_bar(dev);
            break;
        case 0x05:
            dev->conf[addr] = val & 0x01;
            break;

        /* Plain read/write registers; Table 5-1 does not mark 3Dh
           read-only. */
        case 0x0c:
        case 0x0d:
        case 0x3c:
        case 0x3d:
        case 0x6c ... 0x6f:
            dev->conf[addr] = val;
            break;

        /* BAR0: a 4 KB memory window, bits 31:12 (Table 5-1, p. 20 /
           PDF 28). */
        case 0x11:
            dev->conf[addr] = val & 0xf0;
            opti861_update_bar(dev);
            break;
        case 0x12:
        case 0x13:
            dev->conf[addr] = val;
            opti861_update_bar(dev);
            break;

        /* Subsystem IDs. The data book's 2Ch-2Fh text says they are
           writable while 50h bit 3 is 0, and so does the 50h table of the
           preliminary 82C861/82C871 sheet (p. 21 / PDF 21). Only the data
           book's own 50h table says 1 = writable (both on p. 21 / PDF 29).
           The code follows the two that agree. */
        case 0x2c ... 0x2f:
            if (!(dev->conf[0x50] & 0x08))
                dev->conf[addr] = val;
            break;

        case 0x4e:
            dev->conf[addr] = val & 0x27;
            break;
        case 0x50:
            /* Bits 0 and 1 turn off the port I/O cells to save power (p. 21 /
               PDF 29); they do not detach a device, so they only latch. */
            dev->conf[addr] = val & 0x1f;
            break;
        case 0x51:
            dev->conf[addr] = val & 0xdf;
            opti861_update_inta(dev);
            break;
        case 0x52:
            dev->conf[addr] = val & 0x2f;
            break;
        case 0x54:
            dev->conf[addr] = val & 0xfc;
            break;
        case 0x55 ... 0x57:
            dev->conf[addr] = val;
            break;

        default:
            break;
    }
}

/* PCIRST#: configuration space to its defaults, so no window, no bus
   master and INTA# released. The OHCI resets with usb_device. */
static void
opti861_reset(void *priv)
{
    opti861_t *dev = (opti861_t *) priv;

    opti861_conf_reset(dev);
    opti861_update_bar(dev);
    opti861_update_inta(dev);
}

static void
opti861_close(void *priv)
{
    free(priv);
}

static void *
opti861_init(UNUSED(const device_t *info))
{
    opti861_t *dev = (opti861_t *) calloc(1, sizeof(opti861_t));

    dev->usb = device_add_inst(&usb_device, -1);
    /* No SMI handler: a PCI slot has no SMI#, IRQ1, IRQ12 or A20 line, so
       the chip's legacy pins (data book s3.2.4, p. 11 / PDF 19) reach
       nothing on a card. HcControl RWC is hardwired to 0 (p. 23 / PDF 31),
       HcLSThreshold resets to 0 and HcRhDescriptorA POTPGT is a read/write
       byte (p. 27 / PDF 35). */
    ohci_set_chip_flags(dev->usb, OHCI_CHIP_POTPGT_RW);
    ohci_set_irq_handler(dev->usb, opti861_irq, dev);

    opti861_conf_reset(dev);
    /* pci_add_card() marks every card strict, so a card only takes a slot
       of its own type; PCI_ADD_NORMAL is the type of an ordinary slot. */
    pci_add_card(PCI_ADD_NORMAL, opti861_read, opti861_write, dev, &dev->slot);

    return dev;
}

const device_t usb_opti_82c861_device = {
    .name          = "OPTi 82C861 FireLink",
    .internal_name = "opti_82c861",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = opti861_init,
    .close         = opti861_close,
    .reset         = opti861_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* The card list on the Ports settings page; entry 0 is none. */
static const device_t *const usb_card_devices[] = {
    &device_none,
    &usb_opti_82c861_device,
    &usb_via_vt83c572_device,
    NULL
};

int usb_card_type    = 0;
int usb_card_pending = -1;

int
usb_card_get_ndev(void)
{
    return (int) (sizeof(usb_card_devices) / sizeof(usb_card_devices[0])) - 1;
}

const device_t *
usb_card_get_device(int card)
{
    return usb_card_devices[card];
}

const char *
usb_card_get_internal_name(int card)
{
    return device_get_internal_name(usb_card_devices[card]);
}

int
usb_card_get_from_internal_name(const char *s)
{
    for (int c = 0; usb_card_devices[c] != NULL; c++) {
        if (!strcmp(usb_card_devices[c]->internal_name, s))
            return c;
    }

    return 0;
}

/* Whether machine mch gets a USB bus from a card: the card picked in the
   settings dialog (usb_card_pending, before it is saved) or else the saved
   one, if the machine can take it. */
int
usb_card_has_bus(int mch)
{
    int card = (usb_card_pending >= 0) ? usb_card_pending : usb_card_type;

    return (card > 0) && (card < usb_card_get_ndev()) && device_is_valid(usb_card_devices[card], mch);
}

/* Called on hard reset after the machine's own USB controllers exist and
   before any USB device attaches. */
void
usb_card_reset(void)
{
    if (usb_card_type != 0)
        device_add(usb_card_devices[usb_card_type]);
}
