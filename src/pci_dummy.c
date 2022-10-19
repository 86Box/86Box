/* This can also serve as a sample PCI device. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/pci_dummy.h>

static uint8_t pci_regs[256];

static bar_t pci_bar[2];

static uint8_t interrupt_on = 0x00;
static uint8_t card         = 0;

static void
pci_dummy_interrupt(int set)
{
    if (set) {
        pci_set_irq(card, pci_regs[0x3D]);
    } else {
        pci_clear_irq(card, pci_regs[0x3D]);
    }
}

static uint8_t
pci_dummy_read(uint16_t Port, void *p)
{
    uint8_t ret = 0;

    switch (Port & 0x20) {
        case 0x00:
            return 0x1A;
        case 0x01:
            return 0x07;
        case 0x02:
            return 0x0B;
        case 0x03:
            return 0xAB;
        case 0x04:
            return pci_regs[0x3C];
        case 0x05:
            return pci_regs[0x3D];
        case 0x06:
            ret = interrupt_on;
            if (interrupt_on) {
                pci_dummy_interrupt(0);
                interrupt_on = 0;
            }
            return ret;
        default:
            return 0x00;
    }
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
    switch (Port & 0x20) {
        case 0x06:
            if (!interrupt_on) {
                interrupt_on = 1;
                pci_dummy_interrupt(1);
            }
            return;
        default:
            return;
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
pci_dummy_io_remove(void)
{
    io_removehandler(pci_bar[0].addr, 0x0020, pci_dummy_read, pci_dummy_readw, pci_dummy_readl, pci_dummy_write, pci_dummy_writew, pci_dummy_writel, NULL);
}

static void
pci_dummy_io_set(void)
{
    io_sethandler(pci_bar[0].addr, 0x0020, pci_dummy_read, pci_dummy_readw, pci_dummy_readl, pci_dummy_write, pci_dummy_writew, pci_dummy_writel, NULL);
}

static uint8_t
pci_dummy_pci_read(int func, int addr, void *priv)
{
    pclog("AB0B:071A: PCI_Read(%d, %04x)\n", func, addr);

    switch (addr) {
        case 0x00:
            return 0x1A;
        case 0x01:
            return 0x07;
            break;

        case 0x02:
            return 0x0B;
        case 0x03:
            return 0xAB;

        case 0x04: /* PCI_COMMAND_LO */
        case 0x05: /* PCI_COMMAND_HI */
            return pci_regs[addr];

        case 0x06: /* PCI_STATUS_LO */
        case 0x07: /* PCI_STATUS_HI */
            return pci_regs[addr];

        case 0x08:
        case 0x09:
            return 0x00;

        case 0x0A:
            return pci_regs[addr];

        case 0x0B:
            return pci_regs[addr];

        case 0x10: /* PCI_BAR 7:5 */
            return (pci_bar[0].addr_regs[0] & 0xe0) | 0x01;
        case 0x11: /* PCI_BAR 15:8 */
            return pci_bar[0].addr_regs[1];
        case 0x12: /* PCI_BAR 23:16 */
            return pci_bar[0].addr_regs[2];
        case 0x13: /* PCI_BAR 31:24 */
            return pci_bar[0].addr_regs[3];

        case 0x2C:
            return 0x1A;
        case 0x2D:
            return 0x07;

        case 0x2E:
            return 0x0B;
        case 0x2F:
            return 0xAB;

        case 0x3C: /* PCI_ILR */
            return pci_regs[addr];

        case 0x3D: /* PCI_IPR */
            return pci_regs[addr];

        default:
            return 0x00;
    }
}

static void
pci_dummy_pci_write(int func, int addr, uint8_t val, void *priv)
{
    uint8_t valxor;

    pclog("AB0B:071A: PCI_Write(%d, %04x, %02x)\n", func, addr, val);

    switch (addr) {
        case 0x04: /* PCI_COMMAND_LO */
            valxor = (val & 0x03) ^ pci_regs[addr];
            if (valxor & PCI_COMMAND_IO) {
                pci_dummy_io_remove();
                if (((pci_bar[0].addr & 0xffe0) != 0) && (val & PCI_COMMAND_IO)) {
                    pci_dummy_io_set();
                }
            }
            pci_regs[addr] = val & 0x03;
            break;

        case 0x10:       /* PCI_BAR */
            val &= 0xe0; /* 0xe0 acc to RTL DS */
            val |= 0x01; /* re-enable IOIN bit */
                         /*FALLTHROUGH*/

        case 0x11: /* PCI_BAR */
        case 0x12: /* PCI_BAR */
        case 0x13: /* PCI_BAR */
            /* Remove old I/O. */
            pci_dummy_io_remove();

            /* Set new I/O as per PCI request. */
            pci_bar[0].addr_regs[addr & 3] = val;

            /* Then let's calculate the new I/O base. */
            pci_bar[0].addr &= 0xffe0;

            /* Log the new base. */
            pclog("AB0B:071A: PCI: new I/O base is %04X\n", pci_bar[0].addr);

            /* We're done, so get out of the here. */
            if (pci_regs[4] & PCI_COMMAND_IO) {
                if ((pci_bar[0].addr) != 0) {
                    pci_dummy_io_set();
                }
            }
            break;

        case 0x3C: /* PCI_ILR */
            pclog("AB0B:071A: IRQ now: %i\n", val);
            pci_regs[addr] = val;
            return;
    }
}

void
pci_dummy_init(void)
{
    card = pci_add_card(PCI_ADD_NORMAL, pci_dummy_pci_read, pci_dummy_pci_write, NULL);

    pci_bar[0].addr_regs[0] = 0x01;
    pci_regs[0x04]          = 0x03;

    pci_regs[0x3D] = PCI_INTD;
}
