/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5511/5512/5513 Pentium PCI/ISA Chipset.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Tiseno100,
 *
 *          Copyright 2021-2023 Miran Grca.
 *          Copyright 2021-2023 Tiseno100.
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

#include <86box/chipset.h>

#ifdef ENABLE_SIS_5511_LOG
int sis_5511_do_log = ENABLE_SIS_5511_LOG;

static void
sis_5511_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5511_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5511_log(fmt, ...)
#endif

typedef struct sis_5511_t {
    uint8_t index;
    uint8_t nb_slot;
    uint8_t sb_slot;
    uint8_t pad;

    uint8_t regs[16];
    uint8_t states[7];

    uint8_t slic_regs[4096];   

    uint8_t pci_conf[256];
    uint8_t pci_conf_sb[2][256];

    mem_mapping_t slic_mapping;

    sff8038i_t *bm[2];
    smram_t    *smram;
    port_92_t  *port_92;
    void       *pit;
    nvr_t      *nvr;

   uint8_t (*pit_read_reg)(void *priv, uint8_t reg);
} sis_5511_t;

static void
sis_5511_shadow_recalc(sis_5511_t *dev)
{
    int      state;
    uint32_t base;

    for (uint8_t i = 0x80; i <= 0x86; i++) {
        if (i == 0x86) {
            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(0xf0000, 0x10000, state);
                sis_5511_log("000F0000-000FFFFF\n");
            }
        } else {
            base = ((i & 0x07) << 15) + 0xc0000;

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0xa0) {
                state = (dev->pci_conf[i] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base, 0x4000, state);
                sis_5511_log("%08X-%08X\n", base, base + 0x3fff);
            }

            if ((dev->states[i & 0x0f] ^ dev->pci_conf[i]) & 0x0a) {
                state = (dev->pci_conf[i] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                state |= (dev->pci_conf[i] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(base + 0x4000, 0x4000, state);
                sis_5511_log("%08X-%08X\n", base + 0x4000, base + 0x7fff);
            }
        }

        dev->states[i & 0x0f] = dev->pci_conf[i];
    }

    flushmmucache_nopc();
}

static void
sis_5511_smram_recalc(sis_5511_t *dev)
{
    smram_disable_all();

    switch (dev->pci_conf[0x65] >> 6) {
        case 0:
            smram_enable(dev->smram, 0x000e0000, 0x000e0000, 0x8000, dev->pci_conf[0x65] & 0x10, 1);
            break;
        case 1:
            smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x8000, dev->pci_conf[0x65] & 0x10, 1);
            break;
        case 2:
            smram_enable(dev->smram, 0x000e0000, 0x000b0000, 0x8000, dev->pci_conf[0x65] & 0x10, 1);
            break;

        default:
            break;
    }

    flushmmucache();
}

static void
sis_5511_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    sis_5511_t *dev = (sis_5511_t *) priv;

    sis_5511_log("SiS 5511: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    if (func == 0x00)  switch (addr) {
        case 0x07: /* Status - High Byte */
            dev->pci_conf[addr] &= 0xb0;
            break;

        case 0x50:
            dev->pci_conf[addr]   = val;
            cpu_cache_ext_enabled = !!(val & 0x40);
            cpu_update_waitstates();
            break;

        case 0x51:
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x52:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0x53:
        case 0x54:
            dev->pci_conf[addr] = val;
            break;

        case 0x55:
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x56 ... 0x59:
            dev->pci_conf[addr] = val;
            break;

        case 0x5a:
            /* TODO: Fast Gate A20 Emulation and Fast Reset Emulation on the KBC.
                     The former (bit 7) means the chipset intercepts D1h to 64h and 00h to 60h.
                     The latter (bit 6) means the chipset intercepts all odd FXh to 64h.
                     Bit 5 sets fast reset latency. This should be fixed on the other SiS
                     chipsets as well. */
            dev->pci_conf[addr] = val;
            break;

        case 0x5b:
            dev->pci_conf[addr] = val & 0xf7;
            break;

        case 0x5c:
            dev->pci_conf[addr] = val & 0xcf;
            break;

        case 0x5d:
            dev->pci_conf[addr] = val;
            break;

        case 0x5e:
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x5f:
            dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0x60:
            dev->pci_conf[addr] = val & 0x3e;
            if ((dev->pci_conf[0x68] & 1) && (val & 2)) {
                smi_raise();
                dev->pci_conf[0x69] |= 1;
            }
            break;

        case 0x61 ... 0x64:
            dev->pci_conf[addr] = val;
            break;

        case 0x65:
            dev->pci_conf[addr] = val & 0xd0;
            sis_5511_smram_recalc(dev);
            break;

        case 0x66:
            dev->pci_conf[addr] = val & 0x7f;
            break;

        case 0x67:
        case 0x68:
            dev->pci_conf[addr] = val;
            break;

        case 0x69:
            dev->pci_conf[addr] &= val;
            break;

        case 0x6a ... 0x6e:
            dev->pci_conf[addr] = val;
            break;

        case 0x6f:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0x70: /* DRAM Bank Register 0-0 */
        case 0x72: /* DRAM Bank Register 0-1 */
        case 0x74: /* DRAM Bank Register 1-0 */
        case 0x76: /* DRAM Bank Register 1-1 */
        case 0x78: /* DRAM Bank Register 2-0 */
        case 0x7a: /* DRAM Bank Register 2-1 */
        case 0x7c: /* DRAM Bank Register 3-0 */
        case 0x7e: /* DRAM Bank Register 3-1 */
            spd_write_drbs(dev->regs, 0x70, 0x7e, 0x82);
            break;
 
        case 0x71: /* DRAM Bank Register 0-0 */
            dev->pci_conf[addr] = val;
            break;

        case 0x75: /* DRAM Bank Register 1-0 */
        case 0x79: /* DRAM Bank Register 2-0 */
        case 0x7d: /* DRAM Bank Register 3-0 */
            dev->pci_conf[addr] = val & 0x7f;
            break;

        case 0x73: /* DRAM Bank Register 0-1 */
        case 0x77: /* DRAM Bank Register 1-1 */
        case 0x7b: /* DRAM Bank Register 2-1 */
        case 0x7f: /* DRAM Bank Register 3-1 */
            dev->pci_conf[addr] = val & 0x83;
            break;

        case 0x80 ... 0x85:
            dev->pci_conf[addr] = val & 0xee;
            sis_5511_shadow_recalc(dev);
            break;
        case 0x86:
            dev->pci_conf[addr] = val & 0xe8;
            sis_5511_shadow_recalc(dev);
            break;

        case 0x90 ... 0x93: /* 5512 General Purpose Register Index */
            dev->pci_conf[addr] = val;
            break;

        default:
            break;
    }
}

static void
sis_5511_slic_write(uint32_t addr, uint8_t val, void *priv)
{
    sis_5511_t *dev = (sis_5511_t *) priv;

    addr &= 0x00000fff;

    switch (addr) {
        case 0x00000000:
        case 0x00000008:    /* 0x00000008 is a SiS 5512 register. */
            dev->slic_regs[addr] = val;
            break;
        case 0x00000010:
        case 0x00000018:
        case 0x00000028:
        case 0x00000038:
            dev->slic_regs[addr] = val & 0x01;
            break;
        case 0x00000030:
            dev->slic_regs[addr] = val & 0x0f;
            mem_mapping_set_addr(&dev->slic_mapping,
                                 (((uint32_t) (val & 0x0f)) << 28) | 0x0fc00000, 0x00001000);
            break;
    }
}

static uint8_t
sis_5511_read(UNUSED(int func), int addr, void *priv)
{
    const sis_5511_t *dev = (sis_5511_t *) priv;
    uint8_t ret = 0xff;

    if (func == 0x00)
        ret = dev->pci_conf[addr];

    sis_5511_log("SiS 5511: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static uint8_t
sis_5511_slic_read(uint32_t addr, void *priv)
{
    sis_5511_t *dev = (sis_5511_t *) priv;
    uint8_t ret = 0xff;

    addr &= 0x00000fff;

    switch (addr) {
        case 0x00000008:    /* 0x00000008 is a SiS 5512 register. */
            ret = dev->slic_regs[addr];
            break;
    }

    return ret;
}

void
sis_5513_pci_to_isa_write(int addr, uint8_t val, sis_5511_t *dev)
{
    sis_5511_log("SiS 5513 P2I: [W] dev->pci_conf_sb[0][%02X] = %02X\n", addr, val);

    switch (addr) {
        case 0x04: /* Command */
            dev->pci_conf_sb[0][addr] = val & 0x0f;
            break;

        case 0x07: /* Status */
            dev->pci_conf_sb[0][addr] = (dev->pci_conf_sb[0][addr] & 0x06) & ~(val & 0x30);
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

        case 0x48: /* ISA Master/DMA Memory Cycle Control Register 1 */
        case 0x49: /* ISA Master/DMA Memory Cycle Control Register 2 */
        case 0x4a: /* ISA Master/DMA Memory Cycle Control Register 3 */
        case 0x4b: /* ISA Master/DMA Memory Cycle Control Register 4 */
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x60: /* MIRQ0 Remapping Control Register */
        case 0x61: /* MIRQ1 Remapping Control Register */
            sis_5511_log("Set MIRQ routing: MIRQ%i -> %02X\n", addr & 0x01, val);
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
            sis_5511_log("Set MIRQ routing: IDEIRQ -> %02X\n", val);
            dev->pci_conf_sb[0][addr] = val & 0x8f;
            if (val & 0x80)
                pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ2, val & 0xf);
            break;

        case 0x64: /* GPIO0 Control Register */
            dev->pci_conf_sb[0][addr] = val & 0xef;
            break;

        case 0x65:
            dev->pci_conf_sb[0][addr] = val & 0x80;
            break;

        case 0x66: /* GPIO0 Output Mode Control Register */
        case 0x67: /* GPIO0 Output Mode Control Register */
            dev->pci_conf_sb[0][addr] = val;
            break;

        case 0x6a: /* GPIO Status Register */
            dev->pci_conf_sb[0][addr] |= (val & 0x10);
            dev->pci_conf_sb[0][addr] &= ~(val & 0x01);
            break;

        default:
            break;
    }
}

static void
sis_5513_ide_irq_handler(sis_5511_t *dev)
{
    if (dev->pci_conf_sb[1][0x09] & 0x01) {
        /* Primary IDE is native. */
        sis_5511_log("Primary IDE IRQ mode: Native, Native\n");
        sff_set_irq_mode(dev->bm[0], IRQ_MODE_SIS_551X);
    } else {
        /* Primary IDE is legacy. */
        sis_5511_log("Primary IDE IRQ mode: IRQ14, IRQ15\n");
        sff_set_irq_mode(dev->bm[0], IRQ_MODE_LEGACY);
    }

    if (dev->pci_conf_sb[1][0x09] & 0x04) {
        /* Secondary IDE is native. */
        sis_5511_log("Secondary IDE IRQ mode: Native, Native\n");
        sff_set_irq_mode(dev->bm[1], IRQ_MODE_SIS_551X);
    } else {
        /* Secondary IDE is legacy. */
        sis_5511_log("Secondary IDE IRQ mode: IRQ14, IRQ15\n");
        sff_set_irq_mode(dev->bm[1], IRQ_MODE_LEGACY);
    }
}

static void
sis_5513_ide_handler(sis_5511_t *dev)
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

    sis_5511_log("sis_5513_ide_handler(): Disabling primary IDE...\n");
    ide_pri_disable();
    sis_5511_log("sis_5513_ide_handler(): Disabling secondary IDE...\n");
    ide_sec_disable();

    if (ide_io_on) {
        /* Primary Channel Setup */
        if (dev->pci_conf_sb[1][0x4a] & 0x02) {
            sis_5511_log("sis_5513_ide_handler(): Primary IDE base now %04X...\n", current_pri_base);
            ide_set_base(0, current_pri_base);
            sis_5511_log("sis_5513_ide_handler(): Primary IDE side now %04X...\n", current_pri_side);
            ide_set_side(0, current_pri_side);

            sis_5511_log("sis_5513_ide_handler(): Enabling primary IDE...\n");
            ide_pri_enable();

            sis_5511_log("SiS 5513 PRI: BASE %04x SIDE %04x\n", current_pri_base, current_pri_side);
        }

        /* Secondary Channel Setup */
        if (dev->pci_conf_sb[1][0x4a] & 0x04) {
            sis_5511_log("sis_5513_ide_handler(): Secondary IDE base now %04X...\n", current_sec_base);
            ide_set_base(1, current_sec_base);
            sis_5511_log("sis_5513_ide_handler(): Secondary IDE side now %04X...\n", current_sec_side);
            ide_set_side(1, current_sec_side);

            sis_5511_log("sis_5513_ide_handler(): Enabling secondary IDE...\n");
            ide_sec_enable();

            sis_5511_log("SiS 5513: BASE %04x SIDE %04x\n", current_sec_base, current_sec_side);
        }
    }

    sff_bus_master_handler(dev->bm[0], ide_io_on,
                           ((dev->pci_conf_sb[1][0x20] & 0xf0) | (dev->pci_conf_sb[1][0x21] << 8)) + 0);
    sff_bus_master_handler(dev->bm[1], ide_io_on,
                           ((dev->pci_conf_sb[1][0x20] & 0xf0) | (dev->pci_conf_sb[1][0x21] << 8)) + 8);
}

void
sis_5513_ide_write(int addr, uint8_t val, sis_5511_t *dev)
{
    sis_5511_log("SiS 5513 IDE: [W] dev->pci_conf_sb[1][%02X] = %02X\n", addr, val);

    switch (addr) {
        case 0x04: /* Command low byte */
            dev->pci_conf_sb[1][addr] = val & 0x05;
            sis_5513_ide_handler(dev);
            break;
        case 0x06: /* Status low byte */
            dev->pci_conf_sb[1][addr] = val & 0x20;
            break;
        case 0x07: /* Status high byte */
            dev->pci_conf_sb[1][addr] = (dev->pci_conf_sb[1][addr] & 0x06) & ~(val & 0x38);
            break;
        case 0x09: /* Programming Interface Byte */
            dev->pci_conf_sb[1][addr] = (dev->pci_conf_sb[1][addr] & 0x8a) | (val & 0x05);
            sis_5513_ide_irq_handler(dev);
            sis_5513_ide_handler(dev);
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
            sis_5513_ide_handler(dev);
            break;

        case 0x30: /* Expansion ROM Base Address */
        case 0x31: /* Expansion ROM Base Address */
        case 0x32: /* Expansion ROM Base Address */
        case 0x33: /* Expansion ROM Base Address */
            dev->pci_conf_sb[1][addr] = val;
            break;

        case 0x40: /* IDE Primary Channel/Master Drive Data Recovery Time Control */
        case 0x41: /* IDE Primary Channel/Master Drive DataActive Time Control */
        case 0x42: /* IDE Primary Channel/Slave Drive Data Recovery Time Control */
        case 0x43: /* IDE Primary Channel/Slave Drive Data Active Time Control */
        case 0x44: /* IDE Secondary Channel/Master Drive Data Recovery Time Control */
        case 0x45: /* IDE Secondary Channel/Master Drive Data Active Time Control */
        case 0x46: /* IDE Secondary Channel/Slave Drive Data Recovery Time Control */
        case 0x47: /* IDE Secondary Channel/Slave Drive Data Active Time Control */
        case 0x48: /* IDE Command Recovery Time Control */
        case 0x49: /* IDE Command Active Time Control */
            dev->pci_conf_sb[1][addr] = val;
            break;

        case 0x4a: /* IDE General Control Register 0 */
            dev->pci_conf_sb[1][addr] = val & 0x9e;
            sis_5513_ide_handler(dev);
            break;

        case 0x4b: /* IDE General Control Register 1 */
            dev->pci_conf_sb[1][addr] = val & 0xef;
            break;

        case 0x4c: /* Prefetch Count of Primary Channel (Low Byte) */
        case 0x4d: /* Prefetch Count of Primary Channel (High Byte) */
        case 0x4e: /* Prefetch Count of Secondary Channel (Low Byte) */
        case 0x4f: /* Prefetch Count of Secondary Channel (High Byte) */
            dev->pci_conf_sb[1][addr] = val;
            break;

        default:
            break;
    }
}

static void
sis_5513_write(int func, int addr, uint8_t val, void *priv)
{
    sis_5511_t *dev = (sis_5511_t *) priv;

    switch (func) {
        default:
            break;
        case 0:
            sis_5513_pci_to_isa_write(addr, val, dev);
            break;
        case 1:
            sis_5513_ide_write(addr, val, dev);
            break;
    }
}

static uint8_t
sis_5513_read(int func, int addr, void *priv)
{
    const sis_5511_t *dev = (sis_5511_t *) priv;
    uint8_t ret = 0xff;

    if (func == 0x00) {
        switch (addr) {
            default:
                ret = dev->pci_conf_sb[func][addr];
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
        }

        sis_5511_log("SiS 5513 P2I: [R] dev->pci_conf_sb[0][%02X] = %02X\n", addr, ret);
    } else if (func == 0x01) {
        ret = dev->pci_conf_sb[func][addr];

        sis_5511_log("SiS 5513 IDE: [R] dev->pci_conf_sb[1][%02X] = %02X\n", addr, ret);
    }

    return ret;
}

static void
sis_5513_isa_write(uint16_t addr, uint8_t val, void *priv)
{
    sis_5511_t *dev = (sis_5511_t *) priv;

    switch (addr) {
        case 0x22:
            dev->index = val - 0x50;
            break;
        case 0x23:
            sis_5511_log("SiS 5513 ISA: [W] dev->regs[%02X] = %02X\n", dev->index + 0x50, val);

            switch (dev->index) {
                case 0x00:
                    dev->regs[dev->index] = val & 0xed;
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
                case 0x01:
                    dev->regs[dev->index] = val & 0xf4;
                    break;
                case 0x03:
                    dev->regs[dev->index] = val & 3;
                    break;
                case 0x04: /* BIOS Register */
                    dev->regs[dev->index] = val;
                    break;
                case 0x05:
                    dev->regs[dev->index] = val;
                    outb(0x70, val);
                    break;
                case 0x08:
                case 0x09:
                case 0x0a:
                case 0x0b:
                    dev->regs[dev->index] = val;
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
sis_5513_isa_read(uint16_t addr, void *priv)
{
    const sis_5511_t *dev = (sis_5511_t *) priv;
    uint8_t ret = 0xff;

    if (addr == 0x23) {
        if (dev->index == 0x05)
            ret = inb(0x70);
        else
            ret = dev->regs[dev->index];

        sis_5511_log("SiS 5513 ISA: [R] dev->regs[%02X] = %02X\n", dev->index + 0x50, ret);
    }

    return ret;
}

static void
sis_5511_reset(void *priv)
{
    sis_5511_t *dev = (sis_5511_t *) priv;

    /* SiS 5511 */
    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x11;
    dev->pci_conf[0x03] = 0x55;
    dev->pci_conf[0x04] = 0x07;
    dev->pci_conf[0x05] = dev->pci_conf[0x06] = 0x00;
    dev->pci_conf[0x07]                       = 0x02;
    dev->pci_conf[0x08]                       = 0x00;
    dev->pci_conf[0x09] = dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b]                       = 0x06;
    dev->pci_conf[0x50] = dev->pci_conf[0x51] = 0x00;
    dev->pci_conf[0x52]                       = 0x20;
    dev->pci_conf[0x53] = dev->pci_conf[0x54] = 0x00;
    dev->pci_conf[0x55] = dev->pci_conf[0x56] = 0x00;
    dev->pci_conf[0x57] = dev->pci_conf[0x58] = 0x00;
    dev->pci_conf[0x59] = dev->pci_conf[0x5a] = 0x00;
    dev->pci_conf[0x5b] = dev->pci_conf[0x5c] = 0x00;
    dev->pci_conf[0x5d] = dev->pci_conf[0x5e] = 0x00;
    dev->pci_conf[0x5f] = dev->pci_conf[0x60] = 0x00;
    dev->pci_conf[0x61] = dev->pci_conf[0x62] = 0xff;
    dev->pci_conf[0x63]                       = 0xff;
    dev->pci_conf[0x64] = dev->pci_conf[0x65] = 0x00;
    dev->pci_conf[0x66]                       = 0x00;
    dev->pci_conf[0x67]                       = 0xff;
    dev->pci_conf[0x68] = dev->pci_conf[0x69] = 0x00;
    dev->pci_conf[0x6a]                       = 0x00;
    dev->pci_conf[0x6b] = dev->pci_conf[0x6c] = 0xff;
    dev->pci_conf[0x6d] = dev->pci_conf[0x6e] = 0xff;
    dev->pci_conf[0x6f]                       = 0x00;
    dev->pci_conf[0x70] = dev->pci_conf[0x72] = 0x04;
    dev->pci_conf[0x74] = dev->pci_conf[0x76] = 0x04;
    dev->pci_conf[0x78] = dev->pci_conf[0x7a] = 0x04;
    dev->pci_conf[0x7c] = dev->pci_conf[0x7e] = 0x04;
    dev->pci_conf[0x73] = dev->pci_conf[0x77] = 0x80;
    dev->pci_conf[0x7b] = dev->pci_conf[0x7f] = 0x80;
    dev->pci_conf[0x80] = dev->pci_conf[0x81] = 0x00;
    dev->pci_conf[0x82] = dev->pci_conf[0x83] = 0x00;
    dev->pci_conf[0x84] = dev->pci_conf[0x85] = 0x00;
    dev->pci_conf[0x86] = 0x00;

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    sis_5511_smram_recalc(dev);
    sis_5511_shadow_recalc(dev);

    flushmmucache();

    memset(dev->slic_regs, 0x00, 4096 * sizeof(uint8_t));
    dev->slic_regs[0x18] = 0x0f;

    mem_mapping_set_addr(&dev->slic_mapping, 0xffc00000, 0x00001000);

    /* SiS 5513 */
    dev->pci_conf_sb[0][0x00] = 0x39;
    dev->pci_conf_sb[0][0x01] = 0x10;
    dev->pci_conf_sb[0][0x02] = 0x08;
    dev->pci_conf_sb[0][0x03] = 0x00;
    dev->pci_conf_sb[0][0x04] = 0x07;
    dev->pci_conf_sb[0][0x05] = dev->pci_conf_sb[0][0x06] = 0x00;
    dev->pci_conf_sb[0][0x07]                             = 0x02;
    dev->pci_conf_sb[0][0x08] = dev->pci_conf_sb[0][0x09] = 0x00;
    dev->pci_conf_sb[0][0x0a] = 0x01;
    dev->pci_conf_sb[0][0x0b] = 0x06;
    dev->pci_conf_sb[0][0x0e] = 0x80;
    dev->pci_conf_sb[0][0x40] = 0x00;
    dev->pci_conf_sb[0][0x41] = dev->pci_conf_sb[0][0x42] = 0x80;
    dev->pci_conf_sb[0][0x43] = dev->pci_conf_sb[0][0x44] = 0x80;
    dev->pci_conf_sb[0][0x48] = dev->pci_conf_sb[0][0x49] = 0x80;
    dev->pci_conf_sb[0][0x4a] = dev->pci_conf_sb[0][0x4b] = 0x80;
    dev->pci_conf_sb[0][0x60] = dev->pci_conf_sb[0][0x51] = 0x80;
    dev->pci_conf_sb[0][0x62] = 0x00;
    dev->pci_conf_sb[0][0x63] = 0x80;
    dev->pci_conf_sb[0][0x64] = 0x00;
    dev->pci_conf_sb[0][0x65] = 0x80;
    dev->pci_conf_sb[0][0x66] = dev->pci_conf_sb[0][0x67] = 0x00;
    dev->pci_conf_sb[0][0x68] = dev->pci_conf_sb[0][0x69] = 0x00;
    dev->pci_conf_sb[0][0x6a] = 0x04;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);

    dev->regs[0x00] = dev->regs[0x01] = 0x00;
    dev->regs[0x03] = dev->regs[0x04] = 0x00;
    dev->regs[0x05] = 0x00;
    dev->regs[0x08] = dev->regs[0x09] = 0x00;
    dev->regs[0x0a] = dev->regs[0x0b] = 0x00;

    cpu_set_isa_speed(7159091);
    nvr_bank_set(0, 0, dev->nvr);

    /* SiS 5513 IDE Controller */
    dev->pci_conf_sb[1][0x00] = 0x39;
    dev->pci_conf_sb[1][0x01] = 0x10;
    dev->pci_conf_sb[1][0x02] = 0x13;
    dev->pci_conf_sb[1][0x03] = 0x55;
    dev->pci_conf_sb[1][0x04] = dev->pci_conf_sb[1][0x05] = 0x00;
    dev->pci_conf_sb[1][0x06] = dev->pci_conf_sb[1][0x07] = 0x00;
    dev->pci_conf_sb[1][0x08] = 0x00;
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

    sis_5513_ide_irq_handler(dev);
    sis_5513_ide_handler(dev);

    sff_bus_master_reset(dev->bm[0]);
    sff_bus_master_reset(dev->bm[1]);
}

static void
sis_5511_close(void *priv)
{
    sis_5511_t *dev = (sis_5511_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
sis_5511_init(UNUSED(const device_t *info))
{
    sis_5511_t *dev = (sis_5511_t *) calloc(1, sizeof(sis_5511_t));
    uint8_t pit_is_fast = (((pit_mode == -1) && is486) || (pit_mode == 1));

    memset(dev, 0, sizeof(sis_5511_t));

    /* Device 0: SiS 5511 */
    pci_add_card(PCI_ADD_NORTHBRIDGE, sis_5511_read, sis_5511_write, dev, &dev->nb_slot);
    /* Device 1: SiS 5513 */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, sis_5513_read, sis_5513_write, dev, &dev->sb_slot);

    /* SLiC Memory Mapped Registers */
    mem_mapping_add(&dev->slic_mapping,
                    0xffc00000, 0x00001000,
                    sis_5511_slic_read,
                    NULL,
                    NULL,
                    sis_5511_slic_write,
                    NULL,
                    NULL,
                    NULL, MEM_MAPPING_EXTERNAL,
                    dev);

    /* Ports 22h-23h: SiS 5513 ISA */
    io_sethandler(0x0022, 0x0002, sis_5513_isa_read, NULL, NULL, sis_5513_isa_write, NULL, NULL, dev);

    /* MIRQ */
    pci_enable_mirq(0);
    pci_enable_mirq(1);

    /* IDEIRQ */
    pci_enable_mirq(2);

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

    sis_5511_reset(dev);

    return dev;
}

const device_t sis_5511_device = {
    .name          = "SiS 5511",
    .internal_name = "sis_5511",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = sis_5511_init,
    .close         = sis_5511_close,
    .reset         = sis_5511_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
