/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5571 Chipset.
 *
 *
 *
 * Authors: Tiseno100,
 *
 *          Copyright 2021 Tiseno100.
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

#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/port_92.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/smram.h>
#include <86box/usb.h>

#include <86box/chipset.h>

/* Shadow RAM */
#define LSB_READ     ((dev->pci_conf[0x70 + (cur_reg & 0x07)] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY)
#define LSB_WRITE    ((dev->pci_conf[0x70 + (cur_reg & 0x07)] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY)
#define MSB_READ     ((dev->pci_conf[0x70 + (cur_reg & 0x07)] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY)
#define MSB_WRITE    ((dev->pci_conf[0x70 + (cur_reg & 0x07)] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY)
#define SYSTEM_READ  ((dev->pci_conf[0x76] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY)
#define SYSTEM_WRITE ((dev->pci_conf[0x76] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY)

/* IDE Flags (1 Native / 0 Compatibility)*/
#define PRIMARY_COMP_NAT_SWITCH   (dev->pci_conf_sb[1][9] & 1)
#define SECONDARY_COMP_NAT_SWITCH (dev->pci_conf_sb[1][9] & 4)
#define PRIMARY_NATIVE_BASE       (dev->pci_conf_sb[1][0x11] << 8) | (dev->pci_conf_sb[1][0x10] & 0xf8)
#define PRIMARY_NATIVE_SIDE       (((dev->pci_conf_sb[1][0x15] << 8) | (dev->pci_conf_sb[1][0x14] & 0xfc)) + 2)
#define SECONDARY_NATIVE_BASE     (dev->pci_conf_sb[1][0x19] << 8) | (dev->pci_conf_sb[1][0x18] & 0xf8)
#define SECONDARY_NATIVE_SIDE     (((dev->pci_conf_sb[1][0x1d] << 8) | (dev->pci_conf_sb[1][0x1c] & 0xfc)) + 2)
#define BUS_MASTER_BASE           ((dev->pci_conf_sb[1][0x20] & 0xf0) | (dev->pci_conf_sb[1][0x21] << 8))

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
    uint8_t pci_conf[256], pci_conf_sb[3][256];

    int nb_pci_slot, sb_pci_slot;

    port_92_t  *port_92;
    sff8038i_t *ide_drive[2];
    smram_t    *smram;
    usb_t      *usb;

    usb_params_t usb_params;

} sis_5571_t;

static void
sis_5571_shadow_recalc(int cur_reg, sis_5571_t *dev)
{
    if (cur_reg != 0x76) {
        mem_set_mem_state_both(0xc0000 + (0x8000 * (cur_reg & 0x07)), 0x4000, LSB_READ | LSB_WRITE);
        mem_set_mem_state_both(0xc4000 + (0x8000 * (cur_reg & 0x07)), 0x4000, MSB_READ | MSB_WRITE);
    } else
        mem_set_mem_state_both(0xf0000, 0x10000, SYSTEM_READ | SYSTEM_WRITE);

    flushmmucache_nopc();
}

static void
sis_5571_smm_recalc(sis_5571_t *dev)
{
    smram_disable_all();

    switch ((dev->pci_conf[0xa3] & 0xc0) >> 6) {
        case 0x00:
            smram_enable(dev->smram, 0xe0000, 0xe0000, 0x8000, (dev->pci_conf[0xa3] & 0x10), 1);
            break;
        case 0x01:
            smram_enable(dev->smram, 0xe0000, 0xa0000, 0x8000, (dev->pci_conf[0xa3] & 0x10), 1);
            break;
        case 0x02:
            smram_enable(dev->smram, 0xe0000, 0xb0000, 0x8000, (dev->pci_conf[0xa3] & 0x10), 1);
            break;
        case 0x03:
            smram_enable(dev->smram, 0xa0000, 0xa0000, 0x10000, (dev->pci_conf[0xa3] & 0x10), 1);
            break;
    }

    flushmmucache();
}

void
sis_5571_ide_handler(sis_5571_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();
    if (dev->pci_conf_sb[1][4] & 1) {
        if (dev->pci_conf_sb[1][0x4a] & 4) {
            ide_set_base(0, PRIMARY_COMP_NAT_SWITCH ? PRIMARY_NATIVE_BASE : 0x1f0);
            ide_set_side(0, PRIMARY_COMP_NAT_SWITCH ? PRIMARY_NATIVE_SIDE : 0x3f6);
            ide_pri_enable();
        }
        if (dev->pci_conf_sb[1][0x4a] & 2) {
            ide_set_base(1, SECONDARY_COMP_NAT_SWITCH ? SECONDARY_NATIVE_BASE : 0x170);
            ide_set_side(1, SECONDARY_COMP_NAT_SWITCH ? SECONDARY_NATIVE_SIDE : 0x376);
            ide_sec_enable();
        }
    }
}

void
sis_5571_bm_handler(sis_5571_t *dev)
{
    sff_bus_master_handler(dev->ide_drive[0], dev->pci_conf_sb[1][4] & 4, BUS_MASTER_BASE);
    sff_bus_master_handler(dev->ide_drive[1], dev->pci_conf_sb[1][4] & 4, BUS_MASTER_BASE + 8);
}

static void
memory_pci_bridge_write(int func, int addr, uint8_t val, void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    switch (addr) {
        case 0x04: /* Command - low byte */
        case 0x05: /* Command - high byte */
            dev->pci_conf[addr] |= val;
            break;

        case 0x06: /* Status - Low Byte */
            dev->pci_conf[addr] &= val;
            break;

        case 0x07: /* Status - High Byte */
            dev->pci_conf[addr] &= val & 0xbe;
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
        case 0x57: /* SDRAM */
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x59: /* Buffer strength and current rating  */
            dev->pci_conf[addr] = val;
            break;

        case 0x5a:
            dev->pci_conf[addr] = val & 0x03;
            break;

        case 0x60: /* Undocumented */
        case 0x61: /* Undocumented */
        case 0x62: /* Undocumented */
        case 0x63: /* Undocumented */
        case 0x64: /* Undocumented */
        case 0x65: /* Undocumented */
        case 0x66: /* Undocumented */
        case 0x67: /* Undocumented */
        case 0x68: /* Undocumented */
        case 0x69: /* Undocumented */
        case 0x6a: /* Undocumented */
        case 0x6b: /* Undocumented */
            dev->pci_conf[addr] = val;
            break;

        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76: /* Attribute of shadow RAM for BIOS area */
            dev->pci_conf[addr] = val & ((addr != 0x76) ? 0xee : 0xe8);
            sis_5571_shadow_recalc(addr, dev);
            sis_5571_smm_recalc(dev);
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
            port_92_set_features(dev->port_92, !!(val & 0x40), !!(val & 0x80));
            break;

        case 0x84:
        case 0x85:
        case 0x86:
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

            if ((dev->pci_conf[0x9b] & 1) && !!(val & 2)) {
                smi_raise();
                dev->pci_conf[0x9d] |= 1;
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
        case 0x99:
        case 0x9a:
        case 0x9b:
        case 0x9c:
            dev->pci_conf[addr] = val;
            break;

        case 0x9d:
            dev->pci_conf[addr] &= val;
            break;

        case 0x9e: /* STPCLK# Assertion Timer */
        case 0x9f: /* STPCLK# De-assertion Timer */
        case 0xa0:
        case 0xa1:
        case 0xa2:
            dev->pci_conf[addr] = val;
            break;

        case 0xa3: /* SMRAM access control and Power supply control */
            dev->pci_conf[addr] = val & 0xd0;
            sis_5571_smm_recalc(dev);
            break;
    }
    sis_5571_log("SiS5571: dev->pci_conf[%02x] = %02x\n", addr, val);
}

static uint8_t
memory_pci_bridge_read(int func, int addr, void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;
    sis_5571_log("SiS5571: dev->pci_conf[%02x] (%02x)\n", addr, dev->pci_conf[addr]);
    return dev->pci_conf[addr];
}

static void
pci_isa_bridge_write(int func, int addr, uint8_t val, void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;
    switch (func) {
        case 0: /* Bridge */
            switch (addr) {
                case 0x04: /* Command */
                    dev->pci_conf_sb[0][addr] |= val & 0x0f;
                    break;

                case 0x06: /* Status */
                    dev->pci_conf_sb[0][addr] &= val;
                    break;

                case 0x40: /* BIOS Control Register */
                    dev->pci_conf_sb[0][addr] = val & 0x3f;
                    break;

                case 0x41: /* INTA# Remapping Control Register */
                case 0x42: /* INTB# Remapping Control Register */
                case 0x43: /* INTC# Remapping Control Register */
                case 0x44: /* INTD# Remapping Control Register */
                    dev->pci_conf_sb[0][addr] = val & 0x8f;
                    pci_set_irq_routing((addr & 0x07), !(val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
                    break;

                case 0x45:
                    dev->pci_conf_sb[0][addr] = val & 0xec;
                    switch ((val & 0xc0) >> 6) {
                        case 0:
                            cpu_set_isa_speed(7159091);
                            break;
                        case 1:
                            cpu_set_isa_pci_div(4);
                            break;
                        case 2:
                            cpu_set_isa_pci_div(3);
                            break;
                    }
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

                case 0x4c:
                case 0x4d:
                case 0x4e:
                case 0x4f:
                case 0x50:
                case 0x51:
                case 0x52:
                case 0x53:
                case 0x54:
                case 0x55:
                case 0x56:
                case 0x57:
                case 0x58:
                case 0x59:
                case 0x5a:
                case 0x5b:
                case 0x5c:
                case 0x5d:
                case 0x5e:
                    dev->pci_conf_sb[0][addr] = val;
                    break;

                case 0x5f:
                    dev->pci_conf_sb[0][addr] = val & 0x3f;
                    break;

                case 0x60:
                    dev->pci_conf_sb[0][addr] = val;
                    break;

                case 0x61: /* MIRQ Remapping Control Register */
                    dev->pci_conf_sb[0][addr] = val;
                    pci_set_mirq_routing(PCI_MIRQ0, !(val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
                    break;

                case 0x62: /* On-board Device DMA Control Register */
                    dev->pci_conf_sb[0][addr] = val & 0x0f;
                    dma_set_drq((val & 0x07), 1);
                    break;

                case 0x63: /* IDEIRQ Remapping Control Register */
                    dev->pci_conf_sb[0][addr] = val & 0x8f;
                    if (val & 0x80) {
                        sff_set_irq_line(dev->ide_drive[0], val & 0x0f);
                        sff_set_irq_line(dev->ide_drive[1], val & 0x0f);
                    }
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
                    dev->pci_conf_sb[0][addr] = val & 0x1b;
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
                    dev->pci_conf_sb[0][addr] = val & 0x03;
                    break;

                case 0x6e: /* Software-Controlled Interrupt Request, Channels 7-0 */
                case 0x6f: /* Software-Controlled Interrupt Request, channels 15-8 */
                    dev->pci_conf_sb[0][addr] = val;
                    break;

                case 0x70:
                    dev->pci_conf_sb[0][addr] = val & 0xde;
                    break;

                case 0x71: /* Type-F DMA Control Register */
                    dev->pci_conf_sb[0][addr] = val & 0xfe;
                    break;

                case 0x72: /* SMI Triggered By IRQ/GPIO Control */
                case 0x73: /* SMI Triggered By IRQ/GPIO Control */
                    dev->pci_conf_sb[0][addr] = (addr == 0x72) ? val & 0xfe : val;
                    break;

                case 0x74: /* System Standby Timer Reload, System Standby State Exit And Throttling State Exit Control */
                case 0x75: /* System Standby Timer Reload, System Standby State Exit And Throttling State Exit Control */
                case 0x76: /* Monitor Standby Timer Reload And Monitor Standby State ExitControl */
                case 0x77: /* Monitor Standby Timer Reload And Monitor Standby State ExitControl */
                    dev->pci_conf_sb[0][addr] = val;
                    break;
            }
            sis_5571_log("SiS5571-SB: dev->pci_conf[%02x] = %02x\n", addr, val);
            break;

        case 1: /* IDE Controller */
            switch (addr) {
                case 0x04: /* Command low byte */
                    dev->pci_conf_sb[1][addr] = val & 0x05;
                    sis_5571_ide_handler(dev);
                    sis_5571_bm_handler(dev);
                    break;

                case 0x07: /* Status high byte */
                    dev->pci_conf_sb[1][addr] &= val;
                    break;

                case 0x09: /* Programming Interface Byte */
                    dev->pci_conf_sb[1][addr] = val & 0xcf;
                    sis_5571_ide_handler(dev);
                    break;

                case 0x0d: /* Latency Time */
                case 0x10: /* Primary Channel Base Address Register */
                case 0x11: /* Primary Channel Base Address Register */
                case 0x12: /* Primary Channel Base Address Register */
                case 0x13: /* Primary Channel Base Address Register */
                case 0x14: /* Primary Channel Base Address Register */
                case 0x15: /* Primary Channel Base Address Register */
                case 0x16: /* Primary Channel Base Address Register */
                case 0x17: /* Primary Channel Base Address Register */
                case 0x18: /* Secondary Channel Base Address Register */
                case 0x19: /* Secondary Channel Base Address Register */
                case 0x1a: /* Secondary Channel Base Address Register */
                case 0x1b: /* Secondary Channel Base Address Register */
                case 0x1c: /* Secondary Channel Base Address Register */
                case 0x1d: /* Secondary Channel Base Address Register */
                case 0x1e: /* Secondary Channel Base Address Register */
                case 0x1f: /* Secondary Channel Base Address Register */
                    dev->pci_conf_sb[1][addr] = val;
                    sis_5571_ide_handler(dev);
                    break;

                case 0x20: /* Bus Master IDE Control Register Base Address */
                case 0x21: /* Bus Master IDE Control Register Base Address */
                case 0x22: /* Bus Master IDE Control Register Base Address */
                case 0x23: /* Bus Master IDE Control Register Base Address */
                    dev->pci_conf_sb[1][addr] = val;
                    sis_5571_bm_handler(dev);
                    break;

                case 0x30: /* Expansion ROM Base Address */
                case 0x31: /* Expansion ROM Base Address */
                case 0x32: /* Expansion ROM Base Address */
                case 0x33: /* Expansion ROM Base Address */
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
                    dev->pci_conf_sb[1][addr] = val & 0xaf;
                    sis_5571_ide_handler(dev);
                    break;

                case 0x4b: /* IDE General Control register 1 */
                case 0x4c: /* Prefetch Count of Primary Channel (Low Byte) */
                case 0x4d: /* Prefetch Count of Primary Channel (High Byte) */
                case 0x4e: /* Prefetch Count of Secondary Channel (Low Byte) */
                case 0x4f: /* Prefetch Count of Secondary Channel (High Byte) */
                    dev->pci_conf_sb[1][addr] = val;
                    break;
            }
            sis_5571_log("SiS5571-IDE: dev->pci_conf[%02x] = %02x\n", addr, val);
            break;

        case 2: /* USB Controller */
            switch (addr) {
                case 0x04: /* Command - Low Byte */
                    dev->pci_conf_sb[2][addr] = val;
                    ohci_update_mem_mapping(dev->usb, dev->pci_conf_sb[2][0x11], dev->pci_conf_sb[2][0x12], dev->pci_conf_sb[2][0x13], dev->pci_conf_sb[2][4] & 1);
                    break;

                case 0x05: /* Command - High Byte */
                    dev->pci_conf_sb[2][addr] = val & 0x03;
                    break;

                case 0x06: /* Status - Low Byte */
                    dev->pci_conf_sb[2][addr] &= val & 0xc0;
                    break;

                case 0x07: /* Status - High Byte */
                    dev->pci_conf_sb[2][addr] &= val;
                    break;

                case 0x10: /* Memory Space Base Address Register */
                case 0x11: /* Memory Space Base Address Register */
                case 0x12: /* Memory Space Base Address Register */
                case 0x13: /* Memory Space Base Address Register */
                    dev->pci_conf_sb[2][addr] = val & ((addr == 0x11) ? 0x0f : 0xff);
                    ohci_update_mem_mapping(dev->usb, dev->pci_conf_sb[2][0x11], dev->pci_conf_sb[2][0x12], dev->pci_conf_sb[2][0x13], dev->pci_conf_sb[2][4] & 1);
                    break;

                case 0x14: /* IO Space Base Address Register */
                case 0x15: /* IO Space Base Address Register */
                case 0x16: /* IO Space Base Address Register */
                case 0x17: /* IO Space Base Address Register */
                case 0x3c: /* Interrupt Line */
                    dev->pci_conf_sb[2][addr] = val;
                    break;
            }
            sis_5571_log("SiS5571-USB: dev->pci_conf[%02x] = %02x\n", addr, val);
    }
}

static uint8_t
pci_isa_bridge_read(int func, int addr, void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    switch (func) {
        case 0:
            sis_5571_log("SiS5571-SB: dev->pci_conf[%02x] (%02x)\n", addr, dev->pci_conf_sb[0][addr]);
            return dev->pci_conf_sb[0][addr];
        case 1:
            sis_5571_log("SiS5571-IDE: dev->pci_conf[%02x] (%02x)\n", addr, dev->pci_conf_sb[1][addr]);
            return dev->pci_conf_sb[1][addr];
        case 2:
            sis_5571_log("SiS5571-USB: dev->pci_conf[%02x] (%02x)\n", addr, dev->pci_conf_sb[2][addr]);
            return dev->pci_conf_sb[2][addr];
        default:
            return 0xff;
    }
}

static void
sis_5571_usb_update_interrupt(usb_t* usb, void* priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    if (dev->pci_conf_sb[0][0x68] & 0x80) {
        /* TODO: Is the normal PCI interrupt inhibited when USB IRQ remapping is enabled? */
        switch (dev->pci_conf_sb[0][0x68] & 0x0F) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x08:
            case 0x0d:
                break;
            default:
                if (usb->irq_level)
                    picint(1 << dev->pci_conf_sb[0][0x68] & 0x0f);
                else
                    picintc(1 << dev->pci_conf_sb[0][0x68] & 0x0f);
                break;
        }
    } else {
        if (usb->irq_level)
            pci_set_irq(dev->sb_pci_slot, PCI_INTA);
        else
            pci_clear_irq(dev->sb_pci_slot, PCI_INTA);
    }
}

static uint8_t
sis_5571_usb_handle_smi(usb_t* usb, void* priv)
{
    /* Left unimplemented for now. */
    return 1;
}

static void
sis_5571_reset(void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    /* Memory/PCI Bridge */
    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x71;
    dev->pci_conf[0x03] = 0x55;
    dev->pci_conf[0x04] = 0xfd;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x9e] = 0xff;
    dev->pci_conf[0x9f] = 0xff;
    dev->pci_conf[0xa2] = 0xff;

    /* PCI to ISA bridge */
    dev->pci_conf_sb[0][0x00] = 0x39;
    dev->pci_conf_sb[0][0x01] = 0x10;
    dev->pci_conf_sb[0][0x02] = 0x08;
    dev->pci_conf_sb[0][0x04] = 0xfd;
    dev->pci_conf_sb[0][0x08] = 0x01;
    dev->pci_conf_sb[0][0x0a] = 0x01;
    dev->pci_conf_sb[0][0x0b] = 0x06;

    /* IDE Controller */
    dev->pci_conf_sb[1][0x00] = 0x39;
    dev->pci_conf_sb[1][0x01] = 0x10;
    dev->pci_conf_sb[1][0x02] = 0x13;
    dev->pci_conf_sb[1][0x03] = 0x55;
    dev->pci_conf_sb[1][0x08] = 0xc0;
    dev->pci_conf_sb[1][0x0a] = 0x01;
    dev->pci_conf_sb[1][0x0b] = 0x01;
    dev->pci_conf_sb[1][0x0e] = 0x80;
    dev->pci_conf_sb[1][0x4a] = 0x06;
    sff_set_slot(dev->ide_drive[0], dev->sb_pci_slot);
    sff_set_slot(dev->ide_drive[1], dev->sb_pci_slot);
    sff_bus_master_reset(dev->ide_drive[0], BUS_MASTER_BASE);
    sff_bus_master_reset(dev->ide_drive[1], BUS_MASTER_BASE + 8);

    /* USB Controller */
    dev->pci_conf_sb[2][0x00] = 0x39;
    dev->pci_conf_sb[2][0x01] = 0x10;
    dev->pci_conf_sb[2][0x02] = 0x01;
    dev->pci_conf_sb[2][0x03] = 0x70;
    dev->pci_conf_sb[2][0x08] = 0xb0;
    dev->pci_conf_sb[2][0x09] = 0x10;
    dev->pci_conf_sb[2][0x0a] = 0x03;
    dev->pci_conf_sb[2][0x0b] = 0xc0;
    dev->pci_conf_sb[2][0x0e] = 0x80;
    dev->pci_conf_sb[2][0x14] = 0x01;
    dev->pci_conf_sb[2][0x3d] = 0x01;
}

static void
sis_5571_close(void *priv)
{
    sis_5571_t *dev = (sis_5571_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
sis_5571_init(const device_t *info)
{
    sis_5571_t *dev = (sis_5571_t *) malloc(sizeof(sis_5571_t));
    memset(dev, 0x00, sizeof(sis_5571_t));

    dev->nb_pci_slot = pci_add_card(PCI_ADD_NORTHBRIDGE, memory_pci_bridge_read, memory_pci_bridge_write, dev);
    dev->sb_pci_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, pci_isa_bridge_read, pci_isa_bridge_write, dev);

    /* MIRQ */
    pci_enable_mirq(0);

    /* Port 92 & SMRAM */
    dev->port_92 = device_add(&port_92_pci_device);
    dev->smram   = smram_add();

    /* SFF IDE */
    dev->ide_drive[0] = device_add_inst(&sff8038i_device, 1);
    dev->ide_drive[1] = device_add_inst(&sff8038i_device, 2);

    /* USB */
    dev->usb_params.parent_priv      = dev;
    dev->usb_params.update_interrupt = sis_5571_usb_update_interrupt;
    dev->usb_params.smi_handle       = sis_5571_usb_handle_smi;
    dev->usb                         = device_add_parameters(&usb_device, &dev->usb_params);

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
