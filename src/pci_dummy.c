/* This can also serve as a sample PCI device. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/pci_dummy.h>

typedef struct
{
    uint8_t pci_regs[256];

    bar_t pci_bar[2];

    uint8_t card, interrupt_on;
} pci_dummy_t;

static void
pci_dummy_interrupt(int set, pci_dummy_t *dev)
{
    if (set)
        pci_set_irq(dev->card, PCI_INTA);
    else
        pci_clear_irq(dev->card, PCI_INTA);
}

static uint8_t
pci_dummy_read(uint16_t Port, void *p)
{
    pci_dummy_t *dev = (pci_dummy_t *) p;
    uint8_t      ret = 0xff;

    switch (Port & 0x20) {
        case 0x00:
            ret = 0x1a;
            break;
        case 0x01:
            ret = 0x07;
            break;
        case 0x02:
            ret = 0x0b;
            break;
        case 0x03:
            ret = 0xab;
            break;
        case 0x04:
            ret = dev->pci_regs[0x3c];
            break;
        case 0x05:
            ret = dev->pci_regs[0x3d];
            break;
        case 0x06:
            ret = dev->interrupt_on;
            if (dev->interrupt_on) {
                pci_dummy_interrupt(0, dev);
                dev->interrupt_on = 0;
            }
            break;
    }

    return ret;
}

static uint16_t
pci_dummy_readw(uint16_t Port, void *p)
{
    return pci_dummy_read(Port, p);
}

static uint32_t
pci_dummy_readl(uint16_t Port, void *p)
{
    return pci_dummy_read(Port, p);
}

static void
pci_dummy_write(uint16_t Port, uint8_t Val, void *p)
{
    pci_dummy_t *dev = (pci_dummy_t *) p;

    switch (Port & 0x20) {
        case 0x06:
            if (!dev->interrupt_on) {
                dev->interrupt_on = 1;
                pci_dummy_interrupt(1, dev);
            }
            break;
    }
}

static void
pci_dummy_writew(uint16_t Port, uint16_t Val, void *p)
{
    pci_dummy_write(Port, Val & 0xFF, p);
}

static void
pci_dummy_writel(uint16_t Port, uint32_t Val, void *p)
{
    pci_dummy_write(Port, Val & 0xFF, p);
}

static void
pci_dummy_io_remove(pci_dummy_t *dev)
{
    io_removehandler(dev->pci_bar[0].addr, 0x0020, pci_dummy_read, pci_dummy_readw, pci_dummy_readl, pci_dummy_write, pci_dummy_writew, pci_dummy_writel, dev);
}

static void
pci_dummy_io_set(pci_dummy_t *dev)
{
    io_sethandler(dev->pci_bar[0].addr, 0x0020, pci_dummy_read, pci_dummy_readw, pci_dummy_readl, pci_dummy_write, pci_dummy_writew, pci_dummy_writel, dev);
}

static uint8_t
pci_dummy_pci_read(int func, int addr, void *priv)
{
    pci_dummy_t *dev = (pci_dummy_t *) priv;
    uint8_t      ret = 0xff;

    if (func == 0x00)
        switch (addr) {
            case 0x00:
            case 0x2c:
                ret = 0x1a;
                break;
            case 0x01:
            case 0x2d:
                ret = 0x07;
                break;

            case 0x02:
            case 0x2e:
                ret = 0x0b;
                break;
            case 0x03:
            case 0x2f:
                ret = 0xab;
                break;

            case 0x04: /* PCI_COMMAND_LO */
            case 0x05: /* PCI_COMMAND_HI */
            case 0x06: /* PCI_STATUS_LO */
            case 0x07: /* PCI_STATUS_HI */
            case 0x0a:
            case 0x0b:
            case 0x3c: /* PCI_ILR */
                ret = dev->pci_regs[addr];
                break;

            case 0x08: /* Techncially, revision, but we return the card (slot) here. */
                ret = dev->card;
                break;

            case 0x10: /* PCI_BAR 7:5 */
                ret = (dev->pci_bar[0].addr_regs[0] & 0xe0) | 0x01;
                break;
            case 0x11: /* PCI_BAR 15:8 */
                ret = dev->pci_bar[0].addr_regs[1];
                break;

            case 0x3d: /* PCI_IPR */
                ret = PCI_INTA;
                break;

            default:
                ret = 0x00;
                break;
        }

    // pclog("AB0B:071A: PCI_Read(%d, %04X) = %02X\n", func, addr, ret);

    return ret;
}

static void
pci_dummy_pci_write(int func, int addr, uint8_t val, void *priv)
{
    pci_dummy_t *dev = (pci_dummy_t *) priv;
    uint8_t      valxor;

    // pclog("AB0B:071A: PCI_Write(%d, %04X, %02X)\n", func, addr, val);

    if (func == 0x00)
        switch (addr) {
            case 0x04: /* PCI_COMMAND_LO */
                valxor = (val & 0x03) ^ dev->pci_regs[addr];
                if (valxor & PCI_COMMAND_IO) {
                    pci_dummy_io_remove(dev);
                    if ((dev->pci_bar[0].addr != 0) && (val & PCI_COMMAND_IO))
                        pci_dummy_io_set(dev);
                }
                dev->pci_regs[addr] = val & 0x03;
                break;

            case 0x10:       /* PCI_BAR */
                val &= 0xe0; /* 0xe0 acc to RTL DS */
                             /*FALLTHROUGH*/

            case 0x11: /* PCI_BAR */
                /* Remove old I/O. */
                pci_dummy_io_remove(dev);

                /* Set new I/O as per PCI request. */
                dev->pci_bar[0].addr_regs[addr & 3] = val;

                /* Then let's calculate the new I/O base. */
                dev->pci_bar[0].addr &= 0xffe0;

                /* Log the new base. */
                // pclog("AB0B:071A: PCI: new I/O base is %04X\n", dev->pci_bar[0].addr);

                /* We're done, so get out of the here. */
                if (dev->pci_regs[4] & PCI_COMMAND_IO) {
                    if ((dev->pci_bar[0].addr) != 0)
                        pci_dummy_io_set(dev);
                }
                break;

            case 0x3c: /* PCI_ILR */
                pclog("AB0B:071A Device %02X: IRQ now: %i\n", dev->card, val);
                dev->pci_regs[addr] = val;
                return;
        }
}

static void
pci_dummy_reset(void *priv)
{
    pci_dummy_t *dev = (pci_dummy_t *) priv;

    /* Lower the IRQ. */
    pci_dummy_interrupt(0, dev);

    /* Disable I/O and memory accesses. */
    pci_dummy_pci_write(0x00, 0x04, 0x00, dev);

    /* Zero all the registers. */
    memset(dev, 0x00, sizeof(pci_dummy_t));
}

static void
pci_dummy_close(void *priv)
{
    pci_dummy_t *dev = (pci_dummy_t *) priv;

    free(dev);
}

static void *
pci_dummy_card_init(const device_t *info)
{
    pci_dummy_t *dev = (pci_dummy_t *) calloc(1, sizeof(pci_dummy_t));

    dev->card = pci_add_card(PCI_ADD_NORMAL, pci_dummy_pci_read, pci_dummy_pci_write, dev);

    return dev;
}

const device_t pci_dummy_device = {
    .name          = "Dummy Device (PCI)",
    .internal_name = "pci_dummy",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = pci_dummy_card_init,
    .close         = pci_dummy_close,
    .reset         = pci_dummy_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

void
pci_dummy_init(int min_slot, int max_slot, int nb_slot, int sb_slot)
{
    int i = 0, j = 1;

    for (i = min_slot; i <= max_slot; i++) {
        if ((i != nb_slot) && (i != sb_slot)) {
            pci_register_slot(i, PCI_CARD_NORMAL, 1, 3, 2, 4);
            device_add_inst(&pci_dummy_device, j);
            j++;
        }
    }
}
