/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 85C50x and 550x Chipsets.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Tiseno100,
 *
 *          Copyright 2020-2024 Miran Grca.
 *          Copyright 2020-2024 Tiseno100.
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
#include <86box/apm.h>
#include <86box/machine.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/plat_unused.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/smram.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/spd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/chipset.h>

#ifdef ENABLE_SIS_85C50X_LOG
int sis_85c50x_do_log = ENABLE_SIS_85C50X_LOG;

static void
sis_85c50x_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_85c50x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_85c50x_log(fmt, ...)
#endif

typedef struct sis_85c50x_t {
    uint8_t    index;
    uint8_t    nb_slot;
    uint8_t    sb_slot;
    uint8_t    type;

    uint8_t    pci_conf[256];
    uint8_t    pci_conf_sb[256];
    uint8_t    pci_conf_ide[256];
    uint8_t    regs[256];
    uint32_t   states[13];

    smram_t   *smram[2];
    port_92_t *port_92;
    void      *pit;
    nvr_t     *nvr;

    uint8_t  (*pit_read_reg)(void *priv, uint8_t reg);
} sis_85c50x_t;

static void
sis_85c50x_shadow_recalc(sis_85c50x_t *dev)
{
    uint32_t base;
    uint32_t can_read;
    uint32_t can_write;
    uint32_t state;

    can_read  = (dev->pci_conf[0x53] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    can_write = (dev->pci_conf[0x53] & 0x20) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;

    state = can_read | can_write;
    if (dev->states[12] != state) {
        mem_set_mem_state_both(0x000f0000, 0x00010000, state);
        sis_85c50x_log("F0000-FFFFF: R%c, W%c\n",
                       (dev->pci_conf[0x53] & 0x40) ? 'I' : 'E',
                       (dev->pci_conf[0x53] & 0x20) ? 'P' : 'I');
        dev->states[12] = state;
    }

    for (uint8_t i = 0; i < 4; i++) {
        base = 0x000e0000 + (i << 14);
        state = (dev->pci_conf[0x54] & (0x80 >> i)) ?
                (can_read | can_write) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        if (dev->states[8 + i] != state) {
            mem_set_mem_state_both(base, 0x00004000, state);
            sis_85c50x_log("%05X-%05X: R%c, W%c\n", base, base + 0x3fff,
                           (dev->pci_conf[0x54] & (0x80 >> i)) ?
                           ((dev->pci_conf[0x53] & 0x40) ? 'I' : 'D') : 'E',
                           (dev->pci_conf[0x54] & (0x80 >> i)) ?
                           ((dev->pci_conf[0x53] & 0x20) ? 'P' : 'I') : 'E');
            dev->states[8 + i] = state;
        }

        base = 0x000d0000 + (i << 14);
        state = (dev->pci_conf[0x55] & (0x80 >> i)) ?
                (can_read | can_write) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        if (dev->states[4 + i] != state) {
            mem_set_mem_state_both(base, 0x00004000, state);
            sis_85c50x_log("%05X-%05X: R%c, W%c\n", base, base + 0x3fff,
                           (dev->pci_conf[0x55] & (0x80 >> i)) ?
                           ((dev->pci_conf[0x53] & 0x40) ? 'I' : 'D') : 'E',
                           (dev->pci_conf[0x55] & (0x80 >> i)) ?
                           ((dev->pci_conf[0x53] & 0x20) ? 'P' : 'I') : 'E');
            dev->states[4 + i] = state;
        }

        base = 0x000c0000 + (i << 14);
        state = (dev->pci_conf[0x56] & (0x80 >> i)) ?
                (can_read | can_write) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        if (dev->states[i] != state) {
            mem_set_mem_state_both(base, 0x00004000, state);
            sis_85c50x_log("%05X-%05X: R%c, W%c\n", base, base + 0x3fff,
                           (dev->pci_conf[0x56] & (0x80 >> i)) ?
                           ((dev->pci_conf[0x53] & 0x40) ? 'I' : 'D') : 'E',
                           (dev->pci_conf[0x56] & (0x80 >> i)) ?
                           ((dev->pci_conf[0x53] & 0x20) ? 'P' : 'I') : 'E');
            dev->states[i] = state;
        }
    }

    flushmmucache_nopc();
}

static void
sis_85c50x_smm_recalc(sis_85c50x_t *dev)
{
    /* NOTE: Naming mismatch - what the datasheet calls "host address" is what we call ram_base. */
    uint32_t host_base = (dev->pci_conf[0x64] << 20) | ((dev->pci_conf[0x65] & 0x07) << 28);

    smram_disable_all();

    if ((((dev->pci_conf[0x65] & 0xe0) >> 5) != 0x00) && (host_base == 0x00000000))
        return;

    switch ((dev->pci_conf[0x65] & 0xe0) >> 5) {
        case 0x00:
            sis_85c50x_log("SiS 50x SMRAM: 000E0000-000E7FFF -> 000E0000-000E7FFF\n");
            smram_enable(dev->smram[0], 0xe0000, 0xe0000, 0x8000, (dev->pci_conf[0x65] & 0x10), 1);
            break;
        case 0x01:
            host_base |= 0x000b0000;
            sis_85c50x_log("SiS 50x SMRAM: %08X-%08X -> 000B0000-000BFFFF\n",
                           host_base, host_base + 0x10000 - 1);
            smram_enable(dev->smram[0], host_base, 0xb0000, 0x10000, (dev->pci_conf[0x65] & 0x10), 1);
            smram_enable(dev->smram[1], host_base ^ 0x00100000, 0xb0000,
                         0x10000, (dev->pci_conf[0x65] & 0x10), 1);
            break;
        case 0x02:
            host_base |= 0x000a0000;
            sis_85c50x_log("SiS 50x SMRAM: %08X-%08X -> 000A0000-000AFFFF\n",
                           host_base, host_base + 0x10000 - 1);
            smram_enable(dev->smram[0], host_base, 0xa0000, 0x10000, (dev->pci_conf[0x65] & 0x10), 1);
            smram_enable(dev->smram[1], host_base ^ 0x00100000, 0xa0000,
                         0x10000, (dev->pci_conf[0x65] & 0x10), 1);
            break;
        case 0x04:
            host_base |= 0x000a0000;
            sis_85c50x_log("SiS 50x SMRAM: %08X-%08X -> 000A0000-000AFFFF\n",
                           host_base, host_base + 0x8000 - 1);
            smram_enable(dev->smram[0], host_base, 0xa0000, 0x8000, (dev->pci_conf[0x65] & 0x10), 1);
            smram_enable(dev->smram[1], host_base ^ 0x00100000, 0xa0000,
                         0x8000, (dev->pci_conf[0x65] & 0x10), 1);
            break;
        case 0x06:
            host_base |= 0x000b0000;
            sis_85c50x_log("SiS 50x SMRAM: %08X-%08X -> 000B0000-000BFFFF\n",
                           host_base, host_base + 0x8000 - 1);
            smram_enable(dev->smram[0], host_base, 0xb0000, 0x8000, (dev->pci_conf[0x65] & 0x10), 1);
            smram_enable(dev->smram[1], host_base ^ 0x00100000, 0xa0000,
                         0x8000, (dev->pci_conf[0x65] & 0x10), 1);
            break;
        default:
            break;
    }
}

static void
sis_85c50x_write(int func, int addr, uint8_t val, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    sis_85c50x_log("85C501: [W] (%02X, %02X) = %02X\n", func, addr, val);

    if (func == 0x00)
        switch (addr) {
            case 0x04: /* Command - low byte */
                dev->pci_conf[addr] = (dev->pci_conf[addr] & 0xb4) | (val & 0x4b);
                break;
            case 0x07: /* Status - high byte */
                dev->pci_conf[addr] = ((dev->pci_conf[addr] & 0xf9) & ~(val & 0xf8)) | (val & 0x06);
                break;
            case 0x50:
                if (dev->type & 1)
                    dev->pci_conf[addr] = val & 0xf7;
                else
                    dev->pci_conf[addr] = val;
                break;
            case 0x51: /* Cache */
                dev->pci_conf[addr]   = val;
                cpu_cache_ext_enabled = (val & 0x40);
                cpu_update_waitstates();
                break;
            case 0x52:
                dev->pci_conf[addr] = val;
                break;
            case 0x53: /* Shadow RAM */
            case 0x54:
            case 0x55:
            case 0x56:
                dev->pci_conf[addr] = val;
                sis_85c50x_shadow_recalc(dev);
                break;
            case 0x57:
            case 0x58:
            case 0x59:
            case 0x5a:
            case 0x5c:
            case 0x5d:
            case 0x5e:
            case 0x61:
            case 0x62:
            case 0x63:
            case 0x67:
            case 0x68:
            case 0x6a:
            case 0x6b:
            case 0x6c:
            case 0x6d:
            case 0x6e:
            case 0x6f:
                dev->pci_conf[addr] = val;
                break;
            case 0x5f:
                dev->pci_conf[addr] = val & 0xfe;
                break;
            case 0x5b:
                dev->pci_conf[addr] = val;
                kbc_at_set_fast_reset(!!(val & 0x40));
                break;
            case 0x60: /* SMI */
                if ((dev->pci_conf[0x68] & 0x01) && !(dev->pci_conf[addr] & 0x02) && (val & 0x02)) {
                    dev->pci_conf[0x69] |= 0x01;
                    smi_raise();
                }
                dev->pci_conf[addr] = val & 0x3e;
                break;
            case 0x64: /* SMRAM */
            case 0x65:
                dev->pci_conf[addr] = val;
                sis_85c50x_smm_recalc(dev);
                break;
            case 0x66:
                dev->pci_conf[addr] = (val & 0x7f);
                break;
            case 0x69:
                dev->pci_conf[addr] &= ~val;
                break;
            case 0x70 ... 0x77:
                if (dev->type & 1)
                    spd_write_drbs(dev->pci_conf, 0x70, 0x77, 2);
                break;
            case 0x78:
            case 0x7c ... 0x7e:
                if (dev->type & 1)
                    dev->pci_conf[addr] = val;
                break;
            case 0x79:
                if (dev->type & 1) {
                    spd_write_drbs(dev->pci_conf, 0xf8, 0xff, 4);
                    dev->pci_conf[addr] = 0x00;
                    for (uint8_t i = 0; i < 8; i++)
                        if (dev->pci_conf[0xf8 + i] & 0x80)  dev->pci_conf[addr] |= (1 << i);
                }
                break;
            case 0x7a:
                if (dev->type & 1)
                    dev->pci_conf[addr] = val & 0xfe;
                break;
            case 0x7b:
                if (dev->type & 1)
                    dev->pci_conf[addr] = val & 0xe0;
                break;

            default:
                break;
        }
}

static uint8_t
sis_85c50x_read(int func, int addr, void *priv)
{
    const sis_85c50x_t *dev = (sis_85c50x_t *) priv;
    uint8_t             ret = 0xff;

    if (func == 0x00) {
        if (addr >= 0xf8)
            ret = 0x00;
        else
            ret = dev->pci_conf[addr];
    }

    sis_85c50x_log("85C501: [R] (%02X, %02X) = %02X\n", func, addr, ret);

    return ret;
}

static void
sis_85c50x_ide_recalc(sis_85c50x_t *dev)
{
    ide_pri_disable();
    ide_set_base(0, (dev->pci_conf_ide[0x40] & 0x80) ? 0x0170 : 0x01f0);
    ide_set_side(0, (dev->pci_conf_ide[0x40] & 0x80) ? 0x0376 : 0x03f6);
    ide_pri_enable();

    ide_sec_disable();
    ide_set_base(1, (dev->pci_conf_ide[0x40] & 0x80) ? 0x01f0 : 0x0170);
    ide_set_side(1, (dev->pci_conf_ide[0x40] & 0x80) ? 0x03f6 : 0x0376);
    if (dev->pci_conf_ide[0x41] & 0x01)
        ide_sec_enable();
}

static void
sis_85c50x_sb_write(int func, int addr, uint8_t val, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    sis_85c50x_log("85C503: [W] (%02X, %02X) = %02X\n", func, addr, val);

    if (func == 0x00)  switch (addr) {
        case 0x04: /* Command */
            dev->pci_conf_sb[addr] = val & 0x0f;
            break;
        case 0x07: /* Status */
            dev->pci_conf_sb[addr] &= ~(val & 0x30);
            break;
        case 0x40: /* BIOS Control Register */
            dev->pci_conf_sb[addr] = val & 0x3f;
            break;
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
            /* INTA/B/C/D# Remapping Control Register */
            dev->pci_conf_sb[addr] = val & 0x8f;
            if (val & 0x80)
                pci_set_irq_routing(PCI_INTA + (addr - 0x41), PCI_IRQ_DISABLED);
            else
                pci_set_irq_routing(PCI_INTA + (addr - 0x41), val & 0xf);
            break;
        case 0x48: /* ISA Master/DMA Memory Cycle Control Register 1 */
        case 0x49: /* ISA Master/DMA Memory Cycle Control Register 2 */
        case 0x4a: /* ISA Master/DMA Memory Cycle Control Register 3 */
        case 0x4b: /* ISA Master/DMA Memory Cycle Control Register 4 */
            dev->pci_conf_sb[addr] = val;
            break;

        default:
            break;
    } else if ((dev->type & 2) && !(dev->regs[0x81] & 0x02) && (func == 0x01))  switch (addr) {
        case 0x40:
        case 0x41:
            dev->pci_conf_ide[addr] = val;
            sis_85c50x_ide_recalc(dev);
            break;

        default:
            break;
    }
}

static uint8_t
sis_85c50x_sb_read(int func, int addr, void *priv)
{
    const sis_85c50x_t *dev = (sis_85c50x_t *) priv;
    uint8_t             ret = 0xff;

    if (func == 0x00)  switch (addr) {
        default:
            ret = dev->pci_conf_sb[addr];
            break;
        case 0x4c ... 0x4f:
            if (dev->type & 2)
                ret = pic_read_icw(0, addr & 0x03);
            else
                ret = dev->pci_conf_sb[addr];
            break;
        case 0x50 ... 0x53:
            if (dev->type & 2)
                ret = pic_read_icw(1, addr & 0x03);
            else
                ret = dev->pci_conf_sb[addr];
            break;
        case 0x54 ... 0x55:
            if (dev->type & 2)
                ret = pic_read_ocw(0, addr & 0x01);
            else
                ret = dev->pci_conf_sb[addr];
            break;
        case 0x56 ... 0x57:
            if (dev->type & 2)
                ret = pic_read_ocw(1, addr & 0x01);
            else
                ret = dev->pci_conf_sb[addr];
            break;
        case 0x58 ... 0x5f:
            if (dev->type & 2)
                ret = dev->pit_read_reg(dev->pit, addr & 0x07);
            else
                ret = dev->pci_conf_sb[addr];
            break;
    } else if ((dev->type & 2) && !(dev->regs[0x81] & 0x02) && (func == 0x01))
        ret = dev->pci_conf_ide[addr];

    sis_85c50x_log("85C503: [W] (%02X, %02X) = %02X\n", func, addr, ret);

    return ret;
}

static void
sis_85c50x_isa_write(uint16_t addr, uint8_t val, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    sis_85c50x_log("85C503 ISA: [W] (%04X) = %02X\n", addr, val);

    switch (addr) {
        case 0x22:
            dev->index = val;
            break;

        case 0x23:
            switch (dev->index) {
                case 0x80:
                    if (dev->type & 2) {
                        dev->regs[dev->index] = val;
                        nvr_bank_set(0, !!(val & 0x08), dev->nvr);
                    } else
                        dev->regs[dev->index] = val & 0xe7;
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
                    break;
                case 0x81:
                    if (dev->type & 2)
                        dev->regs[dev->index] = val & 0xf6;
                    else
                        dev->regs[dev->index] = val & 0xf4;
                    break;
                case 0x82:
                    if (dev->type & 2)
                        dev->regs[dev->index] = val;
                    break;
                case 0x83:
                    if (dev->type & 2)
                        dev->regs[dev->index] = val & 0x03;
                    break;
                case 0x84:
                case 0x88:
                case 0x89:
                case 0x8a:
                case 0x8b:
                    dev->regs[dev->index] = val;
                    break;
                case 0x85:
                    outb(0x70, val);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static uint8_t
sis_85c50x_isa_read(uint16_t addr, void *priv)
{
    const sis_85c50x_t *dev = (sis_85c50x_t *) priv;
    uint8_t             ret = 0xff;

    switch (addr) {
        case 0x22:
            ret = dev->index;
            break;

        case 0x23:
            if (dev->index == 0x85)
                ret = inb(0x70);
            else
                ret = dev->regs[dev->index];
            break;

        default:
            break;
    }

    sis_85c50x_log("85C503 ISA: [R] (%04X) = %02X\n", addr, ret);

    return ret;
}

static void
sis_85c50x_reset(void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    /* North Bridge (SiS 85C501/502) */
    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x06;
    dev->pci_conf[0x03] = 0x04;
    dev->pci_conf[0x04] = 0x04;
    dev->pci_conf[0x07] = 0x04;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;

    sis_85c50x_write(0, 0x51, 0x00, dev);
    sis_85c50x_write(0, 0x53, 0x00, dev);
    sis_85c50x_write(0, 0x54, 0x00, dev);
    sis_85c50x_write(0, 0x55, 0x00, dev);
    sis_85c50x_write(0, 0x56, 0x00, dev);
    sis_85c50x_write(0, 0x5b, 0x00, dev);
    sis_85c50x_write(0, 0x60, 0x00, dev);
    sis_85c50x_write(0, 0x64, 0x00, dev);
    sis_85c50x_write(0, 0x65, 0x00, dev);
    sis_85c50x_write(0, 0x68, 0x00, dev);
    sis_85c50x_write(0, 0x69, 0xff, dev);

    if (dev->type & 1) {
        for (uint8_t i = 0; i < 8; i++)
            dev->pci_conf[0x70 + i] = 0x00;
        dev->pci_conf[0x79] = 0x00;
    }

    /* South Bridge (SiS 85C503) */
    dev->pci_conf_sb[0x00] = 0x39;
    dev->pci_conf_sb[0x01] = 0x10;
    dev->pci_conf_sb[0x02] = 0x08;
    dev->pci_conf_sb[0x03] = 0x00;
    dev->pci_conf_sb[0x04] = 0x07;
    dev->pci_conf_sb[0x05] = 0x00;
    dev->pci_conf_sb[0x06] = 0x00;
    dev->pci_conf_sb[0x07] = 0x02;
    dev->pci_conf_sb[0x08] = 0x00;
    dev->pci_conf_sb[0x09] = 0x00;
    dev->pci_conf_sb[0x0a] = 0x01;
    dev->pci_conf_sb[0x0b] = 0x06;
    if (dev->type & 2)
        dev->pci_conf_sb[0x0e] = 0x80;
    sis_85c50x_sb_write(0, 0x41, 0x80, dev);
    sis_85c50x_sb_write(0, 0x42, 0x80, dev);
    sis_85c50x_sb_write(0, 0x43, 0x80, dev);
    sis_85c50x_sb_write(0, 0x44, 0x80, dev);

    if (dev->type & 2) {
        /* IDE (SiS 5503) */
        dev->pci_conf_ide[0x00] = 0x39;
        dev->pci_conf_ide[0x01] = 0x10;
        dev->pci_conf_ide[0x02] = 0x01;
        dev->pci_conf_ide[0x03] = 0x06;
        dev->pci_conf_ide[0x04] = 0x89;
        dev->pci_conf_ide[0x05] = 0x00;
        dev->pci_conf_ide[0x06] = 0x00;
        dev->pci_conf_ide[0x07] = 0x00;
        dev->pci_conf_ide[0x08] = 0x00;
        dev->pci_conf_ide[0x09] = 0x00;
        dev->pci_conf_ide[0x0a] = 0x01;
        dev->pci_conf_ide[0x0b] = 0x01;
        dev->pci_conf_ide[0x0c] = 0x00;
        dev->pci_conf_ide[0x0d] = 0x00;
        dev->pci_conf_ide[0x0e] = 0x80;
        dev->pci_conf_ide[0x0f] = 0x00;
        dev->pci_conf_ide[0x10] = 0x71;
        dev->pci_conf_ide[0x11] = 0x01;
        dev->pci_conf_ide[0x14] = 0xf1;
        dev->pci_conf_ide[0x15] = 0x01;
        dev->pci_conf_ide[0x18] = 0x71;
        dev->pci_conf_ide[0x19] = 0x03;
        dev->pci_conf_ide[0x1c] = 0xf1;
        dev->pci_conf_ide[0x1d] = 0x03;
        dev->pci_conf_ide[0x20] = 0x01;
        dev->pci_conf_ide[0x24] = 0x01;
        dev->pci_conf_ide[0x40] = 0x00;
        dev->pci_conf_ide[0x41] = 0x40;

        sis_85c50x_ide_recalc(dev);
    }

    cpu_set_isa_speed(7159091);

    if (dev->type & 2)
        nvr_bank_set(0, 0, dev->nvr);
}

static void
sis_85c50x_close(void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *) priv;

    smram_del(dev->smram[1]);
    smram_del(dev->smram[0]);
    free(dev);
}

static void *
sis_85c50x_init(UNUSED(const device_t *info))
{
    sis_85c50x_t *dev = (sis_85c50x_t *) calloc(1, sizeof(sis_85c50x_t));
    uint8_t pit_is_fast = (((pit_mode == -1) && is486) || (pit_mode == 1));

    dev->type = info->local;

    /* 501/502 (Northbridge) */
    pci_add_card(PCI_ADD_NORTHBRIDGE, sis_85c50x_read, sis_85c50x_write, dev, &dev->nb_slot);

    /* 503 (Southbridge) */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, sis_85c50x_sb_read, sis_85c50x_sb_write, dev, &dev->sb_slot);
    io_sethandler(0x0022, 0x0002, sis_85c50x_isa_read, NULL, NULL, sis_85c50x_isa_write, NULL, NULL, dev);

    dev->smram[0] = smram_add();
    dev->smram[1] = smram_add();

    dev->port_92 = device_add(&port_92_device);

    if (dev->type & 2) {
        /* PIT */
        dev->pit = device_find_first_priv(DEVICE_PIT);
        dev->pit_read_reg = pit_is_fast ? pitf_read_reg : pit_read_reg;

        /* NVR */
        dev->nvr = device_add(&at_mb_nvr_device);

        device_add(&ide_pci_2ch_device);
    }

    sis_85c50x_reset(dev);

    return dev;
}

const device_t sis_85c50x_device = {
    .name          = "SiS 85C50x",
    .internal_name = "sis_85c50x",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = sis_85c50x_init,
    .close         = sis_85c50x_close,
    .reset         = sis_85c50x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_550x_85c503_device = {
    .name          = "SiS 550x",
    .internal_name = "sis_550x",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = sis_85c50x_init,
    .close         = sis_85c50x_close,
    .reset         = sis_85c50x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_85c50x_5503_device = {
    .name          = "SiS 85C50x",
    .internal_name = "sis_85c50x",
    .flags         = DEVICE_PCI,
    .local         = 2,
    .init          = sis_85c50x_init,
    .close         = sis_85c50x_close,
    .reset         = sis_85c50x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_550x_device = {
    .name          = "SiS 550x",
    .internal_name = "sis_550x",
    .flags         = DEVICE_PCI,
    .local         = 3,
    .init          = sis_85c50x_init,
    .close         = sis_85c50x_close,
    .reset         = sis_85c50x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
