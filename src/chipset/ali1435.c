/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Emulation of ALi M1435 chipset that acts as both the
 *          southbridge.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
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

#define MEM_STATE_SHADOW_R 0x01
#define MEM_STATE_SHADOW_W 0x02
#define MEM_STATE_SMRAM    0x04

typedef struct ali_1435_t {
    uint8_t index;
    uint8_t cfg_locked;
    uint8_t pci_slot;
    uint8_t pad;
    uint8_t regs[16];
    uint8_t pci_regs[256];
} ali1435_t;

#ifdef ENABLE_ALI1435_LOG
int ali1435_do_log = ENABLE_ALI1435_LOG;

static void
ali1435_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1435_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ali1435_log(fmt, ...)
#endif

/* NOTE: We cheat here. The real ALi M1435 uses a level to edge triggered IRQ converter
         when the most siginificant bit is set. We work around that by manipulating the
         emulated PIC's ELCR register. */
static void
ali1435_update_irqs(ali1435_t *dev, int set)
{
    uint8_t val;
    int     reg;
    int     shift;
    int     irq;
    int     irq_map[8] = { -1, 5, 9, 10, 11, 12, 14, 15 };
    pic_t  *temp_pic;

    for (uint8_t i = 0; i < 4; i++) {
        reg   = 0x80 + (i >> 1);
        shift = (i & 1) << 2;
        val   = (dev->pci_regs[reg] >> shift) & 0x0f;
        irq   = irq_map[val & 0x07];
        if (irq == -1)
            continue;
        temp_pic = (irq >= 8) ? &pic2 : &pic;
        irq &= 7;
        if (set && (val & 0x08))
            temp_pic->elcr |= (1 << irq);
        else
            temp_pic->elcr &= ~(1 << irq);
    }
}

static void
ali1435_pci_write(int func, int addr, uint8_t val, void *priv)
{
    ali1435_t *dev = (ali1435_t *) priv;
    int        irq;
    int        irq_map[8] = { -1, 5, 9, 10, 11, 12, 14, 15 };

    ali1435_log("ali1435_write(%02X, %02X, %02X)\n", func, addr, val);

    if (func > 0)
        return;

    if ((addr < 0x04) || (addr == 0x06) || ((addr >= 0x08) && (addr <= 0x0b)))
        return;

    if ((addr >= 0x0f) && (addr < 0x30))
        return;

    if ((addr >= 0x34) && (addr < 0x40))
        return;

    switch (addr) {
        /* Dummy PCI Config */
        case 0x04:
            dev->pci_regs[addr] = (val & 0x7f) | 0x07;
            break;

        case 0x05:
            dev->pci_regs[addr] = (val & 0x01);
            break;

        /* Dummy PCI Status */
        case 0x07:
            dev->pci_regs[addr] &= ~(val & 0xb8);
            break;

        case 0x80:
        case 0x81:
            dev->pci_regs[addr] = val;
            ali1435_update_irqs(dev, 0);
            irq = irq_map[val & 0x07];
            if (irq >= 0) {
                ali1435_log("Set IRQ routing: INT %c -> %02X\n", 0x41 + ((addr & 0x01) << 1), irq);
                pci_set_irq_routing(PCI_INTA + ((addr & 0x01) << 1), irq);
            } else {
                ali1435_log("Set IRQ routing: INT %c -> FF\n", 0x41 + ((addr & 0x01) << 1));
                pci_set_irq_routing(PCI_INTA + ((addr & 0x01) << 1), PCI_IRQ_DISABLED);
            }
            irq = irq_map[(val >> 4) & 0x07];
            if (irq >= 0) {
                ali1435_log("Set IRQ routing: INT %c -> %02X\n", 0x42 + ((addr & 0x01) << 1), irq);
                pci_set_irq_routing(PCI_INTB + ((addr & 0x01) << 1), irq);
            } else {
                ali1435_log("Set IRQ routing: INT %c -> FF\n", 0x42 + ((addr & 0x01) << 1));
                pci_set_irq_routing(PCI_INTB + ((addr & 0x01) << 1), PCI_IRQ_DISABLED);
            }
            ali1435_update_irqs(dev, 1);
            break;

        default:
            dev->pci_regs[addr] = val;
            break;
    }
}

static uint8_t
ali1435_pci_read(int func, int addr, void *priv)
{
    const ali1435_t *dev = (ali1435_t *) priv;
    uint8_t          ret;

    ret = 0xff;

    if (func == 0)
        ret = dev->pci_regs[addr];

    ali1435_log("ali1435_read(%02X, %02X) = %02X\n", func, addr, ret);

    return ret;
}

static void
ali1435_write(uint16_t addr, uint8_t val, void *priv)
{
    ali1435_t *dev = (ali1435_t *) priv;

    switch (addr) {
        case 0x22:
            dev->index = val;
            break;

        case 0x23:
            if (dev->index == 0x03)
                dev->cfg_locked = (val != 0x69);
#ifdef ENABLE_ALI1435_LOG
            else
                ali1435_log("M1435: dev->regs[%02x] = %02x\n", dev->index, val);
#endif

            if (!dev->cfg_locked) {
                switch (dev->index) {
                    /* PCI Mechanism select? */
                    case 0x00:
                        dev->regs[dev->index] = val;
                        ali1435_log("PMC = %i\n", val != 0xc8);
                        pci_key_write(((val & 0xc8) == 0xc8) ? 0xf0 : 0x00);
                        break;

                    /* ???? */
                    case 0x06:
                        dev->regs[dev->index] = val;
                        break;

                    /* ???? */
                    case 0x07:
                        dev->regs[dev->index] = val;
                        break;

                    default:
                        break;
                }
            }
            break;
        default:
            break;
    }
}

static uint8_t
ali1435_read(uint16_t addr, void *priv)
{
    const ali1435_t *dev = (ali1435_t *) priv;
    uint8_t          ret = 0xff;

    if ((addr == 0x23) && (dev->index < 0x10))
        ret = dev->regs[dev->index];
    else if (addr == 0x22)
        ret = dev->index;

    return ret;
}

static void
ali1435_reset(void *priv)
{
    ali1435_t *dev = (ali1435_t *) priv;

    memset(dev->regs, 0, 16);

    dev->regs[0x00] = 0xff;

    dev->cfg_locked = 1;

    memset(dev->pci_regs, 0, 256);

    dev->pci_regs[0x00] = 0x25;
    dev->pci_regs[0x01] = 0x10; /*ALi*/
    dev->pci_regs[0x02] = 0x35;
    dev->pci_regs[0x03] = 0x14; /*M1435*/
    dev->pci_regs[0x04] = 0x07;
    dev->pci_regs[0x07] = 0x04;
    dev->pci_regs[0x0b] = 0x06;

    dev->pci_regs[0x80] = 0x80;
    dev->pci_regs[0x81] = 0x00;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
}

static void
ali1435_close(void *priv)
{
    ali1435_t *dev = (ali1435_t *) priv;

    free(dev);
}

static void *
ali1435_init(UNUSED(const device_t *info))
{
    ali1435_t *dev = (ali1435_t *) calloc(1, sizeof(ali1435_t));

    dev->cfg_locked = 1;

    /* M1435 Ports:
                22h Index Port
                23h Data Port
    */
    io_sethandler(0x0022, 0x0002, ali1435_read, NULL, NULL, ali1435_write, NULL, NULL, dev);

    pci_add_card(PCI_ADD_NORTHBRIDGE, ali1435_pci_read, ali1435_pci_write, dev, &dev->pci_slot);

    ali1435_reset(dev);

    return dev;
}

const device_t ali1435_device = {
    .name          = "Intel ALi M1435",
    .internal_name = "ali1435",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = ali1435_init,
    .close         = ali1435_close,
    .reset         = ali1435_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
