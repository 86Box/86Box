/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation the PCI bus.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/machine.h>
#include "cpu.h"
#include "x86.h"
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/pci.h>
#include <86box/keyboard.h>
#include <86box/plat_unused.h>

#define PCI_ENABLED               0x80000000

typedef struct pci_card_t {
    uint8_t     bus;
    uint8_t     id;
    uint8_t     type;
    uint8_t     irq_routing[PCI_INT_PINS_NUM];

    void *      priv;
    void        (*write)(int func, int addr, uint8_t val, void *priv);
    uint8_t     (*read)(int func, int addr, void *priv);
} pci_card_t;

typedef struct pci_card_desc_t {
    uint8_t     type;
    void *      priv;
    void        (*write)(int func, int addr, uint8_t val, void *priv);
    uint8_t     (*read)(int func, int addr, void *priv);
    uint8_t     *slot;
} pci_card_desc_t;

typedef struct pci_mirq_t {
    uint8_t     enabled;
    uint8_t     irq_line;
    uint8_t     irq_level;
    uint8_t     pad;
} pci_mirq_t;

int         pci_burst_time;
int         agp_burst_time;
int         pci_nonburst_time;
int         agp_nonburst_time;

int         pci_flags;

uint32_t    pci_base = 0xc000;
uint32_t    pci_size = 0x1000;

static pci_card_t  pci_cards[PCI_CARDS_NUM];
static pci_card_desc_t  pci_card_descs[PCI_CARDS_NUM];
static uint8_t     pci_pmc = 0;
static uint8_t     last_pci_card = 0;
static uint8_t     last_normal_pci_card = 0;
static uint8_t     last_normal_pci_card_id = 0;
static uint8_t     last_pci_bus = 1;
static uint8_t     next_pci_card = 0;
static uint8_t     normal_pci_cards = 0;
static uint8_t     next_normal_pci_card = 0;
static uint8_t     pci_card_to_slot_mapping[256][PCI_CARDS_NUM];
static uint8_t     pci_bus_number_to_index_mapping[256];
static uint8_t     pci_irqs[PCI_IRQS_NUM];
static uint8_t     pci_irq_level[PCI_IRQS_NUM];
static uint64_t    pci_irq_hold[PCI_IRQS_NUM];
static pci_mirq_t  pci_mirqs[PCI_MIRQS_NUM];
static int         pci_index;
static int         pci_func;
static int         pci_card;
static int         pci_bus;
static int         pci_key;
static int         pci_trc_reg = 0;
static uint32_t    pci_enable = 0x00000000;

static void        pci_reset_regs(void);

#ifdef ENABLE_PCI_LOG
int pci_do_log = ENABLE_PCI_LOG;

static void
pci_log(const char *fmt, ...)
{
    va_list ap;

    if (pci_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pci_log(fmt, ...)
#endif

void
pci_set_irq_routing(int pci_int, int irq)
{
    pci_irqs[pci_int - 1] = irq;
}

void
pci_set_irq_level(int pci_int, int level)
{
    pci_irq_level[pci_int - 1] = !!level;
}

void
pci_enable_mirq(int mirq)
{
    pci_mirqs[mirq].enabled = 1;
}

void
pci_set_mirq_routing(int mirq, uint8_t irq)
{
    pci_mirqs[mirq].irq_line = irq;
}

uint8_t
pci_get_mirq_level(int mirq)
{
    return pci_mirqs[mirq].irq_level;
}

void
pci_set_mirq_level(int mirq, uint8_t level)
{
    pci_mirqs[mirq].irq_level = level;
}

/* PCI raise IRQ: the first parameter is slot if < PCI_MIRQ_BASE, MIRQ if >= PCI_MIRQ_BASE
                  and < PCI_DIRQ_BASE, and direct IRQ line if >= PCI_DIRQ_BASE (RichardG's
                  hack that may no longer be needed). */
void
pci_irq(uint8_t slot, uint8_t pci_int, int level, int set, uint8_t *irq_state)
{
    uint8_t irq_routing   = 0;
    uint8_t pci_int_index = pci_int - PCI_INTA;
    uint8_t irq_line      = 0;
    uint8_t is_vfio       = 0;

    /* The fast path out an invalid PCI card. */
    if (slot == PCI_CARD_INVALID)
        return;

    switch (slot) {
        default:
            return;

        case 0x00 ... PCI_CARD_MAX:
            /* PCI card. */
            if (!last_pci_card)
                return;

            if (pci_flags & FLAG_NO_IRQ_STEERING)
                irq_line = pci_cards[slot].read(0, 0x3c, pci_cards[slot].priv);
            else {
                irq_routing = pci_cards[slot].irq_routing[pci_int_index];

                switch (irq_routing) {
                    default:
                    case 0x00:
                        return;

                    case 0x01 ... PCI_IRQS_NUM:
                        is_vfio     = pci_cards[slot].type & PCI_CARD_VFIO;
                        irq_routing = (irq_routing - PCI_INTA) & PCI_IRQ_MAX;

                        irq_line = pci_irqs[irq_routing];
                        /* Ignore what was provided to us as a parameter and override it with whatever
                           the chipset is set to. */
                        level    = !!pci_irq_level[irq_routing];
                        if (level && is_vfio)
                            level--;
                        break;

                    /* Sometimes, PCI devices are mapped to direct IRQ's. */
                    case (PCI_DIRQ_BASE | 0x00) ... (PCI_DIRQ_BASE | PCI_DIRQ_MAX):
                        /* Direct IRQ line, always edge-triggered. */
                        irq_line = slot & PCI_IRQ_MAX;
                        break;
                }
            }
            break;
        case (PCI_IIRQ_BASE | 0x00) ... (PCI_IIRQ_BASE | PCI_IIRQS_NUM):
            /* PCI internal routing. */
            if (slot > 0x00) {
                slot = (slot - 1) & PCI_INT_PINS_MAX;

                irq_line    = pci_irqs[slot];

                /* Ignore what was provided to us as a parameter and override it with whatever
                   the chipset is set to. */
                level       = !!pci_irq_level[slot];
            } else {
                irq_line    = 0xff;
                level       = 0;
            }
            break;
        case (PCI_MIRQ_BASE | 0x00) ... (PCI_MIRQ_BASE | PCI_MIRQ_MAX):
            /* MIRQ */
            slot &= PCI_MIRQ_MAX;

            if (!pci_mirqs[slot].enabled)
                return;

            irq_line = pci_mirqs[slot].irq_line;
            break;
        case (PCI_DIRQ_BASE | 0x00) ... (PCI_DIRQ_BASE | PCI_DIRQ_MAX):
            /* Direct IRQ line (RichardG's ACPI workaround, may no longer be needed). */
            irq_line = slot & PCI_IRQ_MAX;
            break;
    }

    if (irq_line > PCI_IRQ_MAX)
        return;

    picint_common(1 << irq_line, level, set, irq_state);
}

uint8_t
pci_get_int(uint8_t slot, uint8_t pci_int)
{
    return pci_cards[slot].irq_routing[pci_int - PCI_INTA];
}

static void
pci_clear_slot(int card)
{
    pci_card_to_slot_mapping[pci_cards[card].bus][pci_cards[card].id] = PCI_CARD_INVALID;

    pci_cards[card].id   = 0xff;
    pci_cards[card].type = 0xff;

    for (uint8_t i = 0; i < 4; i++)
        pci_cards[card].irq_routing[i] = 0;

    pci_cards[card].read  = NULL;
    pci_cards[card].write = NULL;
    pci_cards[card].priv  = NULL;
}

/* Relocate a PCI device to a new slot, required for the configurable
   IDSEL's of ALi M1543(c). */
void
pci_relocate_slot(int type, int new_slot)
{
    int     card = -1;
    int     old_slot;

    if ((new_slot < 0) || (new_slot > 31))
        return;

    for (uint8_t i = 0; i < PCI_CARDS_NUM; i++) {
        if ((pci_cards[i].bus == 0) && (pci_cards[i].type == type)) {
            card = i;
            break;
        }
    }

    if (card == -1)
        return;

    old_slot                              = pci_cards[card].id;
    pci_cards[card].id                    = new_slot;

    if (pci_card_to_slot_mapping[0][old_slot] == card)
        pci_card_to_slot_mapping[0][old_slot] = PCI_CARD_INVALID;

    if (pci_card_to_slot_mapping[0][new_slot] == PCI_CARD_INVALID)
        pci_card_to_slot_mapping[0][new_slot] = card;
}

/* Write PCI enable/disable key, split for the ALi M1435. */
void
pci_key_write(uint8_t val)
{
    pci_key = val & 0xf0;

    if (pci_key)
        pci_flags |= FLAG_CONFIG_IO_ON;
    else
        pci_flags &= ~FLAG_CONFIG_IO_ON;
}

static void
pci_io_handlers(int set)
{
    io_handler(set, 0x0cf8, 4, pci_read, pci_readw, pci_readl, pci_write, pci_writew, pci_writel, NULL);

    if (pci_flags & FLAG_MECHANISM_1)
        io_handler(set, 0x0cfc, 4, pci_read, pci_readw, pci_readl, pci_write, pci_writew, pci_writel, NULL);

    if (pci_flags & FLAG_MECHANISM_2) {
        if (set && pci_key)
            pci_flags |= FLAG_CONFIG_IO_ON;
        else
            pci_flags &= ~FLAG_CONFIG_IO_ON;
    }
}

/* Set PMC (ie. change PCI configuration mechanism), 0 = #2, 1 = #1. */
void
pci_set_pmc(uint8_t pmc)
{
    pci_log("pci_set_pmc(%02X)\n", pmc);

    pci_io_handlers(0);

    pci_flags &= ~FLAG_MECHANISM_MASK;
    pci_flags |= (FLAG_MECHANISM_1 + !(pmc & 0x01));

    pci_io_handlers(1);

    pci_pmc = (pmc & 0x01);
}

static void
pci_reg_write(uint16_t port, uint8_t val)
{
    uint8_t slot = 0;

    if (port >= 0xc000) {
        pci_card  = (port >> 8) & 0xf;
        pci_index = port & 0xfc;
    }

    slot = pci_card_to_slot_mapping[pci_bus_number_to_index_mapping[pci_bus]][pci_card];
    if (slot != PCI_CARD_INVALID) {
        if (pci_cards[slot].write)
            pci_cards[slot].write(pci_func, pci_index | (port & 0x03), val, pci_cards[slot].priv);
    }
    pci_log("PCI: [WB] Mechanism #%i, slot %02X, %s card %02X:%02X, function %02X, index %02X = %02X\n",
            (port >= 0xc000) ? 2 : 1, slot,
            (slot == PCI_CARD_INVALID) ? "non-existent" : (pci_cards[slot].write ? "used" : "unused"),
            pci_card, pci_bus, pci_func, pci_index | (port & 0x03), val);
}

static void
pci_reset_regs(void)
{
    pci_index = pci_card = pci_func = pci_bus = pci_key = 0;
    pci_enable = 0x00000000;

    pci_flags &= ~(FLAG_CONFIG_IO_ON | FLAG_CONFIG_M1_IO_ON);
}

void
pci_pic_reset(void)
{
    pic_reset();
    pic_set_pci_flag(last_pci_card > 0);
}

static void
pci_reset_hard(void)
{
    pci_reset_regs();

    for (uint8_t i = 0; i < PCI_IRQS_NUM; i++) {
        if (pci_irq_hold[i]) {
            pci_irq_hold[i] = 0;

            picintc(1 << i);
        }
    }

    pci_pic_reset();
}

void
pci_reset(void)
{
    if (pci_flags & FLAG_MECHANISM_SWITCH) {
        pci_log("pci_reset(): Switchable configuration mechanism\n");
        pci_set_pmc(0x00);
    }

    pci_reset_hard();
}

static void
pci_trc_reset(uint8_t val)
{
    if (val & 2) {
        dma_reset();
        dma_set_at(1);

        device_reset_all(DEVICE_ALL);

        cpu_alt_reset = 0;

        pci_reset();

        mem_a20_alt = 0;
        mem_a20_recalc();

        flushmmucache();
    }

#ifdef USE_DYNAREC
    if (cpu_use_dynarec)
        cpu_init = 1;
    else
        resetx86();
#else
    resetx86();
#endif
}

void
pci_write(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    pci_log("PCI: [WB] Mechanism #%i port %04X = %02X\n", ((port >= 0xcfc) && (port <= 0xcff)) ? 1 : 2, port, val);

    switch (port) {
        case 0xcf8:
            if (pci_flags & FLAG_MECHANISM_2) {
                pci_func = (val >> 1) & 7;
                pci_key_write(val);

                pci_log("PCI: Mechanism #2 CF8: %sllocating ports %04X-%04X...\n", (pci_flags & FLAG_CONFIG_IO_ON) ? "A" : "Dea",
                        pci_base, pci_base + pci_size - 1);
            }
            break;
        case 0xcf9:
            if (pci_flags & FLAG_TRC_CONTROLS_CPURST)
                cpu_cpurst_on_sr = !(val & 0x10);

            if (!(pci_trc_reg & 4) && (val & 4))
                pci_trc_reset(val);

            pci_trc_reg = val & 0xfd;

            if (val & 2)
                pci_trc_reg &= 0xfb;
            break;
        case 0xcfa:
            if (pci_flags & FLAG_MECHANISM_2)
                pci_bus = val;
            break;
        case 0xcfb:
            if (pci_flags & FLAG_MECHANISM_SWITCH)
                pci_set_pmc(val);
            break;

        case 0xcfc:
        case 0xcfd:
        case 0xcfe:
        case 0xcff:
            if ((pci_flags & FLAG_MECHANISM_1) && (pci_flags & FLAG_CONFIG_M1_IO_ON))
                pci_reg_write(port, val);
           break;

        case 0xc000 ... 0xc0ff:
            if ((pci_flags & FLAG_MECHANISM_2) && (pci_flags & (FLAG_CONFIG_IO_ON | FLAG_CONFIG_DEV0_IO_ON)))
                pci_reg_write(port, val);
            break;

        case 0xc100 ... 0xcfff:
            if ((pci_flags & FLAG_MECHANISM_2) && (pci_flags & FLAG_CONFIG_IO_ON))
                pci_reg_write(port, val);
            break;

        default:
            break;
    }
}

void
pci_writew(uint16_t port, uint16_t val, UNUSED(void *priv))
{
    if (port & 0x0001) {
        /* Non-aligned access, split into two byte accesses. */
        pci_write(port, val & 0xff, priv);
        pci_write(port + 1, val >> 8, priv);
    } else {
        /* Aligned access, still split because we cheat. */
        switch (port) {
            case 0xcfc:
            case 0xcfe:
            case 0xc000 ... 0xcffe:
                pci_write(port, val & 0xff, priv);
                pci_write(port + 1, val >> 8, priv);
                break;

            default:
                break;
        }
    }
}

void
pci_writel(uint16_t port, uint32_t val, UNUSED(void *priv))
{
    if (port & 0x0003) {
        /* Non-aligned access, split into two word accesses. */
        pci_writew(port, val & 0xffff, priv);
        pci_writew(port + 2, val >> 16, priv);
    } else {
        /* Aligned access. */
        switch (port) {
            case 0xcf8:
                /* No split here, actual 32-bit access. */
                if (pci_flags & FLAG_MECHANISM_1) {
                    pci_log("PCI: [WL] Mechanism #1 port 0CF8 = %08X\n", val);

                    pci_index  = val & 0xff;
                    pci_func   = (val >> 8) & 7;
                    pci_card   = (val >> 11) & 31;
                    pci_bus    = (val >> 16) & 0xff;
                    pci_enable = (val & PCI_ENABLED);

                    if (pci_enable)
                        pci_flags |= FLAG_CONFIG_M1_IO_ON;
                    else
                        pci_flags &= ~FLAG_CONFIG_M1_IO_ON;
                    break;
                }
                break;
            case 0xcfc:
            case 0xc000 ... 0xcffc:
                /* Still split because we cheat. */
                pci_writew(port, val & 0xffff, priv);
                pci_writew(port + 2, val >> 16, priv);
                break;

            default:
                break;
        }
    }
}

static uint8_t
pci_reg_read(uint16_t port)
{
    uint8_t slot = 0;
    uint8_t ret  = 0xff;

    if (port >= 0xc000) {
        pci_card  = (port >> 8) & 0xf;
        pci_index = port & 0xfc;
    }

    slot = pci_card_to_slot_mapping[pci_bus_number_to_index_mapping[pci_bus]][pci_card];
    if (slot != PCI_CARD_INVALID) {
        if (pci_cards[slot].read)
            ret = pci_cards[slot].read(pci_func, pci_index | (port & 0x03), pci_cards[slot].priv);
    }
    pci_log("PCI: [RB] Mechanism #%i, slot %02X, %s card %02X:%02X, function %02X, index %02X = %02X\n",
            (port >= 0xc000) ? 2 : 1, slot,
            (slot == PCI_CARD_INVALID) ? "non-existent" : (pci_cards[slot].read ? "used" : "unused"),
            pci_card, pci_bus, pci_func, pci_index | (port & 0x03), ret);

    return ret;
}

uint8_t
pci_read(uint16_t port, UNUSED(void *priv))
{
    uint8_t ret  = 0xff;

    switch (port) {
        case 0xcf8:
            if (pci_flags & FLAG_MECHANISM_2)
                ret = pci_key | (pci_func << 1);
            break;
        case 0xcf9:
            ret = pci_trc_reg & 0xfb;
            break;
        case 0xcfa:
            if (pci_flags & FLAG_MECHANISM_2)
                ret = pci_bus;
            break;
       case 0xcfb:
            if (pci_flags & FLAG_MECHANISM_SWITCH)
                ret = pci_pmc;
            break;

        case 0xcfc:
        case 0xcfd:
        case 0xcfe:
        case 0xcff:
            if ((pci_flags & FLAG_MECHANISM_1) && (pci_flags & FLAG_CONFIG_M1_IO_ON))
                ret = pci_reg_read(port);
            break;

        case 0xc000 ... 0xc0ff:
            if ((pci_flags & FLAG_MECHANISM_2) && (pci_flags & (FLAG_CONFIG_IO_ON | FLAG_CONFIG_DEV0_IO_ON)))
                ret = pci_reg_read(port);
            break;

        case 0xc100 ... 0xcfff:
            if ((pci_flags & FLAG_MECHANISM_2) && (pci_flags & FLAG_CONFIG_IO_ON))
                ret = pci_reg_read(port);
            break;

        default:
            break;
    }

    pci_log("PCI: [RB] Mechanism #%i port %04X = %02X\n", ((port >= 0xcfc) && (port <= 0xcff)) ? 1 : 2, port, ret);

    return ret;
}

uint16_t
pci_readw(uint16_t port, UNUSED(void *priv))
{
    uint16_t ret  = 0xffff;

    if (port & 0x0001) {
        /* Non-aligned access, split into two byte accesses. */
        ret = pci_read(port, priv);
        ret |= ((uint16_t) pci_read(port + 1, priv)) << 8;
    } else {
        /* Aligned access, still split because we cheat. */
        switch (port) {
            case 0xcfc:
            case 0xcfe:
            case 0xc000 ... 0xcffe:
                ret = pci_read(port, priv);
                ret |= ((uint16_t) pci_read(port + 1, priv)) << 8;
                break;

            default:
                break;
        }
    }

    return ret;
}

uint32_t
pci_readl(uint16_t port, UNUSED(void *priv))
{
    uint32_t ret  = 0xffffffff;

    if (port & 0x0003) {
        /* Non-aligned access, split into two word accesses. */
        ret = pci_readw(port, priv);
        ret |= ((uint32_t) pci_readw(port + 2, priv)) << 16;
    } else {
        /* Aligned access. */
        switch (port) {
            case 0xcf8:
                /* No split here, actual 32-bit access. */
                if (pci_flags & FLAG_MECHANISM_1) {
                    ret = pci_index | (pci_func << 8) | (pci_card << 11) | (pci_bus << 16);
                    if (pci_flags & FLAG_CONFIG_M1_IO_ON)
                        ret |= PCI_ENABLED;

                    pci_log("PCI: [RL] Mechanism #1 port 0CF8 = %08X\n", ret);

                    return ret;
                }
                break;
            case 0xcfc:
            case 0xc000 ... 0xcffc:
                /* Still split because we cheat. */
                ret = pci_readw(port, priv);
                ret |= ((uint32_t) pci_readw(port + 2, priv)) << 16;
                break;
        }
    }

    return ret;
}

uint8_t
pci_register_bus(void)
{
    return last_pci_bus++;
}

void
pci_remap_bus(uint8_t bus_index, uint8_t bus_number)
{
    uint8_t i = 1;
    do {
        if (pci_bus_number_to_index_mapping[i] == bus_index)
            pci_bus_number_to_index_mapping[i] = PCI_BUS_INVALID;
    } while (i++ < 0xff);

    if ((bus_number > 0) && (bus_number < 0xff))
        pci_bus_number_to_index_mapping[bus_number] = bus_index;
}

void
pci_register_bus_slot(int bus, int card, int type, int inta, int intb, int intc, int intd)
{
    pci_card_t *dev = &pci_cards[last_pci_card];

    dev->bus                            = bus;
    dev->id                             = card;
    dev->type                           = type;
    dev->irq_routing[0]                 = inta;
    dev->irq_routing[1]                 = intb;
    dev->irq_routing[2]                 = intc;
    dev->irq_routing[3]                 = intd;
    dev->read                           = NULL;
    dev->write                          = NULL;
    dev->priv                           = NULL;
    pci_card_to_slot_mapping[bus][card] = last_pci_card;

    pci_log("pci_register_slot(): pci_cards[%i].bus = %02X; .id = %02X\n", last_pci_card, bus, card);

    if (type == PCI_CARD_NORMAL) {
        last_normal_pci_card++;
        /* This is needed to know at what position to add the bridge. */
        last_normal_pci_card_id = last_pci_card;
    }

    last_pci_card++;
}

static uint8_t
pci_find_slot(uint8_t add_type, uint8_t ignore_slot)
{
    const pci_card_t *dev;
    /* Is the device being added with a strict slot type matching requirement? */
    uint8_t           strict = (add_type & PCI_ADD_STRICT);
    /* The actual type of the device being added, with the strip flag, if any,
       masked. */
    uint8_t           masked_add_type = (add_type & PCI_ADD_MASK);
    /* Is the device being added normal, ie. without the possibility of ever
       being used as an on-board device? */
    uint8_t           normal_add_type = (masked_add_type >= PCI_CARD_NORMAL);
    uint8_t           match;
    uint8_t           normal;
    uint8_t           empty;
    uint8_t           process;
    uint8_t           ret = PCI_CARD_INVALID;

    /* Iterate i until we have either exhausted all the slot or the value of
       ret has changed to something other than PCI_CARD_INVALID. */
    for (uint8_t i = 0; (ret == PCI_CARD_INVALID) && (i < last_pci_card); i++) {
        dev = &pci_cards[i];

        /* Is the slot we are looking at of the exact same type as the device being
           added? */
        match = (dev->type == masked_add_type);
        /* Is the slot we are looking at a normal slot (ie. not an on-board chip)? */
        normal = (dev->type == PCI_CARD_NORMAL);
        /* Is the slot we are looking at empty? */
        empty = !dev->read && !dev->write;
        /* Should we process this slot, ie. were we told to ignore it, if any at all? */
        process = (ignore_slot == PCI_IGNORE_NO_SLOT) || (i != ignore_slot);

        /* This condition is now refactored and made to be easily human-readable. */
        if (empty && process && (match || (!strict && normal && normal_add_type)))
            ret = i;
    }

    return ret;
}

/* Add a PCI card. */
void
pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, void *priv),
             void (*write)(int func, int addr, uint8_t val, void *priv), void *priv, uint8_t *slot)
{
    pci_card_desc_t *dev;

    pci_log("pci_add_card(): PCI card #%02i: type = %i\n", next_pci_card, add_type);

    if (next_pci_card < PCI_CARDS_NUM) {
        dev = &pci_card_descs[next_pci_card];

        dev->type  = add_type | PCI_ADD_STRICT;
        dev->read  = read;
        dev->write = write;
        dev->priv  = priv;
        dev->slot  = slot;

        *(dev->slot) = PCI_CARD_INVALID;

        next_pci_card++;
        if (add_type == PCI_ADD_NORMAL)
            normal_pci_cards++;
    }
}

static void
pci_clear_card(UNUSED(int pci_card))
{
    pci_card_desc_t *dev;

    if (next_pci_card < PCI_CARDS_NUM) {
        dev = &pci_card_descs[next_pci_card];

        memset(dev, 0x00, sizeof(pci_card_desc_t));
    }
}

static uint8_t
pci_register_card(int pci_card)
{
    pci_card_desc_t *dev;
    pci_card_t *card;
    uint8_t     i;
    uint8_t     ret = PCI_CARD_INVALID;

    if (pci_card < PCI_CARDS_NUM) {
        dev = &pci_card_descs[pci_card];

        if (last_pci_card) {
            /* First, find the next available slot. */
            i = pci_find_slot(dev->type, 0xff);

            if (i != PCI_CARD_INVALID) {
                card = &pci_cards[i];
                card->read  = dev->read;
                card->write = dev->write;
                card->priv  = dev->priv;
                card->type |= (dev->type & PCI_CARD_VFIO);

                *(dev->slot) = i;

                ret = i;
            }
        }

        pci_clear_card(pci_card);
    }

    return ret;
}

/* Add an instance of the PCI bridge. */
void
pci_add_bridge(uint8_t agp, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv, uint8_t *slot)
{
    pci_card_t *card;
    uint8_t bridge_slot = agp ? pci_find_slot(PCI_ADD_AGPBRIDGE, 0xff) : last_normal_pci_card_id;

    if (bridge_slot != PCI_CARD_INVALID) {
        card = &pci_cards[bridge_slot];
        card->read  = read;
        card->write = write;
        card->priv  = priv;
    }

    *slot = bridge_slot;
}

/* Register the cards that have been added into slots. */
void
pci_register_cards(void)
{
    uint8_t normal;
#ifdef ENABLE_PCI_LOG
    uint8_t type;
    uint8_t *slot;
#endif

    next_normal_pci_card = 0;

    if (next_pci_card > 0) {
        for (uint8_t i = 0; i < next_pci_card; i++) {
#ifdef ENABLE_PCI_LOG
            type = pci_card_descs[i].type;
            slot = pci_card_descs[i].slot;
#endif
            normal = ((pci_card_descs[i].type & ~PCI_ADD_STRICT) == PCI_CARD_NORMAL);

            /* If this is a normal card, increase the next normal card index. */
            if (normal)
                next_normal_pci_card++;

            /* If this is a normal card and the next one is going to be beyond the last slot,
               add the bridge. */
            if (normal && (next_normal_pci_card >= last_normal_pci_card) &&
                (normal_pci_cards > last_normal_pci_card) && !(pci_flags & FLAG_NO_BRIDGES))
                device_add_inst(&dec21150_device, last_pci_bus);

            pci_register_card(i);
            pci_log("pci_register_cards(): PCI card #%02i: type = %02X, pci device = %02X:%02X\n",
                    i, type, pci_cards[*slot].bus, pci_cards[*slot].id);
        }
    }

    next_pci_card = 0;
    normal_pci_cards = 0;

    next_normal_pci_card = 0;
}

static void
pci_slots_clear(void)
{
    uint8_t i;

    last_pci_card = last_normal_pci_card = 0;
    last_normal_pci_card                 = 0;
    last_pci_bus                         = 1;

    next_pci_card                        = 0;
    normal_pci_cards                     = 0;

    next_normal_pci_card                 = 0;

    for (i = 0; i < PCI_CARDS_NUM; i++)
        pci_clear_slot(i);

    i = 0;
    do {
        for (uint8_t j = 0; j < PCI_CARDS_NUM; j++)
            pci_card_to_slot_mapping[i][j] = PCI_CARD_INVALID;
        pci_bus_number_to_index_mapping[i] = PCI_BUS_INVALID;
    } while (i++ < 0xff);

    pci_bus_number_to_index_mapping[0] = 0; /* always map bus 0 to index 0 */
}

void
pci_init(int flags)
{
    int c;

    pci_base = 0xc000;
    pci_size = 0x1000;

    pci_slots_clear();

    pci_reset_hard();

    pci_trc_reg = 0;
    pci_flags   = flags;

    if (pci_flags & FLAG_NO_IRQ_STEERING) {
        pic_elcr_io_handler(0);
        pic_elcr_set_enabled(0);
    } else {
        pic_elcr_io_handler(1);
        pic_elcr_set_enabled(1);
    }

    pci_pmc = (pci_flags & FLAG_MECHANISM_1) ? 0x01 : 0x00;

    if ((pci_flags & FLAG_MECHANISM_2) && (pci_flags & FLAG_CONFIG_DEV0_IO_ON)) {
        pci_log("PCI: Always expose device 0\n");
        pci_base = 0xc100;
        pci_size = 0x0f00;
    }

    if (pci_flags & FLAG_MECHANISM_SWITCH) {
        pci_log("PCI: Switchable configuration mechanism\n");
        pci_set_pmc(pci_pmc);
    } else
        pci_io_handlers(1);

    for (c = 0; c < PCI_IRQS_NUM; c++) {
        pci_irqs[c]      = PCI_IRQ_DISABLED;
        pci_irq_level[c] = (pci_flags & FLAG_NO_IRQ_STEERING) ? 0 : 1;
    }

    for (c = 0; c < PCI_MIRQS_NUM; c++) {
        pci_mirqs[c].enabled  = 0;
        pci_mirqs[c].irq_line = PCI_IRQ_DISABLED;
    }

    pic_set_pci_flag(1);
}
