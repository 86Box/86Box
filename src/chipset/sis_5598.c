/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SiS 5597/5598 Pentium PCI/ISA Chipset.
 *
 *
 *
 * Authors:	Tiseno100,
 *
 *		Copyright 2021 Tiseno100.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/apm.h>
#include <86box/nvr.h>

#include <86box/acpi.h>
#include <86box/ddma.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/smram.h>
#include <86box/usb.h>

#include <86box/chipset.h>

/* ACPI Flags */
#define ACPI_BASE ((dev->pci_conf_sb[0][0x91] << 8) | dev->pci_conf_sb[0][0x90])
#define ACPI_EN (dev->pci_conf_sb[0][0x40] & 0x80)

/* DIMM */
#define DIMM_BANK0 dev->pci_conf[0x60]
#define DIMM_BANK1 dev->pci_conf[0x61]
#define DIMM_BANK_ENABLE dev->pci_conf[0x63]

/* IDE Flags (1 Native / 0 Compatibility)*/
#define PRIMARY_COMP_NAT_SWITCH (dev->pci_conf_sb[1][9] & 1)
#define SECONDARY_COMP_NAT_SWITCH (dev->pci_conf_sb[1][9] & 4)
#define PRIMARY_NATIVE_BASE (dev->pci_conf_sb[1][0x11] << 8) | (dev->pci_conf_sb[1][0x10] & 0xf8)
#define PRIMARY_NATIVE_SIDE (((dev->pci_conf_sb[1][0x15] << 8) | (dev->pci_conf_sb[1][0x14] & 0xfc)) + 2)
#define SECONDARY_NATIVE_BASE (dev->pci_conf_sb[1][0x19] << 8) | (dev->pci_conf_sb[1][0x18] & 0xf8)
#define SECONDARY_NATIVE_SIDE (((dev->pci_conf_sb[1][0x1d] << 8) | (dev->pci_conf_sb[1][0x1c] & 0xfc)) + 2)
#define BUS_MASTER_BASE ((dev->pci_conf_sb[1][0x20] & 0xf0) | (dev->pci_conf_sb[1][0x21] << 8))

#ifdef ENABLE_SIS_5598_LOG
int sis_5598_do_log = ENABLE_SIS_5598_LOG;
static void
sis_5598_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5598_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define sis_5598_log(fmt, ...)
#endif

typedef struct sis_5598_t
{
    acpi_t *acpi;
    ddma_t *ddma;
    nvr_t *nvr;
    sff8038i_t *ide_drive[2];
    smram_t *smram;
    port_92_t *port_92;
    usb_t *usb;

    int nb_device_id, sb_device_id;
    uint8_t pci_conf[256], pci_conf_sb[3][256];
} sis_5598_t;

void sis_5598_dimm_programming(sis_5598_t *dev)
{
/*
Based completely off the PC Chips M571 Manual
Configurations are forced and don't work as intended
*/
    switch (mem_size >> 10)
    {
    case 8:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc0;
        break;
    case 16:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc0;
        DIMM_BANK1 = 0xc0;
        break;
    case 24:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc2;
        DIMM_BANK1 = 0xc0;
        break;
    case 32:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc2;
        DIMM_BANK1 = 0xc2;
        break;
    case 40:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc8;
        DIMM_BANK1 = 0xc0;
        break;
    case 48:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc8;
        DIMM_BANK1 = 0xc2;
        break;
    case 56: /* Unintended */
    case 64:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc8;
        DIMM_BANK1 = 0xc8;
        break;
    case 72:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc6;
        DIMM_BANK1 = 0xc0;
        break;
    case 80:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc6;
        DIMM_BANK1 = 0xc2;
        break;
    case 88: /* Unintended */
    case 96:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc6;
        DIMM_BANK1 = 0xc8;
        break;
    case 104: /* Unintended */
    case 112: /* Unintended */
    case 120: /* Unintended */
    case 128:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 0xc6;
        DIMM_BANK1 = 0xc6;
        break;
    case 136:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 10 | 0xca;
        DIMM_BANK1 = 0xc0;
        break;
    case 144:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 10 | 0xca;
        DIMM_BANK1 = 2 | 0xc2;
        break;
    case 152: /* Unintended */
    case 160:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 10 | 0xca;
        DIMM_BANK1 = 8 | 0xc8;
        break;
    case 168: /* Unintended */
    case 176: /* Unintended */
    case 184: /* Unintended */
    case 192:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 10 | 0xca;
        DIMM_BANK1 = 6 | 0xc6;
        break;
    case 200: /* Unintended */
    case 208: /* Unintended */
    case 216: /* Unintended */
    case 224: /* Unintended */
    case 232: /* Unintended */
    case 240: /* Unintended */
    case 248: /* Unintended */
    case 256:
        DIMM_BANK_ENABLE = 1;
        DIMM_BANK0 = 10 | 0xca;
        DIMM_BANK1 = 10 | 0xca;
        break;
    }
}

void sis_5598_shadow(int cur_reg, sis_5598_t *dev)
{
    if (cur_reg == 0x76)
    {
        mem_set_mem_state_both(0xf0000, 0x10000, ((dev->pci_conf[cur_reg] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }
    else
    {
        mem_set_mem_state_both(0xc0000 + ((cur_reg & 7) * 0x8000), 0x4000, ((dev->pci_conf[cur_reg] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_both(0xc4000 + ((cur_reg & 7) * 0x8000), 0x4000, ((dev->pci_conf[cur_reg] & 8) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 2) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }
    flushmmucache_nopc();
}

void sis_5598_smram(sis_5598_t *dev)
{
    smram_disable_all();

    switch ((dev->pci_conf[0xa3] & 0xc0) >> 6)
    {
    case 0:
        if (dev->pci_conf[0x74] == 0)
            smram_enable(dev->smram, 0x000e0000, 0x000e0000, 0x8000, dev->pci_conf[0xa3] & 0x10, 1);
        break;
    case 1:
        if (dev->pci_conf[0x74] == 0)
            smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x10000, dev->pci_conf[0xa3] & 0x10, 1);
        break;
    case 2:
        if (dev->pci_conf[0x74] == 0)
            smram_enable(dev->smram, 0x000e0000, 0x000b0000, 0x10000, dev->pci_conf[0xa3] & 0x10, 1);
        break;
    case 3:
        smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x10000, dev->pci_conf[0xa3] & 0x10, 1);
        break;
    }

    flushmmucache();
}

void sis_5598_ddma_update(sis_5598_t *dev)
{
    for (int i = 0; i < 8; i++)
        if (i != 4)
            ddma_update_io_mapping(dev->ddma, i, dev->pci_conf_sb[0][0x80] >> 4, dev->pci_conf_sb[0][0x81], dev->pci_conf_sb[0][0x80] & 1);
}

void sis_5598_ide_handler(sis_5598_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();
    if (dev->pci_conf_sb[1][4] & 1)
    {
        if (dev->pci_conf_sb[1][0x4a] & 4)
        {
            ide_set_base(0, PRIMARY_COMP_NAT_SWITCH ? PRIMARY_NATIVE_BASE : 0x1f0);
            ide_set_side(0, PRIMARY_COMP_NAT_SWITCH ? PRIMARY_NATIVE_SIDE : 0x3f6);
            ide_pri_enable();
        }
        if (dev->pci_conf_sb[1][0x4a] & 2)
        {
            ide_set_base(1, SECONDARY_COMP_NAT_SWITCH ? SECONDARY_NATIVE_BASE : 0x170);
            ide_set_side(1, SECONDARY_COMP_NAT_SWITCH ? SECONDARY_NATIVE_SIDE : 0x376);
            ide_sec_enable();
        }
    }
}

void sis_5598_bm_handler(sis_5598_t *dev)
{
    sff_bus_master_handler(dev->ide_drive[0], dev->pci_conf_sb[1][4] & 4, BUS_MASTER_BASE);
    sff_bus_master_handler(dev->ide_drive[1], dev->pci_conf_sb[1][4] & 4, BUS_MASTER_BASE + 8);
}

static void
sis_5597_write(int func, int addr, uint8_t val, void *priv)
{
    sis_5598_t *dev = (sis_5598_t *)priv;

    switch (addr)
    {
    case 0x04: /* Command */
        dev->pci_conf[addr] = val & 3;
        break;

    case 0x05: /* Command */
        dev->pci_conf[addr] = val & 2;
        break;

    case 0x07: /* Status */
        dev->pci_conf[addr] &= val & 0xb9;
        break;

    case 0x0d: /* Master latency timer */
        dev->pci_conf[addr] = val;
        break;

    case 0x50: /* Host Interface and DRAM arbiter */
        dev->pci_conf[addr] = val & 0xfc;
        break;

    case 0x51:                                     /* L2 Cache Controller */
        dev->pci_conf[addr] = (val & 0xcf) | 0x20; /* 512KB L2 Cache Installed */
        cpu_cache_ext_enabled = !!(val & 0x40);
        cpu_update_waitstates();
        break;

    case 0x52: /* Control Register */
        dev->pci_conf[addr] = val & 0xe3;
        break;

    case 0x53: /* DRAM Control Register */
    case 0x54: /* DRAM Control Register 0*/
        dev->pci_conf[addr] = val;
        break;

    case 0x55: /* FPM/EDO DRAM Control Register 1 */
        dev->pci_conf[addr] = val & 0xfe;
        break;

    case 0x56: /* Memory Data Latch Enable (MDLE) Delay Control Register */
    case 0x57: /* SDRAM Control Register */
        dev->pci_conf[addr] = val;
        break;

    case 0x58:
        dev->pci_conf[addr] = val & 0xfc;
        break;

    case 0x59: /* DRAM signals driving current Control */
        dev->pci_conf[addr] = val;
        break;

    case 0x5a: /* PCI signals driving current Control */
        dev->pci_conf[addr] = val & 3;
        break;

    case 0x6c:                   /* Integrated VGA Controller Control */
        dev->pci_conf[addr] = 0; /* Kill the Integrated GPU */
        break;

    case 0x6d: /* Starting Address of Shared Memory Hole HA[28:23] */
        dev->pci_conf[addr] = val & 2;
        break;

    case 0x6e:
        dev->pci_conf[addr] = val & 0xc0;
        break;

    case 0x70: /* shadow RAM Registers */
    case 0x71: /* shadow RAM Registers */
    case 0x72: /* shadow RAM Registers */
    case 0x73: /* shadow RAM Registers */
    case 0x74: /* shadow RAM Registers */
    case 0x75: /* shadow RAM Registers */
    case 0x76: /* Attribute of shadow RAM for BIOS area */
        dev->pci_conf[addr] = (addr == 0x76) ? (val & 0xe4) : (val & 0xee);
        sis_5598_shadow(addr, dev);
        break;

    case 0x77: /* Characteristics of non-cacheable area */
        dev->pci_conf[addr] = val & 0x0f;
        break;

    case 0x78: /* Allocation of Non-Cacheable area I */
    case 0x79:
    case 0x7a: /* Allocation of Non-Cacheable area II */
    case 0x7b:
        dev->pci_conf[addr] = val;
        break;

    case 0x80: /* PCI master characteristics */
        dev->pci_conf[addr] = val & 0xfe;
        break;

    case 0x81:
        dev->pci_conf[addr] = val & 0xbe;
        break;

    case 0x82:
        dev->pci_conf[addr] = val;
        break;

    case 0x83: /* CPU to PCI characteristics */
        dev->pci_conf[addr] = val;
        port_92_set_features(dev->port_92, !!(val & 0x40), !!(val & 0x80));
        break;

    case 0x84: /* PCI grant timer */
    case 0x85:
    case 0x86: /* CPU idle timer */
        dev->pci_conf[addr] = val;
        break;

    case 0x87: /* Miscellaneous register */
        dev->pci_conf[addr] = val & 0xfc;
        break;

    case 0x88: /* Base address of fast back-to-back area */
    case 0x89:
        dev->pci_conf[addr] = val;
        break;

    case 0x8a: /* Size of fast back-to-back area */
    case 0x8b:
    case 0x90: /* Legacy PMU control register */
    case 0x91: /* Address trap for Legacy PMU function */
    case 0x92:
        dev->pci_conf[addr] = val;
        break;

    case 0x93: /* STPCLK# and APM SMI control */
        dev->pci_conf[addr] = val;
        if ((dev->pci_conf[0x9b] & 1) && (val & 1))
        {
            smi_line = 1;
            dev->pci_conf[0x9d] |= 1;
        }
        break;

    case 0x94: /* Cyrix 6x86 and PMU function control */
        dev->pci_conf[addr] = val & 0xf8;
        break;

    case 0x95:
        dev->pci_conf[addr] = val & 0xfb;
        break;

    case 0x96: /* Time slot and Programmable 10-bit I/O port definition */
        dev->pci_conf[addr] = val & 0xfb;
        break;

    case 0x97: /* Programmable 10-bit I/O port address bits A9~A2 */
    case 0x98: /* Programmable 16-bit I/O port */
    case 0x99:
    case 0x9a: /* System Standby Timer events control */
    case 0x9b: /* Monitor Standdby Timer events control */
    case 0x9c: /* SMI Request events status 0 */
    case 0x9d: /* SMI Request events status 1 */
    case 0x9e: /* STPCLK# Assertion Timer */
    case 0x9f: /* STPCLK#  De-assertion Timer */
    case 0xa0: /* Monitor Standby Timer */
    case 0xa1:
    case 0xa2: /* System Standby Time */
        dev->pci_conf[addr] = val;
        break;

    case 0xa3: /* SMRAM access control and Power supply control */
        dev->pci_conf[addr] = val & 0xd0;
        sis_5598_smram(dev);
        break;
    }

    sis_5598_log("SiS 5597: dev->regs[%02x] = %02x POST: %02x\n", addr, dev->pci_conf[addr], inb(0x80));
}

static uint8_t
sis_5597_read(int func, int addr, void *priv)
{
    sis_5598_t *dev = (sis_5598_t *)priv;
    sis_5598_log("SiS 5597: dev->regs[%02x] (%02x) POST: %02x\n", addr, dev->pci_conf[addr], inb(0x80));
    return dev->pci_conf[addr];
}

void sis_5598_pcitoisa_write(int addr, uint8_t val, sis_5598_t *dev)
{
    switch (addr)
    {
    case 0x04: /* Command Port */
        dev->pci_conf_sb[0][addr] = val & 0x0f;
        break;

    case 0x07: /* Status */
        dev->pci_conf_sb[0][addr] &= val & 0x3f;
        break;

    case 0x0d: /* Master latency timer */
        dev->pci_conf_sb[0][addr] = val;
        break;

    case 0x40: /* BIOS Control Register */
        dev->pci_conf_sb[0][addr] = val;
        acpi_update_io_mapping(dev->acpi, ACPI_BASE, ACPI_EN);
        break;

    case 0x41: /* INTA#/INTB#INTC# Remapping Control Register */
    case 0x42:
    case 0x43:
    case 0x44: /* INTD# Remapping Control Register */
        dev->pci_conf_sb[0][addr] = val & ((addr == 0x44) ? 0x9f : 0x8f);
        pci_set_irq_routing(addr & 7, (val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
        break;

    case 0x45:
        dev->pci_conf_sb[0][addr] = val & 0xfc;
        switch ((val & 0xc0) >> 6)
        {
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
        dev->pci_conf_sb[0][addr] = val;
        break;

    case 0x47: /* DMA Clock and Wait State Control Register */
        dev->pci_conf_sb[0][addr] = val & 0x7f;
        break;

    case 0x48: /* ISA Master/DMA Memory Cycle Control Register 1 */
    case 0x49: /* ISA Master/DMA Memory Cycle Control Register 2 */
    case 0x4a: /* ISA Master/DMA Memory Cycle Control Register 3 */
    case 0x4b: /* ISA Master/DMA Memory Cycle Control Register 4 */
    case 0x4c: /* 4Ch/4Dh/4Eh/4Fh   Initialization Command Word 1/2/3/4 Mirror Register I */
    case 0x4d: /* 4Ch/4Dh/4Eh/4Fh   Initialization Command Word 1/2/3/4 Mirror Register I */
    case 0x4e: /* 4Ch/4Dh/4Eh/4Fh   Initialization Command Word 1/2/3/4 Mirror Register I */
    case 0x4f: /* 4Ch/4Dh/4Eh/4Fh   Initialization Command Word 1/2/3/4 Mirror Register I */
    case 0x50: /* Initialization Command Word 1/2/3/4 mirror Register II */
    case 0x51: /* Initialization Command Word 1/2/3/4 mirror Register II */
    case 0x52: /* Initialization Command Word 1/2/3/4 mirror Register II */
    case 0x53: /* Initialization Command Word 1/2/3/4 mirror Register II */
    case 0x54: /* Operational Control Word 2/3 Mirror Register I */
    case 0x55:
    case 0x56: /* Operational Control Word 2/3 Mirror Register II */
    case 0x57:
    case 0x58: /* Counter Access Ports Mirror Register 0 */
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

    case 0x60: /* Mirror port */
        dev->pci_conf_sb[0][addr] = (uint8_t)inb(0x70);
        break;

    case 0x61: /* IDEIRQ Remapping Control Register */
        dev->pci_conf_sb[0][addr] = val & 0xcf;
        if (val & 0x80)
        {
            sff_set_irq_line(dev->ide_drive[0], val & 0x0f);
            sff_set_irq_line(dev->ide_drive[1], val & 0x0f);
        }
        break;

    case 0x62: /* USBIRQ Remapping Control Register */
    case 0x63: /* GPCS0 Control Register */
    case 0x64: /* GPCS1 Control Register */
    case 0x65: /* GPCS0 Output Mode Control Register */
    case 0x66:
    case 0x67: /* GPCS1 Output Mode Control Register */
    case 0x68:
        dev->pci_conf_sb[0][addr] = val;
        break;

    case 0x69: /* GPCS0/1 De-Bounce Control Register */
        dev->pci_conf_sb[0][addr] = val & 0xdf;
        break;

    case 0x6a: /* ACPI/SCI IRQ Remapping Control Register */
        dev->pci_conf_sb[0][addr] = val;
        if (val & 0x80)
            acpi_set_irq_line(dev->acpi, val & 0x0f);
        break;

    case 0x6b:
        dev->pci_conf_sb[0][addr] = val;
        break;

    case 0x6c:
        dev->pci_conf_sb[0][addr] = val & 0xfe;
        break;

    case 0x6d:
    case 0x6e: /* Software-Controlled Interrupt Request, Channels 7-0 */
    case 0x6f: /* Software-Controlled Interrupt Request, channels 15-8 */
    case 0x70:
        dev->pci_conf_sb[0][addr] = val;
        break;

    case 0x71: /* Type-F DMA Control Register */
        dev->pci_conf_sb[0][addr] = val & 0xef;
        break;

    case 0x72: /* SMI Triggered By IRQ Control */
        dev->pci_conf_sb[0][addr] = val & 0xfa;
        break;

    case 0x73: /* SMI Triggered By IRQ Control */
        dev->pci_conf_sb[0][addr] = val;
        break;

    case 0x74: /* System Standby Timer Reload, System Standby State Exit And Throttling State Exit Control */
        dev->pci_conf_sb[0][addr] = val & 0xfb;
        break;

    case 0x75: /* System Standby Timer Reload, System Standby State Exit And Throttling State Exit Control */
        dev->pci_conf_sb[0][addr] = val;
        break;

    case 0x76: /* Monitor Standby Timer Reload And Monitor Standby State Exit Control */
        dev->pci_conf_sb[0][addr] = val & 0xfb;
        break;

    case 0x77: /* Monitor Standby Timer Reload And Monitor Standby State Exit Control */
        dev->pci_conf_sb[0][addr] = val;
        break;

    case 0x80: /* DDMA Control Register */
    case 0x81:
        dev->pci_conf_sb[0][addr] = val & ((addr == 0x81) ? 0xff : 0xf1);
        sis_5598_ddma_update(dev);
        break;

    case 0x84:
        dev->pci_conf_sb[0][addr] = val & 0xef;
        break;

    case 0x88:
        dev->pci_conf_sb[0][addr] = val;
        break;

    case 0x89: /* Serial Interrupt Enable Register 1 */
        dev->pci_conf_sb[0][addr] = val & 0x7e;
        break;

    case 0x8a: /* Serial Interrupt Enable Register 2 */
        dev->pci_conf_sb[0][addr] = val & 0xef;
        break;

    case 0x90: /* ACPI Base Address Register */
    case 0x91: /* ACPI Base Address Register */
        dev->pci_conf_sb[0][addr] = val;
        acpi_update_io_mapping(dev->acpi, ACPI_BASE, ACPI_EN);
        break;
    }
}

void sis_5598_ide_write(int addr, uint8_t val, sis_5598_t *dev)
{
    switch (addr)
    {
    case 0x04: /* Command */
        dev->pci_conf_sb[1][addr] = val & 7;
        sis_5598_ide_handler(dev);
        sis_5598_bm_handler(dev);
        break;

    case 0x06: /* Status */
        dev->pci_conf_sb[1][addr] = val & 0x20;
        break;

    case 0x07: /* Status */
        dev->pci_conf_sb[1][addr] = val & 0x3c;
        break;

    case 0x0d: /* Latency Timer */
        dev->pci_conf_sb[1][addr] = val;
        break;

    case 0x09: /* Programming Interface Byte */
    case 0x10: /* Primary Channel Command Block Base Address Register */
    case 0x11: /* Primary Channel Command Block Base Address Register */
    case 0x12: /* Primary Channel Command Block Base Address Register */
    case 0x13: /* Primary Channel Command Block Base Address Register */
    case 0x14: /* Primary Channel Control Block Base Address Register */
    case 0x15: /* Primary Channel Control Block Base Address Register */
    case 0x16: /* Primary Channel Control Block Base Address Register */
    case 0x17: /* Primary Channel Control Block Base Address Register */
    case 0x18: /* Secondary Channel Command Block Base Address Register */
    case 0x19: /* Secondary Channel Command Block Base Address Register */
    case 0x1a: /* Secondary Channel Command Block Base Address Register */
    case 0x1b: /* Secondary Channel Command Block Base Address Register */
    case 0x1c: /* Secondary Channel Control Block Base Address Register */
    case 0x1d: /* Secondary Channel Control Block Base Address Register */
    case 0x1e: /* Secondary Channel Control Block Base Address Register */
    case 0x1f: /* Secondary Channel Control Block Base Address Register */
        dev->pci_conf_sb[1][addr] = val & ((addr == 9) ? 0x0f : 0xff);
        sis_5598_ide_handler(dev);
        break;

    case 0x20: /* Bus Master IDE Control Register Base Address */
    case 0x21: /* Bus Master IDE Control Register Base Address */
    case 0x22: /* Bus Master IDE Control Register Base Address */
    case 0x23: /* Bus Master IDE Control Register Base Address */
        dev->pci_conf_sb[1][addr] = val;
        sis_5598_bm_handler(dev);
        break;

    case 0x2c: /* Subsystem ID */
        dev->pci_conf_sb[1][addr] = val;
        break;

    case 0x30: /* Expansion ROM Base Address */
    case 0x31: /* Expansion ROM Base Address */
    case 0x32: /* Expansion ROM Base Address */
    case 0x33: /* Expansion ROM Base Address */
        dev->pci_conf_sb[1][addr] = val;
        break;

    case 0x40: /* IDE Primary Channel/Master Drive Data Recovery Time Control */
        dev->pci_conf_sb[1][addr] = val & 0xcf;
        break;

    case 0x41: /* IDE Primary Channel/Master Drive Control */
        dev->pci_conf_sb[1][addr] = val & 0xe7;
        break;

    case 0x42: /* IDE Primary Channel/Slave Drive Data Recovery Time Control */
        dev->pci_conf_sb[1][addr] = val & 0x0f;
        break;

    case 0x43: /* IDE Primary Channel/Slave Drive Data Active Time Control */
    case 0x44: /* IDE Secondary Channel/Master Drive Data Recovery Time Control */
    case 0x45: /* IDE Secondary Channel/Master Drive Data Active Time Control */
        dev->pci_conf_sb[1][addr] = val & 0xe7;
        break;

    case 0x46: /* IDE Secondary Channel/Slave Drive Data Recovery Time Control */
        dev->pci_conf_sb[1][addr] = val & 0x0f;
        break;

    case 0x47: /* IDE Secondary Channel/Slave Drive Data Active Time Control */
        dev->pci_conf_sb[1][addr] = val & 0xe7;
        break;

    case 0x48: /* IDE Command Recovery Time Control */
    case 0x49: /* IDE Command Active Time Control */
        dev->pci_conf_sb[1][addr] = val & 0x0f;
        break;

    case 0x4a: /* IDE General Control Register 0 */
        dev->pci_conf_sb[1][addr] = val;
        sis_5598_ide_handler(dev);
        break;

    case 0x4b: /* IDE General Control register 1 */
    case 0x4c: /* Prefetch Count of Primary Channel */
    case 0x4d:
    case 0x4e: /* Prefetch Count of  Secondary Channel */
    case 0x4f:
    case 0x50: /* IDE minimum accessed time register */
    case 0x51:
        dev->pci_conf_sb[1][addr] = val;
        break;

    case 0x52: /* IDE Miscellaneous Control Register */
        dev->pci_conf_sb[1][addr] = val & 0x0f;
        break;
    }
}

void sis_5598_usb_write(int addr, uint8_t val, sis_5598_t *dev)
{
    switch (addr)
    {
    case 0x04: /* Command */
        dev->pci_conf_sb[2][addr] = val;
        ohci_update_mem_mapping(dev->usb, dev->pci_conf_sb[2][0x11], dev->pci_conf_sb[2][0x12], dev->pci_conf_sb[2][0x13], dev->pci_conf_sb[2][4] & 1);
        break;

    case 0x05: /* Command */
        dev->pci_conf_sb[2][addr] = val & 3;
        break;

    case 0x06: /* Status */
        dev->pci_conf_sb[2][addr] &= val & 0xf0;
        break;

    case 0x07: /* Status */
        dev->pci_conf_sb[2][addr] &= val;
        break;

    case 0x0d: /* Latency Timer */
        dev->pci_conf_sb[2][addr] = val;
        break;

    case 0x11: /* USB Memory Space Base Address Register */
    case 0x12: /* USB Memory Space Base Address Register */
    case 0x13: /* USB Memory Space Base Address Register */
        dev->pci_conf_sb[2][addr] = val & ((addr == 0x11) ? 0x0f : 0xff);
        ohci_update_mem_mapping(dev->usb, dev->pci_conf_sb[2][0x11], dev->pci_conf_sb[2][0x12], dev->pci_conf_sb[2][0x13], dev->pci_conf_sb[2][4] & 1);
        break;

    case 0x3c: /* Interrupt Line */
    case 0x3d: /* Interrupt Pin */
    case 0x3e: /* Minimum Grant Time */
    case 0x3f: /* Maximum Latency Time */
        dev->pci_conf_sb[2][addr] = val;
        break;
    }
}

static void
sis_5598_write(int func, int addr, uint8_t val, void *priv)
{
    sis_5598_t *dev = (sis_5598_t *)priv;
    switch (func)
    {
    case 0:
        sis_5598_pcitoisa_write(addr, val, dev);
        break;
    case 1:
        sis_5598_ide_write(addr, val, dev);
        break;
    case 2:
        sis_5598_usb_write(addr, val, dev);
        break;
    }
    sis_5598_log("SiS 5598: dev->regs[%02x][%02x] = %02x POST: %02x\n", func, addr, dev->pci_conf_sb[func][addr], inb(0x80));
}

static uint8_t
sis_5598_read(int func, int addr, void *priv)
{
    sis_5598_t *dev = (sis_5598_t *)priv;
    if ((func >= 0) && (func <= 2))
    {
        sis_5598_log("SiS 5598: dev->regs[%02x][%02x] (%02x) POST: %02x\n", func, addr, dev->pci_conf_sb[func][addr], inb(0x80));
        return dev->pci_conf_sb[func][addr];
    }
    else
        return 0xff;
}

static void
sis_5598_defaults(sis_5598_t *dev)
{
    dev->pci_conf[0x00] = 0x39; /* SiS */
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x97; /* 5597 */
    dev->pci_conf[0x03] = 0x55;
    dev->pci_conf[0x08] = 4;
    dev->pci_conf[0x0b] = 6;
    dev->pci_conf[0x0d] = 0xff;
    dev->pci_conf[0x9e] = 0xff;
    dev->pci_conf[0x9f] = 0xff;
    dev->pci_conf[0xa0] = 0xff;

    dev->pci_conf_sb[0][0x00] = 0x39; /* SiS */
    dev->pci_conf_sb[0][0x01] = 0x10;
    dev->pci_conf_sb[0][0x02] = 8; /* 5598 */
    dev->pci_conf_sb[0][0x08] = 1;
    dev->pci_conf_sb[0][0x0a] = 1;
    dev->pci_conf_sb[0][0x0b] = 6;
    dev->pci_conf_sb[0][0x0d] = 0xff;
    dev->pci_conf_sb[0][0x0e] = 0x30;
    dev->pci_conf_sb[0][0x0f] = 0x30;
    dev->pci_conf_sb[0][0x48] = 1;
    dev->pci_conf_sb[0][0x4a] = 0x10;
    dev->pci_conf_sb[0][0x4b] = 0x0f;
    dev->pci_conf_sb[0][0x6d] = 0x19;
    dev->pci_conf_sb[0][0x70] = 0x12;

    dev->pci_conf_sb[1][0x00] = 0x39; /* SiS */
    dev->pci_conf_sb[1][0x01] = 0x10;
    dev->pci_conf_sb[1][0x02] = 0x13; /* 5513 */
    dev->pci_conf_sb[1][0x03] = 0x55;
    dev->pci_conf_sb[1][0x08] = 0xd0;
    dev->pci_conf_sb[0][0x09] = 0x80;
    dev->pci_conf_sb[1][0x0a] = 1;
    dev->pci_conf_sb[1][0x0b] = 1;

    dev->pci_conf_sb[2][0x00] = 0x39; /* SiS */
    dev->pci_conf_sb[2][0x01] = 0x10;
    dev->pci_conf_sb[2][0x02] = 1; /* 7710 */
    dev->pci_conf_sb[2][0x03] = 0x70;
    dev->pci_conf_sb[2][0x06] = 2;
    dev->pci_conf_sb[2][0x07] = 0x80;
    dev->pci_conf_sb[2][0x08] = 0xe0;
    dev->pci_conf_sb[2][0x09] = 0x10;
    dev->pci_conf_sb[2][0x0a] = 3;
    dev->pci_conf_sb[2][0x0b] = 0x0c;
    dev->pci_conf_sb[2][0x0e] = 0x10;
    dev->pci_conf_sb[2][0x3d] = 1;
}

static void
sis_5598_reset(void *priv)
{
    sis_5598_t *dev = (sis_5598_t *)priv;

    /* Program defaults */
    sis_5598_defaults(dev);

    /* Set up ACPI */
    acpi_set_slot(dev->acpi, dev->sb_device_id);
    acpi_set_nvr(dev->acpi, dev->nvr);

    /* Set up IDE */
    sff_set_slot(dev->ide_drive[0], dev->sb_device_id);
    sff_set_slot(dev->ide_drive[1], dev->sb_device_id);
    sff_bus_master_reset(dev->ide_drive[0], BUS_MASTER_BASE);
    sff_bus_master_reset(dev->ide_drive[1], BUS_MASTER_BASE + 8);
}

static void
sis_5598_close(void *priv)
{
    sis_5598_t *dev = (sis_5598_t *)priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
sis_5598_init(const device_t *info)
{
    sis_5598_t *dev = (sis_5598_t *)malloc(sizeof(sis_5598_t));
    memset(dev, 0, sizeof(sis_5598_t));
    dev->nb_device_id = pci_add_card(PCI_ADD_NORTHBRIDGE, sis_5597_read, sis_5597_write, dev); /* Device 0: SiS 5597 */
    dev->sb_device_id = pci_add_card(PCI_ADD_SOUTHBRIDGE, sis_5598_read, sis_5598_write, dev); /* Device 1: SiS 5598 */

    /* ACPI */
    dev->acpi = device_add(&acpi_sis_device);
    dev->nvr = device_add(&at_nvr_device);

    /* DDMA */
    dev->ddma = device_add(&ddma_device);

    /* RAM Bank Programming */
    sis_5598_dimm_programming(dev);

    /* SFF IDE */
    dev->ide_drive[0] = device_add_inst(&sff8038i_device, 1);
    dev->ide_drive[1] = device_add_inst(&sff8038i_device, 2);

    /* SMRAM */
    dev->smram = smram_add();

    /* Port 92 */
    dev->port_92 = device_add(&port_92_pci_device);

    /* USB */
    dev->usb = device_add(&usb_device);

    sis_5598_reset(dev);

    return dev;
}

const device_t sis_5598_device = {
    "SiS 5597/5598",
    DEVICE_PCI,
    0,
    sis_5598_init,
    sis_5598_close,
    sis_5598_reset,
    {NULL},
    NULL,
    NULL,
    NULL};
