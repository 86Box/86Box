/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the VLSI SuperCore and Wildcat chipsets.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          win2kgamer
 *
 *          Copyright 2020-2025 Miran Grca.
 *          Copyright 2025 win2kgamer
 */

#ifdef ENABLE_VL82C59X_LOG
#include <stdarg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
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

#ifdef ENABLE_VL82C59X_LOG
int vl82c59x_do_log = ENABLE_VL82C59X_LOG;

static void
vl82c59x_log(void *priv, const char *fmt, ...)
{
    if (vl82c59x_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define vl82c59x_log(fmt, ...)
#endif

typedef struct vl82c59x_t {
    uint8_t    nb_slot;
    uint8_t    sb_slot;
    uint8_t    type;
    uint8_t    is_compaq;

    uint8_t    pci_conf[256];
    uint8_t    pci_conf_sb[256];

    uint16_t   pmio;
    uint8_t    pmio_set;
    uint8_t    pmreg;

    smram_t   *smram[4];
    port_92_t *port_92;
    nvr_t     *nvr;

    void *    log;  /* New logging system */
} vl82c59x_t;

static int
vl82c59x_shflags(uint8_t access)
{
    int ret = MEM_READ_EXTANY | MEM_WRITE_EXTANY;

    switch (access) {
        default:
        case 0x00:
            ret = MEM_READ_EXTANY | MEM_WRITE_EXTANY;
            break;
        case 0x01:
            ret = MEM_READ_EXTANY | MEM_WRITE_INTERNAL;
            break;
        case 0x02:
            ret = MEM_READ_INTERNAL | MEM_WRITE_EXTANY;
            break;
        case 0x03:
            ret = MEM_READ_INTERNAL | MEM_WRITE_INTERNAL;
            break;
    }

    return ret;
}

static void
vl82c59x_recalc(vl82c59x_t *dev)
{
    uint32_t base;
    uint8_t  access;

    shadowbios       = 0;
    shadowbios_write = 0;

    for (uint8_t i = 0; i < 4; i++) {
        for (uint8_t j = 0; j < 8; j += 2) {
            base   = 0x000c0000 + (i << 16) + (j << 13);
            access = (dev->pci_conf[0x66 + i] >> j) & 3;
            mem_set_mem_state_both(base, 0x4000, vl82c59x_shflags(access));
            shadowbios |= ((base >= 0xe0000) && (access & 0x02));
            shadowbios_write |= ((base >= 0xe0000) && (access & 0x01));
        }
    }

    flushmmucache();
}

static void
vl82c59x_smram(vl82c59x_t *dev)
{
    smram_disable_all();

    /* A/B region SMRAM seems to not be controlled by 591 reg 0x7C/SMRAM enable */
    /* Dell Dimension BIOS breaks if A0000 region is controlled by SMRAM enable */
    if (dev->pci_conf[0x64] & 0x55) {
        smram_enable(dev->smram[0], 0x000a0000, 0x000a0000, 0x10000, dev->pci_conf[0x64] & 0xAA, dev->pci_conf[0x64] & 0x55);
    }
    if (dev->pci_conf[0x65] & 0x55) {
        smram_enable(dev->smram[1], 0x000b0000, 0x000b0000, 0x10000, dev->pci_conf[0x65] & 0xAA, dev->pci_conf[0x65] & 0x55);
    }

    /* Handle E region SMRAM */
    if (dev->pci_conf[0x7C] & 0x80) {
        if (dev->pci_conf[0x68] & 0x05) {
            smram_enable(dev->smram[2], 0x000e0000, 0x000e0000, 0x8000, dev->pci_conf[0x68] & 0x0A, dev->pci_conf[0x68] & 0x05);
        }
        if (dev->pci_conf[0x68] & 0x50) {
            smram_enable(dev->smram[3], 0x000e8000, 0x000e8000, 0x8000, dev->pci_conf[0x68] & 0xA0, dev->pci_conf[0x68] & 0x50);
        }
    }

    flushmmucache();
}

static void
vl82c59x_pm_write(uint16_t addr, uint8_t val, void *priv)
{
    vl82c59x_t *dev = (vl82c59x_t *) priv;

    vl82c59x_log(dev->log, "VL82c593 SMI I/O: [W] (%04X) = %02X\n", addr, val);

    /* Verify SMI Global Enable and Software SMI Enable are set */
    if ((dev->pci_conf_sb[0x6D] & 0x80) && (dev->pci_conf_sb[0x60] & 0x80)) {
        dev->pci_conf_sb[0x61] = 0x80;
        dev->pmreg = val;
        smi_raise();
    }

}

static uint8_t
vl82c59x_pm_read(uint16_t addr, void *priv)
{
    vl82c59x_t *dev = (vl82c59x_t *) priv;
    uint8_t ret = 0x00;

    ret = dev->pmreg;
    vl82c59x_log(dev->log, "VL82c593 SMI I/O: [R] (%04X) = %02X\n", addr, ret);

    return ret;
}

static void
vl82c59x_set_pm_io(void *priv)
{
    vl82c59x_t *dev = (vl82c59x_t *) priv;
    uint8_t highbyte = dev->pci_conf_sb[0x62];
    uint8_t lowbyte = dev->pci_conf_sb[0x63];

    /* Check for existing I/O mapping and remove it */
    if (dev->pmio_set == 1) {
        vl82c59x_log(dev->log, "VL82c59x: Removing SMI IO handler for %04X\n", dev->pmio);
        io_removehandler(dev->pmio, 0x0001, vl82c59x_pm_read, NULL, NULL, vl82c59x_pm_write, NULL, NULL, dev);
        dev->pmio_set = 0;
    }

    if ((highbyte != 0x00) | (lowbyte != 0x00)) {
        dev->pmio = ((highbyte << 8) + lowbyte);
        vl82c59x_log(dev->log, "VL82c59x: Adding SMI IO handler for %04X\n", dev->pmio);
        io_sethandler(dev->pmio, 0x0001, vl82c59x_pm_read, NULL, NULL, vl82c59x_pm_write, NULL, NULL, dev);
        dev->pmio_set = 1;
    }

}

static void
vl82c59x_write(int func, int addr, uint8_t val, void *priv)
{
    vl82c59x_t *dev = (vl82c59x_t *) priv;

    vl82c59x_log(dev->log, "[%04X:%08X] VL82c591: [W] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, val);

    if (func == 0x00)
        switch (addr) {
            case 0x04:
            case 0x05: /* PCI Command Register */
                dev->pci_conf[addr] = val;
                break;
            case 0x54: /* Cache Control Register 1 */
                dev->pci_conf[addr] = val;
                cpu_cache_ext_enabled = (val & 0xc0);
                cpu_update_waitstates();
                break;
            case 0x55: /* Cache Control Register 2 */
                dev->pci_conf[addr] = val;
                cpu_cache_int_enabled = (val & 0x40);
                cpu_update_waitstates();
                break;
            case 0x58: /* RAMCFG0 */
            case 0x59: /* RAMCFG1 */
                dev->pci_conf[addr] = val;
                break;
            case 0x5A: /* Wildcat EDO RAM control */
                if (dev->type == 0x01) {
                    dev->pci_conf[addr] = val;
                }
                break;
            case 0x5C: /* RAMCTL0 */
            case 0x5D: /* RAMCTL1 */
            case 0x5E: /* RAMCTL2 */
            case 0x5F:
            case 0x60:
            case 0x62:
                /* Apricot XEN-PC Ruby/Jade BIOS requires bit 2 to be set or */
                /* CMOS setup hangs on subsequent runs after NVRAM is initialized */
                dev->pci_conf[addr] = val;
                break;
            case 0x64: /* A-B SMRAM regs */
            case 0x65:
                dev->pci_conf[addr] = val;
                vl82c59x_smram(dev);
                break;
            case 0x66: /* Shadow RAM */
            case 0x67:
            case 0x68:
            case 0x69:
                dev->pci_conf[addr] = val;
                vl82c59x_recalc(dev);
                vl82c59x_smram(dev);
                break;
            case 0x6C: /* L2 Cacheability registers */
            case 0x6D:
            case 0x6E:
            case 0x6F:
            case 0x70:
            case 0x71:
            case 0x74: /* Suspected PMRA registers */
            case 0x75:
            case 0x76:
            case 0x78:
            case 0x79:
            case 0x7A:
                dev->pci_conf[addr] = val;
                break;
            case 0x7C: /* MISCSSET, bit 7 is SMRAM enable (for the E region) */
                /* io.c logging shows BIOSes setting Bit 7 here */
                dev->pci_conf[addr] = val;
                vl82c59x_smram(dev);
                break;
            case 0x7D: /* Unknown but seems Wildcat-specific, Zeos and PB600 BIOSes hang if bit 3 is writable */
                if (dev->type == 0x01) {
                    dev->pci_conf[addr] = val & 0xf7;
                }
                break;
            default:
                if (addr > 0x3F)
                    vl82c59x_log(dev->log, "VL82c591: Unknown reg [W] (%02X, %02X) = %02X\n", func, addr, val);
                break;
    }

}

static uint8_t
vl82c59x_read(int func, int addr, void *priv)
{
    const vl82c59x_t *dev = (vl82c59x_t *) priv;
    uint8_t             ret = 0xff;

    if (func == 0x00) {
        switch (addr) {
            default:
                ret = dev->pci_conf[addr];
                break;
        }
    }

    vl82c59x_log(dev->log, "[%04X:%08X] VL82c591: [R] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, ret);

    return ret;
}

static void
vl82c59x_sb_write(int func, int addr, uint8_t val, void *priv)
{
    vl82c59x_t *dev = (vl82c59x_t *) priv;
    uint8_t       irq;
    const uint8_t irq_array[8] = { 3, 5, 9, 10, 11, 12, 14, 15 };

    vl82c59x_log(dev->log, "[%04X:%08X] VL82c593: [W] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, val);

    if (func == 0x00)
        switch (addr) {
            case 0x04:
            case 0x05: /* PCI Command Register */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x50: /* MISCSETC */
            case 0x51: /* MISCSETB */
            case 0x52: /* MISCSETA */
            case 0x53:
            case 0x54:
            case 0x55:
            case 0x56:
            case 0x57:
            case 0x58:
            case 0x59:
            case 0x5A:
                /* Has at least one GPIO bit. Compaq Presario 700/900 586 BIOS */
                /* uses bit 2 as an output to set the onboard ES688's base I/O */
                /* address. Bit 2 cleared = 220, bit 2 set = 240 */
            case 0x5C: /* Interrupt Assertion Level Register */
            case 0x5D:
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x60: /* SMI Enable Register */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x61: /* SMI Status Register */
                dev->pci_conf_sb[addr] = 0x00;
                break;
            case 0x62: /* SMI I/O port high byte */
            case 0x63: /* SMI I/O port low byte */
                dev->pci_conf_sb[addr] = val;
                vl82c59x_set_pm_io(dev);
                break;
            case 0x64: /* System Event Enable Register 1 */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x65: /* System Event Status Register 1 */
                dev->pci_conf_sb[addr] = 0x00;
                break;
            case 0x66: /* System Event Enable Register 2 */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x67: /* System Event Status Register 2 */
                dev->pci_conf_sb[addr] = 0x00;
                break;
            case 0x68: /* System Event Enable Register 3 */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x69: /* System Event Status Register 3 */
                dev->pci_conf_sb[addr] = 0x00;
                break;
            case 0x6A: /* PCI Activity Control Register */
                dev->pci_conf_sb[addr] = val & 0x0f; /* Top 4 bits are Read/Clear */
                break;
            case 0x6B: /* Programmable I/O Range Register High Byte */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x6C: /* Programmable I/O Range Register Low Byte */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x6D: /* System Event Control Register/SMI Global Enable */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x6E:
            case 0x6F:
            case 0x70:
            case 0x71:
            case 0x72: /* GPIO */
                /* Compaq Presario and Prolinea use bits 6-4 for setting ECP DMA */
                /* 011 (0x03) = DMA 3 (Default) */
                /* 100 (0x04) = DMA 0 */
                /* 111 (0x07) = DMA disabled */
            case 0x73: /* GPIO */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x74: /* PCI Interrupt Connection Register (PCIINT0/1) */
                dev->pci_conf_sb[addr] = val;
                irq = irq_array[val & 0x07];
                pci_set_irq_routing(PCI_INTA, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                irq = irq_array[(val & 0x70) >> 4];
                pci_set_irq_routing(PCI_INTB, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                break;
            case 0x75: /* PCI Interrupt Connection Register (PCIINT2/3) */
                dev->pci_conf_sb[addr] = val;
                irq = irq_array[val & 0x07];
                pci_set_irq_routing(PCI_INTC, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                irq = irq_array[(val & 0x70) >> 4];
                pci_set_irq_routing(PCI_INTD, (irq != 0) ? irq : PCI_IRQ_DISABLED);
                break;
            case 0x76: /* PCI Interrupt Connection Register (ISA/PCIINT) */
                dev->pci_conf_sb[addr] = val;
                break;
            case 0x77:
            case 0x78:
                dev->pci_conf_sb[addr] = val;
                break;
            default:
                if (addr > 0x3F)
                    vl82c59x_log(dev->log, "VL82c593: Unknown reg [W] (%02X, %02X) = %02X\n", func, addr, val);
                break;
    }

}

static uint8_t
vl82c59x_sb_read(int func, int addr, void *priv)
{
    const vl82c59x_t *dev = (vl82c59x_t *) priv;
    uint8_t             ret = 0xff;

    if (func == 0x00)
        switch (addr) {
            case 0x69: /* Lower two bits are a CPU speed readout per Compaq's Prolinea E series TRG */
                /* Per the Prolinea TRG bits 5/3/1 of 593 reg 0x73 must be set to 1 to read the jumpers */
                if (dev->is_compaq && (dev->pci_conf_sb[0x73] & 0x2A)) {
                    /* Set bit 2 to 1 as this is required for the Prolinea E to be properly identified
                       in Compaq Computer Setup. */
                    ret = (dev->pci_conf_sb[addr] | 0x04);
                    if (cpu_busspeed <= 50000000)
                        ret = (ret & 0xfd); /* 50MHz: Bit 1 = 0 */
                    else
                        ret = (ret | 0x02); /* 60MHz: Bit 1 = 1 */

                    if (cpu_dmulti <= 1.5)
                        ret = (ret | 0x01); /* 1.5x mult: Bit 0 = 1 */
                    else
                        ret = (ret & 0xfe); /* 2.0x mult: Bit 0 = 0 */
                } else {
                    ret = dev->pci_conf_sb[addr];
                }
                break;
            default:
                ret = dev->pci_conf_sb[addr];
                break;
    }

    vl82c59x_log(dev->log, "[%04X:%08X] VL82c593: [R] (%02X, %02X) = %02X\n", CS, cpu_state.pc, func, addr, ret);

    return ret;

}

static void
vl82c59x_reset(void *priv)
{
    vl82c59x_t *dev = (vl82c59x_t *) priv;

    /* Northbridge (VLSI VL82c591) */
    dev->pci_conf[0x00] = 0x04;
    dev->pci_conf[0x01] = 0x10;
    switch (dev->type) {
        case 0: /* SuperCore */
            dev->pci_conf[0x02] = 0x05;
            dev->pci_conf[0x03] = 0x00;
            break;
        case 1: /* Wildcat */
            dev->pci_conf[0x02] = 0x07;
            dev->pci_conf[0x03] = 0x00;
            break;
    }
    dev->pci_conf[0x08] = 0x00;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;

    /* Southbridge (VLSI VL82c593) */
    dev->pci_conf_sb[0x00] = 0x04;
    dev->pci_conf_sb[0x01] = 0x10;
    switch (dev->type) {
        case 0: /* SuperCore */
            dev->pci_conf_sb[0x02] = 0x06;
            dev->pci_conf_sb[0x03] = 0x00;
            break;
        case 1: /* Wildcat */
            dev->pci_conf_sb[0x02] = 0x08;
            dev->pci_conf_sb[0x03] = 0x00;
            break;
    }
    dev->pci_conf_sb[0x08] = 0x00;
    dev->pci_conf_sb[0x09] = 0x00;
    dev->pci_conf_sb[0x0a] = 0x01;
    dev->pci_conf_sb[0x0b] = 0x06;

    /* Unsure on which register configures this (if any), per Compaq's
     * Pentium-based Presario 700/900 Series and Prolinea E Series Desktop
     * Technical Reference Guides the ISA bus runs at 8MHz while the
     * Zeos Pantera Wildcat user manual says that the ISA bus runs at
     * 7.5MHz on 90MHz (60MHz bus) systems and 8.25MHz on 100MHz (66MHz bus)
     * systems.
     */
    if (cpu_busspeed > 50000000)
        cpu_set_isa_pci_div(4);
    else
        cpu_set_isa_pci_div(3);

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    vl82c59x_smram(dev);

    /* Reset SMI IO port */
    dev->pmio = 0x0000;
    dev->pmio_set = 0;

    cpu_cache_int_enabled = 1;
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();
}

static void
vl82c59x_close(void *priv)
{
    vl82c59x_t *dev = (vl82c59x_t *) priv;

    smram_del(dev->smram[0]);
    smram_del(dev->smram[1]);
    smram_del(dev->smram[2]);
    smram_del(dev->smram[3]);

    if (dev->log != NULL) {
        log_close(dev->log);
        dev->log = NULL;
    }

    free(dev);
}

static void *
vl82c59x_init(UNUSED(const device_t *info))
{
    vl82c59x_t *dev = (vl82c59x_t *) calloc(1, sizeof(vl82c59x_t));

    dev->type = (info->local & 0x0f);

    dev->is_compaq = (info->local >> 4);

    dev->log = log_open("VL82c59x");

    /* VL82c591 (Northbridge) */
    pci_add_card(PCI_ADD_NORTHBRIDGE, vl82c59x_read, vl82c59x_write, dev, &dev->nb_slot);

    /* VL82c593 (Southbridge) */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, vl82c59x_sb_read, vl82c59x_sb_write, dev, &dev->sb_slot);

    dev->port_92 = device_add(&port_92_device);

    /* NVR */
    dev->nvr = device_add(&at_nvr_device);

    dev->smram[0] = smram_add();
    dev->smram[1] = smram_add();
    dev->smram[2] = smram_add();
    dev->smram[3] = smram_add();

    vl82c59x_reset(dev);

    return dev;
}

const device_t vl82c59x_device = {
    .name          = "VLSI VL82c59x (SuperCore)",
    .internal_name = "vl82c59x",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = vl82c59x_init,
    .close         = vl82c59x_close,
    .reset         = vl82c59x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t vl82c59x_compaq_device = {
    .name          = "VLSI VL82c59x (SuperCore with Compaq readout)",
    .internal_name = "vl82c59x_compaq",
    .flags         = DEVICE_PCI,
    .local         = 0x10,
    .init          = vl82c59x_init,
    .close         = vl82c59x_close,
    .reset         = vl82c59x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t vl82c59x_wildcat_device = {
    .name          = "VLSI VL82c59x (Wildcat)",
    .internal_name = "vl82c59x_wildcat",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = vl82c59x_init,
    .close         = vl82c59x_close,
    .reset         = vl82c59x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t vl82c59x_wildcat_compaq_device = {
    .name          = "VLSI VL82c59x (Wildcat with Compaq readout)",
    .internal_name = "vl82c59x_wildcat_compaq",
    .flags         = DEVICE_PCI,
    .local         = 0x11,
    .init          = vl82c59x_init,
    .close         = vl82c59x_close,
    .reset         = vl82c59x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
