/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Utron UT85C501/UT85C502 chipset emulation module
 *
 * Authors: Umut Çağan Uçanok (cfwlr), <ucucanok@outlook.com.tr>
 *
 *          Copyright 2026 Umut Çağan Uçanok.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef ENABLE_UT85C50X_LOG
#include <stdarg.h>
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
#include <86box/log.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>

#ifdef ENABLE_UT85C50X_LOG
int ut85c50x_do_log = ENABLE_UT85C50X_LOG;

static void
ut85c50x_log(void *priv, const char *fmt, ...)
{
    if (ut85c50x_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ut85c50x_log(fmt, ...)
#endif

typedef struct ut85c50x_t {
    uint8_t    slot;

    uint8_t    pci_conf[3][256];
    uint8_t    drb_temp[4];

    smram_t   *smram;
    port_92_t *port_92;

    void *     log;
} ut85c50x_t;

static void
ut85c50x_drb_recalc(ut85c50x_t *dev)
{
    spd_write_drbs(dev->drb_temp, 0x00, 0x03, 16);

    dev->pci_conf[0][0xe0]  = (dev->drb_temp[0] & 0x0f) | ((dev->drb_temp[1] & 0x0f) << 4);
    dev->pci_conf[0][0xe1]  = (dev->drb_temp[2] & 0x0f) | ((dev->drb_temp[3] & 0x0f) << 4);
    dev->pci_conf[0][0xe2]  = (dev->pci_conf[0][0xe2] & 0xf8) | (!!(dev->drb_temp[1] & 0x10)) | (!!(dev->drb_temp[2] & 0x10) << 1) | (!!(dev->drb_temp[3] & 0x10) << 2);
}

static void
ut85c50x_shadow_recalc(ut85c50x_t *dev)
{
    uint32_t can_read;
    uint32_t can_write;
    uint8_t  hi;
    uint8_t  lo;

    for (uint8_t i = 0; i < 3; i++) {
        lo = dev->pci_conf[0][0xe4 + i] & 0x0f;
        hi = dev->pci_conf[0][0xe4 + i] >> 4;

        switch (i) {
            case 0:
            case 1:
                can_read = (lo & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                can_write = (lo & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(0x000c0000 + (i << 16), 0x8000, can_read | can_write);

                can_read = (hi & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                can_write = (hi & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(0x000c8000 + (i << 16), 0x8000, can_read | can_write);
                break;
            
            case 2:
                can_read = (lo & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                can_write = (lo & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(0x000e0000, 0x10000, can_read | can_write);

                can_read = (hi & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
                can_write = (hi & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
                mem_set_mem_state_both(0x000f0000, 0x10000, can_read | can_write);
                break;
        }
    }

    flushmmucache_nopc();
}

static void
ut85c50x_smram_recalc(ut85c50x_t *dev)
{
    smram_disable_all();

    int in_smm_wr  = !!(dev->pci_conf[0][0xe2] & 0x80);
    int in_smm_rd  = !!(dev->pci_conf[0][0xe2] & 0x40);
    int in_smm_all = in_smm_wr || in_smm_rd;
    int in_normal  = in_smm_all && (dev->pci_conf[0][0xe2] & 0x20);
    int en_flags   = (in_smm_wr ? MEM_WRITE_SMRAM : MEM_WRITE_EXTANY) | (in_smm_rd ? MEM_READ_SMRAM : MEM_READ_EXTANY);
    int dis_flags  = MEM_WRITE_EXTANY | MEM_READ_EXTANY;

    smram_enable(dev->smram, 0x000a0000, 0x000a0000, 0x00020000, in_normal, in_smm_all);

    mem_set_mem_state(0x000a0000, 0x00020000, in_normal ? en_flags : dis_flags);
    mem_set_mem_state_smm(0x000a0000, 0x00020000, in_smm_all ? en_flags : dis_flags);
}

static void
ut85c50x_ide_handler(ut85c50x_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();

    if (dev->pci_conf[2][0x04] & 0x04) {
        if (dev->pci_conf[1][0x61] & 0x01)
            ide_pri_enable();

        if (dev->pci_conf[1][0x61] & 0x02)
            ide_sec_enable();
    }
}

static void
ut85c50x_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    ut85c50x_t   *dev = (ut85c50x_t *) priv;
    uint8_t       irq;
    uint8_t       valxor;
    const uint8_t irq_array[16] = { 0, 0, 0, 3, 4, 5, 6, 7, 0, 9, 10, 11, 12, 0, 14, 15 };

    ut85c50x_log(dev->log, "[%04X:%08X] UT85C50x: [W] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, val);

    if (func == 0x00) /* Northbridge Function */
        switch (addr) {
            case 0x00 ... 0x03: /* VID - Vendor Identification Register & DID - Device Identification Register */
                /* These registers are actually writable according to the datasheet, unless function 1, register 6B, bit 1 is set */
                if (!!(dev->pci_conf[1][0x6b] & 0x02))
                    dev->pci_conf[func][addr] = val;
                break;
            case 0x04: /* COM - Command Register */
                dev->pci_conf[func][addr] = (dev->pci_conf[func][addr] & 0x07) | (val & 0xf8);
                break;
            case 0x05:
                dev->pci_conf[func][addr] = val & 0x03;
                break;
            case 0x07: /* DS - Device Status Register */
                dev->pci_conf[func][addr] = val & 0xf0;
                break;
            case 0x0d: /* Latency Timer */
                dev->pci_conf[func][addr] = val & 0xf0;
                break;
            
            case 0xc0: /* CPU-PCI Control Register */
                dev->pci_conf[func][addr] = val;
                break;
            case 0xc1: /* DRAM Refresh Register */
                dev->pci_conf[func][addr] = val;
                break;
            case 0xc4 ... 0xc6:
                /* According to the datasheet these registers are "not used in standard motherboard" */
                dev->pci_conf[func][addr] = val;
                break;

            case 0xd0: /* Cache Control Register */
                dev->pci_conf[func][addr] = val;
                cpu_cache_ext_enabled = val & 0x01;
                cpu_update_waitstates();
                break;

            case 0xd1: /* DRAM Timing Control #1 */
                dev->pci_conf[func][addr] = val;
                break;
            case 0xd2: /* DRAM Timing Control #2 */
                dev->pci_conf[func][addr] = val & 0x7f;
                break;
            case 0xd3:
                dev->pci_conf[func][addr] = val & 0xbf;
                break;
            case 0xd4:
            case 0xe0 ... 0xe1:
                dev->pci_conf[func][addr] = val;
                ut85c50x_drb_recalc(dev);
                break;
            case 0xe2:
                valxor = (dev->pci_conf[func][addr] ^ val) & 0xe0;
                dev->pci_conf[func][addr] = val & 0xef;
                ut85c50x_drb_recalc(dev);
                if (valxor)
                    ut85c50x_smram_recalc(dev);
                break;
            case 0xe3: /* Video Memory Start Address */
                dev->pci_conf[func][addr] = val;
                break;

            case 0xe4 ... 0xe6: /* Shadow Control */
                dev->pci_conf[func][addr] = val;
                ut85c50x_shadow_recalc(dev);
                break;

            case 0xe7: /* Scratch Register */
                dev->pci_conf[func][addr] = val;
                break;
            
            default:
                if (addr > 0x3f)
                    ut85c50x_log(dev->log, "UT85C50x: Invalid or unknown reg [W] (%02X, %02X) = %02X\n", func, addr, val);
                break;
        }
    else if (func == 0x01) /* Southbridge Function */
        switch (addr) {
            case 0x00 ... 0x03: /* VID - Vendor Identification Register & DID - Device Identification Register */
                /* These registers are actually writable according to the datasheet, unless function 1, register 6B, bit 1 is set */
                if (!!(dev->pci_conf[1][0x6b] & 0x02))
                    dev->pci_conf[func][addr] = val;
                break;
            case 0x04: /* COM - Command Register */
                dev->pci_conf[func][addr] = (dev->pci_conf[func][addr] & 0x07) | (val & 0xf8);
                break;
            case 0x05:
                dev->pci_conf[func][addr] = val & 0x03;
                break;
            case 0x07: /* DS - Device Status Register */
                dev->pci_conf[func][addr] = val & 0xf0;
                break;
            case 0x0d: /* Latency Timer */
                dev->pci_conf[func][addr] = val & 0xf0;
                break;

            case 0x60: /* PCI Timing Control / Miscellaneous */
                dev->pci_conf[func][addr] = val;
                break;
            case 0x61:
                dev->pci_conf[func][addr] = val;
                ut85c50x_ide_handler(dev);
                if (val & 0x40)
                    cpu_set_isa_pci_div(3);
                else
                    cpu_set_isa_pci_div(4);
                break;
            case 0x65 ... 0x67: /* IDE 16-Bit IOR / 16-Bit IOW / 8-Bit Timing */
                dev->pci_conf[func][addr] = val;
                break;
            case 0x68: /* Priority Control */
            case 0x69: /* Arbiter Control */
                dev->pci_conf[func][addr] = val;
                break;
            case 0x6a: /* CPU Request Control */
                dev->pci_conf[func][addr] = val & 0x0f;
                break;
            case 0x6b: /* Miscellaneous */
                dev->pci_conf[func][addr] = val & 0x07;
                break;
            case 0x6c: /* INTSTR - Interrupt Routing Register */
                dev->pci_conf[func][addr] = val;
                irq = irq_array[val & 0x0f];
                pci_set_irq_routing(PCI_INTA, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                irq = irq_array[(val & 0xf0) >> 4];
                pci_set_irq_routing(PCI_INTB, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                break;
            case 0x6d:
                dev->pci_conf[func][addr] = val;
                irq = irq_array[val & 0x0f];
                pci_set_irq_routing(PCI_INTC, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                irq = irq_array[(val & 0xf0) >> 4];
                pci_set_irq_routing(PCI_INTD, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                break;

            case 0x80: /* Idle Detector Control */
                dev->pci_conf[func][addr] = (dev->pci_conf[func][0x83] & 0x80) ? (dev->pci_conf[func][addr] & 0x7f) | (val & 0x80) : val;
                break;
            case 0x81: /* Activity Mask */
                dev->pci_conf[func][addr] = val;
                break;
            case 0x82:
                dev->pci_conf[func][addr] = val & 0xfb;
                break;
            case 0x83: /* Control Register */ 
                dev->pci_conf[func][addr] = (dev->pci_conf[func][0x83] & 0x80) ? (dev->pci_conf[func][addr] & 0x7f) | (val & 0x80) : val;
                break;
            case 0x84 ... 0x86: /* 24-bit SMI Timer Test */
                dev->pci_conf[func][addr] = val;
                break;
            case 0x87: /* Status Clear and SMI Timer Test */
                /* This one acts differently based on whether if it's read or written to... needs investigation */
                dev->pci_conf[func][addr] = val & 0xc3;
                break;
            
            default:
                if (addr > 0x3f)
                    ut85c50x_log(dev->log, "UT85C50x: Invalid or unknown reg [W] (%02X, %02X) = %02X\n", func, addr, val);
                break;
        }
    else if (func == 0x02) /* IDE Function */
        /* Seemingly has no device-specific regs */
        switch (addr) {
            case 0x00 ... 0x03: /* VID - Vendor Identification Register & DID - Device Identification Register */
                /* These registers are actually writable according to the datasheet, unless function 1, register 6B, bit 1 is set */
                if (!!(dev->pci_conf[1][0x6b] & 0x02))
                    dev->pci_conf[func][addr] = val;
                break;
            case 0x04: /* COM - Command Register */
                dev->pci_conf[func][addr] = (dev->pci_conf[func][addr] & 0x07) | (val & 0xf8);
                break;
            case 0x05:
                dev->pci_conf[func][addr] = val & 0x03;
                break;
            case 0x07: /* DS - Device Status Register */
                dev->pci_conf[func][addr] = val & 0xf0;
                break;
            case 0x0d: /* Latency Timer */
                dev->pci_conf[func][addr] = val & 0xf0;
                break;

            default:
                if (addr > 0x3f)
                    ut85c50x_log(dev->log, "UT85C50x: Invalid or unknown reg [W] (%02X, %02X) = %02X\n", func, addr, val);
                break;
        }   
}

static uint8_t
ut85c50x_read(int func, int addr, UNUSED(int len), void *priv)
{
    const ut85c50x_t *dev = (ut85c50x_t *) priv;
    uint8_t           ret = 0xff;

    if (func == 0x00 || func == 0x01 || func == 0x02)
        ret = dev->pci_conf[func][addr];

    ut85c50x_log(dev->log, "[%04X:%08X] UT85C50x: [R] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, ret);

    return ret;
}

static void
ut85c50x_reset(void *priv)
{
    ut85c50x_t *dev = (ut85c50x_t *) priv;

    for (uint8_t i = 0; i < 3; i++) {
        /* VID - Vendor Identification Register */
        dev->pci_conf[i][0x00] = 0x88;
        dev->pci_conf[i][0x01] = 0x33;

        /* DID - Device Identification Register */
        dev->pci_conf[i][0x02] = 0x10 | (i + 1);
        dev->pci_conf[i][0x03] = 0x80;

        /* COM - Command Register */
        dev->pci_conf[i][0x04] = 0x07;

        /* DS - Device Status Register */
        dev->pci_conf[i][0x07] = 0x00;

        /* RID - Revision Identification Register */
        dev->pci_conf[i][0x08] = 0x00;

        /* Class Code Register */
        switch (i) {
            case 0:
                dev->pci_conf[0][0x09] = 0x00;
                dev->pci_conf[0][0x0a] = 0x00;
                dev->pci_conf[0][0x0b] = 0x06;
                break;
            case 1:
                dev->pci_conf[1][0x09] = 0x00;
                dev->pci_conf[1][0x0a] = 0x01;
                dev->pci_conf[1][0x0b] = 0x06;
                break;
            case 2:
                dev->pci_conf[2][0x09] = 0x00;
                dev->pci_conf[2][0x0a] = 0x01;
                dev->pci_conf[2][0x0b] = 0x01;
                break;
        }
    }

    dev->pci_conf[0][0x0e] = 0x80; /* Flag as multi-function */

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
}

static void
ut85c50x_close(void *priv)
{
    ut85c50x_t *dev = (ut85c50x_t *) priv;

    if (dev->log != NULL) {
        log_close(dev->log);
        dev->log = NULL;
    }

    smram_del(dev->smram);

    free(dev);
}

static void *
ut85c50x_init(UNUSED(const device_t *info))
{
    ut85c50x_t *dev = (ut85c50x_t *) calloc(1, sizeof(ut85c50x_t));

    dev->log = log_open("UT85C50x");

    dev->smram = smram_add();

    pci_add_card(PCI_ADD_NORTHBRIDGE, ut85c50x_read, ut85c50x_write, dev, &dev->slot);

    dev->port_92 = device_add(&port_92_device);
    device_add(&ide_pci_2ch_device);
    device_add_params(&kbc_at_device, (void *) (KBC_VEN_AMI | 0x00004600)); /* Built-in KBC, this one acts as if it's AMI 'F' */

    ut85c50x_reset(dev);

    return dev;
}

const device_t ut85c50x_device = {
    .name          = "Utron UT85C50x",
    .internal_name = "ut85c50x",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = ut85c50x_init,
    .close         = ut85c50x_close,
    .reset         = ut85c50x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
