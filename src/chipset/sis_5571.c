/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5571 Pentium PCI/ISA Chipset.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023-2024 Miran Grca.
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
#include <86box/io.h>
#include <86box/timer.h>

// #include <86box/dma.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/smram.h>
#include <86box/spd.h>
#include <86box/usb.h>

#include <86box/chipset.h>

#ifdef ENABLE_SIS_5571_LOG
int sis_5571_do_log = ENABLE_SIS_5571_LOG;

static void
sis_5571_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5571_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5571_log(fmt, ...)
#endif

typedef struct sis_5571_t {
    uint8_t     index;
    uint8_t     nb_slot;
    uint8_t     sb_slot;
    uint8_t     pad;

    uint8_t     regs[16];
    uint8_t     states[7];
    uint8_t     pad0;

    uint8_t     usb_unk_regs[8];

    uint8_t     pci_conf[256];
    uint8_t     pci_conf_sb[3][256];

    uint16_t    usb_unk_base;

    sff8038i_t *bm[2];
    smram_t    *smram;
    port_92_t  *port_92;
    void       *pit;
    nvr_t      *nvr;
    usb_t      *usb;

   uint8_t    (*pit_read_reg)(void *priv, uint8_t reg);
} sis_5571_t;

static void
sis_5571_shadow_recalc(sis_5571_t *dev)
{
    int      state;
    uint32_t base;

    for (uint8_t i = 0x70; i <= 0x76; i++) {
        if (i == 0x76) {
            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(0xf0000, 0x10000, state);
                sis_5571_log("000F0000-000FFFFF\n");
            }
        } else {
            base = ((i & 0x07) << 15) + 0xc0000;

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base, 0x4000, state);
                sis_5571_log("%08X-%08X\n", base, base + 0x3fff);
            }

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0x0a) {
                state = (dev->pci_conf[i] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base + 0x4000, 0x4000, state);
                sis_5571_log("%08X-%08X\n", base + 0x4000, base + 0x7fff);
            }
        }

        dev->states[i & 0x0f] = dev->pci_conf[i];
    }

    flushmmucache_nopc();
}

static void
sis_5571_smram_recalc(sis_5571_t *dev)
{
    smram_disable_all();

    switch (dev->pci_conf[0xa3] >> 6) {
        case 0:
            smram_enable(dev->smram, 0x000e0000, 0x000e0000, 0x8000, dev->pci_conf[0xa3] & 0x10, 1);
            break;
        case 1:
            smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x8000, dev->pci_conf[0xa3] & 0x10, 1);
            break;
        case 2:
            smram_enable(dev->smram, 0x000e0000, 0x000b0000, 0x8000, dev->pci_conf[0xa3] & 0x10, 1);
            break;
        case 3:
            smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x10000, dev->pci_conf[0xa3] & 0x10, 1);
            break;

        default:
            break;
    }

    flushmmucache();
}

static void
sis_5571_mem_to_pci_reset(sis_5571_t *dev)
{
    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x71;
    dev->pci_conf[0x03] = 0x55;
    dev->pci_conf[0x04] = 0x05;
    dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = 0x00;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = 0x00;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x0c] = 0x00;
    dev->pci_conf[0x0d] = 0x00;
    dev->pci_conf[0x0e] = 0x00;
    dev->pci_conf[0x0f] = 0x00;

    dev->pci_conf[0x50] = 0x00;
    dev->pci_conf[0x51] = 0x00;
    dev->pci_conf[0x52] = 0x00;
    dev->pci_conf[0x53] = 0x00;
    dev->pci_conf[0x54] = 0x54;
    dev->pci_conf[0x55] = 0x54;
    dev->pci_conf[0x56] = 0x03;
    dev->pci_conf[0x57] = 0x00;
    dev->pci_conf[0x58] = 0x00;
    dev->pci_conf[0x59] = 0x00;
    dev->pci_conf[0x5a] = 0x00;

    /* Undocumented DRAM bank registers. */
    dev->pci_conf[0x60] = dev->pci_conf[0x62] = 0x04;
    dev->pci_conf[0x64] = dev->pci_conf[0x66] = 0x04;
    dev->pci_conf[0x68] = dev->pci_conf[0x6a] = 0x04;
    dev->pci_conf[0x61] = dev->pci_conf[0x65] = 0x00;
    dev->pci_conf[0x63] = dev->pci_conf[0x67] = 0x80;
    dev->pci_conf[0x69]                       = 0x00;
    dev->pci_conf[0x6b]                       = 0x80;

    dev->pci_conf[0x70] = 0x00;
    dev->pci_conf[0x71] = 0x00;
    dev->pci_conf[0x72] = 0x00;
    dev->pci_conf[0x73] = 0x00;
    dev->pci_conf[0x74] = 0x00;
    dev->pci_conf[0x75] = 0x00;
    dev->pci_conf[0x76] = 0x00;

    dev->pci_conf[0x77] = 0x00;
    dev->pci_conf[0x78] = 0x00;
    dev->pci_conf[0x79] = 0x00;
    dev->pci_conf[0x7a] = 0x00;
    dev->pci_conf[0x7b] = 0x00;

    dev->pci_conf[0x80] = 0x00;
    dev->pci_conf[0x81] = 0x00;
    dev->pci_conf[0x82] = 0x00;
    dev->pci_conf[0x83] = 0x00;
    dev->pci_conf[0x84] = 0x00;
    dev->pci_conf[0x85] = 0x00;
    dev->pci_conf[0x86] = 0x00;
    dev->pci_conf[0x87] = 0x00;

    dev->pci_conf[0x8c] = 0x00;
    dev->pci_conf[0x8d] = 0x00;
    dev->pci_conf[0x8e] = 0x00;
    dev->pci_conf[0x8f] = 0x00;

    dev->pci_conf[0x90] = 0x00;
    dev->pci_conf[0x91] = 0x00;
    dev->pci_conf[0x92] = 0x00;
    dev->pci_conf[0x93] = 0x00;
    dev->pci_conf[0x93] = 0x00;
    dev->pci_conf[0x94] = 0x00;
    dev->pci_conf[0x95] = 0x00;
    dev->pci_conf[0x96] = 0x00;
    dev->pci_conf[0x97] = 0x00;
    dev->pci_conf[0x98] = 0x00;
    dev->pci_conf[0x99] = 0x00;
    dev->pci_conf[0x9a] = 0x00;
    dev->pci_conf[0x9b] = 0x00;
    dev->pci_conf[0x9c] = 0x00;
    dev->pci_conf[0x9d] = 0x00;
    dev->pci_conf[0x9e] = 0xff;
    dev->pci_conf[0x9f] = 0xff;

    dev->pci_conf[0xa0] = 0xff;
    dev->pci_conf[0xa1] = 0x00;
    dev->pci_conf[0xa2] = 0xff;
    dev->pci_conf[0xa3] = 0x00;

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    sis_5571_smram_recalc(dev);
    sis_5571_shadow_recalc(dev);

    flushmmucache();
}

static void
sis_5571_mem_to_pci_write(int func, int addr, uint8_t val, void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    if (func == 0) {
        sis_5571_log("SiS 5571 M2P: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

        switch (addr) {
            case 0x04: /* Command - low byte */
            case 0x05: /* Command - high byte */
                dev->pci_conf[addr] = (dev->pci_conf[addr] & 0xfd) | (val & 0x02);
                break;

            case 0x07: /* Status - High Byte */
                dev->pci_conf[addr] &= ~(val & 0xb8);
                break;

            case 0x0d: /* Master latency timer */
                dev->pci_conf[addr] = val;
                break;

            case 0x50: /* Host Interface and DRAM arbiter */
                dev->pci_conf[addr] = val & 0xec;
                break;

            case 0x51: /* CACHE */
                dev->pci_conf[addr]   = val;
                cpu_cache_ext_enabled = !!(val & 0x40);
                cpu_update_waitstates();
                break;

            case 0x52:
                dev->pci_conf[addr] = val & 0xd0;
                break;

            case 0x53: /* DRAM */
                dev->pci_conf[addr] = val & 0xfe;
                break;

            case 0x54: /* FP/EDO */
                dev->pci_conf[addr] = val;
                break;

            case 0x55:
                dev->pci_conf[addr] = val & 0xe0;
                break;

            case 0x56: /* MDLE delay */
                dev->pci_conf[addr] = val & 0x07;
                break;

            case 0x57: /* SDRAM */
                dev->pci_conf[addr] = val & 0xf8;
                break;

            case 0x59: /* Buffer strength and current rating  */
                dev->pci_conf[addr] = val;
                break;

            case 0x5a:
                dev->pci_conf[addr] = val & 0x03;
                break;

            /* Undocumented - DRAM bank registers, the exact layout is currently unknown. */
            case 0x60 ... 0x6b:
                dev->pci_conf[addr] = val;
                break;

            case 0x70 ... 0x75:
                dev->pci_conf[addr] = val & 0xee;
                sis_5571_shadow_recalc(dev);
                break;
            case 0x76:
                dev->pci_conf[addr] = val & 0xe8;
                sis_5571_shadow_recalc(dev);
                break;

            case 0x77: /* Characteristics of non-cacheable area */
                dev->pci_conf[addr] = val & 0x0f;
                break;

            case 0x78: /* Allocation of Non-Cacheable area #1 */
            case 0x79: /* NCA1REG2 */
            case 0x7a: /* Allocation of Non-Cacheable area #2 */
            case 0x7b: /* NCA2REG2 */
                dev->pci_conf[addr] = val;
                break;

            case 0x80: /* PCI master characteristics */
                dev->pci_conf[addr] = val & 0xfe;
                break;

            case 0x81:
                dev->pci_conf[addr] = val & 0xcc;
                break;

            case 0x82:
                dev->pci_conf[addr] = val;
                break;

            case 0x83: /* CPU to PCI characteristics */
                dev->pci_conf[addr] = val;
                /* TODO: Implement Fast A20 and Fast reset stuff on the KBC already! */
                break;

            case 0x84 ... 0x86:
                dev->pci_conf[addr] = val;
                break;

            case 0x87: /* Miscellanea */
                dev->pci_conf[addr] = val & 0xf8;
                break;

            case 0x90: /* PMU control register */
            case 0x91: /* Address trap for green function */
            case 0x92:
                dev->pci_conf[addr] = val;
                break;

            case 0x93: /* STPCLK# and APM SMI control */
                dev->pci_conf[addr] = val;

                if ((dev->pci_conf[0x9b] & 0x01) && (val & 0x02)) {
                    smi_raise();
                    dev->pci_conf[0x9d] |= 0x01;
                }
                break;

            case 0x94: /* 6x86 and Green function control */
                dev->pci_conf[addr] = val & 0xf8;
                break;

            case 0x95: /* Test mode control */
            case 0x96: /* Time slot and Programmable 10-bit I/O port definition */
                dev->pci_conf[addr] = val & 0xfb;
                break;

            case 0x97: /* programmable 10-bit I/O port address */
            case 0x98: /* Programmable 16-bit I/O port */
            case 0x99 ... 0x9c:
                dev->pci_conf[addr] = val;
                break;

            case 0x9d:
                dev->pci_conf[addr] &= val;
                break;

            case 0x9e: /* STPCLK# Assertion Timer */
            case 0x9f: /* STPCLK# De-assertion Timer */
            case 0xa0 ... 0xa2:
                dev->pci_conf[addr] = val;
                break;

            case 0xa3: /* SMRAM access control and Power supply control */
                dev->pci_conf[addr] = val & 0xd0;
                sis_5571_smram_recalc(dev);
                break;

            default:
                break;
        }
    }
}

static uint8_t
sis_5571_mem_to_pci_read(int func, int addr, void *priv)
{
    const sis_5571_t *dev = (sis_5571_t *) priv;
    uint8_t ret = 0xff;

    if (func == 0x00) {
        ret = dev->pci_conf[addr];

        sis_5571_log("SiS 5571 M2P: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);
    }

    return ret;
}

static void
sis_5571_pci_to_isa_reset(sis_5571_t *dev)
{
    /* PCI to ISA Bridge */
    dev->pci_conf_sb[0][0x00] = 0x39;
    dev->pci_conf_sb[0][0x01] = 0x10;
    dev->pci_conf_sb[0][0x02] = 0x08;
    dev->pci_conf_sb[0][0x03] = 0x00;
    dev->pci_conf_sb[0][0x04] = 0x07;
    dev->pci_conf_sb[0][0x05] = 0x00;
    dev->pci_conf_sb[0][0x06] = 0x00;
    dev->pci_conf_sb[0][0x07] = 0x02;
    dev->pci_conf_sb[0][0x08] = 0x01;
    dev->pci_conf_sb[0][0x09] = 0x00;
    dev->pci_conf_sb[0][0x0a] = 0x01;
    dev->pci_conf_sb[0][0x0b] = 0x06;
    dev->pci_conf_sb[0][0x0e] = 0x80;

    dev->pci_conf_sb[0][0x40] = 0x00;
    dev->pci_conf_sb[0][0x41] = dev->pci_conf_sb[0][0x42] = 0x80;
    dev->pci_conf_sb[0][0x43] = dev->pci_conf_sb[0][0x44] = 0x80;
    dev->pci_conf_sb[0][0x45] = 0x00;
    dev->pci_conf_sb[0][0x46] = 0x00;
    dev->pci_conf_sb[0][0x47] = 0x00;
    dev->pci_conf_sb[0][0x48] = dev->pci_conf_sb[0][0x49] = 0x00;
    dev->pci_conf_sb[0][0x4a] = dev->pci_conf_sb[0][0x4b] = 0x00;
    dev->pci_conf_sb[0][0x61] = 0x80;
    dev->pci_conf_sb[0][0x62] = 0x00;
    dev->pci_conf_sb[0][0x63] = 0x80;
    dev->pci_conf_sb[0][0x64] = 0x00;
    dev->pci_conf_sb[0][0x65] = 0x00;
    dev->pci_conf_sb[0][0x66] = dev->pci_conf_sb[0][0x67] = 0x00;
    dev->pci_conf_sb[0][0x68] = 0x80;
    dev->pci_conf_sb[0][0x69] = dev->pci_conf_sb[0][0x6a] = 0x00;
    dev->pci_conf_sb[0][0x6b] = 0x00;
    dev->pci_conf_sb[0][0x6c] = 0x02;
    dev->pci_conf_sb[0][0x6d] = 0x00;
    dev->pci_conf_sb[0][0x6e] = dev->pci_conf_sb[0][0x6f] = 0x00;
    dev->pci_conf_sb[0][0x70] = dev->pci_conf_sb[0][0x71] = 0x00;
    dev->pci_conf_sb[0][0x72] = dev->pci_conf_sb[0][0x73] = 0x00;
    dev->pci_conf_sb[0][0x74] = dev->pci_conf_sb[0][0x75] = 0x00;
    dev->pci_conf_sb[0][0x76] = dev->pci_conf_sb[0][0x77] = 0x00;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ3, PCI_IRQ_DISABLED);

    cpu_set_isa_speed(7159091);
    nvr_bank_set(0, 0, dev->nvr);
}

static void
sis_5571_pci_to_isa_write(int addr, uint8_t val, void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;
    uint8_t old;

    sis_5571_log("SiS 5571 P2I: [W] dev->pci_conf_sb[0][%02X] = %02X\n", addr, val);

    switch (addr) {
        default:
            break;

        case 0x04: /* Command */
            // dev->pci_conf_sb[0][addr] = val & 0x0f;
            break;

        case 0x07: /* Status */
            dev->pci_conf_sb[0][addr] &= ~(val & 0x30);
            break;

        case 0x40: /* BIOS Control Register */
            dev->pci_conf_sb[0][addr] = val & 0x3f;
            break;

        case 0x41: /* INTA# Remapping Control Register */
        case 0x42: /* INTB# Remapping Control Register */
        case 0x43: /* INTC# Remapping Control Register */
        case 0x44: /* INTD# Remapping Control Register */
            dev->pci_conf_sb[0][addr] = val & 0x8f;
            pci_set_irq_routing(addr & 0x07, (val & 0x80) ? PCI_IRQ_DISABLED : (val & 0x0f));
            break;

        case 0x45:
            dev->pci_conf_sb[0][addr] = val & 0xec;
            switch (val >> 6) {
                case 0:
                    cpu_set_isa_speed(7159091);
                    break;
                case 1:
                    cpu_set_isa_pci_div(4);
                    break;
                case 2:
                    cpu_set_isa_pci_div(3);
                    break;

                default:
                    break;
            }
            nvr_bank_set(0, !!(val & 0x08), dev->nvr);
            break;

        case 0x46:
            dev->pci_conf_sb[0][addr] = val & 0xec;
            break;

        case 0x47: /* DMA Clock and Wait State Control Register */
            dev->pci_conf_sb[0][addr] = val & 0x3e;
            break;

        case 0x48: /* ISA Master/DMA Memory Cycle Control Register 1 */
        case 0x49: /* ISA Master/DMA Memory Cycle Control Register 2 */
        case 0x4a: /* ISA Master/DMA Memory Cycle Control Register 3 */
        case 0x4b: /* ISA Master/DMA Memory Cycle Control Register 4 */
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x60:
            outb(0x0070, val);
            break;

        /* Simply skip MIRQ0, so we can reuse the SiS 551x IDEIRQ infrastructure. */
        case 0x61: /* MIRQ Remapping Control Register */
            sis_5571_log("Set MIRQ routing: MIRQ%i -> %02X\n", addr & 0x01, val);
            dev->pci_conf_sb[0][addr] = val & 0xcf;
            if (val & 0x80)
                pci_set_mirq_routing(PCI_MIRQ0 + (addr & 0x01), PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ0 + (addr & 0x01), val & 0xf);
            break;

        case 0x62: /* On-board Device DMA Control Register */
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x63: /* IDEIRQ Remapping Control Register */
            sis_5571_log("Set MIRQ routing: IDEIRQ -> %02X\n", val);
            dev->pci_conf_sb[0][addr] = val & 0x8f;
            if (val & 0x80)
                 pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ2, val & 0xf);
            break;

        case 0x64: /* GPIO Control Register */
            dev->pci_conf_sb[0][addr] = val & 0xef;
            break;

        case 0x65:
            dev->pci_conf_sb[0][addr] = val & 0x1b;
            break;

        case 0x66: /* GPIO Output Mode Control Register */
        case 0x67: /* GPIO Output Mode Control Register */
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x68: /* USBIRQ Remapping Control Register */
            sis_5571_log("Set MIRQ routing: USBIRQ -> %02X\n", val);
            dev->pci_conf_sb[0][addr] = val & 0xcf;
            if (val & 0x80)
                 pci_set_mirq_routing(PCI_MIRQ3, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ3, val & 0xf);
            break;

        case 0x69:
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x6a:
            dev->pci_conf_sb[0][addr] = val & 0xfc;
            break;

        case 0x6b:
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x6c:
            dev->pci_conf_sb[0][addr] = val & 0x02;
            break;

        case 0x6e: /* Software-Controlled Interrupt Request, Channels 7-0 */
            old = dev->pci_conf_sb[0][addr];
            picint((val ^ old) & val);
            picintc((val ^ old) & ~val);
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x6f: /* Software-Controlled Interrupt Request, channels 15-8 */
            old = dev->pci_conf_sb[0][addr];
            picint(((val ^ old) & val) << 8);
            picintc(((val ^ old) & ~val) << 8);
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x70:
            dev->pci_conf_sb[0][addr] = (dev->pci_conf_sb[0][addr] & 0x02) | (val & 0xdc);
            break;

        case 0x71: /* Type-F DMA Control Register */
            dev->pci_conf_sb[0][addr] = val & 0xef;
            break;

        case 0x72: /* SMI Triggered By IRQ/GPIO Control */
        case 0x73: /* SMI Triggered By IRQ/GPIO Control */
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x74: /* System Standby Timer Reload,
                      System Standby State Exit And Throttling State Exit Control */
        case 0x75: /* System Standby Timer Reload,
                      System Standby State Exit And Throttling State Exit Control */
        case 0x76: /* Monitor Standby Timer Reload And Monitor Standby State ExitControl */
        case 0x77: /* Monitor Standby Timer Reload And Monitor Standby State ExitControl */
            dev->pci_conf_sb[0][addr] = val;
            break;
    }
}

static uint8_t
sis_5571_pci_to_isa_read(int addr, void *priv)
{
    const sis_5571_t *dev = (sis_5571_t *) priv;
    uint8_t ret = 0xff;

    switch (addr) {
        default:
            ret = dev->pci_conf_sb[0][addr];
            break;
        case 0x4c ... 0x4f:
            ret = pic_read_icw(0, addr & 0x03);
            break;
        case 0x50 ... 0x53:
            ret = pic_read_icw(1, addr & 0x03);
            break;
        case 0x54 ... 0x55:
            ret = pic_read_ocw(0, addr & 0x01);
            break;
        case 0x56 ... 0x57:
            ret = pic_read_ocw(1, addr & 0x01);
            break;
        case 0x58 ... 0x5f:
            ret = dev->pit_read_reg(dev->pit, addr & 0x07);
            break;
        case 0x60:
            ret = inb(0x0070);
            break;
    }

    sis_5571_log("SiS 5571 P2I: [R] dev->pci_conf_sb[0][%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5571_ide_irq_handler(sis_5571_t *dev)
{
    if (dev->pci_conf_sb[1][0x09] & 0x01) {
        /* Primary IDE is native. */
        sis_5571_log("Primary IDE IRQ mode: Native, Native\n");
        sff_set_irq_mode(dev->bm[0], IRQ_MODE_SIS_551X);
    } else {
        /* Primary IDE is legacy. */
        sis_5571_log("Primary IDE IRQ mode: IRQ14, IRQ15\n");
        sff_set_irq_mode(dev->bm[0], IRQ_MODE_LEGACY);
    }

    if (dev->pci_conf_sb[1][0x09] & 0x04) {
        /* Secondary IDE is native. */
        sis_5571_log("Secondary IDE IRQ mode: Native, Native\n");
        sff_set_irq_mode(dev->bm[1], IRQ_MODE_SIS_551X);
    } else {
        /* Secondary IDE is legacy. */
        sis_5571_log("Secondary IDE IRQ mode: IRQ14, IRQ15\n");
        sff_set_irq_mode(dev->bm[1], IRQ_MODE_LEGACY);
    }
}

static void
sis_5571_ide_handler(sis_5571_t *dev)
{
    uint8_t ide_io_on = dev->pci_conf_sb[1][0x04] & 0x01;

    uint16_t native_base_pri_addr = (dev->pci_conf_sb[1][0x11] | dev->pci_conf_sb[1][0x10] << 8) & 0xfffe;
    uint16_t native_side_pri_addr = (dev->pci_conf_sb[1][0x15] | dev->pci_conf_sb[1][0x14] << 8) & 0xfffe;
    uint16_t native_base_sec_addr = (dev->pci_conf_sb[1][0x19] | dev->pci_conf_sb[1][0x18] << 8) & 0xfffe;
    uint16_t native_side_sec_addr = (dev->pci_conf_sb[1][0x1c] | dev->pci_conf_sb[1][0x1b] << 8) & 0xfffe;

    uint16_t current_pri_base;
    uint16_t current_pri_side;
    uint16_t current_sec_base;
    uint16_t current_sec_side;

    /* Primary Channel Programming */
    current_pri_base = (!(dev->pci_conf_sb[1][0x09] & 1)) ? 0x01f0 : native_base_pri_addr;
    current_pri_side = (!(dev->pci_conf_sb[1][0x09] & 1)) ? 0x03f6 : native_side_pri_addr;

    /* Secondary Channel Programming */
    current_sec_base = (!(dev->pci_conf_sb[1][0x09] & 4)) ? 0x0170 : native_base_sec_addr;
    current_sec_side = (!(dev->pci_conf_sb[1][0x09] & 4)) ? 0x0376 : native_side_sec_addr;

    sis_5571_log("sis_5571_ide_handler(): Disabling primary IDE...\n");
    ide_pri_disable();
    sis_5571_log("sis_5571_ide_handler(): Disabling secondary IDE...\n");
    ide_sec_disable();

    if (ide_io_on) {
        /* Primary Channel Setup */
        if (dev->pci_conf_sb[1][0x4a] & 0x02) {
            sis_5571_log("sis_5571_ide_handler(): Primary IDE base now %04X...\n", current_pri_base);
            ide_set_base(0, current_pri_base);
            sis_5571_log("sis_5571_ide_handler(): Primary IDE side now %04X...\n", current_pri_side);
            ide_set_side(0, current_pri_side);

            sis_5571_log("sis_5571_ide_handler(): Enabling primary IDE...\n");
            ide_pri_enable();

            sis_5571_log("SiS 5571 PRI: BASE %04x SIDE %04x\n", current_pri_base, current_pri_side);
        }

        /* Secondary Channel Setup */
        if (dev->pci_conf_sb[1][0x4a] & 0x04) {
            sis_5571_log("sis_5571_ide_handler(): Secondary IDE base now %04X...\n", current_sec_base);
            ide_set_base(1, current_sec_base);
            sis_5571_log("sis_5571_ide_handler(): Secondary IDE side now %04X...\n", current_sec_side);
            ide_set_side(1, current_sec_side);

            sis_5571_log("sis_5571_ide_handler(): Enabling secondary IDE...\n");
            ide_sec_enable();

            sis_5571_log("SiS 5571: BASE %04x SIDE %04x\n", current_sec_base, current_sec_side);
        }
    }

    sff_bus_master_handler(dev->bm[0], ide_io_on,
                           ((dev->pci_conf_sb[1][0x20] & 0xf0) | (dev->pci_conf_sb[1][0x21] << 8)) + 0);
    sff_bus_master_handler(dev->bm[1], ide_io_on,
                           ((dev->pci_conf_sb[1][0x20] & 0xf0) | (dev->pci_conf_sb[1][0x21] << 8)) + 8);
}

static void
sis_5571_ide_reset(sis_5571_t *dev)
{
    /* PCI IDE */
    dev->pci_conf_sb[1][0x00] = 0x39;
    dev->pci_conf_sb[1][0x01] = 0x10;
    dev->pci_conf_sb[1][0x02] = 0x13;
    dev->pci_conf_sb[1][0x03] = 0x55;
    dev->pci_conf_sb[1][0x04] = dev->pci_conf_sb[1][0x05] = 0x00;
    dev->pci_conf_sb[1][0x06] = dev->pci_conf_sb[1][0x07] = 0x00;
    dev->pci_conf_sb[1][0x08] = 0xc0;
    dev->pci_conf_sb[1][0x09] = 0x8a;
    dev->pci_conf_sb[1][0x0a] = dev->pci_conf_sb[1][0x0b] = 0x01;
    dev->pci_conf_sb[1][0x0c] = dev->pci_conf_sb[1][0x0d] = 0x00;
    dev->pci_conf_sb[1][0x0e] = 0x80;
    dev->pci_conf_sb[1][0x0f] = 0x00;
    dev->pci_conf_sb[1][0x10] = 0xf1;
    dev->pci_conf_sb[1][0x11] = 0x01;
    dev->pci_conf_sb[1][0x14] = 0xf5;
    dev->pci_conf_sb[1][0x15] = 0x03;
    dev->pci_conf_sb[1][0x18] = 0x71;
    dev->pci_conf_sb[1][0x19] = 0x01;
    dev->pci_conf_sb[1][0x1c] = 0x75;
    dev->pci_conf_sb[1][0x1d] = 0x03;
    dev->pci_conf_sb[1][0x20] = 0x01;
    dev->pci_conf_sb[1][0x21] = 0xf0;
    dev->pci_conf_sb[1][0x22] = dev->pci_conf_sb[1][0x23] = 0x00;
    dev->pci_conf_sb[1][0x24] = dev->pci_conf_sb[1][0x25] = 0x00;
    dev->pci_conf_sb[1][0x26] = dev->pci_conf_sb[1][0x27] = 0x00;
    dev->pci_conf_sb[1][0x28] = dev->pci_conf_sb[1][0x29] = 0x00;
    dev->pci_conf_sb[1][0x2a] = dev->pci_conf_sb[1][0x2b] = 0x00;
#ifdef DATASHEET
    dev->pci_conf_sb[1][0x2c] = dev->pci_conf_sb[1][0x2d] = 0x00;
#else
    /* The only Linux lspci listing I could find of this chipset,
       shows a subsystem of 0058:0000. */
    dev->pci_conf_sb[1][0x2c] = 0x58;
    dev->pci_conf_sb[1][0x2d] = 0x00;
#endif
    dev->pci_conf_sb[1][0x2e] = dev->pci_conf_sb[1][0x2f] = 0x00;
    dev->pci_conf_sb[1][0x30] = dev->pci_conf_sb[1][0x31] = 0x00;
    dev->pci_conf_sb[1][0x32] = dev->pci_conf_sb[1][0x33] = 0x00;
    dev->pci_conf_sb[1][0x40] = dev->pci_conf_sb[1][0x41] = 0x00;
    dev->pci_conf_sb[1][0x42] = dev->pci_conf_sb[1][0x43] = 0x00;
    dev->pci_conf_sb[1][0x44] = dev->pci_conf_sb[1][0x45] = 0x00;
    dev->pci_conf_sb[1][0x46] = dev->pci_conf_sb[1][0x47] = 0x00;
    dev->pci_conf_sb[1][0x48] = dev->pci_conf_sb[1][0x49] = 0x00;
    dev->pci_conf_sb[1][0x4a] = 0x06;
    dev->pci_conf_sb[1][0x4b] = 0x00;
    dev->pci_conf_sb[1][0x4c] = dev->pci_conf_sb[1][0x4d] = 0x00;
    dev->pci_conf_sb[1][0x4e] = dev->pci_conf_sb[1][0x4f] = 0x00;

    sis_5571_ide_irq_handler(dev);
    sis_5571_ide_handler(dev);

    sff_bus_master_reset(dev->bm[0]);
    sff_bus_master_reset(dev->bm[1]);
}

static void
sis_5571_ide_write(int addr, uint8_t val, void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    sis_5571_log("SiS 5571 IDE: [W] dev->pci_conf_sb[1][%02X] = %02X\n", addr, val);

    switch (addr) {
        default:
            break;

        case 0x04: /* Command low byte */
            dev->pci_conf_sb[1][addr] = val & 0x05;
            sis_5571_ide_handler(dev);
            break;
        case 0x06: /* Status low byte */
            dev->pci_conf_sb[1][addr] = val & 0x20;
            break;
        case 0x07: /* Status high byte */
            dev->pci_conf_sb[1][addr] = (dev->pci_conf_sb[1][addr] & 0x06) & ~(val & 0x38);
            break;
        case 0x09: /* Programming Interface Byte */
            dev->pci_conf_sb[1][addr] = (dev->pci_conf_sb[1][addr] & 0x8a) | (val & 0x45);
            sis_5571_ide_irq_handler(dev);
            sis_5571_ide_handler(dev);
            break;
        case 0x0d: /* Latency Timer */
            dev->pci_conf_sb[1][addr] = val;
            break;

        /* Primary Base Address */
        case 0x10:
        case 0x11:
        case 0x14:
        case 0x15:
            fallthrough;

        /* Secondary Base Address */
        case 0x18:
        case 0x19:
        case 0x1c:
        case 0x1d:
            fallthrough;

        /* Bus Mastering Base Address */
        case 0x20:
        case 0x21:
            if (addr == 0x20)
                dev->pci_conf_sb[1][addr] = (val & 0xe0) | 0x01;
            else
                dev->pci_conf_sb[1][addr] = val;
            sis_5571_ide_handler(dev);
            break;

    /* The only Linux lspci listing I could find of this chipset,
       does not show any BIOS bar, therefore writes to that are disabled. */
#ifdef DATASHEET
        case 0x30: /* Expansion ROM Base Address */
        case 0x31: /* Expansion ROM Base Address */
        case 0x32: /* Expansion ROM Base Address */
        case 0x33: /* Expansion ROM Base Address */
            dev->pci_conf_sb[1][addr] = val;
            break;
#endif

        case 0x40: /* IDE Primary Channel/Master Drive Data Recovery Time Control */
        case 0x42: /* IDE Primary Channel/Slave Drive Data Recovery Time Control */
        case 0x44: /* IDE Secondary Channel/Master Drive Data Recovery Time Control */
        case 0x46: /* IDE Secondary Channel/Slave Drive Data Recovery Time Control */
        case 0x48: /* IDE Command Recovery Time Control */
            dev->pci_conf_sb[1][addr] = val & 0x0f;
            break;

        case 0x41: /* IDE Primary Channel/Master Drive DataActive Time Control */
        case 0x43: /* IDE Primary Channel/Slave Drive Data Active Time Control */
        case 0x45: /* IDE Secondary Channel/Master Drive Data Active Time Control */
        case 0x47: /* IDE Secondary Channel/Slave Drive Data Active Time Control */
        case 0x49: /* IDE Command Active Time Control */
            dev->pci_conf_sb[1][addr] = val & 0x07;
            break;

        case 0x4a: /* IDE General Control Register 0 */
            dev->pci_conf_sb[1][addr] = val & 0xaf;
            sis_5571_ide_handler(dev);
            break;

        case 0x4b: /* IDE General Control register 1 */
            dev->pci_conf_sb[1][addr] = val;
            break;

        case 0x4c: /* Prefetch Count of Primary Channel (Low Byte) */
        case 0x4d: /* Prefetch Count of Primary Channel (High Byte) */
        case 0x4e: /* Prefetch Count of Secondary Channel (Low Byte) */
        case 0x4f: /* Prefetch Count of Secondary Channel (High Byte) */
            dev->pci_conf_sb[1][addr] = val;
            break;
    }
}

static uint8_t
sis_5571_ide_read(int addr, void *priv)
{
    const sis_5571_t *dev = (sis_5571_t *) priv;
    uint8_t ret = 0xff;

    switch (addr) {
        default:
            ret = dev->pci_conf_sb[1][addr];
            break;

        case 0x09:
            ret = dev->pci_conf_sb[1][addr];
            if (dev->pci_conf_sb[1][0x09] & 0x40)
                ret |= ((dev->pci_conf_sb[1][0x4a] & 0x06) << 3);
            break;

        case 0x3d:
            ret = (dev->pci_conf_sb[1][0x09] & 0x05) ? PCI_INTA : 0x00;
            break;
    }

    sis_5571_log("SiS 5571 IDE: [R] dev->pci_conf_sb[1][%02X] = %02X\n", addr, ret);

    return ret;
}

/* SiS 5571 unknown I/O port (second USB PCI BAR). */
static void
sis_5571_usb_unk_write(uint16_t addr, uint8_t val, void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    addr = (addr - dev->usb_unk_base) & 0x07;

    sis_5571_log("SiS 5571 USB UNK: [W] dev->usb_unk_regs[%02X] = %02X\n", addr, val);

    dev->usb_unk_regs[addr] = val;
}

static uint8_t
sis_5571_usb_unk_read(uint16_t addr, void *priv)
{
    const sis_5571_t *dev = (sis_5571_t *) priv;
    uint8_t ret = 0xff;

    addr = (addr - dev->usb_unk_base) & 0x07;

    ret = dev->usb_unk_regs[addr & 0x07];

    sis_5571_log("SiS 5571 USB UNK: [R] dev->usb_unk_regs[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5571_usb_reset(sis_5571_t *dev)
{
    /* USB */
    dev->pci_conf_sb[2][0x00] = 0x39;
    dev->pci_conf_sb[2][0x01] = 0x10;
    dev->pci_conf_sb[2][0x02] = 0x01;
    dev->pci_conf_sb[2][0x03] = 0x70;
    dev->pci_conf_sb[2][0x04] = dev->pci_conf_sb[1][0x05] = 0x00;
    dev->pci_conf_sb[2][0x06] = 0x00;
    dev->pci_conf_sb[2][0x07] = 0x02;
    dev->pci_conf_sb[2][0x08] = 0xb0;
    dev->pci_conf_sb[2][0x09] = 0x10;
    dev->pci_conf_sb[2][0x0a] = 0x03;
    dev->pci_conf_sb[2][0x0b] = 0x0c;
    dev->pci_conf_sb[2][0x0c] = dev->pci_conf_sb[1][0x0d] = 0x00;
    dev->pci_conf_sb[2][0x0e] = 0x80 /* 0x10  - Datasheet erratum - header type 0x10 is invalid! */;
    dev->pci_conf_sb[2][0x0f] = 0x00;
    dev->pci_conf_sb[2][0x10] = 0x00;
    dev->pci_conf_sb[2][0x11] = 0x00;
    dev->pci_conf_sb[2][0x12] = 0x00;
    dev->pci_conf_sb[2][0x13] = 0x00;
    dev->pci_conf_sb[2][0x14] = 0x01;
    dev->pci_conf_sb[2][0x15] = 0x00;
    dev->pci_conf_sb[2][0x16] = 0x00;
    dev->pci_conf_sb[2][0x17] = 0x00;
    dev->pci_conf_sb[2][0x3c] = 0x00;
    dev->pci_conf_sb[2][0x3d] = PCI_INTA;
    dev->pci_conf_sb[2][0x3e] = 0x00;
    dev->pci_conf_sb[2][0x3f] = 0x00;

    ohci_update_mem_mapping(dev->usb,
                            dev->pci_conf_sb[2][0x11], dev->pci_conf_sb[2][0x12],
                            dev->pci_conf_sb[2][0x13], dev->pci_conf_sb[2][0x04] & 0x02);

    if (dev->usb_unk_base != 0x0000) {
        io_removehandler(dev->usb_unk_base, 0x0002,
                         sis_5571_usb_unk_read, NULL, NULL,
                         sis_5571_usb_unk_write, NULL, NULL, dev);
    }

    dev->usb_unk_base = 0x0000;

    memset(dev->usb_unk_regs, 0x00, sizeof(dev->usb_unk_regs));
}

static void
sis_5571_usb_write(int addr, uint8_t val, void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    sis_5571_log("SiS 5571 USB: [W] dev->pci_conf_sb[2][%02X] = %02X\n", addr, val);

    if (dev->pci_conf_sb[0][0x68] & 0x40)  switch (addr) {
        default:
            break;

        case 0x04: /* Command - Low Byte */
            dev->pci_conf_sb[2][addr] = val & 0x47;
            if (dev->usb_unk_base != 0x0000) {
                io_removehandler(dev->usb_unk_base, 0x0002,
                                 sis_5571_usb_unk_read, NULL, NULL,
                                 sis_5571_usb_unk_write, NULL, NULL, dev);
                if (dev->pci_conf_sb[2][0x04] & 0x01)
                    io_sethandler(dev->usb_unk_base, 0x0002,
                                  sis_5571_usb_unk_read, NULL, NULL,
                                  sis_5571_usb_unk_write, NULL, NULL, dev);
            }
            ohci_update_mem_mapping(dev->usb,
                                    dev->pci_conf_sb[2][0x11], dev->pci_conf_sb[2][0x12],
                                    dev->pci_conf_sb[2][0x13], dev->pci_conf_sb[2][0x04] & 0x02);
            break;

        case 0x05: /* Command - High Byte */
            dev->pci_conf_sb[2][addr] = val & 0x01;
            break;

        case 0x07: /* Status - High Byte */
            dev->pci_conf_sb[2][addr] &= ~(val & 0xf9);
            break;

        case 0x0d: /* Latency Timer */
            dev->pci_conf_sb[2][addr] = val;
            break;

        case 0x11: /* Memory Space Base Address Register */
        case 0x12: /* Memory Space Base Address Register */
        case 0x13: /* Memory Space Base Address Register */
            dev->pci_conf_sb[2][addr] = val & ((addr == 0x11) ? 0xf0 : 0xff);
            ohci_update_mem_mapping(dev->usb,
                                    dev->pci_conf_sb[2][0x11], dev->pci_conf_sb[2][0x12],
                                    dev->pci_conf_sb[2][0x13], dev->pci_conf_sb[2][4] & 0x02);
            break;

        case 0x14: /* IO Space Base Address Register */
        case 0x15: /* IO Space Base Address Register */
            if (dev->usb_unk_base != 0x0000) {
                io_removehandler(dev->usb_unk_base, 0x0002,
                                 sis_5571_usb_unk_read, NULL, NULL,
                                 sis_5571_usb_unk_write, NULL, NULL, dev);
            }
            dev->pci_conf_sb[2][addr] = val;
            dev->usb_unk_base = (dev->pci_conf_sb[2][0x14] & 0xf8) |
                                (dev->pci_conf_sb[2][0x15] << 8);
            if (dev->usb_unk_base != 0x0000) {
                io_sethandler(dev->usb_unk_base, 0x0002,
                              sis_5571_usb_unk_read, NULL, NULL,
                              sis_5571_usb_unk_write, NULL, NULL, dev);
            }
            break;

        case 0x3c: /* Interrupt Line */
            dev->pci_conf_sb[2][addr] = val;
            break;
    }
}

static uint8_t
sis_5571_usb_read(int addr, void *priv)
{
    const sis_5571_t *dev = (sis_5571_t *) priv;
    uint8_t ret = 0xff;

    if (dev->pci_conf_sb[0][0x68] & 0x40) {
        ret = dev->pci_conf_sb[2][addr];

        sis_5571_log("SiS 5571 USB: [R] dev->pci_conf_sb[2][%02X] = %02X\n", addr, ret);
    }

    return ret;
}

static void
sis_5571_sb_write(int func, int addr, uint8_t val, void *priv)
{
    switch (func) {
        case 0x00:
            sis_5571_pci_to_isa_write(addr, val, priv);
            break;
        case 0x01:
            sis_5571_ide_write(addr, val, priv);
            break;
        case 0x02:
            sis_5571_usb_write(addr, val, priv);
            break;
    }
}

static uint8_t
sis_5571_sb_read(int func, int addr, void *priv)
{
    uint8_t ret = 0xff;

    switch (func) {
        case 0x00:
            ret = sis_5571_pci_to_isa_read(addr, priv);
            break;
        case 0x01:
            ret = sis_5571_ide_read(addr, priv);
            break;
        case 0x02:
            ret = sis_5571_usb_read(addr, priv);
            break;
    }

    return ret;
}

static void
sis_5571_reset(void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    /* Memory/PCI Bridge */
    sis_5571_mem_to_pci_reset(dev);

    /* PCI to ISA bridge */
    sis_5571_pci_to_isa_reset(dev);

    /* IDE Controller */
    sis_5571_ide_reset(dev);

    /* USB Controller */
    sis_5571_usb_reset(dev);
}

static void
sis_5571_close(void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
sis_5571_init(UNUSED(const device_t *info))
{
    sis_5571_t *dev = (sis_5571_t *) calloc(1, sizeof(sis_5571_t));
    uint8_t pit_is_fast = (((pit_mode == -1) && is486) || (pit_mode == 1));

    /* Device 0: Memory/PCI Bridge */
    pci_add_card(PCI_ADD_NORTHBRIDGE,
                 sis_5571_mem_to_pci_read, sis_5571_mem_to_pci_write, dev, &dev->nb_slot);
    /* Device 1: Southbridge */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, sis_5571_sb_read, sis_5571_sb_write, dev, &dev->sb_slot);

    /* MIRQ */
    pci_enable_mirq(1);

    /* IDEIRQ */
    pci_enable_mirq(2);

    /* USBIRQ */
    pci_enable_mirq(3);

    /* Port 92h */
    dev->port_92 = device_add(&port_92_device);

    /* SFF IDE */
    dev->bm[0] = device_add_inst(&sff8038i_device, 1);
    dev->bm[1] = device_add_inst(&sff8038i_device, 2);

    /* SMRAM */
    dev->smram = smram_add();

    /* PIT */
    dev->pit = device_find_first_priv(DEVICE_PIT);
    dev->pit_read_reg = pit_is_fast ? pitf_read_reg : pit_read_reg;

    /* NVR */
    dev->nvr = device_add(&at_mb_nvr_device);

    /* USB */
    dev->usb = device_add(&usb_device);

    sis_5571_reset(dev);

    return dev;
}

const device_t sis_5571_device = {
    .name          = "SiS 5571",
    .internal_name = "sis_5571",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = sis_5571_init,
    .close         = sis_5571_close,
    .reset         = sis_5571_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
