/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the OPTi Viper 82c55x chipset
 *
 * Authors: win2kgamer
 *
 *          Copyright 2026 win2kgamer
 */

#define ENABLE_OPTI55X_LOG 1

#ifdef ENABLE_OPTI55X_LOG
#include <stdarg.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef ENABLE_OPTI55X_LOG
#define HAVE_STDARG_H
#endif
#include <86box/86box.h>
#include "cpu.h"
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
#include <86box/keyboard.h>
#include <86box/chipset.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/dma.h>
#include <86box/log.h>

#ifdef ENABLE_OPTI55X_LOG
int opti55x_do_log = ENABLE_OPTI55X_LOG;

static void
opti55x_log(void *priv, const char *fmt, ...)
{
    if (opti55x_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define opti55x_log(fmt, ...)
#endif

typedef struct opti55x_t {
    uint8_t     nb_slot;
    uint8_t     sb_slot;
    uint8_t     ide_slot;

    uint8_t     has_ide;
    uint8_t     pci_conf[256];
    uint8_t     pci_conf_sb[256];
    uint8_t     pci_conf_ide[256];

    uint8_t     idx;
    uint8_t     regs[256];
    uint8_t     conf206;
    uint8_t     enable_ide;
    uint8_t     ide_in_cfg;
    uint8_t     ide_in_cfg_sec;
    uint8_t     ide_regs[0x18];
    uint8_t     read_count;
    uint8_t     ide_reg_en;
    uint8_t     ide_reg_en_sec;

    smram_t    *smram;
    port_92_t  *port_92;
    nvr_t      *nvr;

    sff8038i_t *bm[2];

    void *      log; /* New logging system */
} opti55x_t;

static void opti55x_ide_write(uint16_t addr, uint8_t val, void *priv);
static uint8_t opti55x_ide_read(uint16_t addr, void *priv);
static uint16_t opti55x_ide_readw(uint16_t addr, void *priv);

static void
opti55x_shadow_recalc(void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    mem_set_mem_state_cpu_both(0xc0000, 0x8000, ((dev->regs[0x04] & 0x01) ? MEM_READ_INTERNAL: MEM_READ_EXTANY) | ((dev->regs[0x04] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));

    if (dev->regs[0x1a] & 0x10) {
        mem_set_mem_state_cpu_both(0xc8000, 0x2000, ((dev->regs[0x04] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x04] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_cpu_both(0xca000, 0x2000, ((dev->regs[0x1a] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_cpu_both(0xcc000, 0x2000, ((dev->regs[0x04] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x04] & 0x80) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_cpu_both(0xce000, 0x2000, ((dev->regs[0x1a] & 0x04) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & 0x08) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        for (uint8_t i = 0; i < 8; i+= 2)
            mem_set_mem_state_cpu_both(0xd0000 + (i << 13), 0x2000, ((dev->regs[0x05] & (1 << i)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x05] & (2 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        for (uint8_t i = 1; i < 8; i+= 2)
            mem_set_mem_state_cpu_both(0xd0000 + (i << 13), 0x2000, ((dev->regs[0x1b] & (1 << (i - 1))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1b] & (2 << (i - 1))) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    } else {
        mem_set_mem_state_cpu_both(0xc8000, 0x4000, ((dev->regs[0x04] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x04] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_cpu_both(0xcc000, 0x4000, ((dev->regs[0x04] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x04] & 0x80) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        for (uint8_t i = 0; i < 4; i++)
            mem_set_mem_state_cpu_both(0xd0000 + (i << 14), 0x4000, ((dev->regs[0x05] & (1 << (2 * i))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x05] & (2 << (2 * i))) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }

    mem_set_mem_state_cpu_both(0xe0000, 0x10000, ((dev->regs[0x06] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x06] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    mem_set_mem_state_cpu_both(0xf0000, 0x10000, ((dev->regs[0x06] & 0x04) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x06] & 0x08) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));

    flushmmucache_nopc();
}

static void
opti55x_smram_recalc(void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    smram_disable_all();
    opti55x_log(dev->log, "OPTi 55x SMRAM recalc: reg0D = %02X, reg14 = %02X, reg13 = %02X, reg1d = %02X\n", dev->regs[0x0d], dev->regs[0x14], dev->regs[0x13], dev->regs[0x1d]);
    if (!(dev->regs[0x1d] & 0x03)) {
        opti55x_log(dev->log, "OPTi 55x SMRAM recalc: normal mode\n");
        smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x20000, (dev->regs[0x0d] & 0x08), (dev->regs[0x13] & 0x08));
    } else {
        smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x10000, (dev->regs[0x0d] & 0x08), (dev->regs[0x13] & 0x08) && !(dev->regs[0x1d] & 0x01));
        smram_enable(dev->smram, 0x000b0000, 0x000b0000, 0x10000, (dev->regs[0x0d] & 0x08), (dev->regs[0x13] & 0x08) && !(dev->regs[0x1d] & 0x02));
    }
}

static void
opti55x_pci_irq_recalc(void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    const uint8_t irq_array[8] = { 0, 5, 9, 10, 11, 12, 14, 15 };
    const uint8_t irq_array_2[4] = { 0, 3, 4, 7 };

    uint16_t irqs = ((dev->pci_conf_sb[0x41] << 8) | dev->pci_conf_sb[0x40]);
    uint8_t irqs2 = dev->pci_conf_sb[0x50];
    uint8_t irq;

    irq = irq_array[irqs & 0x07];
    if (irq == 0)
        irq = irq_array_2[irqs2 & 0x03];
    pci_set_irq_routing(PCI_INTA, (irq != 0) ? irq : PCI_IRQ_DISABLED);

    irq = irq_array[(irqs >> 3) & 0x07];
    if (irq == 0)
        irq = irq_array_2[(irqs2 >> 2) & 0x03];
    pci_set_irq_routing(PCI_INTB, (irq != 0) ? irq : PCI_IRQ_DISABLED);

    irq = irq_array[(irqs >> 6) & 0x07];
    if (irq == 0)
        irq = irq_array_2[(irqs2 >> 4) & 0x03];
    pci_set_irq_routing(PCI_INTC, (irq != 0) ? irq : PCI_IRQ_DISABLED);

    irq = irq_array[(irqs >> 9) & 0x07];
    if (irq == 0)
        irq = irq_array_2[(irqs2 >> 6) & 0x03];
    pci_set_irq_routing(PCI_INTD, (irq != 0) ? irq : PCI_IRQ_DISABLED);

}

static void
opti55x_ide_irq_handler(void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    if (!dev->has_ide)
        return;

    opti55x_log(dev->log, "OPTi 558 IDE: IRQ modeset, reg9 = %02X\n", dev->pci_conf_ide[0x09]);

    if ((dev->pci_conf_ide[0x09] & 0x03) == 0x03 )
        sff_set_irq_mode(dev->bm[0], IRQ_MODE_PCI_IRQ_PIN);
    else
        sff_set_irq_mode(dev->bm[0], IRQ_MODE_LEGACY);

    if ((dev->pci_conf_ide[0x09] & 0x0c) == 0x0c )
        sff_set_irq_mode(dev->bm[1], IRQ_MODE_PCI_IRQ_PIN);
    else
        sff_set_irq_mode(dev->bm[1], IRQ_MODE_LEGACY);
}

static void
opti55x_ide_handler(void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    if (!dev->has_ide)
        return;

    uint16_t native_base_pri_addr = (dev->pci_conf_ide[0x10] | dev->pci_conf_ide[0x11] << 8) & 0xfffe;
    uint16_t native_side_pri_addr = (dev->pci_conf_ide[0x14] | dev->pci_conf_ide[0x15] << 8) & 0xfffe;
    uint16_t native_base_sec_addr = (dev->pci_conf_ide[0x18] | dev->pci_conf_ide[0x19] << 8) & 0xfffe;
    uint16_t native_side_sec_addr = (dev->pci_conf_ide[0x1c] | dev->pci_conf_ide[0x1d] << 8) & 0xfffe;

    uint16_t current_pri_base;
    uint16_t current_pri_side;
    uint16_t current_sec_base;
    uint16_t current_sec_side;

    /* Primary Channel Programming */
    current_pri_base = (!(dev->pci_conf_ide[0x09] & 1)) ? 0x01f0 : native_base_pri_addr;
    current_pri_side = (!(dev->pci_conf_ide[0x09] & 1)) ? 0x03f6 : native_side_pri_addr;

    /* Secondary Channel Programming */
    current_sec_base = (!(dev->pci_conf_ide[0x09] & 1)) ? 0x0170 : native_base_sec_addr;
    current_sec_side = (!(dev->pci_conf_ide[0x09] & 1)) ? 0x0376 : native_side_sec_addr;

    opti55x_log(dev->log, "OPTi 558 IDE handler: %s mode\n", !(dev->pci_conf_ide[0x09] & 0x01) ? "Legacy" : "Native");
    opti55x_log(dev->log, "OPTi 558 IDE handler: primary addr = %04X/%04X, secondary = %04X/%04X\n", current_pri_base, current_pri_side, current_sec_base, current_sec_side);

    ide_pri_disable();
    ide_sec_disable();

    opti55x_log(dev->log, "In OPTi 558 IDE handler: IDE is %sabled, ide_in_cfg = %i, ide_in_cfg_sec = %i\n", dev->enable_ide ? "En" : "Dis", dev->ide_in_cfg, dev->ide_in_cfg_sec);

    if (dev->enable_ide) {
        if (dev->pci_conf_ide[0x04] & 0x01) {
            ide_set_base(0, current_pri_base);
            ide_set_side(0, current_pri_side);
            if (!dev->ide_in_cfg)
                ide_pri_enable();
        }

        if ((dev->pci_conf_ide[0x04] & 0x01) && !(dev->pci_conf_ide[0x40] & 0x08)) {
            ide_set_base(1, current_sec_base);
            ide_set_side(1, current_sec_side);
            if (!dev->ide_in_cfg_sec)
                ide_sec_enable();
        }
    }

    sff_bus_master_handler(dev->bm[0], dev->enable_ide, ((dev->pci_conf_ide[0x20] & 0xf0) | (dev->pci_conf_ide[0x21] << 8)) + 0);
    sff_bus_master_handler(dev->bm[1], dev->enable_ide, ((dev->pci_conf_ide[0x20] & 0xf0) | (dev->pci_conf_ide[0x21] << 8)) + 8);
}

static void
opti55x_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    //opti55x_log(dev->log, "[%04X:%08X] OPTi 55x: [W] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, val);

    if (func == 0x00) switch (addr) {
        case 0x04: case 0x05: /* PCI Command Register */
            dev->pci_conf[addr] = val;
            break;
        case 0x40 ... 0x43: /* 82C557M Memory Control Register */
            dev->pci_conf[addr] = val;
            break;
        case 0x44 ... 0x47: /* 82C557M DRAM Data Latching Control Register */
            dev->pci_conf[addr] = val;
            break;
        default:
            break;
    }
}

static uint8_t
opti55x_read(int func, int addr, UNUSED(int len), void *priv)
{
    const opti55x_t *dev = (opti55x_t *) priv;
    uint8_t          ret = 0xff;

    if (func == 0x00) switch (addr) {
        default:
            ret = dev->pci_conf[addr];
            break;
    }

    //opti55x_log(dev->log, "[%04X:%08X] OPTi 55x: [R] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, ret);

    return ret;
}

static void
opti55x_sb_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    opti55x_log(dev->log, "[%04X:%08X] OPTi 558: [W] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, val);

    if (func == 0x00) switch (addr) {
        case 0x04: case 0x05: /* PCI Command Register */
            dev->pci_conf_sb[addr] = val;
            break;
        case 0x40 ... 0x41: /* 82C558M Keyboard Control Register */
            if (addr == 0x41)
                dev->pci_conf_sb[addr] = ((dev->pci_conf_sb[addr] & 0xc0) | (val & 0x3f));
            else
                dev->pci_conf_sb[addr] = val;
            opti55x_pci_irq_recalc(dev);
            break;
        case 0x42 ... 0x43: /* 82C558M Misc Control Register 1 */
            dev->pci_conf_sb[addr] = val;
            break;
        case 0x44 ... 0x45: /* 82C558M Pin Functionality Register 1 */
            dev->pci_conf_sb[addr] = val;
            break;
        case 0x46 ... 0x47: /* 82C558M Cycle Control Register 1 */
            dev->pci_conf_sb[addr] = val;
            if (addr == 0x47) {
                uint8_t clkdiv = ((val >> 4) & 0x03);
                switch (clkdiv) {
                    case 0x00:
                        cpu_set_isa_pci_div(4);
                        opti55x_log(dev->log, "ISACLK divider now /4\n");
                        break;
                    case 0x01:
                        cpu_set_isa_pci_div(3);
                        opti55x_log(dev->log, "ISACLK divider now /3\n");
                        break;
                    case 0x02:
                        cpu_set_isa_pci_div(2);
                        opti55x_log(dev->log, "ISACLK divider now /2\n");
                        break;
                    case 0x03:
                        cpu_set_isa_pci_div(1);
                        opti55x_log(dev->log, "ISACLK divider now /1\n");
                        break;
                }
            }
            break;
        case 0x48 ... 0x49: /* 82C558M Pin Functionality Register 2 */
            dev->pci_conf_sb[addr] = val;
            break;
        case 0x4a ... 0x4b: /* 82C558M ROMCS# Range Control Register */
            dev->pci_conf_sb[addr] = val;
            /* Work around soft reset hang on Octek Rhino 8 BIOS */
            if ((addr == 0x4a) && (val == 0x30))
                flushmmucache();
            break;
        case 0x4c ... 0x4d: /* 82C558M Reserved Register 3 */
            dev->pci_conf_sb[addr] = val;
            break;
        case 0x4e ... 0x4f: /* 82C558M Misc Control Register 2 */
            if (addr == 0x4f) {
                opti55x_log(dev->log, "OPTi 558 MCR2 write! val = %02X\n", val);
                dev->pci_conf_sb[addr] = ((dev->pci_conf_sb[addr] & 0x20) | (val & 0xdf));
                dev->enable_ide = (dev->pci_conf_sb[addr] & 0x40);
                opti55x_log(dev->log, "OPTi 558 IDE now %sabled\n", dev->enable_ide ? "En" : "Dis");
            } else
                dev->pci_conf_sb[addr] = val;
            opti55x_ide_handler(dev);
            opti55x_ide_irq_handler(dev);
            break;
        case 0x50 ... 0x51: /* 82C558M Trigger Control Register */
            dev->pci_conf_sb[addr] = val;
            break;
        case 0x52 ... 0x53: /* 82C558M Interrupt Multiplexing Control Register */
            dev->pci_conf_sb[addr] = val;
            break;
        case 0x54 ... 0x55: /* 82C558M DMA Control Register */
            dev->pci_conf_sb[addr] = val;
            break;
        default:
            break;
    }

    #ifdef C558_MODE
    if (func == 0x01) switch (addr) {
        case 0x04: case 0x05: /* PCI Command Register */
            dev->pci_conf_ide[addr] = val;
            break;
        case 0x09: /* Programming Inteface */
            dev->pci_conf_ide[addr] = val;
            opti55x_ide_handler(dev);
            opti55x_ide_irq_handler(dev);
            break;
        case 0x10 ... 0x13: /* Primary Command Block BAR */
        case 0x14 ... 0x17: /* Primary Control Block BAR */
        case 0x18 ... 0x1b: /* Secondary Command Block BAR */
        case 0x1c ... 0x1f: /* Secondary Control Block BAR */
            dev->pci_conf_ide[addr] = val;
            opti55x_ide_handler(dev);
            break;
        case 0x20 ... 0x23: /* Bus Master IDE BAR */
            dev->pci_conf_ide[addr] = val;
            opti55x_ide_handler(dev);
            break;
        case 0x3c: /* Interrupt Line Register */
            dev->pci_conf_ide[addr] = val;
            break;
        case 0x40: /* IDE Initialization Control Register */
            dev->pci_conf_ide[addr] = val;
            opti55x_ide_handler(dev);
            break;
        case 0x43: /* IDE Enhanced Mode Register */
            dev->pci_conf_ide[addr] = val;
            break;
        default:
            break;
    }
    #endif
}

static uint8_t
opti55x_sb_read(int func, int addr, UNUSED(int len), void *priv)
{
    const opti55x_t *dev = (opti55x_t *) priv;
    uint8_t          ret = 0xff;

    if (func == 0x00) switch (addr) {
        default:
            ret = dev->pci_conf_sb[addr];
            break;
    }

    #ifdef C558_MODE
    if (func == 0x01) switch (addr) {
        default:
            ret = dev->pci_conf_ide[addr];
            break;
    }
    #endif

    opti55x_log(dev->log, "[%04X:%08X] OPTi 558: [R] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, ret);

    return ret;
}

static void
opti55x_pciide_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    opti55x_log(dev->log, "[%04X:%08X] OPTi 558 IDE: [W] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, val);

    if (func == 0x00) switch (addr) {
        case 0x04: case 0x05: /* PCI Command Register */
            dev->pci_conf_ide[addr] = val;
            opti55x_ide_handler(dev);
            break;
        case 0x06: case 0x07: /* PCI Status Register */
            dev->pci_conf_ide[addr] = val;
            break;
        case 0x09: /* Programming Inteface */
            dev->pci_conf_ide[addr] = val;
            opti55x_ide_handler(dev);
            opti55x_ide_irq_handler(dev);
            break;
        case 0x10 ... 0x13: /* Primary Command Block BAR */
        case 0x14 ... 0x17: /* Primary Control Block BAR */
        case 0x18 ... 0x1b: /* Secondary Command Block BAR */
        case 0x1c ... 0x1f: /* Secondary Control Block BAR */
            dev->pci_conf_ide[addr] = val;
            dev->pci_conf_ide[0x10] &= 0xf9;
            dev->pci_conf_ide[0x14] &= 0xfd;
            dev->pci_conf_ide[0x18] &= 0xf9;
            dev->pci_conf_ide[0x1c] &= 0xfd;
            opti55x_ide_handler(dev);
            break;
        case 0x20 ... 0x23: /* Bus Master IDE BAR */
            dev->pci_conf_ide[addr] = val;
            dev->pci_conf_ide[0x20] &= 0xf1;
            opti55x_ide_handler(dev);
            break;
        case 0x3c: /* Interrupt Line Register */
            dev->pci_conf_ide[addr] = val;
            break;
        case 0x40: /* IDE Initialization Control Register */
            dev->pci_conf_ide[addr] = val;
            opti55x_ide_handler(dev);
            break;
        case 0x43: /* IDE Enhanced Mode Register */
            dev->pci_conf_ide[addr] = val;
            break;
        default:
            break;
    }

}

static uint8_t
opti55x_pciide_read(int func, int addr, UNUSED(int len), void *priv)
{
    const opti55x_t *dev = (opti55x_t *) priv;
    uint8_t          ret = 0x00;

    if (func == 0x00) switch (addr) {
        default:
            ret = dev->pci_conf_ide[addr];
            break;
    }

    opti55x_log(dev->log, "[%04X:%08X] OPTi 558 IDE: [R] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, ret);

    return ret;
}

static void
opti55x_isa_write(uint16_t addr, uint8_t val, void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    switch (addr) {
        case 0x22:
            dev->idx = val;
            break;
        case 0x23:
            switch (dev->idx) {
                case 0x01: /* Integrated 82C206 Configuration Register */
                    dev->conf206 = val;
                    break;
                default:
                    break;
            }
            break;
        case 0x24:
            switch (dev->idx) {
                case 0x00:
                    if (dev->regs[0x13] & 0x80) /* 82C557M Byte Merge/Prefetch and Sony Cache Module Control */
                        dev->regs[0x00] = val;
                    else { /* 82C54x-compatible DRAM Config Register 1 */
                        /* TODO: Implement DRAM rows */
                        dev->regs[0x00] = val;
                    }
                    break;
                case 0x01: /* 82C557M DRAM Control Register 1 */
                    dev->regs[0x01] = val;
                    break;
                case 0x02: /* 82C557M Cache Control Register 1 */
                    dev->regs[0x02] = val;
                    cpu_cache_ext_enabled = ((dev->regs[0x02] & 0x30) == 0x30);
                    cpu_update_waitstates();
                    break;
                case 0x03: /* 82C557M Cache Control Register 2 */
                    dev->regs[0x03] = val;
                    break;
                case 0x04 ... 0x06: /* 82C557M Shadow RAM Control Register 1 */
                    dev->regs[dev->idx] = val;
                    opti55x_shadow_recalc(dev);
                    break;
                case 0x07: /* 82C557M Tag Test Register */
                    dev->regs[0x07] = val;
                    break;
                case 0x08: /* 82C557M CPU Cache Control Register */
                    dev->regs[0x08] = val;
                    break;
                case 0x09: /* 82C557M System Memory Function Register */
                    dev->regs[0x09] = val;
                    break;
                case 0x0a: /* 82C557M DRAM Hole A Address Decode Register 1 */
                    dev->regs[0x0a] = val;
                    break;
                case 0x0b: /* 82C557M DRAM Hole B Address Decode Register 2 */
                    dev->regs[0x0b] = val;
                    break;
                case 0x0c: /* 82C557M Extended DMA Register */
                    dev->regs[0x0c] = val;
                    break;
                case 0x0d: /* 82C557M Clock Control Register */
                    dev->regs[0x0d] = val;
                    opti55x_smram_recalc(dev);
                    break;
                case 0x0e: /* 82C557M Cycle Control Register 1 */
                    dev->regs[0x0e] = val;
                    break;
                case 0x0f: /* 82C557M Cycle Control Register 2 */
                    dev->regs[0x0f] = val;
                    break;
                case 0x10: /* 82C557M Misc Control Register 1 */
                    dev->regs[0x10] = val;
                    break;
                case 0x11: /* 82C557M Misc Control Register 2 */
                    dev->regs[0x11] = val;
                    break;
                case 0x12: /* 82C557M Refresh Control Register */
                    dev->regs[0x12] = val;
                    break;
                case 0x13: /* 82C557M Memory Decode Control Register 1 */
                    dev->regs[0x13] = val;
                    opti55x_smram_recalc(dev);
                    break;
                case 0x14: /* 82C557M Memory Decode Control Register 2 */
                    dev->regs[0x14] = val;
                    opti55x_smram_recalc(dev);
                    break;
                case 0x15: /* 82C557M PCI Cycle Control Register 1 */
                    dev->regs[0x15] = val;
                    break;
                case 0x16: /* 82C557M Dirty/Tag RAM Control Register */
                    dev->regs[0x16] = val;
                    break;
                case 0x17: /* 82C557M PCI Cycle Control Register 2 */
                    dev->regs[0x17] = val;
                    break;
                case 0x18: /* 82C557M Tristate Control Register */
                    dev->regs[0x18] = val;
                    break;
                case 0x19: /* 82C557M Memory Decode Control Register 3 */
                    dev->regs[0x19] = val;
                    break;
                case 0x1a: /* 82C557M Memory Shadow Control Register 1 */
                    dev->regs[0x1a] = val;
                    opti55x_shadow_recalc(dev);
                    break;
                case 0x1b: /* 82C557M Memory Shadow Control Register 2 */
                    dev->regs[0x1b] = val;
                    opti55x_shadow_recalc(dev);
                    break;
                case 0x1c: /* 82C557M EDO DRAM Control Register */
                    dev->regs[0x1c] = val;
                    break;
                case 0x1d: /* 82C557M Reserved Register 3 */
                    dev->regs[0x1d] = val;
                    opti55x_smram_recalc(dev);
                    break;
                case 0x1e: /* 82C557M BOFF# Control Register Register */
                    dev->regs[0x1e] = val;
                    break;
                case 0x1f: /* 82C557M Reserved Register */
                    dev->regs[0x1f] = val;
                    break;
                case 0xe0: /* GREEN Mode Control/Enable Status */
                    dev->regs[0xe0] = ((dev->regs[0xe0] & 0x10) | (val & 0xef));
                    if ((val & 0x01) && (dev->regs[0xe1] & 0x01)) {
                        dev->regs[0xe0] |= 0x10;
                        smi_raise();
                    }
                    break;
                case 0xe1: /* EPMI Control/GREEN Event Timer */
                    dev->regs[0xe1] = val;
                    break;
                case 0xe2: /* GREEN Event Timer Initial Count Register */
                    dev->regs[0xe2] = val;
                    break;
                case 0xe3: /* IRQ Event Enable Register 1 */
                    dev->regs[0xe3] = val;
                    break;
                case 0xe4: /* IRQ Event Enable Register 2 */
                    dev->regs[0xe4] = val;
                    break;
                case 0xe5: /* DRQ Event Enable Register */
                    dev->regs[0xe5] = val;
                    break;
                case 0xe6: /* Device Cycle Monitor Enable Register */
                    dev->regs[0xe6] = val;
                    break;
                case 0xe7: /* Wake-up Source/Programmable I/O/Memory Address Mask Register */
                    dev->regs[0xe7] = val;
                    break;
                case 0xe8: /* Programmable IO/MEM Address Range Register 1 */
                    dev->regs[0xe8] = val;
                    break;
                case 0xe9: /* Programmable IO/MEM Address Range Register 2 */
                    dev->regs[0xe9] = val;
                    break;
                case 0xea: /* Enter GREEN State Port Register */
                    dev->regs[0xea] = val;
                    break;
                case 0xeb: /* Return to NORMAL State Configuration Port Register */
                    dev->regs[0xeb] = val;
                    break;
                case 0xec: /* Shadow Register for External Power Control Latch Register */
                    dev->regs[0xec] = val;
                    break;
                case 0xed: /* Device Cycle Enable/Status Register */
                    dev->regs[0xed] = val;
                    break;
                case 0xee: /* STPCLK# Modulation Register */
                    dev->regs[0xee] = val;
                    break;
                case 0xef: /* Miscellaneous Register */
                    dev->regs[0xef] = val;
                    break;
                case 0xf0: /* Device Timer CLK Select/Enable Status Register */
                    dev->regs[0xf0] = val;
                    break;
                case 0xf1: /* Device 0 Timer Initial Count Register */
                    dev->regs[0xf1] = val;
                    break;
                case 0xf2: /* Device 1 Timer Initial Count Register */
                    dev->regs[0xf2] = val;
                    break;
                case 0xf3: /* Device Timer IO/MEM Select, Mask Bits Register */
                    dev->regs[0xf3] = val;
                    break;
                case 0xf4: /* Device 0 IO/MEM Address Register */
                    dev->regs[0xf4] = val;
                    break;
                case 0xf5: /* Device 0 IO/MEM Address Register */
                    dev->regs[0xf5] = val;
                    break;
                case 0xf6: /* Device 1 IO/MEM Address Register */
                    dev->regs[0xf6] = val;
                    break;
                case 0xf7: /* Device 1 IO/MEM Address Register */
                    dev->regs[0xf7] = val;
                    break;
                case 0xfa ... 0xfb: /* Reserved Register 1 */
                    dev->regs[dev->idx] = val;
                    break;
                case 0xfc: /* Power Management Control Register 1 */
                    dev->regs[0xfc] = val;
                    break;
                case 0xfd: /* Power Management Control Register 2 */
                    dev->regs[0xfd] = val;
                    break;
                case 0xfe: /* Power Management Control Register 3 */
                    dev->regs[0xfe] = val;
                    break;
                case 0xff: /* General Purpose Chip Select Register */
                    dev->regs[0xff] = val;
                    break;
                default:
                    break;
            }
            break;
    }

    opti55x_log(dev->log, "[%04X:%08X] OPTi 55x ISA: [W] %04X = %02X\n", CS, cpu_state.pc, addr, val);
}

static uint8_t
opti55x_isa_read(uint16_t addr, void *priv)
{
    const opti55x_t *dev = (opti55x_t *) priv;
    uint8_t          ret = 0xff;

    if (addr == 0x22)
        ret = dev->idx;

    if (addr == 0x24) {
        if (dev->idx == 0xe0)
            ret = (dev->regs[0xe0] & 0xef) | (in_smm ? 0x10 : 0);
        else
            ret = dev->regs[dev->idx];
    }

    opti55x_log(dev->log, "[%04X:%08X] OPTi 55x ISA: [R] %04X = %02X\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

static void
opti55x_ide_cfg_handler(uint8_t is_sec, void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    if (!dev->has_ide)
        return;

    if (is_sec) {
        /* Should call ide_sec_disable here but that causes a segfault in io.c */
        io_removehandler(0x0170, 0x0007, opti55x_ide_read, NULL, NULL, opti55x_ide_write, NULL, NULL, dev);
        if (dev->read_count == 2) {
            io_sethandler(0x0170, 0x0007, opti55x_ide_read, NULL, NULL, opti55x_ide_write, NULL, NULL, dev);
        } else
            opti55x_ide_handler(dev);
    } else {
        /* Should call ide_pri_disable here but that causes a segfault in io.c */
        io_removehandler(0x01f0, 0x0007, opti55x_ide_read, NULL, NULL, opti55x_ide_write, NULL, NULL, dev);
        if (dev->read_count == 2) {
            opti55x_log(dev->log, "OPTi 558 IDE: config handler enable\n");
            io_sethandler(0x01f0, 0x0007, opti55x_ide_read, NULL, NULL, opti55x_ide_write, NULL, NULL, dev);
        } else {
            opti55x_log(dev->log, "OPTi 558 IDE: config handler disable\n");
            opti55x_ide_handler(dev);
        }
    }
}

static void
opti55x_ide_write(uint16_t addr, uint8_t val, void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    if (!(dev->pci_conf_ide[0x04] & 0x01))
        return;

    opti55x_log(dev->log, "OPTi 558 IDE write: addr = %04X, val = %02X\n", addr, val);
    opti55x_log(dev->log, "readcount = %i, ide_reg_en = %i, ide_in_cfg = %i, ide_reg_en_sec = %i, ide_in_cfg_sec = %i\n", dev->read_count, dev->ide_reg_en, dev->ide_in_cfg, dev->ide_reg_en_sec, dev->ide_in_cfg_sec);

    uint8_t ide_sec = 0;
    if ((addr & 0x1F0) != 0x1F0)
        ide_sec = 1;

    addr &= 0x0007;
    if (ide_sec)
        addr |= 0x10;

    if (dev->ide_in_cfg || dev->ide_in_cfg_sec) {
        switch (addr & 0x0007) {
            case 0x0000: /* Read Cycle Timing Register-A/B */
            case 0x0001: /* Write Cycle Timing Register-A/B */
                dev->ide_regs[addr] = val;
                break;
            case 0x0002: /* Internal ID Register */
                dev->ide_regs[addr] = (val & 0xc3);
                if (val & 0x80) {
                    if (!ide_sec) {
                        dev->ide_reg_en = 0;
                        dev->ide_in_cfg = 0;
                        dev->read_count = 0;
                        opti55x_ide_cfg_handler(ide_sec, dev);
                    } else {
                        dev->ide_reg_en_sec = 0;
                        dev->ide_in_cfg_sec = 0;
                        dev->read_count = 0;
                        opti55x_ide_cfg_handler(ide_sec, dev);
                    }
                }
                break;
            case 0x0003: /* Control Register */
                dev->ide_regs[addr] = (val & 0x9c) | 0x01;
                break;
            case 0x0005: /* Strap Register */
                dev->ide_regs[addr] = ((dev->ide_regs[addr] & 0xfe) | (val & 0x01));
                break;
            case 0x0006: /* Miscellaneous Register */
                dev->ide_regs[addr] = (val & 0x7f);
                break;
        }
    } else {
        if (dev->read_count == 2) {
            if ((addr & 0x0007) == 0002) {
                if ((val & 0x03) == 0x03) {
                    if (!ide_sec)
                        dev->ide_reg_en = 1;
                    else
                        dev->ide_reg_en_sec = 1;
                }
                if ((val & 0xc0) == 0x00) {
                    if (dev->ide_reg_en)
                        dev->ide_in_cfg = 1;
                    if (dev->ide_reg_en_sec)
                        dev->ide_in_cfg_sec = 1;
                }
            }
            opti55x_ide_cfg_handler(ide_sec, dev);
            //opti55x_log(dev->log, "readcount2, val = %02X\n", val);
            //opti55x_log(dev->log, "readcount2, ide_reg_en now %i, ide_in_cfg now %i\n", dev->ide_reg_en, dev->ide_in_cfg);
        } else
            dev->read_count = 0;
    }
}

static uint8_t
opti55x_ide_read(uint16_t addr, void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;
    uint8_t          ret = 0xff;
    #ifdef ENABLE_OPTI55X_LOG
    uint16_t         logaddr = addr;
    #endif
    uint8_t ide_sec = 0;

    if (!(dev->pci_conf_ide[0x04] & 0x01))
        return ret;

    if ((addr & 0x1F0) != 0x1F0)
        ide_sec = 1;

    addr &= 0x0007;
    if (ide_sec)
        addr |= 0x10;

    if (dev->ide_in_cfg || dev->ide_in_cfg_sec) {
        if ((addr & 0x0007) != 0x0002)
            ret = dev->ide_regs[addr];
    } else
        dev->read_count = 0;

    opti55x_log(dev->log, "OPTi 558 IDE read: addr = %04X, val = %02X\n", logaddr, ret);
    opti55x_log(dev->log, "readcount = %i, ide_reg_en = %i, ide_in_cfg = %i\n", dev->read_count, dev->ide_reg_en, dev->ide_in_cfg);

    return ret;

}

static uint16_t
opti55x_ide_readw(uint16_t addr, void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;
    uint16_t         ret = 0xffff;
    #ifdef ENABLE_OPTI55X_LOG
    uint16_t         logaddr = addr;
    #endif

    if (!(dev->pci_conf_ide[0x04] & 0x01))
        return ret;

    uint8_t ide_sec = 0;
    if ((addr & 0x1F0) != 0x1F0)
        ide_sec = 1;

    addr &= 0x0007;
    if (ide_sec)
        addr |= 0x10;


    if (!dev->ide_in_cfg && !dev->ide_in_cfg_sec) {
        if ((addr & 0x0007) == 0x01) {
            if (dev->read_count < 2)
                dev->read_count++;
        } else
            dev->read_count = 0;
        if (dev->read_count == 2) {
            opti55x_ide_cfg_handler(ide_sec, dev);
        }
    } else {
        if ((addr & 0x0007) != 0x01)
            dev->read_count = 0;
        opti55x_ide_cfg_handler(ide_sec, dev);
    }

    if ((dev->read_count == 2) && !ide_sec) {
        dev->ide_reg_en = 1;
        dev->ide_in_cfg = 1;
        opti55x_ide_cfg_handler(ide_sec, dev);
    }
    if ((dev->read_count == 2) && ide_sec) {
        dev->ide_reg_en_sec = 1;
        dev->ide_in_cfg_sec = 1;
        opti55x_ide_cfg_handler(ide_sec, dev);
    }

    opti55x_log(dev->log, "OPTi 558 IDE read: addr = %04X, val = %04X\n", logaddr, ret);
    opti55x_log(dev->log, "readcount = %i, ide_reg_en = %i, ide_in_cfg = %i\n", dev->read_count, dev->ide_reg_en, dev->ide_in_cfg);

    return ret;
}

static void
opti55x_reset(void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    /* 82C557M */
    dev->pci_conf[0x00] = 0x45;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x57;
    dev->pci_conf[0x03] = 0xC5;
    dev->pci_conf[0x08] = 0x00;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;

    /* 82C558M */
    dev->pci_conf_sb[0x00] = 0x45;
    dev->pci_conf_sb[0x01] = 0x10;
    dev->pci_conf_sb[0x02] = 0x58;
    dev->pci_conf_sb[0x03] = 0xC5;
    dev->pci_conf_sb[0x08] = 0x10;
    dev->pci_conf_sb[0x09] = 0x00;
    dev->pci_conf_sb[0x0a] = 0x01;
    dev->pci_conf_sb[0x0b] = 0x06;
    dev->pci_conf_sb[0x4f] = 0x40; /* 82C558M Rev 1 */

    dev->pci_conf_ide[0x00] = 0x45;
    dev->pci_conf_ide[0x01] = 0x10;
    #ifdef C558_MODE
    dev->pci_conf_ide[0x02] = 0x58;
    dev->pci_conf_ide[0x03] = 0xC5;
    #else
    dev->pci_conf_ide[0x02] = 0x21;
    dev->pci_conf_ide[0x03] = 0xC6;
    #endif
    dev->pci_conf_ide[0x04] = 0x01;
    dev->pci_conf_ide[0x08] = 0x00;
    dev->pci_conf_ide[0x09] = 0x80;
    dev->pci_conf_ide[0x0a] = 0x01;
    dev->pci_conf_ide[0x0b] = 0x01;
    #ifdef C558_MODE
    dev->pci_conf_ide[0x0e] = 0x80;
    #endif

    dev->pci_conf_ide[0x20] = 0x01;
    dev->pci_conf_ide[0x23] = 0x80;
    dev->pci_conf_ide[0x3C] = 0x0E;
    dev->pci_conf_ide[0x3D] = 0x01;

    /* IDE */
    if (dev->has_ide) {
        opti55x_ide_handler(dev);
        opti55x_ide_irq_handler(dev);

        sff_bus_master_reset(dev->bm[0]);
        sff_bus_master_reset(dev->bm[1]);
    }

    /* DMA S/G */
    dma_set_params(1, 0xffffffff);
}

static void
opti55x_close(void *priv)
{
    opti55x_t *dev = (opti55x_t *) priv;

    smram_del(dev->smram);

    if (dev->log != NULL) {
        log_close(dev->log);
        dev->log = NULL;
    }

    free(dev);
}

static void *
opti55x_init(const device_t *info)
{
    opti55x_t *dev = (opti55x_t *) calloc(1, sizeof(opti55x_t));

    dev->log = log_open("OPTi55x");

    dev->has_ide = (info->local & 0x01);

    /* Northbridge */
    pci_add_card(PCI_ADD_NORTHBRIDGE, opti55x_read, opti55x_write, dev, &dev->nb_slot);

    /* Southbridge */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, opti55x_sb_read, opti55x_sb_write, dev, &dev->sb_slot);

    dev->port_92 = device_add(&port_92_device);

    /* ISA registers */
    io_sethandler(0x0022, 0x0003, opti55x_isa_read, NULL, NULL, opti55x_isa_write, NULL, NULL, dev);
    if (dev->has_ide) {
        io_sethandler(0x0171, 0x0001, NULL, opti55x_ide_readw, NULL, NULL, NULL, NULL, dev);
        io_sethandler(0x01f1, 0x0001, NULL, opti55x_ide_readw, NULL, NULL, NULL, NULL, dev);
    }

    dev->smram = smram_add();
    /* SMRAM is always A0000-BFFFF on this chipset */
    smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x20000, 0, 1);

    /* IDE */
    if (dev->has_ide) {
        pci_add_card(PCI_ADD_IDE, opti55x_pciide_read, opti55x_pciide_write, dev, &dev->ide_slot);

        dev->bm[0] = device_add_inst(&sff8038i_device, 1);
        dev->bm[1] = device_add_inst(&sff8038i_device, 2);
    }

    /* DMA S/G */
    dma_set_sg_base(0x04);
    dma_set_params(1, 0xffffffff);
    dma_ext_mode_init();
    dma_high_page_init();

    opti55x_reset(dev);

    return dev;
}

const device_t opti55x_device = {
    .name          = "OPTi 82C55x (Viper)",
    .internal_name = "opti55x",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = opti55x_init,
    .close         = opti55x_close,
    .reset         = opti55x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t opti55x_noide_device = {
    .name          = "OPTi 82C55x (Viper) (IDE disabled)",
    .internal_name = "opti55x_ide",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = opti55x_init,
    .close         = opti55x_close,
    .reset         = opti55x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

